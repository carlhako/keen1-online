# Design

## Context

After the first two changes, the game is deterministic, its state can be snapshotted and
checksummed, and all game-changing actions (movement, votes, pauses) travel as per-frame input
bits. The web build runs the C game under ASYNCIFY: blocking loops yield to the browser in
`plat_vsync()` / `plat_delay()`. Data is preloaded at `/data`, and the page waits for a click
before `callMain`.

`locomotive-wars` (`/home/carl/projects/locomotivewars`) is the hosting model: a Node 22 TypeScript
server serving the client and `/ws` on one port (8080), `config.json`, vitest tests, and a TLS
reverse proxy in front. Its netcode is server-authoritative with snapshots. Keen's game logic is C
in the browser, so this change keeps the hosting model but uses lockstep.

## Goals / Non-Goals

**Goals:**
- Two browsers play one game in step, with lag up to about 80 ms one way hidden by the input delay.
- The server never runs the game, but can still bring a joiner, a returning player or a drifted
  browser to the exact current state.
- Server tests run without a browser; end-to-end checks use two browser tabs.

**Non-Goals:**
- Rollback or prediction. Agreed: a little lag is fine for this game.
- Accounts or player names. Players are P1 and P2.
- Saves, high scores and the installer: `server-saves-deploy`.

## Decisions

### The server seals frames
Each browser sends `input{frame, bits}` for frames ahead of the one it is simulating. The server
collects inputs per frame, and once it has one from every seated player it broadcasts
`frame{n, inputs[], events[]}`. Browsers simulate only sealed frames, in order. The server inserts
system events (add player, remove player, start, end) into the next frame it seals, so every
browser applies them on the same frame without negotiation.
- Alternative: peers send inputs to each other and agree on event frames among themselves.
  Rejected: it needs a protocol to pick the frame for joins and leaves, which sealing gives for
  free.
- Cost: a player's own input also makes a round trip through the server. With a lead of 2 frames
  (about 82 ms) this is invisible at normal RTTs.

### Input lead is each browser's own choice
How many frames ahead a browser sends its input is local: the default is 2, raised when the
measured round trip needs it (`ceil(rtt / 41 ms) + 1`, capped at 6). The server does not care how
far ahead an input is, up to 120 frames.

### Checkpoints let the server catch anyone up
Every 240 frames (about 10 s), and at every level start, the browser of the longest-connected player
uploads a snapshot (a few tens of KB, binary WebSocket frame) for a frame whose checksum all
players agreed on. The server keeps the latest checkpoint and every sealed frame after it. To bring
a browser up to date (join, rejoin, resync, empty game rejoined), the server sends checkpoint plus
frames. The browser restores and fast-forwards without drawing or sound until it reaches the live
frame. No browser is ever asked to pause while someone joins.

### Desync handling
Browsers send `checksum{frame, sum}` every 24 frames. A mismatch makes the server request an
immediate snapshot from the longest-connected player and catch the other browser up from it. Each
resync is logged with the game's frame log, so the drift can be replayed natively with
`--replay` to find the cause.

### Game and seat lifecycle on the server
Server state is a `Game` with `id`, `seats[2]` (connection, rejoin token, last input time),
`phase` (`waiting`, `playing`, `paused`, `empty`), `location` (map or level n, read from frame
events), checkpoint and frame log. The rules from the specs map onto it as follows:
- **Play single player** or the second join starts the game: the server picks a seed and seals
  frame 0 with `start`.
- A seat that closes or sends nothing for 15 s is removed: phase becomes `paused` and the server
  stops sealing. **Continue single player** seals a `remove player` event and resumes.
- A join or rejoin seals an `add player` event. The new Keen is placed on top of the
  lowest-numbered remaining Keen (in the C game, not on the server).
- An empty game is kept 60 s. A rejoin catches up from the checkpoint and frames.
- After the ending, the browsers send `finished`. The server ends the game and they reload to the lobby.
- `maxGames` counts every game in any phase until it is removed.

### Rejoin token
On joining a seat the server issues a random token, which the browser keeps in `localStorage` with
the game id. The lobby marks that game Rejoin while the token's seat is open. Anyone may still take
an open seat, as the user asked.

### Browser side: lobby in the page, game in wasm
The lobby is plain HTML/JS in `web/shell.html`, talking to `/ws`. Choosing New game, Join or Rejoin
starts the wasm game with `callMain(['--online'])`. The game skips the title loop. A small JS
bridge (`web/net.js`) queues incoming messages, which the C side drains each frame through
`EM_JS` calls, and passes snapshots through the wasm heap. Going back to the lobby reloads the
page, which also resets the wasm runtime.

### Server package
`server/` is a standalone Node 22 TypeScript package (`ws`, vitest), shaped like
`locomotive-wars/packages/server`. It serves `build/web/` statically and `/ws`, and reads
`config.json` (`port` 8080, `maxGames` 5, `inputLeadMax` 6, `disconnectSec` 15, `emptyGameSec` 60,
`checkpointFrames` 240). The build id compared in `hello` is a hash of the wasm file, computed at
build time and injected into the page and the server.

## Risks / Trade-offs

- **[Hidden tabs]** Browsers throttle background timers, so a hidden tab sends inputs about once a
  second and the partner sees "Waiting for player 2". → Accepted and spelled out in the specs. After
  15 s without input, the seat is dropped and the player can rejoin.
- **[Fast-forward cost]** Catching up 240 frames headless must be quick in wasm. → Game frames
  are tiny. If it is slow, the checkpoint interval comes down.
- **[Reverse proxy and WebSockets]** The proxy must pass `Upgrade` headers and keep idle sockets open.
  → The server sends a ping every 10 s. The deploy notes in `server-saves-deploy` show the proxy
  settings.
- **[Trusting browsers]** Lockstep trusts each browser's game. → It's a co-op game with no
  rankings worth cheating for except high scores, which `server-saves-deploy` treats as unverified.

## Migration Plan

New service, no existing data. Rollback means stopping the service.

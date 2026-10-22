# keen1-online server

A small Node 22 server for the online co-op game. It serves the web build (`build/web`) and a
WebSocket at `/ws` on one port, keeps the lobby (the list of running games), and relays the players'
inputs so that every browser in a game runs exactly the same game.

The server never runs the game. The game is the C program in `src/`, compiled to WebAssembly and run
by each browser. The server only decides _when_ things happen, by numbering frames, and keeps enough
state to bring a newcomer, a returning player or a browser that drifted up to date.

## Running it

```sh
cd server
npm install
npm run build        # TypeScript to dist/
npm start            # http://localhost:8080
npm test             # unit tests (no browser needed)
```

`make web` in the repository root builds the page and game first. The server looks for the web build
in `../web` (a release) or `../build/web` (a checkout); `KEEN_WEB_DIR` overrides it.

Saves and the high score table are files under `dataDir` (see [Saves and the high score table](#saves-and-the-high-score-table)).

The 31 Keen 1 shareware files the page needs live in `dataDir/game/` (the installer puts them there;
the server logs `NO GAME DATA` at start when they are missing). The server lists them at `data/index.json`
and serves each at `data/<NAME>` (only names from its fixed list, `src/gamefiles.ts`), and all of them as
one gzip-compressed tar at `data/game.tar.gz` once the set is complete (built in memory, rebuilt when a
file changes). The page fetches the archive before the game starts, and the files one by one if that
fails; some web filters block the single binary files but let the archive through.

`/healthz` answers `{"status":"ok","games":N,"players":N,"buildId":"...","gameData":"ok","version":"..."}`;
`gameData` is `missing` until all the game files are in place, and `version` is the release's
`package.json` version, which the installer checks after an upgrade.

The server works under any URL path prefix: `/keen/`, `/keen/index.js`, `/keen/healthz`, `/keen/data/...`
and the WebSocket at `/keen/ws` are answered like their unprefixed forms, and `/keen` redirects to
`/keen/`. A path is matched by its shortest tail that names something the server serves; a path without
an extension that names nothing is taken for the prefix and redirected to its slash form, and everything
else is a 404.

### Configuration

`config.json` in the directory the server is started from (or the file named by `KEEN_CONFIG`) may set
these keys; every key is optional. `config.example.json` lists the defaults. `PORT` in the
environment overrides `port`. An unknown key is reported and ignored, a bad value stops the server.

| Key                | Default | Meaning                                                                                        |
| ------------------ | ------- | ---------------------------------------------------------------------------------------------- |
| `port`             | 8080    | TCP port for the page and `/ws`.                                                               |
| `maxGames`         | 5       | Games that may exist at once, in any phase, until they are removed.                            |
| `inputLeadMax`     | 6       | The most frames ahead a browser may schedule its input (it is told in `welcome`).              |
| `disconnectSec`    | 15      | Seconds the game waits for a player's input before dropping that player.                       |
| `emptyGameSec`     | 60      | Seconds a game with nobody in it is kept, joinable, before it is removed.                      |
| `checkpointFrames` | 240     | Frames between checkpoints (a multiple of 24, the checksum interval).                          |
| `dataDir`          | "data"  | Directory for saves and the high score table. A relative path starts at the working directory. |

### Debugging: the frame log

`KEEN_FRAMELOG=dir npm start` writes every game's sealed frames to `dir/game-N.netlog`, together
with the checksums the browsers reported. `keen1 --replay dir/game-N.netlog` plays such a log natively
(headless), and `--netsums FILE` writes the checksums the native run reaches, which should equal the
reported ones. This is how a desync is chased down. See the README in the repository root for the
other `keen1` options.

## The lockstep model

The game is deterministic: from the same state and the same inputs it always reaches the same next
state, on any machine. So the browsers only need to exchange inputs.

- A frame is one step of the game, about 41 ms (24.3 a second). Frames are numbered from 0 across the
  world map and every level of a game; the number never resets.
- Each browser sends its player's input for a frame _before_ it simulates that frame: `lead` frames
  ahead, 2 normally (about 82 ms), more when the measured round trip needs it (`ceil(rtt / 41 ms) + 1`,
  at most `inputLeadMax`). The lead is each browser's own choice.
- The server **seals** frame _n_ when it has an input for it from every seat that takes part, and
  broadcasts `frame` with every seat's input to everybody. A browser simulates only sealed frames, in
  order, so no browser is ever ahead of the others by more than its lead.
- The server decides what else happens on a frame and says so inside the sealed frame, as `events`:
  the game starting (`start`), a player's Keen being added (`add`) or removed (`remove`). Every browser
  applies them on the same frame, with no negotiation.
- An input is a number: the direction (0 to 8, 8 = none) in bits 0 to 3, the two buttons in bits 4 and
  5, and the one-frame events (votes, pauses) in bits 6 to 10. A seat without a player has input 8.
- Every 24 frames each browser reports a checksum of its state. If the reports for a frame disagree,
  the browser that has been in the game longest is asked for its state (`snap`), which replaces the
  state of the browsers that differ.
- The browser that has been in the game longest (`duty`) also uploads a **checkpoint**, a snapshot of
  the whole game, every `checkpointFrames` frames and at the start of every level. The server keeps
  the latest one that all players agree on, and every frame sealed after it. A browser that joins,
  rejoins or is repaired gets the checkpoint and the frames since (`restore`), restores the snapshot
  and fast-forwards through the frames without drawing or sound until it is level with the others.
  Nobody else is paused for this.

### Frame numbers and checksums

`sum` for frame _f_ is the checksum of the state _before_ frame _f_ is simulated (so the sum for
frame 0 is the freshly started game). Browsers report at every multiple of 24 and at the first frame
of each map phase or level.

## Messages

JSON text frames, `{"t": "<type>", ...}`, and two binary frames. The protocol version is 1; the page
and the server must also have the same build id, a hash of the game's wasm file (`build-id.txt` next
to the page, `window.KEEN_BUILD_ID` in it), or the server answers `reject` with reason `version` and
closes the connection.

### Browser to server

| Type       | Fields                   | Meaning                                                                                                                    |
| ---------- | ------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| `hello`    | `v`, `build`             | First message. Protocol version and build id.                                                                              |
| `create`   |                          | Start a new game; the sender is player 1 and waits for player 2.                                                           |
| `load`     | `save`                   | Start a new game from the save with that id (the sender is player 1 and waits for player 2).                               |
| `save`     | `name`, `rec`            | Store the shared game record (`rec`, 92 bytes in base64) under `name` (1 to 20 printable characters). Answered by `saved`. |
| `score`    | `name`, `score`, `parts` | The team name for the high score table, from the seat `ended` named. `parts` is 0 to 15.                                   |
| `join`     | `game`, `token?`         | Take an open seat in a game. With the `token` of a seat the sender had before, that seat is taken if it is open.           |
| `single`   |                          | The waiting player chooses Play single player: the game starts with one Keen.                                              |
| `leave`    |                          | Leave the game (the Esc menu): the seat opens, as for a disconnect.                                                        |
| `input`    | `f`, `b`                 | The sender's input `b` for frame `f`.                                                                                      |
| `sum`      | `f`, `c`, `l`, `s?`      | Checksum `c` of the state before frame `f`; `l` is where the game is (0 map, n level n); `s` is the shared score.          |
| `ready`    | `f`                      | A joining browser has restored its snapshot and caught up; it is about to simulate frame `f`.                              |
| `continue` |                          | Continue single player, after the other player left.                                                                       |
| `ending`   |                          | The ending cinematic started: no inputs come until `finished`.                                                             |
| `finished` |                          | The ending is over. When every player has said so the game moves on to the high score table (`ended`).                     |
| `ping`     | `c`                      | Answered by `pong` with the same `c`; the browser measures its round trip with it.                                         |

Binary, browser to server: `[kind u8][frame u32 le][checksum u32 le][snapshot ...]`. Kind 1 is a
**checkpoint** for `frame` (it counts once every player reported the same checksum for that frame).
Kind 2 is the **resync** snapshot the server asked for with `snap`.

### Server to browser

| Type      | Fields                               | Meaning                                                                                                                                                                                                                                                          |
| --------- | ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `welcome` | `v`, `cfg`                           | The handshake succeeded. `cfg` has `checkpointFrames`, `inputLeadMax` and `disconnectSec`.                                                                                                                                                                       |
| `reject`  | `reason`, `count?`, `max?`           | A request failed: `version` (the connection is closed), `full` (`maxGames` reached), `nogame`, `closed` (both seats taken, or the game is over), `busy` (already in a game), `nosave` (no save with that id), `invalid` (a high score entry was refused), `bad`. |
| `lobby`   | `games`, `count`, `max`              | The running games, sent to everybody in the lobby whenever they change. Each game has `id`, `seats` (players in it), `open` (seats that can be taken), `phase` and `loc`.                                                                                        |
| `joined`  | `game`, `seat`, `token`, `phase`     | The sender is in a game. The `token` is for rejoining.                                                                                                                                                                                                           |
| `start`   | `seed`, `mask`, `rec?`               | The game starts: the random seed and which seats have a Keen. A game loaded from a save carries its record in `rec` (base64).                                                                                                                                    |
| `saves`   | `saves`                              | The saves on the server, newest first, each with `id`, `name`, `date`, `levels` (completed) and `score`. Sent to everybody in the lobby whenever they change.                                                                                                    |
| `saved`   | `ok`, `id?`                          | Whether the sender's `save` was stored.                                                                                                                                                                                                                          |
| `scores`  | `rows`                               | The high score table: 7 rows of `score`, `name` and `parts`. Sent to everybody when they connect and whenever it changes.                                                                                                                                        |
| `frame`   | `n`, `in`, `ev?`                     | Sealed frame `n`: the input of seat 0 and 1, and the events on it.                                                                                                                                                                                               |
| `restore` | `f`, `fresh`, `seed`, `mask`, `rec?` | Catch up from frame `f`: a binary snapshot follows (unless `fresh`: start from the seed and `rec`, the record of a loaded game), then the sealed frames from `f` on.                                                                                             |
| `golive`  | `from`                               | The joining browser's Keen appears on frame `from`; it must send inputs for `from` and later.                                                                                                                                                                    |
| `snap`    |                                      | Send me your state now: another browser drifted.                                                                                                                                                                                                                 |
| `duty`    | `on`                                 | Whether this browser uploads checkpoints.                                                                                                                                                                                                                        |
| `paused`  | `seats`                              | The listed seats left; the game waits for `continue` or for somebody to join.                                                                                                                                                                                    |
| `resumed` |                                      | The game runs again.                                                                                                                                                                                                                                             |
| `ended`   | `entry`                              | The ending is over. `entry` is the seat to type the team name, or -1 when the table is final (or no name is wanted): show it, then `leave`. Sent again with -1 once a name is stored.                                                                            |
| `left`    |                                      | The sender is out of its game (it left, or was dropped) and back in the lobby.                                                                                                                                                                                   |
| `pong`    | `c`                                  | Answer to `ping`.                                                                                                                                                                                                                                                |

Binary, server to browser: `[1][frame u32 le][snapshot ...]`, the snapshot of a `restore`.

Events in `ev` are `{"t":"start","seed","mask"}`, `{"t":"add","seat"}` and `{"t":"remove","seat"}`,
applied in the order listed, before the frame's inputs. A joining player's Keen is placed on top of the
lowest-numbered Keen that is alive.

## Games, seats and what happens when somebody leaves

A game has two seats and a phase: `waiting` (player 1 alone, before it starts), `playing`, `paused`
(a player left and the other has not chosen Continue yet), `empty` (kept for `emptyGameSec`) or
`scoring` (the ending is over; see below).

- A seat is dropped when its connection closes, when its player leaves, or when the game has waited for
  its input for `disconnectSec` seconds. A game that is waiting for a slow player is not dropped for
  that: only the seat whose input is missing is. The server sends `paused` to the player who is left
  and stops sealing frames.
- `continue` seals a `remove` event for the Keen that was left behind and the game goes on with one
  player. A player who joins instead gets an `add` event (and a `remove` first if the seat still had
  a Keen), and the game resumes with both.
- A joining browser first takes the seat (`joined`, and the lobby says 2/2), then receives `restore`,
  fast-forwards, and sends `ready`. The server answers `golive` with the frame the Keen appears on
  (a few frames ahead while others play, at once when the game was paused).
- An input is dropped when it is malformed, for a frame already sealed or more than 120 frames ahead,
  or beyond 60 input messages a second from one connection. A connection that keeps sending malformed
  messages is closed.
- The server pings every connection every 10 seconds, which also keeps the connection open behind a
  reverse proxy.

## Saves and the high score table

Both live under `dataDir` and survive restarts. Nothing is ever deleted by the server.

### Saves

- `save` is accepted from a player in a running game while it is on the world map, never in a level or
  while waiting. The record is checked against the original's ranges (92 bytes; the ship parts, keys and
  completed-level flags 0 or 1; lives, ammo, score and positions in range), and the name must be 1 to 20
  printable characters. Otherwise the answer is `saved` with `ok` false and nothing is stored.
- Each save is a file `saves/<id>.json` in `dataDir`: `id`, `name`, `date`, `levels`, `score` and the
  record in base64. Ids count up from 1. The list is read when the server starts and kept in memory, so
  copying a file in or deleting one by hand needs a restart. A file that cannot be read is skipped with a
  log line.
- `load` creates a game like `create` does (the same `maxGames` limit, the same waiting screen) whose
  `start` message, and the `restore` of a player who joins later, carry the record.

### High scores

- `highscores.json` in `dataDir` holds 7 rows, written to a temporary file and renamed. It starts as the
  original's table (seven times 100 points, Yorpy, Gargile and Zzapp!). A row is inserted above the first
  lower score, as in the original, and the last row falls off.
- Every `sum` may carry the shared score `s`; the game reports it once more just before the ending.
- When every player in a game has sent `finished`, the game is `scoring`. If the latest reported score is
  above the lowest row, `ended` names the lowest-numbered player still in the game, who types the team
  name and answers with `score`. The server accepts it only from that seat, in that phase, with a score
  no higher than the one reported and a name of 1 to 12 printable characters; otherwise it answers
  `reject` with `invalid` and the entry stays open. A name that is not typed within two minutes, or whose
  typist leaves, is given up on.
- Once the table is final everybody in the game gets `scores` and `ended` with `entry` -1, shows the table
  and sends `leave`. A game in this phase cannot be joined, and goes when its last player leaves.
- None of this proves the score is honest: the server cannot play the game. It only keeps a client from
  entering a score without finishing a game, or one higher than the game said it had.

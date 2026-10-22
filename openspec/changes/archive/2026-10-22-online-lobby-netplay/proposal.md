# Proposal

## Why

The co-op game (`multiplayer-engine-core`, `coop-game-rules`) is deterministic and runs from
per-frame inputs, but both players still sit at one keyboard. To play from two browsers, players
need a server to find each other through, a way to exchange inputs so both games stay identical,
and a way to survive a partner leaving or a newcomer joining mid-game.

## What Changes

- A Node 22 server, hosted in a Proxmox LXC behind a TLS reverse proxy like `locomotive-wars`, that
  serves the web build and a WebSocket at `/ws` on one port.
- The site opens on a lobby listing running games: players, and whether each is on the world map or
  in a level. At most 5 games run at once (configurable).
- Starting a new game shows "waiting for player 2" with a **Play single player** button. Anyone can
  join a game with an open seat.
- Lockstep netcode: the server relays each player's per-frame inputs to the other with a short
  input delay, hands out the random seed, compares checksums, and resyncs a game that drifted.
- Joining mid-game: the joining browser receives a snapshot of the running game and its Keen
  appears on top of the partner's.
- When a partner disconnects, the game pauses with a **Continue single player** option, and the
  open seat can be filled by the returning player or anyone from the lobby.
- A game is kept for 60 seconds after its last player leaves, then removed.

## Capabilities

### New Capabilities

- `game-lobby`: the list of running games, creating, joining and the running-game limit.
- `lockstep-netcode`: the browser-server protocol, input relay and delay, seeding, desync detection
  and resync, and snapshot hand-off for joiners.
- `session-continuity`: what happens when a player disconnects, stalls, rejoins or leaves, and when
  a game ends.

### Modified Capabilities

None.

## Impact

- New `server/` package (Node 22, TypeScript, `ws`), with config, tests and an npm build, following
  the `locomotive-wars` layout and conventions.
- `web/` shell gains the lobby page and a WebSocket bridge between the page and the game.
- `src/` gains a network input source, add-player and remove-player events in the input stream,
  and snapshot export and import through the bridge. The original title menu is skipped in the
  online build: the lobby starts the game.
- Depends on `multiplayer-engine-core` and `coop-game-rules`. `server-saves-deploy` adds saves,
  high scores and the installer on top.

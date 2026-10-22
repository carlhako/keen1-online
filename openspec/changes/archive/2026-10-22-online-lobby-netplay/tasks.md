# Tasks

Work in `/home/carl/projects/keen1-online` after `multiplayer-engine-core` and `coop-game-rules` are
complete. Server tests run with `npm test` in `server/`. Browser checks use two tabs (or two
browsers) on `http://localhost:8080`.

## 1. Server scaffold

- [x] 1.1 Create `server/` as a Node 22 TypeScript package (`ws`, vitest, eslint/prettier like `locomotive-wars`) with `npm run build`, `npm start` and `npm test`; verify `npm test` runs an empty suite and `npm start` serves `build/web/index.html` on port 8080
- [x] 1.2 Load `config.json` (`port`, `maxGames`, `inputLeadMax`, `disconnectSec`, `emptyGameSec`, `checkpointFrames`) with defaults and validation, plus a `config.example.json`; verify tests for defaults, overrides and rejected bad values
- [x] 1.3 Compute the build id (hash of the wasm file) in `make web`, inject it into the page and expose it to the server; verify the page and server report the same id after a build

## 2. Protocol and lobby

- [x] 2.1 Implement `/ws` with `hello` / `welcome` / `reject(version)` and a 10 s ping; verify tests for a matching hello, a wrong protocol version and a wrong build id
- [x] 2.2 Implement the game list (`lobby` messages on change), `create`, `join`, `leave`, and `maxGames` with `reject(full)`; verify tests: 5 games allowed, a 6th refused, joining a game with an open seat still works at the limit, 2/2 games not joinable
- [x] 2.3 Implement waiting, Play single player, and the second join starting the game with a seeded `start` event; verify tests for both start paths and that both seats receive the same seed
- [x] 2.4 Build the lobby page in `web/shell.html` (game list with seats and location, New game, Join, Rejoin, server-full message with retry, waiting screen with Play single player); verify in the browser that two tabs see each other's games appear and update

## 3. Lockstep

- [x] 3.1 Implement frame sealing on the server (collect inputs per frame from every seat, broadcast `frame{n, inputs, events}`), input validation (bounds, 120-frame horizon, 60 msgs/s, closing repeat offenders); verify tests for sealing order, a missing input holding the frame, and each validation rule
- [x] 3.2 Add the browser bridge `web/net.js` and the C network input source: send local input at the current lead, simulate only sealed frames, show "Waiting for player n" after 0.5 s without one; verify two tabs play the same game, moving each Keen from its own tab
- [x] 3.3 Measure round trip from pings and raise the input lead up to `inputLeadMax`; verify with an artificial 150 ms delay in a test proxy that play stays smooth with a larger lead
- [x] 3.4 Report checksums every 24 frames, detect mismatches on the server and log them; verify a test that feeds mismatched checksums triggers a resync request
- [x] 3.5 Upload checkpoints from the longest-connected player every `checkpointFrames` and at level start; keep the latest plus the sealed frames after it; verify a test that the server holds exactly one checkpoint and the frames since
- [x] 3.6 Implement catch-up in the browser (restore checkpoint, fast-forward sealed frames headless and silent, then go live) and use it for resync; verify in two tabs that a deliberately corrupted state in one tab is repaired and both checksums agree afterwards
- [x] 3.7 Write the server's frame log for a game to disk on request (debug option) and check it replays natively with `--replay`; verify the native replay's checksums match those the browsers reported
- [x] 3.8 Document the protocol messages and the lockstep model in `server/README.md`; verify the documented messages match the implementation's message types

## 4. Joining, leaving and disconnects

- [x] 4.1 Seal `add player` events for joins and rejoins (C side places the new Keen on top of the lowest-numbered Keen) and catch the joiner up; verify a second tab joining a solo game mid-level appears on top of player 1 with matching checksums
- [x] 4.2 Detect closed seats and 15 s without input, pause sealing, and show "Player n disconnected" with Continue single player; verify by closing one tab mid-level
- [x] 4.3 Implement Continue single player with a `remove player` event, applying the co-op end rules when no living Keen is left; verify both the "alive remaining player" and "dead remaining player" cases
- [x] 4.4 Issue rejoin tokens, keep them in `localStorage`, and mark Rejoin in the lobby; verify a reloaded tab sees its game as Rejoin and returns as the same player number
- [x] 4.5 Keep empty games for `emptyGameSec`, joinable as 0/2, then remove them; verify tests with a fake clock and a browser check that a rejoin within 60 s resumes the game
- [x] 4.6 Add Leave game to the Esc menu, and end the game after the ending (`finished`, reload to lobby); verify both in two tabs
- [x] 4.7 Show a resumable "Waiting for player n" for a stalled tab and resume automatically; verify by hiding one tab for 5 s
- [x] 4.8 Document lobby, joining, disconnect and rejoin behaviour for players in `README.md`; verify against a two-tab session

## 5. Integration check

- [x] 5.1 Play a two-browser session from the lobby: start, play single, partner joins mid-level, complete two levels via the map handshake, partner disconnects and rejoins, both leave; verify no unrepaired desync is logged and the lobby list matches each step

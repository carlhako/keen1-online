# Developing keen1-online

keen1-online is a fork of [keen-clone](https://github.com/carlhako/keen-clone), the C/SDL2 port of
Commander Keen 1 rebuilt from the original `KEEN1.EXE` with [REA](https://github.com/morluto/rea)
and Ghidra. The same C code builds two ways:

- **The web build** (`make web`), compiled to WebAssembly with [Emscripten](https://emscripten.org).
  This is the game players get, served by the Node server in `server/`.
- **The native build** (`make`, `./keen1`), used only as the headless test and replay tool: the
  determinism tests in `tests/run.sh` and CI run it. It is not a way to play.

Neither build contains game data. To run the tests and the server locally you need the shareware
v1.31 files in `shareware/data`: if you only have the shareware installer (`1keen.zip` with
`CK1SW131.SHR`), extract it there with `tools/unshr.py`. The [operator guide](guide.md#game-data)
lists the zips that work.

## Building the web port

You need `git`, `python3` and `make`, plus the Emscripten SDK. No root access is
needed; this installs it in `~/tools/emsdk`:

```
git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
~/tools/emsdk/emsdk install latest
~/tools/emsdk/emsdk activate latest
```

Then, in each new shell, load the Emscripten environment and build:

```
source ~/tools/emsdk/emsdk_env.sh
make web
```

The first build downloads and compiles Emscripten's SDL2 port, which takes a minute;
later builds take a few seconds. The output lands in `build/web/`:

| File | Contents |
|------|----------|
| `index.html` | the page: lobby, game canvas, keyboard handling |
| `index.js` | Emscripten runtime and loader |
| `index.wasm` | the compiled game |
| `lobby-bg.png` | the picture behind the lobby (copied from `web/`) |
| `build-id.txt` | a hash of `index.wasm`, also inside the page: the server refuses a page from another build |

`make clean` removes it along with the native build.

Two more WebAssembly builds run under Node, for tests: `make node` (`build/node/keen1.js`, plays
replays headless so their checksums can be compared with the native build's) and `make nodenet`
(`build/nodenet/keen1.js`, a headless online client that plays through the same `web/net.js` as
the page; the end-to-end tests drive it).

## Running it locally

Start the server (it serves `build/web/` and the lobby's WebSocket on one port):

```
cd server && npm install && npm run build && npm start
```

or `npm run dev` in `server/` to run it from the TypeScript sources and restart on changes. The
server serves the game files from `<dataDir>/game/` (`data/game/` in the directory you start it
from); put the shareware files there first, for example
`mkdir -p data/game && cp ../shareware/data/*.CK1 ../shareware/data/KEEN1.EXE data/game/` in
`server/` (the server logs `NO GAME DATA` at start when they are missing, and `/healthz` says
`"gameData":"missing"`). Then open <http://localhost:8080> (in two browser windows, to play both
sides yourself). The server lists `build/web` when it starts, so restart it after `make web`.

## How the web port works

The game is written like the DOS original, with blocking `for(;;)` loops in the menus,
windows and game loop. Those stay as they are: the build uses Emscripten's
`-sASYNCIFY`, and `plat_vsync()` calls `emscripten_sleep()` once per frame, which
hands control back to the browser to draw the canvas and deliver keyboard events.
The game files are fetched from the server into `/data` before the game starts (`web/pre.js`);
`/save` is an IndexedDB-backed
directory that is loaded before the game starts and flushed after every write.
The browser-specific code is behind `#ifdef __EMSCRIPTEN__` in `src/`; the page and
storage setup are in `web/shell.html` and `web/pre.js`.

## The native build

You need a C compiler, `make` and the SDL2 development files (Debian/Ubuntu: `libsdl2-dev`).

```
make                       # builds ./keen1 (finds SDL2 via sdl2-config / pkg-config)
make SDL2_DIR=/some/sdl2   # or: headers in include/SDL2, library in lib/
```

The options the tests use (`./keen1 --help` lists them all):

| Option | Use |
|--------|-----|
| `--data DIR` | the directory with the game files, `shareware/data` in the tests |
| `--level N`, `--seed N` | start in level N; the game's random seed |
| `--replay F`, `--checksums F` | run a replay headless; write one state checksum per frame |
| `--record F` | write the inputs of a game to a replay file |
| `--frame-delay MS` | sleep MS per frame in a headless replay (real-time independence) |
| `--selftest F` | replay F and check snapshot/restore and checksum behaviour |
| `--local2`, `--coop` | two co-op players in one window; co-op rules with one player |
| `--netlog F` | play an online game's frame log headless (`--seat`, `--netsums`, `--net-snapshot`, `--net-restore`) |
| `--dump-objs` | list the objects when the game exits |

## Deterministic simulation

keen1-online runs the game as a deterministic simulation, so that two machines given the
same inputs play the same game. The rules the game code follows:

- **Fixed step.** Every simulated frame advances the game by 6 timer ticks (about 24.3
  frames per second, the speed the original runs at on a modern machine). Real time only
  paces drawing; a slow machine slows the game down instead of taking bigger steps.
- **No wall clock in game logic.** Nothing that affects the game reads `plat_ticks()`.
  Animation frames that pick sprites (and so hitboxes) use the game tick counter `gtick`.
  `plat_ticks()` is left to pacing, fades, sound and drawing effects, and each such use
  has a comment saying so.
- **All randomness from the game RNG.** `rand8()` and `rand_range()` draw from a state
  that is part of the game state. Every level starts it from a seed (`--seed N` sets the
  game seed; a replay carries its own).
- **State can be saved and summarised.** `sim_save()` / `sim_restore()` capture and
  restore objects, special objects, both level planes, the shared game record, the
  players with their cameras, the RNG and the frame counters. `sim_checksum()` is an
  FNV-1a hash of that snapshot, computed every frame. Think/touch function pointers are
  written as ids from a registry in `src/sim.c`; a new think function must be added there
  (the game aborts with a message if one is missing).

### Replays

A replay is a text file of per-frame inputs. It runs headless (no window or sound, as
fast as possible):

```
./keen1 --data shareware/data --replay tests/replays/two-players-levels-1-2.txt --checksums out.txt
```

`--checksums FILE` works for any run, not only replays; it writes one 8-digit hex
checksum per simulated frame. Two runs with the same replay (and seed) must produce
identical files, whatever the real-time speed: `--frame-delay 1` sleeps 1 ms per frame
to show it. `--record FILE` writes the inputs of a normal game in the same format, and
`--dump-objs` lists the objects when the game exits.

The format, one item per line (`#` starts a comment):

| Line | Meaning |
|------|---------|
| `seed N` | random seed for the levels that follow |
| `players N` | number of players (1 to 4) |
| `coop 1` | co-op rules; the replay then drives the whole game (see below) |
| `level N` | start a segment: play level N |
| `map` | start a segment: the world map, until a level starts (makes the replay drive the whole game) |
| `F P DIR B1 B2 [EV]` | from frame F of the segment, player P (1-based) holds direction DIR (0-7 clockwise from up, 8 none) and buttons B1 (jump) and B2 (pogo; both = fire) until the next line for that player. EV is a one-frame event that applies at frame F only: 1 restart vote, 2 return-to-map vote, 4 pause on, 8 pause off |
| `ammo N`, `score N`, `parts JBVW` | test aids: the level segment starts with N raygun charges, N points, or these ship parts (four 0/1 digits: joystick, battery, vacuum, whiskey) |
| `warp F P X Y` | test aid: at frame F put player P's Keen at tile X,Y |
| `tile F X Y T` | test aid: at frame F set the tile at X,Y to tile number T |
| `shot F FILE` | test aid: save the screen as a PNG after frame F |
| `view P` | test aid: the screen shows player P (default 1) |
| `end F` | the segment stops after F frames (without it, when the level or the map phase ends) |

Frames count from 0 at the start of each level or map phase. A recorded game replays to the
same checksums as the run that recorded it.

A replay with `coop 1` or a `map` segment runs the game as it runs live. Each map phase and
each level the game starts takes the next segment, so a level restarted by a failure or a vote
simply uses the next `level` segment (the game checks that the kind and level match). A replay
that starts with a `level` segment starts in that level, like `--level N`. While a replay runs
`KEEN_FIND=N` lists the tiles with behaviour N in each level, and `KEEN_FIND=tN` the tiles
with tile number N, as an aid for writing `warp` lines.

### Tests

```
make test                  # needs shareware/data and ./keen1
source ~/tools/emsdk/emsdk_env.sh && make node   # optional: the WebAssembly build for Node
```

`tests/run.sh` checks the snapshot/restore self-test (`--selftest FILE`: snapshot at
frame 300, run to 600, restore, run again and compare every frame; also that a changed
object field, tile, RNG word or tick changes the checksum), that results do not depend
on real time, that a recorded two-player game replays to the same checksums, and, if
`make node` has been run, that the WebAssembly build gives the same checksums as the
native build (`node build/node/keen1.js --data shareware/data --replay FILE ...`).

It also checks the online code natively: `--netlog FILE` plays the frame log of an
online game headless (the server writes one per game with `KEEN_FRAMELOG=dir`, and
`--replay` takes it too; `tools/replay2netlog.py` turns a co-op replay into one). It checks that a
frame log gives the same game as the replay it came from, that a second player joining
mid-game from a snapshot in a fresh process stays in step, and what happens to a level when a
player leaves.

`tests/online/` has the end-to-end tests: the real server and headless game clients (the Node
build `make nodenet` with the same `web/net.js` the page uses) playing through real
WebSockets, with a delay proxy for slow connections. `make test-online` builds `nodenet` and runs
them; the server must be built first:

```
source ~/tools/emsdk/emsdk_env.sh && (cd server && npm install && npm run build)
make test-online           # or: make nodenet && node --test tests/online/e2e.test.mjs
```

`tests/online/partner.mjs` is a bot that stays connected for manual testing, a second player
for a real browser tab (see its header).

The server has its own tests (`npm test` in `server/`, with `npm run typecheck` and
`npm run lint`), and `tests/install-truncation.sh` checks that the installer runs nothing when
its download is cut off part way through. CI (`.github/workflows/ci.yml`) runs all of these
except the end-to-end tests.

## Two players on one screen

The engine carries up to 4 Keens (`MAXPLAYERS`); the first two-player work uses 2. Each
Keen has his own input, facing, invulnerability timer, camera and world-map position.
Enemies react to the nearest living Keen, objects wake when any player's camera sees them,
hazards, pickups, doors, switches and the exit react to whichever Keen touches them (what
is collected goes into the one shared game record), Keens pass through each other and
their shots pass through each other. With more than one player, player 2's Keen is drawn
in a second colour scheme (green helmet) and each Keen carries a P1/P2 tag, in levels and
on the world map.

For development, `--local2` starts a two-player game in one window; the screen follows
player 1:

```
./keen1 --data shareware/data --local2 --level 1     # straight into level 1
./keen1 --data shareware/data --local2               # from the title screen: both Keens on the map
```

Player 1 uses the normal keys. Player 2 uses

| Key | Action |
|-----|--------|
| W, S, A, D | up, down, left, right |
| Q, E, Z, C | diagonals (up-left, up-right, down-left, down-right) |
| V | jump |
| B | pogo stick (V + B together fire the raygun) |

`--local2` is a co-op game (below), so the cheat keys are off and C and D are free to be
P2 movement keys. `--coop` starts a co-op game with one player.

The `KEEN_SCRIPT` test harness (frame-timed key events, run with
`SDL_VIDEODRIVER=dummy` for headless runs) takes an optional player number on `down` and
`up` lines. A scancode with a player number names a key of player 1's key set and is
translated to the same key of that player's set, so a script written for player 1 also
drives player 2:

```
25 down 4d        # right arrow: player 1 walks right
25 down 4d 2      # the same key for player 2 (D): player 2 walks right
50 up 4d 2
```

```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy KEEN_SCRIPT=tests/scripts/two-players-level1.txt \
    ./keen1 --data shareware/data --level 1 --seed 7 --local2 --checksums out.txt
```

## Co-op rules

Every game from the online lobby, and a native game started with `--local2` or `--coop`, plays
by co-op rules, so that one player's death or message never freezes the other's game. The
rules live in `src/coop.c` and the co-op code paths of `engine.c`, `menus.c` and `ui.c`; the
state they use (`game_t`) is part of every snapshot and checksum. Without `--local2` or
`--coop` the native build plays exactly as the original does.

**In a level**

- There are no lives and no lives counter: losing a level never ends the game and score
  never earns an extra Keen. The cheats are off.
- Each player is alive, dead or exited. A Keen whose death animation ends starts again,
  alive, at the level's start marker (with his camera); the level, its enemies and the shared
  record stay as they are. Only once somebody has exited is a death final: that Keen is dead.
  A Keen is exited when he walks through the exit door, and the others see "PLAYER N HAS
  FINISHED THE LEVEL" at the top. A dead or exited player's screen follows the
  lowest-numbered player still alive.
- Ammo is one shared counter, and a raygun pickup gives seven shots instead of five, since
  more players waste more shots.
- The level succeeds when nobody is alive and at least one player exited: it is marked done,
  what was collected is kept, and everybody returns to the map.
- The shared record (score, ammo, keycards, ship parts, pogo, completed levels) is rolled
  back to how it was when the level started whenever the level restarts by vote or is left
  by vote, so a ship part picked up in a level you restart is back in the level.
- Each player hears only what happens on his screen: every sound in a level has a source (the
  object thinking or touching, the Keen on a tile, the cannon), and `snd_muted` is set while it
  is off the local camera. His own Keen is always heard, and a Keen dying, a door opening and the
  exit door are heard by everybody (`snd_play_always`). Sound is not game state.
- Any player, alive or not, votes to restart by holding R for half a second (player 2 in
  `--local2` uses Y); doing it again withdraws the vote. When every player has voted the
  level fails and restarts (with one player, at once).
- Esc opens a menu over the game (the game goes on): M votes to return to the world map, and
  voting again withdraws. When every player has voted the level ends without being marked
  done, with the shared record rolled back, and everybody is back on the map.
- Pending votes show on every screen: "PLAYER 2 WANTS TO RESTART". Votes are cleared when a
  level starts or ends.
- The in-level teleporter and the castle switch end the level for everybody, and the Keens
  land at the teleporter's destination.
- If the shared record holds all four ship parts after a level succeeds, the ending plays
  for every player.

**Messages.** Nothing waits for a key. The message of a Yorp statue is a banner shown to
everybody for about five seconds; the castle switch shakes every screen under a banner and
the level ends 80 frames later. The status screen (Space) opens only for the player who
pressed it, over the game, and closes on his next Space or button press; while it is open
his Keen stands still. Banners are game state (every machine shows the same ones on the same
frames); overlays are local. Online, C opens a chat prompt (an overlay like the others); the
line goes to the server as a `chat` message beside the frames, comes back to every player in
the game and shows at the top for 15 seconds. Chat is not game state.

**On the world map**

- Each player walks his own Keen with his own camera; every Keen is drawn on every screen
  in its colours with its P1/P2 tag, and Keens pass through each other. The Keens start on
  the same spot.
- To play a level, press jump or pogo on its entrance (a level that is not done). That makes
  a *request*: both players see "PLAYER 1 WANTS TO PLAY LEVEL 4", and the other players get
  an arrow at the edge of their screen pointing at the requester when he is off screen. The
  level starts when every player has pressed on that same entrance while the request stands
  (with one player, at once). The request is cancelled when the requester walks off the
  entrance, and a press on a different entrance makes it that player's request instead.
- After a level succeeds or is left by vote, every Keen is back at that level's entrance
  (after the in-level teleporter, at its landing spot) and any request is gone.
- A map teleporter moves only the Keen that used it: its pad animation (16 frames at each
  end, the same tiles on the same frames as in the original) does not stop the other
  players. The ship's parts screen opens only for the player who pressed, over the map, and
  closes on his next button press; his Keen stands still while it is open.
- F1 (help) and F3 (key setup) work only on the world map: they pause everybody, and the
  others see "PLAYER 1 IS READING HELP" until the screen closes. In a level they do nothing.
  F2 toggles the sound of the player who pressed it.

How the events travel: votes and pauses are one-frame event bits (`EV_*` in `game.h`) in the
per-frame input, recorded in replays like key presses. The local client decides when to emit
them (R held for 0.5 s, the Esc menu), so the game itself applies them identically
everywhere.

## Repository layout

- `web/` the browser page (`shell.html`: the lobby and game screen), IndexedDB glue (`pre.js`)
  and the WebSocket bridge to the game (`net.js`) for `make web`.
- `server/` the online server: lobby, input relay, checkpoints, saves and high scores (Node 22,
  TypeScript); see [its README](../server/README.md).
- `deploy/` the installer for a Debian server or LXC (`install.sh`); `.github/workflows/` CI and
  releases; `make release` builds the package (see [Releasing](guide.md#releasing)).
- `docs/` this guide and the [operator guide](guide.md).
- `tests/` replays and scripts for the determinism tests (`run.sh`), the installer truncation
  test, `tests/online/` the end-to-end tests.
- `src/` the port: `engine.c` (objects, collision, Keen), `actors.c` (enemies, doors,
  bridges), `menus.c` (title, map, ending, scores), `ui.c` (windows, text, status),
  `gfx.c` (LZW graphics), `kdata.c` (data from the EXE, version detection) and
  `platform.c` (SDL2 video, keyboard, PC-speaker sound at the original 145.6 Hz), `sim.c`
  (snapshots, checksums, replays), `coop.c` (co-op rules, players joining and leaving) and
  `net.c` (online play: sealed frames, input lead, checkpoints and catch-up).
- `analysis/` reverse engineering output: `NOTES.md` (file formats, addresses,
  findings), the Ghidra decompilation of all 388 functions (`keen1_decomp.c`, cleaned
  and annotated as `keen1_clean.c` / `keen1_annot.c`), the disassembly (`code.dis`),
  the call graph and the raw REA output (`rea_evidence.tar.xz`).
- `tools/` the scripts used: LZEXE unpacker, REA/Ghidra MCP driver and dump scripts,
  decompilation cleanup, graphics dumper, shareware installer extractor, and
  `replay2netlog.py`.

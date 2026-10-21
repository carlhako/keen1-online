# keen1-online: Commander Keen 1, online co-op

keen1-online is a fork of [keen-clone](https://github.com/carlhako/keen-clone), the
native C/SDL2 port of Commander Keen 1 (Marooned on Mars). It starts from keen-clone's
web build and adds deterministic lockstep simulation, up to two Keens in one level, and
(in later changes) online co-op play in the browser. keen-clone stays the faithful
single-player port: see it for the reverse-engineering notes (`analysis/`) and the tools
that produced them (`tools/`), which are kept here for reference.

Build the browser version with `make web` (see [Web port](#web-port)). The rest of this
README is inherited from keen-clone and describes the port itself.

## About the port

A native C/SDL2 reimplementation of *Commander Keen in Invasion of the Vorticons,
Episode One: Marooned on Mars* (id Software / Apogee, 1990). It was rebuilt from the
original `KEEN1.EXE` with [REA](https://github.com/morluto/rea) and Ghidra, translating
the decompiled game logic function by function: Keen's physics, enemies, tile
behaviour, world map, menus, ending and high scores.
It builds as a native Linux program or, with Emscripten, as a web page you can
play in the browser (see [Web port](#web-port)).

**No game data is included.** The port loads the original data files (graphics,
levels, sounds, texts, and data tables read from `KEEN1.EXE`), so you need your own
copy of Keen 1. The free shareware episode (v1.31) works, as does v1.1; the version
is detected automatically.

## Building

You need a C compiler, `make` and the SDL2 development files
(Debian/Ubuntu: `sudo apt install build-essential libsdl2-dev`).

```
make                       # builds ./keen1 (finds SDL2 via sdl2-config / pkg-config)
make SDL2_DIR=/some/sdl2   # or: headers in include/SDL2, library in lib/
```

## Running

Put the Keen 1 files (`KEEN1.EXE`, `EGAHEAD.CK1`, `EGALATCH.CK1`, `EGASPRIT.CK1`,
`LEVEL*.CK1`, `SOUNDS.CK1`, ...) in a directory, then:

```
./keen1 --data /path/to/keen1      # or set $KEEN1_DATA, or run from that directory
./keen1 --fullscreen --scale 4
./keen1 --level 5                  # jump straight into a level, like KEEN1 /LEVEL05
```

The game also looks in `data/` next to the binary. High scores, saves and key
settings are written back to the data directory, as the original does.

If you only have the shareware installer (`1keen.zip` containing `CK1SW131.SHR`),
`tools/unshr.py` extracts it. `make dist` packages the binary, a `run.sh` launcher and
the data from a game zip (`make dist GAMEZIP=/path/to/keen1.zip`) into
`keen1-linux-x86_64.tar.gz`.

## Web port

The same code also runs in a web browser, compiled to WebAssembly with
[Emscripten](https://emscripten.org). The web build bundles the free shareware
episode. Online co-op needs the keen1-online server in `server/`, which serves the page
and relays the players' inputs (see [Playing online](#playing-online)).

### Building the web port

You need `git`, `python3` and `make`, plus the Emscripten SDK. No root access is
needed; this installs it in `~/tools/emsdk`:

```
git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
~/tools/emsdk/emsdk install latest
~/tools/emsdk/emsdk activate latest
```

The build packs the shareware v1.31 data from `shareware/data`. If you only have the
shareware installer (`1keen.zip` with `CK1SW131.SHR`), extract it there with
`tools/unshr.py`. To use files from somewhere else, pass `WEB_DATA=/path/to/dir`.
Only bundle the shareware episode: the registered episodes aren't free to distribute.

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
| `index.data` | the packed shareware game files |
| `build-id.txt` | a hash of `index.wasm`, also inside the page: the server refuses a page from another build |

`make clean` removes it along with the native build.

### Running it locally

Start the server (it serves `build/web/` and the lobby's WebSocket on one port):

```
cd server && npm install && npm run build && npm start
```

Then open <http://localhost:8080> (in two browser windows, to play both sides yourself).

### Playing in the browser

- The page opens on the lobby (see [Playing online](#playing-online)); choosing a game
  starts it. Clicking in the lobby is also the gesture browsers need before they allow sound.
- The keys are the same as the native version (see [Controls](#controls)). While the
  game runs, the page keeps keys for itself: arrows and Space don't scroll, Alt
  doesn't open a browser menu, F1/F3 don't open browser help or search, and F5
  saves the game rather than reloading the page.
- Some browser shortcuts can't be blocked by a page, notably Ctrl+W (close tab),
  Ctrl+T and Ctrl+N. If you press those by accident while jumping, use F3 in the game
  to move jump and pogo to other keys.
- Use the browser's own full screen (F11) for a bigger picture. The game fills the
  window at 4:3 with sharp pixels.
- High scores, key settings and saved games are stored in the browser's IndexedDB
  and survive reloads and restarts. They belong to that browser and that address:
  another browser, a private window, or a different host or port starts empty.
  Clearing the site's data in the browser settings resets them.
- When the game stops (it crashed, or the connection to the server was lost) a *Thanks for
  playing* screen is shown; *Play again* reloads the page, back to the lobby.

### Putting it on a website

The page is the lobby of an online game, so it needs the server in `server/` behind it:
a static web host is not enough. Put a reverse proxy with TLS in front of the server and
let it pass WebSocket upgrades on `/ws` (and keep idle connections open: the server pings
every 10 seconds). The whole game is about 1.5 MB.

### How the web port works

The game is written like the DOS original, with blocking `for(;;)` loops in the menus,
windows and game loop. Those stay as they are: the build uses Emscripten's
`-sASYNCIFY`, and `plat_vsync()` calls `emscripten_sleep()` once per frame, which
hands control back to the browser to draw the canvas and deliver keyboard events.
The game files are preloaded read-only as `/data`; `/save` is an IndexedDB-backed
directory that is loaded before the game starts and flushed after every write.
The browser-specific code is behind `#ifdef __EMSCRIPTEN__` in `src/`; the page and
storage setup are in `web/shell.html` and `web/pre.js`.

## Playing online

Open the page: the lobby lists the running games with how many of their 2 seats are filled
and where they are (the world map or a level). The server runs at most 5 games at once.

- **New game** starts a game with you as player 1 and shows *Waiting for player 2*. When
  somebody joins, the game starts for both of you on the world map. **Play single player**
  starts at once; the second seat stays open for anybody to join later.
- **Join** takes the open seat of a game, whether it is waiting, being played by one
  player, or paused. Your Keen appears on top of the other Keen, in the level or on the map,
  and the game goes on without stopping. A game with 2 players has no Join button.
- When the server is full, New game is disabled and says *Server full (5/5)*; **Retry** tries again. Joining games with an open seat still works.
- If your partner's connection closes (or stops sending for 15 seconds), your game pauses
  with *Player 2 disconnected* and **Continue single player**: their Keen goes, and the level
  carries on under the co-op rules (if nobody is left alive, the level fails and restarts).
  Anybody from the lobby can take the open seat; joining while you are paused resumes the game
  with both of you.
- A browser that closed or lost a game sees it marked **Rejoin** in the lobby while its seat
  is open, and rejoining puts it back as the same player. A game nobody is in is kept for
  60 seconds, listed as 0/2, and then removed.
- If your partner's tab is hidden or busy, you see *Waiting for player 2* and the game goes
  on by itself when their inputs arrive again (after 15 seconds they are dropped).
- Esc opens the menu: **L** leaves the game (the lobby shows it, your seat opens), **M** votes
  to return to the map, as in a local co-op game.
- After the ending both of you are back at the lobby, and the game is removed.

The details (messages, lockstep, checkpoints and how a joining or drifted browser catches
up) are in [server/README.md](server/README.md).

## Deterministic simulation and replays

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

`tests/run.sh` also checks the online code natively: `--netlog FILE` plays the frame log of an
online game headless (the server writes one per game with `KEEN_FRAMELOG=dir`, and
`--replay` takes it too; `tools/replay2netlog.py` turns a co-op replay into one). It checks that a
frame log gives the same game as the replay it came from, that a second player joining
mid-game from a snapshot in a fresh process stays in step, and what happens to a level when a
player leaves.

`tests/online/` has the end-to-end tests: the real server and headless game clients (the Node
build `make nodenet` with the same `web/net.js` the page uses) playing through real
WebSockets, with a delay proxy for slow connections:

```
source ~/tools/emsdk/emsdk_env.sh && make nodenet && (cd server && npm install && npm run build)
node --test tests/online/e2e.test.mjs
```

`tests/online/partner.mjs` is a bot that stays connected for manual testing, a second player
for a real browser tab (see its header).

`tests/run.sh` checks the snapshot/restore self-test (`--selftest FILE`: snapshot at
frame 300, run to 600, restore, run again and compare every frame; also that a changed
object field, tile, RNG word or tick changes the checksum), that results do not depend
on real time, that a recorded two-player game replays to the same checksums, and, if
`make node` has been run, that the WebAssembly build gives the same checksums as the
native build (`node build/node/keen1.js --data shareware/data --replay FILE ...`).

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

A game started with `--local2` or `--coop` (and later every game from the online lobby) plays
by co-op rules, so that one player's death or message never freezes the other's game. The
rules live in `src/coop.c` and the co-op code paths of `engine.c`, `menus.c` and `ui.c`; the
state they use (`game_t`) is part of every snapshot and checksum. Without `--local2` or
`--coop` the game plays exactly as the original port does.

**In a level**

- There are no lives and no lives counter: losing a level never ends the game and score
  never earns an extra Keen. The cheats are off.
- Each player is alive, dead or exited. A Keen is dead when his death animation ends and
  exited when he walks through the exit door. A dead or exited player's screen follows the
  lowest-numbered player still alive.
- The level succeeds when nobody is alive and at least one player exited: it is marked done,
  what was collected is kept, and everybody returns to the map. It fails when everybody is
  dead (nobody exited) and restarts at once.
- The shared record (score, ammo, keycards, ship parts, pogo, completed levels) is rolled
  back to how it was when the level started whenever the level fails, restarts by vote or is
  left by vote, so a ship part picked up in a level you lose is back in the level.
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
frames); overlays are local.

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

## Controls

| Key | Action |
|-----|--------|
| Arrows (or W A S D in a one-player game) | move / select |
| Ctrl | jump |
| Alt | pogo stick (once found) |
| Ctrl + Alt | fire the raygun |
| Space | status screen |
| F1 help, F2 sound, F3 redefine keys, F5 save (on the world map) | |
| Esc | quit (co-op: menu with the return-to-map vote) |
| R held for half a second | co-op: vote to restart the level |
| Alt + Enter | toggle fullscreen |

The original cheats work too: C+T+Space gives items, G+O+D toggles god mode (not in co-op).

## Repository layout

- `web/` the browser page (`shell.html`: the lobby and game screen), IndexedDB glue (`pre.js`)
  and the WebSocket bridge to the game (`net.js`) for `make web`.
- `server/` the online server: lobby, input relay and checkpoints (Node 22, TypeScript); see its README.
- `tests/` replays and scripts for the determinism tests (`run.sh`), `tests/online/` the end-to-end tests.
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
  decompilation cleanup, graphics dumper, shareware installer extractor.

Commander Keen is a trademark of id Software. This project is an unofficial fan
reimplementation and is not affiliated with id Software or Apogee.

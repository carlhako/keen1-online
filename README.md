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
episode, so the result is a self-contained folder you can serve from any web server.

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
| `index.html` | the page: start screen, game canvas, keyboard handling |
| `index.js` | Emscripten runtime and loader |
| `index.wasm` | the compiled game |
| `index.data` | the packed shareware game files |

`make clean` removes it along with the native build.

### Running it locally

Browsers won't load WebAssembly from a `file://` page, so serve the folder over HTTP:

```
python3 -m http.server 8137 --directory build/web
```

Then open <http://localhost:8137>.

### Playing in the browser

- Click the start screen, or press any key, once it says *Click or press a key to
  start*. Browsers only allow sound after you interact with the page, so the game
  waits for this.
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
- Quitting the game (Esc on the world map, then D) shows a *Thanks for playing* screen; *Play again*
  reloads the page.

### Putting it on a website

Copy the four files in `build/web/` to any static web host (a plain web server,
GitHub Pages, Netlify and so on), all in the same folder. Nothing runs on the server.
Serving `.wasm` files as `application/wasm` lets the browser compile while
downloading; with another type it still works, just a little slower to start. The
whole game is about 1.5 MB.

### How the web port works

The game is written like the DOS original, with blocking `for(;;)` loops in the menus,
windows and game loop. Those stay as they are: the build uses Emscripten's
`-sASYNCIFY`, and `plat_vsync()` calls `emscripten_sleep()` once per frame, which
hands control back to the browser to draw the canvas and deliver keyboard events.
The game files are preloaded read-only as `/data`; `/save` is an IndexedDB-backed
directory that is loaded before the game starts and flushed after every write.
The browser-specific code is behind `#ifdef __EMSCRIPTEN__` in `src/`; the page and
storage setup are in `web/shell.html` and `web/pre.js`.

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
| `level N` | start a segment: play level N |
| `F P DIR B1 B2` | from frame F of the segment, player P (1-based) holds direction DIR (0-7 clockwise from up, 8 none) and buttons B1 (jump) and B2 (pogo; both = fire) until the next line for that player |
| `ammo N` | test aid: the segment starts with N raygun charges |
| `warp F P X Y` | test aid: at frame F put player P's Keen at tile X,Y |
| `end F` | the segment stops after F frames (without it, when the level ends) |

Frames count from 0 at the start of each level. A recorded game replays to the same
checksums as the run that recorded it.

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

The cheat keys (C+T+Space, G+O+D) read the keyboard directly, so C and D double as P2
movement keys in this mode.

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

## Controls

| Key | Action |
|-----|--------|
| Arrows (or W A S D in a one-player game) | move / select |
| Ctrl | jump |
| Alt | pogo stick (once found) |
| Ctrl + Alt | fire the raygun |
| Space | status screen |
| F1 help, F2 sound, F3 redefine keys, F5 save (on the world map) | |
| Esc | quit |
| Alt + Enter | toggle fullscreen |

The original cheats work too: C+T+Space gives items, G+O+D toggles god mode.

## Repository layout

- `web/` the browser page (`shell.html`) and IndexedDB glue (`pre.js`) for `make web`.
- `src/` the port: `engine.c` (objects, collision, Keen), `actors.c` (enemies, doors,
  bridges), `menus.c` (title, map, ending, scores), `ui.c` (windows, text, status),
  `gfx.c` (LZW graphics), `kdata.c` (data from the EXE, version detection) and
  `platform.c` (SDL2 video, keyboard, PC-speaker sound at the original 145.6 Hz).
- `analysis/` reverse engineering output: `NOTES.md` (file formats, addresses,
  findings), the Ghidra decompilation of all 388 functions (`keen1_decomp.c`, cleaned
  and annotated as `keen1_clean.c` / `keen1_annot.c`), the disassembly (`code.dis`),
  the call graph and the raw REA output (`rea_evidence.tar.xz`).
- `tools/` the scripts used: LZEXE unpacker, REA/Ghidra MCP driver and dump scripts,
  decompilation cleanup, graphics dumper, shareware installer extractor.

Commander Keen is a trademark of id Software. This project is an unofficial fan
reimplementation and is not affiliated with id Software or Apogee.

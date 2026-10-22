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
[Emscripten](https://emscripten.org). The web build contains no game data: the page
fetches the free shareware episode from the server when it loads (see
[Game data](#game-data)). Online co-op needs the keen1-online server in `server/`, which serves the page
and relays the players' inputs (see [Playing online](#playing-online)).

### Building the web port

You need `git`, `python3` and `make`, plus the Emscripten SDK. No root access is
needed; this installs it in `~/tools/emsdk`:

```
git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
~/tools/emsdk/emsdk install latest
~/tools/emsdk/emsdk activate latest
```

The build does not need the game files. To run the replay tests and the native game you
need the shareware v1.31 data in `shareware/data`: if you only have the shareware installer
(`1keen.zip` with `CK1SW131.SHR`), extract it there with `tools/unshr.py`.

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
| `build-id.txt` | a hash of `index.wasm`, also inside the page: the server refuses a page from another build |

`make clean` removes it along with the native build.

### Running it locally

Start the server (it serves `build/web/` and the lobby's WebSocket on one port):

```
cd server && npm install && npm run build && npm start
```

The server serves the game files from `<dataDir>/game/` (`data/game/` in the directory you start it
from); put the shareware files there first, for example
`mkdir -p data/game && cp shareware/data/*.CK1 shareware/data/KEEN1.EXE data/game/` (the server logs
`NO GAME DATA` at start when they are missing, and `/healthz` says `"gameData":"missing"`).
Then open <http://localhost:8080> (in two browser windows, to play both sides yourself).

### Playing in the browser

- The page opens on the lobby (see [Playing online](#playing-online)); choosing a game
  starts it. Clicking in the lobby is also the gesture browsers need before they allow sound.
- The keys are the same as the native version (see [Controls](#controls)). While the
  game runs, the page keeps keys for itself: arrows and Space don't scroll, Alt
  doesn't open a browser menu, F1/F3 don't open browser help or search, and F5
  saves the game (on the world map) rather than reloading the page.
- Some browser shortcuts can't be blocked by a page, notably Ctrl+W (close tab),
  Ctrl+T and Ctrl+N. If you press those by accident while jumping, use F3 in the game
  to move jump and pogo to other keys.
- Use the browser's own full screen (F11) for a bigger picture. The game fills the
  window at 4:3 with sharp pixels.
- Key settings are stored in the browser's IndexedDB and survive reloads and restarts. They
  belong to that browser and that address: another browser, a private window, or a different
  host or port starts empty. Saved games and high scores of online games live on the server
  instead (see [Playing online](#playing-online)).
- When the game stops (it crashed, or the connection to the server was lost) a *Thanks for
  playing* screen is shown; *Play again* reloads the page, back to the lobby.

### Putting it on a website

The page is the lobby of an online game, so it needs the server in `server/` behind it:
a static web host is not enough. See [Running a server](#running-a-server) for installing it on a
Debian server or Proxmox LXC and putting a TLS reverse proxy in front. The whole game is about
1.5 MB.

### How the web port works

The game is written like the DOS original, with blocking `for(;;)` loops in the menus,
windows and game loop. Those stay as they are: the build uses Emscripten's
`-sASYNCIFY`, and `plat_vsync()` calls `emscripten_sleep()` once per frame, which
hands control back to the browser to draw the canvas and deliver keyboard events.
The game files are fetched from the server into `/data` before the game starts (`web/pre.js`);
`/save` is an IndexedDB-backed
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
- After the ending the high score table is entered and shown (see [High scores](#high-scores)),
  then both of you are back at the lobby and the game is removed.

### Saving and loading

Saved games are kept on the server, shared by everybody who plays on it.

- **F5 on the world map** asks for a name (up to 20 letters; Enter alone uses *GAME n - k LEVELS*,
  Esc cancels) and stores the shared game under it: score, items, completed levels and where the
  Keens are. Either player can save. While one of you types the name the other is paused and sees
  *PLAYER n IS SAVING*; the saver is told *Saved* or *Save failed*. F5 in a level does nothing.
- **Load game** in the lobby lists every save, newest first, with its date, completed levels and
  score. **Load** starts a *new* game from it, which counts toward the 5-game limit and waits for a
  partner (or **Play single player**) like any new game. The save itself is never changed or removed:
  it can be loaded as often as you like, and nobody's progress in a loaded game touches it.
- Saves are never deleted by the server. They are small files, one per save, in the server's data
  directory (`saves/<id>.json`, see [Where things are](#where-things-are)); delete one by hand (and
  restart the server) to remove it. There are no accounts: anybody who can open the page sees every save.

### High scores

The server keeps one high score table, the original's seven rows, for everybody on it. When the
ending is over and the score is above the lowest row, player 1 (or whoever is left, in a
one-player game) types a team name in the original's entry screen while the other player sees
*PLAYER 1 IS ENTERING YOUR TEAM NAME* (12 letters at most). Then both players see the table, and
return to the lobby. If the score is too low nobody is asked for a name and the table is shown
straight away. The lobby has a **High scores** button with the same table. The browser's own
`SCORES.CK1` is not used online. The server cannot check that a score was earned: it only refuses
entries from a game that did not reach the ending, or with a score above what that game reported.

The details (messages, lockstep, checkpoints and how a joining or drifted browser catches
up) are in [server/README.md](server/README.md).

## Running a server

### From a release

A [release](https://github.com/carlhako/keen1-online/releases) is a single package,
`keen1-online-<version>.tar.gz`, with the built server, the web build, the installer, the data
unpacker and this README. It holds no game data (see [Game data](#game-data)). It needs Node 22
(see [`.nvmrc`](.nvmrc)) and no build step:

```
tar xzf keen1-online-0.2.0.tar.gz && cd keen1-online-0.2.0/server
npm ci --omit=dev
mkdir -p data/game    # then copy the game files into it, see Game data
npm start
```

The lobby is at <http://localhost:8080/>. Saves and high scores are written to `data/` in the
directory you start it from, the game files are read from `data/game/`; the
[server README](server/README.md#configuration) lists the settings.

### On a Debian server or Proxmox LXC

[`deploy/install.sh`](deploy/install.sh) sets the game up as a service that starts on boot: it
creates a `keen1-online` user, installs Node.js and the latest release under `/opt/keen1-online`,
fetches and checks the shareware game files, writes a config, and enables the `keen1-online` systemd service. You need Debian 12 or newer with
systemd (or a Proxmox LXC built from a Debian template) on amd64 or arm64, a root shell, and
outbound access to `github.com`, `nodejs.org` and `registry.npmjs.org` (and `archive.org` for the game
files, unless you supply the zip yourself, see [Game data](#game-data)).

Install (as root; a fresh LXC lacks curl, so install it first):

```
apt-get update && apt-get install -y curl
```

```
curl -fsSL https://github.com/carlhako/keen1-online/releases/latest/download/install.sh | bash
```

It prints the address to play at, `http://<server-ip>:8080/` by default.

Upgrade by running the same command again. With nothing newer it does nothing and leaves the server
running. Otherwise it installs the new release beside the running one, restarts the service on it and
checks that `/healthz`, the page at `/` and the WebSocket at `/ws` answer. If they do not, the
previous release is put back and started again and the script stops with an error (see the log:
`journalctl -u keen1-online`). Config, game files, saves and high scores are never touched; players are
disconnected while the service restarts. To go back to an older release, or to pick one:

```
curl -fsSL https://github.com/carlhako/keen1-online/releases/latest/download/install.sh | RELEASE_TAG=v0.2.0 bash
```

Options, set in front of `bash` as above:

| Variable      | Effect                                                                          |
|---------------|---------------------------------------------------------------------------------|
| `RELEASE_TAG` | Install this release (`v0.2.0`) instead of the latest; can also go back         |
| `KO_TARBALL`  | Install from a release package already on the server instead of downloading one |
| `KO_FORCE`    | Set to `1` to reinstall and restart even when already up to date                |
| `KO_REPO`     | Take releases from another GitHub repository (`owner/name`)                     |
| `KEEN_DATA_ZIP` | Take the game files from this zip (see [Game data](#game-data))               |
| `KEEN_DATA_URL` | Download the game files zip from this address instead                         |

#### Game data

The release contains no game files. The installer puts the free Keen 1 v1.31 shareware files (31
of them) in `/var/lib/keen1-online/game/`, where the server reads them and serves them to the page.
It looks for a zip in this order and uses the first that works:

1. `KEEN_DATA_ZIP=/path/to/zip`, or a `1keen.zip`, `keen1.zip` or
   `Commander_Keen_1_-_Marooned_on_Mars_1990.zip` in the current directory or in `/root`.
2. `KEEN_DATA_URL`, when it is set.
3. The Internet Archive (`msdos_Commander_Keen_1_-_Marooned_on_Mars_1990`; a plain folder of files).
4. `https://www.commander-keen.com/downloads/keen1.zip` (Apogee's installer, unpacked with
   `tools/unshr.py`; `python3` is installed with apt only for this one).

A server with no internet access can be given the zip by hand:

```
scp 1keen.zip root@<server>:/root/
```

and then the install command is run on it as usual (or `KEEN_DATA_ZIP=/root/1keen.zip` in front of
`bash`). The two zips known to work:

| Source | Zip md5 |
|--------|---------|
| Internet Archive, `Commander_Keen_1_-_Marooned_on_Mars_1990.zip` | `fd6a12de35a7ae7b2f44a61176b99d30` |
| commander-keen.com / Apogee, `1keen.zip` (`keen1.zip`)           | `7375d0452276388d52c35d0b3ad6ab82` |

The zip itself is not what is checked: the installer compares the md5 of every game file with the list
in `install.sh`, so a zip packed another way works if it holds the same files, and a zip with a
different version (v1.1, the registered episodes) is refused, naming the file. When a check fails
nothing is installed and the running service is left alone. Run again and nothing is downloaded once
the files are there and correct. `curl http://<server>:8080/healthz` shows `"gameData":"ok"` when
the set is complete; when it is not, the page says the server has no game data installed.

Running a release by hand (not through the installer) needs the same 31 files, upper-case names, in
`<dataDir>/game/`: everything from `1keen.zip`'s `CK1SW131.SHR` (`tools/unshr.py`) except `CTLPANEL.CK1`,
`SCORES.CK1`, `ORDER.FRM` and `VENDOR.DOC`.

#### Settings

`/etc/keen1-online/config.json` is written from [`server/config.example.json`](server/config.example.json)
on the first install and left alone after that. Its keys are `port`, `maxGames`, `inputLeadMax`,
`disconnectSec`, `emptyGameSec`, `checkpointFrames` and `dataDir` (the installer sets it to
`/var/lib/keen1-online`); the [server README](server/README.md#configuration) explains each. Edit it and
`systemctl restart keen1-online`. To change only the port, put `PORT=9000` in
`/etc/keen1-online/env` and restart. A port below 1024 works although the server does not run as root.

#### Where things are

| Path                                         | What                                                    |
|----------------------------------------------|---------------------------------------------------------|
| `/opt/keen1-online/current`                  | The running release (links to `releases/<version>`)     |
| `/opt/keen1-online/node/current`             | Node.js (links to `node/v<version>`)                    |
| `/etc/keen1-online/config.json`              | Config                                                  |
| `/etc/keen1-online/env`                      | Optional environment, such as `PORT=9000`               |
| `/var/lib/keen1-online/game/`                | The shareware game files (root-owned, kept on upgrade)  |
| `/var/lib/keen1-online/saves/<id>.json`      | One file per saved game, never deleted by the server    |
| `/var/lib/keen1-online/highscores.json`      | The high score table                                    |
| `/etc/systemd/system/keen1-online.service`   | The service unit (rewritten on every run: use `systemctl edit keen1-online` for changes) |

Day to day: `systemctl status keen1-online`, `journalctl -u keen1-online -f`, and `systemctl
stop|start|restart keen1-online`. To uninstall:

```
systemctl disable --now keen1-online
rm -rf /opt/keen1-online /etc/keen1-online /etc/systemd/system/keen1-online.service
systemctl daemon-reload && userdel keen1-online
```

That keeps the saves and the high scores in `/var/lib/keen1-online`; delete that too to remove them.

### Behind a TLS reverse proxy

The page connects to `wss://<same host>/<same path>/ws` when it is opened over `https`, so a proxy
only has to forward everything to the server's port, WebSocket upgrades on `ws` included, and not close
idle connections early (the server pings every 10 seconds, which counts as traffic).

nginx:

```nginx
map $http_upgrade $connection_upgrade { default upgrade; '' close; }

server {
    listen 443 ssl;
    server_name keen.example.com;
    # ssl_certificate and ssl_certificate_key as usual

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $connection_upgrade;
        proxy_read_timeout 1h;       # the default 60 s would cut a quiet game
    }
}
```

Caddy (it forwards WebSocket upgrades by itself and gets the certificate):

```
keen.example.com {
    reverse_proxy 127.0.0.1:8080
}
```

#### Under a path such as `/keen`

The server answers on any path prefix, so a proxy that keeps the prefix needs no rewrite rule: the
page at `https://example.com/keen/`, its files, `data/`, `healthz` and the WebSocket at
`wss://example.com/keen/ws` all arrive with `/keen` in front, and `/keen` is sent on to `/keen/`.
In nginx, a trailing slash after the address in `proxy_pass` would strip the prefix, so leave it
off:

```nginx
location /keen/ {
    proxy_pass http://127.0.0.1:8080;     # no URI part: /keen/... is passed on as it is
    proxy_http_version 1.1;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection $connection_upgrade;
    proxy_read_timeout 1h;
}
```

(The `map` is the one above. A proxy that does strip the prefix works too.) In Caddy:

```
example.com {
    reverse_proxy /keen/* 127.0.0.1:8080
    redir /keen /keen/
}
```

### Proxmox notes

An LXC made for the game should start with the host: `pct set <id> --onboot 1` (the service inside
starts on boot by itself). A container to run the installer in, from the Debian 12 template:

```
pct create 120 local:vztmpl/debian-12-standard_12.7-1_amd64.tar.zst --hostname keen1 \
  --cores 1 --memory 512 --rootfs local-lvm:4 --net0 name=eth0,bridge=vmbr0,ip=dhcp \
  --unprivileged 1 --onboot 1
pct start 120
pct push 120 1keen.zip /root/1keen.zip     # optional: skips the download
pct exec 120 -- bash -c 'apt-get update && apt-get install -y curl'
pct exec 120 -- bash -c 'curl -fsSL https://github.com/carlhako/keen1-online/releases/latest/download/install.sh | bash'
```

(The template file name changes with Proxmox's template list: see `pveam available`.)

### Releasing

Releases are built by [`.github/workflows/release.yml`](.github/workflows/release.yml), not by hand.

1. Bump `version` in `server/package.json` (and `server/package-lock.json`) and add a
   `## <version> - <date>` section to [`CHANGELOG.md`](CHANGELOG.md): it becomes the release notes, and
   a tag without one is not published.
2. Commit and push. In the repository's Actions tab, run *release* by hand for a dry run: it builds
   the package exactly as a tag would and keeps `keen1-online-<version>.tar.gz` and `install.sh` as
   workflow artifacts without publishing, so the package can be tried on a test container
   (`KO_TARBALL=/path/to/keen1-online-<version>.tar.gz`, see above).
3. Tag and push: `git tag v<version> && git push origin v<version>`. The workflow checks the tag
   equals the version in `package.json`, builds the web build (Emscripten 6.0.13) and the server, and
   publishes the package and `install.sh` as release assets, which is what
   `releases/latest/download/install.sh` serves.

`make release` does the build step on a machine with Emscripten (see
[Building the web port](#building-the-web-port)). It refuses to run with uncommitted changes, so every
release matches a commit (written into the package as `COMMIT`), and it fails if the package holds
any game file (`.CK1`, `.SHR`, `KEEN1.EXE`): the game data is fetched at install time and never
redistributed. Never put the registered episodes anywhere near a release.
`.github/workflows/ci.yml` checks every push: the server's typecheck, lint and tests, and the native
build. It also runs the replay determinism tests when the repository variable `KEEN_SHAREWARE_URL` points
at a download of a zip with the game (either zip of [Game data](#game-data)).

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
| F1 help, F2 sound, F3 redefine keys, F5 save (on the world map; online: to the server) | |
| Esc | quit (co-op: menu with the return-to-map vote) |
| R held for half a second | co-op: vote to restart the level |
| Alt + Enter | toggle fullscreen |

The original cheats work too: C+T+Space gives items, G+O+D toggles god mode (not in co-op).

## Repository layout

- `web/` the browser page (`shell.html`: the lobby and game screen), IndexedDB glue (`pre.js`)
  and the WebSocket bridge to the game (`net.js`) for `make web`.
- `server/` the online server: lobby, input relay, checkpoints, saves and high scores (Node 22, TypeScript); see its README.
- `deploy/` the installer for a Debian server or LXC (`install.sh`); `.github/workflows/` CI; `make release` builds the package.
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

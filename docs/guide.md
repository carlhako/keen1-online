# Running a keen1-online server

The game is a web page and the server behind it: the server serves the page, the game files and
the lobby's WebSocket, relays the players' inputs, and keeps the saved games and the high score
table. A static web host is not enough. The whole game is about 1.5 MB.

The [README](../README.md#install) has the one-line install. This guide has the rest: the
installer's options, the game data, settings, where things are, running a release by hand, putting
it behind HTTPS, Proxmox notes, what players see on the server, and how releases are made. The
server's messages and configuration keys are in [server/README.md](../server/README.md).

## Install on a Debian server or Proxmox LXC

[`deploy/install.sh`](../deploy/install.sh) sets the game up as a service that starts on boot: it
creates a `keen1-online` user, installs Node.js and the latest release under `/opt/keen1-online`,
fetches and checks the shareware game files, writes a config, and enables the `keen1-online`
systemd service. You need Debian 12 or newer with systemd (or a Proxmox LXC built from a Debian
template) on amd64 or arm64, a root shell, and outbound access to `github.com`, `nodejs.org` and
`registry.npmjs.org` (and `archive.org` for the game files, unless you supply the zip yourself, see
[Game data](#game-data)).

Install (as root; a fresh LXC lacks curl, so install it first):

```
apt-get update && apt-get install -y curl
```

```
curl -fsSL https://github.com/carlhako/keen1-online/releases/latest/download/install.sh | bash
```

It prints the address to play at, `http://<server-ip>:8080/` by default. The script only acts
once it has been downloaded whole: a download cut off part way through runs nothing.

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

| Variable        | Effect                                                                          |
|-----------------|---------------------------------------------------------------------------------|
| `RELEASE_TAG`   | Install this release (`v0.2.0`) instead of the latest; can also go back         |
| `KO_TARBALL`    | Install from a release package already on the server instead of downloading one |
| `KO_FORCE`      | Set to `1` to reinstall and restart even when already up to date                |
| `KO_REPO`       | Take releases from another GitHub repository (`owner/name`)                     |
| `KEEN_DATA_ZIP` | Take the game files from this zip (see [Game data](#game-data))                 |
| `KEEN_DATA_URL` | Download the game files zip from this address instead                           |

## Game data

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

## Settings

`/etc/keen1-online/config.json` is written from [`server/config.example.json`](../server/config.example.json)
on the first install and left alone after that. Its keys are `port`, `maxGames`, `inputLeadMax`,
`disconnectSec`, `emptyGameSec`, `checkpointFrames` and `dataDir` (the installer sets it to
`/var/lib/keen1-online`); the [server README](../server/README.md#configuration) explains each. Edit it and
`systemctl restart keen1-online`. To change only the port, put `PORT=9000` in
`/etc/keen1-online/env` and restart. A port below 1024 works although the server does not run as root.

## Where things are

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

## Running a release by hand

A [release](https://github.com/carlhako/keen1-online/releases) is a single package,
`keen1-online-<version>.tar.gz`, with the built server, the web build, the installer, the data
unpacker, the README and these guides. It holds no game data (see [Game data](#game-data)). It
needs Node 22 (see [`.nvmrc`](../.nvmrc)) and no build step:

```
tar xzf keen1-online-0.3.1.tar.gz && cd keen1-online-0.3.1/server
npm ci --omit=dev
mkdir -p data/game    # then copy the game files into it, see Game data
npm start
```

The lobby is at <http://localhost:8080/>. Saves and high scores are written to `data/` in the
directory you start it from, the game files are read from `data/game/`; the
[server README](../server/README.md#configuration) lists the settings.

## Behind a TLS reverse proxy

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

### Under a path such as `/keen`

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

## Proxmox notes

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

## Playing on the server

What players see, in more detail than the README's [Playing](../README.md#playing).

### The lobby

Open the page: the lobby lists the running games with how many of their 2 seats are filled
and where they are (the world map or a level). The server runs at most 5 games at once
(`maxGames`).

- **New game** starts a game with you as player 1 and shows *Waiting for player 2*. When
  somebody joins, the game starts for both of you on the world map. **Play single player**
  starts at once; the second seat stays open for anybody to join later.
- **Join** takes the open seat of a game, whether it is waiting, being played by one
  player, or paused. Your Keen appears on top of the other Keen, in the level or on the map,
  and the game goes on without stopping. A game with 2 players has no Join button.
- When the server is full, New game is disabled and says *Server full (5/5)*; **Retry** tries again. Joining games with an open seat still works.
- If your partner's connection closes (or stops sending for 10 minutes), your game pauses
  with *Player 2 disconnected* and **Continue single player**: their Keen goes, and the level
  carries on under the co-op rules (a Keen that dies starts again at the beginning of the level).
  Anybody from the lobby can take the open seat; joining while you are paused resumes the game
  with both of you.
- A browser that closed or lost a game sees it marked **Rejoin** in the lobby while its seat
  is open, and rejoining puts it back as the same player. A game nobody is in is kept for
  60 seconds, listed as 0/2, and then removed.
- If your partner's tab is hidden or busy, you see *Waiting for player 2* and the game goes
  on by itself when their inputs arrive again (after 10 minutes they are dropped).
- Esc opens the menu: **L** leaves the game (the lobby shows it, your seat opens), **M** votes
  to return to the map.
- **C** opens a chat prompt at the bottom of the screen: type up to 32 characters and press
  Enter (Esc cancels). Your Keen stands still while you type; the game does not pause. The line
  shows at the top of both players' screens for 15 seconds, the newest three at a time. Chat goes
  through the server beside the game and is not saved anywhere.
- After the ending the high score table is entered and shown (see [High scores](#high-scores)),
  then both of you are back at the lobby and the game is removed.
- When the game stops (it crashed, or the connection to the server was lost) a *Thanks for
  playing* screen is shown; *Play again* reloads the page, back to the lobby.
- Key settings (F3) are stored in the browser's IndexedDB: another browser, a private window, or
  a different host or port starts with the default keys.

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
up) are in [server/README.md](../server/README.md).

## Releasing

Releases are built by [`.github/workflows/release.yml`](../.github/workflows/release.yml), not by hand.

1. Bump `version` in `server/package.json` (and `server/package-lock.json`) and add a
   `## <version> - <date>` section to [`CHANGELOG.md`](../CHANGELOG.md): it becomes the release notes, and
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
[Building the web port](development.md#building-the-web-port)). It refuses to run with uncommitted
changes, so every release matches a commit (written into the package as `COMMIT`), and it fails if
the package holds any game file (`.CK1`, `.SHR`, `KEEN1.EXE`): the game data is fetched at install
time and never redistributed. Never put the registered episodes anywhere near a release.
`.github/workflows/ci.yml` checks every push: the installer's truncation guard, the server's
typecheck, lint and tests, and the native build. It also runs the replay determinism tests when the
repository variable `KEEN_SHAREWARE_URL` points at a download of a zip with the game (either zip of
[Game data](#game-data)).

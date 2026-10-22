# keen1-online: Commander Keen 1, online co-op

*Commander Keen in Invasion of the Vorticons, Episode One: Marooned on Mars* (id Software /
Apogee, 1990), played in the browser by two players in the same game. You host it on your own
Debian server or Proxmox LXC; the server runs the lobby, relays the players' inputs, and keeps
everybody's saved games and one shared high score table. The installer fetches the free v1.31
shareware episode and checks every file, so no game data is in this repository. The game code is
the [keen-clone](https://github.com/carlhako/keen-clone) port, rebuilt from the original
`KEEN1.EXE` with REA and Ghidra, made deterministic for lockstep play.

## Install

On Debian 12 or newer, or a Proxmox LXC made from a Debian template, as root. A fresh LXC has no
curl, so install it first:

```bash
apt-get update && apt-get install -y curl
```

```bash
curl -fsSL https://github.com/carlhako/keen1-online/releases/latest/download/install.sh | bash
```

It installs Node.js, the latest release and the game files, starts the `keen1-online` service
(which starts again on boot), and prints the address to play at, `http://<server-ip>:8080/`.
Open TCP port 8080 to the players on your network.

Run the same command again to upgrade: with nothing newer it does nothing; otherwise it swaps in
the new release and puts the old one back if the new one does not start. Your settings, saves and
high scores are kept.

To serve it over HTTPS (a public server, or a path such as `/keen` on an existing site), put a
reverse proxy in front: the [operator guide](docs/guide.md#behind-a-tls-reverse-proxy) has nginx
and Caddy settings, along with the installer's options and where everything lives.

## Requirements

| | |
|---|---|
| System | Debian 12 or newer with systemd, or a Proxmox LXC from a Debian template; a root shell |
| Architecture | amd64 or arm64 |
| Memory | 512 MB is enough (the [Proxmox example](docs/guide.md#proxmox-notes) uses that) |
| Network, installing | outbound HTTPS to `github.com`, `nodejs.org`, `registry.npmjs.org` and `archive.org` |
| Network, playing | players reach TCP 8080 (or the port you set, or your proxy) |
| Game data | fetched by the installer; offline, copy `1keen.zip` to `/root` first ([Game data](docs/guide.md#game-data)) |
| Build tools | none: releases are prebuilt |
| Players | a desktop browser and a keyboard |

## Playing

- The page opens on the **lobby**: the running games, with their seats (2 per game) and where they
  are. **New game** waits for a second player; **Play single player** starts at once and leaves
  the seat open. **Join** takes the open seat of any game, even mid-level, and your Keen appears
  where your partner's is. A browser that lost its game can **Rejoin** as the same player.
- If your partner leaves or drops, the game pauses and offers **Continue single player**; anybody
  can take the seat again later.
- Co-op has no lives: a Keen that dies starts again at the beginning of the level, and the level
  goes on. It is done once one of you has walked out of the exit and the other has too (or dies
  after that); the screen tells you when your partner is out. You hear your partner's sounds only
  while they are on your screen, except deaths, doors opening and the exit door. Press **C** to
  chat: type a line and press Enter; both of you see it for 15 seconds. Hold **R** to vote to
  restart a level; **Esc** opens a menu
  where **M** votes to return to the world map and **L** leaves the game. A level starts when both
  players press jump on its entrance on the world map.
- **F5 on the world map** saves the game on the server under a name; **Load game** in the lobby
  starts a new game from any save. Everybody on the server shares the saves and one **high score**
  table, entered after the ending.
- Click in the lobby before playing: browsers allow sound only after a click. While the game runs
  the page keeps keys such as arrows, Space, Alt, F1 and F5 for itself, but Ctrl+W, Ctrl+T and
  Ctrl+N still reach the browser: use F3 to move jump and pogo to other keys if you hit those.
  Press F11 for the browser's full screen.

The [operator guide](docs/guide.md#playing-on-the-server) has the details.

## Controls

| Key | Action |
|-----|--------|
| Arrows or W A S D | move / select |
| Ctrl | jump |
| Alt | pogo stick (once found) |
| Ctrl + Alt | fire the raygun |
| Space | status screen |
| F1, F2, F3 | help, sound on/off, redefine keys (F1 and F3 on the world map only) |
| F5 | save the game (on the world map) |
| R, held half a second | vote to restart the level |
| C | chat (online): type a line, Enter sends it, Esc cancels |
| Esc | menu: return-to-map vote, leave the game |

Key settings are kept in the browser, per browser and per address.

## Documentation

- [docs/guide.md](docs/guide.md): running a server: installer options, game data, settings, where
  things are, running a release by hand, TLS reverse proxy, Proxmox notes, releasing.
- [docs/development.md](docs/development.md): building and testing: the web build, running the
  server locally, the native test build, replays, determinism and co-op rules, repository layout.
- [server/README.md](server/README.md): the server's protocol and configuration keys.
- [CHANGELOG.md](CHANGELOG.md): what changed in each release.

Commander Keen is a trademark of id Software. This project is an unofficial fan
reimplementation and is not affiliated with id Software or Apogee.

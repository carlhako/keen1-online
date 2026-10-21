# Proposal

## Why

Online games run through the server, but browser saves and high scores live only in one player's
browser, so neither fits a game two people share. The service also needs to be installable and
upgradable in a Proxmox LXC behind a reverse proxy, the way `locomotive-wars` is.

## What Changes

- F5 on the world map saves the shared game to the server under a name. Any player in a co-op game
  can save, and the game pauses for the other player while the name is typed.
- The lobby gains **Load game**: a list of every save on the server, newest first, that starts a new
  game from the chosen save, waiting for a partner or played single. Saves are never deleted
  automatically.
- A server-wide high score table replaces the browser one. After the ending, a qualifying score is
  entered under a team name typed by player 1, and the table is shown to both players before the
  lobby. The lobby shows the table too.
- `deploy/install.sh` sets the game up on a Debian LXC as a systemd service from a GitHub release,
  upgrading in place with rollback, like `locomotive-wars`.
- A release package (`make release`) bundles the built server, the web build and the shareware
  episode data. CI checks the server and the game on every push.
- Notes for putting a TLS reverse proxy in front, including WebSocket upgrade settings.

## Capabilities

### New Capabilities

- `server-saves`: saving from the world map, listing and loading saves, and how saves are stored.
- `server-highscores`: the shared high score table, entering a team name after the ending, and
  showing the table.
- `server-deployment`: release packaging, the installer, the service, its config and data paths,
  upgrades and the reverse-proxy setup.

### Modified Capabilities

None.

## Impact

- `server/`: save and high score storage and messages. `web/shell.html`: Load game list and
  high score table.
- `src/menus.c`: F5 save prompt and the high score entry go to the server in the online build
  instead of files.
- New `deploy/install.sh`, `Makefile` `release` target, `.github/workflows/ci.yml`, `.nvmrc`.
- Depends on `online-lobby-netplay`.

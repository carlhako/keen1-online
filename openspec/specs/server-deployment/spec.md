# server-deployment Specification

## Purpose

Makes the game server installable, configurable and upgradable as a service on a Debian-based
server or Proxmox LXC, reachable through a TLS reverse proxy.

## Requirements

### Requirement: Release package
Each release SHALL be a single `keen1-online-<version>.tar.gz` containing the built server, the web
build without any game data, `config.example.json`, the installer, the data unpacker and the
README. It SHALL run with Node 22 and its production dependencies, with no build step.

#### Scenario: Run a release by hand
- **WHEN** a release package is extracted, `npm ci --omit=dev` is run in `server/`, the game files
  are placed in the game data directory, and `npm start` is run
- **THEN** the lobby is served at `http://<host>:8080/` and a game can be started

#### Scenario: No game data in the package
- **WHEN** a release package is listed
- **THEN** it contains no `.CK1` files and no `KEEN1.EXE`

### Requirement: Installer sets up a service
`deploy/install.sh`, run as root on Debian 12 or newer with systemd (amd64 or arm64), SHALL create
a `keen1-online` system user, install Node.js 22 and the latest release under
`/opt/keen1-online`, put the game data in place, write `/etc/keen1-online/config.json` on first
install only, and enable a `keen1-online` systemd service that starts on boot.

#### Scenario: Fresh LXC
- **WHEN** the installer runs on a fresh Debian LXC with curl installed
- **THEN** the game data is downloaded and verified, the service is running and enabled for boot,
  and the script prints the address to play at

#### Scenario: Reboot
- **WHEN** the container or server is restarted
- **THEN** the service starts again without anyone logging in

### Requirement: Upgrade in place with rollback
Running the installer again SHALL install a newer release beside the running one and switch the
service to it, and SHALL put the previous release back and restart it if the new one does not
answer. With nothing newer it SHALL change nothing. Config, saves and high scores SHALL never be
touched by an upgrade.

#### Scenario: Bad release
- **WHEN** a new release fails to answer after starting
- **THEN** the previous release is running again and the installer exits with an error

#### Scenario: Already current
- **WHEN** the installed release is the latest
- **THEN** the installer reports it is up to date and the service is not restarted

### Requirement: Data and config locations
The service SHALL keep saves and the high score table under `/var/lib/keen1-online`, read its config
from `/etc/keen1-online/config.json`, and take an optional `PORT` from `/etc/keen1-online/env`.

#### Scenario: Port override
- **WHEN** `/etc/keen1-online/env` contains `PORT=9000` and the service restarts
- **THEN** the game answers on port 9000

### Requirement: Works behind a TLS reverse proxy
The game SHALL work when served over `https` through a reverse proxy that forwards WebSocket
upgrades, with the page connecting to `wss://` on the same host and under the same path it was
loaded from. The README SHALL show proxy settings for nginx and Caddy, with and without a path
prefix.

#### Scenario: HTTPS through the proxy
- **WHEN** a player opens `https://keen.example.com/` through the proxy
- **THEN** the lobby connects over `wss://keen.example.com/ws` and games play normally

#### Scenario: HTTPS through a proxy path
- **WHEN** a player opens `https://example.com/keen/` through a proxy that passes `/keen` on to the
  server unchanged
- **THEN** the lobby connects over `wss://example.com/keen/ws` and games play normally

### Requirement: Game data is fetched and checked by the installer
The installer SHALL place the Keen 1 v1.31 shareware files in `/var/lib/keen1-online/game/`,
taking them from a zip the operator supplied or else downloading one, and SHALL install a file
only when its md5 matches the release's list of known-good files. It SHALL not need the zip to be
laid out in any particular way.

#### Scenario: Operator supplies the zip
- **WHEN** `KEEN_DATA_ZIP=/root/1keen.zip` is set, or a known zip name is in the current directory
  or `/root`
- **THEN** the installer uses that zip and downloads nothing for the game data

#### Scenario: Download with fallback
- **WHEN** no zip was supplied and the Internet Archive download fails
- **THEN** the installer tries the commander-keen.com download, and stops with instructions for
  supplying a zip if that fails too

#### Scenario: Either zip layout
- **WHEN** the zip holds plain game files in a folder, or Apogee's installer with `CK1SW131.SHR`
- **THEN** the same set of game files ends up in the game data directory

#### Scenario: Wrong files
- **WHEN** a file fails its md5 check
- **THEN** nothing is installed from that zip, the service is left as it was, and the installer
  names the file

#### Scenario: Already installed
- **WHEN** the installer runs again and every game file is present and correct
- **THEN** it downloads nothing and leaves the game data directory untouched

### Requirement: Game data is served to the page
The server SHALL serve the files in the game data directory to the page, with a list of their
names, and SHALL report in `/healthz` whether the game data is complete. The page SHALL load
those files before the game starts.

#### Scenario: Page loads the data
- **WHEN** a browser opens the lobby with the game data in place
- **THEN** it fetches the listed files and the game starts as before

#### Scenario: Data missing
- **WHEN** the game data directory is empty
- **THEN** `/healthz` reports the data as missing and the page tells the player the server has no
  game data, instead of staying on "Loading game data"

#### Scenario: Only game files are served
- **WHEN** a request names a file that is not in the game file list, or a path outside the
  directory
- **THEN** the server answers 404

### Requirement: Works under any URL path prefix
The server SHALL answer the page, its files, `data/`, `/healthz` and the WebSocket on whatever path
prefix a request carries, so that a reverse proxy that keeps a prefix such as `/keen` needs no
rewrite rule.

#### Scenario: Prefixed page and files
- **WHEN** a request for `/keen/` or `/keen/index.js` arrives
- **THEN** the server answers with the lobby page or that file

#### Scenario: Prefix without a trailing slash
- **WHEN** a request for `/keen` arrives
- **THEN** the server redirects to `/keen/`

#### Scenario: Prefixed WebSocket
- **WHEN** a WebSocket upgrade arrives on `/keen/ws`
- **THEN** it is accepted like one on `/ws`

#### Scenario: Missing file stays missing
- **WHEN** a request for `/keen/nothing.js` arrives
- **THEN** the server answers 404

### Requirement: Tagged releases are published by a workflow
Pushing a tag `v<version>` SHALL build and publish the release package and `install.sh` as
release assets, with the release notes taken from that version's section of `CHANGELOG.md`. A tag
without a changelog section SHALL not be published.

#### Scenario: Publish a release
- **WHEN** `v0.2.0` is pushed and `CHANGELOG.md` has a `0.2.0` section
- **THEN** a GitHub release with `keen1-online-0.2.0.tar.gz` and `install.sh` appears, and
  `releases/latest/download/install.sh` serves the installer

#### Scenario: Missing notes
- **WHEN** a tag is pushed with no changelog section for it
- **THEN** the workflow fails before building and publishes nothing

#### Scenario: Dry run
- **WHEN** the workflow is started by hand
- **THEN** it builds the assets and keeps them as workflow artifacts without publishing

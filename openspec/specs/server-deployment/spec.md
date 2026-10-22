# server-deployment Specification

## Purpose

Makes the game server installable, configurable and upgradable as a service on a Debian-based
server or Proxmox LXC, reachable through a TLS reverse proxy.

## Requirements

### Requirement: Release package
Each release SHALL be a single `keen1-online-<version>.tar.gz` containing the built server, the web
build with the shareware episode data, `config.example.json`, the installer and the README. It
SHALL run with Node 22 and its production dependencies, with no build step.

#### Scenario: Run a release by hand
- **WHEN** a release package is extracted, `npm ci --omit=dev` is run in `server/`, and `npm start` is run
- **THEN** the lobby is served at `http://<host>:8080/`

### Requirement: Installer sets up a service
`deploy/install.sh`, run as root on Debian 12 or newer with systemd (amd64 or arm64), SHALL create
a `keen1-online` system user, install Node.js 22 and the latest release under
`/opt/keen1-online`, write `/etc/keen1-online/config.json` on first install only, and enable a
`keen1-online` systemd service that starts on boot.

#### Scenario: Fresh LXC
- **WHEN** the installer runs on a fresh Debian LXC with curl installed
- **THEN** the service is running and the script prints the address to play at

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
upgrades on `/ws`, with the page connecting to `wss://` on the same host. The README SHALL show
proxy settings for nginx and Caddy.

#### Scenario: HTTPS through the proxy
- **WHEN** a player opens `https://keen.example.com/` through the proxy
- **THEN** the lobby connects over `wss://keen.example.com/ws` and games play normally

# Spec Delta

## MODIFIED Requirements

### Requirement: Release package
Each release SHALL be a single `keen1-online-<version>.tar.gz` containing the built server, the web
build without any game data, `config.example.json`, the installer, the data unpacker, the README
and the guides in `docs/`. It SHALL run with Node 22 and its production dependencies, with no build
step.

#### Scenario: Run a release by hand
- **WHEN** a release package is extracted, `npm ci --omit=dev` is run in `server/`, the game files
  are placed in the game data directory, and `npm start` is run
- **THEN** the lobby is served at `http://<host>:8080/` and a game can be started

#### Scenario: No game data in the package
- **WHEN** a release package is listed
- **THEN** it contains no `.CK1` files and no `KEEN1.EXE`

#### Scenario: Guides travel with the README
- **WHEN** a release package is extracted
- **THEN** every `docs/` file the README links to is present next to it

### Requirement: Works behind a TLS reverse proxy
The game SHALL work when served over `https` through a reverse proxy that forwards WebSocket
upgrades, with the page connecting to `wss://` on the same host and under the same path it was
loaded from. The operator guide (`docs/guide.md`) SHALL show proxy settings for nginx and Caddy,
with and without a path prefix.

#### Scenario: HTTPS through the proxy
- **WHEN** a player opens `https://keen.example.com/` through the proxy
- **THEN** the lobby connects over `wss://keen.example.com/ws` and games play normally

#### Scenario: HTTPS through a proxy path
- **WHEN** a player opens `https://example.com/keen/` through a proxy that passes `/keen` on to the
  server unchanged
- **THEN** the lobby connects over `wss://example.com/keen/ws` and games play normally

## ADDED Requirements

### Requirement: Installer runs only when downloaded whole
The installer SHALL take no action unless the whole script was read, so that a `curl | bash` cut
off part way through the download changes nothing on the machine.

#### Scenario: Truncated download
- **WHEN** only the first part of `install.sh` reaches `bash`, at any point in the file
- **THEN** no package is installed, no user, file or service is created or changed, and the
  service keeps running as it was

#### Scenario: Complete download
- **WHEN** the whole of `install.sh` is piped to `bash`
- **THEN** it installs or upgrades as before

### Requirement: README leads with the install
The README SHALL give the commands that install the game on a Debian server or Proxmox LXC before
any other section, and SHALL not describe building or playing the game as a native program.
Operator detail SHALL be in `docs/guide.md` and build and test detail in `docs/development.md`,
both linked from the README.

#### Scenario: Finding the install command
- **WHEN** someone opens the repository's README
- **THEN** the `apt-get install -y curl` and `releases/latest/download/install.sh | bash` commands
  are in the first section after the introduction

#### Scenario: No native play instructions
- **WHEN** the README is read end to end
- **THEN** it has no instructions for compiling, packaging or running `keen1` to play on Linux

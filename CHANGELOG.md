# Changelog

Each released version has a section headed `## <version> - <date>`. The release workflow
publishes that section as the GitHub release notes. A tag without a section here is not published,
so add the entry with the version bump.

## 0.2.1 - 2026-10-22

### What's new

- **The README leads with the install.** It is now a short page: what the game is, the two
  install commands, requirements, how to play and the controls. Everything else moved to
  `docs/guide.md` (running a server: installer options, game data, settings, TLS reverse proxy,
  Proxmox notes, releasing) and `docs/development.md` (building, testing, replays, co-op rules).
  Both guides are in the release package next to the README.
- **The installer cannot run half-downloaded.** `install.sh` does all its work in a function
  called on its last line, so a `curl | bash` cut off part way through runs nothing. CI checks
  this for every possible cut.
- **The game files load as one download.** The page fetches all 31 files as a single
  `data/game.tar.gz` and unpacks it in the browser, falling back to the single files. This gets
  the game past web filters (such as some corporate proxies) that block the binary files one by one.
- **No native player package.** `make dist` and its launcher script are gone, and the docs no
  longer describe playing the game as a Linux program. The native build stays as the tool the
  determinism tests use.

### Upgrading

Run the installer again. Nothing else changes: the same paths, options and service.

## 0.2.0 - 2026-10-22

### What's new

- **The installer fetches the game files.** The release no longer contains any game data. On
  install, `install.sh` downloads the free Keen 1 v1.31 shareware episode (Internet Archive, then
  commander-keen.com), checks every file against a list of known md5 sums and keeps them in
  `/var/lib/keen1-online/game/`. Already have `1keen.zip`? Put it in `/root` (or set
  `KEEN_DATA_ZIP`) and nothing is downloaded. Upgrades leave the game files alone.
- **Works behind a proxy path such as `/keen`.** The server answers the page, `data/`, `/healthz`
  and the WebSocket under whatever prefix the request carries, and sends `/keen` to `/keen/`, so
  a proxy needs no rewrite rule.
- **`/healthz` reports `gameData`** (`ok` or `missing`), and the page says so when the server has
  no game data installed instead of staying on "Loading game data".
- **Releases are built by a workflow** when a `v*` tag is pushed, with `install.sh` attached as
  its own asset, so `releases/latest/download/install.sh` always serves the current installer.

### Upgrading

Run the installer again. It adds the game files and keeps the config, saves and high scores.
Running a release by hand now needs the game files in the game data directory (the README says how).

# Proposal

## Why

keen1-online is now a browser game served from a Debian server or Proxmox LXC, but the README still
reads like keen-clone's: it opens with compiling and running a native Linux binary, and the one
command an operator needs, the LXC installer, is at line 245 of 697 under "Running a server".
People looking for how to install it do not find it. `cobble` and `xonotic-webui` put the install
command first in a short README and keep the rest in a guide; keen1-online should do the same, and
drop what only made sense for a game played natively on Linux.

## What Changes

- **Short README, install first.** `README.md` becomes a page of about 100 lines: a paragraph on
  what the game is, then the LXC install (`apt-get install -y curl`, then the
  `releases/latest/download/install.sh | bash` one-liner, the address it prints, rerun to upgrade),
  a requirements table, how to play (lobby, controls, saves and high scores) and links to the two
  guides.
- **Operator guide.** A new `docs/guide.md` takes the server material now in the README: installer
  options, game data, settings, where things are, running a release by hand, the TLS reverse proxy
  (nginx and Caddy, with and without a path prefix), Proxmox notes and releasing.
- **Developer guide.** A new `docs/development.md` takes the build and test material: Emscripten,
  `make web` / `node` / `nodenet`, running the server locally, the native build as the test
  harness, determinism tests and replays, `--local2`, the `KEEN_SCRIPT` harness, the co-op rules
  and the repository layout.
- **Native play removed from the docs.** The "About the port", "Building" and "Running" sections
  (`./keen1 --fullscreen`, `--scale`, `data/` next to the binary, `run.sh`) go. The native build
  stays, documented only as the tool `tests/run.sh` and CI use.
- **Files no longer needed removed.** `packaging/keen1.sh` and the Makefile's `dist` target and
  `GAMEZIP` variable (a Linux player package) are deleted. `.claude/launch.json`, which serves
  `build/web` with a static file server that cannot run the game any more, is pointed at the real
  server.
- **Installer cannot run half a script.** `deploy/install.sh` is wrapped in a `main` function
  called on its last line, as `xonotic-webui`'s is, so a `curl | bash` cut off mid-download runs
  nothing.
- **Release carries the guides.** `make release` puts `docs/` in the package next to the README,
  so the README's links work in an extracted release.
- **Patch release 0.2.1** so the hardened `install.sh` is what `releases/latest/download/install.sh`
  serves: version bump and `CHANGELOG.md` section. Pushing the tag stays with the maintainer.

Kept as they are: `analysis/`, `tools/`, `server/README.md` (the protocol and config reference the
server's README test reads), and the installer's behaviour.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `server-deployment`: the release package also carries `docs/`; the proxy settings live in the
  operator guide rather than the README; the installer runs only when it was downloaded whole; the
  README leads with the install command.

## Impact

- Docs: `README.md` (rewritten), new `docs/guide.md` and `docs/development.md`, `CHANGELOG.md`.
- `deploy/install.sh` (structure only, plus its pointers from "the README" to the guide).
- `Makefile` (`dist` and `GAMEZIP` removed, header comment, `release` copies `docs/`).
- Deleted: `packaging/keen1.sh` (and the `packaging/` directory).
- `.claude/launch.json`, `server/package.json` (version 0.2.1).
- New `tests/install-truncation.sh`, run as one extra step in CI's server job.
- No change to the game, the server's behaviour, the web build or the release workflow.

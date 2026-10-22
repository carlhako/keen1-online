# Proposal

## Why

The release package carries the shareware episode inside the WebAssembly bundle, so a release can
only be built on a machine that has the data and Emscripten, by hand, and every release
redistributes the game files. The installer should fetch the free episode itself, releases should
be built by a tag-triggered workflow like `cobble` and `xonotic-webui`, and the server must work
behind a reverse proxy that keeps a path prefix such as `/keen` (today that returns "Not found").

## What Changes

- **Data-free release.** The web build no longer uses `--preload-file`. `web/pre.js` fetches the
  game files from the server (`data/index.json`, then each file) into the virtual `/data`
  directory before the game starts. The server serves them from `<dataDir>/game/`.
- **Installer fetches the data.** `deploy/install.sh` puts the Keen 1 v1.31 shareware files in
  `/var/lib/keen1-online/game/`, keeps them across upgrades and skips the step when they are
  already there and correct. Where it looks, in order:
  1. `KEEN_DATA_ZIP=/path/to/zip`, or a `1keen.zip`, `keen1.zip` or
     `Commander_Keen_1_-_Marooned_on_Mars_1990.zip` in the current directory or `/root`.
  2. `KEEN_DATA_URL`, when set.
  3. The Internet Archive zip (plain files in a `CKeen1/` folder), retried on 5xx.
  4. `https://www.commander-keen.com/downloads/keen1.zip` (Apogee's installer; unpacked with
     `tools/unshr.py`, which installs `python3` only in this case).
- **Checks the files, not the zip.** The installer verifies the md5 of each of the 31 game files
  against a list in the release, so any mirror or packaging works. Without a verified set it stops
  and says how to supply the zip. `/healthz` reports whether the game data is present.
- **Any URL prefix.** The server serves the game, `/healthz`, `data/` and the `ws` WebSocket under
  whatever path the request arrives on, and redirects `/keen` to `/keen/`. The page opens its
  WebSocket relative to its own URL.
- **Release pipeline.** `.github/workflows/release.yml` on a `v*` tag builds the data-free web
  build (Emscripten) and the server, publishes `keen1-online-<version>.tar.gz` and `install.sh` as
  release assets with notes taken from a new `CHANGELOG.md`; a manual run does a dry run. CI sets
  up Emscripten at the version used locally.
- **README** gains the install one-liner, where to put a downloaded zip, the md5 sums and where
  they come from, Proxmox `onboot` and `pct create` notes, prefix proxy examples (nginx, Caddy)
  and the release process.
- **Release 0.2.0** (0.1.0 was never published): version bump, changelog, tag.
- **BREAKING (packaging only).** A release no longer plays without game data; running one by hand
  needs the data directory filled (the installer does it, and the README says how).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `server-deployment`: the release package has no game data; the installer fetches and verifies
  it; releases are published by a tag workflow with the installer as its own asset; the server
  works under any path prefix.

## Impact

- `deploy/install.sh`, `Makefile` (`web`, `release`), `web/pre.js`, `web/shell.html`, `web/net.js`
  (WebSocket URL), `server/src/http.ts`, `server/src/server.ts`, `server/src/main.ts`, their
  tests, `server/package.json` (version), `README.md`, new `CHANGELOG.md`,
  `.github/workflows/release.yml` and `.github/scripts/release-notes.sh`, `.github/workflows/ci.yml`
  (`KEEN_SHAREWARE_URL` can point at the archive zip).
- Build: Emscripten no longer needs `shareware/data` for the release build; the replay tests still
  do.
- The unpacker `tools/unshr.py` ships in the release for the fallback source.
- Existing `server-deployment` requirements for config, data locations and upgrades stay;
  `/var/lib/keen1-online/game/` is new state that upgrades never touch.

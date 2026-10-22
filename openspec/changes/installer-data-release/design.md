# Design

## Context

See proposal.md for why. Facts that shape the approach:

- The web build packs `shareware/data` into `index.data` (`--preload-file ...@/data`). The game looks
  in its game directory and then in `WEB_DATA_DIR` (`/data`); `CTLPANEL.CK1` and `SCORES.CK1` are
  optional (the game falls back to defaults), so only the 31 data files are needed.
- `deploy/install.sh` already installs releases beside the running one, swaps them in and rolls
  back on a failed health check. Its inputs are the release tarball and `.nvmrc`.
- The server (`server/src/http.ts`) takes a list of web files at startup via `sirv`, answers
  `/healthz`, and `server.ts` accepts a WebSocket upgrade only on exactly `/ws`. `web/net.js`
  opens `wss://<host>/ws`.
- The user's proxy maps `/keen` to `10.0.1.182:9000` and keeps the prefix, so the server sees
  `/keen/`, `/keen/index.js` and `/keen/ws`.
- `cobble` and `xonotic-webui` publish from a tag with `softprops/action-gh-release`, notes from a
  `CHANGELOG.md` section via `.github/scripts/release-notes.sh`, and `install.sh` as a release
  asset next to the tarball. Both have a manual dry-run trigger.
- Locally Emscripten is 6.0.13 (`~/tools/emsdk`). CI has no emsdk today.

## Goals / Non-Goals

**Goals:**
- A release is built by CI from a tag, contains no game data, and installs end to end on a fresh
  LXC with one command.
- Whatever zip the operator ends up with, the game files on the server are the known v1.31 files.
- The same server answers at `/` and at any prefix with no configuration.

**Non-Goals:**
- Supporting the registered episodes, other versions (v1.1) or other games' data.
- A `basePath` config key or `X-Forwarded-Prefix` handling; prefixes are tolerated, not configured.
- Serving the data from a CDN, or letting the browser download it from a third party.
- Changing the game, netcode or saves.

## Decisions

**D1: Game data lives on the server and the page loads it at start.**
`<dataDir>/game/` (`/var/lib/keen1-online/game`) holds the 31 files. The server serves
`data/index.json` (names and sizes of the allow-listed files that exist) and `data/<NAME>`. `web/pre.js`
adds a run dependency, fetches the index and each file, `FS.writeFile`s them into `/data`, then
releases the dependency. The same files for every client also guarantees identical data for lockstep.
Alternatives: keep `--preload-file` and build the package on the server (needs emsdk there, no);
let the installer rewrite `index.data` (needs Emscripten's packager, no); fetch one concatenated
blob (one request, but a format to invent for ~380 KB).

**D2: The server has a fixed list of game file names.**
`server/src/gamefiles.ts` lists the 31 names. `/data/<NAME>` is served only for those, case
exactly as listed, read from the game directory per request, so no path from the request reaches
the filesystem. `/healthz` gains `gameData: "ok" | "missing"` (all 31 present). A test compares this
list with the installer's md5 list so they cannot drift. The server starts without data and
says so in its log; the page shows "This server has no game data installed" when the index is
empty or a fetch fails.

**D3: The installer verifies files, from either zip layout.**
The md5 list (31 lines, `md5  NAME`) sits in `install.sh` as a heredoc so the standalone script is
self-contained. Source order is the proposal's. Extraction: `unzip` to a temp directory; if it holds
`CK1SW131.SHR`, run `tools/unshr.py` from the extracted release (installing `python3` with apt only
then); otherwise find the game files in the tree (case-insensitively, any folder depth). Every
required file must be found and match; then they are copied into `game/.new` and renamed to `game/`.
Any failure leaves the previous `game/` alone and stops the installer before it stops the service.
Download is `curl --retry 3 --retry-all-errors` per source; the zip md5s are not pinned because the
per-file md5s are the check.
Alternatives: pin the zip md5 (breaks on a re-pack, rejects a good mirror); a Node unpacker instead
of python (more code to trust, python3 is one apt package and only on the fallback path).

**D4: One routing function resolves the prefix.**
`resolveRoute(path)` splits the decoded path into segments and, for i = 0, 1, 2 ..., tries the tail
`/seg[i]/.../seg[n]` against, in order: `/ws`, `/healthz`, `/data/index.json`, `/data/<NAME>`, a file in the web
directory (a set read at startup, replacing sirv's own scan), and `/` (the index, only when the
original path ended in `/`). The first tail that matches wins, so the shortest prefix is dropped.
A path that matches nothing and whose last segment has no `.` is taken to be a prefix of the game
and gets a 302 to the same path plus `/` (query kept); anything else is 404. Segments with `..`,
a backslash or NUL are 404 before matching. The WebSocket `upgrade` handler uses the same function
and accepts the `/ws` route. Static files are handed to `sirv` with `req.url` set to the matched tail.
Alternatives: a `basePath` setting (the proxy is then configured twice and a mismatch is the bug
the user hit); stripping a fixed number of segments (breaks when the prefix has two).

**D5: The page finds the server relative to itself.**
`net.js` builds `new URL('ws', location.href)` and swaps `http` for `ws`. The redirect in D4
guarantees the page URL ends in `/`, so `ws`, `data/index.json`, `index.js` and `index.wasm` all
resolve under the same prefix. Nothing in `shell.html` or `pre.js` uses a root-absolute URL; the
node test client (`KEEN_NODE_NET`) is checked for the old URL.

**D6: Release workflow and notes.**
`release.yml` runs on `v*` tags and by hand. Steps: check out; fail unless the tag equals
`v<server/package.json version>` and `release-notes.sh` finds a `CHANGELOG.md` section (the
bedrock script, adapted); set up Node from `.nvmrc`; set up Emscripten 6.0.13 with
`mymindstorm/setup-emsdk` (cached); `make release`; upload the tarball and `deploy/install.sh`
as `install.sh`; publish only for a tag. `make release` drops the shareware requirement, copies
`tools/unshr.py` into the package and keeps its clean-tree check (true in CI after `npm ci`
because `node_modules` is ignored). The existing `ci.yml` game job keeps replay tests behind
`KEEN_SHAREWARE_URL`, now settable to the Internet Archive zip; its extraction step finds the game
files in the zip itself rather than sharing code with the installer, which must stay a single
standalone script.

**D7: Version.** 0.1.0 was never tagged. This is 0.2.0 because the package layout (no data)
changes; `server/package.json` is the single source and the Makefile reads it.

## Risks / Trade-offs

- [Internet Archive item moves or returns 503] → retries, second source, `KEEN_DATA_URL`, and
  `KEEN_DATA_ZIP` for a zip the operator already has; the error says all three.
- [A "good" mirror carries different data] → rejected by the per-file md5, naming the file.
- [Unknown paths without a dot redirect instead of 404] → intended for prefix tolerance; typos like
  `/keen/healthzz` end on the lobby page. Files and anything with an extension still 404.
- [Page needs 32 requests before the game starts] → small files over keep-alive; the server sends
  `no-cache` with ETags so repeat visits revalidate cheaply.
- [A proxy that strips the prefix and also a client that adds it] → not a case: tails are matched,
  so both work.
- [CI emsdk version drifts from the local one] → pinned to 6.0.13; the build id is a hash of the
  wasm, so a different compiler is still a consistent release.
- [Release assets built by CI differ from the tested local build] → the dry-run trigger builds the
  same assets and keeps them as workflow artifacts to try on the LXC before tagging.

## Migration Plan

1. Land the change, run the dry-run workflow, install its tarball on a test LXC with
   `KO_TARBALL=` and no zip (downloads), then again with a zip supplied.
2. Bump to 0.2.0, update the changelog, tag `v0.2.0`.
3. On the existing server at `10.0.1.182`: run `install.sh` from the release. It fetches the game
   data, swaps in 0.2.0 and keeps config, saves and scores. Rollback is the installer's existing
   one (0.1.0 plays without the data directory, so a restore works).
4. Point the `/keen` proxy location at port 9000 as it is; no rewrite rule needed.

## Open Questions

- Whether to also offer `data/` files gzip'd to the browser; the files are small, so later if ever.

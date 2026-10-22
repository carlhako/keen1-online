# Tasks

## 1. Server: path prefixes

- [x] 1.1 In `server/src/http.ts`, add `resolveRoute` per design D4 (shortest-prefix tail match over `/ws`, `/healthz`, data routes, a startup set of web files, and `/`; 302 to the slash form for dotless unmatched paths; 404 for `..`, backslash and NUL segments). Verify with new cases in `http.test.ts`: `/keen/`, `/keen/index.js`, `/a/b/healthz`, `/keen` redirects to `/keen/` with its query kept, `/keen/nothing.js` is 404, `/keen/../x` is 404, `/` still works.
- [x] 1.2 In `server/src/server.ts`, make the WebSocket upgrade use the same resolver. Verify with a `ws.test.ts` case that connects to `/keen/ws` and plays a hello exchange, and that `/keen/other` still gets the 404 close.
- [x] 1.3 Make `web/net.js` build its URL from `new URL('ws', location.href)` and give the fake `location` in `tests/online/bot.mjs` an `href`. Verify with `make test-online` (needs emsdk and `npm run build`), and by checking `shell.html`, `pre.js` and `net.js` for root-absolute URLs (`grep -n "'/" web/*`).

## 2. Server: serving the game data

- [x] 2.1 Add `server/src/gamefiles.ts` with the 31 file names and serve `data/index.json` and `data/<NAME>` from `<dataDir>/game/` through the resolver (allow-list only, no path from the request used for the filesystem). Add `gameData: 'ok' | 'missing'` to `/healthz` and a log line at start when it is missing. Verify with `http.test.ts` cases: index lists present files, a file is served byte for byte, an unlisted name and `../` are 404, `/healthz` flips from `missing` to `ok` when the files are added.
- [x] 2.2 Change `web/pre.js` to fetch `data/index.json` and each file into `/data` before `callMain`, and `web/shell.html` to show "This server has no game data installed" when it is empty or a fetch fails. Verify in a browser (preview) with the data present (the game starts and level 1 plays) and with the directory empty (the message shows and loading does not hang).
- [x] 2.3 Remove `--preload-file` and the `WEB_DATA` requirement from the `web` target in the `Makefile`. Verify `make web` succeeds with `shareware/data` moved away, and that `build/web` has no `index.data`.
- [x] 2.4 Update the `tests/online` setup (e2e server start) to point the server's game directory at `shareware/data` where a page is loaded. Verify `make test-online` passes.

## 3. Installer

- [x] 3.1 Add the 31-line md5 list to `deploy/install.sh` and a test (`server/src/gamefiles.test.ts`) that parses it and checks it names exactly the files in `gamefiles.ts`; verify the test fails if either list differs.
- [x] 3.2 Add the data step to `deploy/install.sh`: source order (`KEEN_DATA_ZIP`, known zip names in cwd and `/root`, `KEEN_DATA_URL`, Internet Archive, commander-keen.com), `curl --retry 3 --retry-all-errors`, `unzip` (installed with apt when missing), either zip layout (plain files at any depth, or `CK1SW131.SHR` through `tools/unshr.py` with `python3` installed only then), per-file md5 check, copy to `game/.new` and rename. It runs after the release is extracted and before the service is stopped; a failure names the file or source and how to supply a zip. Verify with `bash -n`, `shellcheck` if available, and on a throwaway Debian container: fresh install from the archive zip, with `KEEN_DATA_ZIP` set to each of the two zips, with a corrupted zip (nothing changes, service untouched), and a second run (downloads nothing).
- [ ] 3.3 Check the installer end to end on the Debian LXC / container with a built `0.2.0` tarball (`KO_TARBALL=`): the lobby loads over `http://<host>:8080/`, `/healthz` says `gameData: ok`, the service survives a reboot, and an upgrade run keeps `game/`, saves and scores.

## 4. Release pipeline

- [x] 4.1 Make `make release` data-free: no `shareware/data` needed, `tools/unshr.py` copied into the package, `deploy/` included. Verify the tarball lists no `.CK1`/`KEEN1.EXE` (a check added to the `release` target that fails if it finds any) and installs with `KO_TARBALL`.
- [x] 4.2 Add `CHANGELOG.md` (format as in cobble: a `## <version> - <date>` heading per release) with a `0.2.0` section, and `.github/scripts/release-notes.sh` adapted from `bedrock-server`. Verify `release-notes.sh v0.2.0` prints the section and exits non-zero for a version with no entry.
- [ ] 4.3 Add `.github/workflows/release.yml` per design D6 (tag trigger plus manual dry run, tag must match `server/package.json`, Emscripten 6.0.13, Node from `.nvmrc`, tarball and `install.sh` uploaded, publish only on a tag). Verify by running the dry run from the Actions tab and downloading its artifacts, and check the tarball with `tar -tzf`.
- [ ] 4.4 Update `ci.yml` so the replay tests can use the Internet Archive zip (find the game files in the zip), set the `KEEN_SHAREWARE_URL` variable on the repository, and verify the `Game` job runs the replay tests instead of skipping them.

## 5. README and spec

- [ ] 5.1 Update `README.md`: the install one-liner from `releases/latest/download/install.sh`; where to put `1keen.zip` (`scp` to `/root/`, `KEEN_DATA_ZIP`, `KEEN_DATA_URL`); the two sources with their zip md5s and the note that the game files are what is checked; Proxmox `pct set <id> --onboot 1` and an optional `pct create` example; prefix examples for nginx (`location /keen/ { proxy_pass http://host:9000; }` without a rewrite) and Caddy; and the updated release process (tag, CHANGELOG, dry run). `readme.test.ts` must still pass, and each documented command is run once as written where possible.
- [ ] 5.2 Update `openspec/specs/server-deployment/spec.md` from the change's delta by running the sync/archive workflow when the change is finished (not by hand now).

## 6. Release 0.2.0

- [x] 6.1 Bump `server/package.json` (and the lockfile) to `0.2.0`; run the server tests, lint, typecheck and `make test`; commit.
- [ ] 6.2 Push, run the dry run, tag `v0.2.0` after the user agrees, wait for the workflow, and verify `https://github.com/carlhako/keen1-online/releases/latest/download/install.sh` downloads and the release notes show the changelog section.
- [ ] 6.3 On the server at `10.0.1.182`: run the installer, confirm `/healthz` and the lobby at `https://<proxy>/keen/`, and play a game through the proxy.

## Workflow follow-up

- Archive the change once the release is live and the proxy path is confirmed working.

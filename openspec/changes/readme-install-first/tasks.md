# Tasks

## 1. Installer: run only when downloaded whole

- [x] 1.1 Wrap `deploy/install.sh` per design D4: constants stay at file scope, everything from the root/apt-get/systemctl checks to the final `log` lines moves into `main() { ... }`, the work-directory `trap` is set inside `main`, and the last line is `{ main "$@"; }` (a bare `main "$@"` cut to `main` would still run). Update the header comment to say why. Verify `bash -n deploy/install.sh` passes and `git diff -w` shows the body moved, not edited.
- [x] 1.2 Add `tests/install-truncation.sh`: for every line of `deploy/install.sh`, run bash as a non-root user on the file cut at the start and in the middle of that line, and fail if any run prints `==>`, `!!` or `XX`; then run the whole file and require exactly the `run as root` error. Verify it passes on the wrapped script and fails on the unwrapped one (`git stash` the 1.1 change to check).
- [ ] 1.3 Add a step that runs `tests/install-truncation.sh` to CI's server job in `.github/workflows/ci.yml`. Verify the CI run on the branch shows the step passing.
- [ ] 1.4 Run the wrapped installer as root in a fresh Debian 13 LXC or container with `KO_TARBALL` set to a package built from this branch (`make release`): verify a fresh install prints the play address and `/healthz` answers, and that a second run reports "nothing to do" without restarting the service.

## 2. Remove the native player packaging

- [x] 2.1 Delete `packaging/keen1.sh` (and `packaging/`), the Makefile's `dist` target, `DIST`, `GAMEZIP` and their `clean` and `.PHONY` entries. Rewrite the Makefile header so `make` is described as the native test/replay build and `make web` / `make release` as the game. Verify `make clean && make` still builds `./keen1`, `make dist` is gone ("No rule"), and `grep -rn 'GAMEZIP\|packaging/\|keen1-linux' --exclude-dir=openspec --exclude-dir=analysis .` finds nothing.
- [x] 2.2 Replace the `python3 -m http.server` entry in `.claude/launch.json` with one that runs the server (`npm run dev` in `server/`, port 8080) per design D6. Verify with `preview_start` that the lobby loads with game data in `server/data/game` and the browser console shows no errors.

## 3. Operator guide

- [x] 3.1 Create `docs/guide.md` from the README's "Running a server" sections (From a release, On a Debian server or Proxmox LXC with its options table, Game data, Settings, Where things are, Behind a TLS reverse proxy with nginx and Caddy with and without `/keen`, Proxmox notes, Releasing) and the detail of Saving and loading and High scores, per design D2. Fix relative links for the new location (`../server/README.md#configuration`, `../deploy/install.sh`, `../.nvmrc`). Verify every heading in that part of the old README has a home in the guide and that each relative link in the guide resolves to an existing file.
- [x] 3.2 Point the installer's messages that say "see the README" at `docs/guide.md` (the `#game-data` section). Verify with `grep -n README deploy/install.sh`.

## 4. Developer guide

- [x] 4.1 Create `docs/development.md` from the README's Web port build sections (emsdk, `make web`, the build output table, running it locally, how the web port works), Deterministic simulation, Replays, Tests, Two players on one screen (`--local2`, `KEEN_SCRIPT`), Co-op rules and Repository layout, per design D2. Describe `./keen1` only as the headless test and replay build (`make`, SDL2 dev files, the flags `tests/run.sh` uses). Mention `make node`, `make nodenet` and `make test-online`, and drop the dangling `tests/online/README.md` reference from the Makefile comment or point it at the guide. Verify the commands in its build and test sections run as written (`make web`, `make`, `make test` with `shareware/data`).

## 5. Short README

- [x] 5.1 Rewrite `README.md` per design D1: intro paragraph, Install (curl, then the one-liner, the printed address, rerun to upgrade, TCP 8080, HTTPS pointer to the guide), Requirements table, Playing (lobby, two players, browser notes, co-op votes and the Esc menu, saves and high scores in short), Controls (browser keys; keep Alt+Enter only if it works in the browser, checked in the preview), Documentation links, trademark line. Verify the install commands are the first section after the intro, the file is about 100 lines, every relative link resolves, and `grep -n -i 'fullscreen\|--scale\|make dist\|run.sh\|libsdl2\|build-essential' README.md` finds nothing.
- [x] 5.2 Check the move is complete: list every `##`/`###` heading of the README at `HEAD~` (before this change) and confirm each one's content is in README.md, `docs/guide.md`, `docs/development.md` or was deliberately dropped (native Building, Running, `make dist`). Verify `cd server && npm test` still passes (its README test reads `server/README.md`, which is unchanged).

## 6. Release 0.2.1

- [x] 6.1 Make `make release` copy `docs/` into the package beside `README.md` (design D5) and mention it in the Makefile's release comment. Verify `make release` on a clean tree lists `docs/guide.md` and `docs/development.md` in the tarball and its game-data check still passes.
- [ ] 6.2 Bump `server/package.json` (and the lockfile's version) to `0.2.1` and add a `## 0.2.1 - <date>` section to `CHANGELOG.md`: README leads with the install, docs moved to `docs/`, installer cannot run half-downloaded, native player packaging removed. Verify `.github/scripts/release-notes.sh v0.2.1` prints the section and a release dry run (`workflow_dispatch`) succeeds.

## Workflow follow-up

- The maintainer pushes the `v0.2.1` tag; then check that `releases/latest/download/install.sh` serves the wrapped script.
- Archive the change with `/opsx:archive` after review, which syncs the `server-deployment` delta into the main spec.

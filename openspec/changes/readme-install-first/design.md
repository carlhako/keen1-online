# Design

## Context

See proposal.md for why. The current `README.md` (697 lines) mixes four audiences: people who want
to play, operators installing a server, developers building and testing, and keen-clone's native
port. `server/README.md` is a separate protocol and configuration reference;
`server/src/readme.test.ts` reads that file (not the root README) and checks its message tables
and config defaults, so it is not touched here.

The native `./keen1` build is not a player product any more, but it is load-bearing:
`tests/run.sh` (snapshot self-test, frame-delay independence, `--local2` scripted runs, `--netlog`
join and leave checks) and CI's "Game" job build and run it. The web build shares `platform.c`'s
SDL code through Emscripten, so removing the native target would save almost no code and would
need the test suite moved onto `make node` first.

`deploy/install.sh` runs top to bottom at file scope. It is usually run as
`curl ... | bash`, where bash executes each command as it arrives, so a connection that drops part
way through runs a partial install.

## Goals / Non-Goals

**Goals:**
- A reader of the README finds the install command without scrolling.
- Every piece of the current README that still applies has one home: README, guide, development
  guide, or `server/README.md`.
- The installer is safe to pipe from `curl`.

**Non-Goals:**
- Removing or porting the native build, or moving the tests to WebAssembly.
- Removing `analysis/` or `tools/`.
- A Proxmox host-side script that creates the container (`pct create`); the guide keeps its
  Proxmox notes for doing that by hand.
- Any change to the installer's behaviour, options or layout beyond the wrapper.
- Screenshots (the README can gain them later; none exist yet).

## Decisions

### D1. README shape, modelled on xonotic-webui

```
# keen1-online
<one paragraph: Keen 1 co-op in the browser, two players, server-hosted saves and scores;
 shareware data fetched by the installer; trademark line moves to the end>

## Install                      <- Debian 12+/Proxmox LXC, as root
   apt-get update && apt-get install -y curl
   curl -fsSL .../releases/latest/download/install.sh | bash
   prints http://<ip>:8080/ ; rerun the same command to upgrade ; open TCP 8080 on the LAN
   one line on putting it behind HTTPS -> docs/guide.md

## Requirements                 <- table: OS, arch, memory, network/outbound hosts, game data, build tools: none

## Playing                      <- lobby, two players, browser notes, saves and high scores

## Controls                     <- the browser-relevant keys

## Documentation                <- docs/guide.md, docs/development.md, server/README.md

## Licence / trademark
```

Alternative considered: keep one README and only move the install section to the top. Rejected:
it stays ~700 lines with native instructions in it, which is the problem the user hit, and the two
sibling projects already settled on README + guide.

### D2. Where each current section goes

| Current README section | Goes to |
|---|---|
| Intro, About the port | README intro (rewritten, web only); keen-clone credit and REA/Ghidra origin as one sentence |
| Building, Running (native) | removed; the native flags the tests use are listed in development.md |
| Web port: Building / Running locally / How it works | development.md |
| Web port: Playing in the browser, Putting it on a website | README "Playing"; the website note is the guide's intro |
| Playing online, Saving and loading, High scores | README "Playing" (short), detail in guide.md |
| Running a server (all subsections), Releasing | guide.md |
| Deterministic simulation, Replays, Tests | development.md |
| Two players on one screen, Co-op rules | development.md; the player-visible rules (votes, R to restart, Esc menu) summarised in README "Playing" |
| Controls | README, without Alt+Enter unless it is confirmed to work in the browser |
| Repository layout | development.md |

Anchors other files link to (`#game-data` from `install.sh` messages, `server/README.md#configuration`)
are kept or updated so no link breaks.

### D3. The native build stays as a developer tool

`make` still builds `./keen1` (CI runs plain `make`, and `make test` depends on it). The Makefile
header and development.md describe it as the headless test and replay build, needing SDL2
development files. `make dist`, `GAMEZIP`, the `dist` clean rule and `packaging/keen1.sh` go,
since they only package the native game for players.

Alternative considered: make `web` the default target. Rejected for this change: it needs emcc on
PATH, would break CI's `make` step and the replay loop, and buys nothing for players.

### D4. Installer wrapper

Everything after the constant assignments (from the root/apt/systemd checks to the final `log`)
moves into `main() { ... }`, and the file's last line is `{ main "$@"; }`. bash cannot run a
function until it has parsed its closing brace, so any cut before the last line leaves either an
unclosed function (a syntax error, nothing runs) or only variable assignments. The call is a `{ }`
group rather than a bare `main "$@"` because a cut inside a bare call that leaves `main` would still
run it (found by the truncation test); every prefix of the group is an unclosed group, a syntax
error. Helper functions stay where
they are; they read the same variables, which are still set before they are called. The `trap`
for the work directory is set inside `main`.

Verified by a new `tests/install-truncation.sh`: for every line of the script, it feeds bash the
file cut at the start and in the middle of that line, as a non-root user, and fails if any run
prints the installer's log or error markers (the root check is the first thing `main` does, so a
partial run that reached it would print `XX run as root`). The whole file, as non-root, must print
exactly that error. It needs only bash, so it runs as a step in CI's server job.

Alternative considered: `{ ...; }` block or `exit` guards. The function is what xonotic-webui uses
and reads the same to anyone who knows that script.

### D5. Release package carries `docs/`

`make release` copies `docs/` beside `README.md`, so the README's relative links resolve in an
extracted package. The game-data check on the tarball is unchanged.

### D6. `.claude/launch.json`

The `python3 -m http.server` entry serves `build/web` without the server, so the page cannot load
its game data or reach `/ws`. It is replaced by an entry that runs the server (`npm run dev` in
`server/`, port 8080), which serves `build/web` and the lobby together.

### D7. Release 0.2.1

The installer change only reaches operators through the `install.sh` release asset, so this ships
as 0.2.1: `server/package.json` version and a `CHANGELOG.md` section (docs restructure, installer
truncation guard). Tagging `v0.2.1` and pushing it is left to the maintainer.

## Risks / Trade-offs

- [Content lost in the move] → tasks check every current README heading against D2's table, and
  `rg` for links into removed anchors before the old text is deleted.
- [Wrapper changes behaviour] → the body is moved, not edited; a fresh install and an "already
  current" rerun on a Debian 13 LXC (or container) are part of the tasks.
- [Docs drift between README and guide] → the README carries only the one-liner and links; the
  options table and everything else live once, in the guide.
- [GitHub shows `docs/` links fine, but old links to README anchors from outside the repo break]
  → acceptable; the README's Documentation section names where each topic went.

## Migration Plan

Nothing to migrate on installed servers: the installer's behaviour, paths and options are the
same, and rerunning it upgrades to 0.2.1 as usual. Rollback is the installer's existing
`RELEASE_TAG=v0.2.0`.

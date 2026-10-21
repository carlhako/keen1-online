# Design

## Context

After `online-lobby-netplay`, `server/` runs the lobby and lockstep relay but stores nothing. The C
game still saves `SAVEDn.CK1` and `SCORES.CK1` through the web build's IndexedDB-backed `/save`.
The save is the 92-byte `gamestate_t` (`src/game.h`). The table is the original's 7-entry `hs`
record in `src/menus.c` (score, ship parts, 12-character names). `locomotive-wars` has the
installer and service model to copy: `deploy/install.sh` (service user, Node from nodejs.org,
release from GitHub, `/opt/<name>/releases/<version>` with a `current` link, config written once,
health check and rollback).

## Goals / Non-Goals

**Goals:**
- Saves and scores live on the server, shared by everyone on it.
- One-command install and upgrade on a Debian LXC, mirroring `locomotive-wars` so both services
  are run the same way.

**Non-Goals:**
- Accounts, per-player save ownership, or deleting saves from the UI (deleting is by hand on disk).
- Running the reverse proxy itself. The README documents it, and the user's existing proxy does it.

## Decisions

### Saves are the game record plus metadata
A save is the 92-byte `gamestate_t` (base64 in JSON) plus name, date, levels done and score.
Each save is stored as its own file, `/var/lib/keen1-online/saves/<id>.json`, so a growing number of
saves never means rewriting one big file. The list is built by reading the directory at start-up and
kept in memory.
- Alternative: a full snapshot. Rejected: saves only happen on the world map, where the record is
  the whole game, as in the original.

### Saving pauses through the input stream
F5 on the map emits `PAUSE_ON` with reason "saving", as F1 does in `coop-game-rules`. The saving
browser runs the original name prompt locally, sends `save{record, name}`, shows the reply, then
emits `PAUSE_OFF`. The record is the one both games share, so it doesn't matter which player saves.

### Loading a save is a new game with a starting record
`load{saveId}` creates a game like `create` does (same limit), and its `start` event carries the
record. The C game starts its world-map loop from that record instead of a new game.

### High scores: one JSON file, validated against the game's own reports
`/var/lib/keen1-online/highscores.json` holds the 7 entries, written atomically (temp file and
rename). The server accepts a submission only from a game that sent `finished`. Each `frame`
checksum report also carries the shared score, and a submission's score must not exceed the last
one reported. This blocks casual forgery without trying to verify the game itself.

### Release package and data
`make release` builds the wasm (`make web`) and the server, and packs
`keen1-online-<version>/{server/dist, server/package*.json, web/, config.example.json, deploy/,
README.md, .nvmrc}`. The shareware v1.31 data is already preloaded into the web build's `.data`
file, so the release carries it. It stays out of git as before. Releases are uploaded with
`gh release create`. CI (GitHub Actions) runs the server's lint, typecheck and tests plus the native
build and replay determinism test. It does not build the wasm (no emsdk in CI), so releases are built
locally.
- Alternative: the installer downloads the shareware zip from a mirror and extracts it with
  `tools/unshr.py`. Rejected: an extra moving part and a Python dependency on the server, for the
  same freely distributed shareware files.

### Installer mirrors locomotive-wars
`deploy/install.sh` is adapted from `locomotive-wars/deploy/install.sh`, with names changed
(`keen1-online` user, service, `/opt`, `/etc`, `/var/lib` paths). It keeps the same options
(`RELEASE_TAG`, `KO_TARBALL`, `KO_FORCE`) and the same health check against `/` after a restart.
The health check also confirms `/ws` accepts an upgrade.

## Risks / Trade-offs

- **[Shareware redistribution]** The release ships Apogee's shareware episode files. → Only the
  unmodified shareware episode v1.31, which was released for free distribution, never the
  registered episodes. The README states this and credits id Software and Apogee.
- **[Saves visible to everyone]** No accounts means any visitor can load any save. → Accepted for a
  personal server. Saves contain only game progress and a name.
- **[Unbounded saves]** The user accepted saves accumulating. → Each is a few hundred bytes. The
  README says where they are, for pruning by hand.
- **[Releases built locally]** No wasm build in CI. → `make release` refuses to run with
  uncommitted changes, so a release always matches a commit.

## Migration Plan

First install via the installer. Upgrades re-run it. Rolling back is
`RELEASE_TAG=<older> bash install.sh`. Saves and scores are untouched in every case.

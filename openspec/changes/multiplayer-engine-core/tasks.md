# Tasks

## 1. Create the keen1-online repository

Run group 1 from `keen-clone` (`/home/carl/projects/commander-keen`), where this change was written.
From 1.6 onward, work in `/home/carl/projects/keen1-online`, where the copied change continues.

- [x] 1.1 Clone the web-build branch into the new repo: `git clone --branch claude/funny-lumiere-19c88b /home/carl/projects/commander-keen /home/carl/projects/keen1-online`; verify `web/shell.html` and the `web:` target in the `Makefile` exist there
- [x] 1.2 Make it its own main branch with keen-clone as upstream: `cd /home/carl/projects/keen1-online && git branch -m main && git remote rename origin upstream && git remote set-url upstream https://github.com/carlhako/keen-clone.git`; verify `git branch` shows `main` and `git remote -v` shows only `upstream` pointing at GitHub
- [x] 1.3 Copy the untracked shareware data: `cp -r /home/carl/projects/commander-keen/shareware /home/carl/projects/keen1-online/`; verify `shareware/data/EGAHEAD.CK1` exists and `git status` does not list `shareware/` (still ignored)
- [x] 1.4 Carry over OpenSpec and its agent setup: `cp -r /home/carl/projects/commander-keen/openspec /home/carl/projects/commander-keen/.pi /home/carl/projects/keen1-online/ && mkdir -p /home/carl/projects/keen1-online/.claude && cp -r /home/carl/projects/commander-keen/.claude/skills /home/carl/projects/commander-keen/.claude/commands /home/carl/projects/keen1-online/.claude/`, then `cd /home/carl/projects/keen1-online && openspec init --tools claude,pi`; verify `openspec list` in keen1-online shows `multiplayer-engine-core`, `coop-game-rules`, `online-lobby-netplay` and `server-saves-deploy`
- [x] 1.5 Remove the four changes from keen-clone so they are only applied in keen1-online: `rm -r /home/carl/projects/commander-keen/openspec/changes/{multiplayer-engine-core,coop-game-rules,online-lobby-netplay,server-saves-deploy}`; verify `openspec list` in keen-clone shows no changes
- [x] 1.6 Mark tasks 1.1-1.5 done in keen1-online's copy of this file, rewrite `README.md` as the keen1-online fork (what it is, link to keen-clone for the single-player port and reverse-engineering notes, how to build `make web`), and make the first commit; verify `source ~/tools/emsdk/emsdk_env.sh && make web` builds `build/web/index.html` and the game starts in the browser
- [x] 1.7 Create the GitHub repo and push: `cd /home/carl/projects/keen1-online && gh repo create carlhako/keen1-online --private --source . --push` (choose `--public` instead if wanted); verify `gh repo view carlhako/keen1-online` shows the commit

## 2. Deterministic simulation

- [x] 2.1 Fix the simulation step at 6 tics in `frame_start()` (keep real-time pacing for drawing); verify single-player levels 1 and 2 play at normal speed in the native build
- [x] 2.2 Add the game tick counter `gtick` to game state and replace every `plat_ticks()` read in `engine.c`, `actors.c` and the world-map code in `menus.c` that affects game logic; verify `grep -n plat_ticks src/engine.c src/actors.c src/menus.c` lists only pacing, sound or local-effect uses, each with a comment saying so
- [x] 2.3 Make the RNG state part of game state with a seed setter; verify two native runs of the same `KEEN_SCRIPT` with the same seed show the same enemy positions at the end
- [x] 2.4 Add a think/touch function registry and snapshot save/restore of the full game state (objects, specials, tile planes, `gs`, players, cameras, RNG, `gtick`, level globals); verify a unit-style test that snapshots at frame 300, runs to 600, restores, re-runs to 600 and gets the same state
- [x] 2.5 Add an FNV-1a checksum over the serialised snapshot, computed each frame; verify it changes when a single object field or tile is changed in a test
- [x] 2.6 Add headless replay: `--replay <file>` runs a recorded per-player input list and `--checksums <out>` writes one checksum per frame; verify the same recording replayed twice, once with a 1 ms frame delay and once with none, produces identical checksum files
- [x] 2.7 Document the replay format and the determinism rules (no `plat_ticks()` in game logic, all randomness from the game RNG) in `README.md`; verify the documented replay command runs as written

## 3. Multiple players

- [x] 3.1 Introduce `player_t` and the player array; move `keen_facing`, `keen_invuln`, `ctrl`, `ctrl_prev`, the camera and map position into it, with Keens in object slots `0..nplayers-1`; verify single-player levels 1, 2 and 4 still play and complete as before
- [x] 3.2 Route `kill_keen`, `keen_tiles`, the exit door, hazards, pickups, doors, switches and the in-level teleporter through the Keen that triggered them; verify in local two-player mode that player 2 collecting a keycard opens the door for player 1 and that a hazard kills only the Keen touching it
- [x] 3.3 Add the nearest-living-Keen helper and use it for every `objs[0]` read in `actors.c`; verify with a replay that a Garg charges the closer Keen and ignores a dead one
- [x] 3.4 Make object activation and deactivation test every player's camera; verify an enemy seen only by player 2 wakes in local two-player mode
- [x] 3.5 Keep Keen shots from hitting other Keens and Keens passing through each other; verify in local two-player mode that a shot passes the partner
- [x] 3.6 Draw player 2 with the second colour scheme and both Keens with P1/P2 tags when there are two players, in levels and on the world map; verify by screenshot of a two-player level and of the map
- [x] 3.7 Add local two-player mode (`--local2`, second key set for player 2, screen following player 1) and extend `KEEN_SCRIPT` lines with an optional player number; verify a two-player script replays with stable checksums
- [x] 3.8 Document `--local2`, the player 2 keys and the two-player script format in `README.md`; verify the documented commands run as written

## 4. Integration check

- [x] 4.1 Record a two-player replay through levels 1 and 2, then replay it in the native build and in the web build (headless browser or Node); verify the checksum sequences match

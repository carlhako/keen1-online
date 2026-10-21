# Design

## Context

The port is a function-by-function translation of the decompiled `KEEN1.EXE`. Its shape matters
for this change:

- Keen is hard-wired as `objs[0]`: `kill_keen`, `keen_tiles`, `scroll_follow`, the enemy
  steering helpers in `actors.c` (`keen_left`, Garg/Vorticon/tank choices around lines 14-125 and
  586-609) and the in-level teleporter all read or write `objs[0]`. Per-Keen values live in
  globals: `keen_facing`, `keen_invuln`, `ctrl`, `ctrl_prev`, `scroll_x/y`.
- Objects run through a copy: each think function works on the global `cur`, copied in and out of
  `objs[i]` by `play_level`.
- `frame_start()` sets `tics` from the wall clock (clamped 6..15), enemy AI rolls `rand8() < tics`,
  and about 13 places read `plat_ticks()` for animation, several of which pick sprites that
  decide hitboxes.
- Objects wake when inside the camera (`scroll_tx/ty`) and go back to sleep off screen.
- The web build (branch `claude/funny-lumiere-19c88b`) keeps the blocking DOS-style loops by
  yielding to the browser with ASYNCIFY in `plat_vsync()` / `plat_delay()`. That build is the base
  of `keen1-online`.
- Game data is not in git (`.gitignore` excludes `shareware/`, `*.CK1`, `*.EXE`); `make web`
  bundles `shareware/data`.

## Goals / Non-Goals

**Goals:**
- Same inputs and seed give byte-identical state on every frame, on any machine running the same
  build.
- Up to 2 Keens now; nothing in the player code limits it to 2.
- Keep the decompiled code recognisable: change the Keen-specific parts, not the overall structure.
- Verifiable offline: local two-player mode and a headless replay test.

**Non-Goals:**
- Co-op rules (who wins, restarting, the map handshake, non-blocking pop-ups): `coop-game-rules`.
- Networking, lobby and server: `online-lobby-netplay`.
- Changing single-player gameplay. Fixed tics are the one deliberate difference, and on modern
  machines the original already runs at 6 ticks per frame.

## Decisions

### The fork is a clone of the web-build branch, with data copied beside it
`keen1-online` is cloned from the local `keen-clone` checkout at the web-build branch, since that
branch is not pushed. `origin` is renamed `upstream` and pointed at
`https://github.com/carlhako/keen-clone.git` so fixes can still be pulled. `shareware/` stays
ignored by git and is copied in by hand, as in `keen-clone`. The `analysis/` and `tools/`
reverse-engineering material stays in the fork for reference. Alternative: a fresh repo with only
`src/` and `web/`, which loses history and the RE notes the port's comments refer to.

### Fixed `tics = 6`
`frame_start()` still paces drawing to real time, but the simulation always advances 6 ticks. If
the machine falls behind, the game slows rather than taking bigger steps.
- Alternative: keep variable tics and send them with the inputs. Rejected: it adds a value to
  agree on every frame for no gameplay benefit.

### A game tick counter replaces `plat_ticks()` in game logic
A frame counter in the game state (`gtick`, advanced by `tics` each frame) replaces every
`plat_ticks()` read in `engine.c`, `actors.c` and the world-map code. `plat_ticks()` is left for
pacing, sound and purely local effects (border flash, palette fades).

### Player context instead of `objs[0]`
A `player_t` array holds each player's object slot, facing, invulnerability, input (current and
previous), camera, map position and status. Keens occupy object slots `0..nplayers-1`, so
"Keen thinks first, is drawn on top" ordering still holds. Before a Keen's think runs,
`play_level` sets a current-player pointer, and the `keen_*` functions read `plr->facing`,
`plr->invuln`, `plr->ctrl` where they read globals today. `kill_keen()` takes the Keen it kills.
- Alternative: one `play_level` loop per player. Rejected: it would duplicate the object loop and
  break the shared world.

### Nearest living Keen for enemies
A helper returns the nearest living Keen to `cur` by horizontal distance (ties to the lower
player number). The `objs[0].x` reads in `actors.c` use it. With one player it returns that Keen,
so single player behaves exactly as before.

### Cameras live in the game state; drawing uses the local one
Each player's `scroll_x/y` is part of the game state and is updated by the same follow rules. The
activation test checks every camera. Drawing (`rf_refresh`) uses the local player's camera.
Effects that draw from inside think functions (`rf_tile` in the exit walk and doors) are drawing,
not state, so they stay as they are.

### Snapshots serialise function pointers as ids
`think` and `touch` pointers in objects and specials are written as indexes into a registry of
think/touch functions. This makes a snapshot independent of where functions sit in memory and lets
the checksum cover them. The checksum is FNV-1a over the serialised snapshot. A snapshot is the
objects, specials, both level tile planes, `gs`, the players, the RNG state, `gtick` and the
level-scope globals (`lights_on`, `last_extra`, bounds).

### Input sources
Per-frame input for each player comes from an input source: local keyboard (key set 1 or 2), a
script (the existing `KEEN_SCRIPT` harness, extended with a player field), or later the network.
`check_hotkeys()` and `check_cheats()` stay keyboard-only and outside the per-player input.

### Player 2 colours and tags
Player 2's Keen sprites are drawn through a small EGA colour remap (helmet and shirt colours
swapped to a second scheme) at draw time. The P1/P2 tag is a 2-character label drawn above each
Keen from the game's own font, only when there is more than one player.

## Risks / Trade-offs

- **[Missed real-time read]** A `plat_ticks()` or `tics` dependency left in game logic causes
  desyncs that only show online. → The replay test runs the same recording at two different
  real-time speeds and requires identical checksums, and a grep check lists every remaining
  `plat_ticks()` call outside the platform layer.
- **[Blocking pop-ups in the game loop]** The statue message, castle-switch shake, status screen
  and hotkey windows still block inside `play_level`. With local two-player this pauses both
  players, which is fine offline. → `coop-game-rules` turns them into non-blocking banners before
  the game goes online.
- **[Hitbox changes from animation timing]** Moving animation to `gtick` may shift which frame a
  sprite is on by a frame compared with today. → Accept it; single-player regression checks look at
  behaviour (levels still completable, enemies still behave), not frame-exact equality with the old
  build.
- **[Native vs wasm]** Online play only compares wasm with wasm, but tests run natively. → The
  game logic is integer-only. If a replay disagrees between native and wasm, the replay test also
  runs under Node against the wasm build.

## Migration Plan

Not applicable: a new repository. `keen-clone` is unchanged apart from removing the copied
OpenSpec changes once they live in `keen1-online`.

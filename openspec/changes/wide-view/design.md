# Design

## Context

See proposal.md for why. The relevant parts of the code today:

- **Frame buffer.** `screen[SCR_W * SCR_H]` (320x200, `src/platform.h`). Every drawing routine
  clips against `SCR_W`/`SCR_H` and uses `SCR_W` as the row stride: `put_tile`, `put_sprite`, the
  text primitives and `scroll_text_area` in `src/gfx.c`, the full-screen picture loader and
  `edge_arrow` in `src/ui.c`, and `write_png`.
- **Presenting.** `plat_present` copies the buffer to a 320x200 texture and draws it to the whole
  window at `SDL_RenderSetLogicalSize(ren, 320, 240)`, so each pixel is shown 5:6. In the browser,
  CSS sizes the canvas (`#screen` is a 4:3 box in `web/shell.html`), and the resizable,
  high-DPI SDL window follows the canvas.
- **Level renderer.** `rf_refresh` draws 21x14 tiles from `scroll_x`/`scroll_y`. `rf_sprite_ex`
  clips at `0x150`, and its column limits `0x15`/`0x16` find the foreground tiles to redraw over
  a sprite. `rf_label` and `rf_tile` clip the same way. All of these assume 20 visible columns.
- **The camera is simulation state.** `players[i].cam_x/cam_y/cam_tx/cam_ty` are snapshotted and
  checksummed (`src/sim.c`). The engine reads them in `scroll_follow` (follows Keen in a
  20-tile window), `cam_sees` (an enemy wakes when it is 2 tiles inside the camera area,
  `cam_sees(.., 2, 2, 0x17, 0xc)`, and falls asleep or is removed 9 tiles outside it,
  `obj_offscreen`), the death line in `keen_dying`, and the camera limits
  `scrollmin_*`/`scrollmax_*` (`map_w - 22`, which keeps the 2-tile border wall hidden).
- **What gets drawn.** `draw_camera` copies the local player's camera to `scroll_x`/`scroll_y`.
  The castle-switch shake is added afterwards, from its own random generator. The level loop
  draws objects only when `active` is set. `spawn_actors` gives every level actor a sprite when it
  is created, and `obj_offscreen` puts actors back to sleep with their last sprite.
- **Sound culling.** `heard_at` in `src/engine.c` tests against a 320x200 box (plus a tile) at the
  local player's camera. It does not touch game state.
- **Fixed screens.** Fixed screens set `scroll_x`/`scroll_y` to fixed values (title on level 90,
  story, help viewers in `src/ui.c`, order screens, the ending) and draw text with
  `draw_char`/`draw_bitmap` at 8-pixel columns of the 48-column EGA page.
- **Level sizes.** Measured from the v1.31 level files, the visible width is `map_w - 4` tiles.
  It is 20 tiles for levels 2, 5 and 6, 40 to 50 for levels 9 to 12, and 58 or more for the rest.

## Goals / Non-Goals

**Goals:**
- No change to any byte of game state, snapshots, checksums or lockstep traffic. View width is
  purely local.
- Run the existing level, world map and fixed-screen code paths unchanged apart from widths and
  offsets.
- Keep the renderer's per-frame cost about proportional to the pixels drawn.

**Non-Goals:**
- Taller views, or a different pixel shape.
- Changing wake ranges, camera movement or camera limits (that is option B2 from the
  exploration, ruled out).
- Making menus or text screens use the extra width.
- Integer-only scaling.

## Decisions

### 1. Fixed-stride buffer with a current width

`screen` becomes `SCR_MAXW * SCR_H` (480x200) with a constant row stride `SCR_MAXW`, plus a
global `scr_w` for the current frame's width (320 to 480). Clipping uses `scr_w`. Indexing uses
the stride.

- **Why:** the stride never changes, so the many `y * SCR_W` sites become one mechanical rename,
  and `memmove` in `scroll_text_area` keeps working on whole rows.
- **Rejected:** reallocating the buffer on resize. Pointers and row math would change mid-frame,
  for no benefit at 96 KB.

`SCR_W` is renamed so every remaining use is visited. 320 stays as `SCR_BASEW` for the fixed
screens.

### 2. Wide mode is opt-in per loop

A flag (`view_wide`) is set by the level loop (`play_level_run`) and the world map loop, and
cleared on the way out. Anything that draws a full fixed screen from inside those loops (help
viewer, story, high scores, the ending) saves the flag, clears it, and restores it. Each frame:
`scr_w = view_wide ? want_w : 320`.

- **Why:** the fixed screens are many and old. Treating 320 as the default, and widening only
  the two places that are views of a map, means a forgotten screen still looks right.
- **Rejected:** letting every screen widen and showing black in its extra columns. Every screen
  would have to clear its margins, and title screens drawn on level 90 would show extra map.

### 3. Placement is a pure function of the camera, applied when drawing

A helper `view_left(cam_x)` returns the drawn frame's left edge for a camera position:
`cam_x - (scr_w - 320) / 2`, clamped to `[lo, hi - scr_w]`, where `lo = scrollmin_x` and
`hi = scrollmax_x + 320 px` are the bounds of what the original could ever show. When
`hi - lo < scr_w`, the frame is centered on that range instead, and the renderer paints the
columns outside `[lo, hi)` black. `draw_camera` and the single-player world map scroll assignment
set `scroll_x = view_left(cam)`. The shake is added after placement, so the existing jitter
test still covers it.

- **Why:** `scroll_x` keeps meaning "left edge of what is drawn", so `rf_sprite`, `rf_tile`,
  `rf_label` and `rf_refresh` only need their width limits widened, not a second offset.
  Because placement is computed from the camera and never stored in the camera, game state cannot
  depend on it.
- **Rejected:** widening the camera itself (B2), which changes the game.
- **Rejected:** an offset applied inside `rf_refresh` only, which would put sprites queued earlier
  in the wrong place.

The narrow-level black is drawn by the tile loop: a tile column outside `[lo, hi)` is filled with
colour 0 instead of its map tile. Sprites there are clipped the same way. Without that, the border
walls would show.

### 4. Generalise the renderer by columns, not by special cases

- `rf_refresh` draws `scr_w / 16 + 1` columns.
- `rf_sprite_ex` clips at `scr_w + 32` and uses column limits from `scr_w` (foreground-redraw
  columns `0 .. scr_w / 16`).
- `rf_label` and `rf_tile` clip at the same limits. These are the only magic numbers; they are
  replaced by expressions with a comment saying what the original values were.

### 5. Text over a view is centered

`draw_char`, `draw_bitmap`, `draw_tile_at` and `draw_sprite_at` add `view_ox = (scr_w - 320) / 2`.
That is 0 on fixed screens, so they are unchanged. Windows over a level (the status window,
menus, banners, chat) land in the middle of the view. `scroll_text_area` works on the full stride,
so it is unaffected.

### 6. Draw sleeping actors

In the level loop's draw pass, the `objs[i].active` test is removed. Any object with a non-zero
`type` is drawn. Only level actors can be inactive (Keens never go through `obj_offscreen`; absent
seats have `type == 0`), so this draws exactly the sleeping enemies, in the sprite they spawned
with or slept in.

- **Effect in the original 320 area:** an actor whose position is in the half tile row at the
  bottom edge (the wake test stops at `cam_ty + 12`) is now drawn standing there instead of
  missing. This is accepted as a small visual change in favour of "what you see is there".

### 7. Present at the current width

The texture is created 480x200 once. `plat_present` copies `scr_w` columns, sets
`SDL_RenderSetLogicalSize(ren, scr_w, 240)` when `scr_w` changes, clears to black, and copies
the source rectangle `{0, 0, scr_w, 200}`. SDL letterboxes inside the window.

`want_w` comes from the renderer output size:
`want_w = clamp(round(240 * out_w / out_h), 320, 480)`, rounded down to an even number. It is
recomputed on `SDL_WINDOWEVENT_SIZE_CHANGED` and at start. The scale is set by the height whenever
the window is 4:3 or wider, so a fixed 320 screen and a wide level view have the same pixel size
and switching between them does not zoom.

- **Rejected:** black bars in the overscan border colour. The user asked for black, and today's
  page shows black there too.

### 8. Page layout

In `web/shell.html`, `#screen` becomes `position: fixed; inset: 0` (the whole window). The canvas
fills it, and SDL does the fitting.

- `--px` (one game pixel, used by the lobby's window frames) becomes
  `min(100vw / 320, 100vh / 240)`, the size of a game pixel at the height-fit scale.
- The lobby background already uses `cover`, so it fills the window as is.
- The overlays use `inset: 0`, so they follow `#screen`.

### 9. Sound and partner arrow use the drawn view

- `heard_at` tests against the drawn frame from `view_left(cam_x)` and `scr_w`, plus a tile. It
  runs with the same `scr_w` as the frame being drawn.
- `edge_arrow` uses `scr_w` for its bounds and center.

### 10. Test hook for view width

The environment variable `KEEN_VIEW_W` (a test aid like `KEEN_NOJITTER`) forces `want_w`, also
in headless runs, where the renderer still draws into `screen`. `tests/run.sh` replays a co-op
recording that crosses enemies waking and sleeping (`coop-levels-1-4`) at 320 and at 480 and
compares the checksum files with `cmp`, as the jitter test does. That proves that drawing sleeping
actors and wide placement never write game state. A `shot` at 480 in a short script gives a PNG
for a manual look; it is not compared.

## Risks / Trade-offs

- [A fixed screen drawn from inside a level or the world map is missed and picks up wide mode] →
  Decision 2 makes 320 the default. Tasks list each known entry point (help, status, high scores,
  save/load, quit menu, story, ending). The manual check walks through each.
- [A remaining 320 or `0x15` assumption in the renderer leaves a stripe undrawn at 480] → Renaming
  `SCR_W` forces every use to be revisited. The 480 screenshot shows any gap.
- [A sleeping actor's spawn pose looks odd: a tank spawned half a tile up before it falls, or a
  Yorp facing the wrong way] → Accepted. It only lasts until the actor wakes, a few tiles later.
- [The shake jitter gets clamped at level edges] → Placement is computed before the jitter, so
  the jitter is not clamped, the same as now.
- [A view can come close to the 9-tile sleep distance] → 5 extra tiles per side is less than 9, so
  an awake enemy never freezes inside the view. Keep the 480 cap tied to this margin, and say so in
  a comment next to it.
- [High-DPI canvas cost at 480 wide] → It is the same number of window pixels as now, only the
  texture is wider. The cost is negligible.

## Migration Plan

Client only. It ships with the next release. Nothing on the wire changes. The existing build-id
check in the handshake already sends players with an old page back to reload. Rollback is
reverting the commit.

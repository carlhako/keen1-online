# Proposal

## Why

The page holds the game in a 4:3 box, so on a 16:9 or wider monitor a third or more of the
browser window is black bars. Players want the game to use the window. Stretching the 320x200
picture would distort it. This change instead shows more of the level to the sides, without
changing how the game plays. Two things limit how far the view can grow: the original wake ranges
for enemies, and the camera is part of the lockstep simulation.

## What Changes

- **A wider view of levels and the world map.** During a level and on the world map, the picture
  is as wide as the window allows: from the original 320 game pixels up to 480 (1.5 times), always
  200 tall. Each game pixel keeps today's 4:3 CRT shape, so the widest view, 480 pixels, fills a
  2:1 window. A wider window gets black bars left and right. A window narrower than 4:3 gets bars
  above and below, as now.
- **Drawing only, the game is unchanged.** The extra columns are drawn around each player's
  existing camera. The camera's movement and limits, where enemies wake and fall asleep, and the
  line off the top of the view where a falling Keen dies all stay exactly as in the original. The
  simulation, saves, snapshots, checksums and lockstep traffic do not change, so players with
  different window sizes can share a game.
- **Sleeping enemies are drawn.** An enemy that has not woken yet is drawn standing still where it
  is. It wakes at the original distance, so in the wider view you can see an enemy before it starts
  to move.
- **Level edges stay hidden.** The view never shows past the area the original could scroll to. A
  level narrower than the view (levels 2, 5 and 6 are 20 tiles wide) is centered, with black on
  both sides.
- **Full-screen pictures stay 320 wide.** The title, story, help, menus, high scores, the ending
  and other full-screen pictures are drawn as now and centered at the same scale. Windows drawn
  over a level (the status window, menus, messages, chat) are centered in the wider view.
- **You hear what you see.** In co-op, a partner's or an enemy's sound is heard while its source
  is inside the view actually drawn, wide or not.
- **The page fills the window.** `web/shell.html` no longer holds a 4:3 box. The canvas covers the
  window and the game fits itself inside it. The lobby and the overlays (paused, finished, waiting)
  cover the whole window too.

## Capabilities

### New Capabilities

- `wide-view`: how the game picture fits the browser window: the view width range, what is drawn
  in the extra columns, how sleeping enemies look, level-edge and narrow-level handling, which
  screens stay 320 wide, and the guarantee that view width never affects game state.

### Modified Capabilities

- `coop-level-flow`: the *Partner sounds heard on screen* requirement now defines "on screen" as
  the view the player actually sees, which can be wider than the original 320 pixels.

## Impact

- **Code:** `src/platform.c`/`platform.h` (frame buffer with a fixed 480-pixel stride and a current
  width, texture, logical size, window resize), `src/gfx.c` (tile, sprite and label clipping, the
  column loops, text placement), `src/engine.c` (view placement from the camera, drawing sleeping
  actors, `heard_at`), `src/menus.c` and `src/ui.c` (turning the wide view on for levels and the
  world map, the partner edge arrow, the full-screen picture loader), `web/shell.html` (the page
  layout).
- **Unchanged:** the server, `web/net.js`, the netcode, save and snapshot formats, and replay files.
- **Tests:** `tests/run.sh` gains a check that a replay gives identical checksums at view widths
  320 and 480, through a test-only setting for the view width.
- **Native build:** the resizable desktop window follows the same rules. It still opens at 4:3, so
  it starts with the 320 view.
- **No release:** a version bump and the `CHANGELOG.md` section come with the next release, not
  with this change.

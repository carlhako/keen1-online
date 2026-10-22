# Tasks

Build and test as usual on this machine: `make SDL2_DIR=$HOME/tools/sdl2`, run with
`LD_LIBRARY_PATH=$HOME/tools/sdl2/lib`, test suite `sh tests/run.sh` with `shareware/data`.

## 1. Frame buffer with a current width

- [x] 1.1 In `src/platform.h`, replace `SCR_W` with `SCR_MAXW` (480, the row stride) and `SCR_BASEW` (320), and add `extern int scr_w` (current frame width, default 320) per design decision 1. Put the comment that ties the 480 cap to the 9-tile sleep distance next to `SCR_MAXW`. Verify the build fails at every old `SCR_W` use (`make` error list) before 1.2.
- [x] 1.2 Update every former `SCR_W` site to index with `SCR_MAXW` and clip with `scr_w`: `put_tile`, `put_sprite`, `draw_char`, `draw_bitmap`, `scroll_text_area` and the label loop in `src/gfx.c`, the full-screen picture loader and `edge_arrow` in `src/ui.c`, and `write_png` and `plat_present` in `src/platform.c`. Verify `make` builds, `grep -rn 'SCR_W\b' src` finds nothing, and `sh tests/run.sh` passes unchanged (still 320 everywhere).

## 2. Present at the current width

- [x] 2.1 In `src/platform.c` per design decision 7: create the texture `SCR_MAXW`x200, compute `want_w` from `SDL_GetRendererOutputSize` at init and on `SDL_WINDOWEVENT_SIZE_CHANGED` (clamped to 320 to 480, even), honour `KEEN_VIEW_W` when set (headless included), and in `plat_present` copy `scr_w` columns, clear to black, set `SDL_RenderSetLogicalSize(scr_w, 240)` when it changes, and copy the `{0,0,scr_w,200}` source rectangle. Verify a windowed run under `SDL_VIDEODRIVER=dummy` with `KEEN_VIEW_W=480` and a `shot` script line writes a 480x200 PNG with the 320 picture at the left and black to the right (wide mode is not on yet).

## 3. Wide mode, placement and renderer

- [x] 3.1 Add the `view_wide` flag and the per-frame `scr_w = view_wide ? want_w : 320` per design decision 2. Set the flag in `play_level_run` and the world map loop, and clear it on every way out. In the help viewer, story, high scores, save/load and quit screens and the ending, wherever they are reached from inside a level or the world map, save the flag, clear it, and restore it. Verify with `KEEN_VIEW_W=480` screenshots that the title, story and help screens are 320 wide and centered, and that a level frame is 480 wide.
- [x] 3.2 Add `view_left(cam_x)` per design decision 3: centered extra width, clamped to `[scrollmin_x, scrollmax_x + 320 px - scr_w]`, centered when the level is narrower than the view. Use it in `draw_camera` and in the single-player world map scroll assignments in `src/menus.c`, keeping the shake jitter after placement. Verify with 480 screenshots: Keen mid-level 1 (80 extra pixels each side), at level 1's left start (all extra on the right, no border wall), and on the world map.
- [x] 3.3 Generalise the level renderer per design decision 4: `rf_refresh` draws `scr_w / 16 + 1` columns, `rf_sprite_ex`, `rf_tile` and `rf_label` clip and choose foreground-redraw columns from `scr_w`, and tile columns outside the level's showable range are filled with black for narrow levels. Verify with 480 screenshots that the right edge of level 1 has no undrawn stripe and that sprites at the right edge get their foreground tiles redrawn over them. Use `warp` in a replay to put Keen at a place where he stands behind a foreground tile near the right edge. Also verify that level 2 shows its 20 tiles centered with black on both sides and no border wall.
- [x] 3.4 Center text drawn over a view per design decision 5 (`view_ox` in `draw_char`, `draw_bitmap`, `draw_tile_at` and `draw_sprite_at`). Verify with 480 screenshots that the Space status window, the Esc menu over a level, a co-op banner and the chat line are centered, and that 320 screenshots of the same are byte-identical to ones taken before this change.

## 4. Sleeping actors, sound and partner arrow

- [x] 4.1 Draw sleeping actors per design decision 6: remove the `active` test from the level loop's draw pass. Verify with a 480 screenshot that a Yorp beyond the wake distance in level 1 is drawn standing still, and with a short replay that it starts moving only at the original distance (its position is unchanged in `--replay` output until then).
- [x] 4.2 Make `heard_at` use the drawn frame (`view_left` and `scr_w`) and make `edge_arrow` use `scr_w` for its bounds and center, per design decision 9. Verify with `--local2` in a 480 window that a partner in the extra columns is drawn with no arrow pointing to them, and that sounds from there are heard (the `snd_muted` state in a debugger or a temporary log).
- [x] 4.3 Add the width-independence test to `tests/run.sh`, next to the jitter test, per design decision 10: replay `tests/replays/coop-levels-1-4.txt` with `KEEN_VIEW_W=320` and with `KEEN_VIEW_W=480`, and `cmp` the checksum files. Document `KEEN_VIEW_W` with the other test aids in `docs/development.md`. Verify the new check passes, and that it fails if wide placement is temporarily made to write `players[].cam_x`.

## 5. Page layout

- [x] 5.1 In `web/shell.html` per design decision 8: make `#screen` cover the window, keep the canvas filling it, and change `--px` to `min(100vw / 320, 100vh / 240)`. Verify after `make web` and a server restart, in the browser pane at 1920x1080, 1280x1024 and 3440x1440 (`resize_window`), that the lobby covers the window and its window frames are crisp, that a single-player game shows a 427, 320 and 480 wide view respectively with bars only at 3440x1440 (sides) and 1280x1024 (top and bottom), and that the console shows no errors.
- [x] 5.2 Check the overlays in the browser: *Waiting for player 2*, *Player 2 disconnected* (pause) and the end screen cover the window. Verify with screenshots at 1920x1080.

## 6. Integration

- [x] 6.1 Run the full suite including the WebAssembly part (`source ~/tools/emsdk/emsdk_env.sh && make node`, then `sh tests/run.sh`). Verify every check passes, including wasm == native.
- [x] 6.2 Play online with two browser tabs of different sizes (one 1280x960, one 1920x1080) through a level with enemies and the castle switch shake, using the `keen1-server` launch entry. Verify there is no desync message, both views look right, and resizing a tab mid-level changes only its own view.

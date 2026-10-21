# Tasks

Work in `/home/carl/projects/keen1-online` after `multiplayer-engine-core` is complete. Every
verification below uses the native build with `--local2` (or `--coop` for a one-player co-op game)
and, where it says replay, a recorded `KEEN_SCRIPT` with checksums from `--checksums`.

## 1. Co-op game flag and input events

- [x] 1.1 Add `game.coop`, the `--coop` (one player) development flag, and set it for `--local2`; verify a non-co-op single-player run still loses a life on death and returns to the map
- [x] 1.2 Add the one-frame input event bits (`RESTART_TOGGLE`, `MAP_TOGGLE`, `PAUSE_ON`, `PAUSE_OFF`) to the per-frame input, the replay format and the checksum; verify a replay containing each event reproduces identical checksums twice

## 2. Level flow

- [x] 2.1 Add `player_t.status` and set `EXITED` from the exit walk and `DEAD` at the end of the death animation in co-op; verify in `--local2` that one Keen dying leaves the other playing
- [x] 2.2 Evaluate success and failure after each frame (no alive + any exited = success, no alive = fail) and restart the level on fail; verify replays for: one exits then partner dies (success), one dies then partner exits (success), both die (restart)
- [x] 2.3 Copy `gs` at level start and restore it on fail or leave; verify a replay that collects the battery in level 3 and then fails shows the battery back in the level and the score unchanged
- [x] 2.4 Make dead and exited players' cameras follow the lowest-numbered living Keen; verify by screenshot that the dead player's view follows the partner
- [x] 2.5 Add the restart vote (hold R 0.5 s locally emits `RESTART_TOGGLE`; all votes = fail) and the Esc-menu return-to-map vote (`MAP_TOGGLE`; all votes = leave without marking done); verify replays for both votes, a withdrawn vote, and a solo co-op restart
- [x] 2.6 End the level for everyone on the in-level teleporter and the castle switch; verify a replay where player 1 takes the teleporter puts both Keens at the landing spot
- [x] 2.7 Skip lives, the "Keens left" window and extra-life awards in co-op, and disable cheats; verify ten deaths in a row keep the game going and G+O+D does nothing
- [x] 2.8 Play the ending for every player when all four parts are held after a success; verify with a replay starting from a state with three parts
- [x] 2.9 Document the co-op level rules and the R / Esc votes in `README.md`; verify the README matches the behaviour in `--local2`

## 3. Messages

- [x] 3.1 Add the banner table to game state (included in snapshots and checksum) and draw banners after the playfield; verify a banner shows on both players' frames in a two-player screenshot replay
- [x] 3.2 Show vote and map-request banners from the vote and request state; verify they appear and clear as the specs describe
- [x] 3.3 Turn the statue message into a 5-second banner in co-op; verify player 2 keeps moving while player 1's statue banner is up
- [x] 3.4 Turn the castle switch shake into a game-state timer with a local render jitter and a banner; verify the level ends after the shake in a replay and the game RNG checksum matches with and without drawing
- [x] 3.5 Make the Space status screen a local overlay in co-op that sends neutral input while open; verify the partner's game continues while it is open
- [x] 3.6 Keep the original blocking windows when `game.coop` is clear; verify the single-player statue window still waits for a key

## 4. World map

- [x] 4.1 Run every player's Keen on the map with its own camera and draw all Keens with colours and tags; verify by screenshot in `--local2`
- [x] 4.2 Add `map_req` (request, replace, cancel when the requester leaves, confirm) and start the level when every player has confirmed, at once in a one-player game; verify replays for request, confirm, cancel and replace
- [x] 4.3 Draw the request banner and the off-screen arrow pointing at the requester; verify by screenshot with the requester off screen
- [x] 4.4 Return every Keen to the level entrance (or teleporter landing spot) after a level and clear any request; verify after success and after a return-to-map vote
- [x] 4.5 Turn `teleport_anim` into a per-player timed teleport state; verify player 2 keeps walking during player 1's teleport and the tiles animate the same as before
- [x] 4.6 Make the ship screen a local overlay; verify the partner keeps walking while it is open
- [x] 4.7 In co-op, allow F1 and F3 only on the map, bracketed by `PAUSE_ON`/`PAUSE_OFF` with a "PLAYER n IS READING HELP" banner, and keep F2 local; verify F1 in a level does nothing and F1 on the map pauses both
- [x] 4.8 Document the map handshake and the per-player map specials in `README.md`; verify against `--local2`

## 5. Integration check

- [x] 5.1 Play levels 1 to 4 in `--local2` from a new co-op game through the map handshake, recording a replay; verify the replay reproduces the same checksums and ends with levels 1 to 4 done

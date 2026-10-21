# Design

## Context

After `multiplayer-engine-core`, `play_level` runs several Keens in one deterministic game with a
player array, per-player cameras and per-frame inputs from an input source. The control flow is
still the original's:

- `play_level` returns when `level_result` is set (exit door 1, in-level teleporter 2, castle
  switch 3) or when `objs[0].type == 0` (Keen's death animation left the screen). `play_game`
  then subtracts a life or marks the level done and returns to the map loop.
- The exit walk sets `type = 0` and `level_result = 1` as soon as Keen passes the door.
- Blocking windows inside the loop: `statue_message()`, the castle switch shake loop in
  `switch_activate()`, the Space status screen and F1/F3 via `check_hotkeys()`, the ship screen in
  `map_special()`, and `teleport_anim()` (a 16-frame loop that also changes tiles).
- On the map, `map_keen` sets `sel_level` from a button press over an entrance, and `play_game`
  starts that level straight away.

Everything that changes the game must flow through per-frame inputs, so that later two browsers
running in lockstep stay identical. Anything only one player sees must stay out of game state.

## Goals / Non-Goals

**Goals:**
- Implement the co-op rules in `specs/` as part of the deterministic game, testable offline with
  `--local2` and replays.
- Keep the original single-player flow working for the non-co-op development build.

**Non-Goals:**
- Network transport, lobby, joining, disconnects: `online-lobby-netplay`.
- Saving and high scores: `server-saves-deploy`.

## Decisions

### A co-op flag on the game
`game.coop` is set for every game started from the lobby (and by `--local2` / `--coop` in
development). It switches on: player status and the new end conditions, unlimited lives, votes,
the map handshake, banners instead of windows, and cheats off. With it clear, `play_game` and
`play_level` behave as in `keen-clone`.

### Player status replaces the "Keen gone" check
`player_t.status` is `ALIVE`, `DEAD` or `EXITED`. In co-op the exit walk sets `EXITED` instead of
`level_result = 1`, and the end of the death animation sets `DEAD` instead of ending the level.
After the object loop each frame, `play_level` evaluates:
`no ALIVE && any EXITED -> success`, `no ALIVE -> fail`, `all restart votes -> fail`,
`all map votes -> leave`. Results 2 (teleporter) and 3 (castle) end the level for all, as before.
A dead or exited player's camera follows the lowest-numbered living Keen.

### Rollback uses a copy of `gs` taken at level start
`play_level` copies `gs` on entry. On fail or leave it copies it back, which covers score, ammo,
keys, ship parts, pogo and done flags, and makes the original "lose the ship part when you die in
levels 3/4/8/16" rule unnecessary in co-op. `last_extra` is reset from the restored score.

### Votes and pauses travel as input bits
The per-frame input gains one-frame event bits: `RESTART_TOGGLE`, `MAP_TOGGLE`, `PAUSE_ON` and
`PAUSE_OFF`. The local client decides when to emit them: holding R for 0.5 s (counted locally in
real frames) emits `RESTART_TOGGLE`, the local Esc menu's "return to map" emits `MAP_TOGGLE`, and
opening or closing F1/F3 on the map emits the pause bits. The game applies them deterministically,
so every machine sees the same votes on the same frame.
- Alternative: separate control messages outside the input stream. Rejected: they would need their
  own ordering against frames, which the input stream already provides.

### Banners are game state; overlays are local
A small banner table (text id, arguments, frames left) lives in the game state and is drawn after
the playfield, so every player sees the same banners at the same time and a joiner's snapshot
includes them. Vote and request banners are derived each frame from the vote and request state,
not queued. Personal overlays (status screen, ship screen, Esc menu) are local UI only: while one is
open, that player's input source sends neutral movement, so their Keen stands still.

### The level-start handshake is game state
`map_req` holds `{requester, level, confirmed mask}`. A button press edge on an entrance either
creates or replaces the request, or confirms it. The request clears when the requester's Keen no
longer overlaps the entrance. When the confirmed mask covers all players, `play_game` starts the
level. In a game with one player the first press confirms immediately. The off-screen arrow is
drawn locally from the requester's position.

### Blocking loops become timed states
- `teleport_anim` becomes a per-player teleport state with a frame counter. It sets the same tiles
  on the same frames, and the Keen is frozen while it runs, but the game loop keeps going for others.
- The castle switch shake becomes a game-state timer (the original's 80 frames). The screen jitter
  is a local render offset from a local random source, so it never touches the game RNG. The level
  ends when the timer runs out.
- The statue message becomes a 5-second banner.
- In co-op, F1/F3 still run their original screens locally, bracketed by `PAUSE_ON`/`PAUSE_OFF`.
  While any pause is active the game does not advance and other players see the banner.

### Ending and lives
In co-op the "Keens left" window, lives decrement and extra-life award are skipped. The ending runs
locally for each player once the shared record has all four parts after a success. What happens
afterwards (back to the lobby, high scores) belongs to the later changes.

## Risks / Trade-offs

- **[Local timers emitting game events]** Holding R for 0.5 s is measured in local real time, so the
  vote may land on slightly different frames for different players. → Only the emitted bit matters
  to the game, and it travels in the input stream like a key press.
- **[Status overlay leaves Keen standing]** A player checking the status screen mid-level is
  defenceless. → Matches what a player expects from opening a menu without pausing. It is their
  choice to open it.
- **[Pause while both on the map]** Two players opening help at once. → The pause count is per
  player, and the game resumes only when every pause bit is cleared.
- **[Both Keens start on the same spot]** The level start marker holds one Keen. → Keens pass
  through each other, and the P1/P2 tags keep them readable.

## Open Questions

- Exact banner wording and placement (top or bottom of the playfield) can be tuned while
  implementing without changing the specs.

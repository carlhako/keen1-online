# Proposal

## Why

With several Keens in one deterministic game (`multiplayer-engine-core`), the game still follows
single-player rules: a death ends the level, lives run out, and pop-ups freeze everything until
one person presses a key. Co-op needs its own rules for who must reach the exit, what happens on a
death, how the pair agree on a level, and messages that do not stop the partner's game.

## What Changes

- Co-op games have unlimited lives and no lives counter. Score, ammo, keycards, ship parts and the
  pogo stay shared, as the engine already does.
- Each player is alive, dead or exited. Dead and exited players watch their partner.
- A level succeeds once nobody is alive and at least one player reached the exit; both players
  then return to the world map at that level's entrance.
- A level fails when every player is dead, or when all players vote to restart. It restarts at
  once, with score and items rolled back to how they were when the level started.
- A "return to map" vote from the Esc menu leaves the level for everyone, also rolling back.
- On the world map, a button press on an entrance sends a request shown to both players, with an
  arrow toward the requester; the level starts when the partner presses on the same entrance.
- The ship's parts screen and the status screen open only for the player who asked, and the
  statue and castle messages become banners. None of them pause the game.
- Map teleporters move only the player who used them, without stopping the other.
- In co-op, F1 help and F3 key setup pause both players and work only on the world map. Cheats are
  off.
- A game with one seat filled (solo) follows the same rules, so a partner can join at any time.

## Capabilities

### New Capabilities

- `coop-level-flow`: player status in a level, success and failure, restart and return-to-map
  votes, rollback of shared items, watching the partner, and the ending.
- `coop-world-map`: two Keens on the world map, the level-start handshake, and map specials
  (ship, teleporters) acting per player.
- `game-messages`: banners and per-player overlays that replace the original's blocking windows
  during play.

### Modified Capabilities

None. The co-op rules build on `multiplayer-actors` and `deterministic-simulation` from
`multiplayer-engine-core` without changing them.

## Impact

- `src/engine.c` (`play_level` end conditions, statue and castle switch, status screen),
  `src/menus.c` (`play_game`, `map_keen`, `map_special`, `teleport_anim`, `level_teleport`,
  `ending`), `src/ui.c` (banner and overlay drawing).
- Depends on `multiplayer-engine-core`. Verified with its local two-player mode and replay harness
  before `online-lobby-netplay` connects two browsers.

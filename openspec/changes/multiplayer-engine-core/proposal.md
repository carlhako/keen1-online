# Proposal

## Why

Keen 1 online co-op lives in a new repository, `keen1-online`, forked from the web build of the
Keen 1 port so the original reverse-engineering repository (`keen-clone`) stays a faithful
single-player port. Before any networking, the engine has to run the same on two browsers and
carry more than one Keen. Today it assumes a single Keen in `objs[0]`, and its speed and enemy AI
follow the wall clock, so two machines would drift apart within seconds.

## What Changes

- Create the `keen1-online` repository from the web-build branch (`claude/funny-lumiere-19c88b`)
  of `keen-clone`, with the shareware data and the OpenSpec setup and changes carried over.
- The game advances in fixed steps of 6 ticks (about 24.3 frames per second, the speed the
  original already runs at on any modern machine), regardless of how fast frames are drawn.
- All randomness comes from a seeded generator, and no game logic reads the wall clock
  (animation frames that affect hitboxes count as game logic).
- Game state can be captured as a snapshot, restored from one, and summarised as a checksum.
- The engine carries up to 2 Keens (built for N), each with its own input, facing, invulnerability,
  camera and world-map position. Enemies react to the nearest living Keen. Objects activate when
  any player's camera can see them.
- Player 2's Keen is drawn in a second colour scheme and each Keen carries a small P1/P2 tag
  when more than one player is present.
- A local two-player development mode (second key set in one window) and a headless replay test
  so this change can be verified without a server.
- Single player keeps playing as it does today.

## Capabilities

### New Capabilities

- `deterministic-simulation`: fixed-step game advance, seeded randomness, state snapshot/restore and
  checksums, so identical inputs give identical games on any machine.
- `multiplayer-actors`: several Keens in one level or on one world map, each with its own input
  and camera, enemies targeting the nearest Keen, and how each Keen is drawn and labelled.

### Modified Capabilities

None. `keen1-online` starts with no specs.

## Impact

- New repository `keen1-online` (GitHub `carlhako/keen1-online`), upstream remote pointing at
  `carlhako/keen-clone`.
- `src/engine.c`, `src/actors.c`, `src/menus.c`, `src/game.h`: player array instead of `objs[0]`
  and the `keen_*` / `ctrl` globals; fixed tics; RNG and tick counter in game state.
- `src/platform.c`: second key set for local testing, colour remap for player 2's sprites,
  KEEN_SCRIPT harness extended for two players.
- Later changes (`coop-game-rules`, `online-lobby-netplay`, `server-saves-deploy`) build on this.

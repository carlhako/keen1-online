# deterministic-simulation Specification

## Purpose

Makes the game advance identically on every machine given the same starting state and the same
per-frame inputs, so two browsers can run one shared game in lockstep.

## Requirements

### Requirement: Fixed simulation step
The game SHALL advance in fixed steps of 6 timer ticks (timer rate 1193182/8192 Hz) per simulated
frame, in levels and on the world map, independent of how long drawing a frame takes.

#### Scenario: Slow frame does not change the step
- **WHEN** a frame takes twice as long as usual to draw
- **THEN** the next simulated frame still advances the game by exactly 6 ticks

#### Scenario: Single-player speed unchanged
- **WHEN** a single player plays a level on a normal machine
- **THEN** Keen and enemies move at the same speed as in the original port (about 24 frames per second)

### Requirement: Seeded randomness
All random decisions made by game logic SHALL come from a generator whose state is part of the game
state and can be set from a seed.

#### Scenario: Same seed, same enemy behaviour
- **WHEN** two runs start the same level with the same seed and receive the same inputs
- **THEN** every enemy makes the same choices on the same frames in both runs

### Requirement: No wall-clock input to game logic
Game logic, including animation frames that decide hitboxes or behaviour, SHALL depend only on the
game state and the per-frame inputs, never on real time.

#### Scenario: Animation driven by game time
- **WHEN** a run is replayed at a different real-time speed (for example headless, as fast as possible)
- **THEN** every object shows the same sprite on the same frame as in the original run

### Requirement: Game state snapshot and restore
The game SHALL capture its complete state (objects, special objects, level tiles, shared game
record, players, cameras, random generator, frame counter) as a snapshot and restore it later.

#### Scenario: Restore continues identically
- **WHEN** a snapshot taken at frame N is restored and the same inputs are fed from frame N onward
- **THEN** the game continues exactly as the run that produced the snapshot

### Requirement: Game state checksum
The game SHALL compute a checksum of its state each frame that changes whenever any part of the
captured state changes.

#### Scenario: Identical runs agree
- **WHEN** two runs receive identical seeds and inputs
- **THEN** their checksums are equal on every frame

#### Scenario: Divergence is detected
- **WHEN** one run's state differs from the other's in any captured field
- **THEN** the checksums for that frame differ

### Requirement: Recorded input replay
The game SHALL be able to run headless from a recorded list of per-frame inputs for each player and
report the checksum of each frame.

#### Scenario: Replay is reproducible
- **WHEN** the same recording is replayed twice
- **THEN** both replays report the same checksum sequence

# Spec Delta

## MODIFIED Requirements

### Requirement: Partner sounds heard on screen
A player SHALL hear the sounds of their own Keen, and the sounds of other Keens, enemies, shots
and cannons only while their source is on that player's screen. A Keen dying, a door opening
and a Keen walking into the exit door SHALL be heard by every player wherever they happen.
Sound is local output, never game state. "On screen" SHALL mean inside the view that player
actually sees, which is wider than the original 320 pixels when their window is wide.

#### Scenario: Partner collects off screen
- **WHEN** player 2 collects a candy bar far outside player 1's screen
- **THEN** player 1 does not hear it, and player 2 does

#### Scenario: Partner dies off screen
- **WHEN** player 2's Keen dies outside player 1's screen
- **THEN** player 1 hears the death sound

#### Scenario: Partner in the extra columns of a wide view
- **WHEN** player 2 collects a candy bar inside player 1's 480-wide view but outside the original 320-wide area
- **THEN** player 1 hears it

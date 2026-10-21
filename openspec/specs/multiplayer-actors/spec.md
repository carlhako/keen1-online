# multiplayer-actors Specification

## Purpose

Lets one level or one world map hold several Keens, each controlled by its own player with its own
view, and defines how enemies and the level react to more than one Keen.

## Requirements

### Requirement: Multiple Keens in one game
The game SHALL support at least 2 players at once, each controlling a separate Keen with its own
input, facing, invulnerability timer, movement state and world-map position. Single player SHALL be
the same game with one player.

#### Scenario: Two Keens move independently
- **WHEN** player 1 holds right and player 2 holds left in the same level
- **THEN** player 1's Keen walks right and player 2's Keen walks left

#### Scenario: Single player unchanged
- **WHEN** a game has one player
- **THEN** the level plays as in the original port, with no second Keen and no player tags

### Requirement: Per-player camera
Each player SHALL have a camera that follows their own Keen, and each player's screen SHALL be
drawn from their own camera.

#### Scenario: Players far apart
- **WHEN** the two Keens are at opposite ends of a level
- **THEN** each player's screen shows their own Keen's surroundings

### Requirement: Objects activate for any player
An object SHALL become active when it enters any player's camera view, and SHALL stay active while
any player's camera can see it, using the same view margins as the original.

#### Scenario: Enemy wakes for the far player
- **WHEN** a Garg comes into view of player 2 only
- **THEN** the Garg wakes and starts moving, on both players' games

### Requirement: Enemies react to the nearest living Keen
Every enemy decision that the original makes relative to Keen's position SHALL use the nearest
living Keen, measured horizontally, with ties going to the lower player number.

#### Scenario: Garg charges the closer Keen
- **WHEN** a Garg looks for Keen with player 1 four tiles away and player 2 ten tiles away
- **THEN** the Garg charges toward player 1

#### Scenario: Dead Keen ignored
- **WHEN** the nearest Keen is dying or has left the level
- **THEN** enemies react to the other Keen instead

### Requirement: Tile interactions per Keen
Hazards, pickups, doors, switches, the exit door and teleporters SHALL react to whichever Keen
touches them, and an item collected by any Keen SHALL go into the one shared game record.

#### Scenario: Partner picks up a keycard
- **WHEN** player 2 touches a yellow keycard
- **THEN** the keycard is removed from the level and the shared record shows the yellow card for both players

#### Scenario: Hazard kills only the Keen touching it
- **WHEN** player 1 touches a fire tile
- **THEN** player 1's Keen dies and player 2's Keen is unaffected

### Requirement: Keens do not collide with each other
Keens SHALL pass through each other, and a Keen's raygun shot SHALL not hit another Keen.

#### Scenario: Shot passes partner
- **WHEN** player 1 fires while player 2 stands in the line of fire
- **THEN** the shot passes player 2 and continues as if player 2 were not there

### Requirement: Players drawn distinctly
When a game has more than one player, player 2's Keen SHALL be drawn in a second colour scheme on
the map and in levels, and each Keen SHALL carry a small P1 or P2 tag above it.

#### Scenario: Two players visible
- **WHEN** both Keens are on screen in a two-player game
- **THEN** they are drawn in different colours, tagged P1 and P2

### Requirement: Local two-player development mode
A development option SHALL start a two-player game in one window with a second key set for
player 2, the screen following player 1.

#### Scenario: Second key set controls player 2
- **WHEN** the game is started in local two-player mode and the player 2 keys are pressed
- **THEN** player 2's Keen moves while player 1's Keen follows the first key set

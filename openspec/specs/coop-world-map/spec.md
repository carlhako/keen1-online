# coop-world-map Specification

## Purpose

Defines how players share the world map in a co-op game: each walks their own Keen, they agree on
a level by both pressing at its entrance, and map specials act on one player at a time.

## Requirements

### Requirement: Keens on the world map
Each player SHALL walk their own Keen on the world map with their own camera. Every Keen SHALL be
drawn on every player's screen, and Keens SHALL pass through each other.

#### Scenario: Partner visible on the map
- **WHEN** player 2's Keen is within player 1's view on the map
- **THEN** player 1 sees player 2's Keen in player 2's colours with its P2 tag

### Requirement: Level start request
When a player presses jump or pogo while standing on the entrance of a level that is not done, and
no other player has requested that same level, a start request for that level SHALL be made.

#### Scenario: First press makes a request
- **WHEN** player 1 presses jump on the entrance of level 4 and player 2 is elsewhere
- **THEN** a request by player 1 for level 4 is made and the level does not start yet

#### Scenario: Completed level
- **WHEN** a player presses jump on the entrance of a level marked done
- **THEN** no request is made, as in the original

### Requirement: Request banner and pointer
While a start request is pending, every player SHALL see a banner naming the requesting player
and the level, and every other player SHALL see an arrow at their screen edge pointing toward the
requester's Keen whenever it is off their screen.

#### Scenario: Partner sees the request
- **WHEN** player 1 makes a request for level 4
- **THEN** both players see "PLAYER 1 WANTS TO PLAY LEVEL 4" and player 2 sees an arrow toward player 1 if player 1 is off screen

### Requirement: Level starts when every player confirms
A requested level SHALL start for every player once every player in the game has pressed jump or
pogo on that same entrance while the request is pending.

#### Scenario: Partner confirms
- **WHEN** player 1 has requested level 4 and player 2 presses jump on level 4's entrance
- **THEN** level 4 starts for both players

#### Scenario: Solo game
- **WHEN** a co-op game has one player and that player presses jump on an entrance
- **THEN** the level starts at once

### Requirement: Request cancelled or replaced
A pending request SHALL be cancelled when the requesting player's Keen leaves that entrance, and
SHALL be replaced when another player makes a request for a different level.

#### Scenario: Requester walks away
- **WHEN** player 1 has requested level 4 and walks off its entrance
- **THEN** the request and its banner disappear

#### Scenario: Partner suggests another level
- **WHEN** player 1 has requested level 4 and player 2 presses jump on level 7's entrance
- **THEN** the request becomes player 2's request for level 7

### Requirement: Return to the map after a level
After a level succeeds or is left, every player's Keen SHALL appear on the world map at that
level's entrance (or at the teleporter landing spot after the in-level teleporter), with any
pending request cleared.

#### Scenario: Back on the map together
- **WHEN** level 4 succeeds
- **THEN** both Keens stand at level 4's entrance, now shown as done

### Requirement: Map teleporters act per player
A world-map teleporter SHALL move only the Keen that used it, and its animation SHALL not stop
other players from moving.

#### Scenario: Teleporting alone
- **WHEN** player 1 uses a teleporter while player 2 walks on the map
- **THEN** player 1's Keen moves to the destination and player 2 keeps walking throughout

### Requirement: Ship screen per player
Pressing a button at the ship SHALL show the ship parts screen only to the player who pressed, as
an overlay that does not pause the game, closing on that player's next button press.

#### Scenario: Partner keeps walking
- **WHEN** player 1 opens the ship screen
- **THEN** player 1 sees the parts collected so far while player 2 keeps walking on the map

### Requirement: Help and key setup pause everyone on the map
In a co-op game, F1 help and F3 key setup SHALL only open on the world map, and SHALL pause every
player with a banner naming who opened them until they close. F2 sound toggles only for the
player who pressed it.

#### Scenario: Help on the map
- **WHEN** player 1 presses F1 on the world map
- **THEN** player 1 sees the help, and player 2's game pauses with "PLAYER 1 IS READING HELP"

#### Scenario: Help in a level
- **WHEN** a player presses F1 during a co-op level
- **THEN** nothing opens and the game continues

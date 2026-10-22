# server-saves Specification

## Purpose

Lets players keep a shared online game's progress on the server and start a new game from any save
the server holds.

## Requirements

### Requirement: Save from the world map
In an online game, pressing F5 on the world map SHALL ask the player who pressed it for a save name
(up to 20 characters, defaulting to the game number and levels done) and store the shared game
record on the server. F5 SHALL do nothing in a level, as in the original.

#### Scenario: Save during co-op
- **WHEN** player 2 presses F5 on the world map and types "mars run"
- **THEN** a save named "mars run" holding the current score, items, completed levels and map position appears in the server's save list

#### Scenario: F5 in a level
- **WHEN** a player presses F5 during a level
- **THEN** nothing is saved

### Requirement: Partner paused while saving
While a player types a save name, every other player in the game SHALL be paused with a banner
naming who is saving, until the save is stored or cancelled.

#### Scenario: Partner waits
- **WHEN** player 1 opens the save prompt
- **THEN** player 2 sees "PLAYER 1 IS SAVING" and cannot move until player 1 finishes or cancels

### Requirement: Save confirmation
The saving player SHALL see whether the server stored the save, and a failed save SHALL leave the
game unchanged.

#### Scenario: Server refuses
- **WHEN** the server cannot store the save
- **THEN** the saving player sees "Save failed" and the game continues as before

### Requirement: Load game from the lobby
The lobby SHALL list every save on the server, newest first, with its name, date, levels done and
score. Loading one SHALL create a new game from that save on the world map, counted toward the
running-game limit, showing the waiting screen with Play single player.

#### Scenario: Load and wait
- **WHEN** a visitor loads "mars run"
- **THEN** a new game starts from that save's record and map position and waits for player 2

#### Scenario: Load at the limit
- **WHEN** 5 games are running and a visitor tries to load a save
- **THEN** the server refuses it as for any new game

### Requirement: Saves kept and validated
The server SHALL keep saves until they are deleted by hand, and SHALL refuse a save whose record is
not a valid Keen 1 game record (wrong size, or values outside the original's ranges).

#### Scenario: Malformed save
- **WHEN** a client sends a save whose completed-level flags are not 0 or 1
- **THEN** the server refuses it and stores nothing

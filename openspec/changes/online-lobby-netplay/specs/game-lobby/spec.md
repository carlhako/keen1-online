# Spec Delta

## Purpose

Lets visitors to the site see the running games, start a new one, or join one with an open seat,
within a limit on how many games the server runs at once.

## ADDED Requirements

### Requirement: Lobby is the start page
Opening the site SHALL show the lobby: every running game with its number, how many of its 2
seats are filled, and whether it is on the world map or in a level (with the level number).
The list SHALL update while the lobby is open.

#### Scenario: Two games running
- **WHEN** one game has 2 players in level 4 and another has 1 player on the world map
- **THEN** the lobby lists both, as "2/2, level 4" and "1/2, world map"

### Requirement: New game
The lobby SHALL offer **New game**. Choosing it SHALL create a game with the visitor as player 1,
starting a new game on the world map, and show "Waiting for player 2" with a **Play single
player** button.

#### Scenario: Start waiting
- **WHEN** a visitor chooses New game
- **THEN** a new game appears in everyone's lobby as 1/2 and the visitor sees the waiting screen

#### Scenario: Play single player
- **WHEN** the waiting player chooses Play single player
- **THEN** the game starts on the world map with one Keen, and its seat stays open for others to join

#### Scenario: Partner arrives while waiting
- **WHEN** a second visitor joins a game whose player 1 is still on the waiting screen
- **THEN** the game starts for both on the world map

### Requirement: Join a game with an open seat
The lobby SHALL offer **Join** for every game with an open seat. Joining SHALL make the visitor
player 2 of that game, whether it is waiting, being played solo, or paused after a disconnect.

#### Scenario: Join a solo game
- **WHEN** a visitor joins a game being played solo in level 6
- **THEN** the visitor becomes player 2 in level 6, with their Keen appearing on top of player 1's

#### Scenario: Full game
- **WHEN** a game has 2 players
- **THEN** the lobby shows it without a Join option

### Requirement: Running-game limit
The server SHALL run at most `maxGames` games at once (default 5). While the limit is reached,
**New game** SHALL be disabled with a "Server full (5/5)" message and a retry option. Joining
games with open seats SHALL still work.

#### Scenario: Sixth game
- **WHEN** 5 games are running and a visitor tries to start a new game
- **THEN** the server refuses it and the visitor sees "Server full (5/5)"

#### Scenario: Slot frees up
- **WHEN** one of the 5 games ends and the visitor retries
- **THEN** the new game is created

### Requirement: Leaving to the lobby
A player SHALL be able to leave a game from the Esc menu and return to the lobby. Their seat then
opens as for a disconnect.

#### Scenario: Leave mid-level
- **WHEN** player 2 chooses Leave game in a level
- **THEN** player 2 sees the lobby, and player 1's game pauses as for a disconnect

### Requirement: Game ends after the ending
After the ending has played, the game SHALL end and its players SHALL return to the lobby.

#### Scenario: Finished game
- **WHEN** both players have watched the ending
- **THEN** the game disappears from the lobby and both players see the lobby

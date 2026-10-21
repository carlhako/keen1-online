# Spec Delta

## Purpose

Defines how a co-op level plays out for the players in it: who is alive, when the level is won or
lost, how players agree to restart or leave, and what happens to shared items when a level is lost.

## ADDED Requirements

### Requirement: Unlimited lives in co-op
Co-op games, including a co-op game with only one seat filled, SHALL have no lives counter: losing
a level never ends the game, and score-based extra lives SHALL not be shown.

#### Scenario: Many deaths
- **WHEN** both players die in the same level ten times in a row
- **THEN** the level restarts each time and the game continues

### Requirement: Player status in a level
Each player in a level SHALL be alive, dead or exited. A player becomes dead when their Keen's
death animation finishes, and exited when their Keen walks through the exit door.

#### Scenario: Keen dies while partner plays on
- **WHEN** player 1's Keen is killed by a Garg while player 2 is alive
- **THEN** player 1 becomes dead and player 2 keeps playing

### Requirement: Watching the partner
A dead or exited player's screen SHALL follow a Keen that is still alive, until the level ends.

#### Scenario: Exited player watches
- **WHEN** player 2 has exited and player 1 is still alive
- **THEN** player 2's screen follows player 1's Keen

### Requirement: Level success
When no player is alive and at least one player has exited, the level SHALL succeed: it is marked
done, items and score collected in it are kept, and every player returns to the world map at that
level's entrance with a living Keen.

#### Scenario: One exits, partner dead
- **WHEN** player 1 is dead and player 2 walks through the exit door
- **THEN** the level is marked done and both players appear on the world map at its entrance

#### Scenario: One exits, partner dies afterwards
- **WHEN** player 1 has exited and player 2 then dies
- **THEN** the level succeeds

### Requirement: Level failure restarts the level
When every player is dead, the level SHALL fail and restart at once with every player alive at
the level start.

#### Scenario: Both die
- **WHEN** player 1 dies and later player 2 dies, with nobody exited
- **THEN** the same level restarts with both Keens at the start

### Requirement: Shared items roll back on failure
When a level fails, restarts by vote, or is left by vote, the shared game record (score, ammo,
keycards, ship parts, pogo, completed levels) SHALL return to its state at the start of that level.

#### Scenario: Ship part lost on restart
- **WHEN** a player collects the battery in level 3, earns 5000 points, and then the level fails
- **THEN** after the restart the battery is back in the level and the score is what it was when the level started

### Requirement: Restart vote
Any player, alive, dead or exited, SHALL be able to vote to restart the level by holding the
restart key (R by default) for half a second; doing so again withdraws the vote. When every player
in the game has voted, the level SHALL fail and restart.

#### Scenario: Both vote
- **WHEN** player 1 holds R and later player 2 holds R
- **THEN** the level restarts with shared items rolled back

#### Scenario: Vote withdrawn
- **WHEN** player 1 votes and then holds R again before player 2 votes
- **THEN** player 1's vote is withdrawn and the level continues

#### Scenario: Solo player restarts
- **WHEN** the only player in the game holds R
- **THEN** the level restarts at once

### Requirement: Return-to-map vote
The Esc menu SHALL offer a vote to return to the world map. When every player has voted, the level
SHALL end without being marked done, shared items SHALL roll back, and players SHALL return to the
map at that level's entrance.

#### Scenario: Both leave the level
- **WHEN** both players choose "return to map" from the Esc menu
- **THEN** both appear on the world map at the level's entrance and the level is not marked done

### Requirement: Votes are visible and reset
Pending votes SHALL be shown to every player as a banner naming who voted, and SHALL be cleared
whenever a level starts or ends.

#### Scenario: Banner for a pending vote
- **WHEN** player 2 votes to restart
- **THEN** both players see "PLAYER 2 WANTS TO RESTART" until the vote completes, is withdrawn, or the level ends

### Requirement: Level-ending events end it for everyone
The in-level teleporter and the castle switch in level 16 SHALL end the level for every player in
the same way as for the player who triggered them.

#### Scenario: Teleporter
- **WHEN** player 1 uses the in-level teleporter while player 2 is elsewhere in the level
- **THEN** the level ends and both players appear on the world map at the teleporter's landing spot

### Requirement: Ending shown to every player
When the shared record holds all four ship parts after a level succeeds, the ending SHALL play for
every player in the game.

#### Scenario: Last part collected by partner
- **WHEN** player 2 collects the last ship part and the level succeeds
- **THEN** both players see the ending

### Requirement: Cheats off in co-op
The original cheat key combinations SHALL have no effect in co-op games.

#### Scenario: God mode combination
- **WHEN** a player presses G, O and D together in a co-op level
- **THEN** nothing changes

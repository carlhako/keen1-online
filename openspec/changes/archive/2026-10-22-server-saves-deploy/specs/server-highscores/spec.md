# Spec Delta

## Purpose

Keeps one high score table for everyone who plays on the server, entered by team at the end of a
finished game.

## ADDED Requirements

### Requirement: One table on the server
The server SHALL keep a single high score table of 7 entries in the original's layout (score, name
up to 12 characters, ship parts held), initialised with the original's default entries, and
survive server restarts.

#### Scenario: Restart keeps scores
- **WHEN** the service restarts
- **THEN** the table shows the same entries as before

### Requirement: Team name entry after the ending
After the ending of an online game, if the shared score would enter the table, player 1 (or the only
remaining player) SHALL type a team name in the original's name entry, while other players see
"PLAYER 1 IS ENTERING YOUR TEAM NAME". The entry SHALL then be stored on the server.

#### Scenario: Qualifying co-op score
- **WHEN** a two-player game finishes with a score above the table's lowest entry
- **THEN** player 1 types "carl & sam" and the table gains that entry with the game's score and ship parts

#### Scenario: Score too low
- **WHEN** the finished game's score is below every entry
- **THEN** no name is asked for

### Requirement: Table shown after entry and in the lobby
After the name entry (or straight after the ending when the score did not qualify), every player in
the game SHALL see the table before returning to the lobby, and the lobby SHALL show the current
table.

#### Scenario: Both see the table
- **WHEN** player 1 finishes entering the team name
- **THEN** both players see the updated table, then the lobby

### Requirement: Score submissions validated
The server SHALL accept a high score only from a game that sent its ending, with a score no higher
than the score in that game's last agreed checkpoint or frame report, and a name of printable
characters up to 12 long.

#### Scenario: Forged score
- **WHEN** a connection submits a score for a game that never reached the ending
- **THEN** the server refuses it and the table is unchanged

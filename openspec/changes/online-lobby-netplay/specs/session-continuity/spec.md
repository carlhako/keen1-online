# Spec Delta

## Purpose

Defines what happens to a game when a player disconnects, stalls, comes back or leaves, so a
dropped connection never loses the game for the player who is still there.

## ADDED Requirements

### Requirement: Disconnect pauses the game
When a player's connection closes, or no input arrives from them for 15 seconds, the server SHALL
remove them from the game and open their seat. The remaining player's game SHALL pause with
"Player 2 disconnected" and a **Continue single player** button.

#### Scenario: Partner's browser closes
- **WHEN** player 2 closes their tab during a level
- **THEN** player 1's game pauses and shows "Player 2 disconnected" with Continue single player

### Requirement: Continue single player
Choosing **Continue single player** SHALL resume the game with the remaining player alone under the
co-op rules. The departed player's Keen SHALL be removed. If nobody is left alive in the level, the
level ends by the usual co-op rules.

#### Scenario: Continue alone in a level
- **WHEN** player 1 continues single player after player 2 disconnected mid-level
- **THEN** the level resumes with only player 1's Keen, and the game shows 1/2 in the lobby

#### Scenario: Remaining player was dead
- **WHEN** player 1 was already dead and player 2, the last living Keen, disconnected, and player 1 continues
- **THEN** the level fails and restarts with player 1 alive

### Requirement: Rejoin and join after a disconnect
An opened seat SHALL be joinable from the lobby by anyone, including the player who left. A browser
that left or lost a game SHALL see that game marked **Rejoin** in the lobby. A join while the
remaining player is paused SHALL resume the game with both players.

#### Scenario: Dropped player returns
- **WHEN** player 2's connection drops and they reload the site
- **THEN** their former game is marked Rejoin, and choosing it puts them back as player 2 with their Keen on top of player 1's

#### Scenario: Someone else takes the seat
- **WHEN** player 1 has continued single player and a new visitor joins
- **THEN** the visitor becomes player 2 as in any mid-game join

### Requirement: Stalled player
While a browser has stopped sending inputs (for example a hidden tab), the other player's game SHALL
wait and show "Waiting for player 2", and SHALL resume by itself when inputs arrive again within 15
seconds.

#### Scenario: Partner switches tabs briefly
- **WHEN** player 2 switches to another tab for 5 seconds
- **THEN** player 1 sees "Waiting for player 2" and the game resumes when player 2 returns

### Requirement: Empty game kept briefly
When the last player leaves a game, the server SHALL keep it for 60 seconds, listed in the lobby as
0/2 and joinable, and then remove it. A game removed this way SHALL no longer count toward the
running-game limit.

#### Scenario: Both drop and one returns
- **WHEN** both players' connections drop and player 1 reloads within 60 seconds
- **THEN** player 1 can rejoin the game where it was

#### Scenario: Nobody returns
- **WHEN** 60 seconds pass with no player in a game
- **THEN** the game disappears from the lobby

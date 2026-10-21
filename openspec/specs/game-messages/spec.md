# game-messages Specification

## Purpose

Replaces the original's blocking message windows during co-op play with banners and per-player
overlays, so one player reading a message never freezes the other player's game.

## Requirements

### Requirement: Banners do not pause play
In co-op games, banners SHALL be drawn over the playfield without pausing the game or taking
input, and SHALL disappear after their time or when the condition they describe ends.

#### Scenario: Playing under a banner
- **WHEN** a banner is shown and a player holds right
- **THEN** that player's Keen keeps walking right

### Requirement: Statue message as a banner
Touching a statue in a co-op level SHALL show the statue's message as a banner to every player for
about five seconds, instead of a window that waits for a key.

#### Scenario: Partner keeps moving
- **WHEN** player 1 touches a statue
- **THEN** both players see its message while player 2 keeps playing

### Requirement: Castle switch message
Pulling the switch in level 16 in a co-op game SHALL shake every player's screen and show its
message to every player before the level ends, without waiting for a key.

#### Scenario: Castle switch
- **WHEN** player 2 pulls the castle switch
- **THEN** both screens shake and show the message, then the level ends for both

### Requirement: Status screen as a personal overlay
In co-op games, the status screen (Space) SHALL open only for the player who pressed it, as an
overlay that does not pause the game, and close on that player's next Space or button press.

#### Scenario: Status during play
- **WHEN** player 1 presses Space in a level while player 2 is fighting a Vorticon
- **THEN** player 1 sees the status overlay and player 2's game continues unchanged

### Requirement: Single-player windows unchanged outside co-op
Outside co-op games (for example the local single-player development build), the original
blocking windows SHALL behave as before.

#### Scenario: Classic statue window
- **WHEN** a non-co-op game touches a statue
- **THEN** the original window appears and waits for a key

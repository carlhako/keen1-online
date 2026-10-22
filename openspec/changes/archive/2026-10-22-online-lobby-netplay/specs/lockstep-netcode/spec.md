# Spec Delta

## Purpose

Keeps every browser in a game running the identical game by exchanging per-frame inputs through
the server, and recovers when a browser's game drifts or a new browser joins.

## ADDED Requirements

### Requirement: WebSocket transport with versioned handshake
Clients SHALL connect over WebSocket at `/ws`. The first client message SHALL be `hello` with the
protocol version and the game build id. The server SHALL reply `welcome`, or `reject` with reason
`version` when either differs from the server's, and then close the connection.

#### Scenario: Outdated page
- **WHEN** a browser still running an older build connects after the server was upgraded
- **THEN** it receives `reject` with reason `version` and the page asks the player to reload

### Requirement: Shared seed and start frame
When a game starts, the server SHALL choose a random seed and send it with the start frame to
every player, and each browser SHALL start its game from that seed.

#### Scenario: Both start alike
- **WHEN** a game starts with two players
- **THEN** both browsers receive the same seed and their first-frame checksums match

### Requirement: Input relay
Each browser SHALL send its player's input for every frame, tagged with the frame number. The
server SHALL forward each input to every other player in the game, in frame order.

#### Scenario: Partner's input arrives
- **WHEN** player 1 sends the input for frame 500
- **THEN** player 2 receives player 1's input for frame 500

### Requirement: Input delay
A browser SHALL schedule its local input for the frame 2 frames ahead of the one it is simulating
(about 80 ms), and SHALL simulate a frame only once it has every player's input for that frame.

#### Scenario: Normal latency
- **WHEN** the partner's inputs arrive within 80 ms
- **THEN** the game runs at full speed without stalling

#### Scenario: Late input
- **WHEN** the partner's input for the next frame has not arrived
- **THEN** the game waits on that frame and, after half a second, shows "Waiting for player 2"

### Requirement: Input validation
The server SHALL drop malformed input messages, inputs for frames already relayed or more than 120
frames ahead, and more than 60 input messages per second from one connection. It SHALL close
connections that repeatedly send malformed messages.

#### Scenario: Flooding client
- **WHEN** a connection sends 200 input messages in one second
- **THEN** the server relays at most 60 of them and the other player's game is unaffected

### Requirement: Desync detection and resync
Each browser SHALL report its game checksum every 24 frames. When the reports for a frame
disagree, the server SHALL take a snapshot from the browser of the player who has been in the game
longest and load it into the other browsers, which then continue from that frame.

#### Scenario: Drifted game
- **WHEN** player 1 has been in the game longest and player 2's checksum for frame 2400 differs from player 1's
- **THEN** player 2's game is replaced by player 1's state at frame 2400 and both continue in step

### Requirement: Snapshot hand-off for joiners
When a player joins a running game, the game SHALL add the new player at an agreed frame, with
their Keen placed on top of the first player's Keen in the level or on the map. A browser already
in the game SHALL send the snapshot for that frame, and the joining browser SHALL start from it.

#### Scenario: Join mid-level
- **WHEN** a visitor joins a solo game at frame 9000 in level 6
- **THEN** both games contain two Keens from frame 9000, with player 2's Keen at player 1's position, and their checksums match

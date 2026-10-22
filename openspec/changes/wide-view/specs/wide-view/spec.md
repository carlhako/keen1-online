# Spec Delta

## Purpose

Fits the game picture to the browser window: levels and the world map show more of the world to
the sides, up to 1.5 times the original width, without changing how the game plays.

## ADDED Requirements

### Requirement: View width follows the window
During a level and on the world map, the picture SHALL be 200 game pixels tall and as wide as the
window allows, from 320 to 480 game pixels, with game pixels kept at the original 4:3 display
shape (5:6 width to height). The picture SHALL be scaled to fit the window and centered, with
black outside it. A resize SHALL take effect from the next drawn frame.

#### Scenario: 16:9 window
- **WHEN** a level is played in a 1920x1080 window
- **THEN** the view is about 427 game pixels wide and fills the window with no bars

#### Scenario: Ultrawide window
- **WHEN** a level is played in a 3440x1440 window
- **THEN** the view is 480 game pixels wide, fills the window's height, and has black bars left and right

#### Scenario: Narrow window
- **WHEN** a level is played in a window narrower than 4:3
- **THEN** the view is 320 game pixels wide, fills the window's width, and has black bars above and below

#### Scenario: Window resized mid-level
- **WHEN** the player makes the window wider during a level
- **THEN** the view widens on the next frame and the game goes on uninterrupted

### Requirement: View width never affects the game
The view width SHALL only change what is drawn and which sounds are heard. Camera movement and
limits, where enemies wake and fall asleep, where a falling Keen dies, and every other part of the
game state SHALL be the same at every view width. Players with different window sizes SHALL be
able to share one online game.

#### Scenario: Same replay at two widths
- **WHEN** one recorded game is replayed with the view 320 wide and again with it 480 wide
- **THEN** every frame's game state checksum is the same in both runs

#### Scenario: Partners with different windows
- **WHEN** one player of an online game has a 4:3 window and the other a 21:9 window
- **THEN** the game stays in step for both and neither is reported as out of sync

### Requirement: Wider view centered on the original camera
The extra width SHALL be split evenly to the left and right of the 320-pixel area the original
camera shows, and SHALL then be shifted only as far as needed to stay inside the area the original
camera could ever show. The view SHALL never show past that area.

#### Scenario: Middle of a level
- **WHEN** Keen is in the middle of a wide level with a 480-wide view
- **THEN** the view shows 80 extra pixels on each side of what the original would show

#### Scenario: Left edge of a level
- **WHEN** the camera is at the left limit of a level
- **THEN** the view's left edge is the original's left edge and all the extra width is on the right

### Requirement: Narrow levels centered
When the area the original camera could ever show is narrower than the view, that area SHALL be
centered in the view and the rest of the view SHALL be black.

#### Scenario: A 20-tile level
- **WHEN** level 2 is played with a 480-wide view
- **THEN** the level's 320 visible pixels are centered with 80 pixels of black on each side, and its border walls are not shown

### Requirement: Sleeping enemies drawn in place
An enemy that is asleep SHALL be drawn where it is, in its current pose and without moving, while
it is inside the view. It SHALL wake at the same distance as in the original.

#### Scenario: Enemy seen before it wakes
- **WHEN** Keen walks right in a 480-wide view and a sleeping Yorp comes into the right edge of the view
- **THEN** the Yorp is drawn standing still and starts to move only when it reaches the original wake distance

### Requirement: Fixed screens stay 320 wide
Screens that are not a view of a level or the world map (the title, story, help, order and
preview screens, menus shown full-screen, high scores, the ending and full-screen pictures) SHALL
be drawn 320 wide, centered, at the same scale as the view, with black on either side. Windows
drawn over a level or the world map (the status window, menus, messages, chat) SHALL be centered
in the view.

#### Scenario: Title screen in a wide window
- **WHEN** the title screen is shown in a 1920x1080 window
- **THEN** it is 320 game pixels wide, the same height as the level view, with black on both sides

#### Scenario: Status window over a wide level
- **WHEN** the player presses Space in a level with a 480-wide view
- **THEN** the status window is drawn in the middle of the view

### Requirement: Partner arrow at the edge of the wider view
The arrow that points to a partner Keen off screen SHALL appear only while that Keen is outside
the view actually drawn, and SHALL sit at that view's edge.

#### Scenario: Partner in the extra columns
- **WHEN** player 2's Keen is in the extra columns of player 1's 480-wide view
- **THEN** player 1 sees player 2's Keen and no arrow points to it

### Requirement: Page fills the window
The game page SHALL use the whole browser window. The lobby and the overlays shown over a game
(paused, finished, waiting for a player) SHALL cover the whole window.

#### Scenario: Lobby in a wide window
- **WHEN** the page is opened in a 1920x1080 window
- **THEN** the lobby's picture and panels cover the whole window, with no black bars at the sides

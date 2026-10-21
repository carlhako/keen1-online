#!/usr/bin/env python3
"""Writes the KEEN_SCRIPT for tests/run.sh's co-op playthrough: a new --local2 game from the
title screen, then levels 1 to 4, each started with the map handshake (both players press jump
on the entrance) and finished with both Keens at the exit door. The `warp` lines are test aids
(they are recorded into the replay); everything else is key presses."""
import sys

levels = [  # level, entrance tile, exit tile
    (1, (21, 39), (113, 8)),
    (2, (24, 30), (17, 5)),
    (3, (11, 23), (73, 10)),
    (4, (24, 25), (114, 6)),
]
out = []
def at(t, cmd):
    out.append((t, cmd))

at(5, "key 39 x")          # leave the intro
at(30, "key 39 x")         # leave the title page for the menu
at(100, "down 1c")         # Enter: New Game
at(104, "up 1c")
t = 200                    # the map is running
for lvl, (ex, ey), (xx, xy) in levels:
    at(t, "warp 1 %d %d" % (ex, ey))
    at(t, "warp 2 %d %d" % (ex, ey))
    at(t + 4, "down 1d")       # player 1 jumps: a request
    at(t + 7, "up 1d")
    at(t + 12, "down 1d 2")    # player 2 jumps on the same entrance: the level starts
    at(t + 15, "up 1d 2")
    at(t + 120, "warp 1 %d %d" % (xx, xy))   # the level is running: both walk out
    at(t + 125, "warp 2 %d %d" % (xx, xy))
    t += 320
at(t, "quit")
for tt, cmd in sorted(out, key=lambda e: e[0]):
    print(tt, cmd)

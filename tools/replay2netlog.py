#!/usr/bin/env python3
"""Turns a co-op replay (tests/replays) into a frame log that `keen1 --netlog` plays, the way an
online game would see it: segments joined into one run of frames, the inputs of both players on
every frame, and the replay's `warp` test aids kept (as global frames).

    tools/replay2netlog.py REPLAY NETLOG [--solo-until FRAME] [--add SEAT@FRAME] [--remove SEAT@FRAME]

--solo-until FRAME   start with player 1 only and add player 2 at FRAME
--add / --remove     extra events on top (SEAT is 1 or 2)
"""
import argparse
import re

ap = argparse.ArgumentParser()
ap.add_argument("replay")
ap.add_argument("netlog")
ap.add_argument("--solo-until", type=int)
ap.add_argument("--add", action="append", default=[])
ap.add_argument("--remove", action="append", default=[])
args = ap.parse_args()

gameseed = 1
segs = []
for line in open(args.replay):
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    m = re.match(r"gameseed (\d+)", line)
    if m:
        gameseed = int(m.group(1))
        continue
    if line == "map" or re.match(r"level \d+", line):
        segs.append({"events": [], "warps": [], "end": None})
        continue
    if not segs:
        continue
    seg = segs[-1]
    m = re.match(r"end (\d+)", line)
    if m:
        seg["end"] = int(m.group(1))
        continue
    m = re.match(r"warp (\d+) (\d+) (\d+) (\d+)", line)
    if m:
        seg["warps"].append(tuple(int(x) for x in m.groups()))
        continue
    m = re.match(r"(\d+) (\d+) (\d+) (\d+) (\d+)(?: (\d+))?$", line)
    if m:
        f, p, d, b1, b2, ev = m.groups()
        seg["events"].append((int(f), int(p), int(d), int(b1), int(b2), int(ev or 0)))

out = ["# keen1-online netlog (from %s)" % args.replay, "seed %d" % gameseed]
out.append("mask %d" % (1 if args.solo_until is not None else 3))
extra = {}
if args.solo_until is not None:
    extra.setdefault(args.solo_until, []).append("add:2")
for spec in args.add:
    seat, frame = spec.split("@")
    extra.setdefault(int(frame), []).append("add:%s" % seat)
for spec in args.remove:
    seat, frame = spec.split("@")
    extra.setdefault(int(frame), []).append("remove:%s" % seat)

offset = 0
frames = []
for seg in segs:
    if seg["end"] is None:
        raise SystemExit("every segment needs an `end` line (record it from a live run)")
    for (f, p, x, y) in seg["warps"]:
        out.append("warp %d %d %d %d" % (offset + f, p, x, y))
    for f in range(seg["end"]):
        ins = []
        for p in (1, 2):
            c = (8, 0, 0)
            ev = 0
            for (ef, ep, d, b1, b2, e) in seg["events"]:
                if ep == p and ef <= f:
                    c = (d, b1, b2)
                    if ef == f:
                        ev |= e
            ins.append(c[0] | (c[1] << 4) | (c[2] << 5) | (ev << 6))
        frames.append((offset + f, ins))
    offset += seg["end"]
for n, ins in frames:
    line = "%d %d %d" % (n, ins[0], ins[1])
    if n in extra:
        line += " " + " ".join(extra[n])
    out.append(line)
open(args.netlog, "w").write("\n".join(out) + "\n")
print("%s: %d frames" % (args.netlog, len(frames)))

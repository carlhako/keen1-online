#!/bin/sh
# Determinism tests for keen1-online. Run from the repository root (make test).
# Needs the shareware data in shareware/data and a built ./keen1; the
# native-vs-WebAssembly check also needs `make node` and node on the PATH.
set -u
cd "$(dirname "$0")/.."
DATA=${DATA:-shareware/data}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
ok()   { printf '  ok      %s\n' "$1"; }
bad()  { printf '  FAILED  %s\n' "$1"; fail=1; }
[ -f "$DATA/EGAHEAD.CK1" ] || { echo "No game data in $DATA"; exit 1; }

echo "snapshot / checksum self-test"
./keen1 --data "$DATA" --selftest tests/replays/walk-level1.txt > "$TMP/self.txt" 2>&1 \
    && ok "snapshot restore and checksum sensitivity" || { cat "$TMP/self.txt"; bad "self-test"; }

echo "real-time independence"
for r in tests/replays/*.txt; do
    n=$(basename "$r" .txt)
    ./keen1 --data "$DATA" --replay "$r" --checksums "$TMP/$n.fast" > /dev/null
    ./keen1 --data "$DATA" --replay "$r" --checksums "$TMP/$n.slow" --frame-delay 1 > /dev/null
    cmp -s "$TMP/$n.fast" "$TMP/$n.slow" && ok "$n: 1 ms per frame == no delay ($(wc -l < "$TMP/$n.fast") frames)" \
        || bad "$n: checksums differ with a frame delay"
done

echo "recording a two-player game and replaying it headless"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy KEEN_SCRIPT=tests/scripts/two-players-level1.txt \
    ./keen1 --data "$DATA" --level 1 --seed 7 --local2 --record "$TMP/rec.txt" --checksums "$TMP/win.ck" > /dev/null
./keen1 --data "$DATA" --replay "$TMP/rec.txt" --checksums "$TMP/head.ck" > /dev/null
cmp -s "$TMP/win.ck" "$TMP/head.ck" && ok "windowed run == replay of its recording ($(wc -l < "$TMP/win.ck") frames)" \
    || bad "windowed run and its replay differ"

echo "co-op rules"
# check REPLAY PATTERN... : the replay's report must contain every pattern (grep -E, one line each).
check() {
    r=$1; shift
    ./keen1 --data "$DATA" --replay "tests/replays/$r.txt" > "$TMP/out.txt" 2>&1
    for pat in "$@"; do
        grep -Eq -- "$pat" "$TMP/out.txt" || { bad "$r: missing '$pat'"; return; }
    done
    ok "$r"
}
check single-player-death "result 0 \(died\)" "lives 3, levels done:"
check coop-exit-then-death "result 1 \(completed\)" "P1: exited  P2: dead"
check coop-death-then-exit "result 6 \(stopped\)" "P1: alive  P2: exited"
check coop-respawn "result 6 \(stopped\)" "P1: alive  P2: alive"
check coop-rollback "result 4 \(failed\)" "score 0 ammo 0 lives 4 parts 0000  P1: alive  P2: alive"
check coop-votes "ended after 41 frames, result 4 \(failed\)" "ended after 51 frames, result 5 \(left by vote\)"
check coop-solo-restart "ended after 11 frames, result 4 \(failed\)"
check coop-events "result 5 \(left by vote\)" "P1 21,39  P2 21,39"
check coop-castle-switch "ended after 91 frames, result 3 \(castle switch\)"
check coop-teleporter "result 2 \(teleported\)" "P1 62,37  P2 62,37"
check coop-map-teleporter "P1 28,6 "
check coop-handshake "frame 10: request by player 1 for level 4, confirmed mask 1" "frame 20: no request" \
    "frame 42: request by player 2 for level 2, confirmed mask 2" "frame 52: request by player 2 for level 2, confirmed mask 3"
check coop-return-to-map "levels done: 1$" "P1 24,30  P2 24,30"
check coop-ending "all four ship parts: the ending plays" "levels done: 3"
check coop-levels-1-4 "levels done: 1 2 3 4"
check coop-statue "result 6 \\(stopped\\)"
# A dead Keen starts again where the level starts (12288,63743 in level 1), and the level goes on.
for r in coop-respawn coop-ten-deaths; do
    ./keen1 --data "$DATA" --replay "tests/replays/$r.txt" --dump-objs > "$TMP/out.txt" 2>&1
    ! grep -q "result 4 (failed)" "$TMP/out.txt" && grep -q "P1: alive  P2: alive" "$TMP/out.txt" \
        && [ "$(grep -Ec '^obj  [01] type  1 x   12288 y   63743$' "$TMP/out.txt")" = 2 ] \
        && ok "$r: both Keens back at the level start, no restart" || bad "$r: the Keens did not respawn"
done
./keen1 --data "$DATA" --replay tests/replays/coop-castle-switch.txt --checksums "$TMP/jit.ck" > /dev/null
KEEN_NOJITTER=1 ./keen1 --data "$DATA" --replay tests/replays/coop-castle-switch.txt --checksums "$TMP/nojit.ck" > /dev/null
cmp -s "$TMP/jit.ck" "$TMP/nojit.ck" && ok "castle switch: checksums the same with and without the screen jitter" \
    || bad "castle switch: the jitter reaches the game state"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy KEEN_SCRIPT=tests/scripts/coop-levels-1-4.txt \
    ./keen1 --data "$DATA" --local2 --seed 7 --record "$TMP/play.txt" --checksums "$TMP/play.win.ck" > /dev/null
./keen1 --data "$DATA" --replay "$TMP/play.txt" --checksums "$TMP/play.head.ck" > "$TMP/out.txt"
cmp -s "$TMP/play.win.ck" "$TMP/play.head.ck" && grep -q "levels done: 1 2 3 4" "$TMP/out.txt" \
    && ok "windowed co-op game through levels 1-4 == replay of its recording ($(wc -l < "$TMP/play.win.ck") frames)" \
    || bad "windowed co-op game and its replay differ"

echo "online: playing the frames a server seals (--netlog)"
# The same inputs through the online path give the same game as the replay they came from.
python3 tools/replay2netlog.py tests/replays/coop-levels-1-4.txt "$TMP/levels.netlog" > /dev/null
./keen1 --data "$DATA" --netlog "$TMP/levels.netlog" --checksums "$TMP/levels.net.ck" > /dev/null
cmp -s "$TMP/coop-levels-1-4.fast" "$TMP/levels.net.ck" \
    && ok "frame log of coop-levels-1-4 == the replay it came from ($(wc -l < "$TMP/levels.net.ck") frames)" \
    || bad "frame log and replay of coop-levels-1-4 differ"
# A browser that joins restores a checkpoint in a new process and must stay in step with the others.
# join JOIN_FRAME FROM_FRAME: player 2 joins at JOIN_FRAME; a second process starts from the snapshot
# of FROM_FRAME as player 2 and must produce the same checksums from there on.
join() {
    python3 tools/replay2netlog.py tests/replays/coop-levels-1-4.txt "$TMP/join.netlog" --solo-until "$1" > /dev/null
    ./keen1 --data "$DATA" --netlog "$TMP/join.netlog" --seat 1 --checksums "$TMP/join.a.ck" \
        --net-snapshot "$2:$TMP/join.snap" --netsums "$TMP/join.a.sums" > /dev/null
    ./keen1 --data "$DATA" --netlog "$TMP/join.netlog" --seat 2 --net-restore "$TMP/join.snap" \
        --checksums "$TMP/join.b.ck" --netsums "$TMP/join.b.sums" > /dev/null
    tail -n +$(($2 + 1)) "$TMP/join.a.ck" | cmp -s - "$TMP/join.b.ck" \
        && ok "$3 ($(wc -l < "$TMP/join.b.ck") frames)" || bad "$3: the joiner drifted"
    awk -v f="$2" '$1 > f' "$TMP/join.b.sums" > "$TMP/join.b.sums.tail"
    awk -v f="$2" '$1 > f' "$TMP/join.a.sums" | cmp -s - "$TMP/join.b.sums.tail" \
        && ok "$3: the reported checksums agree" || bad "$3: reported checksums differ"
}
join 260 230 "player 2 joins in level 2 from a checkpoint in level 2 (restore into a level)"
join 500 450 "player 2 joins on the map from a checkpoint on the map (restore into the map)"
join 300 100 "player 2 joins from a checkpoint two segments earlier"
python3 tools/replay2netlog.py tests/replays/coop-levels-1-4.txt "$TMP/leave.netlog" --remove 2@270 > /dev/null
./keen1 --data "$DATA" --netlog "$TMP/leave.netlog" --checksums "$TMP/leave.ck" > "$TMP/out.txt" 2>&1
grep -q "netlog: end at frame 770" "$TMP/out.txt" \
    && ok "player 2 leaves in level 2: the game goes on for player 1 alone" || bad "leave: the game did not run on"

# Continue single player after a partner left: the co-op end rules apply to who is left.
# solo_log FILE REMOVE_FRAME WARP...: both start level 1 from the map; player 2 leaves at REMOVE_FRAME.
solo_log() {
    out=$1; at=$2; shift 2
    python3 - "$out" "$at" "$@" <<'PY'
import sys
out, at, warps = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
lines = ["# keen1-online netlog (test)", "seed 7", "mask 3", "warp 0 1 21 39", "warp 0 2 21 39"] + warps
for f in range(500):
    b = 24 if f == 10 else 8          # both press jump on level 1's entrance
    lines.append("%d %d %d%s" % (f, b, b, " remove:2" if f == at else ""))
open(out, "w").write("\n".join(lines) + "\n")
PY
}
loc_at() { awk -v f="$2" '$1 == f { print $3 }' "$1"; }
solo_log "$TMP/alive.netlog" 100 "warp 150 1 113 8"
./keen1 --data "$DATA" --netlog "$TMP/alive.netlog" --netsums "$TMP/alive.sums" > /dev/null
[ "$(loc_at "$TMP/alive.sums" 120)" = 1 ] && [ "$(loc_at "$TMP/alive.sums" 192)" = 0 ] \
    && ok "player 2 leaves, player 1 alive: the level goes on and player 1 finishes it" \
    || bad "player 2 leaves, player 1 alive: the level did not go on"
solo_log "$TMP/dead.netlog" 330 "warp 50 1 5 120"
./keen1 --data "$DATA" --netlog "$TMP/dead.netlog" --netsums "$TMP/dead.sums" > /dev/null
[ -z "$(loc_at "$TMP/dead.sums" 331)" ] && [ "$(loc_at "$TMP/dead.sums" 480)" = 1 ] \
    && ok "player 2 leaves after player 1 died and started again: the level goes on for player 1" \
    || bad "player 2 leaves after player 1 respawned: the level did not go on"

# The last level with all four ship parts: the ending plays, then the game tells the server it is done.
python3 - "$TMP/ending.netlog" <<'PY'
import sys
lines = ["# keen1-online netlog (test)", "seed 7", "mask 3", "warp 0 1 21 39", "warp 0 2 21 39",
         "parts 20 1111", "warp 100 1 113 8", "warp 104 2 113 8"]
lines += ["%d %d %d" % (f, 24 if f == 10 else 8, 24 if f == 10 else 8) for f in range(700)]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
PY
timeout 60 ./keen1 --data "$DATA" --netlog "$TMP/ending.netlog" > "$TMP/out.txt" 2>&1
grep -q "the ending plays" "$TMP/out.txt" && grep -q "the game finished at frame .*levels done: 1" "$TMP/out.txt" \
    && ok "all four ship parts: the ending plays and the game finishes" || bad "the ending did not play and finish"

# A game loaded from a save starts from its record: score, completed levels, ship parts and map position.
python3 - "$TMP/loaded.netlog" <<'PY'
import struct, sys
done = [1 if i in (0, 2) else 0 for i in range(16)]
rec = struct.pack("<5h4h16hhhi4i8hh", 1, 0, 1, 0, 1, 0, 0, 0, 0, *done, 4, 0, 4200,
                  21 << 12, 39 << 12, (21 << 12) - 0x9000, 0, *([0] * 8), 0)
assert len(rec) == 92
lines = ["# keen1-online netlog (test)", "seed 7", "mask 1", "record " + rec.hex()]
lines += ["%d 8 8" % f for f in range(60)]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
PY
./keen1 --data "$DATA" --netlog "$TMP/loaded.netlog" > "$TMP/out.txt" 2>&1
grep -q "netlog: end at frame 60, score 4200, levels done: 1 3$" "$TMP/out.txt" \
    && ok "a game loaded from a save starts from its record (score 4200, levels 1 and 3)" \
    || { cat "$TMP/out.txt"; bad "a game loaded from a save did not start from its record"; }

echo "native vs WebAssembly (Node)"
if [ -f build/node/keen1.js ] && command -v node > /dev/null; then
    for r in tests/replays/*.txt; do
        n=$(basename "$r" .txt)
        node build/node/keen1.js --data "$DATA" --replay "$r" --checksums "$TMP/$n.wasm" > /dev/null
        cmp -s "$TMP/$n.fast" "$TMP/$n.wasm" && ok "$n: wasm == native" || bad "$n: wasm differs from native"
    done
    node build/node/keen1.js --data "$DATA" --netlog "$TMP/levels.netlog" --checksums "$TMP/levels.net.wasm" > /dev/null
    cmp -s "$TMP/levels.net.ck" "$TMP/levels.net.wasm" && ok "online frame log: wasm == native" || bad "online frame log: wasm differs from native"
    node build/node/keen1.js --data "$DATA" --netlog "$TMP/join.netlog" --seat 2 --net-restore "$TMP/join.snap" --checksums "$TMP/join.wasm.ck" > /dev/null
    cmp -s "$TMP/join.b.ck" "$TMP/join.wasm.ck" && ok "online join from a snapshot: wasm == native" || bad "online join from a snapshot: wasm differs from native"
    node build/node/keen1.js --data "$DATA" --netlog "$TMP/loaded.netlog" > "$TMP/out.txt" 2>&1
    grep -q "score 4200, levels done: 1 3$" "$TMP/out.txt" && ok "a game loaded from a save: wasm == native" \
        || bad "a game loaded from a save: wasm differs from native"
else
    echo "  skipped (run: source ~/tools/emsdk/emsdk_env.sh && make node)"
fi
exit $fail

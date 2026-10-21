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
check coop-death-then-exit "result 1 \(completed\)" "P1: dead  P2: exited"
check coop-both-die "result 4 \(failed\)" "P1: dead  P2: dead" "result 6 \(stopped\)"
check coop-rollback "result 4 \(failed\)" "score 0 ammo 0 lives 4 parts 0000  P1: dead  P2: dead"
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
./keen1 --data "$DATA" --replay tests/replays/coop-ten-deaths.txt > "$TMP/out.txt" 2>&1
[ "$(grep -c 'result 4 (failed)' "$TMP/out.txt")" = 10 ] && ok "coop-ten-deaths: ten failures in a row, the game goes on" \
    || bad "coop-ten-deaths: not ten failures"
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

echo "native vs WebAssembly (Node)"
if [ -f build/node/keen1.js ] && command -v node > /dev/null; then
    for r in tests/replays/*.txt; do
        n=$(basename "$r" .txt)
        node build/node/keen1.js --data "$DATA" --replay "$r" --checksums "$TMP/$n.wasm" > /dev/null
        cmp -s "$TMP/$n.fast" "$TMP/$n.wasm" && ok "$n: wasm == native" || bad "$n: wasm differs from native"
    done
else
    echo "  skipped (run: source ~/tools/emsdk/emsdk_env.sh && make node)"
fi
exit $fail

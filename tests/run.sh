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

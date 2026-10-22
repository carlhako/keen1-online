#!/usr/bin/env bash
# Checks that deploy/install.sh does nothing when `curl ... | bash` is cut off part way through.
# For every line of the installer it pipes bash the script cut at the start and in the middle of
# that line and fails if any run prints the installer's log, warning or error marker (==>, !!, XX).
# The whole script must stop at its first check with exactly the "run as root" error. Run it as a
# non-root user, so a cut that did reach the installer's work could not change anything.
set -u
export LC_ALL=C
cd "$(dirname "$0")/.."
SCRIPT=${1:-deploy/install.sh}
[ "$(id -u)" -ne 0 ] || { echo "run this as a non-root user"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

# Feeds bash the first $1 bytes of the script, as curl would, from an empty directory.
run_cut() {
    head -c "$1" "$SCRIPT" | (cd "$TMP" && bash) 2>&1
}

# Byte offsets: the start of each line, and the middle of it.
cuts=()
offset=0
while IFS= read -r line || [ -n "$line" ]; do
    len=${#line}
    cuts+=("$offset" "$((offset + len / 2))")
    offset=$((offset + len + 1))
done < "$SCRIPT"

for n in "${cuts[@]}"; do
    out=$(run_cut "$n")
    if printf '%s' "$out" | grep -qE '==>|!!|XX'; then
        echo "  FAILED  cut at byte $n ran part of the installer:"
        printf '%s\n' "$out" | sed 's/^/          /'
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "  ok      ${#cuts[@]} cuts of $SCRIPT ran nothing"

whole=$(run_cut "$(wc -c < "$SCRIPT")")
want=$(printf '\033[1;31mXX \033[0m %s' "run as root")
if [ "$whole" = "$want" ]; then
    echo "  ok      the whole script stops with \"run as root\""
else
    echo "  FAILED  the whole script, as non-root, printed:"
    printf '%s\n' "$whole" | sed 's/^/          /'
    fail=1
fi
exit "$fail"

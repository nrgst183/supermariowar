#!/bin/bash
# The determinism and netplay checks that the unit tests can't do: they run the
# real game headless (--autotest, no window, no sound).
#
#  1. Rollback sync test, every game mode: every tick rolls back up to 8 ticks,
#     re-simulates them and checks that the state is identical.
#  2. Replays: matches recorded with CPU players play back exactly (all stored
#     checksums match), with random jumps in the timeline.
#  3. Netplay over 127.0.0.1 (scripts/netplay-test.sh): 2 and 3 peers, without
#     and with simulated lag and loss, then the disconnect tests (a joiner
#     killed, a joiner leaving, the host killed: the CPU takes over).
#
# Usage: scripts/ci-checks.sh [BUILD_DIR]   (default: build; needs its smw binary)
# The netplay logs go to $CHECKS_LOGS (default: a temporary folder, removed after).
# Exit status: the number of failed checks (0: all passed).

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-build}
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
SMW="$BUILD/smw"
[ -x "$SMW" ] || { echo "no smw binary in $BUILD"; exit 2; }

export SDL_VIDEO_DRIVER=dummy SDL_VIDEODRIVER=dummy SDL_AUDIO_DRIVER=dummy SDL_AUDIODRIVER=dummy
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
LOGS=${CHECKS_LOGS:-$WORK/logs}
mkdir -p "$LOGS"
fails=0

# Runs the game headless with its own home folder (replays, settings)
smw() {
    HOME="$WORK/home" "$SMW" --datadir "$ROOT/data" --autotest --at-norender 1 "$@"
}

echo "== Rollback sync test (22 game modes)"
for mode in $(seq 0 21); do
    out=$(smw --at-mode "$mode" --at-frames 1500 --at-seed 7 --at-synctest 8 2>&1)
    mismatches=$(grep -c "MISMATCH" <<< "$out")
    if [ "$mismatches" -ne 0 ] || ! grep -q "replay: saved" <<< "$out"; then
        echo "FAIL mode $mode: $mismatches mismatches"
        grep -m 3 "MISMATCH" <<< "$out"
        fails=$((fails + 1))
    else
        echo "ok   mode $mode"
    fi
done

echo "== Replays (CPU players, random jumps)"
for mode in 0 3 9 15; do
    rm -rf "$WORK/home"
    file=$(smw --at-mode "$mode" --at-frames 2500 --at-seed 5 2>&1 | sed -n 's/^replay: saved \([^ ]*\).*/\1/p' | tail -1)
    if [ -z "$file" ]; then
        echo "FAIL mode $mode: no replay saved"
        fails=$((fails + 1))
        continue
    fi
    out=$(smw --at-replay-seeks 6 --replay "$file" 2>&1 | grep "^replay: done")
    if [ -z "$out" ] || grep -q "differently" <<< "$out"; then
        echo "FAIL mode $mode: ${out:-the replay did not finish}"
        fails=$((fails + 1))
    else
        echo "ok   mode $mode: ${out#replay: }"
    fi
done

echo "== Netplay (127.0.0.1)"
bash "$ROOT/scripts/netplay-test.sh" --builds "$BUILD" --peers "2 3" --conditions "a c" --matches 1 --out "$LOGS/net" \
    || fails=$((fails + 1))

echo "== Netplay disconnects"
bash "$ROOT/scripts/netplay-test.sh" --builds "$BUILD" --disconnect --peers "3" --conditions "a" --out "$LOGS/disconnect" \
    || fails=$((fails + 1))

echo
if [ $fails -eq 0 ]; then
    echo "All checks passed"
else
    echo "$fails check(s) failed"
fi
exit $fails

#!/bin/bash
# Screenshots of the online menus (main menu, Online menu and its error messages, the lobby as
# host and as joiner, the match menus, the leave dialog, the map download).
#
# Runs real game processes on 127.0.0.1 with SDL's dummy video driver and the scripted menu walk
# `--autotest --at-match shots` (see MenuAutoTest.cpp): one process hosts, a second joins (its data
# directory has no copy of the map the host picks, so it downloads it), a third only provokes the
# error messages. The PNGs are the screen surface of each process.
#
# Usage: scripts/menu-screenshots.sh [OUTDIR] [BUILD_DIR]
#   OUTDIR     where the PNGs go (default: ./menu_shots)
#   BUILD_DIR  the build directory with the smw binary (default: build)
# Environment: PORT (default 7300) is the host's UDP port, BUSY_PORT (default 7390) the one that is
# blocked for the "port in use" error.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-menu_shots}
BUILD=${2:-$ROOT/build}
PORT=${PORT:-7300}
BUSY_PORT=${BUSY_PORT:-7390}
WORK=$(mktemp -d)
MAP_FILE="4matsy_Block Piles.map"
MAP_NAME="Block Piles"

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/*.png

# The joiner's copy of the data directory (links to its files) lacks the host's map
JOIN_DATA="$WORK/joindata"
cp -rs "$(readlink -f "$ROOT/data")/." "$JOIN_DATA"
rm -f "$JOIN_DATA/maps/$MAP_FILE"

run() {    # NAME DATADIR ARGS...
    local name=$1 data=$2
    shift 2
    mkdir -p "$WORK/home_$name"
    (cd "$BUILD" && HOME="$WORK/home_$name" SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy timeout 400 \
        ./smw --datadir "$data" --autotest --at-match shots --at-shots "$OUT" --at-realtime 3 \
        --at-frames 30000 --at-humans 1 --at-mode 0 --at-seed 5 "$@" > "$WORK/$name.log" 2>&1)
}

# The error messages: a UDP port in use, and nobody to connect to
python3 - "$BUSY_PORT" <<'PY' &
import socket, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("0.0.0.0", int(sys.argv[1])))
time.sleep(120)
PY
BLOCKER=$!
mkdir -p "$WORK/errors"
run errors "$ROOT/data" --at-role errors --at-addr "$BUSY_PORT" --at-nick Mario
kill $BLOCKER 2>/dev/null

# The lobby: host and joiner at the same time
run host "$ROOT/data" --at-role host --at-nick Mario --at-map "$MAP_NAME" --at-addr "$PORT" --net-sim-latency 15 &
HOST=$!
sleep 4
run join "$JOIN_DATA" --at-role join --at-nick Luigi --at-addr "127.0.0.1:$PORT" --net-sim-latency 15 &
JOIN=$!
wait $HOST $JOIN

for name in errors host join; do
    echo "== $name"
    grep -E "^shots:" "$WORK/$name.log"
done
echo "logs: $WORK"
ls "$OUT"

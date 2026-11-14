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
# HOST_LACKS_FROM=L: the host's data directory lacks every skin from "L" on, so that the joiner's skin
# (Link, and the ones it scrolls to) is downloaded by the host (the end-to-end test of the other direction).
# HOST_SKIN and JOIN_SKIN (default Kirby and Link) are the players' skins; the joiner's data directory
# lacks the host's, so that it is downloaded.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-menu_shots}
BUILD=${2:-$ROOT/build}
PORT=${PORT:-7300}
BUSY_PORT=${BUSY_PORT:-7390}
WORK=$(mktemp -d)
MAP_FILE="4matsy_Block Piles.map"
MAP_NAME="Block Piles"
HOST_SKIN=${HOST_SKIN:-Kirby}
JOIN_SKIN=${JOIN_SKIN:-Link}

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/*.png

# The joiner's copy of the data directory (links to its files) lacks the host's map
JOIN_DATA="$WORK/joindata"
cp -rs "$(readlink -f "$ROOT/data")/." "$JOIN_DATA"
rm -f "$JOIN_DATA/maps/$MAP_FILE"
rm -f "$JOIN_DATA/gfx/skins/$HOST_SKIN.png" "$JOIN_DATA/gfx/skins/$HOST_SKIN.bmp"

HOST_DATA="$ROOT/data"
if [ -n "${HOST_LACKS_FROM:-}" ]; then
    HOST_DATA="$WORK/hostdata"
    cp -rs "$(readlink -f "$ROOT/data")/." "$HOST_DATA"
    for f in "$HOST_DATA"/gfx/skins/*; do
        name=$(basename "$f")
        if [[ "${name^^}" > "${HOST_LACKS_FROM^^}" || "${name^^}" == "${HOST_LACKS_FROM^^}"* ]]; then rm -f "$f"; fi
    done
fi

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
run host "$HOST_DATA" --at-role host --at-nick Mario --at-map "$MAP_NAME" --at-addr "$PORT" --net-sim-latency 15 --net-skin "$HOST_SKIN" --net-slow-map 8 &
HOST=$!
sleep 4
run join "$JOIN_DATA" --at-role join --at-map "$MAP_NAME" --at-nick Luigi --at-addr "127.0.0.1:$PORT" --net-sim-latency 15 --net-skin "$JOIN_SKIN" &
JOIN=$!
wait $HOST $JOIN

for name in errors host join; do
    echo "== $name"
    grep -E "^shots:" "$WORK/$name.log"
done
echo "logs: $WORK"
ls "$OUT"

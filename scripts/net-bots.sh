#!/bin/bash
# Lagged bots to play online against on one computer: CPU players that join your lobby over the
# network, each from its own copy of the game, with a simulated connection. Their AI runs on their
# machine and its buttons reach you late, as a far-away player's would (--net-bot).
#
# Usage: scripts/net-bots.sh [BOTS] [ROUND_TRIP_MS] [HOST[:PORT]] [extra arguments for the bots...]
#   BOTS            how many (default 1, at most 3)
#   ROUND_TRIP_MS   their round trip to you (default 300): half of it is added to every packet they
#                   send. For the other half, start your own game with the same --net-sim-latency
#                   (half the round trip): smw --net-sim-latency 150
#   HOST[:PORT]     your lobby (default 127.0.0.1:7300, the port Host uses)
#   extra           e.g. --net-sim-jitter 20 --net-sim-loss 5
#
# Host a game from the menus (Online, Host), then run this: the bots join, get ready, and play every
# match you start; they quit when you close the lobby. Their windows start minimized and muted, each
# with its own settings folder (nothing of yours is touched).
set -u
cd "$(dirname "$0")/.." || exit 1
BOTS=${1:-1}
RTT=${2:-300}
HOST=${3:-127.0.0.1:7300}
shift $(( $# < 3 ? $# : 3 ))
[ "$BOTS" -ge 1 ] && [ "$BOTS" -le 3 ] || { echo "1 to 3 bots"; exit 2; }
case "$HOST" in *:*) ;; *) HOST="$HOST:7300" ;; esac

SMW=""
for candidate in build/vs/Release/smw.exe build/vs/RelWithDebInfo/smw.exe build/vs/Debug/smw.exe build/smw build/Release/smw; do
    [ -x "$candidate" ] && { SMW=$candidate; break; }
done
[ -n "$SMW" ] || { echo "no smw binary found under build/"; exit 2; }

HOMES=()
cleanup() { for h in "${HOMES[@]}"; do rm -rf "$h"; done; }
trap cleanup EXIT

PIDS=()
for ((b = 1; b <= BOTS; b++)); do
    home=$(mktemp -d)
    HOMES+=("$home")
    homeArg=$home
    command -v cygpath > /dev/null && homeArg=$(cygpath -w "$home")
    SMW_HOME="$homeArg" HOME="$home" SDL_AUDIO_DRIVER=dummy "$SMW" --autotest --at-minimized 1 --at-humans 1 \
        --at-seed $((40 + b)) --net-bot 1 --net-name "Bot $b" --net-join "$HOST" --net-sim-latency $((RTT / 2)) "$@" \
        > "$home/bot.log" 2>&1 &
    PIDS+=($!)
    echo "bot $b: joining $HOST, ${RTT} ms round trip (log: $home/bot.log)"
    sleep 0.5
done
echo "Your own half: start your game with --net-sim-latency $((RTT / 2)) (for the whole ${RTT} ms both ways)."
for pid in "${PIDS[@]}"; do wait "$pid"; done

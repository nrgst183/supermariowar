#!/bin/bash
# End-to-end test of the rollback netplay: runs real game processes on
# 127.0.0.1 that play online matches against each other with seeded random
# input (--autotest --at-humans), under simulated network conditions, and
# checks that every peer computed exactly the same match.
#
# For each run (peer count x condition) it checks that:
#  - every peer finished every match,
#  - the state checksums of all confirmed frames ("netsum:" lines) are
#    identical on all peers, and no peer reported a desync,
# and prints rollback, stall and CPU statistics. With several builds (e.g. a
# GCC debug build and a clang optimized build), peers alternate between them:
# a cross-compiler determinism test.
#
# Usage: scripts/netplay-test.sh [options]
#   --builds "DIR..."     build directories with an smw binary (default: build)
#   --peers "2 3 4"       peer counts to run
#   --conditions "a b c d"
#                         a: no added lag   b: 60 ms latency, 15 ms jitter
#                         c: 100 ms latency, 5% loss
#                         d: 150 ms latency, 20 ms jitter, 10% loss
#                         (latency and jitter are one way: the round trip gets them twice)
#   --matches N           matches per run (default 2)
#   --frames N            frame limit of a match (default 1200, 62.5 frames a second)
#   --match KIND          single (default) or minigame
#   --start N             first match in the mode/map cycle (default 0; each run adds 3)
#   --players N           players per match, the peers' humans then CPU players (default 4)
#   --delay N             input delay (default: automatic, from the ping measured in the lobby)
#   --out DIR             where the logs go (default /tmp/smw-netplay-test)
#   --disconnect          run the disconnect tests instead
#   --map-transfer        run the map transfer test instead: 3 peers play the
#                         map "Block Piles"; peer 1's data has no such map,
#                         peer 2's has another file of that name; both must
#                         download the host's (into maps/net/ of their copy of
#                         the data directory) and agree on every checksum
#   --smoke               quick check for CI: 2 peers, condition b, 1 match of 600
#                         frames, and a watchdog (kills the game after 150 s).
#                         Prints one PASS/FAIL line; same exit status rules.
# Exit status: 0 if every run passed.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDS="build"
PEERS="2 3 4"
CONDITIONS="a b c d"
MATCHES=2
FRAMES=1200
KIND=single
START=0
PLAYERS=4
DELAY=auto
OUT=/tmp/smw-netplay-test
DISCONNECT=0
MAPTEST=0
SMOKE=0
PORT=7400

while [ $# -gt 0 ]; do
    case "$1" in
        --builds) BUILDS="$2"; shift 2 ;;
        --peers) PEERS="$2"; shift 2 ;;
        --conditions) CONDITIONS="$2"; shift 2 ;;
        --matches) MATCHES="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --match) KIND="$2"; shift 2 ;;
        --start) START="$2"; shift 2 ;;
        --players) PLAYERS="$2"; shift 2 ;;
        --delay) DELAY="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --disconnect) DISCONNECT=1; shift ;;
        --map-transfer) MAPTEST=1; shift ;;
        --smoke) SMOKE=1; shift ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done

if [ $SMOKE -eq 1 ]; then
    PEERS="2"; CONDITIONS="b"; MATCHES=1; FRAMES=600
fi

DELAY_ARGS=""
[ "$DELAY" = "auto" ] || DELAY_ARGS="--net-delay $DELAY"

mkdir -p "$OUT"
read -r -a BUILD_LIST <<< "$BUILDS"
for b in "${BUILD_LIST[@]}"; do
    case "$b" in /*) ;; *) b="$ROOT/$b" ;; esac
    [ -x "$b/smw" ] || { echo "no smw binary in $b"; exit 2; }
done

condition_args() {
    case "$1" in
        a) echo "" ;;
        b) echo "--net-sim-latency 60 --net-sim-jitter 15" ;;
        c) echo "--net-sim-latency 100 --net-sim-loss 5" ;;
        d) echo "--net-sim-latency 150 --net-sim-jitter 20 --net-sim-loss 10" ;;
        *) echo "BAD" ;;
    esac
}

binary_for() {
    local b=${BUILD_LIST[$(( $1 % ${#BUILD_LIST[@]} ))]}
    case "$b" in /*) ;; *) b="$ROOT/$b" ;; esac
    echo "$b"
}

# Runs one peer: $1 = peer index, $2 = log, rest = arguments
run_peer() {
    local peer=$1 log=$2; shift 2
    local dir; dir=$(binary_for "$peer")
    local home; home=$(mktemp -d)
    (
        cd "$dir" || exit 1
        TIMEFORMAT="proccpu: user %U sys %S real %R"
        time HOME="$home" SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
            ./smw --datadir "${PEER_DATA[$peer]:-$ROOT/data}" --autotest --at-norender 1 --net-stats 300 "$@"
        echo "exit $?"
    ) > "$log" 2>&1
    rm -rf "$home"
}

# Starts the host and the joiners of one session; $1 = peers, $2 = run name,
# $3 = port, $4 = condition args, $5 = extra host args, rest: per-peer extra args (peer:args)
start_session() {
    local n=$1 name=$2 port=$3 cond=$4 hostextra=$5; shift 5
    local p
    PIDS=()
    run_peer 0 "$OUT/$name.p0.log" --at-match "$KIND" --at-players "$PLAYERS" --at-humans "$n" \
        --at-matches "$MATCHES" --at-frames "$FRAMES" --at-start "$START_INDEX" --at-seed 7 \
        --net-host "$port" --net-peers "$n" $DELAY_ARGS $cond $hostextra "${MAP_ARGS[@]}" &
    PIDS+=($!)
    for ((p = 1; p < n; p++)); do
        sleep 0.4
        local extra=""
        for e in "$@"; do
            [ "${e%%:*}" = "$p" ] && extra="${e#*:}"
        done
        run_peer "$p" "$OUT/$name.p$p.log" --at-humans 1 --at-seed $((7 + p)) \
            --net-join "127.0.0.1:$port" $cond $extra &
        PIDS+=($!)
    done
}

field() {    # field NAME LINE: the number after "NAME "
    echo "$2" | sed -n "s/.*$1 \([-+0-9.]*\).*/\1/p" | head -1
}

# Checks the logs of one run and prints its line of the result table
check_run() {
    local n=$1 name=$2 cond=$3
    local ok=1 reasons=""
    local host="$OUT/$name.p0.log"

    for ((p = 0; p < n; p++)); do
        local log="$OUT/$name.p$p.log"
        local done; done=$(grep -c "^autotest: match [0-9]* ended" "$log")
        [ "$done" -eq "$MATCHES" ] || { ok=0; reasons+=" p$p:matches=$done"; }
        grep -q "^exit 0" "$log" || { ok=0; reasons+=" p$p:exit"; }
        if grep -q "DESYNC\|ERROR\|synctest: MISMATCH" "$log"; then ok=0; reasons+=" p$p:desync/error"; fi
        if [ "$p" -gt 0 ] && ! diff -q <(grep "^netsum:" "$host") <(grep "^netsum:" "$log") > /dev/null; then
            ok=0; reasons+=" p$p:netsum-differs"
        fi
    done
    local sums; sums=$(grep -c "^netsum:" "$host")
    [ "$sums" -gt 0 ] || { ok=0; reasons+=" no-netsum"; }

    # Statistics over all peers and matches
    local stats
    stats=$(cat "$OUT/$name".p*.log | awk '
        /^netmatch:/ {
            for (i = 1; i <= NF; i++) {
                if ($i == "frames") frames += $(i+1)
                if ($i == "in") secs += $(i+1) + 0
                if ($i == "rollbacks") rb += $(i+1)
                if ($i == "(avg") ;
                if ($i == "resimulated") resim += $(i+1)
                if ($i == "delay") { dly += $(i+1); ndly++ }
                if ($i == "stalls") stalls += $(i+1)
                if ($i == "skips") skips += $(i+1)
                if ($i == "barrier-waits") bw += $(i+1)
                if ($i == "max" && $(i+1) ~ /^[0-9]+\)$/) { m = $(i+1); sub(/\)/, "", m); if (m + 0 > maxrb) maxrb = m + 0 }
            }
            matches++
        }
        /^netcpu:/ { for (i = 1; i <= NF; i++) { if ($i == "update") { cpu += $(i+1); ncpu++ } if ($i == "tick") { tick += $(i+1) } } }
        /^netpeer:/ { for (i = 1; i <= NF; i++) if ($i == "rtt") { rtt += $(i+1); nrtt++ } }
        /^proccpu:/ { pu += $3; ps += $5; pr += $7; np++ }
        END {
            if (secs == 0) secs = 1
            printf "%4.1f %6.2f %5.2f %3d %6d %6d %5d %6.0f %5.1f %5.0f %5.1f",
                (ndly ? dly / ndly : 0), rb / secs, (rb ? resim / rb : 0), maxrb, stalls, skips, bw, (ncpu ? cpu / ncpu : 0), (ncpu ? tick / ncpu : 0),
                (nrtt ? rtt / nrtt : 0), (pr ? 100 * (pu + ps) / pr : 0)
        }')
    local status="PASS"
    [ $ok -eq 1 ] || status="FAIL"
    printf "%-5s %5d  %-4s %-9s %5d  %s %s\n" "$status" "$n" "$cond" "$BUILD_TAG" "$sums" "$stats" "$reasons"
    [ $ok -eq 1 ]
}

BUILD_TAG=$(for b in "${BUILD_LIST[@]}"; do basename "$b"; done | paste -sd+ -)
PEER_DATA=()
MAP_ARGS=()

if [ $MAPTEST -eq 1 ]; then
    # Copies of the data directory (links to its files) for peers 1 and 2
    map="4matsy_Block Piles.map"
    for p in 1 2; do
        d="$OUT/data$p"
        rm -rf "$d"
        cp -rs "$(readlink -f "$ROOT/data")/." "$d"
        rm -f "$d/maps/$map"
        PEER_DATA[$p]="$d"
    done
    cp "$ROOT/data/maps/4matsy_Cloud Props.map" "${PEER_DATA[2]}/maps/$map"    # same name, other contents
    START_INDEX=$START
    MATCHES=1
    name="maptransfer"
    MAP_ARGS=(--at-map "Block Piles")
    start_session 3 "$name" "$PORT" "$(condition_args b)" ""
    for pid in "${PIDS[@]}"; do wait "$pid"; done
    fails=0
    check_run 3 "$name" b || fails=1
    for p in 1 2; do
        log="$OUT/$name.p$p.log"
        grep -E "^netplay: (match 1: |downloading|saved the map|ERROR)" "$log" | sed "s/^/  peer $p: /"
        f="${PEER_DATA[$p]}/maps/net/$map"
        if ! cmp -s "$f" "$ROOT/data/maps/$map"; then echo "  FAIL: peer $p has no copy of the host's map in maps/net"; fails=1; fi
    done
    exit $fails
fi

if [ $DISCONNECT -eq 1 ]; then
    # 1. A joiner is killed (no goodbye): the others notice the silence or the lost connection
    # 2. A joiner leaves the match (--net-quit-at): the others see it disconnect
    # 3. The host is killed
    fails=0
    START_INDEX=$START
    for test in kill-joiner quit-joiner kill-host; do
        name="disconnect-$test"
        n=3
        port=$((PORT + 90))
        PORT=$((PORT + 3))
        cond=$(condition_args b)
        case $test in
            quit-joiner) start_session $n "$name" $port "$cond" "" "2:--net-quit-at 400" ;;
            *) start_session $n "$name" $port "$cond" "" ;;
        esac
        if [ $test = kill-joiner ]; then
            sleep 12; pkill -9 -f "at-seed 9 --net-join 127.0.0.1:$port" 2>/dev/null
        elif [ $test = kill-host ]; then
            sleep 12; pkill -9 -f "net-host $port" 2>/dev/null
        fi
        for pid in "${PIDS[@]}"; do wait "$pid"; done
        echo "== $test"
        for ((p = 0; p < n; p++)); do
            log="$OUT/$name.p$p.log"
            echo "  peer $p: $(grep -E "^netplay: (peer [0-9]+ .* the match ends|leaving|the lobby closed)" "$log" | tr '\n' ' ') [$(grep -E '^exit|Segmentation|Aborted' "$log" | tr '\n' ' ')]"
        done
        survivors=$(seq 0 $((n - 1)))
        [ $test = kill-host ] && survivors="1 2"
        [ $test = kill-joiner ] && survivors="0 1"
        for p in $survivors; do
            log="$OUT/$name.p$p.log"
            grep -q "^exit 0" "$log" || { echo "  FAIL: peer $p didn't exit cleanly"; fails=$((fails + 1)); }
            grep -qE "^netplay: (peer [0-9]+ .* the match ends|leaving|the lobby closed)" "$log" || { echo "  FAIL: peer $p reported nothing"; fails=$((fails + 1)); }
        done
    done
    exit $((fails > 0))
fi

echo "logs: $OUT"
echo "STATUS peers cond builds     sums  dly   rb/s   len max stalls  skips bwait cpu_us tick_us rtt  cpu%  notes"
fails=0
run=0
for n in $PEERS; do
    for c in $CONDITIONS; do
        cond=$(condition_args "$c")
        [ "$cond" = "BAD" ] && { echo "unknown condition $c"; exit 2; }
        START_INDEX=$((START + run * 3))
        name="n${n}_$c"
        port=$((PORT + run * 10))
        start_session "$n" "$name" "$port" "$cond" ""
        if [ $SMOKE -eq 1 ]; then
            # Watchdog: a hung game must fail the run, not the CI job's timeout
            ( sleep 150; kill "${PIDS[@]}" 2>/dev/null ) &
            WATCHDOG=$!
        fi
        for pid in "${PIDS[@]}"; do wait "$pid"; done
        [ $SMOKE -eq 1 ] && kill "$WATCHDOG" 2>/dev/null
        check_run "$n" "$name" "$c" || fails=$((fails + 1))
        run=$((run + 1))
    done
done
echo "columns: dly input delay, rb/s rollbacks per second per peer, len avg re-simulated ticks per rollback, max longest rollback,"
echo "  stalls/skips/bwait frames waited for inputs/time sync/a barrier (sum over peers), cpu_us avg cost of a frame"
echo "  (network + simulation + rollbacks), tick_us avg simulation tick, rtt avg round trip (ms), cpu% process CPU use"
exit $((fails > 0))

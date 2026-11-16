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
#   (NETPLAY_EXTRA_ARGS: more arguments for every peer, e.g. "--at-runahead 2")
#   --frames N            frame limit of a match (default 1200, 62.5 frames a second)
#   --match KIND          single (default), minigame, or a whole multi-match mode:
#                         tournament (2 wins), tour (the tour "0smw", 3 stops) or
#                         world (the world "MrMister_Simple"). Then --matches is
#                         only an upper bound (default 30), and the peers must
#                         also agree on the series state after and before every
#                         match and on the final standings ("modesum:" lines, see
#                         src/smw/rollback/NetSeries.h)
#   --start N             first match in the mode/map cycle (default 0; each run adds 3)
#   --players N           players per match, the peers' humans then CPU players (default 4)
#   --delay N             input delay (default: automatic, from the ping measured in the lobby)
#   --out DIR             where the logs go (default /tmp/smw-netplay-test)
#   --disconnect          run the disconnect tests instead: in a match of 3 or 4
#                         peers (--peers, default "3 4") under each condition
#                         (--conditions, default "a c"), a joiner is killed
#                         (SIGKILL), a joiner leaves (--net-quit-at), the host
#                         is killed, and (4 peers) two joiners are killed at
#                         once. The others must play the match to its end
#                         (the CPU takes over the lost players), agree on
#                         every confirmed checksum and on the drop frame, and
#                         report no desync. --tests "kill-joiner ..." runs only these.
#   --series-disconnect   with --match tour or world: 3 peers play it, and peer 2
#                         is killed on the second tour stop / world map screen
#                         (between matches): the others must finish the series
#                         with its players as CPU players and agree on it
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
SERIESDISC=0
PORT=7400
PEERS_SET=0
LINEBUF=""
command -v stdbuf > /dev/null && LINEBUF="stdbuf -oL"
DISC_TESTS=""
CONDITIONS_SET=0

while [ $# -gt 0 ]; do
    case "$1" in
        --builds) BUILDS="$2"; shift 2 ;;
        --peers) PEERS="$2"; PEERS_SET=1; shift 2 ;;
        --conditions) CONDITIONS="$2"; CONDITIONS_SET=1; shift 2 ;;
        --matches) MATCHES="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --match) KIND="$2"; shift 2 ;;
        --start) START="$2"; shift 2 ;;
        --players) PLAYERS="$2"; shift 2 ;;
        --delay) DELAY="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --disconnect) DISCONNECT=1; shift ;;
        --tests) DISC_TESTS="$2"; shift 2 ;;
        --series-disconnect) SERIESDISC=1; shift ;;
        --map-transfer) MAPTEST=1; shift ;;
        --smoke) SMOKE=1; shift ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done

SERIES=0
MAP_ARGS_SERIES=()
case "$KIND" in
    tournament) SERIES=1 ;;
    tour) SERIES=1; MAP_ARGS_SERIES=(--at-map 0smw) ;;
    world) SERIES=1; MAP_ARGS_SERIES=(--at-map MrMister_Simple) ;;
esac
[ $SERIES -eq 1 ] && [ "$MATCHES" -eq 2 ] && MATCHES=30

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
        # line buffered: the disconnect tests watch the logs while the game runs
        time HOME="$home" SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
            $LINEBUF ./smw --datadir "${PEER_DATA[$peer]:-$ROOT/data}" --autotest --at-norender 1 --net-stats 300 ${NETPLAY_EXTRA_ARGS:-} "$@"
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
        --net-host "$port" --net-peers "$n" $DELAY_ARGS $cond $hostextra "${MAP_ARGS[@]}" "${MAP_ARGS_SERIES[@]}" &
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
        if [ $SERIES -eq 1 ]; then
            # The whole tournament/tour/world: as many matches as the host, the same series states
            local hostDone; hostDone=$(grep -c "^autotest: match [0-9]* ended" "$host")
            [ "$done" -eq "$hostDone" ] && [ "$done" -gt 0 ] || { ok=0; reasons+=" p$p:matches=$done"; }
            grep -q "^modesum: series final" "$log" || { ok=0; reasons+=" p$p:no-final"; }
            if [ "$p" -gt 0 ] && ! diff -q <(grep "^modesum:" "$host") <(grep "^modesum:" "$log") > /dev/null; then
                ok=0; reasons+=" p$p:modesum-differs"
            fi
        else
            [ "$done" -eq "$MATCHES" ] || { ok=0; reasons+=" p$p:matches=$done"; }
        fi
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
    if [ $SERIES -eq 1 ]; then
        echo "      $KIND: $(grep -c "^autotest: match [0-9]* ended" "$host") matches, $(grep -c "^modesum:" "$host") modesums, final: $(grep "^modesum: series final" "$host" | sed 's/^modesum: series final //')$(grep -q "^autotest: stuck" "$host" && echo " (the host's autotest got stuck on the world map and ended it)")"
    fi
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

if [ $SERIESDISC -eq 1 ]; then
    [ $SERIES -eq 1 ] && [ "$KIND" != tournament ] || { echo "--series-disconnect needs --match tour or world"; exit 2; }
    START_INDEX=$START
    name="seriesdisc-$KIND"
    start_session 3 "$name" "$PORT" "" ""
    screen=$([ "$KIND" = tour ] && echo "tour stop" || echo "world")
    for ((i = 0; i < 600; i++)); do
        [ "$(grep -c "screen [0-9]*: $screen" "$OUT/$name.p0.log" 2>/dev/null)" -ge 2 ] && break
        sleep 0.5
    done
    sleep 0.5
    pkill -9 -f "at-seed 9 --net-join 127.0.0.1:$PORT" 2>/dev/null
    for pid in "${PIDS[@]}"; do wait "$pid"; done
    fails=0
    for p in 0 1; do
        log="$OUT/$name.p$p.log"
        grep -q "^exit 0" "$log" || { echo "  FAIL: peer $p didn't exit cleanly"; fails=1; }
        grep -q "^modesum: series final" "$log" || { echo "  FAIL: peer $p didn't finish the series"; fails=1; }
        grep -q "DESYNC\|ERROR" "$log" && { echo "  FAIL: peer $p: desync/error"; fails=1; }
    done
    grep -q "is gone, their players are CPU players" "$OUT/$name.p0.log" || { echo "  FAIL: the host didn't replace the lost players"; fails=1; }
    diff -q <(grep "^modesum:" "$OUT/$name.p0.log") <(grep "^modesum:" "$OUT/$name.p1.log") > /dev/null || { echo "  FAIL: modesum differs"; fails=1; }
    echo "== series disconnect ($KIND): $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
    grep -hE "^netplay: series: .* gone|^modesum: series final|^netplay: series over" "$OUT/$name.p0.log" | sed "s/^/  host: /"
    exit $fails
fi

if [ $DISCONNECT -eq 1 ]; then
    # Waits until a peer's log shows that its match reached frame $2 (netstats lines)
    wait_frame() {
        local log=$1 frame=$2 t=0
        while [ $t -lt 600 ]; do
            local f; f=$(grep -o "^netstats: frame [0-9]*" "$log" 2>/dev/null | tail -1 | awk '{print $3}')
            [ -n "$f" ] && [ "$f" -ge "$frame" ] && return 0
            sleep 0.1; t=$((t + 1))
        done
        return 1
    }
    # Kills peer $2 of the session on port $1 (no goodbye)
    kill_peer() {
        if [ "$2" -eq 0 ]; then
            pkill -9 -f "net-host $1" 2>/dev/null
        else
            pkill -9 -f "at-seed $((7 + $2)) --net-join 127.0.0.1:$1" 2>/dev/null
        fi
    }

    [ $PEERS_SET -eq 1 ] || PEERS="3 4"
    [ $CONDITIONS_SET -eq 1 ] || CONDITIONS="a c"
    MATCHES=1
    START_INDEX=$START
    fails=0
    echo "logs: $OUT"
    echo "STATUS peers cond test         lost  survivors  end  sums  notes"
    for n in $PEERS; do
        for c in $CONDITIONS; do
            cond=$(condition_args "$c")
            [ "$cond" = "BAD" ] && { echo "unknown condition $c"; exit 2; }
            tests="kill-joiner quit-joiner kill-host"
            [ "$n" -ge 4 ] && tests="$tests kill-two"
            [ -n "$DISC_TESTS" ] && tests="$DISC_TESTS"
            for test in $tests; do
                name="disconnect-n${n}_$c-$test"
                port=$((PORT + 90))
                PORT=$((PORT + 3))
                rm -f "$OUT/$name".p*.log
                # the victims: the last joiner, the host, or the last two joiners
                case $test in
                    kill-joiner|quit-joiner) victims="$((n - 1))" ;;
                    kill-host) victims="0" ;;
                    kill-two) victims="$((n - 2)) $((n - 1))" ;;
                esac
                if [ $test = quit-joiner ]; then
                    start_session "$n" "$name" $port "$cond" "" "$((n - 1)):--net-quit-at 400"
                else
                    start_session "$n" "$name" $port "$cond" ""
                    # in the middle of the match (300 of $FRAMES frames)
                    v=${victims%% *}
                    wait_frame "$OUT/$name.p$v.log" 300 || echo "  (peer $v never reached frame 300)"
                    for v in $victims; do kill_peer $port "$v"; done
                fi
                for pid in "${PIDS[@]}"; do wait "$pid" 2>/dev/null; done

                # The survivors: everyone but the victims
                survivors=""
                for ((p = 0; p < n; p++)); do
                    case " $victims " in *" $p "*) ;; *) survivors="$survivors $p" ;; esac
                done
                ok=1; reasons=""
                first=${survivors# }; first=${first%% *}
                ref="$OUT/$name.p$first.log"
                ends=""
                for p in $survivors; do
                    log="$OUT/$name.p$p.log"
                    grep -q "^exit 0" "$log" || { ok=0; reasons+=" p$p:exit"; }
                    grep -q "^autotest: match 1 ended" "$log" || { ok=0; reasons+=" p$p:unfinished"; }
                    grep -q "^netplay: match 1 ended at confirmed frame" "$log" || { ok=0; reasons+=" p$p:no-confirmed-end"; }
                    if grep -q "DESYNC\|ERROR\|synctest: MISMATCH\|Segmentation\|Aborted" "$log"; then ok=0; reasons+=" p$p:desync/error"; fi
                    for v in $victims; do
                        # The process started as peer v may have joined in another order (load): its peer id is in its log
                        vid=$(grep -o "local peer [0-9]" "$OUT/$name.p$v.log" | head -1 | awk '{print $NF}')
                        [ -n "$vid" ] || vid=$v
                        grep -q "^netplay: peer $vid dropped from frame" "$log" || { ok=0; reasons+=" p$p:no-drop-of-$vid"; }
                    done
                    diff -q <(grep "^netsum:" "$ref") <(grep "^netsum:" "$log") > /dev/null || { ok=0; reasons+=" p$p:netsum-differs"; }
                    diff -q <(grep "^netplay: peer [0-9] dropped from frame" "$ref") <(grep "^netplay: peer [0-9] dropped from frame" "$log") > /dev/null ||
                        { ok=0; reasons+=" p$p:drop-frame-differs"; }
                    ends+=" $(grep -o "^netplay: match 1 ended at confirmed frame [0-9]*" "$log" | awk '{print $NF}')"
                done
                sums=$(grep -c "^netsum:" "$ref")
                [ "$sums" -gt 0 ] || { ok=0; reasons+=" no-netsum"; }
                lastsum=$(grep "^netsum:" "$ref" | tail -1 | awk '{print $5}')
                endframe=$(echo $ends | awk '{print $1}')
                [ -n "$endframe" ] && [ -n "$lastsum" ] && [ $((endframe - lastsum)) -lt 60 ] || { ok=0; reasons+=" netsum-stops-early($lastsum/$endframe)"; }
                drops=$(grep -o "^netplay: peer [0-9] dropped from frame [0-9]*" "$ref" | awk '{print "p" $3 "@" $NF}' | paste -sd, -)
                status="PASS"; [ $ok -eq 1 ] || { status="FAIL"; fails=$((fails + 1)); }
                printf "%-5s %5d  %-4s %-12s %-5s %-10s %4s %5d  %s%s\n" "$status" "$n" "$c" "$test" "$(echo $victims | tr ' ' ,)" \
                    "$(echo $survivors | tr ' ' ,)" "$endframe" "$sums" "${drops:-none}" "$reasons"
            done
        done
    done
    echo "columns: lost the peers killed or quitting, end the confirmed end frame, sums confirmed checksums compared,"
    echo "  notes: peer@frame from which the CPU played the lost peer's players"
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

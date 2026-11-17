#include "CmdArgs.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


namespace cmd {

AutoTestArgs g_autotest;
NetArgs g_net;
std::string g_replay;

void show_windows_console()
{
#ifdef _WIN32
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stderr);
    freopen("CONOUT$", "w", stdout);
#endif
}


void print_help(const char* title, const char* version)
{
    printf("%s %s\n\n", title, version);
    printf(
        "Super Mario War is a Super Mario multiplayer game. The goal is to stomp\n"
        "as many other Marios as possible to win the game. It's a tribute to Nintendo\n"
        "and the game Mario War by Samuele Poletto.\n\n");
    printf("Options:\n");
    printf("  -h, --help              Prints this help\n");
    printf("      --datadir <DIR>     Sets the data directory to DIR (default: ./data)\n");
    printf("      --debug             Shows the debug console on Windows\n");
    printf("\nHeadless auto-play (debugging aid, CPU players, no device input):\n");
    printf("      --autotest          Enables auto-play; the options below only apply with it\n");
    printf("      --at-match <KIND>   single (default), minigame, tournament, tour, world, menus or shots\n");
    printf("      --at-mode <N>       Game mode index 0..21 (minigame: 0 pipe, 1 hammer boss,\n"
           "                          2 bomb boss, 3 fire boss, 4 boxes); default: cycle all\n");
    printf("      --at-map <NAME>     Map short name (e.g. 0smw); default: cycle the map list\n"
           "                          (tour, world: the file name of the tour or world, e.g. 0smw)\n");
    printf("      --at-players <N>    Players 1..4 (default 4, 0 = cycle 2..4)\n");
    printf("      --at-frames <N>     Max frames per match (menus: total frames), default 3000\n");
    printf("      --at-matches <N>    Quit after N matches (tour/world: stages), default 1\n");
    printf("      --at-start <N>      Index of the first match in the mode/map cycle\n");
    printf("      --at-seed <N>       Seed the random number generators (reproducible run)\n");
    printf("      --at-cpu-level <N>  CPU difficulty 0-4 (default: the configured one)\n");
    printf("      --at-ai-old <MASK>  Players whose bit is set use the original CPU logic (AI comparisons)\n");
    printf("      --at-ai-bench 1     Print kills, deaths and score per player at the end of each match\n");
    printf("      --at-realtime <N>   Run at normal speed and print the measured tick and render\n"
           "                          rates, with frame rate 1: VSync, 2: Uncapped, 3: 62.5 FPS\n");
    printf("      --at-resim <0|1>    1: run every gameplay tick as a re-simulation (sounds,\n"
           "                          eyecandy and other side effects suppressed); the\n"
           "                          results must match a normal run\n");
    printf("      --at-runahead <N>   Run-ahead (online): show the match N ticks (0..3) ahead; the\n"
           "                          simulation (and its checksums) must be unaffected\n");
    printf("      --at-shots <DIR>    --at-match shots: save screenshots of the main, online and lobby menus\n"
           "                          to DIR (with --at-role host|join|orig, --at-addr IP[:PORT], --at-nick NAME)\n");
    printf("      --at-norender <0|1> 1: don't draw or present frames (faster headless runs,\n"
           "                          e.g. under valgrind); the simulation is unaffected\n");
    printf("      --at-humans <N>     The first N players are \"human\" players, driven by\n"
           "                          seeded random input from outside the simulation\n");
    printf("      --at-synctest <N>   Rollback sync test: every tick, roll back up to N ticks,\n"
           "                          re-simulate them and check that the state is identical\n");
    printf("      --at-peersum <N>    Print the cross-machine state checksum (\"peersum:\" lines)\n"
           "                          every N frames, to compare runs of different builds\n");
    printf("      --at-shot-every <N> Save a PNG of the screen every N drawn frames of a\n"
           "                          match (needs rendering), into --at-shot-dir <DIR>\n");
    printf("      --at-shot-dir <DIR> Where --at-shot-every saves the screenshots\n");
    printf("      --at-shot-ticks 1   --at-shot-every counts simulation ticks, not drawn frames\n");
    printf("\nRollback netplay (testing; combines with --autotest):\n");
    printf("      --net-host <PORT>   Host a lobby on this UDP port\n");
    printf("      --net-peers <N>     --autotest host: wait for N peers, the host included (default 2)\n");
    printf("      --net-join <IP:PORT> Join the lobby of a host\n");
    printf("      --net-name <NAME>   Nickname in the lobby (default \"Player N\")\n");
    printf("      --net-skin <NAME[,NAME2]> Skin of the local player(s) in the lobby (testing)\n");
    printf("      --net-port <PORT>   Local UDP port of a joiner (default: any)\n");
    printf("      --net-slow-map <N>  Send one map chunk per N frames (to look at the transfer)\n");
    printf("      --net-delay <N>     Input delay in ticks (default: automatic, from the ping)\n");
    printf("      --net-checksum <N>  Compare state checksums every N frames (\"netsum:\" lines)\n");
    printf("      --net-stats <N>     Print \"netstats:\" lines every N frames (default 300, 0 off)\n");
    printf("      --net-timeout <MS>  A peer that sends nothing for this long is gone (5000)\n");
    printf("      --net-quit-at <F>   Leave the match at frame F (disconnect test)\n");
    printf("      --net-upnp <0|1>    Ask the router to forward the host's port with UPnP\n"
           "                          (default: yes, except with --autotest)\n");
    printf("  --replay <FILE>         Play a replay (from the replays folder, or a path);\n");
    printf("                          with --autotest, quit when it ends\n");
    printf("      --net-sim-latency <MS> Simulated one-way latency of every packet sent\n");
    printf("      --net-sim-jitter <MS>  Simulated +- jitter of that latency\n");
    printf("      --net-sim-loss <PCT>   Simulated loss of the unreliable packets sent\n");
}


Args parse_args(int argc, char* argv[])
{
    Args result;
    AutoTestArgs& at = g_autotest;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            result.success = true;
            result.show_help = true;
            return result;
        }

        if (arg == "--debug") {
            result.debug = true;
            continue;
        }

        if (arg == "--autotest") {
            at.enabled = true;
            continue;
        }

        if (arg.rfind("--at-", 0) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: `%s` requires a parameter, see `--help`\n", arg.c_str());
                return result;
            }
            const std::string val = argv[i];
            if (arg == "--at-match") {
                if (val == "single") at.kind = AutoTestArgs::Kind::Single;
                else if (val == "minigame") at.kind = AutoTestArgs::Kind::Minigame;
                else if (val == "tour") at.kind = AutoTestArgs::Kind::Tour;
                else if (val == "world") at.kind = AutoTestArgs::Kind::World;
                else if (val == "tournament") at.kind = AutoTestArgs::Kind::Tournament;
                else if (val == "menus") at.kind = AutoTestArgs::Kind::Menus;
                else if (val == "shots") at.kind = AutoTestArgs::Kind::Shots;
                else {
                    fprintf(stderr, "Error: unknown `--at-match` kind `%s`, see `--help`\n", val.c_str());
                    return result;
                }
            }
            else if (arg == "--at-mode") at.mode = atoi(val.c_str());
            else if (arg == "--at-map") at.map = val;
            else if (arg == "--at-players") at.players = atoi(val.c_str());
            else if (arg == "--at-frames") at.frames = atoi(val.c_str());
            else if (arg == "--at-matches") at.matches = atoi(val.c_str());
            else if (arg == "--at-start") at.start = atoi(val.c_str());
            else if (arg == "--at-seed") at.seed = atoi(val.c_str());
            else if (arg == "--at-cpu-level") at.cpuLevel = atoi(val.c_str());
            else if (arg == "--at-ai-old") at.aiLegacyMask = atoi(val.c_str());
            else if (arg == "--at-ai-bench") at.aiBench = atoi(val.c_str()) != 0;
            else if (arg == "--at-replay-seeks") at.replaySeeks = atoi(val.c_str());
            else if (arg == "--at-prompts") at.prompts = atoi(val.c_str());
            else if (arg == "--at-colors") at.colors = atoi(val.c_str());
            else if (arg == "--at-shot-ticks") at.shotTicks = atoi(val.c_str()) != 0;
            else if (arg == "--at-resim") at.resim = atoi(val.c_str()) != 0;
            else if (arg == "--at-runahead") at.runahead = atoi(val.c_str());
            else if (arg == "--at-realtime") at.realtime = atoi(val.c_str());
            else if (arg == "--at-humans") at.humans = atoi(val.c_str());
            else if (arg == "--at-synctest") at.synctest = atoi(val.c_str());
            else if (arg == "--at-peersum") at.peersum = atoi(val.c_str());
            else if (arg == "--at-norender") at.norender = atoi(val.c_str()) != 0;
            else if (arg == "--at-shot-every") at.shotEvery = atoi(val.c_str());
            else if (arg == "--at-shot-dir") at.shotDir = val;
            else if (arg == "--at-shots") at.shotsDir = val;
            else if (arg == "--at-role") at.shotsRole = val;
            else if (arg == "--at-addr") at.shotsAddr = val;
            else if (arg == "--at-nick") at.shotsName = val;
            else if (arg == "--at-shotmap") at.shotsMap = atoi(val.c_str());
            else {
                fprintf(stderr, "Error: unknown option `%s`, see `--help`\n", arg.c_str());
                return result;
            }
            continue;
        }

        if (arg.rfind("--net-", 0) == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: `%s` requires a parameter, see `--help`\n", arg.c_str());
                return result;
            }
            const std::string val = argv[i];
            NetArgs& net = g_net;
            if (arg == "--net-host") net.hostPort = atoi(val.c_str());
            else if (arg == "--net-peers") net.peers = atoi(val.c_str());
            else if (arg == "--net-join") {
                const size_t colon = val.rfind(':');
                if (colon == std::string::npos) {
                    fprintf(stderr, "Error: `--net-join` expects IP:PORT\n");
                    return result;
                }
                net.joinHost = val.substr(0, colon);
                net.joinPort = atoi(val.c_str() + colon + 1);
            }
            else if (arg == "--net-port") net.port = atoi(val.c_str());
            else if (arg == "--net-name") net.name = val;
            else if (arg == "--net-skin") net.skin = val;
            else if (arg == "--net-slow-map") net.slowMap = (std::max)(1, atoi(val.c_str()));
            else if (arg == "--net-delay") net.delay = atoi(val.c_str());
            else if (arg == "--net-checksum") net.checksumInterval = atoi(val.c_str());
            else if (arg == "--net-stats") net.statsInterval = atoi(val.c_str());
            else if (arg == "--net-timeout") net.timeoutMs = atoi(val.c_str());
            else if (arg == "--net-quit-at") net.quitAt = atoi(val.c_str());
            else if (arg == "--net-upnp") net.upnp = atoi(val.c_str());
            else if (arg == "--net-sim-latency") net.simLatencyMs = atoi(val.c_str());
            else if (arg == "--net-sim-jitter") net.simJitterMs = atoi(val.c_str());
            else if (arg == "--net-sim-loss") net.simLossPercent = static_cast<float>(atof(val.c_str()));
            else {
                fprintf(stderr, "Error: unknown option `%s`, see `--help`\n", arg.c_str());
                return result;
            }
            continue;
        }

        if (arg == "--replay") {
            if (++i >= argc) {
                fprintf(stderr, "Error: `--replay` requires a parameter, see `--help`\n");
                return result;
            }
            g_replay = argv[i];
            continue;
        }

        if (arg == "--datadir") {
            if (++i >= argc) {
                fprintf(stderr, "Error: `--datadir` requires a parameter, see `--help`\n");
                return result;
            }
            result.data_root = argv[i];
            continue;
        }
    }

    result.success = true;
    return result;
}

} //namespace cmd

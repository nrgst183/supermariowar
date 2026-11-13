#include "CmdArgs.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


namespace cmd {

AutoTestArgs g_autotest;

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
    printf("\nHeadless auto-play (debugging aid, all CPU players, no human input):\n");
    printf("      --autotest          Enables auto-play; the options below only apply with it\n");
    printf("      --at-match <KIND>   single (default), minigame, tour, world or menus\n");
    printf("      --at-mode <N>       Game mode index 0..21 (minigame: 0 pipe, 1 hammer boss,\n"
           "                          2 bomb boss, 3 fire boss, 4 boxes); default: cycle all\n");
    printf("      --at-map <NAME>     Map short name (e.g. 0smw); default: cycle the map list\n");
    printf("      --at-players <N>    CPU players 1..4 (default 4, 0 = cycle 2..4)\n");
    printf("      --at-frames <N>     Max frames per match (menus: total frames), default 3000\n");
    printf("      --at-matches <N>    Quit after N matches (tour/world: stages), default 1\n");
    printf("      --at-start <N>      Index of the first match in the mode/map cycle\n");
    printf("      --at-seed <N>       Seed the random number generators (reproducible run)\n");
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
                else if (val == "menus") at.kind = AutoTestArgs::Kind::Menus;
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
            else {
                fprintf(stderr, "Error: unknown option `%s`, see `--help`\n", arg.c_str());
                return result;
            }
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

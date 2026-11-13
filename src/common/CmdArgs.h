#pragma once

#include <string>


namespace cmd {

/// Options of the opt-in headless auto-play harness (a debugging aid).
/// Everything is off unless `--autotest` is given.
struct AutoTestArgs {
    enum class Kind { Single, Minigame, Tour, World, Menus };

    bool enabled = false;
    Kind kind = Kind::Single;
    int mode = -1;           // game mode index (0..21, or 0..2 for minigames); -1 = cycle through all
    std::string map;         // exact map name; empty = cycle through the map list
    int players = 4;         // number of CPU players (1..4); 0 = cycle 2, 3, 4
    int frames = 3000;       // max frames per match (Menus: total menu frames)
    int matches = 1;         // quit after this many matches (Tour/World: stages)
    int start = 0;           // index of the first match in the mode/map cycle
    int seed = 0;            // RNG seed for reproducible runs; 0 = time based
};

extern AutoTestArgs g_autotest;

struct Args {
    bool success = false;
    bool show_help = false;
    bool debug = false;
    std::string data_root;
};
Args parse_args(int argc, char* argv[]);


void print_help(const char* title, const char* version);
void show_windows_console();

} // namespace cmd

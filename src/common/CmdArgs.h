#pragma once

#include <string>


namespace cmd {

/// Options of the opt-in headless auto-play harness (a debugging aid).
/// Everything is off unless `--autotest` is given.
struct AutoTestArgs {
    enum class Kind { Single, Minigame, Tour, World, Menus, Shots };

    bool enabled = false;
    Kind kind = Kind::Single;
    int mode = -1;           // game mode index (0..21, or 0..2 for minigames); -1 = cycle through all
    std::string map;         // exact map name; empty = cycle through the map list
    int players = 4;         // number of players (1..4); 0 = cycle 2, 3, 4
    int frames = 3000;       // max frames per match (Menus: total menu frames)
    int matches = 1;         // quit after this many matches (Tour/World: stages)
    int start = 0;           // index of the first match in the mode/map cycle
    int seed = 0;            // RNG seed for reproducible runs; 0 = time based
    bool resim = false;      // run every gameplay tick as a re-simulation (side effects suppressed)
    int realtime = 0;        // 0: unthrottled; 1/2/3: normal pacing with the VSync/Uncapped/62.5 FPS frame
                             // rate, printing the measured tick and render rates
    int humans = 0;          // the first N players are humans, with seeded random input (not from devices)
    int synctest = 0;        // rollback sync test distance in ticks; 0 = off
    int peersum = 0;         // print the cross-machine state checksum every N frames; 0 = off
    bool norender = false;   // skip drawing and presenting (much faster under valgrind or sanitizers)
    int shotEvery = 0;       // save a PNG of the screen every N drawn frames of an online match; 0 = off
    std::string shotDir;     // ... into this directory
    // --at-match shots: walks through the main and online menus and saves screenshots (PNG) of them
    std::string shotsDir;    // --at-shots DIR: where the screenshots go
    std::string shotsRole = "host";  // --at-role host|join: host a lobby, or join --at-addr
    std::string shotsAddr;   // --at-addr IP[:PORT]: the lobby to join
    std::string shotsName;   // --at-nick NAME: the nickname to enter in the online menu
    int shotsMap = 0;        // --at-shotmap N: host: pick the N-th map of the list (to be downloaded by joiners without it)
};

extern AutoTestArgs g_autotest;

/// Options of the rollback netplay (for testing until there is a menu for it)
struct NetArgs {
    int hostPort = 0;          // --net-host PORT: host a session
    int peers = 2;             // --net-peers N: (autotest host) wait for N peers, the host included
    std::string joinHost;      // --net-join IP:PORT: join a session
    int joinPort = 0;
    int port = 0;              // --net-port: the local UDP port of a joiner (0: any)
    std::string name;          // --net-name: nickname in the lobby
    int delay = -1;            // --net-delay: input delay in ticks (-1: automatic)
    int checksumInterval = 60; // --net-checksum N: compare (and print) state checksums every N frames
    int statsInterval = 300;   // --net-stats N: print statistics every N frames (0: off)
    int timeoutMs = 5000;      // --net-timeout MS: a silent peer is gone after this long
    int quitAt = 0;            // --net-quit-at F: leave the match at frame F (disconnect test)
    // Network condition simulator (testing only)
    int simLatencyMs = 0;      // --net-sim-latency MS: added one-way delay of every packet sent
    int simJitterMs = 0;       // --net-sim-jitter MS: +- random variation of that delay
    float simLossPercent = 0;  // --net-sim-loss PCT: drop this share of the unreliable packets sent

    bool enabled() const { return hostPort > 0 || !joinHost.empty(); }
};

extern NetArgs g_net;

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

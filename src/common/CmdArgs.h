#pragma once

#include <string>


namespace cmd {

/// Options of the opt-in headless auto-play harness (a debugging aid).
/// Everything is off unless `--autotest` is given.
struct AutoTestArgs {
    enum class Kind { Single, Minigame, Tour, World, Menus, Shots, Tournament, Screens, History, Editors };

    bool everything = false;  // --at-match everything: menus, every mode, minigames, a tournament, a tour
                              // and a world, one after the other (the stage's kind in `kind`)

    bool enabled = false;
    Kind kind = Kind::Single;
    int mode = -1;           // game mode index (0..21, or 0..2 for minigames); -1 = cycle through all
    std::string map;         // exact map name; empty = cycle through the map list
    std::string rankMapsFile;    // --at-rank-maps FILE: every map loaded and scored (CMap::busyScore), the busiest
                                 // written to FILE (a name a line: an --at-maps list), then quit
    std::string mapsFile;    // --at-maps FILE: each match's map picked at random from these (a name a line),
                             // a different order every run (the memory test's busiest maps)
    int players = 4;         // number of players (1..4); 0 = cycle 2, 3, 4
    int frames = 3000;
    int speed = 1;           // --at-speed N: a match's ticks N at a time, drawn once (faster runs that still draw)       // max frames per match (Menus: total menu frames)
    int matches = 1;         // quit after this many matches (Tour/World: stages)
    int start = 0;           // index of the first match in the mode/map cycle
    int seed = 0;            // RNG seed for reproducible runs; 0 = time based
    int cpuLevel = -1;       // CPU difficulty 0-4; -1 = keep the configured one
    int aiLegacyMask = 0;    // bit k: player k uses the original CPU logic (for comparing AI versions)
    int aiPlainMask = 0;     // --at-ai-plain: players whose bit is set don't use the newest AI changes (A/B tests)
    bool aiBench = false;    // print kills/deaths per player at the end of each match
    bool shotTicks = false;  // --at-shot-every counts simulation ticks instead of drawn frames
    bool resim = false;      // run every gameplay tick as a re-simulation (side effects suppressed)
    int runahead = 0;        // --at-runahead N: run-ahead ticks online, -1 Auto (the setting isn't used in auto-play)
    bool onlinePanel = false;   // --at-online-panel 1: the online panel (pause button, online) open in the matches
    int realtime = 0;        // 0: unthrottled; 1/2/3: normal pacing with the VSync/Uncapped/62.5 FPS frame
                             // rate, printing the measured tick and render rates
    int humans = 0;          // the first N players are humans, with seeded random input (not from devices)
    int synctest = 0;        // rollback sync test distance in ticks; 0 = off
    int peersum = 0;         // print the cross-machine state checksum every N frames; 0 = off
    int determinism = 0;     // --at-determinism N: the determinism test's suite, N times (src/smw/Determinism.h)
    std::string determinismRef;  // --at-determinism-ref FILE: the checksums to compare with (another build's)
    std::string determinismOut;  // --at-determinism-out FILE: where to write this run's
    int colors = -1;         // --at-colors N: the "Overlay Colors" option (0 night, 1 pipe, 2 castle, 3 SMB3)
    int prompts = -1;        // --at-prompts N: the "Button prompts" option (0 auto, 1 Xbox, 2 original Xbox)
    bool pad = false;
    int pace = 1;
    int mmStart = -1;        // --at-mm-start N: the Map Maker's rules: 0 builders start building, 1 playing
    int mmBlank = -1;        // --at-mm-blank 1: the Map Maker starts from a blank map            // --at-pace N: --at-role matchedit's buttons each held N frames (a person's pace: 15)
    bool perf = false;
    int powerup = -1;        // --at-powerup N: every player (re)spawns with powerup N (3 feather, 8 P-Wings...: AI comparisons)
    int itemRespawn = -1;    // --at-item-respawn N: ticks before a ? block gives an item again (the option)       // --at-perf 1: each match's cost per tick and per frame (perf: lines)        // --at-pad 1: the prompts as for a controller (as if one had been used last)
    int replaySeeks = 0;     // --replay: jump to a random tick every N frames (tests the theater's seeking)
    bool norender = false;   // skip drawing and presenting (much faster under valgrind or sanitizers)
    bool minimized = false;  // --at-minimized 1: the window opens minimized (runs in the background)
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
    std::string skin;          // --net-skin NAME[,NAME2]: skin of our player(s) (testing: skins are otherwise chosen in the menus)
    int skinDownloads = -1;    // --net-skin-downloads 0/1: overrides the setting (testing: the "?" of a skin not here)
    int mapDownloads = -1;     // --net-map-downloads 0/1: overrides the setting (testing: a peer that won't download the map)
    int slowMap = 1;           // --net-slow-map N: the host sends one map chunk per N frames (testing: to catch the transfer in screenshots)
    int delay = -1;            // --net-delay: input delay in ticks (-1: automatic)
    int checksumInterval = 60; // --net-checksum N: compare (and print) state checksums every N frames
    int statsInterval = 300;   // --net-stats N: print statistics every N frames (0: off)
    int timeoutMs = 5000;      // --net-timeout MS: a silent peer is gone after this long
    int quitAt = 0;            // --net-quit-at F: leave the match at frame F (disconnect test)
    int upnp = -1;             // --net-upnp 0|1: ask the router to forward the host's port (-1: yes, except with --autotest)
    // Network condition simulator (testing only)
    int simLatencyMs = 0;      // --net-sim-latency MS: added one-way delay of every packet sent
    int simJitterMs = 0;       // --net-sim-jitter MS: +- random variation of that delay
    float simLossPercent = 0;  // --net-sim-loss PCT: drop this share of the unreliable packets sent

    bool enabled() const { return hostPort > 0 || !joinHost.empty(); }
};

extern NetArgs g_net;

/// --replay FILE: plays a replay (a file in the replays folder, or a path) at
/// startup; with --autotest, the game quits when it ends
extern std::string g_replay;

struct Args {
    bool success = false;
    bool show_help = false;
    bool debug = false;
    bool lowMemory = false;    // --low-memory: the Xbox's way with memory (platform::lowMemory), here too
    std::string data_root;
};
Args parse_args(int argc, char* argv[]);


void print_help(const char* title, const char* version);
void show_windows_console();

} // namespace cmd

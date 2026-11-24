#pragma once

#include <cstdint>
#include <string>
#include <vector>

// THE MATCH HISTORY: every match that was played, local or online, finished
// or left early, with its replay (rollback/Replay.h).
//
// Two files in the settings folder, one entry appended per match:
//   match_history.txt    to read: the date, the mode and goal, the map, the
//                        teams with their players and final scores
//   match_history.jsonl  the same as one JSON object per line, for tools and
//                        for the Match History screen (MenuMatchHistory.cpp)
// The replays are in replays/ (the newest 200 are kept).
namespace history {

struct Player {
    int slot = 0;
    std::string name;      // "Player 1", "CPU", the online name
    std::string skin;      // the skin's name
    bool human = true;
};

struct Team {
    int score = 0;
    int place = 1;         // 1: first (teams with the same score share it)
    std::vector<Player> players;
};

struct Entry {
    int64_t time = 0;          // unix time of the end
    std::string matchType;     // "Single Game", "Tournament", ...
    std::string mode;          // "Classic", ...
    int modeId = -1;           // the game mode (for its icon)
    int goal = 0;
    std::string map;
    int ticks = 0;             // 62.5 per second
    bool online = false;
    std::string result;        // "won", "draw", "left"
    int winner = -1;           // the winning team (result "won")
    std::vector<Team> teams;
    std::string replay;        // file in replay::replayDir(), "" if none
};

// The match that just ended (or was left): from the game's state. Called
// once per match, when the gameplay state is left; nothing during a replay.
void recordMatchEnd(const std::string& replayFile, bool online);

// The map of the match recorded last: its thumbnail saved, if there is none yet, for the history's
// details (which otherwise draws it there, a pause on the screen). The menus call it as they come
// back from the match, its map still g_map and the match's sprites gone
void saveMatchThumbnail();

// The entries of match_history.jsonl, newest first (at most `max`)
std::vector<Entry> load(size_t max = 1000);

std::string textFile();
std::string jsonFile();

// For both files and the screen: "3:07", "Sun 2026-11-15 09:43"
std::string formatDuration(int ticks);
std::string formatTime(int64_t time);
// "11-15 09:43"
std::string formatShortTime(int64_t time);

// One entry as JSON / back (only what toJson writes)
std::string toJson(const Entry& entry);
bool fromJson(const std::string& line, Entry& entry);

} // namespace history

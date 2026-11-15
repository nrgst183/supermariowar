#pragma once

#include "InputFrame.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// REPLAYS (match history, see MatchHistory.h)
//
// The simulation is deterministic (the rollback netplay depends on it), so a
// match is its match config (MatchConfig.h: the settings, the players, the
// map and the RNG seed) plus the input of every tick. A replay plays it again
// by starting the match from the config, like an online peer does
// (startConfiguredMatch), and feeding it the recorded inputs.
//
// Recorded per tick: the four input bytes and the slots the CPU had taken over
// (online, the players of peers that left). Every 60 ticks the state checksum
// (peerChecksum) is recorded too: the playback compares them, so a replay that
// plays differently (another game version, other tile types) says so.
//
// The map file is stored in the replay: a replay still plays after the map was
// changed or deleted (the copy goes to the map download folder, like a map that
// a lobby sent).
//
// The players' skin files are stored too (a replay shows the same players on
// another computer: a skin that isn't there goes to the skin download folder),
// and the music: the song the match started with and every change of song
// (by tick), since a match picks its songs at random.
//
// File (little-endian): "SMWREPLAY" + u8 0, u16 version, u64 simulation data
// hash, i64 start time (unix), u32 + config bytes (MatchConfig::serialize),
// 4 x (str skin path, str player name), str map name, u32 + map bytes,
// u32 tick count, u32 runs + runs of (u16 length, 4 input bytes, u8 CPU mask)
// (version 4 and on: for each player, then the CPU mask, u32 runs + runs of
// (varint length, u8 byte)),
// u32 checksums + (u32 tick, u64 checksum); version 2 and on: 4 x (u32 + skin
// file bytes), u32 songs + (u32 tick, str song: relative to the data folder
// when it's in it); version 3 and on: u32 windows + 4 x u16 average ping per
// window; version 4 and on: the CPU players' bytes are in the ticks, then u32 changes
// + (u32 tick, u8 slot, u16 keys) of the buttons of CPUs that took over a human; str = u16 length + bytes. Older replays still play.
struct COutputControl;

namespace replay {

struct TickRecord {
    InputFrame input;
    uint8_t cpuMask = 0;

    bool operator==(const TickRecord&) const = default;
};

struct Replay {
    static constexpr uint16_t VERSION = 4;
    static constexpr int PING_WINDOW = 60;    // ticks (about a second)
    static constexpr uint16_t NO_PING = 0xffff;

    uint64_t dataHash = 0;
    int64_t startTime = 0;
    std::vector<uint8_t> config;
    std::array<std::string, INPUT_MAX_PLAYERS> skins;
    std::array<std::string, INPUT_MAX_PLAYERS> names;
    std::string mapName;
    std::vector<uint8_t> map;
    std::vector<TickRecord> ticks;
    std::map<uint32_t, uint64_t> checksums;    // after tick N (g_sim.tick == N)
    std::array<std::vector<uint8_t>, INPUT_MAX_PLAYERS> skinFiles;    // (version 2)
    std::vector<std::pair<uint32_t, std::string>> music;              // (version 2) from tick N on, this song
    // (version 3) online: each player's average ping (ms) over each PING_WINDOW ticks, as the recording
    // computer saw it (NO_PING: none, a player on that computer or a CPU)
    std::vector<std::array<uint16_t, INPUT_MAX_PLAYERS>> pings;
    // The file's version (what the simulation of its time did differently)
    uint16_t version = VERSION;
    // (version 4) From version 4 on, CPU players play by input bytes in the ticks like the human
    // ones (see CpuDriver.h): a replay keeps its CPUs' play when the AI changes. Only a CPU that
    // took over a human that left an online match thinks inside the simulation; its buttons
    // after it thought, whenever they changed, are these: what it held (bits 0-7) and had just
    // pressed (8-15). The playback uses them instead of that AI's own (it still thinks, for its
    // state and the random numbers).
    struct CpuKeys {
        uint32_t tick;
        uint8_t slot;
        uint16_t keys;
    };
    std::vector<CpuKeys> cpuKeys;

    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& bytes);
};

bool writeReplayFile(const std::string& path, const Replay& replay);
bool readReplayFile(const std::string& path, Replay& replay, std::string& error);

// The folder of the replays (settings folder + "replays/")
std::string replayDir();

// ---- recording (every match: local and online)

// A local match: the menus set it up and loaded `mapPath`. Reseeds the
// gameplay generator (the seed goes in the config). Call it where the
// configured start (startConfiguredMatch) seeds: before the map objects load.
void beginLocal(const std::string& mapPath);
// An online match: the config every peer starts from
void beginOnline(const std::vector<uint8_t>& config, const std::array<std::string, INPUT_MAX_PLAYERS>& names,
    const std::array<std::string, INPUT_MAX_PLAYERS>& skins);
bool recording();
// Before each simulation tick (GameplayState::advanceTick); online, a rolled
// back tick is recorded again with its corrected input
void recordTick(const InputFrame& input);
// After a tick (not online: there the confirmed checksums come in)
void afterTick();
// The match's song changed (any song the game picks for it, at random or the next one)
void noteMusic();
// Online: a confirmed checksum (after tick `frame`)
void recordChecksum(int frame, uint64_t sum);
// The match is over: writes the replay (the ticks up to the current one).
// Returns its file name in replayDir() ("" if there was nothing to write).
std::string finishRecording();
void cancelRecording();

// ---- playback

// Loads the replay and starts its match (the gameplay state takes over).
// The menu's settings are saved and come back with stopPlayback().
bool startPlayback(const std::string& file, std::string& error);
bool playing();
// The next tick's input (and the CPU takeover); false at the end
bool nextTick(InputFrame& input, uint8_t& cpuMask);
// After a tick: compares the state with the recorded checksum
void checkTick();
// The recorded song of the current tick plays (after a start, a step, a jump)
void syncMusic();
// An online replay: a player's average ping at the current tick, as recorded (-1: none)
int pingAt(int slot);

// CPU players play by input bytes (false only while playing a replay from before version 4,
// whose CPUs thought inside the simulation)
bool cpuInputs();

// A CPU player has thought (CPlayer::move): recording, its buttons are kept; playing a replay that
// has them, they become the recorded ones
void afterCpuThink(int slot, COutputControl& keys);

// ---- the theater (GameplayTheater.cpp): moving around in a replay
//
// When a replay starts, it is played through once quietly (as a rollback
// re-simulation: no sounds or effects, a few milliseconds): every
// KEYFRAME_TICKS a snapshot of the state is kept (rollback/Snapshot.h), and
// the score changes become the timeline's markers. Seeking loads the
// snapshot before the tick and runs the rest.
constexpr int KEYFRAME_TICKS = 250;    // 4 seconds

struct Marker {
    int tick = 0;
    int team = 0;
    int delta = 0;       // the score change (0: the match was decided)
};
const std::vector<Marker>& markers();
// Runs the next tick of the replay; false at the end
bool step();
// The state after `tick` ticks (g_sim.tick == tick)
void seek(int tick);
// A player's name in the replay ("" if none)
const std::string& playerName(int slot);
// The replay's map name and date (for the theater's title)
const std::string& mapName();
int64_t startTime();
// Restores the menu's settings
void stopPlayback();
// The menu, once: true if it is back from a replay (it shows the match history)
bool takeReturnedFromReplay();

struct PlaybackStatus {
    int tick = 0;
    int total = 0;
    bool diverged = false;    // a checksum didn't match
    int divergedAt = -1;
    int checksumsMatched = 0;
    bool finished = false;
};
const PlaybackStatus& playbackStatus();

} // namespace replay

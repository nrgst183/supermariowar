#pragma once

#include "InputFrame.h"
#include "MatchTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// THE MATCH CONFIGURATION OF AN ONLINE MATCH
//
// Everything that decides how a match plays out, apart from the inputs: the
// host captures it from its menus and sends it; every peer (the host too)
// starts the match from the received copy, so all of them begin from the
// same state.
//
// Wire format (MatchConfig::serialize, little-endian):
//   u16 format version
//   u32 RNG seed, u8 match type, i16 game mode, i16 goal, u8 boss type
//   str map reference, u64 map file hash
//   u8 input delay, i32 frame limit, i16 game over ticks
//   4 x slot: u8 control (0 empty, 1 human, 2 CPU), i8 driving peer (-1 for
//             CPU and empty slots), i16 color, u8 keyboard, i16 stored powerup
//   i16 team count, 4 x 3 x i16 team members, 4 x i16 team sizes
//   u16 size + the simulation settings (simulationSettings() in MatchConfig.cpp:
//   every game_values setting the simulation reads, and all game mode settings)
//   u16 size + the series state (SeriesState.h; empty outside a tournament,
//   tour or world)
//
// THE RULE FOR NEW SETTINGS: a setting that the simulation reads must be
// listed in visitSimulationSettings() (MatchConfig.cpp), or peers with
// different local settings desync.
namespace rollback {

struct MatchSlot {
    uint8_t control = 0;     // game_values.playercontrol: 0 empty, 1 human, 2 CPU
    int8_t peer = -1;        // the peer whose inputs drive it (humans only)
    int16_t color = 0;
    bool keyboard = true;    // keyboard players drop through platforms with "down"
    int16_t storedPowerup = -1;
};

struct MatchConfig {
    static constexpr uint16_t FORMAT_VERSION = 3;

    uint32_t seed = 0;
    MatchType matchType = MatchType::SingleGame;
    int16_t gameMode = 0;
    int16_t goal = 0;
    uint8_t bossType = 0;
    std::string mapRef;      // path below the data directory, or "@" + map name
    uint64_t mapHash = 0;
    uint8_t inputDelay = 2;
    // The match ends (for everyone, at a confirmed frame) at this frame (0:
    // never; --autotest's frame limit) and this many frames after game over
    int32_t frameLimit = 0;
    int16_t gameOverTicks = 310;
    std::array<MatchSlot, INPUT_MAX_PLAYERS> slots {};
    int16_t teamCount = 0;
    int16_t teamIds[4][3] = {};
    int16_t teamCounts[4] = {};
    std::vector<uint8_t> settings;
    // Tournament, tour, world: the host's state of the series (SeriesState.h)
    std::vector<uint8_t> series;

    std::vector<uint8_t> serialize() const;
    // False if the data is malformed
    bool deserialize(const uint8_t* data, size_t size);
};

// Host: the match the menus set up (game_values, the current map), with
// `peerOfSlot` telling which peer drives each human slot.
// False (with a message) if this match can't be played online.
bool captureMatchConfig(MatchConfig& config, const std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot, uint32_t seed, int inputDelay, std::string& error);

// A local match for its replay (Replay.h): the match the menus set up, on the
// map they loaded (any match type; every human slot is "peer 0")
bool captureLocalMatchConfig(MatchConfig& config, const std::string& loadedMap, uint32_t seed, std::string& error);
// Tournament, tour and world: the multi-match modes (NetSeries.h)
bool isSeriesMatchType(MatchType type);
// Before a series match is captured or compared: the series state that the
// match doesn't use is normalized (the single player of a bonus house)
void prepareSeriesStateForMatch(int gameMode);

// Every peer: checks that the match can be played here. `needMap` (if given)
// tells whether what is missing is the map (it can be downloaded: then it is
// found by its hash in mapDownloadDirs())
bool checkMatchConfig(const MatchConfig& config, std::string& error, bool* needMap = nullptr);

// The map file of a match here ("" or a file with other contents if it's missing)
std::string matchMapPath(const MatchConfig& config);
// Where downloaded maps are saved and looked for: maps/net/ in the data
// directory, else in the home directory (if the data directory is read-only)
std::vector<std::string> mapDownloadDirs();
// Skins: the folders whose skin files are compared by hash (the game's skins and the downloaded
// ones), and where downloaded skins are saved (skins/net/ in the data directory, else the home directory)
std::vector<std::string> skinSearchDirs();
std::vector<std::string> skinDownloadDirs();
// Loads the sprites of the player in a slot of the running online match: the skin of its owner
// (downloaded if need be), the default skin while it isn't here. Cosmetic only.
void loadMatchSkin(int slot, short color);
// A computer player's skin (an index into the skin list, -1: the slot isn't a CPU): a random one,
// picked when the slot became a CPU and kept (the lobby's card and the match show the same).
// Cosmetic only: each machine picks its own.
short cpuSkin(int slot);
// A skin picked for a computer player in the lobby (the host's player 1, after its own): that CPU's
// from then on, random skins on or off, until the slot stops being a CPU
void pickCpuSkin(int slot, short skin);
/// The skin a local match gives `slot`: a computer player's random one (cpuSkin), else its own
short matchSkinId(int slot);
// A replay: the skin files of its players, used instead of the lobby's (null: none)
void setMatchSkinOverride(const std::array<std::string, INPUT_MAX_PLAYERS>* paths);

// Every peer: sets up game_values, loads the map and enters the gameplay
// state with the match ready to run frame 0
void startConfiguredMatch(const MatchConfig& config);

// The input device type of a player slot, as the simulation sees it: from the
// match config online, from the local input settings otherwise
uint32_t simulationPlayerDevice(short slot);
// Online: the device types the config set (cleared when the match ends)
void setSimulationPlayerDevices(const std::array<bool, INPUT_MAX_PLAYERS>* keyboard);

// The simulation settings of game_values (see visitSimulationSettings), as
// in MatchConfig::settings; the series send them too (NetSeries.h)
std::vector<uint8_t> captureSimulationSettings();
bool applySimulationSettings(const std::vector<uint8_t>& bytes);

// Hash of the loaded simulation data that isn't part of a map: the tile types
// of every tileset (they decide collisions), for the connection handshake
uint64_t simulationDataHash();

// Hash of a file's contents; 0 if it can't be read
uint64_t fileHash(const std::string& path);

} // namespace rollback

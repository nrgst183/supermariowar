#include "MatchConfig.h"
#include "LoadLog.h"
#include "Replay.h"

#include "ConfigNames.h"
#include "GSGameplay.h"
#include "map.h"
#include "MapList.h"
#include "NetGame.h"
#include "path.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "TilesetManager.h"
#include "FileList.h"
#include "SeriesState.h"
#include "WorldTourStop.h"
#include "gamemodes/BonusHouse.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;

namespace {

// THE SIMULATION SETTINGS. Every setting of game_values that the simulation
// reads, visited in a fixed order by the writer and the reader below. Local
// settings stay out: volumes, video, input devices and key bindings, music
// packs, skins, menu state. Per-slot and team settings are in MatchConfig.
template <typename Visit>
void visitSimulationSettings(Visit&& v, CGameValues& g)
{
    // gameplay
    v(g.respawn);
    v(g.shieldstyle);
    v(g.shieldtime);
    v(g.outofboundstime);
    v(g.suicidetime);
    v(g.warplockstyle);
    v(g.warplocktime);
    v(g.cpudifficulty);
    v(g.pointspeed);
    v(g.secretsenabled);
    v(g.teamcollision);
    v(g.teamcolors);
    // styles that change timings or spawn objects
    v(g.spawnstyle);
    v(g.awardstyle);
    v(g.scoreboardstyle);    // the score positions are simulation state
    v(g.screencrunch);
    v(g.showwinningcrown);
    v(g.startgamecountdown);
    v(g.startmodedisplay);
    v(g.deadteamnotice);
    // weapons
    v(g.fireballttl);
    v(g.featherjumps);
    v(g.boomerangstyle);
    v(g.boomeranglife);
    v(g.shellttl);
    v(g.wandfreezetime);
    v(g.hammerttl);
    v(g.hammerdelay);
    v(g.hammerpower);
    v(g.blueblockttl);
    v(g.redblockttl);
    v(g.grayblockttl);
    // weapon limits
    v(g.fireballlimit);
    v(g.hammerlimit);
    v(g.boomeranglimit);
    v(g.featherlimit);
    v(g.leaflimit);
    v(g.pwingslimit);
    v(g.tanookilimit);
    v(g.bombslimit);
    v(g.wandlimit);
    // powerups
    v(g.storedpowerupdelay);
    v(g.itemrespawntime);
    v(g.hiddenblockrespawn);
    v(g.swapstyle);
    v(g.bonuswheel);
    v(g.keeppowerup);
    v(g.overridepowerupsettings);
    v(g.powerupweights);      // the effective drop rates (from the selected preset)
    // runtime values that carry over from earlier matches
    v(g.cputurn);
    v(g.worldpointsbonus);
    v(g.windaffectsplayers);
    v(g.spinscreen);
    v(g.reversewalk);
    v(g.spotlights);
    // all game mode settings (the effective ones, see SetGameModeSettingsFromMenu)
    v(g.gamemodesettings);
}

template <typename T> struct is_c_array : std::false_type {};
template <typename T, size_t N> struct is_c_array<T[N]> : std::true_type {};

struct SettingsWriter {
    ByteWriter& w;

    template <typename T>
    void operator()(const T& value)
    {
        if constexpr (std::is_same_v<T, bool>) {
            w.u8(value ? 1 : 0);
        } else if constexpr (std::is_enum_v<T>) {
            w.i32(static_cast<int32_t>(value));
        } else if constexpr (std::is_same_v<T, float>) {
            w.f32(value);
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
            w.u8(static_cast<uint8_t>(value));
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 2) {
            w.u16(static_cast<uint16_t>(value));
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 4) {
            w.u32(static_cast<uint32_t>(value));
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
            w.u64(static_cast<uint64_t>(value));
        } else if constexpr (is_c_array<T>::value || config::is_std_array<T>::value) {
            for (const auto& element : value)
                (*this)(element);
        } else if constexpr (config::has_named_struct<T>::value) {
            std::apply([this, &value](const auto&... f) { ((*this)(value.*(f.member)), ...); }, config::NamedStruct<T>::table);
        } else {
            static_assert(!sizeof(T), "add a serialization for this setting type");
        }
    }
};

struct SettingsReader {
    ByteReader& r;

    template <typename T>
    void operator()(T& value)
    {
        if constexpr (std::is_same_v<T, bool>) {
            value = r.u8() != 0;
        } else if constexpr (std::is_enum_v<T>) {
            value = static_cast<T>(r.i32());
        } else if constexpr (std::is_same_v<T, float>) {
            value = r.f32();
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1) {
            value = static_cast<T>(r.u8());
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 2) {
            value = static_cast<T>(r.u16());
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 4) {
            value = static_cast<T>(r.u32());
        } else if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
            value = static_cast<T>(r.u64());
        } else if constexpr (is_c_array<T>::value || config::is_std_array<T>::value) {
            for (auto& element : value)
                (*this)(element);
        } else if constexpr (config::has_named_struct<T>::value) {
            std::apply([this, &value](const auto&... f) { ((*this)(value.*(f.member)), ...); }, config::NamedStruct<T>::table);
        } else {
            static_assert(!sizeof(T), "add a serialization for this setting type");
        }
    }
};

std::vector<uint8_t> captureSettings()
{
    std::vector<uint8_t> bytes(4096);
    ByteWriter w(bytes.data(), bytes.size());
    SettingsWriter writer {w};
    visitSimulationSettings(writer, game_values);
    bytes.resize(w.ok() ? w.size() : 0);
    return bytes;
}

// The size was checked by checkMatchConfig(): peers of the same build write
// the same fields
bool applySettings(const std::vector<uint8_t>& bytes)
{
    ByteReader r(bytes.data(), bytes.size());
    SettingsReader reader {r};
    visitSimulationSettings(reader, game_values);
    return r.ok() && r.remaining() == 0;
}

// Where the config's map is here: where its reference points, if that file
// has the right contents, else a downloaded copy with those contents
std::string mapPathFor(const MatchConfig& config)
{
    std::string path;
    if (!config.mapRef.empty() && config.mapRef[0] == '@') {
        if (maplist->findexact(config.mapRef.c_str() + 1, false))
            path = maplist->currentFilename();
    } else {
        path = convertPath(config.mapRef);
    }
    if (config.mapHash == 0 || (!path.empty() && fileHash(path) == config.mapHash))
        return path;
    for (const std::string& dir : mapDownloadDirs()) {
        std::string downloaded = findMapByHash(dir, config.mapHash);
        if (!downloaded.empty())
            return downloaded;
    }
    return path;
}

std::array<bool, INPUT_MAX_PLAYERS> g_keyboardDevices {};
bool g_devicesFromConfig = false;

} // namespace

std::vector<uint8_t> captureSimulationSettings()
{
    return captureSettings();
}

bool applySimulationSettings(const std::vector<uint8_t>& bytes)
{
    return bytes.size() == captureSettings().size() && applySettings(bytes);
}

uint32_t simulationPlayerDevice(short slot)
{
    if (g_devicesFromConfig)
        return g_keyboardDevices[static_cast<size_t>(slot)] ? DEVICE_KEYBOARD_ID : DEVICE_KEYBOARD_ID + 1;
    return game_values.playerInput.inputControls[slot]->iDevice;
}

void setSimulationPlayerDevices(const std::array<bool, INPUT_MAX_PLAYERS>* keyboard)
{
    g_devicesFromConfig = keyboard != nullptr;
    if (keyboard)
        g_keyboardDevices = *keyboard;
}

std::vector<std::string> mapDownloadDirs()
{
    return {convertPath("maps/net/"), GetHomeDirectory() + "maps/net/"};
}

std::vector<std::string> skinSearchDirs()
{
    std::vector<std::string> dirs {convertPath("gfx/skins/")};
    for (const std::string& dir : skinDownloadDirs())
        dirs.push_back(dir);
    return dirs;
}

std::vector<std::string> skinDownloadDirs()
{
    return {convertPath("skins/net/"), GetHomeDirectory() + "skins/net/"};
}

namespace {
std::array<std::string, INPUT_MAX_PLAYERS> g_skinOverride;

// The CPUs' skins (cpuSkin), and the random numbers they're picked with: not the game's (those are
// the simulation's, the same on every machine)
std::array<short, INPUT_MAX_PLAYERS> g_cpuSkins {-1, -1, -1, -1};
std::array<bool, INPUT_MAX_PLAYERS> g_wasCpu {};
uint32_t g_skinRandom = 0;

uint32_t skinRandom()
{
    if (g_skinRandom == 0)
        g_skinRandom = static_cast<uint32_t>(SDL_GetTicksNS()) | 1u;
    g_skinRandom ^= g_skinRandom << 13;
    g_skinRandom ^= g_skinRandom >> 17;
    g_skinRandom ^= g_skinRandom << 5;
    return g_skinRandom;
}
}

short cpuSkin(int slot)
{
    if (slot < 0 || slot >= INPUT_MAX_PLAYERS || !skinlist || skinlist->count() == 0)
        return -1;
    const size_t s = static_cast<size_t>(slot);
    const NetLobby* lobby = NetGame::instance().lobby();
    const bool cpu = lobby ? lobby->slot(slot).kind == SlotKind::Cpu : game_values.playercontrol[slot] == 2;
    if (!cpu) {
        g_wasCpu[s] = false;
        return -1;
    }
    const short count = static_cast<short>(skinlist->count());
    if (!g_wasCpu[s] || g_cpuSkins[s] < 0 || g_cpuSkins[s] >= count) {
        g_wasCpu[s] = true;
        //(not another CPU's, when there are skins enough)
        short pick = 0;
        for (int attempt = 0; attempt < 8; attempt++) {
            pick = static_cast<short>(skinRandom() % static_cast<uint32_t>(count));
            bool taken = false;
            for (size_t o = 0; o < g_cpuSkins.size(); o++)
                taken |= o != s && g_wasCpu[o] && g_cpuSkins[o] == pick;
            if (!taken)
                break;
        }
        g_cpuSkins[s] = pick;
    }
    return g_cpuSkins[s];
}

void setMatchSkinOverride(const std::array<std::string, INPUT_MAX_PLAYERS>* paths)
{
    g_skinOverride = paths ? *paths : std::array<std::string, INPUT_MAX_PLAYERS> {};
}

void loadMatchSkin(int slot, short color)
{
    const NetLobby* lobby = NetGame::instance().lobby();
    std::string path = slot >= 0 && slot < INPUT_MAX_PLAYERS ? g_skinOverride[static_cast<size_t>(slot)] : std::string();
    if (path.empty() && lobby)
        path = lobby->slotSkinPath(slot);
    //(a computer player: its random skin)
    if (path.empty()) {
        const short skin = cpuSkin(slot);
        if (skin >= 0)
            path = skinlist->at(static_cast<size_t>(skin)).path.string();
    }
    if (!path.empty()) {
        try {
            rm->spr_player[slot] = rm->LoadFullSkin(std::filesystem::path(path), color);
            return;
        } catch (const std::string& what) {
            printf("netplay: can't use the skin %s: %s\n", path.c_str(), what.c_str());
        }
    }
    //Not here (yet), or humans only: the default skin in the color of the slot
    rm->spr_player[slot] = rm->LoadFullSkin(static_cast<short>(0), color);
}

std::string matchMapPath(const MatchConfig& config)
{
    return mapPathFor(config);
}

uint64_t fileHash(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return 0;
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return net::fnv1a64(bytes.data(), bytes.size());
}

uint64_t simulationDataHash()
{
    uint64_t hash = net::fnv1a64("tiletypes", 9);
    if (!g_tilesetmanager)
        return hash;
    //(every match starts with it: the files are read again only when the tilesets or their tile types
    //changed, a DVD seek each on the Xbox)
    static size_t s_count = 0;
    static unsigned s_saved = 0;
    static uint64_t s_hash = 0;
    if (s_hash != 0 && s_count == g_tilesetmanager->count() && s_saved == CTileset::savedCount())
        return s_hash;
    for (size_t i = 0; i < g_tilesetmanager->count(); i++) {
        // Only the names and tile type files are hashed: no need to load the images
        const CTileset* tileset = g_tilesetmanager->tilesetInfo(i);
        hash = net::fnv1a64(tileset->name().data(), tileset->name().size(), hash);
        const uint64_t contents = fileHash((tileset->directory() / "tileset.tls").string());
        hash = net::fnv1a64(&contents, sizeof(contents), hash);
    }
    s_count = g_tilesetmanager->count();
    s_saved = CTileset::savedCount();
    s_hash = hash;
    return hash;
}

std::vector<uint8_t> MatchConfig::serialize() const
{
    std::vector<uint8_t> bytes(512 + settings.size() + series.size());
    ByteWriter w(bytes.data(), bytes.size());
    w.u16(FORMAT_VERSION);
    w.u32(seed);
    w.u8(static_cast<uint8_t>(matchType));
    w.i16(gameMode);
    w.i16(goal);
    w.u8(bossType);
    w.str(mapRef);
    w.u64(mapHash);
    w.u8(inputDelay);
    w.i32(frameLimit);
    w.i16(gameOverTicks);
    for (const MatchSlot& slot : slots) {
        w.u8(slot.control);
        w.i8(slot.peer);
        w.i16(slot.color);
        w.u8(slot.keyboard ? 1 : 0);
        w.i16(slot.storedPowerup);
    }
    w.i16(teamCount);
    for (const auto& team : teamIds) {
        for (int16_t id : team)
            w.i16(id);
    }
    for (int16_t count : teamCounts)
        w.i16(count);
    w.u16(static_cast<uint16_t>(settings.size()));
    w.bytes(settings.data(), settings.size());
    w.u16(static_cast<uint16_t>(series.size()));
    w.bytes(series.data(), series.size());
    bytes.resize(w.ok() ? w.size() : 0);
    return bytes;
}

bool MatchConfig::deserialize(const uint8_t* data, size_t size)
{
    ByteReader r(data, size);
    if (r.u16() != FORMAT_VERSION)
        return false;
    seed = r.u32();
    matchType = static_cast<MatchType>(r.u8());
    gameMode = r.i16();
    goal = r.i16();
    bossType = r.u8();
    mapRef = r.str();
    mapHash = r.u64();
    inputDelay = r.u8();
    frameLimit = r.i32();
    gameOverTicks = r.i16();
    for (MatchSlot& slot : slots) {
        slot.control = r.u8();
        slot.peer = r.i8();
        slot.color = r.i16();
        slot.keyboard = r.u8() != 0;
        slot.storedPowerup = r.i16();
    }
    teamCount = r.i16();
    for (auto& team : teamIds) {
        for (int16_t& id : team)
            id = r.i16();
    }
    for (int16_t& count : teamCounts)
        count = r.i16();
    const uint16_t settingsSize = r.u16();
    const uint8_t* settingsBytes = r.bytes(settingsSize);
    const uint16_t seriesSize = r.u16();
    const uint8_t* seriesBytes = r.bytes(seriesSize);
    if (!r.ok() || r.remaining() != 0)
        return false;
    settings.assign(settingsBytes, settingsBytes + settingsSize);
    series.assign(seriesBytes, seriesBytes + seriesSize);
    return teamCount >= 1 && teamCount <= 4;
}

namespace {
bool captureConfig(MatchConfig& config, const std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot, uint32_t seed, int inputDelay, std::string& error,
    const std::string* loadedMap);
}

bool captureMatchConfig(MatchConfig& config, const std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot, uint32_t seed, int inputDelay, std::string& error)
{
    return captureConfig(config, peerOfSlot, seed, inputDelay, error, nullptr);
}

bool captureLocalMatchConfig(MatchConfig& config, const std::string& loadedMap, uint32_t seed, std::string& error)
{
    //The local players: "peer 0" drives every human slot
    const std::array<int8_t, INPUT_MAX_PLAYERS> peerOfSlot {{0, 0, 0, 0}};
    return captureConfig(config, peerOfSlot, seed, 0, error, &loadedMap);
}

namespace {
bool captureConfig(MatchConfig& config, const std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot, uint32_t seed, int inputDelay, std::string& error,
    const std::string* loadedMap)
{
    const bool series = isSeriesMatchType(game_values.matchtype);
    if (!loadedMap && game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::MiniGame && !series) {
        error = "this match type can't be played online";
        return false;
    }

    config = MatchConfig();
    config.seed = seed;
    config.matchType = game_values.matchtype;
    config.gameMode = static_cast<int16_t>(game_values.gamemode->gamemode);
    config.goal = game_values.gamemode->goal;
    config.bossType = static_cast<uint8_t>(game_values.gamemodesettings.boss.bosstype);
    config.inputDelay = static_cast<uint8_t>(std::clamp(inputDelay, 0, 16));

    //The minigames have fixed maps (see MenuState::update), except the
    //world's, which may name one; the bonus houses have theirs
    std::string path;
    const short mode = game_values.gamemode->gamemode;
    const bool minigame = mode == game_mode_pipe_minigame || mode == game_mode_boss_minigame || mode == game_mode_boxes_minigame;
    const bool worldMinigameMap = minigame && game_values.matchtype == MatchType::World && game_values.tourstopcurrent < game_values.tourstops.size() &&
        maplist->findexact(game_values.tourstops[game_values.tourstopcurrent]->pszMapFile.c_str(), true);
    if (loadedMap) {
        //The map that the menu loaded (a quick game's random one, a world's stage)
        const std::string root = convertPath("");
        if (!root.empty() && loadedMap->rfind(root, 0) == 0)
            config.mapRef = loadedMap->substr(root.size());
        else
            config.mapRef = "@" + stripPathAndExtension(*loadedMap);
    } else if (mode == game_mode_bonus) {
        config.mapRef = "maps/special/two52_special_bonushouse.map";
    } else if (worldMinigameMap) {
        const std::string& file = maplist->currentFilename();
        const std::string root = convertPath("");
        config.mapRef = !root.empty() && file.rfind(root, 0) == 0 ? file.substr(root.size()) : "@" + maplist->currentShortmapname();
    } else switch (game_values.gamemode->gamemode) {
        case game_mode_pipe_minigame:
            config.mapRef = "maps/special/two52_special_pipe_minigame.map";
            break;
        case game_mode_boss_minigame:
            switch (game_values.gamemodesettings.boss.bosstype) {
                case Boss::Hammer: config.mapRef = "maps/special/two52_special_hammerboss_minigame.map"; break;
                case Boss::Bomb: config.mapRef = "maps/special/two52_special_bombboss_minigame.map"; break;
                case Boss::Fire: config.mapRef = "maps/special/two52_special_fireboss_minigame.map"; break;
            }
            break;
        case game_mode_boxes_minigame:
            config.mapRef = "maps/special/two52_special_boxes_minigame.map";
            break;
        default: {
            const std::string& file = maplist->currentFilename();
            const std::string root = convertPath("");
            if (!root.empty() && file.rfind(root, 0) == 0)
                config.mapRef = file.substr(root.size());
            else
                config.mapRef = "@" + maplist->currentShortmapname();
            break;
        }
    }
    config.mapHash = fileHash(loadedMap ? *loadedMap : mapPathFor(config));
    if (config.mapHash == 0) {
        error = "could not read the map " + config.mapRef;
        return false;
    }

    int humans = 0;
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        MatchSlot& slot = config.slots[static_cast<size_t>(k)];
        slot.control = static_cast<uint8_t>(game_values.playercontrol[k]);
        slot.peer = slot.control == 1 ? peerOfSlot[static_cast<size_t>(k)] : -1;
        if (slot.control == 1 && slot.peer < 0) {
            error = "player " + std::to_string(k + 1) + " is human but no peer drives it";
            return false;
        }
        humans += slot.control == 1;
        slot.color = game_values.colorids[k];
        slot.keyboard = simulationPlayerDevice(static_cast<short>(k)) == DEVICE_KEYBOARD_ID;
        slot.storedPowerup = game_values.storedpowerups[k];
    }
    (void)humans;

    config.teamCount = g_sim.score_cnt;
    for (int t = 0; t < 4; t++) {
        config.teamCounts[t] = game_values.teamcounts[t];
        for (int m = 0; m < 3; m++)
            config.teamIds[t][m] = game_values.teamids[t][m];
    }

    config.settings = captureSettings();
    if (config.settings.empty()) {
        error = "the settings don't fit in a message";
        return false;
    }
    if (series) {
        prepareSeriesStateForMatch(config.gameMode);
        config.series = captureSeriesState();
        if (config.series.empty()) {
            error = "the series state doesn't fit in a message";
            return false;
        }
    }
    return true;
}
} // namespace

bool isSeriesMatchType(MatchType type)
{
    return type == MatchType::Tournament || type == MatchType::Tour || type == MatchType::World;
}

void prepareSeriesStateForMatch(int gameMode)
{
    //Only a bonus house is for one player (the menus leave it set after one)
    if (gameMode != game_mode_bonus)
        game_values.singleplayermode = -1;
}

bool checkMatchConfig(const MatchConfig& config, std::string& error, bool* needMap)
{
    if (needMap)
        *needMap = false;
    const bool series = isSeriesMatchType(config.matchType);
    if (config.matchType != MatchType::SingleGame && config.matchType != MatchType::MiniGame && !series) {
        error = "unsupported match type";
        return false;
    }
    if (series != !config.series.empty()) {
        error = "the series state is missing";
        return false;
    }
    if (series && (game_values.matchtype != config.matchType || !isSeriesMatchType(game_values.matchtype))) {
        error = "this peer isn't playing this tournament, tour or world";
        return false;
    }
    const int mode = config.gameMode;
    if (!(mode >= 0 && mode < GAMEMODE_LAST) && mode != game_mode_pipe_minigame && mode != game_mode_boss_minigame && mode != game_mode_boxes_minigame &&
            !(mode == game_mode_bonus && config.matchType == MatchType::World)) {
        error = "unknown game mode " + std::to_string(mode);
        return false;
    }

    if (config.settings.size() != captureSettings().size()) {
        error = "the settings don't match this game version";
        return false;
    }

    const std::string path = mapPathFor(config);
    const uint64_t hash = path.empty() ? 0 : fileHash(path);
    if (hash != config.mapHash) {
        error = hash == 0 ? "the map " + config.mapRef + " is missing here" : "the map " + config.mapRef + " is different here (another version of the file)";
        if (needMap)
            *needMap = true;
        return false;
    }
    return true;
}

void startConfiguredMatch(const MatchConfig& config)
{
    loadlog::mark("match (lobby's): starting");
    //The game mode
    game_values.matchtype = config.matchType;
    switch (config.gameMode) {
        case game_mode_pipe_minigame:
            game_values.gamemode = pipegamemode;
            break;
        case game_mode_boss_minigame:
            bossgamemode->SetBossType(static_cast<Boss>(config.bossType));
            game_values.gamemode = bossgamemode;
            break;
        case game_mode_boxes_minigame:
            game_values.gamemode = boxesgamemode;
            break;
        case game_mode_bonus:
            game_values.gamemode = bonushousemode;
            break;
        default:
            game_values.gamemode = gamemodes[config.gameMode];
            currentgamemode = config.gameMode;
            break;
    }
    game_values.gamemode->goal = config.goal;

    //Players and teams
    std::array<bool, INPUT_MAX_PLAYERS> keyboard {};
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const MatchSlot& slot = config.slots[static_cast<size_t>(k)];
        game_values.playercontrol[k] = slot.control;
        game_values.colorids[k] = slot.color;
        game_values.storedpowerups[k] = slot.storedPowerup;
        keyboard[static_cast<size_t>(k)] = slot.keyboard;
    }
    setSimulationPlayerDevices(&keyboard);
    g_sim.score_cnt = config.teamCount;
    for (int t = 0; t < 4; t++) {
        game_values.teamcounts[t] = config.teamCounts[t];
        for (int m = 0; m < 3; m++)
            game_values.teamids[t][m] = config.teamIds[t][m];
    }

    //Settings
    applySettings(config.settings);
    //(a tour stop's own settings don't become the menu's)
    if (config.matchType != MatchType::Tour && config.matchType != MatchType::World)
        game_values.gamemodemenusettings = game_values.gamemodesettings;
    game_values.singleplayermode = -1;
    game_values.autokill = false;
    //The series state: the host's (a peer's own was compared with it, see NetGame::beginMatch)
    if (!config.series.empty())
        applySeriesState(config.series);

    //From here on every peer consumes the same random numbers
    RandomNumberGenerator::gameplay().seed(config.seed);

    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        if (game_values.playercontrol[k] > 0)
            loadMatchSkin(k, game_values.colorids[k]);
    }
    loadlog::mark("match (lobby's): settings, players' skins");

    //The same steps as the menu's match start (MenuState::update)
    const std::string path = mapPathFor(config);
    g_map->loadMap(path, read_type_full);
    loadlog::mark("match (lobby's): map");
    LoadCurrentMapBackground();
    loadlog::mark("match (lobby's): background");

    if (game_values.music) {
        musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), stripPathAndExtension(path), g_map->szBackgroundFile);
        rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
        rm->backgroundmusic[0].play(game_values.playnextmusic, false);
        replay::noteMusic();
    }
    loadlog::mark("match (lobby's): music");

    game_values.appstate = AppState::Game;

    g_map->predrawbackground(rm->spr_background, rm->spr_backmap[0]);
    g_map->predrawforeground(rm->spr_frontmap[0]);
    //(the second copy, the animated tiles' other buffer: the same pixels, copied, not drawn again;
    //set up as the drawing sets it: the back layer opaque, the front one blended)
    if (gfx_copypixels(rm->spr_backmap[0].getSurface(), rm->spr_backmap[1].getSurface()))
        SDL_SetSurfaceBlendMode(rm->spr_backmap[1].getSurface(), SDL_BLENDMODE_NONE);
    else
        g_map->predrawbackground(rm->spr_background, rm->spr_backmap[1]);
    if (gfx_copypixels(rm->spr_frontmap[0].getSurface(), rm->spr_frontmap[1].getSurface())) {
        SDL_SetSurfaceColorKey(rm->spr_frontmap[1].getSurface(), false, 0);
        SDL_SetSurfaceBlendMode(rm->spr_frontmap[1].getSurface(), SDL_BLENDMODE_BLEND);
    } else {
        g_map->predrawforeground(rm->spr_frontmap[1]);
    }
    g_map->fitDrawAreas(rm->spr_frontmap[0]);    //(only where it shows)
    loadlog::mark("match (lobby's): the map's layers drawn");
    g_map->SetupAnimatedTiles();
    LoadMapObjects(false);
    loadlog::mark("match (lobby's): animated tiles, map objects");

    GameStateManager::instance().changeStateTo(&GameplayState::instance());
    loadlog::mark("match (lobby's): gameplay state entered");
}

} // namespace rollback

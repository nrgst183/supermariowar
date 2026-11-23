#include "Replay.h"
#include "DebugProfile.h"

#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "MapList.h"
#include "MatchConfig.h"
#include "NetGame.h"
#include "NetLobby.h"
#include "NetProtocol.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SeriesState.h"
#include "SimulationState.h"
#include "SkinTransfer.h"
#include "StateChecksum.h"
#include "Snapshot.h"
#include "eyecandy.h"
#include "input.h"
#include "GSGameplay.h"
#include "Resimulation.h"
#include "sfx.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifdef SMW_XBOX
#include <SDL3/SDL.h>
#endif

extern CGameValues game_values;
extern MapList* maplist;
extern SkinList* skinlist;
extern CResourceManager* rm;
extern MusicList* musiclist;

namespace replay {
namespace {

constexpr char MAGIC[10] = {'S', 'M', 'W', 'R', 'E', 'P', 'L', 'A', 'Y', 0};
constexpr uint32_t CHECKSUM_EVERY = 60;
constexpr size_t MAX_REPLAYS = 200;

// ---- byte streams (little-endian, growable)

struct Writer {
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v)); u16(static_cast<uint16_t>(v >> 16)); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
    // 7 bits per byte, the high bit says another byte follows
    void varint(uint32_t v)
    {
        while (v >= 0x80) {
            u8(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        u8(static_cast<uint8_t>(v));
    }
    void bytes(const std::vector<uint8_t>& b) { u32(static_cast<uint32_t>(b.size())); out.insert(out.end(), b.begin(), b.end()); }
    void str(const std::string& s)
    {
        const size_t n = std::min<size_t>(s.size(), 0xffff);
        u16(static_cast<uint16_t>(n));
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
};

struct Reader {
    const std::vector<uint8_t>& in;
    size_t pos = 0;
    bool ok = true;

    bool need(size_t n)
    {
        if (!ok || in.size() - pos < n)
            ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? in[pos++] : 0; }
    uint16_t u16() { const uint16_t lo = u8(); return static_cast<uint16_t>(lo | (u8() << 8)); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (static_cast<uint32_t>(u16()) << 16); }
    uint64_t u64() { const uint64_t lo = u32(); return lo | (static_cast<uint64_t>(u32()) << 32); }
    uint32_t varint()
    {
        uint32_t v = 0;
        for (int shift = 0; shift < 35 && ok; shift += 7) {
            const uint8_t b = u8();
            v |= static_cast<uint32_t>(b & 0x7f) << shift;
            if (!(b & 0x80))
                return v;
        }
        ok = false;
        return 0;
    }
    std::vector<uint8_t> bytes(size_t max)
    {
        const uint32_t n = u32();
        if (n > max || !need(n))
            return ok = false, std::vector<uint8_t> {};
        std::vector<uint8_t> b(in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        return b;
    }
    std::string str()
    {
        const uint16_t n = u16();
        if (!need(n))
            return {};
        std::string s(in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        return s;
    }
};

bool readFile(const std::string& path, std::vector<uint8_t>& bytes, size_t maxBytes)
{
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes)
        return false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    bytes.resize(static_cast<size_t>(size));
    return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)));
}

// ---- recording

struct Recording {
    bool active = false;
    bool online = false;
    Replay replay;
    //Online: the pings seen in each window, to average
    std::vector<std::array<uint32_t, INPUT_MAX_PLAYERS>> pingSum;
    std::vector<std::array<uint16_t, INPUT_MAX_PLAYERS>> pingCount;
};

// The buttons as 16 bits: held (0-7), just pressed (8-15)
uint16_t packKeys(const COutputControl& keys)
{
    uint16_t bits = 0;
    for (int k = 0; k < NUM_KEYS && k < 8; k++) {
        if (keys.keys[k].fDown)
            bits |= static_cast<uint16_t>(1u << k);
        if (keys.keys[k].fPressed)
            bits |= static_cast<uint16_t>(1u << (k + 8));
    }
    return bits;
}

void unpackKeys(uint16_t bits, COutputControl& keys)
{
    for (int k = 0; k < NUM_KEYS && k < 8; k++) {
        keys.keys[k].fDown = (bits >> k) & 1u;
        keys.keys[k].fPressed = (bits >> (k + 8)) & 1u;
    }
}
Recording g_rec;

std::string skinPathOf(int slot)
{
    const short cpu = game_values.playercontrol[slot] == 2 ? rollback::cpuSkin(slot) : -1;
    const short id = cpu >= 0 ? cpu : game_values.skinids[slot];
    if (skinlist && id >= 0 && static_cast<size_t>(id) < skinlist->count())
        return skinlist->at(static_cast<size_t>(id)).path.string();
    return {};
}

// A song as the replay keeps it: relative to the data folder if it's in it (the
// same song on another computer), else its full path
std::string songRef(const std::filesystem::path& song)
{
    const std::string path = song.generic_string();
    const std::string root = std::filesystem::path(convertPath("")).generic_string();
    if (!root.empty() && path.size() > root.size() && path.compare(0, root.size(), root) == 0)
        return path.substr(root.size());
    return path;
}

std::filesystem::path songPath(const std::string& ref)
{
    const std::filesystem::path path(ref);
    return path.is_absolute() ? path : std::filesystem::path(convertPath(ref));
}

void startRecording(Replay&& replay, bool online)
{
    g_rec = Recording();
    g_rec.replay = std::move(replay);
    g_rec.replay.dataHash = rollback::simulationDataHash();
    g_rec.replay.startTime = static_cast<int64_t>(std::time(nullptr));
    g_rec.online = online;
    g_rec.active = !g_rec.replay.config.empty();

    //The skins go in the replay
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const std::string& skin = g_rec.replay.skins[static_cast<size_t>(k)];
        if (skin.empty() || !readFile(skin, g_rec.replay.skinFiles[static_cast<size_t>(k)], rollback::SKIN_MAX_BYTES))
            g_rec.replay.skinFiles[static_cast<size_t>(k)].clear();
    }
    //And the song the match starts with (more come with noteMusic)
    if (game_values.music && musiclist && musiclist->count() > 0)
        g_rec.replay.music.emplace_back(0, songRef(musiclist->currentMusic()));

    //The map goes in the replay
    rollback::MatchConfig config;
    if (g_rec.active && config.deserialize(g_rec.replay.config.data(), g_rec.replay.config.size())) {
        const std::string path = rollback::matchMapPath(config);
        if (!path.empty() && readFile(path, g_rec.replay.map, rollback::MAP_MAX_BYTES))
            g_rec.replay.mapName = stripPathAndExtension(path);
        else
            g_rec.replay.map.clear();
    }
}

#ifdef SMW_XBOX
// On the Xbox, each saved film is a game save (the dashboard's memory screen
// shows them, and can copy or delete them), named from its file name:
// "2026-11-16_030912.smwr" is "Film 2026-11-16 03:09:12" ("_2": " (2)")
constexpr char FILM_PREFIX[] = "Film ";
constexpr char FILM_FILE[] = "film.smwr";

std::string saveGameName(const std::string& file)
{
    std::string stem = file.ends_with(".smwr") ? file.substr(0, file.size() - 5) : file;
    std::string name = FILM_PREFIX;
    if (stem.size() >= 17 && stem[10] == '_') {
        name += stem.substr(0, 10) + " " + stem.substr(11, 2) + ":" + stem.substr(13, 2) + ":" + stem.substr(15, 2);
        if (stem.size() > 18 && stem[17] == '_')
            name += " (" + stem.substr(18) + ")";
        else
            name += stem.substr(17);
    } else {
        name += stem;
    }
    return name;
}

// Keeps the newest MAX_REPLAYS films
void pruneReplays()
{
    std::vector<std::string> films;
    SDL_XboxEnumerateSaveGames([](void* list, const char* name) {
        if (std::strncmp(name, FILM_PREFIX, sizeof(FILM_PREFIX) - 1) == 0)
            static_cast<std::vector<std::string>*>(list)->push_back(name);
    }, &films);
    if (films.size() <= MAX_REPLAYS)
        return;
    //(the names have the date and time: oldest first)
    std::sort(films.begin(), films.end());
    for (size_t k = 0; k + MAX_REPLAYS < films.size(); k++)
        SDL_XboxDeleteSaveGame(films[k].c_str());
}
#else
// Keeps the newest MAX_REPLAYS replays
void pruneReplays()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(replayDir(), ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".smwr")
            files.push_back(entry.path());
    }
    if (files.size() <= MAX_REPLAYS)
        return;
    //(the names start with the date and time: oldest first)
    std::sort(files.begin(), files.end());
    for (size_t k = 0; k + MAX_REPLAYS < files.size(); k++)
        fs::remove(files[k], ec);
}
#endif

// ---- playback

struct MenuBackup {
    MatchType matchtype {};
    CGameMode* gamemode = nullptr;
    short goal = 0;
    short winningteam = -1;
    bool gameover = false;
    short currentgamemode = 0;
    short playercontrol[4] {};
    short colorids[4] {};
    short storedpowerups[4] {};
    short teamids[4][3] {};
    short teamcounts[4] {};
    short scoreCount = 0;
    std::vector<uint8_t> settings;
    GameModeSettings gamemodemenusettings {};
    short singleplayermode = -1;
    bool autokill = false;
    std::vector<uint8_t> series;
    bool hasMap = false;
    std::multimap<std::string, MapListNode>::iterator map;
};

struct Playback {
    bool active = false;
    Replay replay;
    size_t next = 0;
    PlaybackStatus status;
    MenuBackup backup;
    //The theater
    std::vector<std::pair<int, std::unique_ptr<rollback::Snapshot>>> keyframes;
    std::vector<Marker> markers;
    std::string musicNow;    // the recorded song playing ("": none yet)
    std::array<std::vector<Replay::CpuKeys>, INPUT_MAX_PLAYERS> cpuKeysBySlot;    // the CPUs' buttons, by player
};
Playback g_play;
bool g_returnedFromReplay = false;

// The effects of earlier ticks that aren't simulation state (eyecandy,
// sounds): gone when the replay jumps
void clearCosmetics()
{
    for (CEyecandyContainer& container : eyecandy)
        container.clean();
    sfx_stopallsounds();
}

// The theater's pass through the whole replay (see Replay.h)
void prepareTimeline()
{
    const Uint64 start = SDL_GetTicks();
    auto keyframe = [](int tick) {
        auto snapshot = std::make_unique<rollback::Snapshot>();
        rollback::saveState(*snapshot);
        g_play.keyframes.emplace_back(tick, std::move(snapshot));
    };
    keyframe(0);
    std::array<short, 4> scores {};
    for (int t = 0; t < 4; t++)
        scores[static_cast<size_t>(t)] = g_sim.score[static_cast<size_t>(t)].score;
    bool over = game_values.gamemode && game_values.gamemode->gameover;
    {
        sim::ResimulationScope quiet;
        while (step()) {
            for (int t = 0; t < g_sim.score_cnt && t < 4; t++) {
                const short now = g_sim.score[static_cast<size_t>(t)].score;
                if (now != scores[static_cast<size_t>(t)])
                    g_play.markers.push_back(Marker {g_sim.tick, t, now - scores[static_cast<size_t>(t)]});
                scores[static_cast<size_t>(t)] = now;
            }
            if (!over && game_values.gamemode && game_values.gamemode->gameover) {
                over = true;
                g_play.markers.push_back(Marker {g_sim.tick, std::max<short>(0, game_values.gamemode->winningteam), 0});
            }
            if (g_sim.tick % KEYFRAME_TICKS == 0)
                keyframe(g_sim.tick);
        }
    }
    printf("replay: timeline ready: %zu keyframes, %zu markers, %llu ms%s\n", g_play.keyframes.size(), g_play.markers.size(),
        static_cast<unsigned long long>(SDL_GetTicks() - start), g_play.status.diverged ? ", it plays differently" : "");
    //Back to the start
    rollback::loadState(*g_play.keyframes.front().second);
    g_play.next = 0;
    g_play.status.tick = 0;
    g_play.status.finished = false;
    g_play.status.checksumsMatched = 0;
    clearCosmetics();
}

void saveMenu(MenuBackup& b)
{
    b.matchtype = game_values.matchtype;
    b.gamemode = game_values.gamemode;
    b.goal = game_values.gamemode ? game_values.gamemode->goal : 0;
    b.winningteam = game_values.gamemode ? game_values.gamemode->winningteam : -1;
    b.gameover = game_values.gamemode && game_values.gamemode->gameover;
    b.currentgamemode = currentgamemode;
    for (int k = 0; k < 4; k++) {
        b.playercontrol[k] = game_values.playercontrol[k];
        b.colorids[k] = game_values.colorids[k];
        b.storedpowerups[k] = game_values.storedpowerups[k];
        b.teamcounts[k] = game_values.teamcounts[k];
        for (int m = 0; m < 3; m++)
            b.teamids[k][m] = game_values.teamids[k][m];
    }
    b.scoreCount = g_sim.score_cnt;
    b.settings = rollback::captureSimulationSettings();
    b.gamemodemenusettings = game_values.gamemodemenusettings;
    b.singleplayermode = game_values.singleplayermode;
    b.autokill = game_values.autokill;
    b.series = rollback::captureSeriesState();
    b.hasMap = maplist && maplist->count() > 0;
    if (b.hasMap)
        b.map = maplist->GetCurrent();
}

void restoreMenu(const MenuBackup& b)
{
    game_values.matchtype = b.matchtype;
    game_values.gamemode = b.gamemode;
    if (game_values.gamemode) {
        game_values.gamemode->goal = b.goal;
        game_values.gamemode->winningteam = b.winningteam;
        game_values.gamemode->gameover = b.gameover;
    }
    currentgamemode = b.currentgamemode;
    for (int k = 0; k < 4; k++) {
        game_values.playercontrol[k] = b.playercontrol[k];
        game_values.colorids[k] = b.colorids[k];
        game_values.storedpowerups[k] = b.storedpowerups[k];
        game_values.teamcounts[k] = b.teamcounts[k];
        for (int m = 0; m < 3; m++)
            game_values.teamids[k][m] = b.teamids[k][m];
    }
    g_sim.score_cnt = b.scoreCount;
    rollback::applySimulationSettings(b.settings);
    game_values.gamemodemenusettings = b.gamemodemenusettings;
    game_values.singleplayermode = b.singleplayermode;
    game_values.autokill = b.autokill;
    rollback::applySeriesState(b.series);
    if (b.hasMap)
        maplist->SetCurrent(b.map);

    rollback::setSimulationPlayerDevices(nullptr);
    rollback::setMatchSkinOverride(nullptr);
    //The players' own skins again (a replay loaded its players')
    for (int k = 0; k < 4; k++) {
        if (game_values.playercontrol[k] > 0)
            rm->spr_player[k] = rm->LoadFullSkin(rollback::matchSkinId(k), game_values.colorids[k]);
    }
    RandomNumberGenerator::gameplay().seedFromTime();
}

// A skin of the replay: its file if it is still there, else the skin of the
// same file name here, else "" (the default skin)
std::string findSkin(const std::string& recorded)
{
    namespace fs = std::filesystem;
    if (recorded.empty())
        return {};
    std::error_code ec;
    if (fs::is_regular_file(recorded, ec))
        return recorded;
    const std::string name = fs::path(recorded).filename().string();
    for (size_t k = 0; skinlist && k < skinlist->count(); k++) {
        if (skinlist->at(k).path.filename().string() == name)
            return skinlist->at(k).path.string();
    }
    return {};
}

} // namespace

// ---- the file

std::vector<uint8_t> Replay::serialize() const
{
    Writer w;
    w.out.insert(w.out.end(), std::begin(MAGIC), std::end(MAGIC));
    w.u16(VERSION);
    w.u64(dataHash);
    w.u64(static_cast<uint64_t>(startTime));
    w.bytes(config);
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        w.str(skins[static_cast<size_t>(k)]);
        w.str(names[static_cast<size_t>(k)]);
    }
    w.str(mapName);
    w.bytes(map);
    w.u32(static_cast<uint32_t>(ticks.size()));

    //(version 4) Run-length, each player's bytes (and the CPU mask) on their own: a player's
    //input stays the same for several ticks, even when someone's changes every tick
    auto writeRuns = [&](auto valueOf) {
        std::vector<std::pair<uint32_t, uint8_t>> runs;
        for (const TickRecord& tick : ticks) {
            const uint8_t v = valueOf(tick);
            if (!runs.empty() && runs.back().second == v)
                runs.back().first++;
            else
                runs.emplace_back(1, v);
        }
        w.u32(static_cast<uint32_t>(runs.size()));
        for (const auto& [length, v] : runs) {
            w.varint(length);
            w.u8(v);
        }
    };
    for (size_t slot = 0; slot < INPUT_MAX_PLAYERS; slot++)
        writeRuns([slot](const TickRecord& tick) { return tick.input.players[slot]; });
    writeRuns([](const TickRecord& tick) { return tick.cpuMask; });

    w.u32(static_cast<uint32_t>(checksums.size()));
    for (const auto& [tick, sum] : checksums) {
        w.u32(tick);
        w.u64(sum);
    }
    //(version 2)
    for (const std::vector<uint8_t>& skin : skinFiles)
        w.bytes(skin);
    w.u32(static_cast<uint32_t>(music.size()));
    for (const auto& [tick, song] : music) {
        w.u32(tick);
        w.str(song);
    }
    //(version 3)
    w.u32(static_cast<uint32_t>(pings.size()));
    for (const auto& window : pings) {
        for (uint16_t ping : window)
            w.u16(ping);
    }
    //(version 4)
    w.u32(static_cast<uint32_t>(cpuKeys.size()));
    for (const CpuKeys& change : cpuKeys) {
        w.u32(change.tick);
        w.u8(change.slot);
        w.u16(change.keys);
    }
    return std::move(w.out);
}

bool Replay::deserialize(const std::vector<uint8_t>& bytes)
{
    *this = Replay();
    if (bytes.size() < sizeof(MAGIC) || std::memcmp(bytes.data(), MAGIC, sizeof(MAGIC)) != 0)
        return false;
    Reader r {bytes, sizeof(MAGIC)};
    const uint16_t version = r.u16();
    if (version < 1 || version > VERSION)
        return false;
    this->version = version;
    dataHash = r.u64();
    startTime = static_cast<int64_t>(r.u64());
    config = r.bytes(1 << 16);
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        skins[static_cast<size_t>(k)] = r.str();
        names[static_cast<size_t>(k)] = r.str();
    }
    mapName = r.str();
    map = r.bytes(rollback::MAP_MAX_BYTES);
    const uint32_t tickCount = r.u32();
    if (!r.ok || tickCount > 50'000'000u)
        return false;
    if (version >= 4) {
        //Each player's runs, then the CPU mask's
        ticks.assign(tickCount, TickRecord {});
        for (int column = 0; column <= INPUT_MAX_PLAYERS && r.ok; column++) {
            const uint32_t runCount = r.u32();
            if (runCount > tickCount)
                return false;
            uint32_t at = 0;
            for (uint32_t k = 0; k < runCount && r.ok; k++) {
                const uint32_t length = r.varint();
                const uint8_t v = r.u8();
                if (length > tickCount - at)
                    return false;
                for (uint32_t t = at; t < at + length; t++) {
                    if (column < INPUT_MAX_PLAYERS)
                        ticks[t].input.players[static_cast<size_t>(column)] = v;
                    else
                        ticks[t].cpuMask = v;
                }
                at += length;
            }
            if (at != tickCount)
                return false;
        }
    } else {
        const uint32_t runCount = r.u32();
        if (runCount > tickCount)
            return false;
        ticks.reserve(tickCount);
        for (uint32_t k = 0; k < runCount && r.ok; k++) {
            const uint16_t length = r.u16();
            TickRecord tick;
            for (uint8_t& b : tick.input.players)
                b = r.u8();
            tick.cpuMask = r.u8();
            if (ticks.size() + length > tickCount)
                return false;
            ticks.insert(ticks.end(), length, tick);
        }
    }
    const uint32_t sums = r.u32();
    for (uint32_t k = 0; k < sums && r.ok; k++) {
        const uint32_t tick = r.u32();
        checksums[tick] = r.u64();
    }
    if (version >= 2) {
        for (std::vector<uint8_t>& skin : skinFiles)
            skin = r.bytes(rollback::SKIN_MAX_BYTES);
        const uint32_t songs = r.u32();
        if (songs > 100'000u)
            return false;
        for (uint32_t k = 0; k < songs && r.ok; k++) {
            const uint32_t tick = r.u32();
            music.emplace_back(tick, r.str());
        }
    }
    if (version >= 3) {
        const uint32_t windows = r.u32();
        if (windows > 5'000'000u)
            return false;
        for (uint32_t k = 0; k < windows && r.ok; k++) {
            std::array<uint16_t, INPUT_MAX_PLAYERS> window;
            for (uint16_t& ping : window)
                ping = r.u16();
            pings.push_back(window);
        }
    }
    if (version >= 4) {
        const uint32_t changes = r.u32();
        if (changes > 50'000'000u)
            return false;
        cpuKeys.reserve(changes);
        for (uint32_t k = 0; k < changes && r.ok; k++) {
            CpuKeys change;
            change.tick = r.u32();
            change.slot = r.u8();
            change.keys = r.u16();
            if (change.slot >= INPUT_MAX_PLAYERS)
                return false;
            cpuKeys.push_back(change);
        }
    }
    return r.ok && ticks.size() == tickCount;
}

bool writeReplayFile(const std::string& path, const Replay& replay)
{
    const std::vector<uint8_t> bytes = replay.serialize();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file && file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool readReplayFile(const std::string& path, Replay& replay, std::string& error)
{
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, 64u << 20)) {
        error = "can't read the replay " + path;
        return false;
    }
    if (!replay.deserialize(bytes)) {
        error = "the replay is damaged or from another game version";
        return false;
    }
    return true;
}

std::string replayDir()
{
    return GetHomeDirectory() + "replays/";
}

std::string replayPath(const std::string& file, bool create)
{
#ifdef SMW_XBOX
    if (char* dir = SDL_XboxOpenSaveGame(saveGameName(file).c_str(), create)) {
        std::string path = std::string(dir) + FILM_FILE;
        SDL_free(dir);
        return path;
    }
    //(no such save: an older build's film in the settings folder, or saves
    // can't be made: the settings folder)
#else
    (void)create;
#endif
    return replayDir() + file;
}

bool replayExists(const std::string& file)
{
    std::error_code ec;
    return !file.empty() && std::filesystem::is_regular_file(replayPath(file, false), ec);
}

// ---- recording

void beginLocal(const std::string& mapPath)
{
    cancelRecording();
    const uint32_t seed = static_cast<uint32_t>(RandomNumberGenerator::gameplay().getInteger(0x7fffffff));
    rollback::MatchConfig config;
    std::string error;
    if (!rollback::captureLocalMatchConfig(config, mapPath, seed, error)) {
        printf("replay: this match isn't recorded: %s\n", error.c_str());
        return;
    }
    //From here on, the match consumes the random numbers of this seed (as
    //startConfiguredMatch does when the replay plays)
    RandomNumberGenerator::gameplay().seed(seed);

    Replay replay;
    replay.config = config.serialize();
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        if (game_values.playercontrol[k] == 0)
            continue;
        replay.skins[static_cast<size_t>(k)] = skinPathOf(k);
        replay.names[static_cast<size_t>(k)] = game_values.playercontrol[k] == 1 ? "Player " + std::to_string(k + 1) : "CPU";
    }
    startRecording(std::move(replay), false);
}

void beginOnline(const std::vector<uint8_t>& config, const std::array<std::string, INPUT_MAX_PLAYERS>& names,
    const std::array<std::string, INPUT_MAX_PLAYERS>& skins)
{
    cancelRecording();
    if (playing())
        return;
    Replay replay;
    replay.config = config;
    replay.names = names;
    replay.skins = skins;
    startRecording(std::move(replay), true);
}

void noteMusic()
{
    if (!g_rec.active || !musiclist || musiclist->count() == 0)
        return;
    const std::string song = songRef(musiclist->currentMusic());
    //(the match's own ticks: at its start, the tick counter can still be the last match's)
    const uint32_t tick = static_cast<uint32_t>(g_rec.replay.ticks.size());
    auto& music = g_rec.replay.music;
    if (!music.empty() && music.back().first == tick)
        music.back().second = song;    //(picked again before the tick: the last one)
    else if (music.empty() || music.back().second != song)
        music.emplace_back(tick, song);
}

bool recording()
{
    return g_rec.active;
}

void recordTick(const InputFrame& input)
{
    if (!g_rec.active)
        return;
    const size_t tick = static_cast<size_t>(std::max(0, g_sim.tick));
    std::vector<TickRecord>& ticks = g_rec.replay.ticks;
    //(online, a rollback records the ticks after it again)
    if (ticks.size() > tick)
        ticks.resize(tick);
    while (ticks.size() < tick)
        ticks.push_back(ticks.empty() ? TickRecord() : ticks.back());
    ticks.push_back(TickRecord {input, g_sim.cpuTakeover});
    //(the CPUs' buttons of this tick and later come again)
    std::vector<Replay::CpuKeys>& cpu = g_rec.replay.cpuKeys;
    while (!cpu.empty() && cpu.back().tick >= tick)
        cpu.pop_back();

    //Online: the other players' pings, for the replay's ping bars
    const rollback::NetGame& net = rollback::NetGame::instance();
    const rollback::NetLobby* lobby = net.lobby();
    if (!g_rec.online || !lobby)
        return;
    const size_t window = tick / static_cast<size_t>(Replay::PING_WINDOW);
    if (g_rec.pingSum.size() <= window) {
        g_rec.pingSum.resize(window + 1, {});
        g_rec.pingCount.resize(window + 1, {});
    }
    for (int s = 0; s < INPUT_MAX_PLAYERS; s++) {
        const rollback::LobbySlot& slot = lobby->slot(s);
        if (slot.kind != rollback::SlotKind::Human || slot.peer == net.localPeer() || slot.peer < 0 || !lobby->peer(slot.peer).present)
            continue;
        const int ping = net.peerPingMs(slot.peer);
        if (ping < 0)
            continue;
        g_rec.pingSum[window][static_cast<size_t>(s)] += static_cast<uint32_t>(ping);
        g_rec.pingCount[window][static_cast<size_t>(s)]++;
    }
}

void afterTick()
{
    if (!g_rec.active || g_rec.online || g_sim.tick <= 0 || g_sim.tick % CHECKSUM_EVERY != 0)
        return;
    g_rec.replay.checksums[static_cast<uint32_t>(g_sim.tick)] = rollback::peerChecksum();
}

void recordChecksum(int frame, uint64_t sum)
{
    if (g_rec.active && frame > 0)
        g_rec.replay.checksums[static_cast<uint32_t>(frame)] = sum;
}

std::string finishRecording()
{
    if (!g_rec.active)
        return {};
    g_rec.active = false;
    Replay& replay = g_rec.replay;
    //The ticks that ran (online: up to the confirmed end)
    const size_t ticks = static_cast<size_t>(std::max(0, g_sim.tick));
    if (replay.ticks.size() > ticks)
        replay.ticks.resize(ticks);
    for (auto it = replay.checksums.begin(); it != replay.checksums.end();)
        it = it->first > ticks ? replay.checksums.erase(it) : std::next(it);
    while (!replay.cpuKeys.empty() && replay.cpuKeys.back().tick >= ticks)
        replay.cpuKeys.pop_back();
    //The pings: an average per window
    replay.pings.clear();
    const size_t windows = std::min(g_rec.pingSum.size(), (ticks + Replay::PING_WINDOW - 1) / Replay::PING_WINDOW);
    for (size_t w = 0; w < windows; w++) {
        std::array<uint16_t, INPUT_MAX_PLAYERS> average;
        for (size_t s = 0; s < average.size(); s++) {
            const uint16_t count = g_rec.pingCount[w][s];
            average[s] = count ? static_cast<uint16_t>(std::min<uint32_t>(g_rec.pingSum[w][s] / count, 0xfffe)) : Replay::NO_PING;
        }
        replay.pings.push_back(average);
    }
    if (replay.ticks.empty())
        return {};

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(replayDir(), ec);
    char stamp[32];
    const std::time_t when = static_cast<std::time_t>(replay.startTime);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", &local);
    std::string name = std::string(stamp) + ".smwr";
    for (int n = 2; replayExists(name); n++)
        name = std::string(stamp) + "_" + std::to_string(n) + ".smwr";

    const std::string path = replayPath(name, true);
    if (!writeReplayFile(path, replay)) {
        printf("replay: can't write %s\n", path.c_str());
#ifdef SMW_XBOX
        SDL_XboxDeleteSaveGame(saveGameName(name).c_str());
#endif
        return {};
    }
    printf("replay: saved %s (%zu ticks)\n", name.c_str(), replay.ticks.size());
    pruneReplays();
    g_rec = Recording();
    return name;
}

void cancelRecording()
{
    g_rec = Recording();
}

// ---- playback

bool startPlayback(const std::string& file, std::string& error)
{
    const std::string path = file.find('/') == std::string::npos && file.find('\\') == std::string::npos ? replayPath(file, false) : file;
    Replay replay;
    if (!readReplayFile(path, replay, error))
        return false;
    rollback::MatchConfig config;
    if (!config.deserialize(replay.config.data(), replay.config.size())) {
        error = "the replay's match is from another game version";
        return false;
    }
    if (config.settings.size() != rollback::captureSimulationSettings().size()) {
        error = "the replay's settings are from another game version";
        return false;
    }

    //The map: the one here if it has the same contents, else the replay's copy
    std::string mapPath = rollback::matchMapPath(config);
    if (mapPath.empty() || rollback::fileHash(mapPath) != config.mapHash) {
        if (replay.map.empty()) {
            error = "the map " + config.mapRef + " is missing";
            return false;
        }
        std::string saveError;
        if (!rollback::saveDownloadedMap(GetHomeDirectory() + "maps/net/", replay.mapName.empty() ? "replay" : replay.mapName, replay.map, config.mapHash,
                mapPath, saveError)) {
            error = "can't use the replay's map: " + saveError;
            return false;
        }
    }
    if (replay.dataHash != rollback::simulationDataHash())
        printf("replay: %s was made with other tile types; it may play differently\n", file.c_str());

    g_play = Playback();
    g_play.replay = std::move(replay);
    for (const Replay::CpuKeys& change : g_play.replay.cpuKeys)
        g_play.cpuKeysBySlot[change.slot].push_back(change);
    saveMenu(g_play.backup);
    g_play.active = true;
    g_play.status.total = static_cast<int>(g_play.replay.ticks.size());

    //The skins: the file here if it is the same skin, else the replay's copy
    //(saved with the downloaded skins), else one of the same name
    std::array<std::string, INPUT_MAX_PLAYERS> skins;
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const std::string& recorded = g_play.replay.skins[static_cast<size_t>(k)];
        const std::vector<uint8_t>& stored = g_play.replay.skinFiles[static_cast<size_t>(k)];
        std::string skin = findSkin(recorded);
        if (!stored.empty()) {
            const uint64_t hash = rollback::net::fnv1a64(stored.data(), stored.size());
            std::vector<uint8_t> here;
            if (skin.empty() || !readFile(skin, here, rollback::SKIN_MAX_BYTES) || rollback::net::fnv1a64(here.data(), here.size()) != hash) {
                const std::string name = std::filesystem::path(recorded).stem().string();
                std::string saved;
                std::string saveError;
                for (const std::string& dir : rollback::skinDownloadDirs()) {
                    if (rollback::saveDownloadedSkin(dir, name.empty() ? "replay" : name, stored, hash, saved, saveError))
                        break;
                    saved.clear();
                }
                if (!saved.empty())
                    skin = saved;
                else
                    printf("replay: can't use the skin of player %d: %s\n", k + 1, saveError.c_str());
            }
        }
        skins[static_cast<size_t>(k)] = skin;
    }
    rollback::setMatchSkinOverride(&skins);

    printf("replay: playing %s (%d ticks)\n", file.c_str(), g_play.status.total);
    rollback::startConfiguredMatch(config);
    prepareTimeline();
    syncMusic();
    return true;
}

bool cpuInputs()
{
    return !g_play.active || g_play.replay.version >= 4;
}

void afterCpuThink(int slot, COutputControl& keys)
{
    if (slot < 0 || slot >= INPUT_MAX_PLAYERS)
        return;
    const uint32_t tick = static_cast<uint32_t>(std::max(0, g_sim.tick));
    if (g_rec.active) {
        //A change from this CPU's last recorded buttons (the list is in tick order)
        const uint16_t bits = packKeys(keys);
        std::vector<Replay::CpuKeys>& cpu = g_rec.replay.cpuKeys;
        auto last = std::find_if(cpu.rbegin(), cpu.rend(), [slot](const Replay::CpuKeys& c) { return c.slot == slot; });
        if (last == cpu.rend() || last->keys != bits)
            cpu.push_back(Replay::CpuKeys {tick, static_cast<uint8_t>(slot), bits});
        return;
    }
    if (!g_play.active)
        return;
    const std::vector<Replay::CpuKeys>& slotKeys = g_play.cpuKeysBySlot[static_cast<size_t>(slot)];
    if (slotKeys.empty())
        return;
    //The last change at or before this tick
    auto it = std::upper_bound(slotKeys.begin(), slotKeys.end(), tick, [](uint32_t t, const Replay::CpuKeys& c) { return t < c.tick; });
    if (it == slotKeys.begin())
        return;
    unpackKeys(std::prev(it)->keys, keys);
}

int pingAt(int slot)
{
    if (!g_play.active || slot < 0 || slot >= INPUT_MAX_PLAYERS || g_play.replay.pings.empty())
        return -1;
    const size_t window = std::min(static_cast<size_t>(std::max(0, g_sim.tick)) / Replay::PING_WINDOW, g_play.replay.pings.size() - 1);
    const uint16_t ping = g_play.replay.pings[window][static_cast<size_t>(slot)];
    return ping == Replay::NO_PING ? -1 : ping;
}

void syncMusic()
{
    if (!g_play.active || !game_values.music || g_play.replay.music.empty())
        return;
    //The last change of song at or before this tick (before the first: the first)
    const auto& music = g_play.replay.music;
    auto it = std::upper_bound(music.begin(), music.end(), static_cast<uint32_t>(std::max(0, g_sim.tick)),
        [](uint32_t tick, const auto& change) { return tick < change.first; });
    const std::string& song = it == music.begin() ? music.front().second : std::prev(it)->second;
    if (song == g_play.musicNow)
        return;
    prof::Scope profile(prof::Section::Music);
    g_play.musicNow = song;
    const std::filesystem::path path = songPath(song);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        printf("replay: the song %s isn't here; another one plays\n", song.c_str());
        return;
    }
    rm->backgroundmusic[0].stop();
    rm->backgroundmusic[0] = sfxMusic(path);
    //(after the invincibility or time warning music, if that's playing)
    if (!rm->sfx_invinciblemusic.isPlaying() && !rm->sfx_timewarning.isPlaying())
        rm->backgroundmusic[0].play(game_values.playnextmusic, false);
}

bool playing()
{
    return g_play.active;
}

bool nextTick(InputFrame& input, uint8_t& cpuMask)
{
    if (!g_play.active || g_play.next >= g_play.replay.ticks.size()) {
        g_play.status.finished = true;
        return false;
    }
    const TickRecord& tick = g_play.replay.ticks[g_play.next++];
    input = tick.input;
    cpuMask = tick.cpuMask;
    return true;
}

void checkTick()
{
    if (!g_play.active)
        return;
    g_play.status.tick = g_sim.tick;
    if (g_play.status.diverged)
        return;
    const auto it = g_play.replay.checksums.find(static_cast<uint32_t>(g_sim.tick));
    if (it == g_play.replay.checksums.end())
        return;
    if (rollback::peerChecksum() == it->second) {
        g_play.status.checksumsMatched++;
    } else {
        g_play.status.diverged = true;
        g_play.status.divergedAt = g_sim.tick;
        printf("replay: the match plays differently from tick %d on\n", g_sim.tick);
    }
}

const std::vector<Marker>& markers()
{
    return g_play.markers;
}

bool step()
{
    InputFrame input;
    uint8_t cpuMask = 0;
    if (!nextTick(input, cpuMask))
        return false;
    if (cpuMask & ~g_sim.cpuTakeover)
        GameplayState::instance().takeOverByCpu(cpuMask);
    GameplayState::instance().advanceTick(input);
    checkTick();
    g_play.status.finished = g_play.next >= g_play.replay.ticks.size();
    return true;
}

void seek(int tick)
{
    if (!g_play.active || g_play.keyframes.empty())
        return;
    prof::Scope profile(prof::Section::Seek);
    tick = std::clamp(tick, 0, static_cast<int>(g_play.replay.ticks.size()));
    //Close ahead: on from here; else from the snapshot before it
    if (tick < g_sim.tick || tick - g_sim.tick > KEYFRAME_TICKS) {
        auto it = std::upper_bound(g_play.keyframes.begin(), g_play.keyframes.end(), tick,
            [](int t, const auto& keyframe) { return t < keyframe.first; });
        --it;
        rollback::loadState(*it->second);
        g_play.next = static_cast<size_t>(it->first);
    }
    {
        sim::ResimulationScope quiet;
        while (g_sim.tick < tick && step()) {
        }
    }
    clearCosmetics();
    g_play.status.tick = g_sim.tick;
    g_play.status.finished = g_play.next >= g_play.replay.ticks.size();
}

const std::string& playerName(int slot)
{
    static const std::string none;
    return slot >= 0 && slot < INPUT_MAX_PLAYERS ? g_play.replay.names[static_cast<size_t>(slot)] : none;
}

const std::string& mapName()
{
    return g_play.replay.mapName;
}

int64_t startTime()
{
    return g_play.replay.startTime;
}

void stopPlayback()
{
    if (!g_play.active)
        return;
    printf("replay: done at tick %d of %d, %d checksums matched%s\n", g_play.status.tick, g_play.status.total, g_play.status.checksumsMatched,
        g_play.status.diverged ? ", it played differently" : "");
    restoreMenu(g_play.backup);
    g_play = Playback();
    g_returnedFromReplay = true;
}

bool takeReturnedFromReplay()
{
    const bool returned = g_returnedFromReplay;
    g_returnedFromReplay = false;
    return returned;
}

const PlaybackStatus& playbackStatus()
{
    return g_play.status;
}

} // namespace replay

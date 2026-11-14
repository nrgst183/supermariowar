#include "SeriesState.h"

#include "FileList.h"
#include "GameValues.h"
#include "NetProtocol.h"
#include "path.h"
#include "SimulationState.h"
#include "world.h"
#include "world/WorldVehicle.h"

#include <string>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;

namespace {
constexpr uint16_t SERIES_STATE_VERSION = 1;

template <typename Visit>
void visitSeriesState(Visit&& v, CGameValues& g)
{
    v(g.tournamentgames);
    v(g.tournamentwinner);
    v(g.singleplayermode);
    v(g.worldpointsbonus);
    for (TournamentScores& score : g.tournament_scores) {
        v(score.wins);
        for (short& type : score.type)
            v(type);
        v(score.total);
    }
    for (short& p : g.storedpowerups)
        v(p);
    for (short& p : g.gamepowerups)
        v(p);
    for (short& count : g.worldpowerupcount)
        v(count);
    for (auto& items : g.worldpowerups) {
        for (short& item : items)
            v(item);
    }
}
} // namespace

std::vector<uint8_t> captureSeriesState()
{
    std::vector<uint8_t> bytes(2048);
    ByteWriter w(bytes.data(), bytes.size());
    w.u16(SERIES_STATE_VERSION);
    w.u32(static_cast<uint32_t>(game_values.tourstopcurrent));
    w.u8(game_values.worldskipscoreboard ? 1 : 0);
    visitSeriesState([&w](short& value) { w.i16(value); }, game_values);
    w.u64(game_values.matchtype == MatchType::World ? worldMapHash() : 0);
    bytes.resize(w.ok() ? w.size() : 0);
    return bytes;
}

bool applySeriesState(const std::vector<uint8_t>& bytes)
{
    ByteReader r(bytes.data(), bytes.size());
    if (r.u16() != SERIES_STATE_VERSION)
        return false;
    //Check first, then write
    ByteReader check = r;
    check.u32();
    check.u8();
    visitSeriesState([&check](short&) { check.i16(); }, game_values);
    check.u64();
    if (!check.ok() || check.remaining() != 0)
        return false;

    game_values.tourstopcurrent = r.u32();
    game_values.worldskipscoreboard = r.u8() != 0;
    visitSeriesState([&r](short& value) { value = r.i16(); }, game_values);
    return true;
}

uint64_t seriesStateHash(const std::vector<uint8_t>& bytes)
{
    return net::fnv1a64(bytes.data(), bytes.size());
}

uint64_t worldMapHash()
{
    const short width = g_worldmap.getWidth();
    const short height = g_worldmap.getHeight();
    if (width <= 0 || height <= 0)
        return 0;

    uint64_t hash = 0xcbf29ce484222325ull;
    auto add = [&hash](int32_t value) { hash = net::fnv1a64(&value, sizeof(value), hash); };
    add(width);
    add(height);
    for (short row = 0; row < height; row++) {
        for (short col = 0; col < width; col++) {
            const WorldMapTile& tile = g_worldmap.getTile(col, row);
            add(tile.iCompleted);
            add(tile.iForegroundSprite);
            add(tile.iConnectionType);
            add((tile.fConnection[0] ? 1 : 0) | (tile.fConnection[1] ? 2 : 0) | (tile.fConnection[2] ? 4 : 0) | (tile.fConnection[3] ? 8 : 0));
        }
    }
    const Vec2s player = g_worldmap.GetPlayerCurrentTile();
    add(player.x);
    add(player.y);
    for (const WorldVehicle& vehicle : g_worldmap.getVehicles()) {
        const Vec2s tile = vehicle.getCurrentTile();
        add(vehicle.isEnabled() ? 1 : 0);
        add(tile.x);
        add(tile.y);
    }
    return hash;
}

std::string seriesStandings()
{
    std::string text = "winner " + std::to_string(game_values.tournamentwinner) + " wins";
    for (short t = 0; t < g_sim.score_cnt && t < 4; t++)
        text += (t ? "," : " ") + std::to_string(game_values.tournament_scores[t].wins);
    text += " totals";
    for (short t = 0; t < g_sim.score_cnt && t < 4; t++)
        text += (t ? "," : " ") + std::to_string(game_values.tournament_scores[t].total);
    return text;
}

std::string seriesMatchName()
{
    switch (game_values.matchtype) {
        case MatchType::Tournament: return "Tournament, " + std::to_string(game_values.tournamentgames) + " wins";
        case MatchType::Tour: return "Tour";
        case MatchType::World: return "World";
        default: return "";
    }
}

std::string seriesFileName()
{
    if (game_values.matchtype == MatchType::Tour && game_values.tourindex >= 0 && static_cast<size_t>(game_values.tourindex) < tourlist->count())
        return GetNameFromFileName(tourlist->at(game_values.tourindex).string(), true);
    if (game_values.matchtype == MatchType::World && game_values.worldindex >= 0 && static_cast<size_t>(game_values.worldindex) < worldlist->count())
        return GetNameFromFileName(worldlist->at(game_values.worldindex).string(), true);
    return "(the host chooses each game)";
}

} // namespace rollback

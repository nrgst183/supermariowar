#include "Snapshot.h"
#include "DebugProfile.h"

#include "GameMode.h"
#include "GameValues.h"
#include "map.h"
#include "ObjectBase.h"
#include "player.h"
#include "SimArena.h"
#include "SimulationState.h"
#include "movingplatform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <typeinfo>

namespace rollback {

namespace {
struct GameModeEntry {
    const CGameMode* mode = nullptr;
    size_t size = 0;
};
std::array<GameModeEntry, 40> gameModes;
size_t gameModeCount = 0;

size_t gameModeSize(const CGameMode* mode)
{
    for (size_t i = 0; i < gameModeCount; i++) {
        if (gameModes[i].mode == mode)
            return gameModes[i].size;
    }
    return 0;
}

// Every piece of simulation state outside the arena (see Snapshot.h).
// `fn(void* data, size_t size, const char* name)`.
template <typename Fn>
void forEachStateRegion(Fn&& fn)
{
    fn(&g_sim, sizeof(g_sim), "g_sim");

    // The runtime fields of game_values (the rest are settings)
    CGameValues& gv = game_values;
    fn(&gv.flags, sizeof(gv.flags), "game_values.flags");
    fn(&gv.appstate, sizeof(gv.appstate), "game_values.appstate");
    fn(&gv.gamemode, sizeof(gv.gamemode), "game_values.gamemode");
    fn(&gv.screenfade, sizeof(gv.screenfade), "game_values.screenfade");
    fn(&gv.screenfadespeed, sizeof(gv.screenfadespeed), "game_values.screenfadespeed");
    fn(&gv.tournamentwinner, sizeof(gv.tournamentwinner), "game_values.tournamentwinner");
    fn(&gv.tourstopcurrent, sizeof(gv.tourstopcurrent), "game_values.tourstopcurrent");
    fn(&gv.storedpowerups, sizeof(gv.storedpowerups), "game_values.storedpowerups");
    fn(&gv.gamepowerups, sizeof(gv.gamepowerups), "game_values.gamepowerups");
    fn(&gv.worldpowerups, sizeof(gv.worldpowerups), "game_values.worldpowerups");
    fn(&gv.worldpowerupcount, sizeof(gv.worldpowerupcount), "game_values.worldpowerupcount");
    fn(&gv.worldskipscoreboard, sizeof(gv.worldskipscoreboard), "game_values.worldskipscoreboard");
    fn(&gv.bulletbilltimer, sizeof(gv.bulletbilltimer), "game_values.bulletbilltimer");
    fn(&gv.bulletbillspawntimer, sizeof(gv.bulletbillspawntimer), "game_values.bulletbillspawntimer");
    fn(&gv.cputurn, sizeof(gv.cputurn), "game_values.cputurn");
    fn(&gv.tournament_scores, sizeof(gv.tournament_scores), "game_values.tournament_scores");
    fn(&gv.windaffectsplayers, sizeof(gv.windaffectsplayers), "game_values.windaffectsplayers");
    fn(&gv.spinscreen, sizeof(gv.spinscreen), "game_values.spinscreen");
    fn(&gv.reversewalk, sizeof(gv.reversewalk), "game_values.reversewalk");
    fn(&gv.spotlights, sizeof(gv.spotlights), "game_values.spotlights");
    fn(&gv.unlocksecret1part1, sizeof(gv.unlocksecret1part1), "game_values.unlocksecret1part1");
    fn(&gv.unlocksecret1part2, sizeof(gv.unlocksecret1part2), "game_values.unlocksecret1part2");
    fn(&gv.unlocksecret2part1, sizeof(gv.unlocksecret2part1), "game_values.unlocksecret2part1");
    fn(&gv.unlocksecret2part2, sizeof(gv.unlocksecret2part2), "game_values.unlocksecret2part2");
    fn(&gv.unlocksecret3part1, sizeof(gv.unlocksecret3part1), "game_values.unlocksecret3part1");
    fn(&gv.unlocksecret3part2, sizeof(gv.unlocksecret3part2), "game_values.unlocksecret3part2");
    fn(&gv.unlocksecretunlocked, sizeof(gv.unlocksecretunlocked), "game_values.unlocksecretunlocked");

    g_map->forEachRuntimeRegion(fn);

    // The active game mode, settings included (they don't change during a match)
    if (CGameMode* mode = game_values.gamemode) {
        if (const size_t size = gameModeSize(mode))
            fn(static_cast<void*>(mode), size, "gamemode");
    }
}

} // namespace

void registerGameMode(const CGameMode* mode, size_t size)
{
    for (size_t i = 0; i < gameModeCount; i++) {
        if (gameModes[i].mode == mode) {
            gameModes[i].size = size;
            return;
        }
    }

    if (gameModeCount < gameModes.size())
        gameModes[gameModeCount++] = {mode, size};
}

namespace {
// The arena bytes that don't matter (see Snapshot.h), sorted: the object lists' unused capacity
struct Hole {
    size_t offset;
    size_t size;
};

size_t findHoles(std::array<Hole, 4>& holes)
{
    sim::Arena& arena = sim::Arena::instance();
    const CObjectContainer* containers[] = {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]};
    size_t count = 0;
    for (const CObjectContainer* container : containers) {
        const CObjectContainer::List& list = container->list();
        const auto* data = reinterpret_cast<const std::byte*>(list.data());
        if (!data || !arena.contains(data))
            continue;
        const size_t start = static_cast<size_t>(data - arena.base()) + list.size() * sizeof(*list.data());
        const size_t end = static_cast<size_t>(data - arena.base()) + list.capacity() * sizeof(*list.data());
        if (end > start + 32)
            holes[count++] = {start, end - start};
    }
    //(sorted: four at most)
    for (size_t i = 1; i < count; i++) {
        for (size_t k = i; k > 0 && holes[k].offset < holes[k - 1].offset; k--)
            std::swap(holes[k], holes[k - 1]);
    }
    return count;
}
} // namespace

void saveState(Snapshot& snapshot)
{
    prof::Scope profile(prof::Section::SaveState);
    sim::Arena& arena = sim::Arena::instance();

    // The arena: the spans between the bytes that don't matter
    std::array<Hole, 4> holes;
    const size_t holeCount = findHoles(holes);
    snapshot.m_arenaUsed = arena.used();
    snapshot.m_spans.clear();
    size_t at = 0;
    size_t saved = 0;
    for (size_t h = 0; h < holeCount; h++) {
        const Hole& hole = holes[h];
        if (hole.offset >= snapshot.m_arenaUsed)
            break;
        if (hole.offset > at) {
            snapshot.m_spans.push_back({static_cast<uint32_t>(at), static_cast<uint32_t>(hole.offset - at)});
            saved += hole.offset - at;
        }
        at = std::max(at, hole.offset + hole.size);
    }
    if (at < snapshot.m_arenaUsed) {
        snapshot.m_spans.push_back({static_cast<uint32_t>(at), static_cast<uint32_t>(snapshot.m_arenaUsed - at)});
        saved += snapshot.m_arenaUsed - at;
    }
    snapshot.m_arenaSaved = saved;

    // Then the regions: lay them out first, to know the size
    size_t offset = saved;
    size_t count = 0;
    forEachStateRegion([&](void* data, size_t size, const char* name) {
        if (count == Snapshot::MAX_REGIONS) {
            fprintf(stderr, "[rollback] Too many state regions, raise Snapshot::MAX_REGIONS\n");
            return;
        }
        snapshot.m_regions[count++] = {data, size, offset, name};
        offset += size;
    });
    snapshot.m_regionCount = count;
    snapshot.m_size = offset;

    // Grows only when the state is larger than ever before (the pieces follow each other, no padding)
    if (snapshot.m_bytes.size() < snapshot.m_size)
        snapshot.m_bytes.resize(snapshot.m_size + snapshot.m_size / 4);

    std::byte* out = snapshot.m_bytes.data();
    size_t pos = 0;
    for (const Snapshot::Span& span : snapshot.m_spans) {
        std::memcpy(out + pos, arena.base() + span.offset, span.size);
        pos += span.size;
    }
    for (size_t i = 0; i < count; i++) {
        const Snapshot::Region& region = snapshot.m_regions[i];
        std::memcpy(out + region.offset, region.data, region.size);
    }

    snapshot.m_nextNetworkId = CObject::nextNetworkId();
    snapshot.m_tick = g_sim.tick;
}

void loadState(const Snapshot& snapshot)
{
    prof::Scope profile(prof::Section::LoadState);
    if (snapshot.empty())
        return;

    // The pause and exit dialog flags live in game_values.flags, but belong
    // to the frame logic around the simulation, not to the simulation
    GameplayFlags& flags = game_values.flags;
    const bool pausegame = flags.pausegame;
    const bool exitinggame = flags.exitinggame;
    const bool exityes = flags.exityes;

    const std::byte* in = snapshot.m_bytes.data();
    sim::Arena& arena = sim::Arena::instance();
    const size_t currentUsed = arena.used();
    size_t pos = 0;
    for (const Snapshot::Span& span : snapshot.m_spans) {
        std::memcpy(arena.base() + span.offset, in + pos, span.size);
        pos += span.size;
    }
    //(what was used after the saved state: zero again, as if never used)
    if (currentUsed > snapshot.m_arenaUsed)
        std::memset(arena.base() + snapshot.m_arenaUsed, 0, currentUsed - snapshot.m_arenaUsed);
    for (size_t i = 0; i < snapshot.m_regionCount; i++) {
        const Snapshot::Region& region = snapshot.m_regions[i];
        std::memcpy(region.data, in + region.offset, region.size);
    }

    CObject::setNextNetworkId(snapshot.m_nextNetworkId);

    flags.pausegame = pausegame;
    flags.exitinggame = exitinggame;
    flags.exityes = exityes;
}

std::vector<std::byte> Snapshot::arenaBytes() const
{
    std::vector<std::byte> arena(m_arenaUsed, std::byte {0});
    size_t pos = 0;
    for (const Span& span : m_spans) {
        std::memcpy(arena.data() + span.offset, m_bytes.data() + pos, span.size);
        pos += span.size;
    }
    return arena;
}

uint64_t Snapshot::hash() const
{
    uint64_t h = hashBytes(m_bytes.data(), m_size);
    h = hashBytes(&m_nextNetworkId, sizeof(m_nextNetworkId), h);
    return h;
}

uint64_t hashBytes(const void* data, size_t size, uint64_t seed)
{
    // Four independent multiply-xor lanes over 32-byte blocks, then a final
    // mix. Any single changed word changes the result.
    constexpr uint64_t PRIME = 0x100000001b3ull;
    uint64_t lane[4] = {0xcbf29ce484222325ull ^ seed, 0x84222325cbf29ce4ull, 0x9e3779b97f4a7c15ull, 0xc2b2ae3d27d4eb4full};

    const auto* bytes = static_cast<const unsigned char*>(data);
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        for (int k = 0; k < 4; k++) {
            uint64_t word;
            std::memcpy(&word, bytes + i + 8 * k, sizeof(word));
            lane[k] = (lane[k] ^ word) * PRIME;
        }
    }

    uint64_t h = lane[0] ^ (lane[1] * 31) ^ (lane[2] * 961) ^ (lane[3] * 29791) ^ size;
    for (; i < size; i++)
        h = (h ^ bytes[i]) * PRIME;

    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

namespace {
// A known allocation in the arena, to name the owner of differing bytes
struct KnownBlock {
    const std::byte* address;
    std::string name;
};

std::vector<KnownBlock> knownArenaBlocks()
{
    std::vector<KnownBlock> blocks;
    auto add = [&](const void* address, std::string name) {
        blocks.push_back({static_cast<const std::byte*>(address), std::move(name)});
    };

    for (CPlayer* player : g_sim.players)
        add(player, std::format("CPlayer {}", player->getGlobalID()));

    const CObjectContainer* containers[] = {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]};
    const char* containerNames[] = {"noncolcontainer", "objectcontainer[0]", "objectcontainer[1]", "objectcontainer[2]"};
    for (size_t c = 0; c < 4; c++) {
        add(containers[c]->list().data(), std::format("{} buffer", containerNames[c]));
        for (const std::unique_ptr<CObject>& obj : containers[c]->list()) {
            const CObject& object = *obj;
            add(&object, std::format("{} #{} in {} at ({}, {})", typeid(object).name(), object.networkId(), containerNames[c], object.x(), object.y()));
        }
    }

    for (MovingPlatform* platform : g_map->permanentPlatforms())
        add(platform, "MovingPlatform (permanent)");
    for (MovingPlatform* platform : g_map->temporaryPlatforms())
        add(platform, "MovingPlatform (temporary)");

    std::sort(blocks.begin(), blocks.end(), [](const KnownBlock& a, const KnownBlock& b) { return a.address < b.address; });
    return blocks;
}

std::string nameArenaOffset(const std::vector<KnownBlock>& blocks, size_t offset)
{
    const std::byte* address = sim::Arena::instance().base() + offset;
    auto it = std::upper_bound(blocks.begin(), blocks.end(), address, [](const std::byte* a, const KnownBlock& b) { return a < b.address; });
    if (it == blocks.begin())
        return "arena bookkeeping or an unknown block";

    --it;
    return std::format("{} +{} (if the block is that large)", it->name, address - it->address);
}

// Names the member of g_sim at a byte offset
std::string nameSimulationStateOffset(size_t offset)
{
    const auto* base = reinterpret_cast<const std::byte*>(&g_sim);
    auto at = [&](const void* member) { return static_cast<size_t>(static_cast<const std::byte*>(member) - base); };

    const std::pair<size_t, const char*> members[] = {
        {at(&g_sim.noncolcontainer), "noncolcontainer"},
        {at(&g_sim.objectcontainer), "objectcontainer"},
        {at(&g_sim.players), "players"},
        {at(&g_sim.score), "score"},
        {at(&g_sim.score_cnt), "score_cnt"},
        {at(&g_sim.rng), "rng"},
        {at(&g_sim.respawnCount), "respawnCount"},
        {at(&g_sim.countdownState), "countdownState"},
        {at(&g_sim.countdownTimer), "countdownTimer"},
        {at(&g_sim.windTimer), "windTimer"},
        {at(&g_sim.nextWind), "nextWind"},
        {at(&g_sim.playerKeys), "playerKeys"},
        {at(&g_sim.previousInput), "previousInput"},
        {at(&g_sim.tick), "tick"},
    };

    const char* name = "?";
    for (const auto& [memberOffset, memberName] : members) {
        if (memberOffset <= offset)
            name = memberName;
    }
    return name;
}
} // namespace

std::string describeDifference(const Snapshot& expected, const Snapshot& actual, size_t maxLines)
{
    std::string out;
    size_t lines = 0;
    auto line = [&](const std::string& text) {
        if (lines++ < maxLines)
            out += "    " + text + "\n";
    };

    if (expected.nextNetworkId() != actual.nextNetworkId())
        line(std::format("next object id: {} vs {}", expected.nextNetworkId(), actual.nextNetworkId()));

    // The arena: report differing 16-byte granules, merged into ranges
    if (expected.arenaSize() != actual.arenaSize())
        line(std::format("arena used bytes: {} vs {}", expected.arenaSize(), actual.arenaSize()));

    const std::vector<KnownBlock> blocks = knownArenaBlocks();
    const size_t arenaCommon = std::min(expected.arenaSize(), actual.arenaSize());
    const std::vector<std::byte> expectedArena = expected.arenaBytes();
    const std::vector<std::byte> actualArena = actual.arenaBytes();
    for (size_t pos = 0; pos < arenaCommon;) {
        if (expectedArena[pos] == actualArena[pos]) {
            pos++;
            continue;
        }
        size_t end = pos;
        while (end < arenaCommon && (end - pos < 64) && expectedArena[end] != actualArena[end])
            end++;
        line(std::format("arena bytes {}..{}: {}", pos, end, nameArenaOffset(blocks, pos)));
        pos = end + 1;
    }

    // The other regions
    for (size_t i = 0; i < std::min(expected.regionCount(), actual.regionCount()); i++) {
        const Snapshot::Region& a = expected.region(i);
        const Snapshot::Region& b = actual.region(i);
        if (a.data != b.data || a.size != b.size) {
            line(std::format("region {} differs in layout ({} vs {})", a.name, b.name, i));
            continue;
        }
        for (size_t k = 0; k < a.size; k++) {
            if (expected.bytes()[a.offset + k] != actual.bytes()[b.offset + k]) {
                if (a.data == &g_sim)
                    line(std::format("g_sim.{} (+{})", nameSimulationStateOffset(k), k));
                else
                    line(std::format("{} +{} (of {} bytes)", a.name, k, a.size));
                break;
            }
        }
    }

    if (lines > maxLines)
        out += std::format("    ... and {} more\n", lines - maxLines);
    return out;
}

} // namespace rollback

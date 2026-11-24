#include "StateChecksum.h"

#include "DebugProfile.h"

#include "GameMode.h"
#include "GameValues.h"
#include "map.h"
#include "movingplatform.h"
#include "ObjectContainer.h"
#include "player.h"
#include "SimulationState.h"
#include "IO_Block.h"
#include "objects/moving/MO_CarriedObject.h"
#include "objects/moving/MovingObject.h"

#include <cstring>
#include <string>
#include <type_traits>

namespace rollback {

namespace {
// FNV-1a over explicitly sized values (never over whole structs, whose
// padding bytes are undefined)
class Hasher {
public:
    template <typename T>
    void add(T value) {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
        if constexpr (std::is_same_v<T, bool>) {
            addBytes(static_cast<uint8_t>(value ? 1 : 0));
        } else if constexpr (std::is_enum_v<T>) {
            addBytes(static_cast<int64_t>(value));
        } else {
            addBytes(value);
        }
    }

    uint64_t value() const { return m_hash; }

private:
    template <typename T>
    void addBytes(T value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof(T); i++)
            m_hash = (m_hash ^ bytes[i]) * 0x100000001b3ull;
    }

    uint64_t m_hash = 0xcbf29ce484222325ull;
};
} // namespace

// Reads the private state of the player (a friend of CPlayer)
struct PeerChecksumAccess {
    static void addPlayer(Hasher& h, const CPlayer& p) {
        h.add(p.globalID);
        h.add(p.teamID);
        h.add(p.ix);
        h.add(p.iy);
        h.add(p.fx);
        h.add(p.fy);
        h.add(p.velx);
        h.add(p.vely);
        h.add(p.state);
        h.add(p.inair);
        h.add(p.onice);
        h.add(p.lockjump);
        h.add(p.powerup);
        h.add(p.projectiles);
        h.add(p.bobomb);
        h.add(p.frozen);
        h.add(p.frozentimer);
        h.add(p.killsinrow);
        h.add(p.extrajumps);
        h.add(p.flying);
        h.add(p.spawntimer);
        h.add(p.waittimer);
        h.add(p.ownerPlayerID);
        h.add(p.carriedItem ? p.carriedItem->networkId() : -1);
        h.add(p.respawncounter ? *p.respawncounter : -1);
    }

    // The player's values one by one (stateFields)
    static void addPlayerFields(std::vector<StateField>& out, const CPlayer& p, const std::string& who) {
        auto bits = [](float f) {
            uint32_t b = 0;
            std::memcpy(&b, &f, sizeof(b));
            return static_cast<uint64_t>(b);
        };
        out.push_back({who + " x", bits(p.fx), true});
        out.push_back({who + " y", bits(p.fy), true});
        out.push_back({who + " vx", bits(p.velx), true});
        out.push_back({who + " vy", bits(p.vely), true});
        out.push_back({who + " state", static_cast<uint64_t>(p.state)});
        out.push_back({who + " inair", static_cast<uint64_t>(p.inair)});
        out.push_back({who + " powerup", static_cast<uint64_t>(static_cast<int64_t>(p.powerup))});
        //(the rest of what moves it, not in the checksum's list: what may differ before x and y do)
        out.push_back({who + " next y", bits(p.fPrecalculatedY), true});
        out.push_back({who + " old vx", bits(p.oldvelx), true});
        auto small = [](long v) { return static_cast<uint64_t>(static_cast<int64_t>(v)) & 0xffffu; };
        out.push_back({who + " onice", small(p.onice)});
        out.push_back({who + " lockjump", small(p.lockjump)});
        out.push_back({who + " lockfall", small(p.lockfall)});
        out.push_back({who + " fallthrough", small(p.fallthrough)});
        out.push_back({who + " superjump", small(p.superjumptimer * 16 + p.superjumptype)});
        out.push_back({who + " platform x/y", small(p.iHorizontalPlatformCollision * 256 + p.iVerticalPlatformCollision)});
        out.push_back({who + " extrajumps", small(p.extrajumps)});
        out.push_back({who + " flying", small(p.flying ? 1000 + p.flyingtimer : 0)});
        out.push_back({who + " frozen", small(p.frozen ? 1000 + p.frozentimer : 0)});
        out.push_back({who + " timers", small(p.spawntimer * 64 + p.waittimer)});
        Hasher rest;
        addPlayer(rest, p);
        out.push_back({who + " (all)", rest.value()});
    }
};

uint64_t peerChecksum()
{
    prof::Scope profile(prof::Section::Checksum);
    Hasher h;

    h.add(g_sim.tick);
    const RandomNumberGenerator::State& rng = g_sim.rng.state();
    for (uint32_t word : rng.words)
        h.add(word);
    h.add(rng.index);

    h.add(g_sim.countdownState);
    h.add(g_sim.countdownTimer);
    h.add(g_sim.windTimer);
    h.add(g_sim.nextWind);
    for (uint8_t input : g_sim.previousInput)
        h.add(input);
    if (g_sim.cpuTakeover)    //online only: leaves the other checksums as they were
        h.add(g_sim.cpuTakeover);

    h.add(static_cast<uint32_t>(g_sim.players.size()));
    for (const CPlayer* player : g_sim.players)
        PeerChecksumAccess::addPlayer(h, *player);

    h.add(g_sim.score_cnt);
    for (short i = 0; i < g_sim.score_cnt; i++) {
        h.add(g_sim.score[i].score);
        for (short subscore : g_sim.score[i].subscore)
            h.add(subscore);
    }

    for (const CObjectContainer* container : {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]}) {
        h.add(static_cast<uint32_t>(container->list().size()));
        for (const std::unique_ptr<CObject>& obj : container->list()) {
            h.add(obj->networkId());
            h.add(obj->getObjectType());
            if (const auto* moving = dynamic_cast<const IO_MovingObject*>(obj.get()))
                h.add(moving->getMovingObjectType());
            h.add(obj->x());
            h.add(obj->y());
            h.add(obj->velX());
            h.add(obj->velY());
            h.add(obj->GetState());
            h.add(obj->isDead());
        }
    }

    for (const MovingPlatform* platform : g_map->permanentPlatforms()) {
        const RenderPosition pos = platform->renderPosition();
        h.add(pos.fx);
        h.add(pos.fy);
    }
    h.add(static_cast<uint32_t>(g_map->temporaryPlatforms().size()));
    for (const MovingPlatform* platform : g_map->temporaryPlatforms()) {
        const RenderPosition pos = platform->renderPosition();
        h.add(pos.fx);
        h.add(pos.fy);
    }

    for (short x = 0; x < MAPWIDTH; x++) {
        for (short y = 0; y < MAPHEIGHT; y++)
            h.add(g_map->block(x, y) ? g_map->block(x, y)->networkId() : -1);
    }

    if (const CGameMode* mode = game_values.gamemode) {
        h.add(mode->gamemode);
        h.add(mode->gameover);
        h.add(mode->winningteam);
        h.add(mode->goal);
    }

    const GameplayFlags& flags = game_values.flags;
    h.add(flags.screenshaketimer);
    h.add(flags.slowdownon);
    h.add(flags.slowdowncounter);
    h.add(flags.swapplayers);
    h.add(flags.gamewindx);
    h.add(flags.forceexittimer);
    h.add(game_values.screenfade);
    h.add(game_values.screenfadespeed);
    h.add(game_values.appstate);

    return h.value();
}

std::vector<StateField> stateFields()
{
    std::vector<StateField> out;
    {
        Hasher h;
        const RandomNumberGenerator::State& rng = g_sim.rng.state();
        for (uint32_t word : rng.words)
            h.add(word);
        h.add(rng.index);
        out.push_back({"random numbers", h.value()});
    }
    for (size_t k = 0; k < sizeof(g_sim.previousInput); k++)
        out.push_back({"input " + std::to_string(k + 1), g_sim.previousInput[k]});
    for (const CPlayer* player : g_sim.players)
        PeerChecksumAccess::addPlayerFields(out, *player, "player " + std::to_string(player->getGlobalID() + 1));
    for (short i = 0; i < g_sim.score_cnt; i++)
        out.push_back({"score " + std::to_string(i + 1), static_cast<uint64_t>(static_cast<int64_t>(g_sim.score[i].score))});

    const char* containerNames[] = {"effects", "objects 0", "objects 1", "objects 2"};
    int c = 0;
    for (const CObjectContainer* container : {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]}) {
        Hasher h;
        h.add(static_cast<uint32_t>(container->list().size()));
        for (const std::unique_ptr<CObject>& obj : container->list()) {
            h.add(obj->networkId());
            h.add(obj->getObjectType());
            h.add(obj->x());
            h.add(obj->y());
            h.add(obj->velX());
            h.add(obj->velY());
            h.add(obj->GetState());
            h.add(obj->isDead());
        }
        out.push_back({std::string(containerNames[c++]) + " (" + std::to_string(container->list().size()) + ")", h.value()});
    }
    {
        Hasher h;
        for (const MovingPlatform* platform : g_map->permanentPlatforms()) {
            const RenderPosition pos = platform->renderPosition();
            h.add(pos.fx);
            h.add(pos.fy);
        }
        for (const MovingPlatform* platform : g_map->temporaryPlatforms()) {
            const RenderPosition pos = platform->renderPosition();
            h.add(pos.fx);
            h.add(pos.fy);
        }
        out.push_back({"platforms", h.value()});
    }
    {
        Hasher h;
        for (short x = 0; x < MAPWIDTH; x++)
            for (short y = 0; y < MAPHEIGHT; y++)
                h.add(g_map->block(x, y) ? g_map->block(x, y)->networkId() : -1);
        out.push_back({"blocks", h.value()});
    }
    return out;
}

} // namespace rollback

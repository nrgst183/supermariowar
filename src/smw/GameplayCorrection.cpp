#include "GameplayCorrection.h"

#include "player.h"
#include "objects/moving/MO_CarriedObject.h"
#include "SimulationState.h"

#include <cmath>

namespace {
// Corrections larger than this (in pixels, either way) are drawn at once: they
// aren't a slightly different path but another place (a respawn, a warp,
// wrapping around the screen's edge)
constexpr float MAX_SMOOTHED = 64.0f;
// How much of the offset is left after each tick (0.7: a tenth after 6 ticks, ~100 ms)
constexpr float KEEP_PER_TICK = 0.7f;

int slotOf(const CPlayer& player)
{
    const int id = player.getGlobalID();
    return id >= 0 && id < 4 ? id : -1;
}

// Running around (not dying, spawning, warping): where it is drawn follows its position
bool plain(const CPlayer& player)
{
    return player.isready();
}
} // namespace

void CorrectionSmoother::beforeRollback()
{
    //(a rollback within a rollback: the first one's frame is what was shown)
    if (m_rollbackTick >= 0)
        return;
    m_rollbackTick = g_sim.tick;
    m_shown = {};
    for (const CPlayer* player : g_sim.players) {
        const int slot = slotOf(*player);
        if (slot < 0 || !plain(*player))
            continue;
        //(what was drawn: the position and the offset still on it)
        m_shown[static_cast<size_t>(slot)] = {true, player->renderPosition().ix, player->renderPosition().iy};
    }
}

void CorrectionSmoother::afterResimulatedTick()
{
    if (m_rollbackTick < 0 || g_sim.tick != m_rollbackTick)
        return;
    m_rollbackTick = -1;
    for (const CPlayer* player : g_sim.players) {
        const int slot = slotOf(*player);
        if (slot < 0)
            continue;
        const Shown& shown = m_shown[static_cast<size_t>(slot)];
        Offset& offset = m_offset[static_cast<size_t>(slot)];
        if (!shown.valid || !plain(*player)) {
            offset = {};
            continue;
        }
        const RenderPosition now = player->renderPosition();
        const float dx = static_cast<float>(shown.x - now.ix);
        const float dy = static_cast<float>(shown.y - now.iy);
        if (dx == 0 && dy == 0)
            continue;
        //Drawn before: shown + its offset; to stay there: the new position + the old offset + the difference
        const float x = offset.x + dx;
        const float y = offset.y + dy;
        if (std::fabs(x) > MAX_SMOOTHED || std::fabs(y) > MAX_SMOOTHED) {
            offset = {};
            m_snapped++;
            continue;
        }
        offset = {x, y, true};
        m_smoothed++;
    }
}

void CorrectionSmoother::afterTick()
{
    //(a rollback that never came back to its frame: forget it)
    if (m_rollbackTick >= 0 && g_sim.tick > m_rollbackTick + 1)
        m_rollbackTick = -1;
    for (Offset& offset : m_offset) {
        if (!offset.active)
            continue;
        offset.x *= KEEP_PER_TICK;
        offset.y *= KEEP_PER_TICK;
        if (std::fabs(offset.x) < 0.5f && std::fabs(offset.y) < 0.5f)
            offset = {};
    }
    //Players that stopped running around (died, warp) are drawn where they are
    for (const CPlayer* player : g_sim.players) {
        const int slot = slotOf(*player);
        if (slot >= 0 && !plain(*player))
            m_offset[static_cast<size_t>(slot)] = {};
    }
}

void CorrectionSmoother::reset()
{
    m_rollbackTick = -1;
    m_shown = {};
    m_offset = {};
    m_savedCount = 0;
}

void CorrectionSmoother::apply()
{
    m_savedCount = 0;
    for (CPlayer* player : g_sim.players) {
        const int slot = slotOf(*player);
        if (slot < 0 || !m_offset[static_cast<size_t>(slot)].active || m_savedCount >= SLOTS)
            continue;
        const Offset& offset = m_offset[static_cast<size_t>(slot)];
        auto moved = [&offset](RenderPosition position) {
            position.fx += offset.x;
            position.fy += offset.y;
            position.ix = static_cast<short>(position.ix + static_cast<short>(std::lround(offset.x)));
            position.iy = static_cast<short>(position.iy + static_cast<short>(std::lround(offset.y)));
            return position;
        };
        Saved& saved = m_saved[static_cast<size_t>(m_savedCount++)];
        saved = {player, player->renderPosition(), player->heldItem(), {}};
        player->setRenderPosition(moved(saved.position));
        if (saved.held) {
            saved.heldPosition = saved.held->renderPosition();
            saved.held->setRenderPosition(moved(saved.heldPosition));
        }
    }
}

void CorrectionSmoother::restore()
{
    for (int k = m_savedCount - 1; k >= 0; k--) {
        const Saved& saved = m_saved[static_cast<size_t>(k)];
        if (saved.held)
            saved.held->setRenderPosition(saved.heldPosition);
        saved.player->setRenderPosition(saved.position);
    }
    m_savedCount = 0;
}

#include "GameplayInterpolation.h"

#include "map.h"
#include "movingplatform.h"
#include "ObjectContainer.h"
#include "player.h"
#include "SimulationState.h"

namespace {
// Calls `fn` for everything that is drawn interpolated
template <typename PlayerFn, typename ObjectFn, typename PlatformFn>
void forEachMoving(PlayerFn&& playerFn, ObjectFn&& objectFn, PlatformFn&& platformFn)
{
    for (CPlayer* player : g_sim.players)
        playerFn(*player);

    for (const CObjectContainer* container : {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]}) {
        for (const std::unique_ptr<CObject>& obj : container->list())
            objectFn(*obj);
    }

    for (MovingPlatform* platform : g_map->permanentPlatforms())
        platformFn(*platform);
    for (MovingPlatform* platform : g_map->temporaryPlatforms())
        platformFn(*platform);
}

template <typename T>
void captureOne(T& thing)
{
    thing.renderPrevious = {thing.renderPosition(), true};
}

template <typename T, typename SavedList>
void applyOne(T& thing, SavedList& saved, float alpha)
{
    const RenderPosition current = thing.renderPosition();
    const RenderPosition drawn = interpolatePosition(thing.renderPrevious, current, alpha);
    if (drawn.ix == current.ix && drawn.iy == current.iy && drawn.fx == current.fx && drawn.fy == current.fy)
        return;

    saved.push_back({&thing, current});
    thing.setRenderPosition(drawn);
}
} // namespace


void RenderInterpolator::capture()
{
    forEachMoving(
        [](CPlayer& player) { captureOne(player); },
        [](CObject& object) { captureOne(object); },
        [](MovingPlatform& platform) { captureOne(platform); });
}

void RenderInterpolator::apply(float alpha)
{
    forEachMoving(
        [&](CPlayer& player) { applyOne(player, m_players, alpha); },
        [&](CObject& object) { applyOne(object, m_objects, alpha); },
        [&](MovingPlatform& platform) { applyOne(platform, m_platforms, alpha); });
}

void RenderInterpolator::restore()
{
    // clear() keeps the capacity, so this doesn't allocate after the first frames
    for (const auto& saved : m_players)
        saved.thing->setRenderPosition(saved.position);
    m_players.clear();

    for (const auto& saved : m_objects)
        saved.thing->setRenderPosition(saved.position);
    m_objects.clear();

    for (const auto& saved : m_platforms)
        saved.thing->setRenderPosition(saved.position);
    m_platforms.clear();
}

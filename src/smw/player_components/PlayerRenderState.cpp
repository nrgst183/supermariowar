#include "PlayerRenderState.h"

#include "../player.h"

#include <array>

namespace {
std::array<PlayerRenderState, 4> renderStates;
}

PlayerRenderState& playerRenderState(short globalID)
{
    return renderStates[static_cast<size_t>(globalID) % renderStates.size()];
}

PlayerRenderState& playerRenderState(const CPlayer& player)
{
    return playerRenderState(player.getGlobalID());
}

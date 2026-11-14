#include "PlayerWings.h"

#include "../player.h"
#include "PlayerRenderState.h"
#include "RenderFrame.h"

#include "ResourceManager.h"

void PlayerWings::reset(CPlayer& player)
{
    PlayerRenderState& anim = playerRenderState(player);
    anim.wingsTimer = 0;
    anim.wingsFrame = 0;
}

void PlayerWings::draw(CPlayer &player)
{
    PlayerRenderState& anim = playerRenderState(player);
    unsigned char& iWingsTimer = anim.wingsTimer;
    unsigned short& iWingsFrame = anim.wingsFrame;

    if (player.flying) {
        if (render::advancesAnimations() && ++iWingsTimer >= 8) {
            iWingsTimer = 0;
            iWingsFrame += 26;

            if (iWingsFrame > 26)
                iWingsFrame = 0;
        }
    } else {
        iWingsFrame = 26;
    }

    bool fPlayerFacingRight = player.isFacingRight();
    if (player.iswarping())
        rm->spr_wings.draw(
            player.ix + (fPlayerFacingRight ? - 19 : 15), player.iy - 10,
            {iWingsFrame, fPlayerFacingRight ? 0 : 32, 26, 32},
            static_cast<ClipEdge>(static_cast<short>(player.state) % 4), player.GetWarpPlane());
    else
        rm->spr_wings.draw(
            player.ix + (fPlayerFacingRight ? - 19 : 15), player.iy - 10,
            {iWingsFrame, fPlayerFacingRight ? 0 : 32, 26, 32});
}

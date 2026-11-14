#include "PlayerCape.h"

#include "../player.h"
#include "PlayerRenderState.h"

#include "GameValues.h"
#include "GlobalConstants.h"
#include "RenderFrame.h"
#include "ObjectContainer.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "objects/moving/MO_SpinAttack.h"


void PlayerCape::reset(CPlayer& player)
{
    PlayerRenderState& anim = playerRenderState(player);
    anim.capeTimer = 0;
    anim.capeFrameX = 0;
    anim.capeFrameY = 0;
    anim.capeUp = false;
    anim.capeYOffset = 0;
}

void PlayerCape::restart_animation(CPlayer& player)
{
    playerRenderState(player).capeTimer = 4;
}

void PlayerCape::spin(CPlayer &player)
{
    ifSoundOnPlay(rm->sfx_tailspin);

    restart_animation(player); //Add one extra frame to sync with spinning player sprite

    player.spin.spin(player);

    g_sim.objectcontainer[1].add(new MO_SpinAttack(player.getGlobalID(), player.getTeamID(),  KillStyle::Feather, player.isFacingRight(), 24));
}

void PlayerCape::draw(CPlayer &player)
{
    /* See eyecandy/cape.png for reference */
    PlayerRenderState& anim = playerRenderState(player);
    unsigned char& iCapeTimer = anim.capeTimer;
    unsigned short& iCapeFrameX = anim.capeFrameX;
    unsigned short& iCapeFrameY = anim.capeFrameY;
    bool& fCapeUp = anim.capeUp;
    short& iCapeYOffset = anim.capeYOffset;

    if (render::advancesAnimations() && ++iCapeTimer > 3) {
        if (player.spin.isSpinInProgress()) {
            iCapeFrameX = player.spin.toCapeFrameX();
            iCapeFrameY = 32;
            iCapeYOffset = -8;
        } else if ((!player.inair && player.velx != 0.0f) || (player.inair && player.vely < 1.0f)) {
            iCapeFrameX += 32;
            if (iCapeFrameX > 96)
                iCapeFrameX = 0;

            iCapeFrameY = 0;
            fCapeUp = true;
            iCapeYOffset = 0;
        } else if (!player.inair) {
            if (fCapeUp) {
                fCapeUp = false;
                iCapeFrameX = 0;
            } else {
                iCapeFrameX = 32;
            }

            iCapeFrameY = 96;
            iCapeYOffset = 0;
        } else if (player.inair) {
            iCapeFrameX += 32;
            if (iCapeFrameX > 64)
                iCapeFrameX = 0;

            iCapeFrameY = 64;
            fCapeUp = true;
            iCapeYOffset = -18;
        }

        iCapeTimer = 0;
    }

    bool fPlayerFacingRight = !game_values.reversewalk;
    if (player.spin.isSpinInProgress()) {
        if (player.spin.isReverseWalking())
            fPlayerFacingRight = game_values.reversewalk;
    } else {
        fPlayerFacingRight = player.isFacingRight();
    }

    if (player.iswarping()) {
        rm->spr_cape.draw(
            player.leftX() - PWOFFSET + (fPlayerFacingRight ? - 18 : 18), player.topY() - PHOFFSET + 4 + iCapeYOffset,
            {(fPlayerFacingRight ? 128 : 0) + iCapeFrameX, iCapeFrameY, 32, 32},
            static_cast<ClipEdge>(static_cast<short>(player.state) % 4), player.GetWarpPlane());
    } else {
        rm->spr_cape.draw(
            player.leftX() - PWOFFSET + (fPlayerFacingRight ? - 18 : 18), player.topY() - PHOFFSET + 4 + iCapeYOffset,
            {(fPlayerFacingRight ? 128 : 0) + iCapeFrameX, iCapeFrameY, 32, 32});
    }
}

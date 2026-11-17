
#include "eyecandy.h"
#include "GameValues.h"
#include "GSGameplay.h"
#include "RenderFrame.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "objects/carriable/CO_KuriboShoe.h"

#include <cassert>
#include <cmath>

bool shouldUpdateSprite()
{
    return !game_values.flags.pausegame && !game_values.flags.exitinggame && !game_values.flags.swapplayers;
}

void CPlayer::updateSprite()
{
    int iReverseSprite = game_values.reversewalk ? 1 : 0;

    //Use correct sprite (and animate)
    if (shouldUpdateSprite()) {
        //if player is warping from below, set them in the air
        if (iswarping()) {
            if (state == PlayerState::LeavingWarpDown || state == PlayerState::EnteringWarpUp)
                inair = true;
            else
                inair = false;
        }

        //lockjump is true when we are in the air (even if we fell of an edge)
        if (spin.isSpinInProgress()) {
            sprite_state = spin.toSpriteID();
        } else if (state == PlayerState::Spawning) {
            if (!(sprite_state & PGFX_FACING_LEFT_BIT))
                sprite_state = PGFX_JUMPING_R + iReverseSprite;
            else
                sprite_state = PGFX_JUMPING_L - iReverseSprite;
        } else if (inair) {
            frictionslidetimer = 0;
            rainsteptimer = 0;

            if (isFacingRight())
                sprite_state = PGFX_JUMPING_R;
            else
                sprite_state = PGFX_JUMPING_L;
        } else {
            if (velx > 0.0f) {
                if (playerKeys->game_left.fDown && !playerKeys->game_right.fDown && state == PlayerState::Ready) {
                    sprite_state = PGFX_STOPPING_R + iReverseSprite;

                    if (++frictionslidetimer > 3) {
                        frictionslidetimer = 0;
                        eyecandy[1].emplace<EC_SingleAnimation>(&rm->spr_frictionsmoke, ix, iy + PH - 12, 4, 4, 0, 0, 16, 16);
                    }
                } else {
                    if (onice && !playerKeys->game_right.fDown && !playerKeys->game_left.fDown)
                        sprite_state = PGFX_STANDING_R + iReverseSprite;
                    else {
                        if (--sprswitch < 1) {
                            if (game_values.reversewalk) {
                                if (sprite_state == PGFX_STANDING_L)
                                    sprite_state = PGFX_RUNNING_L;
                                else
                                    sprite_state = PGFX_STANDING_L;
                            } else {
                                if (sprite_state == PGFX_STANDING_R)
                                    sprite_state = PGFX_RUNNING_R;
                                else
                                    sprite_state = PGFX_STANDING_R;
                            }

                            sprswitch = 4;
                        } else {
                            //If animation timer hasn't fired, make sure we're facing the correct direction

                            if (game_values.reversewalk) {
                                if (!(sprite_state & PGFX_FACING_LEFT_BIT))
                                    sprite_state = PGFX_STANDING_L;
                            } else {
                                if (sprite_state & PGFX_FACING_LEFT_BIT)
                                    sprite_state = PGFX_STANDING_R;
                            }
                        }
                    }
                }
            } else if (velx < 0.0f) {
                if (playerKeys->game_right.fDown && !playerKeys->game_left.fDown && state == PlayerState::Ready) {
                    sprite_state = PGFX_STOPPING_L - iReverseSprite;

                    if (++frictionslidetimer > 3) {
                        frictionslidetimer = 0;
                        eyecandy[1].emplace<EC_SingleAnimation>(&rm->spr_frictionsmoke, ix + PW - 16, iy + PH - 12, 4, 4, 0, 0, 16, 16);
                    }
                } else {
                    if (onice && !playerKeys->game_right.fDown && !playerKeys->game_left.fDown)
                        sprite_state = PGFX_STANDING_L - iReverseSprite;
                    else {
                        if (--sprswitch < 1) {
                            if (game_values.reversewalk) {
                                if (sprite_state == PGFX_STANDING_R)
                                    sprite_state = PGFX_RUNNING_R;
                                else
                                    sprite_state = PGFX_STANDING_R;
                            } else {
                                if (sprite_state == PGFX_STANDING_L)
                                    sprite_state = PGFX_RUNNING_L;
                                else
                                    sprite_state = PGFX_STANDING_L;
                            }

                            sprswitch = 4;
                        } else {
                            //If animation timer hasn't fired, make sure we're facing the correct direction
                            if (game_values.reversewalk) {
                                if (sprite_state & PGFX_FACING_LEFT_BIT)
                                    sprite_state = PGFX_STANDING_R;
                            } else {
                                if (!(sprite_state & PGFX_FACING_LEFT_BIT))
                                    sprite_state = PGFX_STANDING_L;
                            }
                        }
                    }
                }
            } else {
                //standing
                if (playerKeys->game_left.fDown)
                    sprite_state = PGFX_STANDING_L - iReverseSprite;
                else if (playerKeys->game_right.fDown)
                    sprite_state = PGFX_STANDING_R + iReverseSprite;
                else {
                    if (sprite_state & PGFX_FACING_LEFT_BIT)
                        sprite_state = PGFX_STANDING_L;
                    else
                        sprite_state = PGFX_STANDING_R;
                }
            }
        }
    } else if (game_values.flags.swapplayers) {
        short iSpriteDirection = 0;

        if (fNewSwapX < fOldSwapX)
            iSpriteDirection = 1;

        if (--sprswitch < 1) {
            if (sprite_state == PGFX_STANDING_R + iSpriteDirection)
                sprite_state = PGFX_RUNNING_R + iSpriteDirection;
            else
                sprite_state = PGFX_STANDING_R + iSpriteDirection;

            sprswitch = 4;
        }
    }
}

void CPlayer::draw_spotlight()
{
    if (game_values.spotlights && state != PlayerState::Dead) {
        spotlightManager.track(this, -1 - globalID, ix + HALFPW, iy + HALFPH, 7);
    }
}

void CPlayer::draw_powerupRing()
{
    if (powerupused.has_value()) {
        short numeyecandy = 8;
        float addangle = TWO_PI / numeyecandy;
        float displayangle = powerupangle;

        for (short k = 0; k < numeyecandy; k++) {
            short powerupX = ix + HALFPW - 8 + static_cast<short>(powerupradius * cos(displayangle));
            short powerupY = iy + HALFPH - 8 + static_cast<short>(powerupradius * sin(displayangle));

            displayangle += addangle;

            if (iswarping())
                rm->spr_storedpowerupsmall.draw(powerupX, powerupY, {static_cast<short>(*powerupused) * 16, 0, 16, 16}, static_cast<ClipEdge>(static_cast<short>(state) % 4), GetWarpPlane());
            else
                rm->spr_storedpowerupsmall.draw(powerupX, powerupY, {static_cast<short>(*powerupused) * 16, 0, 16, 16});
        }
    }
}

void CPlayer::draw_winnerCrown()
{
    if (game_values.showwinningcrown && g_iWinningPlayer == teamID) {
        if (iswarping())
            rm->spr_crown.draw(ix + HALFPW - (isFacingRight() ? 4 : 10), iy - 10 - (kuriboshoe.is_on() ? 16 : 0), {0, 0, 14, 14}, static_cast<ClipEdge>(static_cast<short>(state) % 4), GetWarpPlane());
        else
            rm->spr_crown.draw(ix + HALFPW - (isFacingRight() ? 4 : 10), iy - 10 - (kuriboshoe.is_on() ? 16 : 0));
    }
}

// Advances the tail animation, which used to happen while drawing. The tail's
// state is gameplay state (it slows the fall, and finishing a tail attack uses
// up the leaf), so the simulation calls this once per tick, under exactly the
// conditions in which draw() used to draw the tail.
void CPlayer::updateTail()
{
    if (state == PlayerState::Waiting || state == PlayerState::Spawning)
        return;

    if (tanookisuit.isStatue() || kuriboshoe.is_on() || powerup == 3 || powerup == 8)
        return;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);
    const bool isChicken = gmChicken && gmChicken->chicken() == this;

    if (powerup == 7 || (powerup == -1 && isChicken && game_values.gamemodesettings.chicken.glide))
        tail.update(*this);
}

void CPlayer::draw()
{
    //Don't draw a player that is waiting to respawn
    if (state == PlayerState::Waiting)
        return;

    draw_spotlight();

    if (state == PlayerState::Spawning)
        return;

    //Draw player
    SpriteStrip*& pScoreboardSprite = playerRenderState(*this).scoreboardSprite;
    pScoreboardSprite = sprites;

    CPlayer* chicken = nullptr;
    if (auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode)) {
        chicken = gmChicken->chicken();
    }

    if (tanookisuit.isStatue()) {
        //Make sure the scoreboard still accurately represents the player
        if (bobomb)
            pScoreboardSprite = &rm->spr_bobomb[colorID];
        else if (chicken == this)
            pScoreboardSprite = &rm->spr_chocobo[colorID];
        else if (shyguy)
            pScoreboardSprite = &rm->spr_shyguy[colorID];

        tanookisuit.drawStatue(*this);

        return;
    } else if (bobomb) { //draw him as bob-omb
        pScoreboardSprite = &rm->spr_bobomb[colorID];

        //Add smoke to the top of the bomb
        short& bobombsmoketimer = playerRenderState(*this).bobombSmokeTimer;
        if (render::advancesAnimations() && ++bobombsmoketimer > 2 && (velx != 0.0f || vely != GRAVITATION) && state == PlayerState::Ready) {
            bobombsmoketimer = 0;
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_bobombsmoke, ix + HALFPH - 8, iy - PHOFFSET - 8, 4, 4);
        }
    } else if (chicken == this) { //draw him as chicken
        pScoreboardSprite = &rm->spr_chocobo[colorID];
    } else if (shyguy) { //draw him as chicken
        pScoreboardSprite = &rm->spr_shyguy[colorID];
        rm->spr_ownedtags.draw(ix - PWOFFSET - 8, iy - PHOFFSET - 8, {ownerColorOffsetX, 0, 48, 48});
    }

    if (ownerPlayerID > -1) {
        if (iswarping())
            rm->spr_ownedtags.draw(ix - PWOFFSET - 8, iy - PHOFFSET - 8, {ownerColorOffsetX, 0, 48, 48}, static_cast<ClipEdge>(static_cast<short>(state) % 4), GetWarpPlane());
        else
            rm->spr_ownedtags.draw(ix - PWOFFSET - 8, iy - PHOFFSET - 8, {ownerColorOffsetX, 0, 48, 48});
    }

    //Don't allow cape, tail, wings to be used with shoe
    if (!kuriboshoe.is_on()) {
        if (powerup == 3)
            cape.draw(*this);
        else if (powerup == 8)
            wings.draw(*this);
        //This has to come last otherwise chickens with glide option won't be able to use cape or wings
        else if (powerup == 7 || (powerup == -1 && chicken == this && game_values.gamemodesettings.chicken.glide))
            tail.draw(*this);
    }

    //Draw the player sprite above the shoe
    short iPlayerKuriboOffsetY = 0;
    if (kuriboshoe.is_on())
        iPlayerKuriboOffsetY = 16;

    //Don't draw the player if he is frozen in a shoe
    if (!frozen || !kuriboshoe.is_on()) {
        if (iswarping())
            pScoreboardSprite->at(sprite_state).draw(ix - PWOFFSET, iy - PHOFFSET - iPlayerKuriboOffsetY, {iSrcOffsetX, 0, 32, 32}, static_cast<ClipEdge>(static_cast<short>(state) % 4), GetWarpPlane());
        else
            pScoreboardSprite->at(sprite_state).draw(ix - PWOFFSET, iy - PHOFFSET - iPlayerKuriboOffsetY, {iSrcOffsetX, 0, 32, 32});
    }

    //Draw Kuribo's Shoe
    kuriboshoe.draw(*this);

    //Draw the crown on the player
    draw_winnerCrown();

    if (state < PlayerState::Ready)
        return;

    if (frozen) {
        if (iswarping())
            rm->spr_iceblock.draw(ix - PWOFFSET, iy - PHOFFSET, {0, 0, 32, 32}, static_cast<ClipEdge>(static_cast<short>(state) % 4), GetWarpPlane());
        else
            rm->spr_iceblock.draw(ix - PWOFFSET, iy - PHOFFSET, {0, 0, 32, 32});
    }

    jail.draw(*this);
    suicidetimer.draw(*this);

    //Draw the Ring awards
    if (game_values.awardstyle == AwardStyle::Halo && killsinrow >= MINAWARDSNEEDED)
        awardeffects.drawRingAward(*this);

    //Draw the powerup ring when a powerup is being used
    draw_powerupRing();
}

void CPlayer::drawOutOfScreenIndicators()
{
    outofarena.draw(*this);
}

void CPlayer::updateswap()
{
    if (state != PlayerState::Ready)
        return;

    if (game_values.swapstyle == MysteryMushroomSwapStyle::Blink) {
        setXf(game_values.flags.swapplayersblink ? fOldSwapX : fNewSwapX);
        setYf(game_values.flags.swapplayersblink ? fOldSwapY : fNewSwapY);
    } else {
        setXf(((fNewSwapX - fOldSwapX) * game_values.flags.swapplayersposition) + fOldSwapX);
        setYf(((fNewSwapY - fOldSwapY) * game_values.flags.swapplayersposition) + fOldSwapY);
    }

    if (carriedItem)
        carriedItem->MoveToOwner();
}

// The per-tick part of drawswap(), which used to run while drawing
void CPlayer::updateSwapAnimation()
{
    if (state == PlayerState::Ready && game_values.swapstyle != MysteryMushroomSwapStyle::Blink)
        updateSprite();

    updateTail();
}

void CPlayer::drawswap()
{
    if (state != PlayerState::Ready) {
        draw();
        return;
    }

    draw();

    if (carriedItem)
        carriedItem->draw();
}

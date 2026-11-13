#include "player.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "map.h"
#include "movingplatform.h"
#include "net.h"
#include "ObjectContainer.h"
#include "objectgame.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/ShyGuyTag.h"
#include "gamemodes/Star.h"
#include "gamemodes/Tag.h"
#include "IO_Block.h"
#include "objects/carriable/CO_Bomb.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "objects/moving/MO_Boomerang.h"
#include "objects/moving/MO_Explosion.h"
#include "objects/moving/MO_Fireball.h"
#include "objects/moving/MO_Hammer.h"
#include "objects/moving/MO_IceBlast.h"
#include "objects/moving/MO_Podobo.h"

#include <cassert>
#include <cmath>

void CPlayer::updateFrozenStatus(int keymask)
{
    if (frozen) {
        if ((!(sprite_state & PGFX_FACING_LEFT_BIT) && (keymask & PlayerKeyMask::Left)) || ((sprite_state & PGFX_FACING_LEFT_BIT) && (keymask & PlayerKeyMask::Right)))
            frozentimer -= 5;

        if (--frozentimer <= 0) {
            frozentimer = 0;
            frozen = false;

            //Shield the player after becoming unfrozen to protect against being frozen again
            shield.turn_on();
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, ix + HALFPW - 16, iy + HALFPH - 16, 3, 8);
        }
    }
}

void CPlayer::accelerate(float direction)
{
    // direction is
    //  1 on moving right
    // -1 on moving left
    assert(direction == 1.0 || direction == -1.0);

    if (onice)
        velx += VELMOVINGADDICE * direction;
    else
        velx += VELMOVINGADD * direction;

    float maxVel = 0.0f;
    if (!frozen) {
        auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode);

        if ((game_values.flags.slowdownon != -1 && game_values.flags.slowdownon != teamID) || jail.isActive())
            maxVel = VELSLOWMOVING;
        else if (playerKeys->game_turbo.fDown)
            maxVel = VELTURBOMOVING + ((gmTag && gmTag->tagged() == this) ? TAGGEDBOOST : 0.0f);
        else
            maxVel = VELMOVING + ((gmTag && gmTag->tagged() == this) ? TAGGEDBOOST : 0.0f);
    }
    assert(maxVel >= 0.0);

    if ((direction == 1 && velx > maxVel) || (direction == -1 && velx < -maxVel))
        velx = maxVel * direction;

    if (!inair) {
        //Make player hop or stick to ground in Kuribo's shoe
        if (kuriboshoe.is_on()) {
            //This makes the shoe stick to the ground (for sticky shoes)
            if (kuriboshoe.getType() == STICKY) {
                velx = 0.0f;
            } else {
                //only allow the player to jump in the air from kuribo's shoe if we aren't bouncing on a note block
                if (superjumptimer <= 0) {
                    Jump(direction, 1.0f, true);

                    superjumptype = 3;
                    superjumptimer = 16;
                }
            }
        }
        // If the player suddently moved to the other direction, play skid sound
        else if (direction > 0.0f ? velx < 0.0f : velx > 0.0f)
            game_values.flags.playskidsound = true;

        //If rain candy is turned on
        if ((g_map->eyecandy[0] & 32 || g_map->eyecandy[1] & 32 || g_map->eyecandy[2] & 32) && fabs(velx) > VELMOVINGADD && ++rainsteptimer > 7) {
            rainsteptimer = 0;
            eyecandy[1].emplace<EC_SingleAnimation>(&rm->spr_frictionsmoke, ix, iy + PH - 14, 5, 3, 0, 16, 16, 16);
        }
    }
}

void CPlayer::accelerateRight()
{
    accelerate(1.0);
}

void CPlayer::accelerateLeft()
{
    accelerate(-1.0);
}

void CPlayer::decreaseVelocity()
{
    //Stop ground velocity when wearing the sticky shoe
    if (!inair && kuriboshoe.getType() == STICKY) {
        velx = 0.0f;
        return;
    }

    //Add air/ground friction
    if (velx > 0.0f) {
        if (inair)
            velx -= VELAIRFRICTION;
        else if (onice)
            velx -= VELICEFRICTION;
        else
            velx -= VELMOVINGFRICTION;

        if (velx < 0.0f)
            velx = 0.0f;
    } else if (velx < 0.0f) {
        if (inair)
            velx += VELAIRFRICTION;
        else if (onice)
            velx += VELICEFRICTION;
        else
            velx += VELMOVINGFRICTION;

        if (velx > 0.0f)
            velx = 0.0f;
    }
}

bool CPlayer::canSuperStomp() const {
    return inair && isready() && !superstomp.isInSuperStompState();
}

bool CPlayer::wantsToSuperStomp() const {
    return (playerKeys->game_down.fPressed && playerDevice == DEVICE_KEYBOARD_ID)
        || (playerKeys->game_jump.fPressed && playerKeys->game_down.fDown);
}

bool CPlayer::highJumped() const {
    return (superjumptype != 3 || superjumptimer <= 0);
}

bool CPlayer::isInvincible() const {
    return invincibility.is_on();
}

bool CPlayer::isShielded() const {
    return shield.is_on();
}

bool CPlayer::IsInvincibleOnBottom() const {
    return isInvincible() || isShielded() || kuriboshoe.is_on();
}

bool CPlayer::IsSuperStomping() const {
    return superstomp.isStomping();
}

void CPlayer::tryFallingThroughPlatform(short movement_direction)
{
    //only if on the ground and the jump key was released somewhen after it was pressed the last time
    bool fFellThrough = false;
    if (playerKeys->game_down.fDown) {
        //Check to see what the player is standing on
        fPrecalculatedY = fy + vely;
        short under_tile_y = (static_cast<short>(fPrecalculatedY) + PH) / TILESIZE;

        short left_tile_x = ix / TILESIZE;
        if (left_tile_x < 0)
            left_tile_x += MAPWIDTH;
        else if (left_tile_x >= MAPWIDTH)
            left_tile_x -= MAPWIDTH;

        short right_tile_x;
        if (rightX() >= App::screenWidth)
            right_tile_x = (rightX() - App::screenWidth) / TILESIZE;
        else
            right_tile_x = rightX() / TILESIZE;

        int lefttile = g_map->map(left_tile_x, under_tile_y);
        int righttile = g_map->map(right_tile_x, under_tile_y);

        if (((lefttile & tile_flag_solid_on_top) && (righttile & tile_flag_solid_on_top || righttile == tile_flag_nonsolid || righttile == tile_flag_gap)) ||
                ((righttile & tile_flag_solid_on_top) && (lefttile & tile_flag_solid_on_top || lefttile == tile_flag_nonsolid || lefttile == tile_flag_gap))) {
            fFellThrough = true;
        }

        if (!fFellThrough && platform) {
            fPrecalculatedY += platform->fOldVelY;
            platform->GetTileTypesFromPlayer(this, &lefttile, &righttile);

            if (((lefttile & tile_flag_solid_on_top) && (righttile & tile_flag_solid_on_top || righttile == tile_flag_nonsolid || righttile == tile_flag_gap)) ||
                    ((righttile & tile_flag_solid_on_top) && (lefttile & tile_flag_solid_on_top || lefttile == tile_flag_nonsolid || lefttile == tile_flag_gap))) {
                fFellThrough = true;
            }
        }
    }

    if (fFellThrough) {
        lockfall = true;
        fallthrough = true;
    } else {
        Jump(movement_direction, 1.0f, false);
        ifSoundOnPlay(rm->sfx_jump);
    }

    lockjump = true;
}

void CPlayer::trySuperJumping(short movement_direction)
{
    assert(superjumptype > 0);

    if (superjumptype == 3) { //Kuribo's Shoe Jump
        Jump(movement_direction, 1.0f, false);
        ifSoundOnPlay(rm->sfx_jump);
    }
    if (superjumptype == 2) {
        vely = -VELSUPERJUMP;
        inair = true;
        ifSoundOnPlay(rm->sfx_superspring);
    } else if (superjumptype == 1) {
        vely = -VELTURBOJUMP;
        inair = true;
        ifSoundOnPlay(rm->sfx_springjump);
    }

    superjumptimer = 0;
    lockjump = true;
}

void CPlayer::tryCapeDoubleJump(short movement_direction)
{
    assert(powerup == 3);
    if (kuriboshoe.is_on())
        return;

    if (extrajumps < game_values.featherjumps) {
        if (game_values.featherlimit == 0 || projectilelimit > 0) {
            if (extrajumps < game_values.featherjumps) {
                Jump(movement_direction, 0.8f, false);
                ifSoundOnPlay(rm->sfx_capejump);
                lockjump = true;
            }

            extrajumps++;
        }

        if (game_values.featherlimit > 0)
            DecreaseProjectileLimit();
    }
}

void CPlayer::tryStartFlying()
{
    assert(powerup == 8);
    if (kuriboshoe.is_on() || flying || extrajumps != 0)
        return;

    flying = true;
    game_values.flags.playflyingsound = true;

    lockjump = true;
    extrajumps++;
}

void CPlayer::tryShakingTail()
{
    if (kuriboshoe.is_on() || spin.isSpinInProgress())
        return;

    bool isGlidingChicken = false;
    if (auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode)) {
        isGlidingChicken = gmChicken->chicken() == this
            && game_values.gamemodesettings.chicken.glide
            && powerup == -1;
    }

    if (powerup == 7 || isGlidingChicken) {
        if (game_values.leaflimit == 0 || projectilelimit > 0)
            tail.shake(*this);
    }
}

void CPlayer::updateFlyingStatus()
{
    if (flying) {
        //If they player was frozen while flying, cause them to stop flying
        //or if they have been flying for a while, stop them flying
        if (frozen || ++flyingtimer > 200) {
            flyingtimer = 0;
            flying = false;

            if (game_values.pwingslimit > 0)
                DecreaseProjectileLimit();
        } else { //otherwise allow them to rise and swoop while flying
            if (playerKeys->game_down.fDown && vely < 1.0f) {
                vely += 1.0f;
            } else if (!playerKeys->game_down.fDown && vely > -1.0f) {
                vely -= 1.5f;
                inair = true;
            }
        }
    }
}

void CPlayer::enableFreeFall()
{
    lockjump = false; //the jump key is not pressed: the player may jump again if he is on the ground

    if (vely < -VELSTOPJUMP)
        vely = -VELSTOPJUMP;

    if (flying) {
        flying = false;
        flyingtimer = 0;

        if (game_values.pwingslimit > 0)
            DecreaseProjectileLimit();
    }
}

void CPlayer::move()
{
    //Call the AI if cpu controlled
    if (state == PlayerState::Ready) {
        if (pPlayerAI) {
            //Calculate movement every 4th frame (speed up optimization)
            if (game_values.cputurn == globalID) {
                cpu_think();

                if (playerKeys->game_jump.fDown || playerKeys->game_left.fDown || playerKeys->game_right.fDown)
                    suicidetimer.reset();
            }
            //Let go of the jump button so that we clear "lockjump" so we can jump again when we hit the ground if we want to
            if (inair && vely > 0 && (powerup != 3 || lockjump))
                playerKeys->game_jump.fDown = false;
        }
    }

    if (isInvincible())
        game_values.flags.playinvinciblesound = true;

    if (flying)
        game_values.flags.playflyingsound = true;

    int keymask =
        (playerKeys->game_jump.fPressed ? PlayerKeyMask::Jump : 0) |
        (playerKeys->game_down.fPressed ? PlayerKeyMask::Down : 0) |
        (playerKeys->game_left.fPressed ? PlayerKeyMask::Left : 0) |
        (playerKeys->game_right.fPressed ? PlayerKeyMask::Right : 0) |
        (playerKeys->game_turbo.fPressed ? PlayerKeyMask::Turbo : 0) |
        (playerKeys->game_powerup.fPressed ? PlayerKeyMask::Powerup : 0);

    int keymaskdown =
        (playerKeys->game_jump.fDown ? PlayerKeyMask::Jump : 0) |
        (playerKeys->game_down.fDown ? PlayerKeyMask::Down : 0) |
        (playerKeys->game_left.fDown ? PlayerKeyMask::Left : 0) |
        (playerKeys->game_right.fDown ? PlayerKeyMask::Right : 0) |
        (playerKeys->game_turbo.fDown ? PlayerKeyMask::Turbo : 0) |
        (playerKeys->game_powerup.fDown ? PlayerKeyMask::Powerup : 0);

    //If any key was pressed, reset the suicide timer
    if (keymask || (keymaskdown && (velx < -1.0f || velx > 1.0f)))
        suicidetimer.reset();


    secretcode.update(*this, keymask);
    kuriboshoe.update(*this, keymask);
    cardcollection.update(*this, keymask);
    tanookisuit.update(*this);
    superstomp.update_onGroundHit(*this);

    if (hammertimer > 0)
        hammertimer--;

    spin.update(*this);

    if (throw_star > 0)
        throw_star--;

    if (iSuicideCreditPlayerID >= 0) {
        if (--iSuicideCreditTimer <= 0) {
            iSuicideCreditTimer = 0;
            iSuicideCreditPlayerID = -1;
        }
    }

    iSrcOffsetX = 32 * getPlayerPalette();

    if (!isready()) {
        if (state == PlayerState::Waiting) {
            update_waitingForRespawn();
            return;
        }

        if (isspawning())
            update_respawning();
        else if (iswarping())
            warpstatus.update(*this);
    }
    else if (powerupused.has_value())
        update_usePowerup();

    invincibility.update(*this); // Animate invincibility

    updateFrozenStatus(keymask);

    //if player is warping or spawning don't pay attention to controls
    if (isready()) {
        //Super stomp
        superstomp.update(*this);

        //If player is shaking tail, slow decent
        tail.slowDescent(*this);

        shield.update();

        short movement_direction = 0;	//move left-right-no: -1.. left 0 no 1 ... right

        //Used for bouncing off of note blocks
        if (superjumptimer > 0)
            superjumptimer--;

        if (!frozen) {
            //Determine move direction pressed
            if (tanookisuit.notStatue() && !superstomp.isStomping()) {
                if (playerKeys->game_right.fDown)
                    movement_direction = 1;

                if (playerKeys->game_left.fDown)
                    movement_direction = -1;
            }

            //jump pressed?
            if (playerKeys->game_jump.fDown) {
                if (!lockjump && tanookisuit.notStatue() && !superstomp.isStomping()) {
                    if (!inair && superjumptimer == 0) {
                        tryFallingThroughPlatform(movement_direction);
                    } else if (superjumptimer > 0) {
                        trySuperJumping(movement_direction);
                    } else if (powerup == 3) {
                        tryCapeDoubleJump(movement_direction);
                    } else if (powerup == 8) { //Start pwings flight
                        tryStartFlying();
                    }
                    //This must come last or gliding chickens can't use powerups before this statement
                    else {
                        tryShakingTail();
                    }
                }
            } else
                enableFreeFall();

            if (playerKeys->game_down.fDown) {
                if (!lockfall && !inair && (playerDevice == DEVICE_KEYBOARD_ID || pPlayerAI)) {
                    lockfall = true;
                    fallthrough = true;
                }
            } else {
                lockfall = false;
            }

            //POWERUP RELEASE
            if (playerKeys->game_powerup.fDown)
                tryReleasingPowerup();

            fPressedAcceptItem = playerKeys->game_turbo.fPressed;

            //Projectiles
            if (playerKeys->game_turbo.fDown) {
                useSpecialPowerup();
            } else
                releaseCarriedItem();
        }

        updateFlyingStatus();

        if (movement_direction == 1)
            accelerateRight();
        else if (movement_direction == -1)
            accelerateLeft();
        else
            decreaseVelocity();

        if (!inair) {
            if (game_values.unlocksecret3part2[globalID] > 0)
                game_values.unlocksecret3part2[globalID]--;
        }

        fOldX = fx;
        fOldY = fy;

        if (game_values.windaffectsplayers) {
            oldvelx = velx;
            float windx = game_values.flags.gamewindx / (kuriboshoe.is_on() ? 3.0f : 1.5f);
            velx = CapSideVelocity(velx + windx);
        }

        collision_detection_map();

        //If the player died or entered a warp, don't reset his velocity
        if (game_values.windaffectsplayers && state == PlayerState::Ready)
            velx = oldvelx;
    }

    //Player can be killed by map so only do this code if he is still living
    if (isready()) {
        //Deal with terminal burnup velocity
        burnup.update(*this);
        if (isdead())
            return;

        //Kill the player if he is standing still for too long
        suicidetimer.update(*this);
        if (isdead())
            return;

        //Deal with out of arena timer
        outofarena.update(*this);
        if (isdead())
            return;

        //Deal with release from jail timer
        jail.update(*this);
    }

    updateSprite();
}

void CPlayer::Jump(short iMove, float jumpModifier, bool fKuriboBounce)
{
    if (fKuriboBounce)
        vely = -VELKURIBOBOUNCE;
    else if ((game_values.flags.slowdownon != -1 && game_values.flags.slowdownon != teamID) || jail.isActive())
        vely = -VELSLOWJUMP * jumpModifier;
    else if (fabs(velx) > VELMOVING && iMove != 0 && playerKeys->game_turbo.fDown)
        vely = -VELTURBOJUMP * jumpModifier;
    else
        vely = -VELJUMP * jumpModifier;

    inair = true;

    //Need to help the player off the platform otherwise it will collide with them again
    if (platform) {
        //if (platform->fVelY < 0.0f)
        //	setYf(fy + platform->fVelY);

        platform = nullptr;
    }

    //printf("Player Jumped!\n");
}

bool CPlayer::bouncejump()
{
    superstomp.reset();

    if (playerKeys->game_jump.fDown) {
        lockjump = true;
        vely = -VELJUMP;
        return true;
    } else {
        vely = -VELJUMP / 2;	//jump a little off of collision object
        return false;
    }
}

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

void CMap::movingPlatformCollision(CPlayer * player)
{
    //Collide player with normal moving platforms
    for (MovingPlatform* platform : platforms) {
        platform->collide(player);
        if (!player->isready())
            return;
    }

    //Collide player with temporary platforms (like falling donut blocks)
    for (MovingPlatform* platform : tempPlatforms) {
        platform->collide(player);
        if (!player->isready())
            return;
    }
}

void CPlayer::mapcolldet_handlePlatformVelocity(float &fPlatformVelX, float &fPlatformVelY)
{
    if (platform) {
        fPlatformVelX = platform->fVelX;
        setXf(fx + fPlatformVelX);
        flipsidesifneeded();

        fPlatformVelY = platform->fVelY;

        if (platform->fOldVelY < 0.0f)
            fy += platform->fOldVelY;

        fPrecalculatedY += platform->fOldVelY;
    }
}

bool CPlayer::mapcolldet_handleOutOfScreen()
{
    if (fPrecalculatedY + PH < 0.0f) {
        // on top outside of the screen
        setYf(fPrecalculatedY);
        vely = CapFallingVelocity(GRAVITATION + vely);

        if (!platform) {
            inair = true;
            onice = false;
            superjumptimer = 0;
            fallthrough = false;
        }

        return true;
    } else if (fPrecalculatedY + PH >= 480) {
        //on ground outside of the screen?
        setYi(-PH);
        fOldY = static_cast<float>(-PH - 1);
        fallthrough = false;
        onice = false;

        //Clear the platform if the player wrapped to the top of the screen

        if (platform) {
            vely = platform->fVelY;
            platform = nullptr;
        }

        return true;
    }

    return false;
}

void CPlayer::mapcolldet_moveHorizontally(short direction)
{
    assert(direction == 1 || direction == 3);
    short counter_direction = (direction == 1) ? 3 : 1;

    //Could be optimized with bit shift >> 5
    short ty = static_cast<short>(fy) / TILESIZE;
    short ty2 = (static_cast<short>(fy) + PH) / TILESIZE;
    short tx = -1;

    //printf("Before X - ix: %d\tiy: %d\toldx: %.2f\toldy: %.2f\tty: %d\tty2: %d\ttxl: %d\ttxr: %d\tfx: %.2f\tfy: %.2f\tvelx: %.2f\tvely: %.2f\n", ix, iy, fOldX, fOldY, ty, ty2, ix/TILESIZE, (ix+PW)/TILESIZE, fx, fy, velx, vely);

    bool isMoveKeyDown = false;
    if (direction == 1)
        isMoveKeyDown = playerKeys->game_left.fDown;
    else
        isMoveKeyDown = playerKeys->game_right.fDown;

    if (direction == 1) {
        //moving left
        tx = static_cast<short>(fx) / TILESIZE;
    }
    else {
        //moving right
        if (fx + PW >= App::screenWidth) {
            tx = static_cast<short>(fx + PW - App::screenWidth) / TILESIZE;
            fOldX -= App::screenWidth;
        } else
            tx = (static_cast<short>(fx) + PW) / TILESIZE;
    }

    //Just in case tx out of bounds and flipsidesifneeded wasn't called
    if (tx < 0)
        tx += 20;
    else if (tx > 19)
        tx -= 20;

    IO_Block * topblock = g_map->block(tx, ty);
    IO_Block * bottomblock = g_map->block(tx, ty2);

    int toptile = g_map->map(tx, ty);
    int bottomtile = g_map->map(tx, ty2);

    bool deathTileBehind;
    bool superDeathTileBehind;
    if (direction == 1) {
        deathTileBehind = ((toptile & tile_flag_death_on_right) && (bottomtile & tile_flag_death_on_right)) ||
                          ((toptile & tile_flag_death_on_right) && !(bottomtile & tile_flag_solid)) ||
                          (!(toptile & tile_flag_solid) && (bottomtile & tile_flag_death_on_right));

        superDeathTileBehind = ((toptile & tile_flag_super_or_player_death_right) && (bottomtile & tile_flag_super_or_player_death_right)) ||
                               ((toptile & tile_flag_super_or_player_death_right) && !(bottomtile & tile_flag_solid)) ||
                               (!(toptile & tile_flag_solid) && (bottomtile & tile_flag_super_or_player_death_right));
    }
    else {
        deathTileBehind = ((toptile & tile_flag_death_on_left) && (bottomtile & tile_flag_death_on_left)) ||
                          ((toptile & tile_flag_death_on_left) && !(bottomtile & tile_flag_solid)) ||
                          (!(toptile & tile_flag_solid) && (bottomtile & tile_flag_death_on_left));

        superDeathTileBehind = ((toptile & tile_flag_super_or_player_death_left) && (bottomtile & tile_flag_super_or_player_death_left)) ||
                               ((toptile & tile_flag_super_or_player_death_left) && !(bottomtile & tile_flag_solid)) ||
                               (!(toptile & tile_flag_solid) && (bottomtile & tile_flag_super_or_player_death_left));
    }

    bool fTopBlockSolid = topblock && !topblock->isTransparent() && !topblock->isHidden();
    bool fBottomBlockSolid = bottomblock && !bottomblock->isTransparent() && !bottomblock->isHidden();

    //first check to see if player hit a warp
    if (isMoveKeyDown && !frozen && g_map->checkforwarp(tx, ty, ty2, direction)) {
        if (direction == 1)
            setXf(static_cast<float>((tx << 5) + TILESIZE) + 0.2f); // move to the edge of the tile
        else
            setXf(static_cast<float>((tx << 5) - PW) - 0.2f);       // move to the edge of the tile (tile on the right -> mind the player width)

        warpstatus.enterWarp(*this, g_map->warp(tx, ty2));

        if (iy - PHOFFSET < (ty << 5))
            setYi((ty << 5) + PHOFFSET);
        else if (iy + PH > (ty2 << 5) + TILESIZE - 3)
            setYi((ty2 << 5) + TILESIZE - PH - 3);

        return;
    } else if (fTopBlockSolid || fBottomBlockSolid) {
        if (iHorizontalPlatformCollision == direction) {
            KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);
            return;
        }

        if (netplay.active && netplay.theHostIsMe)
            netplay.client.local_gamehost.prepareMapCollisionEvent(*this);

        bool collisionresult = true;

        if (fTopBlockSolid) { // collide with top block
            collisionresult &= topblock->collide(this, counter_direction,
                netplay.active ? netplay.theHostIsMe || netplay.allowMapCollisionEvent : true);
            flipsidesifneeded();
        }

        if (fBottomBlockSolid) { // then bottom
            collisionresult &= bottomblock->collide(this, counter_direction,
                netplay.active ? netplay.theHostIsMe || netplay.allowMapCollisionEvent : true);
            flipsidesifneeded();
        }

        if (netplay.active && netplay.theHostIsMe && collisionresult)
            netplay.client.local_gamehost.sendMapCollisionEvent();
    } else if (superDeathTileBehind || (deathTileBehind && !isInvincible() && !isShielded() && !shyguy)) {
        if (PlayerKillType::NonKill != KillPlayerMapHazard(superDeathTileBehind, KillStyle::Environment, false))
            return;
    }
    //collision on the side.
    else if ((toptile & tile_flag_solid) || (bottomtile & tile_flag_solid)) { //collide with solid, ice, and death and all sides death
        if (iHorizontalPlatformCollision == direction) {
            KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);
            return;
        }

        if (direction == 1)
            setXf(static_cast<float>((tx << 5) + TILESIZE) + 0.2f); // move to the edge of the tile
        else
            setXf(static_cast<float>((tx << 5) - PW) - 0.2f);       // move to the edge of the tile (tile on the right -> mind the player width)

        fOldX = fx;

        if (fabs(velx) > 0.0f)
            velx = 0.0f;

        if (fabs(oldvelx) > 0.0f)
            oldvelx = 0.0f;

        flipsidesifneeded();
    }
}

void CPlayer::mapcolldet_moveUpward(short txl, short txc, short txr,
    short alignedBlockX, short unAlignedBlockX, float unAlignedBlockFX)
{
    // moving up
    fallthrough = false;

    short ty = static_cast<short>(fPrecalculatedY) / TILESIZE;

    IO_Block * leftblock = g_map->block(txl, ty);
    IO_Block * centerblock = g_map->block(txc, ty);
    IO_Block * rightblock = g_map->block(txr, ty);

    if (playerKeys->game_jump.fDown && !frozen && g_map->checkforwarp(alignedBlockX, unAlignedBlockX, ty, 2)) {
        setYf(static_cast<float>((ty << 5) + TILESIZE) + 0.2f);
        warpstatus.enterWarp(*this, g_map->warp(unAlignedBlockX, ty));

        if (ix - PWOFFSET < (txl << 5) + 1)
            setXi((txl << 5) + PHOFFSET + 1);
        else if (ix + PW + PWOFFSET > (txr << 5) + TILESIZE)
            setXi((txr << 5) + TILESIZE - PW - PWOFFSET);

        return;
    }

    if (centerblock && !centerblock->isTransparent()) {
        if (netplay.active && netplay.theHostIsMe)
            netplay.client.local_gamehost.prepareMapCollisionEvent(*this);

        if (!centerblock->collide(this, 0,
            netplay.active ? netplay.theHostIsMe || netplay.allowMapCollisionEvent : true)) {
            if (iVerticalPlatformCollision == 2 && !centerblock->isHidden())
                KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

            if (netplay.active && netplay.theHostIsMe)
                netplay.client.local_gamehost.sendMapCollisionEvent();

            return;
        }
    }

    //Player hit a solid, ice or death on top
    //or if the player is invincible and hits death or death on bottom

    //There is a known issue where where if a death on bottom tile and a super death on bottom tile
    //are next to each other and the player hits from below and aligns with the super death on bottom
    //the player will then shift over and fully hit the super death on bottom and die even if shielded
    //or invincible.

    int alignedTileType = g_map->map(alignedBlockX, ty);
    if ((alignedTileType & tile_flag_solid) && !(alignedTileType & tile_flag_super_or_player_death_bottom) &&
            (!(alignedTileType & tile_flag_death_on_bottom) || isInvincible() || isShielded() || shyguy)) {
        setYf(static_cast<float>((ty << 5) + TILESIZE) + 0.2f);
        fOldY = fy - 1.0f;

        if (vely < 0.0f)
            vely = -vely * BOUNCESTRENGTH;

        if (iVerticalPlatformCollision == 2)
            KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

        return;
    }

    if (leftblock && !leftblock->isTransparent()) { //then left
        bool useBehavior = alignedBlockX == txl || rightblock == nullptr || rightblock->isTransparent() || rightblock->isHidden();

        if (netplay.active && netplay.theHostIsMe)
            netplay.client.local_gamehost.prepareMapCollisionEvent(*this);

        if (!leftblock->collide(this, 0,
            netplay.active ? (netplay.theHostIsMe || netplay.allowMapCollisionEvent) && useBehavior : useBehavior)) {
            if (iVerticalPlatformCollision == 2 && !leftblock->isHidden())
                KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

            if (netplay.active && netplay.theHostIsMe)
                netplay.client.local_gamehost.sendMapCollisionEvent();

            return;
        }
    }

    if (rightblock && !rightblock->isTransparent()) { //then right
        bool useBehavior = alignedBlockX == txr || leftblock == nullptr || leftblock->isTransparent() || leftblock->isHidden();

        if (netplay.active && netplay.theHostIsMe)
            netplay.client.local_gamehost.prepareMapCollisionEvent(*this);

        if (!rightblock->collide(this, 0,
            netplay.active ? (netplay.theHostIsMe || netplay.allowMapCollisionEvent) && useBehavior : useBehavior)) {
            if (iVerticalPlatformCollision == 2 && !rightblock->isHidden())
                KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

            if (netplay.active && netplay.theHostIsMe)
                netplay.client.local_gamehost.sendMapCollisionEvent();

            return;
        }
    }

    //Player squeezed around the block, ice or death on top
    //or if the player is invincible and hits death or death on bottom
    int unalignedTileType = g_map->map(unAlignedBlockX, ty);
    if ((unalignedTileType & tile_flag_solid) && !(unalignedTileType & tile_flag_super_or_player_death_bottom) &&
            (!(unalignedTileType & tile_flag_death_on_bottom) || isInvincible() || isShielded() || shyguy)) {
        setXf(unAlignedBlockFX);
        fOldX = fx;

        setYf(fPrecalculatedY);
        vely += GRAVITATION;
    } else if ((alignedTileType & tile_flag_player_or_death_on_bottom) || (unalignedTileType & tile_flag_player_or_death_on_bottom)) {
        bool fRespawnPlayer = ((alignedTileType & tile_flag_super_or_player_death_bottom) && (unalignedTileType & tile_flag_super_or_player_death_bottom)) ||
                              ((alignedTileType & tile_flag_super_or_player_death_bottom) &&  !(unalignedTileType & tile_flag_solid)) ||
                              ((alignedTileType & tile_flag_solid) && !(unalignedTileType & tile_flag_super_or_player_death_bottom));

        if (PlayerKillType::NonKill != KillPlayerMapHazard(fRespawnPlayer, KillStyle::Environment, false))
            return;
    } else {
        setYf(fPrecalculatedY);
        vely += GRAVITATION;
    }

    if (!platform) {
        inair = true;
        onice = false;
    }
}

void CPlayer::mapcolldet_moveDownward(short txl, short txc, short txr,
    short alignedBlockX, short unAlignedBlockX, float unAlignedBlockFX)
{
    // moving down / on ground
    short ty = (static_cast<short>(fPrecalculatedY) + PH) / TILESIZE;

    if (playerKeys->game_down.fDown && !frozen && g_map->checkforwarp(txl, txr, ty, 0)) {
        setYf(static_cast<float>((ty << 5) - PH) - 0.2f);
        warpstatus.enterWarp(*this, g_map->warp(txr,ty));

        fallthrough = false;
        platform = nullptr;

        if (ix - PWOFFSET < (txl << 5) + 1)
            setXi((txl << 5) + PHOFFSET + 1);
        else if (ix + PW + PWOFFSET > (txr << 5) + TILESIZE)
            setXi((txr << 5) + TILESIZE - PW - PWOFFSET);

        return;
    }

    IO_Block * leftblock = g_map->block(txl, ty);
    IO_Block * rightblock = g_map->block(txr, ty);

    bool fLeftBlockSolid = leftblock && !leftblock->isTransparent() && !leftblock->isHidden();
    bool fRightBlockSolid = rightblock && !rightblock->isTransparent() && !rightblock->isHidden();

    if (fLeftBlockSolid || fRightBlockSolid) {
        bool collisionresult = true;
        if (netplay.active && netplay.theHostIsMe)
            netplay.client.local_gamehost.prepareMapCollisionEvent(*this);

        if (fLeftBlockSolid) { //collide with left block
            bool useBehavior = alignedBlockX == txl || rightblock == nullptr || rightblock->isTransparent() || rightblock->isHidden();
            collisionresult &= leftblock->collide(this, 2,
                netplay.active ? (netplay.theHostIsMe || netplay.allowMapCollisionEvent) && useBehavior : useBehavior);

            if (netplay.active && netplay.theHostIsMe && collisionresult)
                netplay.client.local_gamehost.sendMapCollisionEvent();

            //If player was bumped and killed then return
            if (state != PlayerState::Ready)
                return;
        }

        if (fRightBlockSolid) { //then right
            bool useBehavior = alignedBlockX == txr || leftblock == nullptr || leftblock->isTransparent() || leftblock->isHidden();
            collisionresult &= rightblock->collide(this, 2,
                netplay.active ? (netplay.theHostIsMe || netplay.allowMapCollisionEvent) && useBehavior : useBehavior);

            if (netplay.active && netplay.theHostIsMe && collisionresult)
                netplay.client.local_gamehost.sendMapCollisionEvent();

            //If player was bumped and killed then return
            if (state != PlayerState::Ready)
                return;
        }

        if (!collisionresult) {
            platform = nullptr;
            onice = false;

            if (iVerticalPlatformCollision == 0)
                KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

            return;
        }
    }

    int lefttile = g_map->map(txl, ty);
    int righttile = g_map->map(txr, ty);

    bool fGapSupport = (velx >= VELTURBOMOVING || velx <= -VELTURBOMOVING) && (lefttile == tile_flag_gap || righttile == tile_flag_gap);

    bool fSolidTileUnderPlayer = (lefttile & tile_flag_solid)  || (righttile & tile_flag_solid);

    if ((lefttile & tile_flag_solid_on_top || righttile & tile_flag_solid_on_top || fGapSupport) && fOldY + PH <= (ty << 5)) {
        //on ground
        //Deal with player down jumping through solid on top tiles

        if (!platform)
            onice = false;

        if (fallthrough && !fSolidTileUnderPlayer) {
            setYf(static_cast<float>((ty << 5) - PH) + 0.2f);

            if (!platform) {
                inair = true;
            }
        } else {
            //we were above the tile in the previous frame
            setYf(static_cast<float>((ty << 5) - PH) - 0.2f);
            vely = GRAVITATION;

            if (!platform) {
                int alignedtile = g_map->map(alignedBlockX, ty);

                if (alignedtile & tile_flag_ice || ((alignedtile == tile_flag_nonsolid || alignedtile == tile_flag_gap) && g_map->map(unAlignedBlockX, ty) & tile_flag_ice))
                    onice = true;
                else
                    onice = false;

                inair = false;
                extrajumps = 0;
                killsinrowinair = 0;
            }
        }

        fOldY = fy - GRAVITATION;

        if (!platform)
            fallthrough = false;

        platform = nullptr;

        if (iVerticalPlatformCollision == 0)
            KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);

        return;
    }

    bool fDeathTileUnderPlayer = ((lefttile & tile_flag_death_on_top) && (righttile & tile_flag_death_on_top)) ||
                                 ((lefttile & tile_flag_death_on_top) && !(righttile & tile_flag_solid)) ||
                                 (!(lefttile & tile_flag_solid) && (righttile & tile_flag_death_on_top));

    bool fSuperDeathTileUnderPlayer = ((lefttile & tile_flag_super_or_player_death_top) && (righttile & tile_flag_super_or_player_death_top)) ||
                                      ((lefttile & tile_flag_super_or_player_death_top) && !(righttile & tile_flag_solid)) ||
                                      (!(lefttile & tile_flag_solid) && (righttile & tile_flag_super_or_player_death_top));

    if (fSolidTileUnderPlayer && !fSuperDeathTileUnderPlayer &&
            (!fDeathTileUnderPlayer || IsInvincibleOnBottom() || shyguy) ) {
        //on ground

        setYf(static_cast<float>((ty << 5) - PH) - 0.2f);
        vely = GRAVITATION;				//1 so we test against the ground again int the next frame (0 would test against the ground in the next+1 frame)

        if (!platform) {
            int alignedtile = g_map->map(alignedBlockX, ty);

            if (alignedtile & tile_flag_ice || ((alignedtile == tile_flag_nonsolid || alignedtile == tile_flag_gap) && g_map->map(unAlignedBlockX, ty) & tile_flag_ice))
                onice = true;
            else
                onice = false;

            inair = false;
            extrajumps = 0;
            killsinrowinair = 0;
        }

        platform = nullptr;

        if (iVerticalPlatformCollision == 0) {
            KillPlayerMapHazard(true, KillStyle::Environment, true, iPlatformCollisionPlayerId);
            return;
        }
    } else if (fDeathTileUnderPlayer || fSuperDeathTileUnderPlayer) {
        if (PlayerKillType::NonKill != KillPlayerMapHazard(fSuperDeathTileUnderPlayer, KillStyle::Environment, false))
            return;
    } else {
        //falling (in air)
        setYf(fPrecalculatedY);
        vely = CapFallingVelocity(GRAVITATION + vely);

        if (!platform) {
            //If we're not hopping with kuribo's shoe, then zero out our super jump timer
            if (superjumptype != 3)
                superjumptimer = 0;

            inair = true;
        }
    }
}

void CPlayer::collision_detection_map()
{
    setXf(fx + velx);
    flipsidesifneeded();

    fPrecalculatedY = fy + vely;  //Fixes weird float rounding error.  Must be computed here before casting to int.  Otherwise, this will miss the bottom collision, but then hit the side collision and the player can slide out of 1x1 spaces.

    float fPlatformVelX = 0.0f;
    float fPlatformVelY = 0.0f;

    float fTempY = fy;

    mapcolldet_handlePlatformVelocity(fPlatformVelX, fPlatformVelY);

    iHorizontalPlatformCollision = -1;
    iVerticalPlatformCollision = -1;
    iPlatformCollisionPlayerId = -1;

    g_map->movingPlatformCollision(this);

    if (state != PlayerState::Ready)
        return;

    fy = fTempY;

    if (mapcolldet_handleOutOfScreen())
        return;

    //-----------------------------------------------------------------
    //  x axis (--)
    //-----------------------------------------------------------------
    if (fy + PH >= 0.0f) {
        if (velx + fPlatformVelX > 0.01f || iHorizontalPlatformCollision == 3)
            mapcolldet_moveHorizontally(3);
        else if (velx + fPlatformVelX < -0.01f || iHorizontalPlatformCollision == 1)
            mapcolldet_moveHorizontally(1);

        if (isdead())
            return;
    }

    //-----------------------------------------------------------------
    //  then y axis (|)
    //-----------------------------------------------------------------

    short txl = 0, txr = 0, txc = 0;
    short iPlayerL = ix, iPlayerC = ix + HALFPW, iPlayerR = ix + PW;

    if (iPlayerL < 0)
        iPlayerL += App::screenWidth;
    else if (iPlayerL >= App::screenWidth)
        iPlayerL -= App::screenWidth;

    if (iPlayerC >= App::screenWidth)
        iPlayerC -= App::screenWidth;

    if (iPlayerR >= App::screenWidth)
        iPlayerR -= App::screenWidth;

    txl = iPlayerL / TILESIZE;
    txc = iPlayerC / TILESIZE;
    txr = iPlayerR / TILESIZE;

    //What block is the player aligned to (this will be the block that has the action on it)
    short alignedBlockX = 0;
    short unAlignedBlockX = 0;
    float unAlignedBlockFX = 0;

    //printf("Before Y - ix: %d\tiy: %d\toldx: %.2f\toldy: %d\tty: %.2f\tty2: %d\ttxl: %d\ttxr: %d\tfx: %.2f\tfy: %.2f\tvelx: %.2f\tvely: %.2f\n", ix, iy, fOldX, fOldY, (int)(fPrecalculatedY) / TILESIZE, ((int)(fPrecalculatedY) + PH) / TILESIZE, txl, txr, fx, fy, velx, vely);

    short overlaptxl = (txl << 5) + TILESIZE + 1;

    if (ix + HALFPW < overlaptxl) {
        alignedBlockX = txl;
        unAlignedBlockX = txr;
        unAlignedBlockFX = static_cast<float>((txr << 5) - PW) - 0.2f;
    } else {
        alignedBlockX = txr;
        unAlignedBlockX = txl;
        unAlignedBlockFX = static_cast<float>((txl << 5) + TILESIZE) + 0.2f;
    }

    float fMovingUp = vely;
    if (platform)
        fMovingUp = vely + fPlatformVelY - GRAVITATION;

    if (fMovingUp < -0.01f) {
        //moving up
        mapcolldet_moveUpward(txl, txc, txr, alignedBlockX, unAlignedBlockX, unAlignedBlockFX);
    } else {
        //moving down / on ground
        mapcolldet_moveDownward(txl, txc, txr, alignedBlockX, unAlignedBlockX, unAlignedBlockFX);
    }

    if (!platform) {
        fallthrough = false;

        if (inair)
            onice = false;
    }

    //printf("After Y - ix: %d\tiy: %d\toldx: %.2f\toldy: %.2f\tty: %d\tty2: %d\ttxl: %d\ttxr: %d\tfx: %.2f\tfy: %.2f\tvelx: %.2f\tvely: %.2f\n\n", ix, iy, fOldX, fOldY, ty, ty, txl, txr, fx, fy, velx, vely);
}

//iPlayerIdCredit is passed in if this platform was triggered by another player and crushed this player (e.g. donut block)
PlayerKillType CPlayer::KillPlayerMapHazard(bool fForce, KillStyle style, bool fKillCarriedItem, short iPlayerIdCredit)
{
    if (iPlayerIdCredit >= 0 || iSuicideCreditPlayerID >= 0) {
        return PlayerKilledPlayer(iPlayerIdCredit >= 0 ? iPlayerIdCredit : iSuicideCreditPlayerID, this, PlayerDeathStyle::Jump, KillStyle::Push, fForce, fKillCarriedItem);
    } else {
        DeathAwards();

        PlayerKillType iKillType = game_values.gamemode->playerkilledself(*this, style);
        if (PlayerKillType::Normal == iKillType || (PlayerKillType::NonKill == iKillType && fForce))
            die(PlayerDeathStyle::Jump, false, fKillCarriedItem);

        ifSoundOnPlay(rm->sfx_deathsound);

        return iKillType;
    }
}

void CPlayer::collidesWith(CPlayer* other)
{
    collisions.handle_p2p(this, other);
}

bool CPlayer::collidesWith(CObject* object)
{
    return collisions.handle_p2o(this, object);
}

void CPlayer::flipsidesifneeded()
{
    //Use ix here to avoid rounding issues (can crash if tile_x_right evals to over the right side of screen)
    if (ix < 0 || fx < 0.0f) {
        //This avoids rounding errors
        setXf(fx + 640);
        fOldX += 640;
        //printf("Flipped Left\n");
    } else if (ix >= 640 || fx >= 640) {
        setXf(fx - 640);
        fOldX -= 640;
        //printf("Flipped Right\n");
    }
}

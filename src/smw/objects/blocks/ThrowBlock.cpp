#include "ThrowBlock.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "map.h"
#include "ObjectContainer.h"
#include "player.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "objects/carriable/CO_ThrowBlock.h"


B_ThrowBlock::B_ThrowBlock(gfxSprite *nspr, Vec2s pos, short iNumSpr, short aniSpeed, ThrowBlockType type) :
    IO_Block(nspr, pos)
{
    iw = 32;
    ih = 32;
    iNumSprites = iNumSpr;
    animationSpeed = aniSpeed;
    animationTimer = 0;
    drawFrame = 0;
    animationWidth = static_cast<short>(nspr->getWidth());
    iType = type;
}

void B_ThrowBlock::draw()
{
    spr->draw(ix, iy, {drawFrame, static_cast<short>(iType) * 32, iw, ih});
}

void B_ThrowBlock::update()
{
    if (++animationTimer >= animationSpeed) {
        animationTimer = 0;

        drawFrame += iw;
        if (drawFrame >= animationWidth) {
            drawFrame = 0;
        }
    }
}

bool B_ThrowBlock::hittop(CPlayer * player, bool useBehavior)
{
    if (useBehavior) {
        player->setYf(static_cast<float>(iposy - PH) - 0.2f);
        player->fOldY = player->fy;
        player->inair = false;
        player->fallthrough = false;
        player->killsinrowinair = 0;
        player->extrajumps = 0;
        player->vely = GRAVITATION;

        if (player->PressedAcceptItemKey() && player->IsAcceptingItem()) {
            GiveBlockToPlayer(player);
            return true;
        } else {
            return false;
        }
    }

    return false;
}

bool B_ThrowBlock::hitright(CPlayer * player, bool useBehavior)
{
    if (useBehavior) {
        player->setXf(static_cast<float>(iposx + iw) + 0.2f);
        player->fOldX = player->fx;

        if (player->velx < 0.0f)
            player->velx = 0.0f;

        if (player->oldvelx < 0.0f)
            player->oldvelx = 0.0f;

        if (player->IsAcceptingItem()) {
            GiveBlockToPlayer(player);
            return true;
        } else {
            return false;
        }
    }

    return true;
}

bool B_ThrowBlock::hitleft(CPlayer * player, bool useBehavior)
{
    if (useBehavior) {
        player->setXf(static_cast<float>(iposx - PW) - 0.2f);
        player->fOldX = player->fx;

        if (player->velx > 0.0f)
            player->velx = 0.0f;

        if (player->oldvelx > 0.0f)
            player->oldvelx = 0.0f;

        if (player->IsAcceptingItem()) {
            GiveBlockToPlayer(player);
            return true;
        } else {
            return false;
        }
    }

    return true;
}

void B_ThrowBlock::GiveBlockToPlayer(CPlayer * player)
{
    auto block = std::make_unique<CO_ThrowBlock>(&rm->spr_blueblock, Vec2s(ix, iy), iType);
    // If the container is full, the block could not be stored after the player took it
    if (!g_sim.objectcontainer[1].isFull() && player->AcceptItem(block.get())) {
        dead = true;
        g_map->blockdata[col][row] = nullptr;
        g_map->UpdateTileGap(col, row);

        block->owner = player;
        block->iPlayerID = player->globalID;
        g_sim.objectcontainer[1].add(std::move(block));
    }
}

void B_ThrowBlock::triggerBehavior()
{
    dead = true;
    g_map->blockdata[col][row] = nullptr;
    g_map->UpdateTileGap(col, row);

    const int srcY = static_cast<int>(iType) * 16;
    eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokenblueblock, ix, iy, -1.5f, -7.0f, 6, 2, 0, srcY, 16, 16);
    eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokenblueblock, ix + 16, iy, 1.5f, -7.0f, 6, 2, 0, srcY, 16, 16);
    eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokenblueblock, ix, iy + 16, -1.5f, -4.0f, 6, 2, 0, srcY, 16, 16);
    eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokenblueblock, ix + 16, iy + 16, 1.5f, -4.0f, 6, 2, 0, srcY, 16, 16);

    ifSoundOnPlay(rm->sfx_breakblock);
}

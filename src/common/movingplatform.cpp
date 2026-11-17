#include "movingplatform.h"

#include "gfx.h"
#include "IO_Block.h"
#include "Game.h"
#include "GameValues.h"
#include "map.h"
#include "player.h"
#include "PlayerKillTypes.h"
#include "ResourceManager.h"
#include "TilesetManager.h"
#include "objects/moving/MovingObject.h"

enum CollisionStyle {collision_none, collision_normal, collision_overlap_left, collision_overlap_right};

namespace {
// Where a body touching the platform across the screen's edge is, relative to it: a screen width
// to the left or to the right (CollisionStyle, from coldec_player / coldec_object)
float wrapShift(short coldec)
{
    if (coldec == collision_overlap_left)
        return -App::screenWidth;
    if (coldec == collision_overlap_right)
        return App::screenWidth;
    return 0;
}
} // namespace

//------------------------------------------------------------------------------
// Moving Platform
//------------------------------------------------------------------------------

MovingPlatform::MovingPlatform(const std::vector<TilesetTile>& tiledata, const std::vector<TileType>& tiletypes, short w, short h, short drawlayer, MovingPlatformPath * path, bool fPreview)
{
    fDead = false;
    iPlayerId = -1;

    short iTileSize = TILESIZE;
    DrawSize drawsize = DrawSize::Ingame;

    if (fPreview) {
        iTileSize = PREVIEWTILESIZE;
        drawsize = DrawSize::Preview;
    }

    iTileData.assign(tiledata.begin(), tiledata.end());
    iTileType.assign(tiletypes.begin(), tiletypes.end());
    assert(iTileData.size() == iTileType.size());
    assert(iTileData.size() == static_cast<size_t>(w * h));

    iTileWidth = w;
    iTileHeight = h;

    iDrawLayer = drawlayer;

    iWidth = w * iTileSize;
    iHeight = h * iTileSize;

    iHalfWidth = iWidth >> 1;
    iHalfHeight = iHeight >> 1;

    pPath = path;
    pPath->SetPlatform(this);

    ResetPath();

    for (short iSurface = 0; iSurface < 2; iSurface++) {
        sprites[iSurface] = gfxSprite::blank(w * iTileSize, h * iTileSize);
        sprites[iSurface].setWrap();

        if (!SDL_SetSurfaceColorKey(sprites[iSurface].getSurface(), true, SDL_MapSurfaceRGB(sprites[iSurface].getSurface(), 255, 0, 255)))
            printf("\n ERROR: Couldn't set ColorKey for moving platform: %s\n", SDL_GetError());

        SDL_FillSurfaceRect(sprites[iSurface].getSurface(), nullptr, SDL_MapSurfaceRGB(sprites[iSurface].getSurface(), 255, 0, 255));
    }

    //Run through all tiles in the platform, detect unknown and blank tiles,
    //and draw all static tiles to the platform surface
    for (short iSurface = 0; iSurface < 2; iSurface++) {
        for (short iCol = 0; iCol < iTileWidth; iCol++) {
            for (short iRow = 0; iRow < iTileHeight; iRow++) {
                const TilesetTile& tile = iTileData[iCol * iTileHeight + iRow];

                if (tile.iID == TILESETNONE)
                    continue;

                if (tile.iID >= 0) {
                    g_tilesetmanager->Draw(sprites[iSurface].getSurface(), tile.iID, drawsize, tile.iCol, tile.iRow, iCol, iRow);
                } else if (tile.iID == TILESETANIMATED) {
                    const SDL_Rect& srcRect = CTilesetManager::rect(drawsize, tile.iCol * 4, tile.iRow);
                    const SDL_Rect& dstRect = CTilesetManager::rect(drawsize, iCol, iRow);
                    rm->spr_tileanimation[static_cast<size_t>(drawsize)].draw(srcRect, sprites[iSurface].getSurface(), dstRect);
                } else if (tile.iID == TILESETUNKNOWN) {
                    const SDL_Rect& srcRect = CTilesetManager::rect(drawsize, 0, 0);
                    const SDL_Rect& dstRect = CTilesetManager::rect(drawsize, iCol, iRow);
                    rm->spr_unknowntile[static_cast<size_t>(drawsize)].draw(srcRect, sprites[iSurface].getSurface(), dstRect);
                }
            }
        }
    }

    rSrcRect.x = 0;
    rSrcRect.y = 0;
    rSrcRect.w = w * iTileSize;
    rSrcRect.h = h * iTileSize;

    //The tiles drawn: runs of tiles in each row, rows with the same runs merged
    std::vector<SDL_Rect> rowRuns;
    for (short iRow = 0; iRow < iTileHeight; iRow++) {
        rowRuns.clear();
        for (short iCol = 0; iCol < iTileWidth; iCol++) {
            if (iTileData[iCol * iTileHeight + iRow].iID == TILESETNONE)
                continue;
            if (!rowRuns.empty() && rowRuns.back().x + rowRuns.back().w == iCol * iTileSize)
                rowRuns.back().w += iTileSize;
            else
                rowRuns.push_back({iCol * iTileSize, iRow * iTileSize, iTileSize, iTileSize});
        }
        //(the same columns as the rows just above: those get taller)
        size_t above = 0;
        while (above < drawParts.size() && drawParts[drawParts.size() - 1 - above].y + drawParts[drawParts.size() - 1 - above].h == iRow * iTileSize)
            above++;
        bool same = above == rowRuns.size() && above > 0;
        for (size_t i = 0; same && i < above; i++) {
            const SDL_Rect& a = drawParts[drawParts.size() - above + i];
            same = a.x == rowRuns[i].x && a.w == rowRuns[i].w;
        }
        if (same) {
            for (size_t i = 0; i < above; i++)
                drawParts[drawParts.size() - above + i].h += iTileSize;
        } else {
            drawParts.insert(drawParts.end(), rowRuns.begin(), rowRuns.end());
        }
    }

    fVelX = pPath->velocity0().x;
    fVelY = pPath->velocity0().y;

    fOldVelX = fVelX;
    fOldVelY = fVelY;
}

MovingPlatform::MovingPlatform(const gfxSprite& sprite, short w, short h, short drawlayer, MovingPlatformPath * path)
{
    fDead = false;
    iPlayerId = -1;

    TilesetTile tile;
    tile.iID = TILESETNONE;
    tile.iCol = 0;
    tile.iRow = 0;
    iTileData.assign(static_cast<size_t>(w * h), tile);
    iTileType.assign(static_cast<size_t>(w * h), TileType::Solid);

    iTileWidth = w;
    iTileHeight = h;

    iDrawLayer = drawlayer;

    iWidth = w * TILESIZE;
    iHeight = h * TILESIZE;

    iHalfWidth = iWidth >> 1;
    iHalfHeight = iHeight >> 1;

    customSprite = &sprite;

    pPath = path;
    pPath->SetPlatform(this);

    ResetPath();

    rSrcRect = {0, 0, iWidth, iHeight};

    fVelX = pPath->velocity0().x;
    fVelY = pPath->velocity0().y;

    fOldVelX = fVelX;
    fOldVelY = fVelY;
}

MovingPlatform::~MovingPlatform()
{
    delete pPath;
}

const TilesetTile& MovingPlatform::tileAt(size_t col, size_t row) const
{
    return iTileData[col * iTileHeight + row];
}

TileType MovingPlatform::tileTypeAt(size_t col, size_t row) const
{
    return iTileType[col * iTileHeight + row];
}

void MovingPlatform::draw()
{
    const SDL_Rect rDstRect = {ix - iHalfWidth + x_shake, iy - iHalfHeight + y_shake, iWidth, iHeight};

    if (customSprite) {
        customSprite->draw(rSrcRect, blitdest, rDstRect);

        //Wrap around the screen edges, like the prerendered tiles do
        SDL_Rect rWrapRect = rDstRect;
        if (rDstRect.x + iWidth >= App::screenWidth)
            rWrapRect.x -= App::screenWidth;
        else if (rDstRect.x < 0)
            rWrapRect.x += App::screenWidth;

        if (rWrapRect.x != rDstRect.x)
            customSprite->draw(rSrcRect, blitdest, rWrapRect);
    } else if (iWidth >= App::screenWidth) {
        sprites[1 - g_iCurrentDrawIndex].draw(rSrcRect, blitdest, rDstRect);
    } else {
        //Each part where the whole would put it, and its copy wrapped around the screen's edge
        //when that shows (as the sprite's own wrapping does for the whole)
        SDL_Surface* surface = sprites[1 - g_iCurrentDrawIndex].getSurface();
        for (const SDL_Rect& part : drawParts) {
            const int x = rDstRect.x + part.x;
            const int y = rDstRect.y + part.y;
            SDL_Rect src = part;
            SDL_Rect dst = {x, y, part.w, part.h};
            SDL_BlitSurface(surface, &src, blitdest, &dst);
            if (x + part.w > App::screenWidth || x < 0) {
                src = part;
                dst = {x < 0 ? x + App::screenWidth : x - App::screenWidth, y, part.w, part.h};
                SDL_BlitSurface(surface, &src, blitdest, &dst);
            }
        }
    }
}

//Draw path for map preview
void MovingPlatform::draw(short iOffsetX, short iOffsetY)
{
    gfx_drawpreview(sprites[0],
        ix - iHalfWidth + iOffsetX, iy - iHalfHeight + iOffsetY,
        0, 0, iWidth, iHeight,
        {iOffsetX, iOffsetY, App::screenWidth/2, App::screenHeight/2},
        true);
}

void MovingPlatform::update()
{
    fOldX = pPath->currentPos0().x;
    fOldY = pPath->currentPos0().y;

    fOldVelX = pPath->velocity0().x;
    fOldVelY = pPath->velocity0().y;

    //Path will affect new fVelX and fVelY to move the platform to it's next location
    pPath->Move(0);

    //Will move the path "shadow" to the next location (for spawn collision detection)
    pPath->Move(1);

    fVelX = pPath->velocity0().x;
    fVelY = pPath->velocity0().y;

    setXf(pPath->currentPos0().x);
    setYf(pPath->currentPos0().y);
}

void MovingPlatform::ResetPath()
{
    pPath->Reset();

    setXf(pPath->currentPos0().x);
    setYf(pPath->currentPos0().y);

    fOldX = fx;
    fOldY = fy;

    //Need to advance spawn platform here if spawn time is greater than 0
    //Advance it the spawn time amount here

}

void MovingPlatform::collide(CPlayer * player)
{
    short coldec = coldec_player(player);
    if (coldec == collision_none) {
        if (player->platform == this)
            player->platform = nullptr;

        return;
    }

    if (fDead)
        return;

    float fColVelX = player->velx - fOldVelX;
    float fColVelY = player->vely - fOldVelY;

    if (player->platform) {
        fColVelY += player->platform->fOldVelY - GRAVITATION;

        if (player->velx < -0.6f || player->velx > 0.6f)
            fColVelX += player->platform->fOldVelX;
    } else if (!player->inair) {
        fColVelY -= GRAVITATION;  //Ignore the gravity if the player is on the ground
    }

    float fRelativeY1;
    float fRelativeY2;

    if (player->platform != this) {
        fRelativeY1 = player->fy - fOldY + iHalfHeight;
        fRelativeY2 = player->fy + PH - fOldY + iHalfHeight;
    } else {
        fRelativeY1 = player->fy - fy + iHalfHeight;
        fRelativeY2 = player->fy + PH - fy + iHalfHeight;
    }

    if (fColVelX > 0.01f || player->iHorizontalPlatformCollision == 3) {
        const float fRelativeX = player->fx + PW - fx + iHalfWidth + wrapShift(coldec);

        if (fRelativeX >= 0.0f && fRelativeX < iWidth) {
            short tx = static_cast<short>(fRelativeX) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeY1 >= 0.0f && fRelativeY1 < iHeight)
                t1 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY1) / TILESIZE));

            if (fRelativeY2 >= 0.0f && fRelativeY2 < iHeight)
                t2 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY2) / TILESIZE));

            if ((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) {
                bool fDeathTileToLeft = pairIsDeadly(t1, t2, tile_flag_death_on_left);

                bool fSuperDeathTileToLeft = pairIsDeadly(t1, t2, tile_flag_super_or_player_death_left);

                if (player->iHorizontalPlatformCollision == 3) {
                    player->KillPlayerMapHazard(true, KillStyle::Environment, true, iPlayerId);
                    return;
                } else if (fSuperDeathTileToLeft || (fDeathTileToLeft && !player->isInvincible() && !player->isShielded() && !player->shyguy)) {
                    if (PlayerKillType::NonKill != player->KillPlayerMapHazard(fSuperDeathTileToLeft, KillStyle::Environment, false, iPlayerId))
                        return;
                } else {
                    player->setXf(static_cast<float>((tx << TILESIZE_SHIFT) - PW) - 0.2f + fx - iHalfWidth);
                    player->flipsidesifneeded();

                    //test to see if we put the object on top of a background tile
                    check_map_collision_right(player);

                    if (fOldVelX < 0.0f)
                        player->fOldX = fx + 10.0f;  //Need to push out the old enough to collide correctly with flip blocks
                    else
                        player->fOldX = fx - 10.0f;  //Need to push out the old enough to collide correctly with flip blocks

                    player->iHorizontalPlatformCollision = 1;
                    player->iPlatformCollisionPlayerId = iPlayerId;

                    if (player->velx > 0.0f)
                        player->velx = 0.0f;
                }
            }
        }
    } else if (fColVelX < -0.01f || player->iHorizontalPlatformCollision == 1) {
        //float fRelativeX = player->fx - fx + iHalfWidth;

        const float fRelativeX = player->fx - fx + iHalfWidth + wrapShift(coldec);

        if (fRelativeX >= 0.0f && fRelativeX < iWidth) {
            short tx = static_cast<short>(fRelativeX) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeY1 >= 0 && fRelativeY1 < iHeight)
                t1 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY1) / TILESIZE));

            if (fRelativeY2 >= 0 && fRelativeY2 < iHeight)
                t2 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY2) / TILESIZE));

            if ((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) {
                bool fDeathTileToRight = pairIsDeadly(t1, t2, tile_flag_death_on_right);

                bool fSuperDeathTileToRight = pairIsDeadly(t1, t2, tile_flag_super_or_player_death_right);

                if (player->iHorizontalPlatformCollision == 1) {
                    player->KillPlayerMapHazard(true, KillStyle::Environment, true, iPlayerId);
                    return;
                } else if (fSuperDeathTileToRight || (fDeathTileToRight && !player->isInvincible() && !player->isShielded() && !player->shyguy)) {
                    if (PlayerKillType::NonKill != player->KillPlayerMapHazard(fSuperDeathTileToRight, KillStyle::Environment, false, iPlayerId))
                        return;
                } else {
                    player->setXf(static_cast<float>((tx << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
                    player->flipsidesifneeded();

                    //test to see if we put the object on top of a background tile
                    check_map_collision_left(player);

                    //WARNING, WARNING! HACKY!!
                    if (fOldVelX < 0.0f)
                        player->fOldX = fx + 10.0f;  //Need to push out the old enough to collide correctly with flip blocks
                    else
                        player->fOldX = fx - 10.0f;  //Need to push out the old enough to collide correctly with flip blocks

                    player->iHorizontalPlatformCollision = 3;
                    player->iPlatformCollisionPlayerId = iPlayerId;

                    if (player->velx < 0.0f)
                        player->velx = 0.0f;
                }
            }
        }
    }

    //Need to ask for collision detection again because the player might be on the other side of the screen now with wrapping
    coldec = coldec_player(player);
    if (coldec == collision_none) {
        if (player->platform == this)
            player->platform = nullptr;

        return;
    }
    const float fRelativeX1 = player->fx - fx + iHalfWidth + wrapShift(coldec);
    const float fRelativeX2 = player->fx + PW - fx + iHalfWidth + wrapShift(coldec);

    if (fColVelY < 0.0f) {
        float fRelativeY;

        if (player->inair || player->platform == this)
            fRelativeY = player->fPrecalculatedY - fy + iHalfHeight;
        else
            fRelativeY = player->fy - fy + iHalfHeight;

        if (fRelativeY >= 0.0f && fRelativeY < iHeight) {
            short ty = static_cast<short>(fRelativeY) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeX1 >= 0.0f && fRelativeX1 < iWidth)
                t1 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX1) / TILESIZE, ty));

            if (fRelativeX2 >= 0.0f && fRelativeX2 < iWidth)
                t2 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX2) / TILESIZE, ty));

            bool fSolidTileOverPlayer = (t1 & tile_flag_solid) || (t2 & tile_flag_solid);

            bool fDeathTileOverPlayer = pairIsDeadly(t1, t2, tile_flag_death_on_bottom);

            bool fSuperDeathTileOverPlayer = pairIsDeadly(t1, t2, tile_flag_super_or_player_death_bottom);

            if (fSolidTileOverPlayer && !fSuperDeathTileOverPlayer &&
                    (!fDeathTileOverPlayer || player->isInvincible() || player->isShielded() || player->shyguy)) {
                if (player->iVerticalPlatformCollision == 2) {
                    player->KillPlayerMapHazard(true, KillStyle::Environment, true, iPlayerId);
                    return;
                } else {
                    player->fPrecalculatedY = (ty << TILESIZE_SHIFT) + TILESIZE + 0.2f + fy - iHalfHeight;
                    player->fOldY = player->fPrecalculatedY - fVelY - GRAVITATION;

                    if (player->vely < 0.0f && fVelY > 0.0f)
                        player->fPrecalculatedY += fVelY;

                    if (player->vely < 0.0f)
                        player->vely = CapFallingVelocity(-player->vely * BOUNCESTRENGTH + fVelY);

                    player->iVerticalPlatformCollision = 0;
                    player->iPlatformCollisionPlayerId = iPlayerId;
                }

                return;
            } else if ((t1 & tile_flag_player_or_death_on_bottom) || (t2 & tile_flag_player_or_death_on_bottom)) {
                if (PlayerKillType::NonKill != player->KillPlayerMapHazard(true, KillStyle::Environment, false, iPlayerId))
                    return;
            }
        }
    } else {
        float fRelativeY = player->fPrecalculatedY + PH - fy + iHalfHeight;

        if (fRelativeY >= 0.0f && fRelativeY < iHeight) {
            short ty = static_cast<short>(fRelativeY) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeX1 >= 0.0f && fRelativeX1 < iWidth)
                t1 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX1) / TILESIZE, ty));

            if (fRelativeX2 >= 0.0f && fRelativeX2 < iWidth)
                t2 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX2) / TILESIZE, ty));

            bool fSolidTileUnderPlayer = (t1 & tile_flag_solid) || (t2 & tile_flag_solid);

            if ((t1 & tile_flag_solid_on_top || t2 & tile_flag_solid_on_top) && player->fOldY + PH <= (ty << TILESIZE_SHIFT) + fOldY - iHalfHeight) {
                //Deal with player down jumping through solid on top tiles
                if (player->fallthrough && !fSolidTileUnderPlayer) {
                    player->fPrecalculatedY = (ty << TILESIZE_SHIFT) - PH + (fVelY > 0.0f ? fVelY : 0.0f) + 0.2f + fy - iHalfHeight;
                    player->inair = true;
                    player->platform = nullptr;
                } else {
                    if (player->iVerticalPlatformCollision == 0) {
                        player->KillPlayerMapHazard(true, KillStyle::Environment, true, iPlayerId);
                        return;
                    } else {
                        player->platform = this;
                        player->iVerticalPlatformCollision = 2;
                        player->iPlatformCollisionPlayerId = iPlayerId;

                        player->fPrecalculatedY = (ty << TILESIZE_SHIFT) - PH - 0.2f + fy - iHalfHeight;
                        player->vely = GRAVITATION;
                        player->inair = false;
                        player->killsinrowinair = 0;
                        player->extrajumps = 0;
                    }
                }

                player->fOldY = player->fPrecalculatedY - fVelY;
                player->fallthrough = false;
                player->onice = false;

                return;
            }

            bool fDeathTileUnderPlayer = pairIsDeadly(t1, t2, tile_flag_death_on_top);

            bool fSuperDeathTileUnderPlayer = pairIsDeadly(t1, t2, tile_flag_super_or_player_death_top);

            if (fSolidTileUnderPlayer && !fSuperDeathTileUnderPlayer &&
                    (!fDeathTileUnderPlayer || player->isInvincible() || player->isShielded() || player->kuriboshoe.is_on() || player->shyguy)) {
                //on ground

                if (player->iVerticalPlatformCollision == 0) {
                    player->KillPlayerMapHazard(true, KillStyle::Environment, true, iPlayerId);
                    return;
                } else {
                    player->platform = this;
                    player->iVerticalPlatformCollision = 2;
                    player->iPlatformCollisionPlayerId = iPlayerId;

                    player->fPrecalculatedY = (ty << TILESIZE_SHIFT) - PH - 0.2f + fy - iHalfHeight;
                    player->fOldY = player->fPrecalculatedY - fVelY;

                    player->vely = GRAVITATION;
                    player->inair = false;
                    player->killsinrowinair = 0;
                    player->extrajumps = 0;

                    if ((t1 & tile_flag_ice && ((t2 & tile_flag_ice) || t2 == tile_flag_nonsolid || t2 == tile_flag_gap)) ||
                            (t2 & tile_flag_ice && ((t1 & tile_flag_ice) || t1 == tile_flag_nonsolid || t1 == tile_flag_gap)))
                        player->onice = true;
                    else
                        player->onice = false;

                    player->fallthrough = false;

                    return;
                }
            } else if (fDeathTileUnderPlayer || fSuperDeathTileUnderPlayer) {
                if (PlayerKillType::NonKill != player->KillPlayerMapHazard(fSuperDeathTileUnderPlayer, KillStyle::Environment, false, iPlayerId))
                    return;
            } else {
                if (player->platform == this)
                    player->platform = nullptr;
            }
        }
    }
}

void MovingPlatform::check_map_collision_right(CPlayer * player)
{
    if (player->fy + PH < 0.0f)
        return;

    short iTestBackgroundX;
    if (player->fx + PW >= App::screenWidth)
        iTestBackgroundX = (static_cast<short>(player->fx) + PW - App::screenWidth) / TILESIZE;
    else
        iTestBackgroundX = (static_cast<short>(player->fx) + PW) / TILESIZE;

    short iTestBackgroundY = (static_cast<short>(player->fy) + PH) / TILESIZE;

    IO_Block * topblock = g_map->block(iTestBackgroundX, iTestBackgroundY);

    if ((topblock && !topblock->isTransparent() && !topblock->isHidden()) ||
            (g_map->map(iTestBackgroundX, iTestBackgroundY) & tile_flag_solid)) {
        player->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) - PW) - 0.2f);
        player->flipsidesifneeded();
        return;
    }

    if (player->fy < 0.0f)
        return;

    short iTestBackgroundY2 = static_cast<short>(player->fy) / TILESIZE;

    IO_Block * bottomblock = g_map->block(iTestBackgroundX, iTestBackgroundY2);

    if ((bottomblock && !bottomblock->isTransparent() && !bottomblock->isHidden()) ||
            (g_map->map(iTestBackgroundX, iTestBackgroundY2) & tile_flag_solid)) {
        player->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) - PW) - 0.2f);
        player->flipsidesifneeded();
        return;
    }
}

void MovingPlatform::check_map_collision_left(CPlayer * player)
{
    if (player->fy + PH < 0.0f)
        return;

    short iTestBackgroundX = static_cast<short>(player->fx) / TILESIZE;
    short iTestBackgroundY = (static_cast<short>(player->fy) + PH) / TILESIZE;

    IO_Block * topblock = g_map->block(iTestBackgroundX, iTestBackgroundY);

    if ((topblock && !topblock->isTransparent() && !topblock->isHidden()) ||
            (g_map->map(iTestBackgroundX, iTestBackgroundY) & tile_flag_solid)) {
        player->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) + TILESIZE) + 0.2f);
        player->flipsidesifneeded();
        return;
    }

    if (player->fy < 0.0f)
        return;

    short iTestBackgroundY2 = static_cast<short>(player->fy) / TILESIZE;

    IO_Block * bottomblock = g_map->block(iTestBackgroundX, iTestBackgroundY2);

    if ((bottomblock && !bottomblock->isTransparent() && !bottomblock->isHidden()) ||
            (g_map->map(iTestBackgroundX, iTestBackgroundY2) & tile_flag_solid)) {
        player->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) + TILESIZE) + 0.2f);
        player->flipsidesifneeded();
        return;
    }
}

bool MovingPlatform::collision_detection_check_sides(IO_MovingObject * object)
{
    short coldec = coldec_object(object);
    if (coldec == collision_none)
        return false;

    if (fDead)
        return false;

    //Figure out where the corners of this object are touching
    Uint8 iCase = 0;

    //Check the left side
    const float fRelativeXLeft = object->fx - fx + iHalfWidth + wrapShift(coldec);

    //Check the right side
    float fRelativeXRight = fRelativeXLeft + object->collisionWidth;

    //Top and Bottom
    float fRelativeYTop = object->fy - fy + iHalfHeight;
    float fRelativeYBottom = fRelativeYTop + object->collisionHeight;

    short txLeft = -1;
    short txRight = -1;

    if (fRelativeXLeft >= 0.0f && fRelativeXLeft < iWidth)
        txLeft = static_cast<short>(fRelativeXLeft) / TILESIZE;

    if (fRelativeXRight >= 0.0f && fRelativeXRight < iWidth)
        txRight = static_cast<short>(fRelativeXRight) / TILESIZE;

    short tyTop = -1;
    short tyBottom = -1;

    if (fRelativeYTop >= 0.0f && fRelativeYTop < iHeight)
        tyTop = static_cast<short>(fRelativeYTop) / TILESIZE;

    if (fRelativeYBottom >= 0.0f && fRelativeYBottom < iHeight)
        tyBottom = static_cast<short>(fRelativeYBottom) / TILESIZE;

    if (txLeft >= 0) {
        int t1 = tile_flag_nonsolid;
        int t2 = tile_flag_nonsolid;

        if (tyTop >= 0)
            t1 = tileToFlags(tileTypeAt(txLeft, tyTop));

        if (tyBottom >= 0)
            t2 = tileToFlags(tileTypeAt(txLeft, tyBottom));

        if (t1 & tile_flag_solid)
            iCase |= 0x01;

        if (t2 & tile_flag_solid)
            iCase |= 0x04;
    }

    if (txRight >= 0) {
        int t1 = tile_flag_nonsolid;
        int t2 = tile_flag_nonsolid;

        if (tyTop >= 0)
            t1 = tileToFlags(tileTypeAt(txRight, tyTop));

        if (tyBottom >= 0)
            t2 = tileToFlags(tileTypeAt(txRight, tyBottom));

        if (t1 & tile_flag_solid)
            iCase |= 0x02;

        if (t2 & tile_flag_solid)
            iCase |= 0x08;
    }

    //Then determine which way is the best way to move this object out of the solid areas
    switch (iCase) {
        //Do nothing
        //[ ][ ]
        //[ ][ ]
    case 0:
        return false;
        break;

        //[X][ ]
        //[ ][ ]
    case 1: {
        if (object->ix + (object->collisionWidth >> 1) > (txLeft << TILESIZE_SHIFT) + TILESIZE) {
            object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        }

        break;
    }

    //[ ][X]
    //[ ][ ]
    case 2: {
        if (object->ix + (object->collisionWidth >> 1) < (txRight << TILESIZE_SHIFT)) {
            object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        }

        break;
    }

    //[X][X]
    //[ ][ ]
    case 3: {
        object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        break;
    }

    //[ ][ ]
    //[X][ ]
    case 4: {
        if (object->ix + (object->collisionWidth >> 1) > (txLeft << TILESIZE_SHIFT) + TILESIZE) {
            object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
        }

        break;
    }

    //[X][ ]
    //[X][ ]
    case 5: {
        object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //[ ][X]
    //[X][ ]
    case 6: {
        if (object->ix + (object->collisionWidth >> 1) > (txLeft << TILESIZE_SHIFT) + TILESIZE) {
            object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
            object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
            object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        }

        break;
    }

    //[X][X]
    //[X][ ]
    case 7: {
        object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //[ ][ ]
    //[ ][X]
    case 8: {
        if (object->ix + (object->collisionWidth >> 1) < (txRight << TILESIZE_SHIFT)) {
            object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
        }

        break;
    }

    //[X][ ]
    //[ ][X]
    case 9: {
        if (object->ix + (object->collisionWidth >> 1) > (txLeft << TILESIZE_SHIFT) + TILESIZE) {
            object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
            object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        } else {
            object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
            object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
            object->flipsidesifneeded();
        }

        break;
    }

    //[ ][X]
    //[ ][X]
    case 10: {
        object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //[X][X]
    //[ ][X]
    case 11: {
        object->setYf(static_cast<float>((tyTop << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //[ ][ ]
    //[X][X]
    case 12: {
        object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
        break;
    }

    //[X][ ]
    //[X][X]
    case 13: {
        object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
        object->setXf(static_cast<float>((txLeft << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //[ ][X]
    //[X][X]
    case 14: {
        object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) - object->collisionHeight) - 0.2f + fy - iHalfHeight);
        object->setXf(static_cast<float>((txRight << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
        object->flipsidesifneeded();
        break;
    }

    //If object is completely inside a block, default to moving it down
    //[X][X]
    //[X][X]
    case 15: {
        object->setYf(static_cast<float>((tyBottom << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fy - iHalfHeight);
        break;
    }

    default:
        break;
    }

    return true;
}

//Reports back if we had to overlap the edges to make the collision
short MovingPlatform::coldec_player(CPlayer * player)
{
    //Special cases to deal with players overlapping the right and left sides of the screen
    if (player->fx + PW < fx - iHalfWidth) {
        if (player->fx + App::screenWidth >= fx + iHalfWidth || player->fx + PW + App::screenWidth < fx - iHalfWidth || player->fPrecalculatedY >= fy + iHalfHeight || player->fPrecalculatedY + PH < fy - iHalfHeight)
            return collision_none;
        else
            return collision_overlap_right;
    } else if (fx + iHalfWidth < player->fx) {
        if (player->fx >= fx + iHalfWidth + App::screenWidth || player->fx + PW < fx - iHalfWidth + App::screenWidth || player->fPrecalculatedY >= fy + iHalfHeight || player->fPrecalculatedY + PH < fy - iHalfHeight)
            return collision_none;
        else
            return collision_overlap_left;
    } else { //Normal case where no overlap
        if (player->fx >= fx + iHalfWidth || fx - iHalfWidth > player->fx + PW || player->fPrecalculatedY >= fy + iHalfHeight || fy - iHalfHeight > player->fPrecalculatedY + PH) {
            return collision_none;
        } else
            return collision_normal;
    }
}

void MovingPlatform::GetTileTypesFromPlayer(CPlayer * player, int * lefttile, int * righttile)
{
    *lefttile = tile_flag_nonsolid;
    *righttile = tile_flag_nonsolid;

    float fRelativeY = player->fPrecalculatedY + PH - fy + iHalfHeight;

    if (fRelativeY < 0.0f || fRelativeY >= iHeight)
        return;

    short ty = static_cast<short>(fRelativeY) / TILESIZE;

    float fRelativeX1 = player->fx - fx + iHalfWidth;

    float fRelativeX2;
    if (player->fx + PW > App::screenWidth)
        fRelativeX2 = player->fx + PW - App::screenWidth - fx + iHalfWidth;
    else
        fRelativeX2 = player->fx + PW - fx + iHalfWidth;

    if (fRelativeX1 >= 0.0f && fRelativeX1 < iWidth)
        *lefttile = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX1) / TILESIZE, ty));

    if (fRelativeX2 >= 0.0f && fRelativeX2 < iWidth)
        *righttile = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX2) / TILESIZE, ty));
}

int MovingPlatform::GetTileTypeFromCoord(short x, short y)
{
    float fRelativeY = y - fy + iHalfHeight;

    if (fRelativeY < 0.0f || fRelativeY >= iHeight)
        return tile_flag_nonsolid;

    if (x >= App::screenWidth)
        x -= App::screenWidth;
    else if (x < 0)
        x += App::screenWidth;

    float fRelativeX = x - fx + iHalfWidth;

    if (fRelativeX < 0.0f || fRelativeX >= iWidth)
        return tile_flag_nonsolid;

    return tileToFlags(tileTypeAt(static_cast<short>(fRelativeX) / TILESIZE, static_cast<short>(fRelativeY) / TILESIZE));
}

void MovingPlatform::collide(IO_MovingObject * object)
{
    float fColVelX = object->velx - fOldVelX;

    short coldec = coldec_object(object);
    if (coldec == collision_none) {
        if (object->platform == this)
            object->platform = nullptr;

        return;
    }

    if (fDead)
        return;

    float fColVelY = object->vely - fOldVelY;

    if (object->platform) {
        fColVelY += object->platform->fOldVelY - GRAVITATION;

        if (object->velx < -0.6f || object->velx > 0.6f)
            fColVelX += object->platform->fOldVelX;
    } else if (!object->inair) {
        fColVelY -= GRAVITATION;  //Ignore the gravity if the object is on the ground
    }

    float fRelativeY1;
    float fRelativeY2;

    if (object->platform != this) {
        fRelativeY1 = object->fy - fOldY + iHalfHeight;
        fRelativeY2 = fRelativeY1 + object->collisionHeight;
    } else {
        fRelativeY1 = object->fy - fy + iHalfHeight;
        fRelativeY2 = fRelativeY1 + object->collisionHeight;
    }

    if (fColVelX > 0.01f || object->iHorizontalPlatformCollision == 3) {
        const float fRelativeX = object->fx + object->collisionWidth - fx + iHalfWidth + wrapShift(coldec);

        if (fRelativeX >= 0.0f && fRelativeX < iWidth) {
            short tx = static_cast<short>(fRelativeX) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeY1 >= 0.0f && fRelativeY1 < iHeight)
                t1 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY1) / TILESIZE));

            if (fRelativeY2 >= 0.0f && fRelativeY2 < iHeight)
                t2 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY2) / TILESIZE));

            if ((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) {
                if (object->iHorizontalPlatformCollision == 3) {
                    object->KillObjectMapHazard();
                    return;
                } else {
                    object->setXf(static_cast<float>((tx << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f + fx - iHalfWidth);
                    object->flipsidesifneeded();

                    //test to see if we put the object on top of a background tile
                    short iTestBackgroundY = static_cast<short>(object->fy) / TILESIZE;
                    short iTestBackgroundY2 = (static_cast<short>(object->fy) + object->collisionHeight) / TILESIZE;

                    short iTestBackgroundX;
                    if (object->fx + object->collisionWidth >= App::screenWidth)
                        iTestBackgroundX = (static_cast<short>(object->fx) + object->collisionWidth - App::screenWidth) / TILESIZE;
                    else
                        iTestBackgroundX = (static_cast<short>(object->fx) + object->collisionWidth) / TILESIZE;

                    IO_Block * topblock = g_map->block(iTestBackgroundX, iTestBackgroundY);
                    IO_Block * bottomblock = g_map->block(iTestBackgroundX, iTestBackgroundY2);

                    if ((topblock && !topblock->isTransparent() && !topblock->isHidden()) ||
                            (bottomblock && !bottomblock->isTransparent() && !bottomblock->isHidden()) ||
                            (g_map->map(iTestBackgroundX, iTestBackgroundY) & tile_flag_solid) ||
                            (g_map->map(iTestBackgroundX, iTestBackgroundY2) & tile_flag_solid)) {
                        object->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) - object->collisionWidth) - 0.2f);
                        object->flipsidesifneeded();
                    }

                    if (fOldVelX < 0.0f)
                        object->fOldX = fx + 10.0f;  //Need to push out the old enough to collide correctly with flip blocks
                    else
                        object->fOldX = fx - 10.0f;  //Need to push out the old enough to collide correctly with flip blocks

                    object->iHorizontalPlatformCollision = 1;

                    if (object->velx > 0.0f)
                        object->velx = -object->velx;

                    removeifprojectile(object, true, true);

                    object->SideBounce(true);
                }
            }
        }
    } else if (fColVelX < -0.01f || object->iHorizontalPlatformCollision == 1) {
        const float fRelativeX = object->fx - fx + iHalfWidth + wrapShift(coldec);

        if (fRelativeX >= 0.0f && fRelativeX < iWidth) {
            short tx = static_cast<short>(fRelativeX) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeY1 >= 0 && fRelativeY1 < iHeight)
                t1 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY1) / TILESIZE));

            if (fRelativeY2 >= 0 && fRelativeY2 < iHeight)
                t2 = tileToFlags(tileTypeAt(tx, static_cast<short>(fRelativeY2) / TILESIZE));

            if ((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) {
                if (object->iHorizontalPlatformCollision == 1) {
                    object->KillObjectMapHazard();
                    return;
                } else {
                    object->setXf(static_cast<float>((tx << TILESIZE_SHIFT) + TILESIZE) + 0.2f + fx - iHalfWidth);
                    object->flipsidesifneeded();

                    //test to see if we put the object on top of a background tile
                    short iTestBackgroundY = static_cast<short>(object->fy) / TILESIZE;
                    short iTestBackgroundY2 = (static_cast<short>(object->fy) + object->collisionHeight) / TILESIZE;
                    short iTestBackgroundX = static_cast<short>(object->fx) / TILESIZE;

                    IO_Block * topblock = g_map->block(iTestBackgroundX, iTestBackgroundY);
                    IO_Block * bottomblock = g_map->block(iTestBackgroundX, iTestBackgroundY2);

                    if ((topblock && !topblock->isTransparent() && !topblock->isHidden()) ||
                            (bottomblock && !bottomblock->isTransparent() && !bottomblock->isHidden()) ||
                            (g_map->map(iTestBackgroundX, iTestBackgroundY) & tile_flag_solid) ||
                            (g_map->map(iTestBackgroundX, iTestBackgroundY2) & tile_flag_solid)) {
                        object->setXf(static_cast<float>((iTestBackgroundX << TILESIZE_SHIFT) + TILESIZE) + 0.2f);
                        object->flipsidesifneeded();
                    }

                    if (fOldVelX < 0.0f)
                        object->fOldX = fx + 10.0f;  //Need to push out the old enough to collide correctly with flip blocks
                    else
                        object->fOldX = fx - 10.0f;  //Need to push out the old enough to collide correctly with flip blocks

                    object->iHorizontalPlatformCollision = 3;

                    if (object->velx < 0.0f)
                        object->velx = -object->velx;

                    removeifprojectile(object, true, true);

                    object->SideBounce(false);
                }
            }
        }
    }

    coldec = coldec_object(object);
    if (coldec == collision_none) {
        if (object->platform == this)
            object->platform = nullptr;

        return;
    }
    const float fRelativeX1 = object->fx - fx + iHalfWidth + wrapShift(coldec);
    const float fRelativeX2 = object->fx + object->collisionWidth - fx + iHalfWidth + wrapShift(coldec);

    if (fColVelY < 0.0f) {
        float fRelativeY;

        if (object->inair || object->platform == this)
            fRelativeY = object->fPrecalculatedY - fy + iHalfHeight;
        else
            fRelativeY = object->fy - fy + iHalfHeight;

        if (fRelativeY >= 0.0f && fRelativeY < iHeight) {
            short ty = static_cast<short>(fRelativeY) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeX1 >= 0.0f && fRelativeX1 < iWidth)
                t1 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX1) / TILESIZE, ty));

            if (fRelativeX2 >= 0.0f && fRelativeX2 < iWidth)
                t2 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX2) / TILESIZE, ty));

            if ((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) {
                if (object->iVerticalPlatformCollision == 2) {
                    object->KillObjectMapHazard();
                    return;
                } else {
                    object->fPrecalculatedY = (ty << TILESIZE_SHIFT) + TILESIZE + 0.2f + fy - iHalfHeight;
                    object->fOldY = object->fPrecalculatedY - fVelY - GRAVITATION;

                    if (object->vely < 0.0f && fVelY > 0.0f)
                        object->fPrecalculatedY += fVelY;

                    if (object->vely < 0.0f)
                        object->vely = CapFallingVelocity(-object->vely + fVelY);

                    object->iVerticalPlatformCollision = 0;
                }

                return;
            }
        }
    } else {
        float fRelativeY = object->fPrecalculatedY + object->collisionHeight - fy + iHalfHeight;;

        if (fRelativeY >= 0.0f && fRelativeY < iHeight) {
            short ty = static_cast<short>(fRelativeY) / TILESIZE;

            int t1 = tile_flag_nonsolid;
            int t2 = tile_flag_nonsolid;

            if (fRelativeX1 >= 0.0f && fRelativeX1 < iWidth)
                t1 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX1) / TILESIZE, ty));

            if (fRelativeX2 >= 0.0f && fRelativeX2 < iWidth)
                t2 = tileToFlags(tileTypeAt(static_cast<short>(fRelativeX2) / TILESIZE, ty));

            if (((t1 & tile_flag_solid_on_top) || (t2 & tile_flag_solid_on_top)) && object->fOldY + object->collisionHeight <= (ty << TILESIZE_SHIFT) + fOldY - iHalfHeight) {
                if (object->iVerticalPlatformCollision == 0) {
                    object->KillObjectMapHazard();
                    return;
                } else {
                    if (object->bounce >= 0.0f)
                        object->platform = this;

                    object->iVerticalPlatformCollision = 2;

                    object->fPrecalculatedY = (ty << TILESIZE_SHIFT) - object->collisionHeight - 0.2f + fy - iHalfHeight;
                    object->fOldY = object->fPrecalculatedY - fVelY;
                    object->vely = object->BottomBounce();
                    object->inair = false;
                    object->onice = false;
                }

                return;
            }

            bool fSuperDeathTileUnderObject = object->fObjectDiesOnSuperDeathTiles && (pairIsDeadly(t1, t2, tile_flag_super_death_top));

            if (((t1 & tile_flag_solid) || (t2 & tile_flag_solid)) && !fSuperDeathTileUnderObject) {
                //on ground
                if (object->iVerticalPlatformCollision == 0) {
                    object->KillObjectMapHazard();
                    return;
                } else {
                    if (object->bounce >= 0.0f)
                        object->platform = this;

                    object->iVerticalPlatformCollision = 2;

                    object->fPrecalculatedY = (ty << TILESIZE_SHIFT) - object->collisionHeight - 0.2f + fy - iHalfHeight;
                    object->fOldY = object->fPrecalculatedY - fVelY;
                    object->vely = object->BottomBounce();
                    object->inair = false;

                    if ((t1 & tile_flag_ice && ((t2 & tile_flag_ice) || t2 == tile_flag_nonsolid || t2 == tile_flag_gap)) ||
                            (t2 & tile_flag_ice && ((t1 & tile_flag_ice) || t1 == tile_flag_nonsolid || t1 == tile_flag_gap)))
                        object->onice = true;
                    else
                        object->onice = false;

                    return;
                }
            } else if (fSuperDeathTileUnderObject) {
                object->KillObjectMapHazard();
                return;
            } else {
                if (object->platform == this)
                    object->platform = nullptr;
            }
        }
    }
}

short MovingPlatform::coldec_object(IO_MovingObject * object)
{
    //Special cases to deal with players overlapping the right and left sides of the screen
    if (object->fx + object->collisionWidth < fx - iHalfWidth) {
        if (object->fx + App::screenWidth >= fx + iHalfWidth || object->fx + object->collisionWidth + App::screenWidth < fx - iHalfWidth || object->fPrecalculatedY >= fy + iHalfHeight || object->fPrecalculatedY + object->collisionHeight < fy - iHalfHeight)
            return collision_none;
        else
            return collision_overlap_right;
    } else if (fx + iHalfWidth < object->fx) {
        if (object->fx >= fx + iHalfWidth + App::screenWidth || object->fx + object->collisionWidth < fx - iHalfWidth + App::screenWidth || object->fPrecalculatedY >= fy + iHalfHeight || object->fPrecalculatedY + object->collisionHeight < fy - iHalfHeight)
            return collision_none;
        else
            return collision_overlap_left;
    } else { //Normal case where no overlap
        if (object->fx >= fx + iHalfWidth || fx - iHalfWidth > object->fx + object->collisionWidth || object->fPrecalculatedY >= fy + iHalfHeight || fy - iHalfHeight > object->fPrecalculatedY + object->collisionHeight)
            return collision_none;
        else
            return collision_normal;
    }
}

bool MovingPlatform::IsInNoSpawnZone(short iX, short iY, short w, short h)
{
    short iTop = static_cast<short>(pPath->currentPos1().y) - iHalfHeight;

    if (iY + h < iTop)
        return false;

    short iBottom = static_cast<short>(pPath->currentPos1().y) + iHalfHeight;

    if (iY >= iBottom)
        return false;

    short iLeft = static_cast<short>(pPath->currentPos1().x) - iHalfWidth;
    short iRight = static_cast<short>(pPath->currentPos1().x) + iHalfWidth;

    //Deal with screen side overlap
    if (iX + w < iLeft)
        iX += App::screenWidth;
    else if (iX >= iRight)
        iX -= App::screenWidth;

    if (iX + w < iLeft || iX >= iRight)
        return false;

    //If we have reached this point, the item is colliding with the bounding rect of the platform
    //but not necessarily colliding with the platform (example a U shaped platform has an open area in the center)

    short iRelativeX[2] = {static_cast<short>(iX - iLeft), static_cast<short>(iX + w - iLeft)};
    short iRelativeY[2] = {static_cast<short>(iY - iTop), static_cast<short>(iY + h - iTop)};

    for (short sX = 0; sX < 2; sX++) {
        for (short sY = 0; sY < 2; sY++) {
            if (iRelativeX[sX] >= 0 && iRelativeX[sX] < iWidth && iRelativeY[sY] >= 0 && iRelativeY[sY] < iHeight) {
                short tx = iRelativeX[sX] / TILESIZE;
                short ty = iRelativeY[sY] / TILESIZE;

                int t = tileToFlags(tileTypeAt(tx, ty));

                if (t & tile_flag_solid) {
                    return true;
                }
            }
        }
    }

    return false;
}

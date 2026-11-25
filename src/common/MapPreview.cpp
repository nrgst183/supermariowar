#include "map.h"

#include "IO_Block.h"
#include "GameValues.h"
#include "GlobalConstants.h"
#include "Game.h"
#include "FileIO.h"
#include "FileList.h"
#include "movingplatform.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "TilesetManager.h"
#include "Version.h"
#include "map/MapReader.h"

#include <SDL3_image/SDL_image.h>

#include <cmath>
#include <iostream>
using std::cout;
using std::endl;

#if defined(__APPLE__)
#include <sys/stat.h>
#endif

gfxSprite CMap::createThumbnailSurface(bool fUseClassicPack)
{
    gfxSprite sThumbnail = gfxSprite::blank(160, 120);

    std::string localSzBackgroundFile;
    std::string path;

    if (fUseClassicPack) {
        localSzBackgroundFile = "gfx/packs/Classic/backgrounds/" + szBackgroundFile;
        path = convertPath(localSzBackgroundFile);

        //if the background file doesn't exist, use the classic background
        if (!FileExists(path))
            path = convertPath("gfx/packs/Classic/backgrounds/Land_Classic.png");
    } else {
        localSzBackgroundFile = "gfx/packs/backgrounds/" + szBackgroundFile;
        path = convertPath(localSzBackgroundFile, gamegraphicspacklist->currentPath());

        //if the background file doesn't exist, use the classic background
        if (!FileExists(path))
            path = convertPath("gfx/packs/backgrounds/Land_Classic.png", gamegraphicspacklist->currentPath());
    }
    {
        gfxSprite sBackground = ImageLoader(path).withoutColorKey().create();
        SDL_Rect srcRectBackground = {0, 0, App::screenWidth, App::screenHeight};
        SDL_Rect dstRectBackground = {0, 0, 160, 120};
        sBackground.drawStretch(srcRectBackground, sThumbnail.getSurface(), dstRectBackground);
    }

    preDrawPreviewBackground(sThumbnail, true);
    preDrawPreviewBlocks(sThumbnail, true);
    preDrawPreviewMapItems(sThumbnail, true);
    drawThumbnailHazards(sThumbnail);
    drawThumbnailPlatforms(sThumbnail);
    preDrawPreviewForeground(sThumbnail, true);
    preDrawPreviewWarps(sThumbnail, true);

    return sThumbnail;
}

//Save thumbnail image
void CMap::saveThumbnail(const std::string &sFile, bool fUseClassicPack)
{
    gfxSprite sThumbnail = createThumbnailSurface(fUseClassicPack);

    //Save the screenshot with the same name as the map file
    IMG_SavePNG(sThumbnail.getSurface(), sFile.c_str());
}

void CMap::drawThumbnailHazards(gfxSprite& targetSurface)
{
    for (const MapHazard& hazard : maphazards) {
        DrawMapHazard(hazard, 2, false, targetSurface.getSurface());
    }
}

void CMap::drawThumbnailPlatforms(gfxSprite& targetSurface)
{
    for (MovingPlatform* platform : platforms) {
        MovingPlatformPath * basepath = platform->pPath;

        if (auto* path = dynamic_cast<StraightPath*>(basepath)) {
            DrawPlatform(path->typeId(), platform->iTileData, path->startPos().x * 2.f, path->startPos().y * 2.f, path->endPos().x * 2.f, path->endPos().y * 2.f, 0.0f, 0.0f, 0.0f, 2, platform->iTileWidth, platform->iTileHeight, true, true, targetSurface.getSurface());
        } else if (auto* path = dynamic_cast<StraightPathContinuous*>(basepath)) {
            DrawPlatform(path->typeId(), platform->iTileData, path->startPos().x * 2.f, path->startPos().y * 2.f, 0, 0, path->angle(), 0.0f, 0.0f, 2, platform->iTileWidth, platform->iTileHeight, true, true, targetSurface.getSurface());
        } else if (auto* path = dynamic_cast<EllipsePath*>(basepath)) {
            DrawPlatform(path->typeId(), platform->iTileData, path->centerPos().x * 2.f, path->centerPos().y * 2.f, 0, 0, path->startAngle(), path->radius().x * 2, path->radius().y * 2, 2, platform->iTileWidth, platform->iTileHeight, true, true, targetSurface.getSurface());
        }
    }
}

void CMap::preDrawPreviewWarps(gfxSprite& targetSurface, bool fThumbnail)
{
    short iTileSize = 16;
    short iScreenshotSize = 0;

    if (fThumbnail) {
        iTileSize = 8;
        iScreenshotSize = 1;
    }

    for (int j = 0; j < MAPHEIGHT; j++) {
        for (int i = 0; i < MAPWIDTH; i++) {
            Warp * wWarp = &warpdata[i][j];

            if (wWarp->connection != -1) {
                SDL_Rect rSrc = {wWarp->connection * iTileSize, wWarp->direction * iTileSize, iTileSize, iTileSize};
                SDL_Rect rDst = {i * iTileSize, j * iTileSize, iTileSize, iTileSize};

                rm->spr_thumbnail_warps[iScreenshotSize].draw(rSrc, targetSurface.getSurface(), rDst);
            }
        }
    }
}

void CMap::preDrawPreviewMapItems(gfxSprite& targetSurface, bool fThumbnail)
{
    short iTileSize = 16;
    short iScreenshotSize = 0;

    if (fThumbnail) {
        iTileSize = 8;
        iScreenshotSize = 1;
    }

    for (const MapItem& item : mapitems) {
        SDL_Rect rSrc = {item.itype * iTileSize, 0, iTileSize, iTileSize};
        SDL_Rect rDst = {item.ix * iTileSize, item.iy * iTileSize, iTileSize, iTileSize};

        rm->spr_thumbnail_mapitems[iScreenshotSize].draw(rSrc, targetSurface.getSurface(), rDst);
    }
}

void CMap::preDrawPreviewBackground(gfxSprite& targetSurface, bool fThumbnail)
{
    drawPreview(targetSurface, 0, fThumbnail);
    drawPreview(targetSurface, 1, fThumbnail);

    if (!game_values.toplayer) {
        drawPreview(targetSurface, 2, fThumbnail);
        drawPreview(targetSurface, 3, fThumbnail);
    }
}

void CMap::preDrawPreviewBackground(const gfxSprite& background, gfxSprite& targetSurface, bool fThumbnail)
{
    SDL_Rect srcrect;
    srcrect.x = 0;
    srcrect.y = 0;
    srcrect.w = App::screenWidth;
    srcrect.h = App::screenHeight;

    SDL_Rect dstrect;
    dstrect.x = 0;
    dstrect.y = 0;

    if (fThumbnail) {
        dstrect.w = 160;
        dstrect.h = 120;
    } else {
        dstrect.w = App::screenWidth/2;
        dstrect.h = App::screenHeight/2;
    }

    background.drawStretch(srcrect, targetSurface.getSurface(), dstrect);

    preDrawPreviewBackground(targetSurface, fThumbnail);
}

void CMap::preDrawPreviewBlocks(gfxSprite& targetSurface, bool fThumbnail)
{
    if (!fThumbnail) {
        SDL_FillSurfaceRect(targetSurface.getSurface(), nullptr, SDL_MapSurfaceRGB(targetSurface.getSurface(), 255, 0, 255));
        SDL_SetSurfaceColorKey(targetSurface.getSurface(), true, SDL_MapSurfaceRGB(targetSurface.getSurface(), 255, 0, 255));
    }

    drawPreviewBlocks(targetSurface, fThumbnail);
}

void CMap::preDrawPreviewForeground(gfxSprite& targetSurface, bool fThumbnail)
{
    if (!fThumbnail) {
        SDL_FillSurfaceRect(targetSurface.getSurface(), nullptr, SDL_MapSurfaceRGB(targetSurface.getSurface(), 255, 0, 255));
        SDL_SetSurfaceColorKey(targetSurface.getSurface(), true, SDL_MapSurfaceRGB(targetSurface.getSurface(), 255, 0, 255));
    }

    if (!game_values.toplayer)
        return;

    drawPreview(targetSurface, 2, fThumbnail);
    drawPreview(targetSurface, 3, fThumbnail);
}

void CMap::drawPreview(gfxSprite& targetSurface, int layer, bool fThumbnail)
{
    int i, j;

    //draw left to right full vertical
    DrawSize iTilesetSize = DrawSize::Preview;
    if (fThumbnail)
        iTilesetSize = DrawSize::Thumbnail;

    for (i = 0; i < MAPWIDTH; i++) {
        for (j = 0; j < MAPHEIGHT; j++) {
            TilesetTile * tile = &mapdata[i][j][layer];
            if (tile->iID == TILESETNONE)
                continue;

            //Handle drawing preview for animated tiles
            if (tile->iID >= 0) {
                g_tilesetmanager->Draw(targetSurface.getSurface(), tile->iID, iTilesetSize, tile->iCol, tile->iRow, i, j);
            } else if (tile->iID == TILESETANIMATED) {
                const SDL_Rect& srcRect = CTilesetManager::rect(iTilesetSize, tile->iCol * 4, tile->iRow);
                const SDL_Rect& dstRect = CTilesetManager::rect(iTilesetSize, i, j);
                rm->spr_tileanimation[static_cast<size_t>(iTilesetSize)].draw(srcRect, targetSurface.getSurface(), dstRect);
            } else if (tile->iID == TILESETUNKNOWN) {
                const SDL_Rect& srcRect = CTilesetManager::rect(iTilesetSize, 0, 0);
                const SDL_Rect& dstRect = CTilesetManager::rect(iTilesetSize, i, j);
                rm->spr_unknowntile[static_cast<size_t>(iTilesetSize)].draw(srcRect, targetSurface.getSurface(), dstRect);
            }
        }
    }
}

void CMap::drawPreviewBlocks(gfxSprite& targetSurface, bool fThumbnail)
{
    int i, j, ts;

    short iBlockSize = PREVIEWTILESIZE;

    if (fThumbnail)
        iBlockSize = THUMBTILESIZE;

    SDL_Rect rectDst;
    rectDst.w = iBlockSize;
    rectDst.h = iBlockSize;

    SDL_Rect rectSrc;
    rectSrc.w = iBlockSize;
    rectSrc.h = iBlockSize;

    rectDst.x = 0;
    for (i = 0; i < MAPWIDTH; i++) {
        rectDst.y = -iBlockSize;	//this is okay, see

        for (j = 0; j < MAPHEIGHT; j++) {
            rectDst.y += iBlockSize;	// here

            ts = objectdata[i][j].iType;
            if (ts == -1)
                continue;

            //Don't draw hidden blocks
            if (objectdata[i][j].fHidden)
                continue;

            rectSrc.x = static_cast<Sint16>(ts * iBlockSize);
            rectSrc.y = 0;

            //Draw the turned off switch blocks too
            if (ts >= 7 && ts <= 10) {
                if (iSwitches[(ts - 7) % 4] == 0)
                    rectSrc.y = iBlockSize;
            } else if (ts >= 11 && ts <= 14) {
                if (objectdata[i][j].iSettings[0] == 0)
                    rectSrc.y = iBlockSize;
            } else if (ts >= 15 && ts <= 19) {
                rectSrc.x = iBlockSize * (ts - 15);
                rectSrc.y = iBlockSize;
            } else if (ts >= 20 && ts <= 29) {
                rectSrc.x = iBlockSize * (ts - 20);
                rectSrc.y = iBlockSize << 1;
            }

            if (fThumbnail)
                rm->spr_blocks[2].draw(rectSrc, targetSurface.getSurface(), rectDst);
            else
                rm->spr_blocks[1].draw(rectSrc, targetSurface.getSurface(), rectDst);
        }

        rectDst.x += iBlockSize;
    }
}

// The background (its frame `frame`: an animated one's, BackgroundAnimation) and the back layers
// over it
void CMap::drawBackgroundLayers(const gfxSprite& background, gfxSprite& mapspr, int frame)
{
    const SDL_Rect src = BackgroundAnimation::frameRect(frame);
    background.draw(src, mapspr.getSurface(), Vec2i {0, 0});

    draw(mapspr, 0);
    draw(mapspr, 1);

    if (!game_values.toplayer) {
        draw(mapspr, 2);
        draw(mapspr, 3);
    }

    //The background is opaque: drawn every frame as a plain copy, not blended pixel by pixel
    SDL_SetSurfaceBlendMode(mapspr.getSurface(), SDL_BLENDMODE_NONE);
}

bool CMap::redrawCell(short x, short y, const gfxSprite& background, gfxSprite& backmap, gfxSprite& frontmap)
{
    for (short layer = 0; layer < MAPLAYERS; layer++) {
        if (mapdata[x][y][layer].iID == TILESETANIMATED)
            return false;
    }
    const SDL_Rect cell {x * TILESIZE, y * TILESIZE, TILESIZE, TILESIZE};
    auto drawLayer = [&](gfxSprite& target, int layer) {
        const TilesetTile& tile = mapdata[x][y][layer];
        if (tile.iID >= 0)
            g_tilesetmanager->Draw(target.getSurface(), tile.iID, DrawSize::Ingame, tile.iCol, tile.iRow, x, y);
    };

    //The back picture: the background's piece (its current frame), the back layers (all four without
    //the top layer in front)
    const SDL_Rect frame = BackgroundAnimation::frameRect(iBackgroundFrame);
    background.draw(SDL_Rect {frame.x + cell.x, frame.y + cell.y, TILESIZE, TILESIZE}, backmap.getSurface(), Vec2i {cell.x, cell.y});
    drawLayer(backmap, 0);
    drawLayer(backmap, 1);
    if (!game_values.toplayer) {
        drawLayer(backmap, 2);
        drawLayer(backmap, 3);
    }

    //The front picture: see-through there, then the front layers
    SDL_Surface* front = frontmap.getSurface();
    SDL_FillSurfaceRect(front, &cell, SDL_MapSurfaceRGBA(front, 0, 0, 0, 0));
    if (game_values.toplayer) {
        drawLayer(frontmap, 2);
        drawLayer(frontmap, 3);
    }
    return true;
}

void CMap::predrawbackground(gfxSprite &background, gfxSprite &mapspr)
{
    //(a match starts with the first frame of an animated background)
    iBackgroundFrame = 0;
    iBackgroundTicks = 0;
    drawBackgroundLayers(background, mapspr, 0);

    //Add animated tile objects for each animated tile in a platform (to be used later for drawing)
    addPlatformAnimatedTiles();
}

void CMap::redrawBackgroundFrame(int frame)
{
    iBackgroundFrame = frame;
    //(both of the match's background layers: the animated tiles draw into one while the other shows)
    for (gfxSprite& layer : rm->spr_backmap) {
        if (layer)    //(the second: not made for a map without animated tiles)
            drawBackgroundLayers(rm->spr_background, layer, frame);
    }
    if (animatedTilesSurface.getSurface()) {
        for (AnimatedTile* tile : animatedtiles) {
            if (tile->fBackgroundAnimated)
                drawAnimatedBackgroundTile(*tile, frame);
        }
    }
}

void CMap::predrawforeground(gfxSprite &foregroundspr)
{
    if (SDL_MUSTLOCK(foregroundspr.getSurface()))
        SDL_LockSurface(foregroundspr.getSurface());

    // Start fully transparent and draw the front layers over it. (This used to be a magenta fill
    // with a magenta color key, which doesn't take effect on this 32-bit surface with alpha under
    // SDL3: the empty parts of front layer tiles were drawn magenta.)
    SDL_Surface* surface = foregroundspr.getSurface();
    SDL_SetSurfaceColorKey(surface, false, 0);
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 0, 0, 0));
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);

    if (SDL_MUSTLOCK(foregroundspr.getSurface()))
        SDL_UnlockSurface(foregroundspr.getSurface());

    draw(foregroundspr, 2);
    draw(foregroundspr, 3);
}

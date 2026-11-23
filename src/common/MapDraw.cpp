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

void CMap::AnimateTiles(short iFrame)
{
    //If the end index for this frame is the same as the next frame, that means there are 0
    //animated tiles to paint this frame so we should skip it
    if (iAnimatedVectorIndices[iFrame] == iAnimatedVectorIndices[iFrame + 1])
        return;

    //For each animated tile we are painting this frame, paint it to the map tiles or a platform
    for (short iTile = iAnimatedVectorIndices[iFrame]; iTile < iAnimatedVectorIndices[iFrame + 1]; iTile++) {
        AnimatedTile * tile = animatedtiles[iTile];

        if (tile->fBackgroundAnimated) {
            animatedTilesSurface.draw(tile->rAnimationSrc[0][iTileAnimationFrame], animatedBackmapSurface, tile->rDest);
        }

        if (tile->fForegroundAnimated) {
            animatedTilesSurface.draw(tile->rAnimationSrc[1][iTileAnimationFrame], animatedFrontmapSurface, tile->rDest);
        }

        if (tile->pPlatform) {
            animatedTilesSurface.draw(tile->rAnimationSrc[0][iTileAnimationFrame], tile->pPlatform->sprites[g_iCurrentDrawIndex].getSurface(), tile->rDest);
        }
    }
}

void CMap::draw(gfxSprite& targetSurface, int layer)
{
    int i, j;

    //draw left to right full vertical
    bltrect.x = 0;
    for (i = 0; i < MAPWIDTH; i++) {
        bltrect.y = -TILESIZE;	//this is okay, see

        for (j = 0; j < MAPHEIGHT; j++) {
            bltrect.y += TILESIZE;	// here

            TilesetTile * tile = &mapdata[i][j][layer];

            //If there is no tile to draw, continue
            if (tile->iID == TILESETNONE)
                continue;

            //If this is an animated tile, then setup an animated tile struct for use in drawing
            if (tile->iID >= 0) {
                g_tilesetmanager->Draw(targetSurface.getSurface(), tile->iID, DrawSize::Ingame, tile->iCol, tile->iRow, i, j);
            } else if (tile->iID == TILESETANIMATED) {
                //See if we already have this tile
                bool fNeedNewAnimatedTile = true;

                short iNewTileId = j * MAPWIDTH + i;
                std::vector<AnimatedTile*>::iterator iter = animatedtiles.begin(), lim = animatedtiles.end();
                while (iter != lim) {
                    if (iNewTileId == (*iter)->id) {
                        fNeedNewAnimatedTile = false;
                        break;
                    }

                    ++iter;
                }

                if (fNeedNewAnimatedTile) {
                    AnimatedTile * animatedtile = new AnimatedTile();
                    animatedtile->id = iNewTileId;

                    animatedtile->fBackgroundAnimated = false;
                    animatedtile->fForegroundAnimated = false;
                    animatedtile->pPlatform = nullptr;

                    for (short iLayer = 0; iLayer < 4; iLayer++) {
                        TilesetTile * layerTile = &mapdata[i][j][iLayer];
                        TilesetTile * toTile = &animatedtile->layers[iLayer];

                        toTile->iID = layerTile->iID;
                        toTile->iCol = layerTile->iCol;
                        toTile->iRow = layerTile->iRow;

                        if (layerTile->iID >= 0) { //If it is part of a tileset
                            (animatedtile->rSrc[iLayer][0]) = {layerTile->iCol << 5, layerTile->iRow << 5, TILESIZE, TILESIZE};
                        } else if (layerTile->iID == TILESETANIMATED) {
                            for (short iRect = 0; iRect < 4; iRect++) {
                                (animatedtile->rSrc[iLayer][iRect]) = {(iRect + (layerTile->iCol << 2)) << 5, layerTile->iRow << 5, TILESIZE, TILESIZE};
                            }

                            //Background is animated if it is a background layer or if it is a foreground layer and we are not displaying the foreground
                            if (iLayer < 2 || !game_values.toplayer)
                                animatedtile->fBackgroundAnimated = true;

                            //Foreground is animated if it is a foreground layer and we are displaying the foreground
                            if (iLayer >= 2 && game_values.toplayer)
                                animatedtile->fForegroundAnimated = true;
                        }
                    }

                    (animatedtile->rDest) = {bltrect.x, bltrect.y, TILESIZE, TILESIZE};
                    animatedtiles.push_back(animatedtile);
                }
            } else if (tile->iID == TILESETUNKNOWN) { //Draw red X where tile should be
                rm->spr_unknowntile[0].draw(CTilesetManager::rect(DrawSize::Ingame, 0, 0), targetSurface.getSurface(), bltrect);
            }
        }

        bltrect.x += TILESIZE;
    }

    bltrect.x = 0;
    bltrect.y = 0;
    bltrect.w = App::screenWidth;
    bltrect.h = App::screenHeight;
}

void CMap::addPlatformAnimatedTiles()
{
    for (MovingPlatform* platform : platforms) {
        short iHeight = platform->iTileHeight;
        short iWidth = platform->iTileWidth;

        short iDestX = 0;
        short iDestY = 0;

        for (short iRow = 0; iRow < iHeight; iRow++) {
            for (short iCol = 0; iCol < iWidth; iCol++) {
                if (platform->tileAt(iCol, iRow).iID == TILESETANIMATED) {
                    AnimatedTile * animatedtile = new AnimatedTile();
                    animatedtile->id = -1;  //we don't want this ID to collide with an animated map tile

                    animatedtile->fBackgroundAnimated = false;
                    animatedtile->fForegroundAnimated = false;
                    animatedtile->pPlatform = platform;

                    const TilesetTile& tile = platform->tileAt(iCol, iRow);
                    TilesetTile * toTile = &animatedtile->layers[0];

                    toTile->iID = tile.iID;
                    toTile->iCol = tile.iCol;
                    toTile->iRow = tile.iRow;

                    for (short iRect = 0; iRect < 4; iRect++) {
                        (animatedtile->rSrc[0][iRect]) = {(iRect + (tile.iCol << 2)) << 5, tile.iRow << 5, TILESIZE, TILESIZE};
                    }

                    (animatedtile->rDest) = {iDestX, iDestY, TILESIZE, TILESIZE};
                    animatedtiles.push_back(animatedtile);
                }

                iDestX += TILESIZE;
            }

            iDestY += TILESIZE;
            iDestX = 0;
        }
    }
}

void CMap::drawAnimatedBackgroundTile(const AnimatedTile& tile, int backgroundFrame)
{
    //(its cell of the background's frame)
    SDL_Rect cell = tile.rDest;
    cell.y += static_cast<int>(BackgroundAnimation::frameRect(backgroundFrame).y);
    for (short sTileAnimationFrame = 0; sTileAnimationFrame < 4; sTileAnimationFrame++) {
        SDL_Rect rDst = tile.rAnimationSrc[0][sTileAnimationFrame];
        rm->spr_background.draw(cell, animatedTilesSurface.getSurface(), rDst);

        for (short iLayer = 0; iLayer < iAnimatedBackgroundLayers; iLayer++) {
            const TilesetTile* tilesetTile = &tile.layers[iLayer];
            if (tilesetTile->iID >= 0) {
                g_tilesetmanager->tileset(tilesetTile->iID)->draw(DrawSize::Ingame, tile.rSrc[iLayer][0], animatedTilesSurface.getSurface(), rDst);
            } else if (tilesetTile->iID == TILESETANIMATED) {
                rm->spr_tileanimation[0].draw(tile.rSrc[iLayer][sTileAnimationFrame], animatedTilesSurface.getSurface(), rDst);
            } else if (tilesetTile->iID == TILESETUNKNOWN) {
                rm->spr_unknowntile[0].draw(CTilesetManager::rect(DrawSize::Ingame, 0, 0), animatedTilesSurface.getSurface(), rDst);
            }
        }
    }
}

void CMap::SetupAnimatedTiles()
{
    iAnimatedBackgroundLayers = 2;
    if (!game_values.toplayer)
        iAnimatedBackgroundLayers = 4;

    g_iCurrentDrawIndex = 0;

    iAnimatedTileCount = animatedtiles.size();
    animatedTilesSurface = gfxSprite();

    if (iAnimatedTileCount > 0) {
        animatedFrontmapSurface = rm->spr_frontmap[g_iCurrentDrawIndex].getSurface();
        animatedBackmapSurface = rm->spr_backmap[g_iCurrentDrawIndex].getSurface();
        animatedTilesSurface = gfxSprite::blank(1024, 1024);

        const Uint32 iClearColor = SDL_MapSurfaceRGBA(animatedTilesSurface.getSurface(), 0, 0, 0, 0);

        std::vector<AnimatedTile*>::iterator iter = animatedtiles.begin(), lim = animatedtiles.end();

        bool fSrcSurfaceFull = false;

        SDL_Rect rDst = {0, 0, 32, 32};
        while (iter != lim && !fSrcSurfaceFull) {
            AnimatedTile * tile = *iter;

            //If the background layer has an animated tile, then create the set of 4 images that will be
            //drawn to this tile during the gameplay (gfx optimization by only drawing from animatedTilesSurface)
            if (tile->fBackgroundAnimated) {
                for (short sTileAnimationFrame = 0; sTileAnimationFrame < 4; sTileAnimationFrame++) {
                    tile->rAnimationSrc[0][sTileAnimationFrame] = rDst;

                    rDst.x += 32;
                    if (rDst.x >= 1024) {
                        rDst.x = 0;
                        rDst.y += 32;
                        if (rDst.y >= 1024) {
                            tile->fForegroundAnimated = false;
                            fSrcSurfaceFull = true;
                            break;
                        }
                    }
                }
            }

            if (tile->fBackgroundAnimated && !fSrcSurfaceFull)
                drawAnimatedBackgroundTile(*tile, 0);

            //If we have run out of room on the animatedTilesSurface surface, then stop reading animated tiles
            if (fSrcSurfaceFull) {
                iter++;
                break;
            }

            if (tile->fForegroundAnimated) {
                for (short sTileAnimationFrame = 0; sTileAnimationFrame < 4; sTileAnimationFrame++) {
                    tile->rAnimationSrc[1][sTileAnimationFrame] = rDst;

                    // The front layer is transparent where it has no tiles (see predrawforeground)
                    SDL_FillSurfaceRect(animatedTilesSurface.getSurface(), &rDst, iClearColor);

                    for (short iLayer = 2; iLayer < 4; iLayer++) {
                        TilesetTile * tilesetTile = &tile->layers[iLayer];
                        if (tilesetTile->iID >= 0) {
                            g_tilesetmanager->tileset(tilesetTile->iID)->draw(DrawSize::Ingame, tile->rSrc[iLayer][0], animatedTilesSurface.getSurface(), rDst);
                        } else if (tilesetTile->iID == TILESETANIMATED) {
                            rm->spr_tileanimation[0].draw(tile->rSrc[iLayer][sTileAnimationFrame], animatedTilesSurface.getSurface(), rDst);
                        } else if (tilesetTile->iID == TILESETUNKNOWN) {
                            rm->spr_unknowntile[0].draw(CTilesetManager::rect(DrawSize::Ingame, 0, 0), animatedTilesSurface.getSurface(), rDst);
                        }
                    }

                    rDst.x += 32;
                    if (rDst.x >= 1024) {
                        rDst.x = 0;
                        rDst.y += 32;
                        if (rDst.y >= 1024) {
                            fSrcSurfaceFull = true;
                            break;
                        }
                    }
                }
            }

            //If we have run out of room on the animatedTilesSurface surface, then stop reading animated tiles
            if (fSrcSurfaceFull) {
                iter++;
                break;
            }

            if (tile->pPlatform) {
                for (short sTileAnimationFrame = 0; sTileAnimationFrame < 4; sTileAnimationFrame++) {
                    tile->rAnimationSrc[0][sTileAnimationFrame] = rDst;

                    TilesetTile * tilesetTile = &tile->layers[0];
                    if (tilesetTile->iID == TILESETANIMATED) {
                        rm->spr_tileanimation[0].draw(tile->rSrc[0][sTileAnimationFrame], animatedTilesSurface.getSurface(), rDst);
                    } else {
                        cout << endl << " ERROR: A nonanimated platform tile was added to the animated tile list" << endl;
                    }

                    rDst.x += 32;
                    if (rDst.x >= 1024) {
                        rDst.x = 0;
                        rDst.y += 32;
                        if (rDst.y >= 1024) {
                            fSrcSurfaceFull = true;
                            break;
                        }
                    }
                }
            }

            ++iter;
        }

        //Turn off animation for the tiles that couldn't fit onto our animation page (256 tiles max)
        while (iter != lim) {
            AnimatedTile * tile = *iter;

            tile->fBackgroundAnimated = false;
            tile->fForegroundAnimated = false;
            tile->pPlatform = nullptr;

            ++iter;
        }

        //Figure out how many animated tiles we will be painting a frame (evenly distribute the painting as much as possible)
        for (short iAnimatedFrame = 0; iAnimatedFrame <= NUM_FRAMES_BETWEEN_TILE_ANIMATION; iAnimatedFrame++)
            iAnimatedVectorIndices[iAnimatedFrame] = (iAnimatedFrame * iAnimatedTileCount) / NUM_FRAMES_BETWEEN_TILE_ANIMATION;

        //Draw all animated tiles to the front buffer
        for (short iFrame = 0; iFrame < NUM_FRAMES_BETWEEN_TILE_ANIMATION; iFrame++)
            AnimateTiles(iFrame);

        //Setup the back buffer to draw animated tiles to each frame
        //This is flipped every NUM_FRAMES_BETWEEN_TILE_ANIMATION to be the displayed surface
        animatedFrontmapSurface = rm->spr_frontmap[1 - g_iCurrentDrawIndex].getSurface();
        animatedBackmapSurface = rm->spr_backmap[1 - g_iCurrentDrawIndex].getSurface();

        //Draw the first set of animated tiles to the back buffer
        AnimateTiles(0);
    }
}

void CMap::drawPlatforms(short iLayer)
{
    std::list<MovingPlatform*>::iterator iterate = platformdrawlayer[iLayer].begin(), lim = platformdrawlayer[iLayer].end();

    while (iterate != lim) {
        (*iterate)->draw();
        iterate++;
    }

    if (iLayer == 2) {
        auto iterateTemps = tempPlatforms.begin(), limTemps = tempPlatforms.end();
        while (iterateTemps != limTemps) {
            (*iterateTemps)->draw();
            iterateTemps++;
        }
    }
}

void CMap::drawPlatforms(short iOffsetX, short iOffsetY, short iLayer)
{
    std::list<MovingPlatform*>::iterator iterate = platformdrawlayer[iLayer].begin(), lim = platformdrawlayer[iLayer].end();

    while (iterate != lim) {
        (*iterate)->draw(iOffsetX, iOffsetY);
        iterate++;
    }
}

void CMap::drawWarpLocks()
{
    for (int iWarpExit = 0; iWarpExit < numwarpexits; iWarpExit++) {
        if (warplocked[warpexits[iWarpExit].connection] || warpexits[iWarpExit].locktimer > 0) {
            rm->spr_warplock.draw(warpexits[iWarpExit].lockx, warpexits[iWarpExit].locky);
        }
    }
}

void CMap::drawfrontlayer()
{
    for (int k = 0; k < numdrawareas; k++)
        rm->spr_frontmap[g_iCurrentDrawIndex].draw(drawareas[k].x, drawareas[k].y, drawareas[k]);

    //Draw gaps in pink for debugging
}

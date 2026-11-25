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
#include <cstring>
#include <iostream>
using std::cout;
using std::endl;

#if defined(__APPLE__)
#include <sys/stat.h>
#endif

// The front layer's rectangles (where layers 2 and 3 have tiles): drawing the front layer image only
// there spares the rest of the screen. Worked out from the tiles when a map loads (the counts saved
// in older maps can be wrong: some cover the whole screen, maps before 1.6 have none) and when one
// is saved.
void CMap::computeDrawAreas()
{
    bool skip[MAPWIDTH][MAPHEIGHT];
    for (int j = 0; j < MAPHEIGHT; j++)
        for (int i = 0; i < MAPWIDTH; i++)
            skip[i][j] = mapdata[i][j][2].iID <= TILESETNONE && mapdata[i][j][3].iID <= TILESETNONE;
    buildDrawAreas(skip);
}

// The same from the front layer image itself, once it's drawn for a match: a tile whose pixels are
// all see-through (some maps have many) draws nothing, so its cell needn't be drawn every frame. The
// cells with animated tiles stay (their next frames may show).
void CMap::fitDrawAreas(const gfxSprite& front)
{
    SDL_Surface* surface = front.getSurface();
    const int bpp = surface ? SDL_BYTESPERPIXEL(surface->format) : 0;
    if (!surface || (bpp != 4 && bpp != 2) || surface->w < MAPWIDTH * TILESIZE || surface->h < MAPHEIGHT * TILESIZE)
        return;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(surface->format);
    if (!details || !details->Amask)
        return;
    if (!SDL_LockSurface(surface))
        return;
    bool skip[MAPWIDTH][MAPHEIGHT];
    for (int j = 0; j < MAPHEIGHT; j++) {
        for (int i = 0; i < MAPWIDTH; i++) {
            const bool tiles = mapdata[i][j][2].iID > TILESETNONE || mapdata[i][j][3].iID > TILESETNONE;
            const bool animated = mapdata[i][j][2].iID == TILESETANIMATED || mapdata[i][j][3].iID == TILESETANIMATED;
            bool shows = animated;
            for (int y = 0; y < TILESIZE && tiles && !shows; y++) {
                const Uint8* line = static_cast<const Uint8*>(surface->pixels) + static_cast<ptrdiff_t>(j * TILESIZE + y) * surface->pitch;
                for (int x = 0; x < TILESIZE && !shows; x++) {
                    //(the Xbox's front layer: 16 bits a pixel)
                    const Uint32 pixel = bpp == 2 ? reinterpret_cast<const Uint16*>(line)[i * TILESIZE + x]
                                                  : reinterpret_cast<const Uint32*>(line)[i * TILESIZE + x];
                    shows = (pixel & details->Amask) != 0;
                }
            }
            skip[i][j] = !(tiles && shows);
        }
    }
    SDL_UnlockSurface(surface);
    buildDrawAreas(skip);
}

// The rectangles (as few as it finds) covering the cells not skipped
void CMap::buildDrawAreas(const bool skip[MAPWIDTH][MAPHEIGHT])
{
    int i, j;
    bool usedtile[MAPWIDTH][MAPHEIGHT];
    for (j = 0; j < MAPHEIGHT; j++)
        for (i = 0; i < MAPWIDTH; i++)
            usedtile[i][j] = skip[i][j];

    //Figure out where the overlayer draw rects are
    numdrawareas = 0;
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            if (!usedtile[i][j]) {
                bool fDownDone = false;
                bool fRightDone = false;

                int downsize = j + 1;
                int rightsize = i + 1;
                int attempt = 1;

                while (true) {
                    if (!fRightDone) {
                        for (int right = i; right < rightsize; right++) {
                            if (right >= MAPWIDTH || j + attempt >= MAPHEIGHT || usedtile[right][j + attempt])
                                fRightDone = true;
                        }

                        if (!fRightDone)
                            downsize++;
                    }

                    if (!fDownDone) {
                        for (int down = j; down < downsize; down++) {
                            if (i + attempt >= MAPWIDTH || down >= MAPHEIGHT || usedtile[i + attempt][down])
                                fDownDone = true;
                        }

                        if (!fDownDone)
                            rightsize++;
                    }

                    if (fDownDone && fRightDone) {
                        if (numdrawareas < MAXDRAWAREAS) {
                            drawareas[numdrawareas].x = static_cast<Sint16>(i << 5);
                            drawareas[numdrawareas].y = static_cast<Sint16>(j << 5);
                            drawareas[numdrawareas].w = static_cast<Uint16>((rightsize - i) << 5);
                            drawareas[numdrawareas].h = static_cast<Uint16>((downsize - j) << 5);

                            numdrawareas++;
                        }

                        for (int down = j; down < downsize; down++) {
                            for (int right = i; right < rightsize; right++) {
                                usedtile[right][down] = true;
                            }
                        }

                        break;
                    }

                    attempt++;
                }
            }
        }
    }
}

std::vector<std::string> g_savedMapFiles;

void CMap::saveMap(const std::string& file, const std::vector<MovingPlatform*>& extra)
{
    //(saved during a match, on one machine: what saving changes, the warps' ids, put back as they were)
    Warp warps[MAPWIDTH][MAPHEIGHT];
    std::memcpy(warps, warpdata, sizeof(warps));
    const size_t own = platforms.size();
    platforms.insert(platforms.end(), extra.begin(), extra.end());
    saveMap(file);
    platforms.resize(own);
    std::memcpy(warpdata, warps, sizeof(warps));
}

void CMap::saveMap(const std::string& file)
{
    int i, j, k;

    g_savedMapFiles.push_back(file);

    cout << "saving map " << file << " ... ";

    BinaryFile mapfile(file, "wb");
    if (!mapfile.is_open()) {
        cout << endl << " ERROR: couldn't save map" << endl;
        return;
    }

    //First write the map compatibility version number
    //(this will allow the map loader to identify if the map needs conversion)
    // For compatibility, let's use the final 1.8 version until
    // there's no actual change in the map format.
    mapfile.write_i32(1); //Major
    mapfile.write_i32(8); //Minor
    mapfile.write_i32(0); //Micro
    mapfile.write_i32(4); //Build
    // mapfile.write_i32(g_iVersion[0]); //Major
    // mapfile.write_i32(g_iVersion[1]); //Minor
    // mapfile.write_i32(g_iVersion[2]); //Micro
    // mapfile.write_i32(g_iVersion[3]); //Build

    //Calculate warp zones
    bool usedtile[MAPWIDTH][MAPHEIGHT];
    for (j = 0; j < MAPHEIGHT; j++)
        for (i = 0; i < MAPWIDTH; i++)
            usedtile[i][j] = false;

    int iHazardCount = 0;
    int iWarpCount = 0;
    int iIceCount = 0;
    int iPowerupBlockCount = 0;
    int iPlatformCount = 0;
    int iDensity = 0;
    int iOnOffBlockCount = 0;
    int iThrowBlockCount = 0;
    int iBreakableBlockCount = 0;
    int iItemDestroyableBlockCount = 0;
    int iHiddenBlockCount = 0;

    for (MovingPlatform* platform : platforms) {
        for (short iCol = 0; iCol < platform->iTileWidth; iCol++) {
            for (short iRow = 0; iRow < platform->iTileHeight; iRow++) {
                //Set the tile type flags for each tile
                TileType iType = platform->tileTypeAt(iCol, iRow);
                unsigned short iFlags = tileToFlags(iType);

                const TilesetTile& tile = platform->tileAt(iCol, iRow);

                if (tile.iID != TILESETNONE)
                    iPlatformCount++;

                if (iFlags & tile_flag_has_death)
                    iHazardCount++;

                if (iFlags & tile_flag_ice)
                    iIceCount++;
            }
        }
    }

    //Examine tiles to calculate map summary data (i.e. presence of death blocks, powerups, ice, switches, etc.)
    short numWarpExits = 0;
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            //Calculate what warp tiles belong together (any warps that have the same connection that are
            //next to each other are merged into a single warp)
            //If there are too many warps, then remove any warp encountered that is over that limit
            if (warpdata[i][j].connection != -1 && !usedtile[i][j] && warpdata[i][j].direction >= 0) {
                int movex = 0, movey = 0;
                int currentx = i, currenty = j;

                if (warpdata[i][j].direction == 0 || warpdata[i][j].direction == 2)
                    movex = 1;
                else
                    movey = 1;

                while (currentx < MAPWIDTH && currenty < MAPHEIGHT) {
                    if (warpdata[currentx][currenty].direction != warpdata[i][j].direction ||
                            warpdata[currentx][currenty].connection != warpdata[i][j].connection) {
                        break;
                    }

                    usedtile[currentx][currenty] = true;

                    if (numWarpExits < MAXWARPS) {
                        warpdata[currentx][currenty].id = numWarpExits;
                    } else {
                        warpdata[currentx][currenty].connection = -1;
                        warpdata[currentx][currenty].direction = WARP_UNDEFINED;
                    }

                    currentx += movex;
                    currenty += movey;
                }

                numWarpExits++;
            }

            short iBlockType = objectdata[i][j].iType;
            int iFlags = tileToFlags(mapdatatop[i][j]);

            //Calculate auto map filters
            if (iFlags & tile_flag_has_death)
                iHazardCount++;

            if (warpdata[i][j].connection != -1)
                iWarpCount++;

            if (iFlags & tile_flag_ice)
                iIceCount++;

            if (iBlockType == 1 || iBlockType == 15) //Powerup/View Block
                iPowerupBlockCount++;

            if (iBlockType == 0) //Breakable Block
                iBreakableBlockCount++;

            if (iBlockType == 6 || iBlockType == 16) //Blue/Red Throw Block
                iThrowBlockCount++;

            if (iBlockType >= 11 && iBlockType <= 14) //On/Off Block
                iOnOffBlockCount++;

            if (iFlags & tile_flag_solid)
                iDensity++;

            if ((iBlockType == 1 || iBlockType == 3 || iBlockType == 4 || iBlockType == 5 || iBlockType == 15 || iBlockType == 17 || iBlockType == 18) && objectdata[i][j].fHidden) //Hidden blocks
                iHiddenBlockCount++;

            if (iBlockType >= 20 && iBlockType <= 29) //Item Destroyable Blocks
                iItemDestroyableBlockCount++;
        }
    }

    //Save summary info for auto map filters (hazards, ice, warps, platforms, bonus blocks, density, etc)
    mapfile.write_i32(iHazardCount);
    mapfile.write_i32(iWarpCount);
    mapfile.write_i32(iIceCount);
    mapfile.write_i32(iPowerupBlockCount);
    mapfile.write_i32(iBreakableBlockCount);
    mapfile.write_i32(iThrowBlockCount);
    mapfile.write_i32(iOnOffBlockCount);
    mapfile.write_i32(iPlatformCount);
    mapfile.write_i32(maphazards.size());
    mapfile.write_i32(iItemDestroyableBlockCount);
    mapfile.write_i32(iHiddenBlockCount);
    mapfile.write_i32(mapitems.size());
    mapfile.write_i32(iDensity);

    //Write tileset names and indexes for translation at load time
    //Number of tilesets used by this map
    short iTilesetCount = g_tilesetmanager->count();
    std::vector<bool> fTilesetUsed(iTilesetCount > 0 ? iTilesetCount : 0, false);

    //Scan map to see how many tilesets were used
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            for (k = 0; k < MAPLAYERS; k++) {
                if (mapdata[i][j][k].iID >= 0 && mapdata[i][j][k].iID < iTilesetCount)
                    fTilesetUsed[mapdata[i][j][k].iID] = true;
            }
        }
    }

    //Scan platforms too for tilesets used
    for (MovingPlatform* platform : platforms) {
        for (short iCol = 0; iCol < platform->iTileWidth; iCol++) {
            for (short iRow = 0; iRow < platform->iTileHeight; iRow++) {
                const TilesetTile& tile = platform->tileAt(iCol, iRow);
                if (tile.iID >= 0 && tile.iID < iTilesetCount)
                    fTilesetUsed[tile.iID] = true;
            }
        }
    }

    short iUsedTilesets = 0;
    for (short iTileset = 0; iTileset < iTilesetCount; iTileset++) {
        if (fTilesetUsed[iTileset])
            iUsedTilesets++;
    }

    mapfile.write_i32(iUsedTilesets);

    //Write each of the tileset names with the index that will be used by this mapfile to represent that tileset
    for (short iTileset = 0; iTileset < iTilesetCount; iTileset++) {
        if (fTilesetUsed[iTileset]) {
            //Tileset ID
            mapfile.write_i32(iTileset);

            //Tileset Name
            mapfile.write_string_long(g_tilesetmanager->tileset(iTileset)->name().c_str());
        }
    }

    //save map tiles and blocks
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            for (k = 0; k < MAPLAYERS; k++) {
                TilesetTile * tile = &mapdata[i][j][k];

                //Make sure the tile's col and row are within the tileset
                if (tile->iID >= 0) {
                    if (tile->iCol < 0 || tile->iCol >= g_tilesetmanager->tileset(tile->iID)->width())
                        tile->iCol = 0;

                    if (tile->iRow < 0 || tile->iRow >= g_tilesetmanager->tileset(tile->iID)->height())
                        tile->iRow = 0;
                }

                mapfile.write_i8(tile->iID);
                mapfile.write_i8(tile->iCol);
                mapfile.write_i8(tile->iRow);
            }

            //Interaction blocks
            mapfile.write_i8(objectdata[i][j].iType);
            mapfile.write_bool(objectdata[i][j].fHidden);
        }
    }

    //Write background File
    mapfile.write_string_long(szBackgroundFile.c_str());

    //Save the default on/off switch states
    for (short iSwitch = 0; iSwitch < 4; iSwitch++)
        mapfile.write_i32(iSwitches[iSwitch]);

    //Write moving platforms
    mapfile.write_i32(platforms.size());

    for (MovingPlatform* platform : platforms) {
        mapfile.write_i32(platform->iTileWidth);
        mapfile.write_i32(platform->iTileHeight);

        for (short iCol = 0; iCol < platform->iTileWidth; iCol++) {
            for (short iRow = 0; iRow < platform->iTileHeight; iRow++) {
                TilesetTile tile = platform->tileAt(iCol, iRow);

                //Make sure the tile's col and row are within the tileset
                if (tile.iID >= 0) {
                    if (tile.iCol < 0 || tile.iCol >= g_tilesetmanager->tileset(tile.iID)->width())
                        tile.iCol = 0;

                    if (tile.iRow < 0 || tile.iRow >= g_tilesetmanager->tileset(tile.iID)->height())
                        tile.iRow = 0;
                }

                mapfile.write_i8(tile.iID);
                mapfile.write_i8(tile.iCol);
                mapfile.write_i8(tile.iRow);

                mapfile.write_i32(static_cast<int>(platform->tileTypeAt(iCol, iRow)));
            }
        }

        mapfile.write_i32(platform->iDrawLayer);

        short iPathType = static_cast<short>(platform->pPath->typeId());
        mapfile.write_i32(iPathType);

        if (auto* path = dynamic_cast<StraightPath*>(platform->pPath)) {
            mapfile.write_float(path->startPos().x);
            mapfile.write_float(path->startPos().y);
            mapfile.write_float(path->endPos().x);
            mapfile.write_float(path->endPos().y);
            mapfile.write_float(path->speed());
        } else if (auto* path = dynamic_cast<StraightPathContinuous*>(platform->pPath)) {
            mapfile.write_float(path->startPos().x);
            mapfile.write_float(path->startPos().y);
            mapfile.write_float(path->angle());
            mapfile.write_float(path->speed());
        } else if (auto* path = dynamic_cast<EllipsePath*>(platform->pPath)) {
            mapfile.write_float(path->radius().x);
            mapfile.write_float(path->radius().y);
            mapfile.write_float(path->centerPos().x);
            mapfile.write_float(path->centerPos().y);
            mapfile.write_float(path->startAngle());
            mapfile.write_float(path->speed());
        }
    }

    //Write map items (carried springs, spikes, kuribo's shoe, etc)
    mapfile.write_i32(mapitems.size());

    for (const MapItem& item : mapitems) {
        mapfile.write_i32(item.itype);
        mapfile.write_i32(item.ix);  //tile aligned
        mapfile.write_i32(item.iy);
    }

    //Write map hazards (fireball strings, rotodiscs, pirhana plants, etc)
    mapfile.write_i32(maphazards.size());

    for (const MapHazard& hazard : maphazards) {
        mapfile.write_i32(static_cast<int32_t>(hazard.itype));
        mapfile.write_i32(hazard.ix);
        mapfile.write_i32(hazard.iy);

        for (short iParam = 0; iParam < NUMMAPHAZARDPARAMS; iParam++)
            mapfile.write_i32(hazard.iparam[iParam]);

        for (short iParam = 0; iParam < NUMMAPHAZARDPARAMS; iParam++)
            mapfile.write_float(hazard.dparam[iParam]);
    }

    //Write eyecandy for all eyecandy layers
    mapfile.write_i32(eyecandy[0]);
    mapfile.write_i32(eyecandy[1]);
    mapfile.write_i32(eyecandy[2]);

    //Write music category
    mapfile.write_i32(musicCategoryID);

    //Write the rest of the map data later so that we can just load part of the map for the preview
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            //Write tile collision types (ice, solid, death, etc.)
            mapfile.write_i32(static_cast<int>(mapdatatop[i][j]));

            //Write per tile warp data
            mapfile.write_i32(warpdata[i][j].direction);
            mapfile.write_i32(warpdata[i][j].connection);
            mapfile.write_i32(warpdata[i][j].id);

            //Write per tile allowed spawn types (player, team specific (1-4), item)
            for (short iType = 0; iType < NUMSPAWNAREATYPES; iType++)
                mapfile.write_bool(nospawn[iType][i][j]);
        }
    }

    //Count blocks that have supplement data (like powerup weights for powerup blocks or state for switched blocks)
    short iBlockCount = 0;
    short iSwitchBlockCount = 0;
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            //powerup block
            if (objectdata[i][j].iType == 1 || objectdata[i][j].iType == 15)
                iBlockCount++;

            //switched blocks
            if (objectdata[i][j].iType >= 11 && objectdata[i][j].iType <= 14)
                iSwitchBlockCount++;
        }
    }

    //Write out the switch block state
    mapfile.write_i32(iSwitchBlockCount);
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            if (objectdata[i][j].iType >= 11 && objectdata[i][j].iType <= 14) {
                mapfile.write_i8(i);
                mapfile.write_i8(j);
                mapfile.write_i8(objectdata[i][j].iSettings[0]);
            }
        }
    }

    //Write number of warp exits
    mapfile.write_i32(numWarpExits);

    for (j = 0; j < MAPHEIGHT; j++)
        for (i = 0; i < MAPWIDTH; i++)
            usedtile[i][j] = false;

    //scan warp data and write out warp exits
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            //if we found a warp area and it hasn't been consumed by another warp area
            //then scan for surrounding warp areas and consume them to create a warp exit
            if (warpdata[i][j].connection != -1 && !usedtile[i][j] && warpdata[i][j].direction >= 0) {
                int movex = 0, movey = 0;
                int currentx = i, currenty = j;

                if (warpdata[i][j].direction == 0 || warpdata[i][j].direction == 2)
                    movex = 1;
                else
                    movey = 1;

                int numblocks = 0;
                while (currentx < MAPWIDTH && currenty < MAPHEIGHT) {
                    if (warpdata[currentx][currenty].direction != warpdata[i][j].direction ||
                            warpdata[currentx][currenty].connection != warpdata[i][j].connection) {
                        break;
                    }

                    usedtile[currentx][currenty] = true;

                    currentx += movex;
                    currenty += movey;
                    numblocks++;
                }

                //We're on the next block that isn't a warp
                currentx -= movex;
                currenty -= movey;

                mapfile.write_i32(warpdata[i][j].direction);
                mapfile.write_i32(warpdata[i][j].connection);
                mapfile.write_i32(warpdata[i][j].id);

                //Write out warp exit x,y position for player and position for lock icon to display
                if (warpdata[i][j].direction == 0) {
                    mapfile.write_i32((((currentx << 5) + TILESIZE - (i << 5)) >> 1) + (i << 5) - HALFPW);
                    mapfile.write_i32((j << 5) - 1 + PHOFFSET);

                    mapfile.write_i32((((currentx << 5) + TILESIZE - (i << 5)) >> 1) + (i << 5) - 16);
                    mapfile.write_i32((j << 5));
                } else if (warpdata[i][j].direction == 2) {
                    mapfile.write_i32((((currentx << 5) + TILESIZE - (i << 5)) >> 1) + (i << 5) - HALFPW);
                    mapfile.write_i32((j << 5) + 1 + PHOFFSET);

                    mapfile.write_i32((((currentx << 5) + TILESIZE - (i << 5)) >> 1) + (i << 5) - 16);
                    mapfile.write_i32((j << 5));
                } else if (warpdata[i][j].direction == 1) {
                    mapfile.write_i32((i << 5) + TILESIZE - PW - PWOFFSET);
                    mapfile.write_i32((currenty << 5) + TILESIZE - PH - 1);

                    mapfile.write_i32((i << 5));
                    mapfile.write_i32((((currenty << 5) + TILESIZE - (j << 5)) >> 1) + (j << 5) - 16);
                } else if (warpdata[i][j].direction == 3) {
                    mapfile.write_i32((i << 5) - 1 + PWOFFSET);
                    mapfile.write_i32((currenty << 5) + TILESIZE - PH - 1);

                    mapfile.write_i32((i << 5));
                    mapfile.write_i32((((currenty << 5) + TILESIZE - (j << 5)) >> 1) + (j << 5) - 16);
                }

                mapfile.write_i32(i);
                mapfile.write_i32(j);
                mapfile.write_i32(numblocks);
            }
        }
    }

    //Calculate player/team spawn zones
    for (short iType = 0; iType <= 5; iType++) {
        calculatespawnareas(iType, false, false);

        //Ok try to find somewhere to spawn using areas that may or may not be there
        if (numspawnareas[iType] == 0)
            calculatespawnareas(iType, true, false);

        //Ok, try to find somewhere to spawn that might be over a death tile
        if (numspawnareas[iType] == 0)
            calculatespawnareas(iType, true, true);
    }

    //Write spawn areas
    for (i = 0; i < NUMSPAWNAREATYPES; i++) {
        mapfile.write_i32(numspawnareas[i]);

        for (int m = 0; m < numspawnareas[i]; m++) {
            mapfile.write_i32(spawnareas[i][m].left);
            mapfile.write_i32(spawnareas[i][m].top);
            mapfile.write_i32(spawnareas[i][m].width);
            mapfile.write_i32(spawnareas[i][m].height);
            mapfile.write_i32(spawnareas[i][m].size);
        }
    }

    computeDrawAreas();

    //Write draw areas
    mapfile.write_i32(numdrawareas);

    for (int m = 0; m < numdrawareas; m++) {
        mapfile.write_i32(drawareas[m].x);
        mapfile.write_i32(drawareas[m].y);
        mapfile.write_i32(drawareas[m].w);
        mapfile.write_i32(drawareas[m].h);
    }

    //Write the number of blocks we have supplement info for
    mapfile.write_i32(iBlockCount);

    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            if (objectdata[i][j].iType == 1 || objectdata[i][j].iType == 15) { //powerup or view block
                mapfile.write_i8(i);
                mapfile.write_i8(j);

                mapfile.write_i8(NUM_BLOCK_SETTINGS);
                for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
                    mapfile.write_i8(objectdata[i][j].iSettings[iSetting]);
            }
        }
    }

    //Write mode item locations like flags and race goals
    mapfile.write_i32(iNumRaceGoals);
    for (j = 0; j < iNumRaceGoals; j++) {
        mapfile.write_i32(racegoallocations[j].x);
        mapfile.write_i32(racegoallocations[j].y);
    }

    mapfile.write_i32(iNumFlagBases);
    for (j = 0; j < iNumFlagBases; j++) {
        mapfile.write_i32(flagbaselocations[j].x);
        mapfile.write_i32(flagbaselocations[j].y);
    }

#if defined(__APPLE__)
    chmod(file.c_str(), S_IRWXU | S_IRWXG | S_IROTH);
#endif

    cout << "done" << endl;

}

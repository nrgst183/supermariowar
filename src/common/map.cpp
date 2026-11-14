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
#include "Resimulation.h"
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

short g_iCurrentDrawIndex = 0;


CMap::CMap()
    : warpexits()
    , tilebltrect()
    , bltrect()
    , drawareas()
    , iSwitches()
    , racegoallocations()
    , flagbaselocations()
{
    mapitems.reserve(MAXMAPITEMS);
    maphazards.reserve(MAXMAPHAZARDS);
}

CMap::~CMap()
{}


//With the new 32x30 tile set, we need to convert old maps to use the
//correct indexes into the tile set

void CMap::clearMap()
{
    int i, j, k;

    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            for (k = 0; k < MAPLAYERS; k++) {
                //reset tile
                mapdata[i][j][k].iID = TILESETNONE;  //no tile selected
            }

            mapdatatop[i][j] = TileType::NonSolid;

            objectdata[i][j].iType = -1;
            warpdata[i][j].direction = WARP_UNDEFINED;
            warpdata[i][j].connection = -1;

            for (short iSpawn = 0; iSpawn < NUMSPAWNAREATYPES; iSpawn++)
                nospawn[iSpawn][i][j] = false;
        }
    }

    eyecandy[0] = 0;
    eyecandy[1] = 0;
    eyecandy[2] = 0;

    mapitems.clear();
    maphazards.clear();

    for (short iSwitch = 0; iSwitch < 4; iSwitch++)
        iSwitches[iSwitch] = 0;

    bltrect.w = TILESIZE;
    bltrect.h = TILESIZE;
}

void CMap::clearPlatforms()
{
    for (short iLayer = 0; iLayer < 5; iLayer++)
        platformdrawlayer[iLayer].clear();

    for (MovingPlatform* platform : platforms)
        delete platform;

    platforms.clear();

    auto iter = tempPlatforms.begin(), lim = tempPlatforms.end();

    while (iter != lim) {
        delete (*iter);
        ++iter;
    }

    tempPlatforms.clear();
}

void CMap::ClearAnimatedTiles()
{
    std::vector<AnimatedTile*>::iterator iter = animatedtiles.begin(), lim = animatedtiles.end();

    while (iter != lim) {
        delete (*iter);
        ++iter;
    }

    animatedtiles.clear();
}

void CMap::loadMap(const std::string& file, ReadType iReadType)
{
    iTileAnimationTimer = 0;
    iTileAnimationFrame = 0;

    ClearAnimatedTiles();

    eyecandy[0] = 0;
    eyecandy[1] = 0;
    eyecandy[2] = 0;
    mapitems.clear();
    maphazards.clear();


    BinaryFile mapfile(file, "rb");
    if (!mapfile.is_open()) {
        cout << endl << " ERROR: Couldn't open map" << endl;
        return;
    }

    Version version;
    version.major = mapfile.read_i32();
    version.minor = mapfile.read_i32();
    version.patch = mapfile.read_i32();
    version.build = mapfile.read_i32();

    if (iReadType != read_type_summary) {
        cout << "loading map " << file;

        if (iReadType == read_type_preview)
            cout << " (preview)";

        if (version >= Version {1, 6, 0, 0}) {
            printf(" [v%d.%d.%d.%d]", version.major, version.minor, version.patch, version.build);
        }
        else
            cout << " [v1.5]";

        cout << " ...";
    }

    MapReader* reader = MapReader::getLoaderByVersion(version);
    reader->load(*this, mapfile, iReadType);
    delete reader;
    reader = nullptr;

    if (iReadType == read_type_summary)
        return;

    clearWarpLocks();
    cout << " done" << endl;
}

void CMap::UpdateAllTileGaps()
{
    //Detect 1 tile gaps between solid tiles
    for (short j = 0; j < MAPHEIGHT; j++) {
        for (short i = 0; i < MAPWIDTH; i++) {
            SetTileGap(i, j);
        }
    }
}

void CMap::UpdateTileGap(short i, short j)
{
    for (short iRow = j; iRow <= j + 1; iRow++) {
        for (short iCol = i - 1; iCol <= i + 1; iCol++) {
            if (iRow >= MAPHEIGHT)
                break;

            short ix = iCol;
            if (ix < 0)
                ix = MAPWIDTH - 1;
            else if (ix >= MAPWIDTH)
                ix = 0;

            SetTileGap(ix, iRow);
        }
    }
}

void CMap::SetTileGap(short i, short j)
{
    short iLeftTile = i - 1;
    if (iLeftTile < 0)
        iLeftTile = MAPWIDTH - 1;

    short iRightTile = i + 1;
    if (iRightTile >= MAPWIDTH)
        iRightTile = 0;

    int topLeftTile = 0;
    int topCenterTile = 0;
    int topRightTile = 0;

    IO_Block * topLeftBlock = nullptr;
    IO_Block * topCenterBlock = nullptr;
    IO_Block * topRightBlock = nullptr;

    if (j > 0) {
        topLeftTile = tileToFlags(mapdatatop[iLeftTile][j - 1]);
        topCenterTile = tileToFlags(mapdatatop[i][j - 1]);
        topRightTile = tileToFlags(mapdatatop[iRightTile][j - 1]);

        topLeftBlock = blockdata[iLeftTile][j - 1];
        topCenterBlock = blockdata[i][j - 1];
        topRightBlock = blockdata[iRightTile][j - 1];
    }

    int leftTile = tileToFlags(mapdatatop[iLeftTile][j]);
    int centerTile = tileToFlags(mapdatatop[i][j]);
    int rightTile = tileToFlags(mapdatatop[iRightTile][j]);

    IO_Block * leftBlock = blockdata[iLeftTile][j];
    IO_Block * centerBlock = blockdata[i][j];
    IO_Block * rightBlock = blockdata[iRightTile][j];

    bool fLeftSolid = (leftTile != tile_flag_nonsolid && leftTile != tile_flag_gap) || (leftBlock && !leftBlock->isTransparent() && !leftBlock->isHidden());

    //The following line used to be:
    //bool fCenterSolid = centerTile != tile_flag_nonsolid || (centerBlock && !centerBlock->isTransparent() && !centerBlock->isHidden());
    //And I don't remember why I specifically did it that way.  It was causing a bug so I changed it back to this:
    bool fCenterSolid = (centerTile != tile_flag_nonsolid && centerTile != tile_flag_gap) || (centerBlock && !centerBlock->isTransparent() && !centerBlock->isHidden());

    bool fRightSolid = (rightTile != tile_flag_nonsolid && rightTile != tile_flag_gap) || (rightBlock && !rightBlock->isTransparent() && !rightBlock->isHidden());

    bool fTopLeftSolid = (topLeftTile & tile_flag_solid) || (topLeftBlock && !topLeftBlock->isTransparent() && !topLeftBlock->isHidden());
    bool fTopCenterSolid = (topCenterTile & tile_flag_solid) || (topCenterBlock && !topCenterBlock->isTransparent() && !topCenterBlock->isHidden());
    bool fTopRightSolid = (topRightTile & tile_flag_solid) || (topRightBlock && !topRightBlock->isTransparent() && !topRightBlock->isHidden());

    if (fLeftSolid && !fCenterSolid && fRightSolid && !fTopLeftSolid && !fTopCenterSolid && !fTopRightSolid) {
        mapdatatop[i][j] = TileType::Gap;
    } else if (mapdatatop[i][j] == TileType::Gap) {
        mapdatatop[i][j] = TileType::NonSolid;
    }
}

void CMap::calculatespawnareas(short iType, bool fUseTempBlocks, bool fIgnoreDeath)
{
    bool usedtile[MAPWIDTH][MAPHEIGHT];
    short i, j;

    for (j = 0; j < MAPHEIGHT; j++)
        for (i = 0; i < MAPWIDTH; i++)
            usedtile[i][j] = false;

    //Figure out areas where spawning is allowed
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            bool fUsed = false;

            if (j >= 13 || j == 0)
                fUsed = true;

            if (!fUsed) {
                if (nospawn[iType][i][j])
                    fUsed = true;
            }

            if (!fUsed && tileToFlags(mapdatatop[i][j]) & tile_flag_solid) {
                fUsed = true;
            }

            //If there is a block there
            if (!fUsed) {
                if (objectdata[i][j].iType != -1)
                    fUsed = true;
            }

            //If this is a player spawn area, have extra restrictions like don't spawn over spikes etc.
            if (0 <= iType && 4 >= iType) {
                //If there is a death tile directly above
                if (!fUsed) {
                    if (j > 0) {
                        if (tileToFlags(mapdatatop[i][j - 1]) & tile_flag_death_on_bottom) {
                            fUsed = true;
                        }
                    }
                }

                //If there is a death tile anywhere below this tile
                if (!fUsed && !fIgnoreDeath) {
                    int m;
                    for (m = j; m < MAPHEIGHT; m++) {
                        TileType type = mapdatatop[i][m];
                        int flags = tileToFlags(type);
                        short objBlock = objectdata[i][m].iType;

                        if (m == j && (flags & tile_flag_solid_on_top))
                            continue;

                        if (type == TileType::DeathOnTop || type == TileType::Death || type == TileType::SuperDeathTop || type == TileType::SuperDeath || type == TileType::PlayerDeath) {
                            fUsed = true;
                            break;
                        }

                        if (fUseTempBlocks) {
                            if ((type != TileType::NonSolid && type != TileType::Gap) || objBlock != -1) {
                                break;
                            }
                        } else {
                            //Ignore the blocks that might not be there anymore (destroyed, turned off, etc)
                            if ((type != TileType::NonSolid && type != TileType::Gap) || (objBlock != -1 && objBlock != 0 && objBlock != 2 && objBlock != 6 && (objBlock < 11 || objBlock > 14) && objBlock != 16 && objBlock < 19)) {
                                break;
                            }
                        }
                    }

                    //If we didn't find a landing spot from here to bottom, then try to wrap around and see
                    if (m == MAPHEIGHT) {
                        for (m = 0; m < j; m++) {
                            TileType type = mapdatatop[i][m];
                            short objBlock = objectdata[i][m].iType;

                            if (type == TileType::DeathOnTop || type == TileType::Death || type == TileType::SuperDeathTop || type == TileType::SuperDeath || type == TileType::PlayerDeath) {
                                fUsed = true;
                                break;
                            }

                            if (fUseTempBlocks) {
                                if ((type != TileType::NonSolid && type != TileType::Gap) || objBlock != -1) {
                                    break;
                                }
                            } else {
                                if ((type != TileType::NonSolid && type != TileType::Gap) || (objBlock != -1 && objBlock != 0 && objBlock != 2 && objBlock != 6 && (objBlock < 11 || objBlock > 14) && objBlock != 16 && objBlock < 19)) {
                                    break;
                                }
                            }
                        }

                        //if (m == j)
                        //fUsed = true;
                    }
                }
            }

            usedtile[i][j] = fUsed;
        }
    }

    //Figure out where the spawn areas are
    numspawnareas[iType] = 0;
    for (j = 0; j < MAPHEIGHT; j++) {
        for (i = 0; i < MAPWIDTH; i++) {
            if (!usedtile[i][j]) {
                bool fDownDone = false;
                bool fRightDone = false;

                short downsize = j + 1;
                short rightsize = i + 1;
                short attempt = 1;

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
                        if (numspawnareas[iType] < MAXSPAWNAREAS) {
                            spawnareas[iType][numspawnareas[iType]].left = i;
                            spawnareas[iType][numspawnareas[iType]].top = j;
                            spawnareas[iType][numspawnareas[iType]].width = rightsize - 1 - i;
                            spawnareas[iType][numspawnareas[iType]].height = downsize - 1 - j;
                            spawnareas[iType][numspawnareas[iType]].size = (rightsize - i) * (downsize - j);

                            numspawnareas[iType]++;
                        }

                        for (short down = j; down < downsize; down++) {
                            for (short right = i; right < rightsize; right++) {
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

void CMap::updatePlatforms()
{
    for (MovingPlatform* platform : platforms) {
        platform->update();
    }

    auto iter = tempPlatforms.begin(), lim = tempPlatforms.end();

    while (iter != lim) {
        if ((*iter)->fDead) {
            delete (*iter);

            iter = tempPlatforms.erase(iter);
            lim = tempPlatforms.end();
        } else {
            (*iter)->update();
            ++iter;
        }
    }
}

void CMap::movingPlatformCollision(IO_MovingObject * object)
{
    for (MovingPlatform* platform : platforms) {
        platform->collide(object);
    }

    auto iterateAll = tempPlatforms.begin(), lim = tempPlatforms.end();
    while (iterateAll != lim) {
        (*iterateAll)->collide(object);
        iterateAll++;
    }
}

bool CMap::movingPlatformCheckSides(IO_MovingObject * object)
{
    bool fRet = false;
    for (MovingPlatform* platform : platforms) {
        fRet |= platform->collision_detection_check_sides(object);
    }

    auto iterateAll = tempPlatforms.begin(), lim = tempPlatforms.end();
    while (iterateAll != lim) {
        fRet |= (*iterateAll)->collision_detection_check_sides(object);
        iterateAll++;
    }

    return fRet;
}

void CMap::resetPlatforms()
{
    for (MovingPlatform* platform : platforms) {
        platform->ResetPath();
    }

    auto iter = tempPlatforms.begin(), lim = tempPlatforms.end();

    while (iter != lim) {
        delete (*iter);
        ++iter;
    }

    tempPlatforms.clear();
}

void CMap::lockconnection(int connection)
{
    //Lock all warp connections
    if (connection == -1) {
        for (short iConnection = 0; iConnection <= maxConnection; iConnection++) {
            warplocked[iConnection] = true;
        }
    } else { //otherwise just lock the one connection
        warplocked[connection] = true;
    }
}

WarpExit * CMap::getRandomWarpExit(int connection, int currentID)
{
    int indices[MAXWARPS];
    int numIndices = 0;

    WarpExit * currentWarp = nullptr;

    for (int k = 0; k < numwarpexits; k++) {
        if (warpexits[k].connection == connection) {
            if (warpexits[k].id == currentID)
                currentWarp = &warpexits[k];
            else
                indices[numIndices++] = k;
        }
    }

    if (numIndices == 0)
        return currentWarp;

    return &warpexits[indices[RANDOM_INT( numIndices)]];
}

void CMap::clearWarpLocks()
{
    for (short iConnection = 0; iConnection < 10; iConnection++) {
        warplocktimer[iConnection] = 0;
        warplocked[iConnection] = false;
    }

    for (short iWarpExit = 0; iWarpExit < numwarpexits; iWarpExit++) {
        warpexits[iWarpExit].locktimer = 0;
    }
}

void CMap::update()
{
    //Unlock locked warps if the time is up
    for (short iConnection = 0; iConnection <= maxConnection; iConnection++) {
        if (warplocked[iConnection]) {
            if (++warplocktimer[iConnection] > game_values.warplocktime) {
                warplocked[iConnection] = false;
                warplocktimer[iConnection] = 0;
            }
        }
    }

    //If warp is individually locked, then reduce lock timer
    for (short iWarpExit = 0; iWarpExit < numwarpexits; iWarpExit++) {
        if (warpexits[iWarpExit].locktimer > 0)
            --warpexits[iWarpExit].locktimer;
    }

    //Animating the tiles is cosmetic (not part of rollback snapshots): a
    //re-simulated tick doesn't advance it again
    if (sim::isResimulating())
        return;

    //Animate the animated tiles
    if (++iTileAnimationTimer >= NUM_FRAMES_BETWEEN_TILE_ANIMATION) {
        iTileAnimationTimer = 0;

        //Flip front and back buffers
        animatedFrontmapSurface = rm->spr_frontmap[g_iCurrentDrawIndex].getSurface();
        animatedBackmapSurface = rm->spr_backmap[g_iCurrentDrawIndex].getSurface();

        g_iCurrentDrawIndex = 1 - g_iCurrentDrawIndex;

        if (++iTileAnimationFrame >= NUM_FRAMES_IN_TILE_ANIMATION)
            iTileAnimationFrame = 0;
    }

    //If there is at least 1 animated tile, then draw its animation
    if (iAnimatedTileCount > 0)
        AnimateTiles(iTileAnimationTimer);
}

bool CMap::findspawnpoint(short iType, short * x, short * y, short width, short height, bool tilealigned)
{
    if (totalspawnsize[iType] <= 0) {
		*x = RANDOM_INT(App::screenWidth);
		*y = RANDOM_INT(App::screenHeight * 0.87f);
        return true;
    }

    int spawnarea = RANDOM_INT(totalspawnsize[iType]);

    int currentsize = 0;
    for (int m = 0; m < numspawnareas[iType]; m++) {
        currentsize += spawnareas[iType][m].size;

        if (spawnarea >= currentsize)
            continue;

        short areawidth = (spawnareas[iType][m].width << 5) + TILESIZE;
        short areaheight = (spawnareas[iType][m].height << 5) + TILESIZE;

        if (width > areawidth || height > areaheight)
            continue;

        if (tilealigned) {
            short xoffset = spawnareas[iType][m].width;
            short yoffset = spawnareas[iType][m].height;

            if (xoffset > 0)
                xoffset = static_cast<short>(RANDOM_INT(xoffset));

            if (yoffset > 0)
                yoffset = static_cast<short>(RANDOM_INT(yoffset));

            *x = (xoffset << 5) + (spawnareas[iType][m].left << 5) + (TILESIZE >> 1) - (width >> 1);
            *y = (yoffset << 5) + (spawnareas[iType][m].top << 5) + (TILESIZE >> 1) - (height >> 1);
        } else {
            short xoffset = areawidth - width - 2;
            short yoffset = areaheight - height - 2;

            if (xoffset > 0)
                xoffset = static_cast<short>(RANDOM_INT(xoffset)) + 1;

            if (yoffset > 0)
                yoffset = static_cast<short>(RANDOM_INT(yoffset)) + 1;

            *x = xoffset + (spawnareas[iType][m].left << 5);
            *y = yoffset + (spawnareas[iType][m].top << 5);
        }

        break;
    }
    //Check to see if we are spawning into a platform
    for (MovingPlatform* platform : platforms) {
        if (platform->IsInNoSpawnZone(*x, *y, width, height))
            return false;
    }

    //Check to see if we are spawning into a temporary (falling) platform
    auto iterateAll = tempPlatforms.begin(), lim = tempPlatforms.end();
    while (iterateAll != lim) {
        if ((*iterateAll)->IsInNoSpawnZone(*x, *y, width, height))
            return false;

        iterateAll++;
    }

    return true;
}

void CMap::AddPermanentPlatform(MovingPlatform * platform)
{
    platforms.emplace_back(platform);
    platformdrawlayer[platform->iDrawLayer].push_back(platform);
}

void CMap::AddTemporaryPlatform(MovingPlatform * platform)
{
    tempPlatforms.push_back(platform);
}

bool CMap::IsInPlatformNoSpawnZone(short x, short y, short width, short height)
{
    for (MovingPlatform* platform : platforms) {
        if (platform->IsInNoSpawnZone(x, y, width, height))
            return true;
    }

    auto iterateAll = tempPlatforms.begin(), lim = tempPlatforms.end();
    while (iterateAll != lim) {
        if ((*iterateAll)->IsInNoSpawnZone(x, y, width, height))
            return true;

        iterateAll++;
    }

    return false;
}

bool CMap::checkforwarp(short iData1, short iData2, short iData3, short iDirection)
{
    Warp * warp1 = nullptr;
    Warp * warp2 = nullptr;

    if (iDirection == 0 || iDirection == 2) {
        warp1 = &warpdata[iData1][iData3];
        warp2 = &warpdata[iData2][iData3];
    } else {
        warp1 = &warpdata[iData1][iData2];
        warp2 = &warpdata[iData1][iData3];
    }

    return warp1->direction == warp2->direction
        && warp1->id == warp2->id
        && warp1->direction == iDirection
        && !warplocked[warp1->connection]
        && warpexits[warp1->id].locktimer <= 0;
}

void CMap::optimize()
{
    for (int j = 0; j < MAPHEIGHT; j++) {
        for (int i = 0; i < MAPWIDTH; i++) {
            for (int m = 1; m < MAPLAYERS; m++) {
                TilesetTile * tile = &mapdata[i][j][m];
                TileType type = g_tilesetmanager->tileset(tile->iID)->tileType(tile->iCol, tile->iRow);
                if (type != TileType::NonSolid && type != TileType::Gap && type != TileType::SolidOnTop) {
                    for (int k = m - 1; k >= 0; k--) {
                        TilesetTile * compareTile = &mapdata[i][j][k];
                        if (compareTile->iID == TILESETNONE) {
                            TilesetTile * fromTile = &mapdata[i][j][k + 1];
                            compareTile->iID = fromTile->iID;
                            compareTile->iCol = fromTile->iCol;
                            compareTile->iRow = fromTile->iRow;

                            fromTile->iID = TILESETNONE;
                            fromTile->iCol = 0;
                            fromTile->iRow = 0;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }
}

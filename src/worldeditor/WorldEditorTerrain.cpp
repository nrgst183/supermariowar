#include "WorldEditorState.h"

void GetForegroundTileValues(short iCol, short iRow, short iOldTiles[9])
{
    short iIndex = 0;
    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                iOldTiles[iIndex++] = g_worldmap.getTile(iAutoCol, iAutoRow).iForegroundSprite;
            }
        }
    }

    for (short i = iIndex; i < 9; i++)
        iOldTiles[i] = 0;
}

bool ForegroundTileValuesChanged(short iCol, short iRow, short iOldTiles[9])
{
    short iIndex = 0;
    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                if (g_worldmap.getTile(iAutoCol, iAutoRow).iForegroundSprite != iOldTiles[iIndex++])
                    return true;
            }
        }
    }

    return false;
}

void UpdatePathSprite(short iCol, short iRow)
{
    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                AutoSetPathSprite(iAutoCol, iAutoRow);
            }
        }
    }
}

void AutoSetPathSprite(short iCol, short iRow)
{
    short iPathTypes[16] = {6, 4, 3, 5, 6, 4, 1, 5, 6, 2, 3, 5, 6, 4, 3, 5};

    short iPath = 0;
    short iNeighborIndex = 0;

    short iForegroundSprite = g_worldmap.getTile(iCol, iRow).iForegroundSprite;
    short iForegroundStyle = iForegroundSprite / WORLD_PATH_SPRITE_SET_SIZE;

    if (iForegroundSprite == 0 || iForegroundSprite >= WORLD_FOREGROUND_STAGE_OFFSET)
        return;

    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoCol == iCol && iAutoRow == iRow)
                continue;

            if ((iAutoCol == iCol && iAutoRow != iRow) || (iAutoCol != iCol && iAutoRow == iRow)) {
                if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                    iForegroundSprite = g_worldmap.getTile(iAutoCol, iAutoRow).iForegroundSprite;

                    if ((iForegroundSprite >= WORLD_BRIDGE_SPRITE_OFFSET && iForegroundSprite <= WORLD_BRIDGE_SPRITE_OFFSET + 3) ||
                        (iForegroundSprite >= WORLD_FOREGROUND_STAGE_OFFSET && iForegroundSprite <= WORLD_FOREGROUND_STAGE_OFFSET + 399) ||
                        /*(iForegroundSprite >= WORLD_FOREGROUND_SPRITE_OFFSET && iForegroundSprite <= WORLD_FOREGROUND_SPRITE_OFFSET + 179) ||*/
                        /*(iForegroundSprite >= WORLD_FOREGROUND_SPRITE_ANIMATED_OFFSET && iForegroundSprite <= WORLD_FOREGROUND_SPRITE_ANIMATED_OFFSET + 29) ||*/
                            (iForegroundSprite >= WORLD_START_SPRITE_OFFSET && iForegroundSprite <= WORLD_START_SPRITE_OFFSET + 1)) {
                        iPath += 1 << iNeighborIndex;
                    } else if (iForegroundSprite >= 0 && iForegroundSprite < WORLD_FOREGROUND_STAGE_OFFSET) {
                        short iPathSprite = iForegroundSprite % WORLD_PATH_SPRITE_SET_SIZE;

                        if (iPathSprite >= 1 && iPathSprite <= 18)
                            iPath += 1 << iNeighborIndex;
                    }
                }

                iNeighborIndex++;
            }
        }
    }

    //#1 == -  2 == |  3 == -o  4 == !  5 == -`  6 == o
    g_worldmap.getTile(iCol, iRow).iForegroundSprite = AdjustForeground(iPathTypes[iPath] + iForegroundStyle * WORLD_PATH_SPRITE_SET_SIZE, iCol, iRow);
}

//Convert foreground sprite to match the background sprite
short AdjustForeground(short iSprite, short iCol, short iRow)
{
    if (iSprite >= WORLD_FOREGROUND_STAGE_OFFSET)
        return iSprite;

    short iBackgroundSprite = g_worldmap.getTile(iCol, iRow).iBackgroundSprite % WORLD_BACKGROUND_SPRITE_SET_SIZE;

    short iPathStyle = iSprite / WORLD_PATH_SPRITE_SET_SIZE;
    iSprite %= WORLD_PATH_SPRITE_SET_SIZE;

    //Convert already adjusted sprites back to their "base" sprite
    if (iSprite >= 11 && iSprite <= 18)
        iSprite = 2 - (iSprite % 2);
    else if (iSprite >= 7 && iSprite <= 10)
        iSprite -= 4;

    if (iBackgroundSprite == 1)
        return iSprite + iPathStyle * WORLD_PATH_SPRITE_SET_SIZE;

    if (iSprite == 2 && (iBackgroundSprite == 12 || iBackgroundSprite == 20 || iBackgroundSprite == 23 ||
        iBackgroundSprite == 24 || iBackgroundSprite == 27 || iBackgroundSprite == 32 || iBackgroundSprite == 36 ||
                        iBackgroundSprite == 37 || iBackgroundSprite == 40)) {
        iSprite = 14;
    } else if (iSprite == 2 && (iBackgroundSprite == 13 || iBackgroundSprite == 21 || iBackgroundSprite == 22 ||
        iBackgroundSprite == 25 || iBackgroundSprite == 26 || iBackgroundSprite == 33 || iBackgroundSprite == 34 ||
                               iBackgroundSprite == 35 || iBackgroundSprite == 41)) {
        iSprite = 16;
    } else if (iSprite == 1 && (iBackgroundSprite == 14 || iBackgroundSprite == 18 || iBackgroundSprite == 19 ||
        iBackgroundSprite == 26 || iBackgroundSprite == 27 || iBackgroundSprite == 31 || iBackgroundSprite == 34 ||
                               iBackgroundSprite == 37 || iBackgroundSprite == 39)) {
        iSprite = 13;
    } else if (iSprite == 1 && (iBackgroundSprite == 15 || iBackgroundSprite == 16 || iBackgroundSprite == 17 ||
        iBackgroundSprite == 24 || iBackgroundSprite == 25 || iBackgroundSprite == 30 || iBackgroundSprite == 35 ||
                               iBackgroundSprite == 36 || iBackgroundSprite == 38)) {
        iSprite = 15;
    } else if (iSprite == 2 && (iBackgroundSprite == 28 || iBackgroundSprite == 38 || iBackgroundSprite == 39 ||
                               iBackgroundSprite == 43)) {
        iSprite = 12;
    } else if (iSprite == 1 && (iBackgroundSprite == 28 || iBackgroundSprite == 40 || iBackgroundSprite == 41 ||
                               iBackgroundSprite == 42)) {
        iSprite = 11;
    } else {
        if (iSprite == 1 || iSprite == 2)
            iSprite += 16;
        else if (iSprite >= 3 && iSprite <= 6)
            iSprite += 4;
    }

    return iSprite + iPathStyle * WORLD_PATH_SPRITE_SET_SIZE;
}

void UpdatePath(short iCol, short iRow)
{
    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                AutoSetPath(iAutoCol, iAutoRow);
            }
        }
    }
}

void AutoSetPath(short iCol, short iRow)
{
    short iPathTypes[16] = {11, 1, 2, 3, 2, 4, 2, 8, 1, 1, 6, 7, 5, 9, 10, 11};

    short iPath = 0;
    short iNeighborIndex = 0;

    if (g_worldmap.getTile(iCol, iRow).iConnectionType == 0)
        return;

    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoCol == iCol && iAutoRow == iRow)
                continue;

            if ((iAutoCol == iCol && iAutoRow != iRow) || (iAutoCol != iCol && iAutoRow == iRow)) {
                if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                    if (g_worldmap.getTile(iAutoCol, iAutoRow).iConnectionType > 0)
                        iPath += 1 << iNeighborIndex;
                }

                iNeighborIndex++;
            }
        }
    }

    //#1 == |  2 == -  3 == -!  4 == L  5 == ,-  6 == -,
    //#7 == -|  8 == -`-  9 == |-  10 == -,-  11 == +

    short iPathType = iPathTypes[iPath];
    short iForegroundSprite = g_worldmap.getTile(iCol, iRow).iForegroundSprite;

    if (iPathType == 2 && iForegroundSprite >= WORLD_BRIDGE_SPRITE_OFFSET && iForegroundSprite <= WORLD_BRIDGE_SPRITE_OFFSET + 1) {
        iPathType = iForegroundSprite - WORLD_BRIDGE_SPRITE_OFFSET + 12;
    } else if (iPathType == 1 && iForegroundSprite >= WORLD_BRIDGE_SPRITE_OFFSET + 2 && iForegroundSprite <= WORLD_BRIDGE_SPRITE_OFFSET + 3) {
        iPathType = iForegroundSprite - WORLD_BRIDGE_SPRITE_OFFSET + 12;
    }

    g_worldmap.getTile(iCol, iRow).iConnectionType = iPathType;
}

bool UpdateForeground(short iCol, short iRow)
{
    short iNewForeground = AdjustForeground(g_worldmap.getTile(iCol, iRow).iForegroundSprite, iCol, iRow);

    if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != iNewForeground) {
        g_worldmap.getTile(iCol, iRow).iForegroundSprite = iNewForeground;
        return true;
    }

    return false;
}

bool UpdateCoastline(short iCol, short iRow)
{
    short iStartCol = iCol == 0 ? 0 : iCol - 1;
    short iEndCol = iCol == iWorldWidth - 1 ? iWorldWidth - 1 : iCol + 1;

    short iStartRow = iRow == 0 ? 0 : iRow - 1;
    short iEndRow = iRow == iWorldHeight - 1 ? iWorldHeight - 1 : iRow + 1;

    bool fRet = false;
    for (short iAutoRow = iStartRow; iAutoRow <= iEndRow; iAutoRow++) {
        for (short iAutoCol = iStartCol; iAutoCol <= iEndCol; iAutoCol++) {
            fRet |= AutoSetTile(iAutoCol, iAutoRow);
            fRet |= UpdateForeground(iAutoCol, iAutoRow);
        }
    }

    return fRet;
}

bool AutoSetTile(short iCol, short iRow)
{
    //Don't need to do anything if this tile is solid
    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite % WORLD_BACKGROUND_SPRITE_SET_SIZE == 1)
        return false;

    bool iTile[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    short iNeighborIndex = 0;

    short iNeighborStyle[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (short iAutoRow = iRow - 1; iAutoRow <= iRow + 1; iAutoRow++) {
        for (short iAutoCol = iCol - 1; iAutoCol <= iCol + 1; iAutoCol++) {
            if (iAutoCol == iCol && iAutoRow == iRow)
                continue;

            if (iAutoRow >= 0 && iAutoRow < iWorldHeight && iAutoCol >= 0 && iAutoCol < iWorldWidth) {
                short iBackgroundSprite = g_worldmap.getTile(iAutoCol, iAutoRow).iBackgroundSprite;

                if (iBackgroundSprite % WORLD_BACKGROUND_SPRITE_SET_SIZE == 1) {
                    iTile[iNeighborIndex] = true;
                    iNeighborStyle[iBackgroundSprite / WORLD_BACKGROUND_SPRITE_SET_SIZE]++;
                }
            }

            iNeighborIndex++;
        }
    }

    short iMaxStyle = 0;
    short iTileStyleOffset = 0;
    for (short iStyle = 0; iStyle < 10; iStyle++) {
        if (iNeighborStyle[iStyle] > iMaxStyle) {
            iMaxStyle = iNeighborStyle[iStyle];
            iTileStyleOffset = iStyle * WORLD_BACKGROUND_SPRITE_SET_SIZE;
        }
    }

    short iNewTile = 0;
    if (iTile[0] && !iTile[1] && iTile[2] && !iTile[3] && !iTile[4] && iTile[5] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 44;
    } else if (iTile[0] && !iTile[1] && !iTile[3] && iTile[4] && iTile[5] && !iTile[6]) {
        iNewTile = iTileStyleOffset + 30;
    } else if (!iTile[1] && iTile[2] && iTile[3] && !iTile[4] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 31;
    } else if (iTile[1] && !iTile[3] && !iTile[4] && iTile[5] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 32;
    } else if (iTile[0] && !iTile[1] && iTile[2] && !iTile[3] && !iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 33;
    } else if (!iTile[1] && iTile[2] && iTile[3] && !iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 34;
    } else if (iTile[0] && !iTile[1] && !iTile[3] && iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 35;
    } else if (iTile[1] && !iTile[3] && iTile[4] && iTile[5] && !iTile[6]) {
        iNewTile = iTileStyleOffset + 36;
    } else if (iTile[1] && iTile[3] && !iTile[4] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 37;
    } else if (iTile[0] && !iTile[1] && iTile[2] && !iTile[4] && iTile[7]) {
        iNewTile = iTileStyleOffset + 45;
    } else if (iTile[2] && !iTile[4] && iTile[5] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 46;
    } else if (iTile[0] && !iTile[3] && iTile[5] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 47;
    } else if (iTile[0] && !iTile[1] && iTile[2] && !iTile[3] && iTile[5]) {
        iNewTile = iTileStyleOffset + 48;
    } else if (iTile[1] && iTile[3] && iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 28;
    } else if (iTile[1] && !iTile[3] && iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 38;
    } else if (iTile[1] && iTile[3] && !iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 39;
    } else if (iTile[1] && iTile[3] && iTile[4] && !iTile[6]) {
        iNewTile = iTileStyleOffset + 40;
    } else if (!iTile[1] && iTile[3] && iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 41;
    } else if (!iTile[1] && iTile[3] && iTile[4] && !iTile[6]) {
        iNewTile = iTileStyleOffset + 42;
    } else if (iTile[1] && !iTile[3] && !iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 43;
    } else if (iTile[0] && !iTile[1] && iTile[4]) {
        iNewTile = iTileStyleOffset + 16;
    } else if (iTile[4] && iTile[5] && !iTile[6]) {
        iNewTile = iTileStyleOffset + 17;
    } else if (iTile[3] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 18;
    } else if (!iTile[1] && iTile[2] && iTile[3]) {
        iNewTile = iTileStyleOffset + 19;
    } else if (iTile[1] && !iTile[4] && iTile[7]) {
        iNewTile = iTileStyleOffset + 20;
    } else if (iTile[2] && !iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 21;
    } else if (iTile[0] && !iTile[3] && iTile[6]) {
        iNewTile = iTileStyleOffset + 22;
    } else if (iTile[1] && !iTile[3] && iTile[5]) {
        iNewTile = iTileStyleOffset + 23;
    } else if (iTile[0] && !iTile[1] && iTile[2]) {
        iNewTile = iTileStyleOffset + 6;
    } else if (iTile[5] && !iTile[6] && iTile[7]) {
        iNewTile = iTileStyleOffset + 7;
    } else if (iTile[0] && !iTile[3] && iTile[5]) {
        iNewTile = iTileStyleOffset + 8;
    } else if (iTile[2] && !iTile[4] && iTile[7]) {
        iNewTile = iTileStyleOffset + 9;
    } else if (iTile[1] && iTile[4]) {
        iNewTile = iTileStyleOffset + 24;
    } else if (iTile[4] && iTile[6]) {
        iNewTile = iTileStyleOffset + 25;
    } else if (iTile[3] && iTile[6]) {
        iNewTile = iTileStyleOffset + 26;
    } else if (iTile[1] && iTile[3]) {
        iNewTile = iTileStyleOffset + 27;
    } else if (iTile[2] && iTile[5]) {
        iNewTile = iTileStyleOffset + 10;
    } else if (iTile[0] && iTile[7]) {
        iNewTile = iTileStyleOffset + 11;
    } else if (iTile[1]) {
        iNewTile = iTileStyleOffset + 12;
    } else if (iTile[6]) {
        iNewTile = iTileStyleOffset + 13;
    } else if (iTile[3]) {
        iNewTile = iTileStyleOffset + 14;
    } else if (iTile[4]) {
        iNewTile = iTileStyleOffset + 15;
    } else if (iTile[2]) {
        iNewTile = iTileStyleOffset + 2;
    } else if (iTile[0]) {
        iNewTile = iTileStyleOffset + 3;
    } else if (iTile[5]) {
        iNewTile = iTileStyleOffset + 4;
    } else if (iTile[7]) {
        iNewTile = iTileStyleOffset + 5;
    } else {
        iNewTile = iTileStyleOffset + 0;
    }

    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != iNewTile) {
        g_worldmap.getTile(iCol, iRow).iBackgroundSprite = iNewTile;
        return true;
    }

    return false;
}

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

//[Direction][Frame]
SDL_Rect g_rFlameRects[4][4] = { { {0, 0, 96, 32}, {0, 32, 96, 32}, {0, 64, 96, 32}, {0, 96, 96, 32} },
    { {96, 0, 96, 32}, {96, 32, 96, 32}, {96, 64, 96, 32}, {96, 96, 96, 32} },
    { {0, 128, 32, 96}, {32, 128, 32, 96}, {64, 128, 32, 96}, {96, 128, 32, 96} },
    { {128, 128, 32, 96}, {160, 128, 32, 96}, {192, 128, 32, 96}, {224, 128, 32, 96} }
};

//[Type][Direction][Frame]
SDL_Rect g_rPirhanaRects[4][4][4] = {
    {   { {0, 0, 32, 48}, {32, 0, 32, 48}, {64, 0, 32, 48}, {96, 0, 32, 48} },
        { {128, 0, 32, 48}, {160, 0, 32, 48}, {192, 0, 32, 48}, {224, 0, 32, 48} },
        { {304, 0, 48, 32}, {304, 32, 48, 32}, {304, 64, 48, 32}, {304, 96, 48, 32} },
        { {304, 128, 48, 32}, {304, 160, 48, 32}, {304, 192, 48, 32}, {304, 224, 48, 32} }
    },

    {   { {0, 48, 32, 48}, {32, 48, 32, 48}, {64, 48, 32, 48}, {96, 48, 32, 48} },
        { {128, 48, 32, 48}, {160, 48, 32, 48}, {192, 48, 32, 48}, {224, 48, 32, 48} },
        { {256, 0, 48, 32}, {256, 32, 48, 32}, {256, 64, 48, 32}, {256, 96, 48, 32} },
        { {256, 128, 48, 32}, {256, 160, 48, 32}, {256, 192, 48, 32}, {256, 224, 48, 32} }
    },

    {   { {0, 96, 32, 64}, {32, 96, 32, 64}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {64, 96, 32, 64}, {96, 96, 32, 64}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {192, 128, 64, 32}, {192, 160, 64, 32}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {192, 192, 64, 32}, {192, 224, 64, 32}, {0, 0, 0, 0}, {0, 0, 0, 0} }
    },

    {   { {0, 160, 32, 48}, {32, 160, 32, 48}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {64, 160, 32, 48}, {96, 160, 32, 48}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {144, 128, 48, 32}, {144, 160, 48, 32}, {0, 0, 0, 0}, {0, 0, 0, 0} },
        { {144, 192, 48, 32}, {144, 224, 48, 32}, {0, 0, 0, 0}, {0, 0, 0, 0} }
    }
};

short iPlatformPathDotOffset[3] = {0, 12, 18};
short iPlatformPathDotSize[3] = {12, 6, 4};

short iFireballHazardSize[3] = {18, 9, 5};

short iStandardOffset[3] = {0, 32, 48};
float dBulletBillFrequency[3] = {10.0f, 5.0f, 2.5f};


namespace {
int GetScreenWidth(int iSize) {
    switch (iSize) {
        case 0: return App::screenWidth;
        case 1: return App::screenWidth / 2;
        case 2: return App::screenWidth / 4;
        default: throw "invalid iSize for width";
    }
}

int GetScreenHeight(int iSize) {
    switch (iSize) {
        case 0: return App::screenHeight;
        case 1: return App::screenHeight / 2;
        case 2: return App::screenHeight / 4;
        default: throw "invalid iSize for height";
    }
}
} // namespace


void DrawMapHazard(const MapHazard& hazard, short iSize, bool fDrawCenter, SDL_Surface* dst)
{
    short iSizeShift = 5 - iSize;
    short iTileSize = 1 << iSizeShift;

    SDL_Rect rDotSrc = {iPlatformPathDotOffset[iSize] + 22, 0, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]}, rDotDst;
    SDL_Rect rPathSrc = {iStandardOffset[iSize], 12, iTileSize, iTileSize}, rPathDst;

    rPathDst = {hazard.ix << (iSizeShift - 1), hazard.iy << (iSizeShift - 1), iTileSize, iTileSize};

    switch (hazard.itype) {
        case MapHazardType::FireBar: {
            if (fDrawCenter)
                rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);

            short iNumDots = 16;
            float dRadius = static_cast<float>((hazard.iparam[0] - 1) * 24) / static_cast<float>(1 << iSize) + (iPlatformPathDotSize[iSize] >> 1);
            float dAngle = hazard.dparam[1];
            for (short iDot = 0; iDot < iNumDots; iDot++) {
                rDotDst.x = static_cast<short>(dRadius * cos(dAngle)) + rPathDst.x + (iTileSize >> 1) - (iPlatformPathDotSize[iSize] >> 1);
                rDotDst.y = static_cast<short>(dRadius * sin(dAngle)) + rPathDst.y + (iTileSize >> 1) - (iPlatformPathDotSize[iSize] >> 1);
                rDotDst.h = rDotDst.w = iPlatformPathDotSize[iSize];

                rm->spr_platformpath.draw(rDotDst.x, rDotDst.y, {rDotSrc.x, rDotSrc.y, rDotDst.w, rDotDst.h});
                dAngle += TWO_PI / iNumDots;
            }

            //Draw the fireball string
            for (short iFireball = 0; iFireball < hazard.iparam[0]; iFireball++) {
                short x = (hazard.ix << (iSizeShift - 1)) + static_cast<short>(static_cast<float>(iFireball * (24 >> iSize)) * cos(hazard.dparam[1])) + (iTileSize >> 1) - (iFireballHazardSize[iSize] >> 1);
                short y = (hazard.iy << (iSizeShift - 1)) + static_cast<short>(static_cast<float>(iFireball * (24 >> iSize)) * sin(hazard.dparam[1])) + (iTileSize >> 1) - (iFireballHazardSize[iSize] >> 1);

                rm->spr_hazard_fireball[iSize].draw(x, y, {0, 0, iFireballHazardSize[iSize], iFireballHazardSize[iSize]});
            }
            break;
        }

        case MapHazardType::RotoDisc: {
            if (fDrawCenter)
                rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);

            short iNumDots = 16;
            float dRadius = (hazard.dparam[2] + (iTileSize >> 1) - (iPlatformPathDotSize[iSize] >> 1)) / static_cast<float>(1 << iSize);
            float dAngle = hazard.dparam[1];
            for (short iDot = 0; iDot < iNumDots; iDot++) {
                rDotDst.x = static_cast<short>(dRadius * cos(dAngle)) + rPathDst.x + (iTileSize >> 1) - (iPlatformPathDotSize[iSize] >> 1);
                rDotDst.y = static_cast<short>(dRadius * sin(dAngle)) + rPathDst.y + (iTileSize >> 1) - (iPlatformPathDotSize[iSize] >> 1);
                rDotDst.h = rDotDst.w = iPlatformPathDotSize[iSize];

                rm->spr_platformpath.draw(rDotDst.x, rDotDst.y, {rDotSrc.x, rDotSrc.y, rDotDst.w, rDotDst.h});
                dAngle += TWO_PI / iNumDots;
            }

            //Draw the rotodiscs
            float dSector = TWO_PI / hazard.iparam[0];
            dAngle = hazard.dparam[1];
            dRadius = hazard.dparam[2] / static_cast<float>(1 << iSize);
            for (short iRotodisc = 0; iRotodisc < hazard.iparam[0]; iRotodisc++) {
                short x = rPathDst.x + static_cast<short>(dRadius * cos(dAngle));
                short y = rPathDst.y + static_cast<short>(dRadius * sin(dAngle));

                rm->spr_hazard_rotodisc[iSize].draw(x, y, {0, 0, iTileSize, iTileSize});

                dAngle += dSector;
            }
            break;
        }

        case MapHazardType::BulletBill: {
            rm->spr_hazard_bulletbill[iSize].draw(rPathDst.x, rPathDst.y, {0, hazard.dparam[0] < 0.0f ? 0 : iTileSize, iTileSize, iTileSize});

            short iBulletPathX = rPathDst.x - iPlatformPathDotSize[iSize];
            if (hazard.dparam[0] > 0.0f)
                iBulletPathX = rPathDst.x + iTileSize;

            short iBulletPathSpacing = static_cast<short>(hazard.dparam[0] * dBulletBillFrequency[iSize]);
            while (iBulletPathX >= 0 && iBulletPathX < GetScreenWidth(iSize)) {
                rDotDst = {iBulletPathX, rPathDst.y + ((iTileSize - iPlatformPathDotSize[iSize]) >> 1), iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
                rm->spr_platformpath.draw(rDotSrc, dst, rDotDst);

                iBulletPathX += hazard.iparam[0] < 0.0f ? -iBulletPathSpacing : iBulletPathSpacing;
            }
            break;
        }

        case MapHazardType::FlameCannon: {
            const SDL_Rect * rect = &g_rFlameRects[hazard.iparam[1]][2];

            short iOffsetX = 0;
            short iOffsetY = 0;

            if (hazard.iparam[1] == 1) {
                iOffsetX = -(iTileSize << 1);
            } else if (hazard.iparam[1] == 2) {
                iOffsetY = -(iTileSize << 1);
            }

            rm->spr_hazard_flame[iSize].draw(rPathDst.x + iOffsetX, rPathDst.y + iOffsetY, {rect->x >> iSize, rect->y >> iSize, rect->w >> iSize, rect->h >> iSize});
            break;
        }

        case MapHazardType::PirhanaGreen:
        case MapHazardType::PirhanaRed:
        case MapHazardType::PirhanaTall:
        case MapHazardType::PirhanaShort: {
            const short iPirhanaPlantType = static_cast<short>(hazard.itype) - static_cast<short>(MapHazardType::PirhanaGreen);
            const SDL_Rect * rect = &g_rPirhanaRects[iPirhanaPlantType][hazard.iparam[1]][0];
            short iOffsetX = 0;
            short iOffsetY = 0;

            if (hazard.iparam[1] == 0) {
                if (hazard.itype == MapHazardType::PirhanaTall)
                    iOffsetY = -iTileSize;
                else
                    iOffsetY = -(iTileSize >> 1);
            } else if (hazard.iparam[1] == 2) {
                if (hazard.itype == MapHazardType::PirhanaTall)
                    iOffsetX = -iTileSize;
                else
                    iOffsetX = -(iTileSize >> 1);
            }

            rm->spr_hazard_pirhanaplant[iSize].draw(rPathDst.x + iOffsetX, rPathDst.y + iOffsetY, {rect->x >> iSize, rect->y >> iSize, rect->w >> iSize, rect->h >> iSize});
            break;
        }
    }
}

void DrawPlatform(
    PlatformPathType pathtype,
    std::span<const TilesetTile> tiles,
    short startX, short startY,
    short endX, short endY,
    float angle, float radiusX, float radiusY,
    short iSize, short iPlatformWidth, short iPlatformHeight,
    bool fDrawPlatform, bool fDrawShadow,
    SDL_Surface* dst)
{
    short iStartX = startX >> iSize;
    short iStartY = startY >> iSize;
    short iEndX = endX >> iSize;
    short iEndY = endY >> iSize;

    float fRadiusX = radiusX / static_cast<float>(1 << iSize);
    float fRadiusY = radiusY / static_cast<float>(1 << iSize);

    short iSizeShift = 5 - iSize;
    int iTileSize = 1 << iSizeShift;

    if (fDrawPlatform) {
        for (short iPlatformX = 0; iPlatformX < iPlatformWidth; iPlatformX++) {
            for (short iPlatformY = 0; iPlatformY < iPlatformHeight; iPlatformY++) {
                const TilesetTile& tile = tiles[iPlatformX * iPlatformHeight + iPlatformY];

                int iDstX = 0;
                int iDstY = 0;

                if (pathtype == PlatformPathType::Ellipse) {
                    iDstX = iStartX + (iPlatformX << iSizeShift) + static_cast<short>(fRadiusX * cos(angle)) - (iPlatformWidth << (iSizeShift - 1));
                    iDstY = iStartY + (iPlatformY << iSizeShift) + static_cast<short>(fRadiusY * sin(angle)) - (iPlatformHeight << (iSizeShift - 1));
                } else {
                    iDstX = iStartX + (iPlatformX << iSizeShift) - (iPlatformWidth << (iSizeShift - 1));
                    iDstY = iStartY + (iPlatformY << iSizeShift) - (iPlatformHeight << (iSizeShift - 1));
                }

                SDL_Rect bltrect = {iDstX, iDstY, iTileSize, iTileSize};
                if (tile.iID >= 0) {
                    const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), tile.iCol, tile.iRow);
                    g_tilesetmanager->tileset(tile.iID)->draw(static_cast<DrawSize>(iSize), srcRect, dst, bltrect);
                } else if (tile.iID == TILESETANIMATED) {
                    const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), tile.iCol * 4, tile.iRow);
                    rm->spr_tileanimation[iSize].draw(srcRect, dst, bltrect);
                } else if (tile.iID == TILESETUNKNOWN) {
                    //Draw unknown tile
                    const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), 0, 0);
                    rm->spr_unknowntile[iSize].draw(srcRect, dst, bltrect);
                }

                bool fNeedWrap = false;
                if (iDstX + iTileSize >= GetScreenWidth(iSize)) {
                    iDstX -= GetScreenWidth(iSize);
                    fNeedWrap = true;
                } else if (iDstX < 0) {
                    iDstX += GetScreenWidth(iSize);
                    fNeedWrap = true;
                }

                if (fNeedWrap) {
                    bltrect.x = iDstX;
                    bltrect.y = iDstY;
                    bltrect.w = iTileSize;
                    bltrect.h = iTileSize;

                    if (tile.iID >= 0) {
                        const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), tile.iCol, tile.iRow);
                        g_tilesetmanager->tileset(tile.iID)->draw(static_cast<DrawSize>(iSize), srcRect, dst, bltrect);
                    } else if (tile.iID == TILESETANIMATED) {
                        const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), tile.iCol * 4, tile.iRow);
                        rm->spr_tileanimation[iSize].draw(srcRect, dst, bltrect);
                    } else if (tile.iID == TILESETUNKNOWN) {
                        const SDL_Rect& srcRect = CTilesetManager::rect(static_cast<DrawSize>(iSize), 0, 0);
                        rm->spr_unknowntile[iSize].draw(srcRect, dst, bltrect);
                    }
                }
            }
        }
    }

    SDL_Rect rPathSrc = {iPlatformPathDotOffset[iSize], 0, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]}, rPathDst;

    if (pathtype == PlatformPathType::Straight) {
        if (fDrawShadow) {
            for (short iCol = 0; iCol < iPlatformWidth; iCol++) {
                for (short iRow = 0; iRow < iPlatformHeight; iRow++) {
                    if (tiles[iCol * iPlatformHeight + iRow].iID != -2)
                        rm->spr_platformstarttile.draw(iStartX - (iPlatformWidth << (iSizeShift - 1)) + (iCol << iSizeShift), iStartY - (iPlatformHeight << (iSizeShift - 1)) + (iRow << iSizeShift), {0, 0, iTileSize, iTileSize});
                }
            }

            for (short iCol = 0; iCol < iPlatformWidth; iCol++) {
                for (short iRow = 0; iRow < iPlatformHeight; iRow++) {
                    if (tiles[iCol * iPlatformHeight + iRow].iID != -2)
                        rm->spr_platformendtile.draw(iEndX - (iPlatformWidth << (iSizeShift - 1)) + (iCol << iSizeShift), iEndY - (iPlatformHeight << (iSizeShift - 1)) + (iRow << iSizeShift), {0, 0, iTileSize, iTileSize});
                }
            }
        }

        //Draw connecting dots
        float dDiffX = static_cast<float>(iEndX - iStartX);
        float dDiffY = static_cast<float>(iEndY - iStartY);

        short iDistance = static_cast<short>(sqrt(dDiffX * dDiffX + dDiffY * dDiffY));

        short iNumSpots = (iDistance >> iSizeShift);
        float dIncrementX = dDiffX / static_cast<float>(iNumSpots);
        float dIncrementY = dDiffY / static_cast<float>(iNumSpots);

        float dX = static_cast<float>(iStartX) - static_cast<float>(iPlatformPathDotSize[iSize] >> 1);
        float dY = static_cast<float>(iStartY) - static_cast<float>(iPlatformPathDotSize[iSize] >> 1);

        for (short iSpot = 0; iSpot < iNumSpots + 1; iSpot++) {
            rPathDst = {static_cast<short>(dX), static_cast<short>(dY), iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
            rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);

            dX += dIncrementX;
            dY += dIncrementY;
        }
    } else if (pathtype == PlatformPathType::StraightContinuous) {
        if (fDrawShadow) {
            for (short iCol = 0; iCol < iPlatformWidth; iCol++) {
                for (short iRow = 0; iRow < iPlatformHeight; iRow++) {
                    if (tiles[iCol * iPlatformHeight + iRow].iID != -2)
                        rm->spr_platformstarttile.draw(iStartX - (iPlatformWidth << (iSizeShift - 1)) + (iCol << iSizeShift), iStartY - (iPlatformHeight << (iSizeShift - 1)) + (iRow << iSizeShift), {0, 0, iTileSize, iTileSize});
                }
            }
        }

        float dIncrementX = static_cast<float>(iTileSize) * cos(angle);
        float dIncrementY = static_cast<float>(iTileSize) * sin(angle);

        float dX = static_cast<float>(iStartX) - static_cast<float>(iPlatformPathDotSize[iSize] >> 1);
        float dY = static_cast<float>(iStartY) - static_cast<float>(iPlatformPathDotSize[iSize] >> 1);

        for (short iSpot = 0; iSpot < 50; iSpot++) {
            rPathDst = {static_cast<short>(dX), static_cast<short>(dY), iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
            rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);

            short iWrapX = static_cast<short>(dX);
            short iWrapY = static_cast<short>(dY);
            bool fNeedWrap = false;
            if (dX + iPlatformPathDotSize[iSize] >= GetScreenWidth(iSize)) {
                iWrapX = static_cast<short>(dX - GetScreenWidth(iSize));
                fNeedWrap = true;
            } else if (dX < 0.0f) {
                iWrapX = static_cast<short>(dX + GetScreenWidth(iSize));
                fNeedWrap = true;
            }

            if (dY + iPlatformPathDotSize[iSize] >= GetScreenHeight(iSize)) {
                iWrapY = static_cast<short>(dY - GetScreenHeight(iSize));
                fNeedWrap = true;
            } else if (dY < 0.0f) {
                iWrapY = static_cast<short>(dY + GetScreenHeight(iSize));
                fNeedWrap = true;
            }

            if (fNeedWrap) {
                rPathDst = {iWrapX, iWrapY, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
                rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);
            }

            dX += dIncrementX;
            dY += dIncrementY;
        }
    } else if (pathtype == PlatformPathType::Ellipse) {
        //Calculate the starting position
        if (fDrawShadow) {
            short iEllipseStartX = static_cast<short>(fRadiusX * cos(angle)) - (iPlatformWidth << (iSizeShift - 1)) + iStartX;
            short iEllipseStartY = static_cast<short>(fRadiusY * sin(angle)) - (iPlatformHeight << (iSizeShift - 1)) + iStartY;

            for (short iCol = 0; iCol < iPlatformWidth; iCol++) {
                for (short iRow = 0; iRow < iPlatformHeight; iRow++) {
                    if (tiles[iCol * iPlatformHeight + iRow].iID != -2)
                        rm->spr_platformstarttile.draw(iEllipseStartX + (iCol << iSizeShift), iEllipseStartY + (iRow << iSizeShift), {0, 0, iTileSize, iTileSize});
                }
            }
        }

        float fAngle = angle;
        for (short iSpot = 0; iSpot < 32; iSpot++) {
            short iX = static_cast<short>(fRadiusX * cos(fAngle)) - (iPlatformPathDotSize[iSize] >> 1) + iStartX;
            short iY = static_cast<short>(fRadiusY * sin(fAngle)) - (iPlatformPathDotSize[iSize] >> 1) + iStartY;

            rPathDst = {iX, iY, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
            rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);

            if (iX + iPlatformPathDotSize[iSize] >= GetScreenWidth(iSize)) {
                rPathDst = {iX - GetScreenWidth(iSize), iY, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
                rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);
            } else if (iX < 0) {
                rPathDst = {iX + GetScreenWidth(iSize), iY, iPlatformPathDotSize[iSize], iPlatformPathDotSize[iSize]};
                rm->spr_platformpath.draw(rPathSrc, dst, rPathDst);
            }

            fAngle += TWO_PI / 32.0f;
        }
    }
}

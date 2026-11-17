#include "WorldEditorState.h"

void DrawMessage()
{
    if (g_messagedisplaytimer > 0) {
        --g_messagedisplaytimer;

        spr_dialog.draw(224, 176, {0, 0, 192, 128});
        rm->menu_font_large.drawCentered(320, 195, g_szMessageTitle.c_str());
        rm->menu_font_large.drawCentered(320, 220, g_szMessageLine[0].c_str());
        rm->menu_font_large.drawCentered(320, 240, g_szMessageLine[1].c_str());
        rm->menu_font_large.drawCentered(320, 260, g_szMessageLine[2].c_str());
    }
}

void updateworldsurface()
{
    g_worldmap.DrawMapToSurface(-1, true, sMapSurface.getSurface(), draw_offset_col, draw_offset_row, 0);
}

void drawmap(bool fScreenshot, short iBlockSize)
{
    if (fNeedBlackBackground)
        SDL_FillSurfaceRect(screen, nullptr, 0x0);

    sMapSurface.draw(rectSrcSurface, blitdest, rectDstSurface);
}

//take screenshots in full and thumbnail sizes
void takescreenshot()
{
    short iTileSizes[3] = {32, 16, 8};

    for (short iScreenshotSize = 0; iScreenshotSize < 3; iScreenshotSize++) {
        short iTileSize = iTileSizes[iScreenshotSize];
        g_worldmap = loadWorldMap(worldlist->at(game_values.worldindex).string(), iTileSize);

        short w, h;
        g_worldmap.GetWorldSize(&w, &h);

        //Draw most of the world to screenshot
        auto sScreenshot = gfxSprite::blank(iTileSize * w, iTileSize * h);
        blitdest = sScreenshot.getSurface();

        g_worldmap.DrawMapToSurface(sScreenshot.getSurface());

        //Draw vehicles to screenshot
        auto itr = vehiclelist.begin(), lim = vehiclelist.end();
        while (itr != lim) {
            WorldVehicle * vehicle = itr->get();

            short ix = vehicle->getCurrentTile().x * iTileSize;
            short iy = vehicle->getCurrentTile().y * iTileSize;

            rm->spr_worldvehicle[iScreenshotSize].draw(ix, iy, {vehicle->getDrawDirection() * iTileSize, vehicle->getDrawSprite() * iTileSize, iTileSize, iTileSize});

            itr++;
        }

        //Draw warps to screenshot
        auto itrWarp = warplist.begin(), limWarp = warplist.end();
        while (itrWarp != limWarp) {
            WorldWarp * warp = itrWarp->get();

            if (warp->posA.x >= 0)
                spr_warps[iScreenshotSize].draw(warp->posA.x * iTileSize, warp->posA.y * iTileSize, {warp->id * iTileSize, 0, iTileSize, iTileSize});

            if (warp->posB.x >= 0)
                spr_warps[iScreenshotSize].draw(warp->posB.x * iTileSize, warp->posB.y * iTileSize, {warp->id * iTileSize, 0, iTileSize, iTileSize});

            itrWarp++;
        }

        //Save the screenshot with the same name as the map file
        std::string szSaveFile("worlds/screenshots/");
        szSaveFile += GetNameFromFileName(worldlist->currentPath().string());

        if (iTileSize == PREVIEWTILESIZE)
            szSaveFile += "_preview";
        else if (iTileSize == THUMBTILESIZE)
            szSaveFile += "_thumb";

        szSaveFile += ".png";
        IMG_SavePNG(sScreenshot.getSurface(), convertPath(szSaveFile).c_str());
    }

    g_worldmap = loadWorldMap(worldlist->at(game_values.worldindex).string(), iTileSizes[0]);
    blitdest = screen;
}

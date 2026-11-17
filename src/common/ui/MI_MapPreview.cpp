#include "MI_MapPreview.h"

#include "GameValues.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "objecthazard.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "objects/IO_FlameCannon.h"
#include "objects/moving/MO_BulletBill.h"
#include "objects/moving/MO_PirhanaPlant.h"
#include "objects/overmap/WO_OrbitHazard.h"
#include "objects/overmap/WO_StraightPathHazard.h"

#include <cstring>

MI_MapPreview::MI_MapPreview(gfxSprite * nspr, short x, short y, short width, short indent)
    : UI_Control(x, y)
    , spr(nspr)
    , iWidth(width)
    , iIndent(indent)
    , iSlideListOut(0)
{
    surfaceMapBackground = gfxSprite::blank(App::screenWidth/2, App::screenHeight/2);
    surfaceMapBlockLayer = gfxSprite::blank(App::screenWidth/2, App::screenHeight/2);
    surfaceMapForeground = gfxSprite::blank(App::screenWidth/2, App::screenHeight/2);
    LoadCurrentMap();

    rectDst.x = x + 16;
    rectDst.y = y + 44;
    rectDst.w = App::screenWidth/2;
    rectDst.h = App::screenHeight/2;

    szMapName.clear();
}

void MI_MapPreview::Update()
{
    //Update hazards
    g_sim.noncolcontainer.update();

    g_sim.objectcontainer[1].update();
    g_sim.objectcontainer[1].cleanDeadObjects();

    g_map->updatePlatforms();
}

void MI_MapPreview::Draw()
{
    if (!m_visible)
        return;

    short iMapBoxX = m_pos.x + (iWidth >> 1) - 176 - iSlideListOut;

    //Draw the background for the map preview
    rm->menu_dialog.draw(iMapBoxX, m_pos.y + 30, {0, 0, 336, 254});
    rm->menu_dialog.draw(iMapBoxX + 336, m_pos.y + 30, {496, 0, 16, 254});
    rm->menu_dialog.draw(iMapBoxX, m_pos.y + 284, {0, 464, 336, 16});
    rm->menu_dialog.draw(iMapBoxX + 336, m_pos.y + 284, {496, 464, 16, 16});

    rectDst.x = iMapBoxX + 16;

    DrawContent();
}

void MI_MapPreview::DrawContent()
{
    surfaceMapBackground.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 0);

    surfaceMapBlockLayer.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 1);

    //Draw map hazards
    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
        if (auto* hazard = dynamic_cast<OMO_OrbitHazard*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<OMO_StraightPathHazard*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<IO_FlameCannon*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<MO_BulletBill*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<MO_PirhanaPlant*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        }
    }

    g_map->drawPlatforms(rectDst.x, rectDst.y, 2);

    if (game_values.toplayer)
        surfaceMapForeground.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 3);
    g_map->drawPlatforms(rectDst.x, rectDst.y, 4);
}

void MI_MapPreview::LoadCurrentMap()
{
    szMapName = maplist->currentShortmapname();

    LoadMap(maplist->currentFilename());
}

void MI_MapPreview::LoadMap(const std::string& szMapPath)
{
    g_map->loadMap(szMapPath, read_type_preview);

    LoadCurrentMapBackground();

    g_map->preDrawPreviewBackground(rm->spr_background, surfaceMapBackground, false);

    g_map->preDrawPreviewBlocks(surfaceMapBlockLayer, false);

    g_map->preDrawPreviewMapItems(surfaceMapBackground, false);

    g_map->preDrawPreviewForeground(surfaceMapForeground, false);

    g_map->preDrawPreviewWarps(game_values.toplayer ? surfaceMapForeground : surfaceMapBackground, false);

    LoadMapHazards(true);
}

bool MI_MapPreview::SetMap(const char * paramSzMapName, bool fWorld)
{
    bool fFound = maplist->findexact(paramSzMapName, fWorld);
    LoadCurrentMap();

    return fFound;
}

void MI_MapPreview::SetSpecialMap(const char * mapName, const char * szMapPath)
{
    szMapName = mapName;

    //(a path below the data directory)
    LoadMap(convertPath(szMapPath));
}

void MI_MapPreview::SetDimensions(short width, short indent)
{
    iWidth = width;
    iIndent = indent;
}

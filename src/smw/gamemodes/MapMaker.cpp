#include "MapMaker.h"

#include "GameValues.h"
#include "GSGameplay.h"
#include "TilesetManager.h"

#include <cstring>

CGM_MapMaker::CGM_MapMaker()
    : CGameMode()
{
    gamemode = game_mode_mapmaker;
    goal = -1;
    SetupModeStrings("Map Maker", "Kills", 5);
}

void CGM_MapMaker::init()
{
    CGameMode::init();
    hostSlot = static_cast<short>(matchedit::hostSlot());
    //(zeroed whole, the padding between the fields too, then set field by field: the state is compared
    //byte for byte by the rollback checks, a copied struct's padding would be garbage)
    std::memset(static_cast<void*>(builders), 0, sizeof(builders));
    std::memset(static_cast<void*>(items), 0, sizeof(items));
    std::memset(static_cast<void*>(hazards), 0, sizeof(hazards));
    std::memset(static_cast<void*>(platforms), 0, sizeof(platforms));
    std::memset(static_cast<void*>(edits), 0, sizeof(edits));
    editCount = 0;
    std::memset(static_cast<void*>(bulks), 0, sizeof(bulks));
    bulkCount = 0;
    history.clear();
    redo.clear();
    for (int slot = 0; slot < 4; slot++) {
        rights[slot] = slot == hostSlot || game_values.mapmaker.builders == 1;
        MapMakerBuilder& b = builders[slot];
        b.cx = MAPWIDTH / 2;
        b.cy = MAPHEIGHT / 2;
        b.layer = 1;
        b.contents = -1;
        b.typeBrush = 1;
        matchedit::defaultSettings(b);
        if (g_tilesetmanager)
            b.tile.iID = static_cast<short>(g_tilesetmanager->classicTilesetIndex());
    }
    itemCount = 0;
    for (const MapItem& item : g_map->items()) {
        if (itemCount < MAXMAPITEMS) {
            MapItem& to = items[itemCount++];
            to.itype = item.itype;
            to.ix = item.ix;
            to.iy = item.iy;
        }
    }
    hazardCount = 0;
    for (const MapHazard& hazard : g_map->hazards()) {
        if (hazardCount < MAXMAPHAZARDS) {
            MapHazard& to = hazards[hazardCount++];
            to.itype = hazard.itype;
            to.ix = hazard.ix;
            to.iy = hazard.iy;
            for (int p = 0; p < NUMMAPHAZARDPARAMS; p++) {
                to.iparam[p] = hazard.iparam[p];
                to.dparam[p] = hazard.dparam[p];
            }
        }
    }
    platformCount = 0;
    edited = game_values.mapmaker.blank != 0;    //(a blank map: nothing of it saved yet)
    started = false;
    background = -1;
    music = -1;
    for (int k = 0; k < 3; k++)
        eyecandy[k] = g_map->eyecandy[k];
    for (int k = 0; k < 4; k++)
        switches[k] = g_map->switches()[k];
    goalCount = g_map->iNumRaceGoals > 0 ? 8 : 0;
    for (int k = 0; k < 8; k++) {
        goals[k][0] = g_map->raceGoals()[k].x;
        goals[k][1] = g_map->raceGoals()[k].y;
    }
    baseCount = g_map->iNumFlagBases > 0 ? 4 : 0;
    for (int k = 0; k < 4; k++) {
        bases[k][0] = g_map->flagBases()[k].x;
        bases[k][1] = g_map->flagBases()[k].y;
    }
}

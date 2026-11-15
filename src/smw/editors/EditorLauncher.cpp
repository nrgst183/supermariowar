#include "EditorLauncher.h"

#include "EditorInput.h"
#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "map.h"
#include "MapList.h"
#include "ResourceManager.h"
#include "WorldTourStop.h"
#include "world.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

extern CGameValues game_values;

namespace leveleditor { void inner_main(); }
namespace worldeditor { void inner_main(); }

namespace editors {
namespace {

// The game's objects that an editor replaces with its own during a session
struct GameObjects {
    CResourceManager* rm = nullptr;
    CMap* map = nullptr;
    MapList* maps = nullptr;
    FiltersList* filters = nullptr;
    GraphicsList* menuPacks = nullptr;
    GraphicsList* gamePacks = nullptr;
    WorldList* worlds = nullptr;
};

GameObjects take()
{
    GameObjects g {rm, g_map, maplist, filterslist, menugraphicspacklist, gamegraphicspacklist, worldlist};
    return g;
}

// Deletes what the editor created (where it replaced the game's) and puts the game's back
void giveBack(const GameObjects& g)
{
    auto restore = [](auto*& global, auto* game) {
        if (global != game)
            delete global;
        global = game;
    };
    restore(rm, g.rm);
    restore(g_map, g.map);
    restore(maplist, g.maps);
    restore(filterslist, g.filters);
    restore(menugraphicspacklist, g.menuPacks);
    restore(gamegraphicspacklist, g.gamePacks);
    restore(worldlist, g.worlds);
}

// The window's close button during a session: the game quits afterwards
bool SDLCALL watchQuit(void* userdata, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        *static_cast<bool*>(userdata) = true;
    return true;
}

// The maps and worlds the editor saved: the game's lists again (the
// selections stay)
void rescanLists()
{
    const std::string currentMap = maplist->isEmpty() ? std::string() : maplist->currentShortmapname();
    MapList* maps = new MapList(false);
    if (maps->isEmpty()) {
        delete maps;    //(nothing to show: keep the old list)
    } else {
        delete maplist;
        maplist = maps;
        maplist->ApplyFilters(game_values.pfFilters);
        if (!currentMap.empty())
            maplist->findexact(currentMap.c_str(), false);
    }

    delete worldlist;
    worldlist = new WorldList();
    if (worldlist->count() > 0)
        game_values.worldindex = static_cast<short>(std::clamp<int>(game_values.worldindex, 0, static_cast<int>(worldlist->count()) - 1));
}

} // namespace

bool run(Editor editor)
{
    const char* name = editor == Editor::Level ? "level" : "world";
    printf("editors: starting the %s editor\n", name);

    const GameObjects game = take();
    const CGameValues savedValues = game_values;
    WorldMap savedWorld = std::move(g_worldmap);
    const bool fullscreen = game_values.fullscreen;

    bool quit = false;
    SDL_AddEventWatch(watchQuit, &quit);
    input::begin(editor);

    try {
        if (editor == Editor::Level)
            leveleditor::inner_main();
        else
            worldeditor::inner_main();
    } catch (const char* what) {
        printf("editors: the %s editor stopped: %s\n", name, what);
    } catch (const std::string& what) {
        printf("editors: the %s editor stopped: %s\n", name, what.c_str());
    } catch (const std::exception& ex) {
        printf("editors: the %s editor stopped: %s\n", name, ex.what());
    }

    input::end();
    SDL_RemoveEventWatch(watchQuit, &quit);

    //The world editor's tour stops (a loaded world's stages) aren't the game's
    std::vector<TourStop*> editorStops = game_values.tourstops;
    giveBack(game);
    game_values = savedValues;
    for (TourStop* stop : editorStops) {
        if (std::find(game_values.tourstops.begin(), game_values.tourstops.end(), stop) == game_values.tourstops.end())
            delete stop;
    }
    g_worldmap = std::move(savedWorld);
    game_values.playerInput.ResetKeys();

    rescanLists();

    //The editors keep their own fullscreen setting
    if (gfx_isfullscreen() != fullscreen)
        gfx_changefullscreen(fullscreen);
    blitdest = screen;
    gfx_settitle(TITLESTRING);

    printf("editors: back from the %s editor%s\n", name, quit ? " (the window was closed)" : "");
    return !quit;
}

} // namespace editors

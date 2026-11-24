#include "EditorLauncher.h"

#include "EditorInput.h"
#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "map.h"
#include "MapList.h"
#include "ResourceManager.h"
#include "Platform.h"
#include "WorldTourStop.h"
#include "world.h"
#include "world/WorldMapIO.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

extern CGameValues game_values;

namespace leveleditor { void inner_main(); void releaseSessionGraphics(); }
namespace worldeditor { void inner_main(); void releaseSessionGraphics(); }

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

// (the last session's graphics: see takeKeptGraphics)
Editor g_keptEditor = Editor::Level;
CResourceManager* g_keptGraphics = nullptr;

// Deletes what the editor created (where it replaced the game's) and puts the game's back; the
// editor's graphics are kept (takeKeptGraphics)
void giveBack(const GameObjects& g, Editor editor)
{
    if (rm != g.rm) {
        freeKeptGraphics();
        g_keptEditor = editor;
        g_keptGraphics = rm;
        rm = g.rm;
    }
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

bool g_running = false;
bool g_closeRequested = false;    // the quit events sent from now on are requestClose()'s
int g_closeFrames = 0;

// The window's close button during a session: the game quits afterwards
bool SDLCALL watchQuit(void* userdata, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT && !g_closeRequested)
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

CResourceManager* takeKeptGraphics(Editor editor)
{
    if (g_keptGraphics && g_keptEditor == editor) {
        CResourceManager* kept = g_keptGraphics;
        g_keptGraphics = nullptr;
        return kept;
    }
    freeKeptGraphics();
    return nullptr;
}

void freeKeptGraphics()
{
    delete g_keptGraphics;
    g_keptGraphics = nullptr;
}

void requestClose()
{
    if (!g_running)
        return;
    //(a quit event closes the editor's screens; one in the middle of something else may take another)
    if (g_closeRequested && ++g_closeFrames % 30 != 0)
        return;
    g_closeRequested = true;
    SDL_Event event {};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

bool run(Editor editor)
{
    const char* name = editor == Editor::Level ? "level" : "world";
    printf("editors: starting the %s editor\n", name);
    platform::setCrashContext(editor == Editor::Level ? "The map editor" : "The world editor");
    g_running = true;
    g_closeRequested = false;
    g_closeFrames = 0;

    //(low memory: the editor loads graphics of its own; the game's menus', matches' and worlds' go
    //first, and come back as the menus draw them)
    if (platform::lowMemory()) {
        rm->freeMenuGraphicsForMatch(false);
        gfxSprite::unloadGroup(SpriteGroup::Game);
    }
    //(the editor's images: 16 bits on the Xbox, as the game's; only drawn from)
    const gfxSprite::CompactLoading compactImages;
    const GameObjects game = take();
    g_savedMapFiles.clear();
    g_savedWorldFiles.clear();
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
    g_running = false;
    g_closeRequested = false;
    //(a quit event of ours still queued isn't the game's)
    SDL_FlushEvent(SDL_EVENT_QUIT);
    //(nor are the presses still queued: the editor leaves as the press that chose "Yes" is read, and
    //the rest of that frame's events, the controller's own for that button among them, would reach the
    //menus as a press of the button the editor was opened from: the editor again)
    for (const SDL_EventType type : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP, SDL_EVENT_TEXT_INPUT, SDL_EVENT_MOUSE_BUTTON_DOWN,
             SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_HAT_MOTION,
             SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_EVENT_GAMEPAD_AXIS_MOTION})
        SDL_FlushEvent(type);

    //The world editor's tour stops (a loaded world's stages) aren't the game's
    std::vector<TourStop*> editorStops = game_values.tourstops;
    giveBack(game, editor);
    game_values = savedValues;
    for (TourStop* stop : editorStops) {
        if (std::find(game_values.tourstops.begin(), game_values.tourstops.end(), stop) == game_values.tourstops.end())
            delete stop;
    }
    g_worldmap = std::move(savedWorld);
    game_values.playerInput.ResetKeys();

    //(the lists made again only for a map or world the game's lists don't have: scanning the folders
    //is slow from a disc, and every session saves its scratch file, which is listed after the first)
    bool newFile = false;
    for (const std::string& file : g_savedMapFiles)
        newFile = newFile || !maplist->hasFile(file);
    for (const std::string& file : g_savedWorldFiles) {
        bool listed = false;
        for (size_t k = 0; k < worldlist->count(); k++)
            listed = listed || worldlist->at(k).string() == file;
        newFile = newFile || !listed;
    }
    if (newFile)
        rescanLists();

    //The editors keep their own fullscreen setting
    if (gfx_isfullscreen() != fullscreen)
        gfx_changefullscreen(fullscreen);
    blitdest = screen;
    gfx_settitle(TITLESTRING);

    printf("editors: back from the %s editor%s\n", name, quit ? " (the window was closed)" : "");
    platform::setCrashContext("The menus");
    //(low memory: not kept for the next session: the menus' graphics need the memory; nor the
    //editor's own)
    if (platform::lowMemory()) {
        freeKeptGraphics();
        if (editor == Editor::Level)
            leveleditor::releaseSessionGraphics();
        else
            worldeditor::releaseSessionGraphics();
    }
    return !quit;
}

} // namespace editors

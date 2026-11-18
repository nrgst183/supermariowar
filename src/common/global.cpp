#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "ResourceManager.h"
#include "TilesetManager.h"

#include <cstdio>
#include <algorithm>
#include <array>

// main game directory, read from command line argument
#ifdef __ANDROID__
std::string RootDataDirectory = GetHomeDirectory() + "data";
#else
std::string RootDataDirectory = GetRootDirectory() + "data";
#endif

CResourceManager *rm;
CGameValues game_values;

FiltersList *filterslist;  //Filters list must be initiallized before maps list because it is used in maplist constructor
MapList *maplist;
SkinList *skinlist;
AnnouncerList *announcerlist;
MusicList *musiclist;
WorldMusicList *worldmusiclist;
GraphicsList *menugraphicspacklist;
GraphicsList *worldgraphicspacklist;
GraphicsList *gamegraphicspacklist;
SoundsList *soundpacklist;
TourList *tourlist;
WorldList *worldlist;

CMap      *g_map;
CTilesetManager *g_tilesetmanager;

//Koopa Bros Weapons
//Mushrooms Only
//Super Mario Bros (All mushrooms ((poison was in lost levels, that counts as SMB1)), fireflower, star, and maybe the shells)
//Super Mario Bros 2 (All mushrooms but poison, star, clock, pow, green/red shell, bombs)
//One I'd like to see is SMWorld (esc{sp}) only stuff...
//0,10,0,1,0,10,6,0,0,0,2,0,8,4,2,4,0,10,0,5,2,0,0


//Conversion from backgrounds to music categories created by NMcCoy
const char * g_szMusicCategoryNames[MAXMUSICCATEGORY] = {"Land", "Underground", "Underwater", "Castle", "Platforms", "Ghost", "Bonus", "Battle", "Desert", "Clouds", "Snow"};


namespace {
//The background loaded last (many maps share one: it is still loaded)
std::string s_loadedBackgroundPath;
const SDL_Surface* s_loadedBackgroundSurface = nullptr;
} // namespace

std::string mapBackgroundPath(const CMap& map)
{
    std::string path = convertPath("gfx/packs/backgrounds/" + map.szBackgroundFile, gamegraphicspacklist->currentPath());

    //if the background file doesn't exist (or a broken map names none: its folder), use the classic background
    if (map.szBackgroundFile.empty() || map.szBackgroundFile.back() == '/' || !FileExists(path)) {
        fprintf(stderr, "[map] Background '%s' isn't installed: using the classic one\n", map.szBackgroundFile.c_str());
        path = convertPath("gfx/packs/backgrounds/Land_Classic.png", gamegraphicspacklist->currentPath());
    }
    return path;
}

const gfxSprite* loadedMapBackground(const std::string& path)
{
    if (path == s_loadedBackgroundPath && rm->spr_background.getSurface() && rm->spr_background.getSurface() == s_loadedBackgroundSurface)
        return &rm->spr_background;
    return nullptr;
}

void setLoadedMapBackground(const std::string& path, gfxSprite&& image)
{
    rm->spr_background = std::move(image);
    s_loadedBackgroundPath = path;
    s_loadedBackgroundSurface = rm->spr_background.getSurface();
}

void LoadCurrentMapBackground()
{
    mapPreviewWaitForLoads();
    const std::string path = mapBackgroundPath(*g_map);
    if (loadedMapBackground(path))
        return;
    setLoadedMapBackground(path, ImageLoader(path).withoutColorKey().create());
}

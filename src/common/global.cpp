#include "FileList.h"
#include "GameValues.h"
#include "gfx.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "ResourceManager.h"
#include "TilesetManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace {
BackgroundAnimation s_backgroundAnimation;
} // namespace

void setLoadedMapBackground(const std::string& path, gfxSprite&& image)
{
    rm->spr_background = std::move(image);
    s_loadedBackgroundPath = path;
    s_loadedBackgroundSurface = rm->spr_background.getSurface();
    s_backgroundAnimation = BackgroundAnimation::of(path, rm->spr_background);
}

const BackgroundAnimation& mapBackgroundAnimation()
{
    return s_backgroundAnimation;
}

int BackgroundAnimation::frameAt(unsigned tick) const
{
    if (ticks.size() < 2 || loopTicks <= 0)
        return 0;
    int t = static_cast<int>(tick % static_cast<unsigned>(loopTicks));
    for (size_t k = 0; k < ticks.size(); k++) {
        if (t < ticks[k])
            return static_cast<int>(k);
        t -= ticks[k];
    }
    return 0;
}

BackgroundAnimation BackgroundAnimation::of(const std::string& imagePath, const gfxSprite& image)
{
    BackgroundAnimation animation;
    const int h = image.getSurface() ? image.getHeight() : 0;
    if (h <= FRAME_H || h % FRAME_H != 0)
        return animation;    //(a still background: one screen)
    const int frames = h / FRAME_H;

    //The timing: "Name.txt" next to "Name.png"
    std::vector<int> given;
    const size_t dot = imagePath.find_last_of('.');
    if (FILE* f = fopen((imagePath.substr(0, dot == std::string::npos ? imagePath.size() : dot) + ".txt").c_str(), "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (char* hash = strchr(line, '#'))
                *hash = '\0';
            for (char* word = strtok(line, " ,;\t\r\n"); word; word = strtok(nullptr, " ,;\t\r\n")) {
                const int n = atoi(word);
                if (n > 0)
                    given.push_back(std::min(n, 60 * 62));    //(a minute at most)
            }
        }
        fclose(f);
    }
    for (int k = 0; k < frames; k++) {
        //(one number: every frame's; fewer than the frames: the last one for the rest)
        const int t = given.empty() ? DEFAULT_TICKS : given[std::min<size_t>(static_cast<size_t>(k), given.size() - 1)];
        animation.ticks.push_back(t);
        animation.loopTicks += t;
    }
    printf("[map] Animated background: %d frames, %d ticks a loop\n", frames, animation.loopTicks);
    return animation;
}

void LoadCurrentMapBackground()
{
    mapPreviewWaitForLoads();
    const std::string path = mapBackgroundPath(*g_map);
    if (loadedMapBackground(path))
        return;
    setLoadedMapBackground(path, ImageLoader(path).withoutColorKey().compact().create());    //(16 bits on the Xbox)
}

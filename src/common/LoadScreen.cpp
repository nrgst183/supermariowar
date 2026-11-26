#include "LoadScreen.h"

#include "CmdArgs.h"
#include "Game.h"
#include "ResourceManager.h"
#include "gfx.h"
#include "path.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

extern CResourceManager* rm;

namespace loadscreen {
namespace {
constexpr Uint64 SHOW_EVERY_NS = 33000000;    // (about 30 times a second: the screen moves, the loading isn't slowed)
constexpr int FIRST_ESTIMATE = 450;           // (the steps of a first run, before any was counted)

void (*g_drawScreen)() = nullptr;
SDL_ThreadID g_thread = 0;
int g_steps = 0;
int g_expected = FIRST_ESTIMATE;
const char* g_phase = "";
Uint64 g_shownAt = 0;

std::string countFile()
{
    return GetHomeDirectory() + "loadsteps.txt";
}

void show(double done)
{
    if (!g_drawScreen || !rm)
        return;
    blitdest = screen;
    g_drawScreen();

    //The bar: a frame, the part done, and what's loading under it
    const int w = 400, h = 14;
    const int x = App::screenWidth / 2 - w / 2, y = App::screenHeight - 72;
    const auto color = [](Uint8 r, Uint8 g, Uint8 b) { return SDL_MapSurfaceRGB(screen, r, g, b); };
    SDL_Rect frame {x - 3, y - 3, w + 6, h + 6};
    SDL_FillSurfaceRect(screen, &frame, color(0, 0, 0));
    SDL_Rect inside {x - 1, y - 1, w + 2, h + 2};
    SDL_FillSurfaceRect(screen, &inside, color(248, 248, 248));
    SDL_Rect empty {x, y, w, h};
    SDL_FillSurfaceRect(screen, &empty, color(24, 32, 64));
    SDL_Rect full {x, y, static_cast<int>(w * std::clamp(done, 0.0, 1.0)), h};
    SDL_FillSurfaceRect(screen, &full, color(56, 176, 64));
    SDL_Rect shine {x, y, full.w, 3};
    SDL_FillSurfaceRect(screen, &shine, color(128, 224, 120));

    rm->menu_font_small.drawCentered(App::screenWidth / 2, y + h + 8, g_phase);

    //(a test run with screenshots: the screen once, partway)
    static bool shot = false;
    if (!shot && cmd::g_autotest.enabled && !cmd::g_autotest.shotsDir.empty() && std::string(g_phase) == "Loading sounds") {
        shot = true;
        gfx_save_screenshot(cmd::g_autotest.shotsDir + "/00_loading.png");
    }

    gfx_flipscreen();
    SDL_PumpEvents();    //(the window isn't "not responding" meanwhile)
    g_shownAt = SDL_GetTicksNS();
}
} // namespace

void begin(void (*drawScreen)())
{
    g_drawScreen = drawScreen;
    g_thread = SDL_GetCurrentThreadID();
    g_steps = 0;
    g_expected = FIRST_ESTIMATE;
    if (FILE* file = fopen(countFile().c_str(), "r")) {
        int last = 0;
        if (fscanf(file, "%d", &last) == 1 && last > 10)
            g_expected = last;
        fclose(file);
    }
    g_phase = "";
    show(0.0);
}

void phase(const char* what)
{
    if (!g_drawScreen)
        return;
    g_phase = what;
    show(static_cast<double>(g_steps) / g_expected);
}

void step()
{
    if (!g_drawScreen || SDL_GetCurrentThreadID() != g_thread)
        return;
    g_steps++;
    if (SDL_GetTicksNS() - g_shownAt >= SHOW_EVERY_NS)
        show(std::min(0.99, static_cast<double>(g_steps) / g_expected));
}

void end()
{
    if (!g_drawScreen)
        return;
    g_phase = "Ready";
    show(1.0);
    if (FILE* file = fopen(countFile().c_str(), "w")) {
        fprintf(file, "%d\n", g_steps);
        fclose(file);
    }
    g_drawScreen = nullptr;
}

} // namespace loadscreen

/* This file is part of SMW.

    SMW is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    SMW is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SMW.  If not, see <http://www.gnu.org/licenses/>. */


#include "CmdArgs.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "GSMenu.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "sfx.h"
#include "SimulationState.h"
#include "TilesetManager.h"
#include "rollback/NetGame.h"
#include "rollback/Snapshot.h"
#include "gamemodes/BonusHouse.h"
#include "gamemodes/CaptureTheFlag.h"
#include "gamemodes/CardCollection.h"
#include "gamemodes/Chase.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/Classic.h"
#include "gamemodes/Coins.h"
#include "gamemodes/Domination.h"
#include "gamemodes/Eggs.h"
#include "gamemodes/Frag.h"
#include "gamemodes/Frenzy.h"
#include "gamemodes/Greed.h"
#include "gamemodes/Health.h"
#include "gamemodes/Jail.h"
#include "gamemodes/KingOfTheHill.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "gamemodes/Owned.h"
#include "gamemodes/Race.h"
#include "gamemodes/ShyGuyTag.h"
#include "gamemodes/Star.h"
#include "gamemodes/Stomp.h"
#include "gamemodes/Survival.h"
#include "gamemodes/Tag.h"
#include "gamemodes/TimeLimit.h"

#include "FPSLimiter.h"
#include "GSSplashScreen.h"
#include "joystick.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _MSC_VER
#if _MSC_VER >= 1400
    #include <stdio.h>
    FILE _iob[] = {*stdin, *stdout, *stderr};
    extern "C" FILE * __cdecl __iob_func(void) { return _iob; }
#endif
#endif

#include <SDL3/SDL_main.h>


//now it's really time for an "engine" (aka resource manager)


//------ system stuff ------
SDL_Surface		*screen = nullptr;		//for gfx (maybe the gfx system should be improved -> resource manager)
SDL_Surface		*blitdest = nullptr;	//the destination surface for all drawing (can be swapped from screen to another surface)

short			x_shake = 0;
short			y_shake = 0;


//------ game relevant stuff ------
// (the simulation state, g_sim, is defined in GSGameplay.cpp)


//Locations for swirl spawn effects
short g_iSwirlSpawnLocations[4][2][25];


// Non-owning views; the objects are owned by ownedGameModes below
CGameMode			*gamemodes[GAMEMODE_LAST];
CGM_Bonus			*bonushousemode = nullptr;
CGM_Pipe_MiniGame	*pipegamemode = nullptr;
CGM_Boss_MiniGame	*bossgamemode = nullptr;
CGM_Boxes_MiniGame	*boxesgamemode = nullptr;

namespace {
// Owns every game mode object, in creation order.
std::vector<std::unique_ptr<CGameMode>> ownedGameModes;

template<typename T>
T* createGameMode()
{
    auto mode = std::make_unique<T>();
    T* view = mode.get();
    ownedGameModes.push_back(std::move(mode));
    rollback::registerGameMode(view, sizeof(T));  // its match state is part of rollback snapshots
    return view;
}

// --at-shot-every: a PNG of the screen every N drawn frames of an online match
// (for reviewing the overlays; the files are numbered)
void autoScreenshot()
{
    static int drawn = 0;
    static int saved = 0;
    if (cmd::g_autotest.shotEvery <= 0 || cmd::g_autotest.shotDir.empty() || !rollback::NetGame::instance().inMatch())
        return;
    if (++drawn % cmd::g_autotest.shotEvery != 0)
        return;
    gfx_save_screenshot(std::format("{}/shot_{:04d}.png", cmd::g_autotest.shotDir, saved++));
}

void destroyGameModes()
{
    // Destroy in creation order
    for (std::unique_ptr<CGameMode>& mode : ownedGameModes)
        mode.reset();
    ownedGameModes.clear();

    std::fill(std::begin(gamemodes), std::end(gamemodes), nullptr);
    bonushousemode = nullptr;
    pipegamemode = nullptr;
    bossgamemode = nullptr;
    boxesgamemode = nullptr;
}
} // namespace

short currentgamemode = 0;


//*************************************
//  MAIN LOOP
//*************************************


#ifdef __EMSCRIPTEN__
void gameloop_frame();
#endif

void gameloop()
{
    SplashScreenState::instance().init();
    GameStateManager::instance().currentState = &SplashScreenState::instance();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(gameloop_frame, 0, 1);
}

void gameloop_frame()
#else
    while (game_values.appstate != AppState::Quit)
#endif
    {
        // Fixed timestep: run the ticks that are due, then render once
        FPSLimiter& limiter = FPSLimiter::instance();
        const int ticks = limiter.frameStart();

        // Rollback netplay (--net-*): starts once everything is loaded, then
        // handles the network every iteration
        rollback::NetGame& net = rollback::NetGame::instance();
        if (cmd::g_net.enabled() && !net.active() && GameStateManager::instance().currentState == &MenuState::instance()) {
            if (!net.init(cmd::g_net))
                game_values.appstate = AppState::Quit;
        }
        net.pump();

        for (int i = 0; i < ticks && game_values.appstate != AppState::Quit; i++)
            GameStateManager::instance().currentState->update();

        GameState* state = GameStateManager::instance().currentState;
        if (cmd::g_autotest.enabled && cmd::g_autotest.norender) {
            // Headless test runs: skip drawing entirely (and wait for the next tick when paced)
            limiter.idle();
        } else if (limiter.shouldRender(ticks, state->rendersBetweenTicks())) {
            if (state->rendersBetweenTicks())
                state->render(limiter.alpha());

            limiter.beforeFlip();
            autoScreenshot();
            gfx_flipscreen();
            limiter.afterFlip();
        } else {
            limiter.idle();
        }
    }

#ifndef __EMSCRIPTEN__
}
#endif

void create_globals()
{
    // this instance will contain the other relevant objects
    rm = new CResourceManager();
    // delete these or use boost GC shared_ptr

    g_map = new CMap();

    filterslist = new FiltersList();
    maplist = new MapList(false);

    //TODO: add proper test via size
    if (maplist->isEmpty()) {
        throw "Empty map directory!";
    }

    skinlist = new SkinList();
    musiclist = new MusicList();
    worldmusiclist = new WorldMusicList();
    soundpacklist = new SoundsList();
    announcerlist = new AnnouncerList();
    tourlist = new TourList();
    worldlist = new WorldList();

    menugraphicspacklist = new GraphicsList();
    worldgraphicspacklist = new GraphicsList();
    gamegraphicspacklist = new GraphicsList();

    announcerlist->setCurrentIndex(0);
    musiclist->setCurrent(0);
    worldmusiclist->setCurrent(0);
    menugraphicspacklist->setCurrentIndex(0);
    worldgraphicspacklist->setCurrentIndex(0);
    gamegraphicspacklist->setCurrentIndex(0);
    soundpacklist->setCurrentIndex(0);

    g_sim.players.reserve(4);

    try {
        UpdateMusicWithOverrides(*musiclist, *worldmusiclist);
    } catch (const std::exception& ex) {
        throw std::format("ERROR: Could not load music overrides: {}", ex.what());
    }
}

void create_gamemodes()
{
    //set game modes
    gamemodes[0] = createGameMode<CGM_Classic>();
    gamemodes[1] = createGameMode<CGM_Frag>();
    gamemodes[2] = createGameMode<CGM_TimeLimit>();
    gamemodes[3] = createGameMode<CGM_Jail>();
    gamemodes[4] = createGameMode<CGM_Coins>();
    gamemodes[5] = createGameMode<CGM_Stomp>();
    gamemodes[6] = createGameMode<CGM_Eggs>();
    gamemodes[7] = createGameMode<CGM_CaptureTheFlag>();
    gamemodes[8] = createGameMode<CGM_Chicken>();
    gamemodes[9] = createGameMode<CGM_Tag>();
    gamemodes[10] = createGameMode<CGM_Star>();
    gamemodes[11] = createGameMode<CGM_Domination>();
    gamemodes[12] = createGameMode<CGM_KingOfTheHill>();
    gamemodes[13] = createGameMode<CGM_Race>();
    gamemodes[14] = createGameMode<CGM_Owned>();
    gamemodes[15] = createGameMode<CGM_Frenzy>();
    gamemodes[16] = createGameMode<CGM_Survival>();
    gamemodes[17] = createGameMode<CGM_Greed>();
    gamemodes[18] = createGameMode<CGM_Health>();
    gamemodes[19] = createGameMode<CGM_Collection>();
    gamemodes[20] = createGameMode<CGM_Chase>();
    gamemodes[21] = createGameMode<CGM_ShyGuyTag>();

    currentgamemode = 0;
    game_values.gamemode = gamemodes[currentgamemode];

    //Special modes
    bonushousemode = createGameMode<CGM_Bonus>();
    pipegamemode = createGameMode<CGM_Pipe_MiniGame>();
    bossgamemode = createGameMode<CGM_Boss_MiniGame>();
    boxesgamemode = createGameMode<CGM_Boxes_MiniGame>();
}

void init_spawnlocations()
{
        //Calculate the swirl spawn effect locations
    float spawnradius = 100.0f;
    float spawnangle = 0.0f;

    for (short i = 0; i < 25; i++) {
        g_iSwirlSpawnLocations[0][0][i] = static_cast<short>(spawnradius * cos(spawnangle));
        g_iSwirlSpawnLocations[0][1][i] = static_cast<short>(spawnradius * sin(spawnangle));

        float angle = spawnangle + HALF_PI;
        g_iSwirlSpawnLocations[1][0][i] = static_cast<short>(spawnradius * cos(angle));
        g_iSwirlSpawnLocations[1][1][i] = static_cast<short>(spawnradius * sin(angle));

        angle = spawnangle + PI;
        g_iSwirlSpawnLocations[2][0][i] = static_cast<short>(spawnradius * cos(angle));
        g_iSwirlSpawnLocations[2][1][i] = static_cast<short>(spawnradius * sin(angle));

        angle = spawnangle + THREE_HALF_PI;
        g_iSwirlSpawnLocations[3][0][i] = static_cast<short>(spawnradius * cos(angle));
        g_iSwirlSpawnLocations[3][1][i] = static_cast<short>(spawnradius * sin(angle));

        spawnradius -= 4.0f;
        spawnangle += 0.1f;
    }
}


//*************************************
//  PROGRAM ENTRY POINT
//*************************************

void main_game();

int main(int argc, char *argv[])
{
    const cmd::Args cmd = cmd::parse_args(argc, argv);
    if (!cmd.success) {
        return 1;
    }
    if (cmd.show_help) {
        cmd::print_help(TITLESTRING, GIT_REVISION);
        return 0;
    }
    if (cmd.debug) {
        cmd::show_windows_console();
    }
    if (!cmd.data_root.empty())
        RootDataDirectory = cmd.data_root;


    try {
        main_game();
    }
    catch (const char* what) {
        gfx_show_catched_error(what);
        return 1;
    }
    catch (const std::string& ex) {
        gfx_show_catched_error(ex);
        return 1;
    }
    catch (const std::exception& ex) {
        gfx_show_catched_error(ex.what());
        return 1;
    }
    catch (...) {
        gfx_show_catched_error({});
        return 1;
    }

    return 0;
}

void main_game()
{
    printf("-------------------------------------------------------------------------------\n");
    printf(" %s %s %s\n", TITLESTRING, GIT_REVISION, GIT_DATE);
    printf("-------------------------------------------------------------------------------\n");
    printf("\n---------------- startup ----------------\n");

    // All gameplay randomness comes from the simulation state's generator
    RandomNumberGenerator::bindGameplay(&g_sim.rng);

    App::registerSdlMetadata(TITLESTRING);
    ensureSettingsDir();
    create_globals();

	gfx_init(App::screenWidth, App::screenHeight, false);		//initialize the graphics (SDL)
    blitdest = screen;

#if	0
    //Comment this in to performance test the preview map loading
    MI_MapField * miMapField = new MI_MapField(&rm->spr_selectfield, 70, 165, "Map", 500, 120);

    for (int k = 0; k < 100; k++) {
        game_values.playerInput.outputControls[3].menu_right.fPressed = true;
        miMapField->SendInput(&game_values.playerInput);
        //printf("Map over-> %s\n", maplist->currentFilename());
    }

    return 0;
#endif

    sfx_init();                     //init the sound system
    JoystickManager::instance().init();

    //currently this only sets the title, not the icon.
    //setting the icon isn't implemented in sdl ->  i'll ask on the mailing list
    gfx_settitle(std::format("{} {} {}", TITLESTRING, GIT_REVISION, GIT_DATE).c_str());
    SDL_HideCursor();

    printf("\n---------------- loading ----------------\n");

    game_values.init();

    create_gamemodes();

    game_values.ReadConfig();

    // Online, every peer must run at the normal speed: pace --autotest runs
    if (cmd::g_net.enabled() && cmd::g_autotest.enabled && cmd::g_autotest.realtime == 0)
        cmd::g_autotest.realtime = 3;

    // The --autotest harness runs unthrottled (see FPSLimiter), unless measuring the frame pacing
    if (cmd::g_autotest.realtime > 0) {
        static constexpr FrameRateMode modes[] = {FrameRateMode::VSync, FrameRateMode::Uncapped, FrameRateMode::TickRate};
        game_values.framelimiter = static_cast<short>(modes[(cmd::g_autotest.realtime - 1) % 3]);
    }

    //Assign the powerup weights to the selected preset
    for (short iPowerup = 0; iPowerup < NUM_POWERUPS; iPowerup++) {
        game_values.powerupweights[iPowerup] = game_values.allPowerupPresets[game_values.poweruppreset][iPowerup];
    }

    if (game_values.fullscreen) {
        gfx_changefullscreen(true);
        blitdest = screen;
    }

    init_spawnlocations();

    //Load the gfx color palette
    try {
        gfx_loadpalette(convertPath("gfx/packs/palette.png", gamegraphicspacklist->currentPath()));
    } catch (const std::string& err) {
        printf("\nwarning: %s -> falling back to BMP\n", err.c_str());
        gfx_loadpalette(convertPath("gfx/packs/palette.bmp", gamegraphicspacklist->currentPath()));
    }

    // Gameplay randomness is seeded from the clock (netplay reseeds it with a
    // shared seed before each match); cosmetic randomness seeds itself.
    RandomNumberGenerator::gameplay().seedFromTime();

    if (cmd::g_autotest.enabled && cmd::g_autotest.seed != 0) {
        RandomNumberGenerator::gameplay().seed(static_cast<uint32_t>(cmd::g_autotest.seed));
    }

    if (cmd::g_autotest.enabled) {
        // Debug builds turn suicide kills off by default (see GameValues.cpp);
        // use the release value so Debug and Release test runs are comparable
        game_values.suicidetime = 310;
    }


    //**********************************************************


    gameloop(); // all the game logic happens here


    //**********************************************************


    printf("\n---------------- shutdown ----------------\n");

    rollback::NetGame::instance().shutdown();

    destroyGameModes();

    JoystickManager::instance().shutdown();
    sfx_close();
    gfx_close();

	// release all resources
	delete rm;
}

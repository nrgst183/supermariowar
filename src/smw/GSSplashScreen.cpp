#include "GSSplashScreen.h"
#include "LoadLog.h"
#include "LoadScreen.h"
#include "Platform.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "gfx.h"
#include "MapList.h"
#include "Game.h"
#include "GameValues.h"
#include "joystick.h"
#include "GSMenu.h"
#include "GSGameplay.h"
#include "eyecandy.h"
#include "path.h"
#include "ResourceManager.h"

//-----------------------------------------------------------------------------
// THE LOAD UP SEQUENCE + SPLASH SCREEN
//-----------------------------------------------------------------------------
//that's a bunch of ugly code, maybe i'll throw it out again

SplashScreenState::SplashScreenState()
{
    alpha = 255;
    state = 7;
    timer = 120;
    firstFrame = true;
}

SplashScreenState& SplashScreenState::instance() {
    static SplashScreenState ss;
    return ss;
}

void SplashScreenState::drawLoadingScreen()
{
    SplashScreenState& splash = instance();
    SDL_FillSurfaceRect(screen, nullptr, 0x0);
    rm->menu_backdrop.setalpha(255);
    rm->menu_backdrop.draw(0, 0);
    rm->menu_smw.setalpha(255);
    rm->menu_smw.draw(App::screenWidth/2 - (static_cast<short>(rm->menu_smw.getWidth()) >> 1), 30);
    rm->menu_version.setalpha(255);
    rm->menu_version.draw(628 - rm->menu_version.getWidth(), 10);
    if (splash.menu_credits) {
        splash.menu_credits->setalpha(255);
        splash.menu_credits->draw(227, 200);
    }
}

void SplashScreenState::onLeaveState()
{
    menu_credits.reset();
}

bool SplashScreenState::init()
{
    rm->loadStartGraphics();
    loadlog::mark("splash graphics");

    menu_credits = std::make_unique<gfxSprite>();
    *menu_credits = ImageLoader(convertPath("gfx/packs/menu/splash_credits.png", menugraphicspacklist->currentPath())).create();

    return true;
}

void SplashScreenState::update()
{
    game_values.playerInput.ClearPressedKeys(1);

    // TODO: move this out of this method maybe

    while (SDL_PollEvent(&loop_event)) {
        switch (loop_event.type) {
        case SDL_EVENT_QUIT: {
            game_values.appstate = AppState::Quit;
            return;
        }
        break;

        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            JoystickManager::instance().handleEvent(loop_event);
            break;

        case SDL_EVENT_KEY_DOWN: {
            switch (loop_event.key.key) {
            case SDLK_RETURN:
                if (loop_event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                    game_values.fullscreen = !game_values.fullscreen;
                    gfx_changefullscreen(game_values.fullscreen);
                    blitdest = screen;
                }
                break;

            case SDLK_F4:
                if (loop_event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                    game_values.appstate = AppState::Quit;
                    return;
                }
                break;

            case SDLK_INSERT:
                gfx_take_screenshot();
                break;

            default:
                break;
            }
            break;
        }

        default:
            break;
        }

        game_values.playerInput.Update(loop_event, 1);
    }

    // --autotest: skip "Press Any Key To Continue"
    if (cmd::g_autotest.enabled && state == 8)
        game_values.playerInput.outputControls[0].menu_select.fPressed = true;

    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        if (game_values.playerInput.outputControls[iPlayer].menu_select.fPressed ||
            game_values.playerInput.outputControls[iPlayer].menu_cancel.fPressed ||
            game_values.playerInput.outputControls[iPlayer].menu_random.fPressed) {
            {
                loadlog::mark("splash: waiting for \"press any key\" (not loading)");
                blitdest = rm->menu_backdrop.getSurface();
                rm->drawShade(App::menuTransparency);
                blitdest = screen;

                eyecandy[2].clean();

                game_values.playerInput.ResetKeys();
                game_values.appstate = AppState::Menu;

                MenuState::instance().init();
                GameplayState::instance().init();
                GameStateManager::instance().changeStateTo(&MenuState::instance());
                return;
            }
        }
    }

    SDL_FillSurfaceRect(screen, nullptr, 0x0);

    if (state == 6 || state == 7 || state == 8) {
        rm->menu_backdrop.setalpha(static_cast<Uint8>(alpha));
        rm->menu_backdrop.draw(0, 0);

        rm->menu_smw.setalpha(static_cast<Uint8>(alpha));
        rm->menu_smw.draw(App::screenWidth/2 - (static_cast<short>(rm->menu_smw.getWidth()) >> 1), 30);  //smw logo

        rm->menu_version.setalpha(static_cast<Uint8>(alpha));
        rm->menu_version.draw(628 - rm->menu_version.getWidth(), 10); //smw logo

        rm->menu_font_large.setAlpha(static_cast<Uint8>(alpha));

        menu_credits->setalpha(static_cast<Uint8>(alpha));
        menu_credits->draw(227, 200);
    }

    if (state == 7) {
        rm->menu_font_large.drawCentered(App::screenWidth/2, App::screenHeight * 0.875f, "Loading...");
    } else if (state == 8) {
        rm->menu_font_large.drawCentered(App::screenWidth/2, App::screenHeight * 0.875f, "Press Any Key To Continue");

        eyecandy[2].cleanDeadObjects();
        eyecandy[2].update();
        eyecandy[2].draw();
    }

    // Only start loading after we displayed something, in the 2nd frame
    if (firstFrame) {
        firstFrame = false;
        return;
    }

    if (state == 7) {
        // load initial coin sound
        rm->backgroundmusic[2] = sfxMusic(musiclist->music(1));
        loadlog::mark("menu music");

        //(a bar and what's loading, drawn as it goes: the loading is in this one frame)
        loadscreen::begin(drawLoadingScreen);
        loadscreen::phase("Loading graphics");
        rm->loadAllGraphics();
        loadlog::mark("all graphics");
        //(low memory, the first boot (or the cache emptied): the matches' sprites converted for the
        //cache now, under "Loading...", not as the first match starts; they're loaded from it then)
        if (platform::lowMemory() && !platform::cacheMarked("sprites_prepared_1")) {
            loadscreen::phase("Loading graphics");
            gfxSprite::prepareGroup(SpriteGroup::Game);
            gfxSprite::prepareGroup(SpriteGroup::Shared);
            platform::markCache("sprites_prepared_1");
            loadlog::mark("the matches' sprites prepared (first boot)");
        }
        loadscreen::phase("Loading sounds");
        rm->LoadGameSounds();
        loadlog::mark("game sounds");

        if (!game_values.soundcapable) {
            game_values.sound = false;
            game_values.music = false;
            game_values.soundvolume = 0;
            game_values.musicvolume = 0;
        }

        //Read the map filter lists
        loadscreen::phase("Loading maps");
        maplist->ReadFilters();
        maplist->ApplyFilters(game_values.pfFilters);
        loadlog::mark("map filters");
        loadscreen::end();

        ifSoundOnPlay(rm->sfx_coin);

        state++;
    }
}

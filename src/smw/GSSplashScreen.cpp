#include "GSSplashScreen.h"

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

void SplashScreenState::onLeaveState()
{
    delete menu_credits;
}

bool SplashScreenState::init()
{
    rm->loadStartGraphics();

//  gfx_loadimagenocolorkey(&rm->menu_dpi_logo, convertPath("gfx/packs/menu/splash_72dpi.png", menugraphicspacklist->current_name()));
//  gfx_loadimagenocolorkey(&rm->menu_contest_winners, convertPath("gfx/packs/menu/splash_contest_winners.png", menugraphicspacklist->current_name()));

    menu_credits = new gfxSprite();
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

    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        if (game_values.playerInput.outputControls[iPlayer].menu_select.fPressed ||
            game_values.playerInput.outputControls[iPlayer].menu_cancel.fPressed ||
            game_values.playerInput.outputControls[iPlayer].menu_random.fPressed) {
            {
                blitdest = rm->menu_backdrop.getSurface();
                rm->menu_shade.setalpha(App::menuTransparency);
                rm->menu_shade.draw(0, 0);
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
        rm->menu_backdrop.setalpha((Uint8)alpha);
        rm->menu_backdrop.draw(0, 0);

        rm->menu_smw.setalpha((Uint8)alpha);
        rm->menu_smw.draw(App::screenWidth/2 - ((short)rm->menu_smw.getWidth() >> 1), 30);  //smw logo

        rm->menu_version.setalpha((Uint8)alpha);
        rm->menu_version.draw(628 - rm->menu_version.getWidth(), 10); //smw logo

        rm->menu_font_large.setAlpha((Uint8)alpha);
        //rm->menu_font_large.drawRightJustified(App::screenWidth * 0.98f, 45, "WIP");

        menu_credits->setalpha((Uint8)alpha);
        menu_credits->draw(227, 200);
    }

    if (state == 7) {
//            _load_drawmsg("Loading...");
        rm->menu_font_large.drawCentered(App::screenWidth/2, App::screenHeight * 0.875f, "Loading...");
    } else if (state == 8) {
//            _load_drawmsg("Press Any Key To Continue");
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

        rm->loadAllGraphics();
        rm->LoadGameSounds();

        if (!game_values.soundcapable) {
            game_values.sound = false;
            game_values.music = false;
            game_values.soundvolume = 0;
            game_values.musicvolume = 0;
        }

        //Read the map filter lists
        maplist->ReadFilters();
        maplist->ApplyFilters(game_values.pfFilters);

        ifSoundOnPlay(rm->sfx_coin);


        state++;
    }
}

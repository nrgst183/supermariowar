#include "GSMenu.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "Game.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "GameplayStyles.h"
#include "joystick.h"
#include "net.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "Score.h"
#include "SimulationState.h"
#include "gamemodes/BonusHouse.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "ui/MI_BonusWheel.h"
#include "ui/MI_MapBrowser.h"
#include "ui/MI_MapField.h"
#include "ui/MI_MapFilterScroll.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_TournamentScoreboard.h"
#include "ui/MI_TourStop.h"
#include "ui/MI_World.h"
#include "Version.h"
#include "world.h"
#include "WorldTourStop.h"
#include "world/WorldMapIO.h"

#include "menu/BonusWheelMenu.h"
#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MapFilterEditMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/ModeOptionsMenu.h"
#include "menu/OptionsMenu.h"
#include "menu/PlayerControlsMenu.h"
#include "menu/PlayerControlsSelectMenu.h"
#include "menu/TeamSelectMenu.h"
#include "menu/TournamentScoreboardMenu.h"
#include "menu/TourStopMenu.h"
#include "menu/WorldMenu.h"

#include "menu/options/EyeCandyOptionsMenu.h"
#include "menu/options/GameplayOptionsMenu.h"
#include "menu/options/GraphicsOptionsMenu.h"
#include "menu/options/PowerupDropRatesMenu.h"
#include "menu/options/PowerupSettingsMenu.h"
#include "menu/options/ProjectileLimitsMenu.h"
#include "menu/options/ProjectileOptionsMenu.h"
#include "menu/options/SoundOptionsMenu.h"
#include "menu/options/TeamOptionsMenu.h"

#include "menu/network/NetEditServersMenu.h"
#include "menu/network/NetLobbyMenu.h"
#include "menu/network/NetNewRoomMenu.h"
#include "menu/network/NetNewRoomSettingsMenu.h"
#include "menu/network/NetRoomMenu.h"
#include "menu/network/NetServersMenu.h"

#include <format>
#include <string>
#include <cstdlib> // atoi()
#include <random>
#include <cassert>
#include <sstream>

// Opt-in --autotest harness (see cmd::AutoTestArgs): drives the menus without human input.
// Returns a menu code to use instead of the current menu's own input, or MENU_CODE_NONE.
MenuCodeEnum MenuState::AutoTestMenuUpdate()
{
    using Kind = cmd::AutoTestArgs::Kind;
    const cmd::AutoTestArgs& at = cmd::g_autotest;

    static std::mt19937 rng(12345 + cmd::g_autotest.start);    // not the game RNG, so gameplay RNG order is untouched
    static int menuFrames = 0;
    static int phase = 0;              // 0 idle in main menu, 1 waiting for settings menu, 2 match running
    static int timer = 0;
    static int scenario = 0;
    static int lastMatchesDone = 0;

    if (game_values.appstate != AppState::Menu)
        return MENU_CODE_NONE;

    ++menuFrames;

    auto pressRandomKey = [&](bool allowCancel) {
        COutputControl& keys = game_values.playerInput.outputControls[0];
        switch (rng() % (allowCancel ? 7 : 6)) {
            case 0: keys.menu_up.fPressed = true; break;
            case 1: keys.menu_down.fPressed = true; break;
            case 2: keys.menu_left.fPressed = true; break;
            case 3: keys.menu_right.fPressed = true; break;
            case 4:
            case 5: keys.menu_select.fPressed = true; break;
            default: keys.menu_cancel.fPressed = true; break;
        }
    };

    if (at.kind == Kind::Menus) {
        if (menuFrames >= at.frames)
            return MENU_CODE_EXIT_APPLICATION;

        if (rng() % 3 == 0)
            pressRandomKey(mCurrentMenu != mMainMenu.get());
        return MENU_CODE_NONE;
    }

    if (g_autoTestMatchesDone >= at.matches)
        return MENU_CODE_EXIT_APPLICATION;

    if (lastMatchesDone != g_autoTestMatchesDone) {
        lastMatchesDone = g_autoTestMatchesDone;
        timer = 0;
    }

    const bool oneShot = at.kind == Kind::Single || at.kind == Kind::Minigame;

    switch (phase) {
        case 0: {
            if (mCurrentMenu != mMainMenu.get()) {
                mCurrentMenu = mMainMenu.get();
                mCurrentMenu->ResetMenu();
            }

            if (++timer < 20)
                return MENU_CODE_NONE;
            timer = 0;

            const int idx = at.start + scenario++;
            const int players = at.players > 0 ? at.players : 2 + idx % 3;
            for (int k = 0; k < 4; k++)
                game_values.playercontrol[k] = k < players ? 2 : 0;

            switch (at.kind) {
                case Kind::Minigame: {
                    static const Minigame minigames[] = {Minigame::PipeCoin, Minigame::HammerBoss, Minigame::BombBoss, Minigame::FireBoss, Minigame::Boxes};
                    game_values.matchtype = MatchType::MiniGame;
                    mMatchSelectionMenu->SetMinigame(minigames[(at.mode >= 0 ? at.mode : idx) % 5]);
                    break;
                }
                case Kind::Tour:
                    game_values.matchtype = MatchType::Tour;
                    game_values.tourindex = static_cast<short>(idx % tourlist->count());
                    break;
                case Kind::World:
                    game_values.matchtype = MatchType::World;
                    game_values.worldindex = static_cast<short>(idx % worldlist->count());
                    break;
                default:
                    game_values.matchtype = MatchType::SingleGame;
                    break;
            }

            mTeamSelectMenu->ResetTeamSelect();
            game_values.tournamentcontrolteam = -1;
            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);

            phase = at.kind == Kind::Single ? 1 : 2;
            return MENU_CODE_TO_GAME_SETUP_MENU;
        }

        case 1: {
            //Single game: pick mode, goal and map in the game settings menu, then start
            const int idx = at.start + scenario - 1;
            const short mode = static_cast<short>((at.mode >= 0 ? at.mode : idx) % GAMEMODE_LAST);

            mGameSettingsMenu->GameModeChanged(mode);
            game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
            currentgamemode = mode;

            //Choose a small goal so the match can end by itself
            SModeOption* options = game_values.gamemode->GetOptions();
            game_values.gamemode->goal = options[1].iValue;
            mGameSettingsMenu->miGoalField[mode]->setCurrentValue(game_values.gamemode->goal);

            //Select the requested map, or advance the map list to cycle through all maps
            if (!at.map.empty()) {
                maplist->findexact(at.map.c_str(), false);
            } else {
                for (int k = 0; k < 5; k++)
                    maplist->next(false);
            }
            mGameSettingsMenu->miMapField->LoadCurrentMap();

            printf("autotest: match %d, mode %d, map %s, %d players\n", scenario, mode, maplist->currentShortmapname().c_str(), g_sim.score_cnt);

            phase = 2;
            timer = 0;
            return MENU_CODE_START_GAME;
        }

        default: {
            if (oneShot) {
                //Back in the menu after the match
                phase = 0;
                timer = 0;
                return MENU_CODE_NONE;
            }

            //Tour / world: press random menu keys until a stage starts or the main menu is reached
            if (mCurrentMenu == mMainMenu.get()) {
                phase = 0;
                timer = 0;
                return MENU_CODE_NONE;
            }

            if (++timer > 6000) {
                //Stuck (e.g. unreadable tour/world file): go back to the main menu
                mCurrentMenu = mMainMenu.get();
                mCurrentMenu->ResetMenu();
                return MENU_CODE_NONE;
            }

            if (timer % 4 == 0)
                pressRandomKey(false);
            return MENU_CODE_NONE;
        }
    }
}

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


#include <format>
#include <string>
#include <cstdlib> // atoi()
#include <random>
#include <cassert>
#include <sstream>

//Debugging code for writing and playing test scripts
#ifdef _DEBUG

void MenuState::LoadScript(const char * szScriptFile)
{
    operations.clear();

    FILE * fp = fopen(szScriptFile, "r");

    if (fp != nullptr) {
        char buffer[256];
        while (fgets(buffer, 256, fp) != nullptr) {
            if (buffer[0] == '#' || buffer[0] == '\n' || buffer[0] == ' ' || buffer[0] == '\t')
                continue;

            ScriptOperation * op = new ScriptOperation();

            //Get controller to use
            op->iController = atoi(strtok(buffer, ",\n"));

            //Get iterations on this operation
            op->iIterations = atoi(strtok(nullptr, ",\n"));

            //Get input operations to perform
            for (short i = 0; i < 8; i++) {
                op->fInput[i] = atoi(strtok(nullptr, ",\n")) != 0;
            }

            operations.push_back(op);
        }

        fclose(fp);
    }

    current = operations.begin();
    iScriptState = 0;
}

void MenuState::ResetScript()
{
    std::vector<ScriptOperation*>::iterator itr = operations.begin();

    while (itr != operations.end()) {
        (*itr)->iTimesExecuted = 0;
        itr++;
    }

    current = operations.begin();
    iScriptState = 0;
}

void MenuState::GetNextScriptOperation()
{
    if (AppState::Menu != game_values.appstate)
        return;

    //static short slowdowntimer = 0;
    //if (++slowdowntimer < 30)
    //return;
    //slowdowntimer = 0;

    if (iScriptState == 1) {
        if (mCurrentMenu == mGameSettingsMenu.get()) {
            if (!fScriptRunPreGameOptions) {
                fScriptRunPreGameOptions = true;
                LoadScript("Scripts/GameModeOptionsOnly.txt");
                iScriptState = 0;
                return;
            } else if (game_values.gamemode->gamemode == game_mode_frag ||
                      game_values.gamemode->gamemode == game_mode_timelimit ||
                      game_values.gamemode->gamemode == game_mode_coins ||
                      game_values.gamemode->gamemode == game_mode_classic ||
                      game_values.gamemode->gamemode == game_mode_eggs ||
                      game_values.gamemode->gamemode == game_mode_owned) {
                LoadScript("Scripts/NoModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_jail) {
                LoadScript("Scripts/JailModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_stomp) {
                LoadScript("Scripts/StompModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_ctf) {
                LoadScript("Scripts/CaptureTheFlagModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_chicken) {
                LoadScript("Scripts/ChickenModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_tag) {
                LoadScript("Scripts/TagModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_star) {
                LoadScript("Scripts/StarModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_domination) {
                LoadScript("Scripts/DominationModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_koth) {
                LoadScript("Scripts/KingOfTheHillModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_race) {
                LoadScript("Scripts/RaceModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_frenzy) {
                LoadScript("Scripts/FrenzyModeOptions.txt");
            } else if (game_values.gamemode->gamemode == game_mode_survival) {
                LoadScript("Scripts/SurvivalModeOptions.txt");
            }

            game_values.gamemode->setdebuggoal();
        } else if (mCurrentMenu == mTourStopMenu.get()) {
            static int tournext = COSMETIC_RANDOM_INT(200);

            if (--tournext < 0) {
                game_values.playerInput.outputControls[0].menu_select.fPressed = true;
                tournext = COSMETIC_RANDOM_INT(200);
                game_values.gamemode->setdebuggoal();
            }
            return;
        }

        return;
    } else if (iScriptState == 2) {
        if (mCurrentMenu == mTournamentScoreboardMenu.get()) {
            static int scoreboardnext = COSMETIC_RANDOM_INT(200);

            if (--scoreboardnext < 0) {
                game_values.playerInput.outputControls[0].menu_select.fPressed = true;
                iScriptState = 3;
                scoreboardnext = COSMETIC_RANDOM_INT(200);
            }

            return;
        } else if (mCurrentMenu == mGameSettingsMenu.get()) {
            iScriptState = 0;
            mCurrentMenu = mMainMenu.get();
            mCurrentMenu->ResetMenu();
            LoadScript("Scripts/StartMenuAutomation.txt");
            fScriptRunPreGameOptions = true;
            return;
        } else if (mCurrentMenu == mBonusWheelMenu.get()) {
            static int bonuswheelnext = COSMETIC_RANDOM_INT(200) + 300;

            if (--bonuswheelnext < 0) {
                game_values.playerInput.outputControls[0].menu_select.fPressed = true;
                //iScriptState = 3;
                bonuswheelnext = COSMETIC_RANDOM_INT(200) + 300;
            }
            return;
        } else if (mCurrentMenu == mTourStopMenu.get()) {
            static int tournext = COSMETIC_RANDOM_INT(200);

            if (--tournext < 0) {
                game_values.playerInput.outputControls[0].menu_select.fPressed = true;
                tournext = COSMETIC_RANDOM_INT(200);
                game_values.gamemode->setdebuggoal();
            }
            return;
        }

        return;
    } else if (iScriptState == 3) {
        if (mCurrentMenu == mBonusWheelMenu.get()) {
            game_values.playerInput.outputControls[0].menu_select.fPressed = true;
            return;
        } else if (mCurrentMenu == mGameSettingsMenu.get()) {
            iScriptState = 0;
            LoadScript("Scripts/GameModeOptionsOnly.txt");
            fScriptRunPreGameOptions = true;
            return;
        } else if (mCurrentMenu == mMainMenu.get()) {
            iScriptState = 0;
            LoadScript("Scripts/StartMenuAutomation.txt");
            fScriptRunPreGameOptions = true;
            return;
        } else if (mCurrentMenu == mTourStopMenu.get()) {
            static int tournext = COSMETIC_RANDOM_INT(200);

            if (--tournext < 0) {
                game_values.playerInput.outputControls[0].menu_select.fPressed = true;
                tournext = COSMETIC_RANDOM_INT(200);
                game_values.gamemode->setdebuggoal();
            }
            return;
        }

        return;
    }

    // here bad reference when pressing Ins!
    ScriptOperation * op = *current;

    while (op->iTimesExecuted >= op->iIterations) {
        if (++current == operations.end()) {
            iScriptState = 1;
            return;
        }

        op = *current;
    }

    op->iTimesExecuted++;

    short iController = op->iController;

    if (iController == 5)
        iController = COSMETIC_RANDOM_INT(4);

    short iKeys[8];
    short iNumKeys = 0;

    for (short i = 0; i < 8; i++) {
        if (op->fInput[i])
            iKeys[iNumKeys++] = i;
    }

    if (iController == MAX_PLAYERS) { //All controllers
        game_values.playerInput.outputControls[0].keys[iKeys[COSMETIC_RANDOM_INT(iNumKeys)]].fPressed = true;
        game_values.playerInput.outputControls[1].keys[iKeys[COSMETIC_RANDOM_INT(iNumKeys)]].fPressed = true;
        game_values.playerInput.outputControls[2].keys[iKeys[COSMETIC_RANDOM_INT(iNumKeys)]].fPressed = true;
        game_values.playerInput.outputControls[3].keys[iKeys[COSMETIC_RANDOM_INT(iNumKeys)]].fPressed = true;
    } else {
        game_values.playerInput.outputControls[iController].keys[iKeys[COSMETIC_RANDOM_INT(iNumKeys)]].fPressed = true;
    }
}

void MenuState::StartRecordScript()
{
    operations.clear();
}

void MenuState::SaveScript(const char * szScriptFile)
{
    FILE * fp = fopen(szScriptFile, "w");

    if (fp) {
        std::vector<ScriptOperation*>::iterator itr = operations.begin();

        while (itr != operations.end()) {
            ScriptOperation * op = *itr;

            //Add line breaks
            if (op->iController == -1) {
                fprintf(fp, "\n");
                itr++;
                continue;
            }

            fprintf(fp, "%d,%d", op->iController, op->iIterations);

            for (short i = 0; i < 8; i++) {
                fprintf(fp, ",%d", op->fInput[i]);
            }

            fprintf(fp, "\n");

            itr++;
        }

        fclose(fp);

#if defined(__APPLE__)
        chmod(szScriptFile, S_IRWXU | S_IRWXG | S_IROTH);
#endif
    }
}

void MenuState::AddEmtpyLineToScript()
{
    ScriptOperation * op = new ScriptOperation();
    op->iController = -1;
    operations.push_back(op);
}

void MenuState::SetNextScriptOperation()
{
    for (short i = 0; i < 8; i++) {
        if (game_values.playerInput.outputControls[0].keys[i].fPressed) {
            ScriptOperation * op = new ScriptOperation();

            op->iController = 0;
            op->iIterations = 1;

            for (short iKey = 0; iKey < 8; iKey++) {
                op->fInput[iKey] = false;
            }

            op->fInput[i] = true;

            operations.push_back(op);
        }
    }
}


#endif

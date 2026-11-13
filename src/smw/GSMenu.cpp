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

// std::uniue_ptr requires that the special member function have access
// to the complete types.
MenuState::MenuState() = default;
MenuState::~MenuState() = default;

MenuState& MenuState::instance()
{
    static MenuState menu;
    return menu;
}

bool MenuState::init()
{
    mMainMenu = std::make_unique<UI_MainMenu>();

    // Options menu
    mOptionsMenu = std::make_unique<UI_OptionsMenu>();
    mGameplayOptionsMenu = std::make_unique<UI_GameplayOptionsMenu>();
    mTeamOptionsMenu = std::make_unique<UI_TeamOptionsMenu>();
    mPowerupDropRatesMenu = std::make_unique<UI_PowerupDropRatesMenu>();
    mPowerupSettingsMenu = std::make_unique<UI_PowerupSettingsMenu>();
    mProjectileLimitsMenu = std::make_unique<UI_ProjectileLimitsMenu>();
    mProjectileOptionsMenu = std::make_unique<UI_ProjectileOptionsMenu>();
    mGraphicsOptionsMenu = std::make_unique<UI_GraphicsOptionsMenu>();
    mEyeCandyOptionsMenu = std::make_unique<UI_EyeCandyOptionsMenu>();
    mSoundOptionsMenu = std::make_unique<UI_SoundOptionsMenu>();

    // Controls menu
    mPlayerControlsSelectMenu = std::make_unique<UI_PlayerControlsSelectMenu>();
    mPlayerControlsMenu = std::make_unique<UI_PlayerControlsMenu>();

    // Gameplay menu
    mModeOptionsMenu = std::make_unique<UI_ModeOptionsMenu>();
    mMatchSelectionMenu = std::make_unique<UI_MatchSelectionMenu>();
    mGameSettingsMenu = std::make_unique<UI_GameSettingsMenu>();
    mMapFilterEditMenu = std::make_unique<UI_MapFilterEditMenu>();
    mTourStopMenu = std::make_unique<UI_TourStopMenu>();
    mWorldMenu = std::make_unique<UI_WorldMenu>();
    mTeamSelectMenu = std::make_unique<UI_TeamSelectMenu>();
    mTournamentScoreboardMenu = std::make_unique<UI_TournamentScoreboardMenu>();
    mBonusWheelMenu = std::make_unique<UI_BonusWheelMenu>();

    // Multiplayer menu
    mNetServersMenu = std::make_unique<UI_NetServersMenu>();
    mNetEditServersMenu = std::make_unique<UI_NetEditServersMenu>();
    mNetLobbyMenu = std::make_unique<UI_NetLobbyMenu>();
    mNetNewRoomMenu = std::make_unique<UI_NetNewRoomMenu>();
    mNetNewRoomSettingsMenu = std::make_unique<UI_NetNewRoomSettingsMenu>(mGameSettingsMenu.get());
    mNetRoomMenu = std::make_unique<UI_NetRoomMenu>();

    mCurrentMenu = mMainMenu.get();
    szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();

    return true;
}

//---------------------------------------------------------------
// RUN THE MENU
//---------------------------------------------------------------

void MenuState::onEnterState()
{
    iUnlockMinigameOptionIndex = 0;

    iTournamentAIStep = 0;
    iTournamentAITimer = 0;

    //Reset the keys each time we switch from menu to game and back
    game_values.playerInput.ResetKeys();

    fNeedMenuMusicReset = false;

    if (game_values.gamemode->winningteam > -1 && game_values.tournamentwinner == -1 &&
            (((game_values.matchtype == MatchType::SingleGame || game_values.matchtype == MatchType::QuickGame || game_values.matchtype == MatchType::MiniGame || game_values.matchtype == MatchType::Tournament || game_values.matchtype == MatchType::NetGame) && game_values.bonuswheel == BonusWheelStyle::EveryGame) || (game_values.matchtype == MatchType::Tour && game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType))) {
        mBonusWheelMenu->miBonusWheel->Reset(false);
        mCurrentMenu = mBonusWheelMenu.get();
    } else if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame && game_values.matchtype != MatchType::NetGame) {
        mCurrentMenu = mTournamentScoreboardMenu.get();
    } else if (game_values.matchtype == MatchType::QuickGame) {
        //Reset back to main menu after quick game
        mCurrentMenu = mMainMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (game_values.matchtype == MatchType::NetGame) {
        mCurrentMenu = mNetLobbyMenu.get();
        mCurrentMenu->ResetMenu();
        netplay.joinSuccessful = false;
        netplay.gameRunning = false;
    }

    if (game_values.matchtype == MatchType::World) {
        if (game_values.gamemode->winningteam > -1 && game_values.tournamentwinner == -1) { //Stage is completed
            mWorldMenu->miWorld->SetControllingTeam(game_values.gamemode->winningteam);
            mWorldMenu->miWorld->SetCurrentStageToCompleted(game_values.gamemode->winningteam);

            mWorldMenu->CloseStageStart();

            //Clear out the stored powerups after a game that had a winner
            if (!game_values.worldskipscoreboard) {
                //Only clear out the stored powerup if we were allowed to use it in the game
                if (game_values.gamemode->HasStoredPowerups()) {
                    for (short iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++)
                        game_values.storedpowerups[iPlayer] = -1;
                }
            }
        }

        UpdateScoreBoard();

        //If we're suposed to skip the scoreboard, then reset back to the world map
        if (game_values.worldskipscoreboard) {
            mCurrentMenu = mWorldMenu.get();
            mCurrentMenu->ResetMenu();

            game_values.worldskipscoreboard = false;
        } else {
            mTournamentScoreboardMenu->miTournamentScoreboard->RefreshWorldScores(game_values.gamemode->winningteam);
        }
    } else if (game_values.matchtype == MatchType::Tour) {
        mTournamentScoreboardMenu->miTournamentScoreboard->RefreshTourScores();

        if (game_values.tourstopcurrent < game_values.tourstops.size())
            mTourStopMenu->miTourStop->Refresh(game_values.tourstopcurrent);
    } else if (game_values.matchtype == MatchType::Tournament) {
        mTournamentScoreboardMenu->miTournamentScoreboard->StopSwirl();
        if (game_values.gamemode->winningteam > -1) {
            mTournamentScoreboardMenu->miTournamentScoreboard->RefreshTournamentScores(game_values.gamemode->winningteam);

            //Set the next controlling team
            switch (game_values.tournamentcontrolstyle) {
            case TournamentControlStyle::GameWinner: { //Winning Team
                game_values.tournamentcontrolteam = game_values.gamemode->winningteam;
                break;
            }

            case TournamentControlStyle::GameLoser: { //Losing Team
                short iNumInPlace = 0;
                short iInPlace[4];
                short iLowestPlace = 0;
                for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
                    if (g_sim.score[iScore].place == iLowestPlace) {
                        iInPlace[iNumInPlace++] = iScore;
                    } else if (g_sim.score[iScore].place > iLowestPlace) {
                        iNumInPlace = 1;
                        iInPlace[0] = iScore;
                        iLowestPlace = g_sim.score[iScore].place;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::LeadingTeams: { //Tournament Ahead Teams
                short iNumInPlace = 0;
                short iInPlace[4];
                short iMostWins = 0;
                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (game_values.tournament_scores[iTeam].wins == iMostWins) {
                        iInPlace[iNumInPlace++] = iTeam;
                    } else if (game_values.tournament_scores[iTeam].wins > iMostWins) {
                        iNumInPlace = 1;
                        iInPlace[0] = iTeam;
                        iMostWins = game_values.tournament_scores[iTeam].wins;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::TrailingTeams: { //Tournament Behind Teams
                short iNumInPlace = 0;
                short iInPlace[4] = {0,0,0,0};
                short iLeastWins = 20; //Most possible wins are 10

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (game_values.tournament_scores[iTeam].wins == iLeastWins) {
                        iInPlace[iNumInPlace++] = iTeam;
                    } else if (game_values.tournament_scores[iTeam].wins < iLeastWins) {
                        iNumInPlace = 1;
                        iInPlace[0] = iTeam;
                        iLeastWins = game_values.tournament_scores[iTeam].wins;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::Random: { //Random
                game_values.tournamentcontrolteam = RANDOM_INT( g_sim.score_cnt);
                break;
            }

            case TournamentControlStyle::RandomLoser: { //Random Losing Team
                short iNumInPlace = 0;
                short iInPlace[4] = {0, 0, 0, 0};
                short iWinner = 0;

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (g_sim.score[iTeam].place == 0) {
                        iWinner = iTeam;
                        break;
                    }
                }

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (iTeam == iWinner)
                        continue;

                    iInPlace[iNumInPlace++] = iTeam;
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::RoundRobin: { //Round Robin
                game_values.tournamentcontrolteam = game_values.tournamentnextcontrol;

                if (++game_values.tournamentnextcontrol >= g_sim.score_cnt)
                    game_values.tournamentnextcontrol = 0;

                break;
            }

            default: {
                game_values.tournamentcontrolteam = -1;
                break;
            }
            }
        }
    }

    //Reset game mode back to the current game mode in case we came from boss mode
    game_values.gamemode = gamemodes[currentgamemode];

    //Keep track if we entered the menu loop as part of a tournament, if we exit the tournament
    //we need to reset the menu music back to normal
    if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame && game_values.matchtype != MatchType::NetGame)
        fNeedMenuMusicReset = true;

    if (game_values.music) {
        if (game_values.tournamentwinner < 0) {
            if (game_values.matchtype == MatchType::SingleGame || game_values.matchtype == MatchType::QuickGame || game_values.matchtype == MatchType::MiniGame || game_values.matchtype == MatchType::NetGame)
                rm->backgroundmusic[2].play(false, false);
            else if (game_values.matchtype == MatchType::World)
                rm->backgroundmusic[5].play(false, false);
            else
                rm->backgroundmusic[3].play(false, false);
        }
    }

    if (game_values.matchtype != MatchType::World && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::NetGame) {
        if (mCurrentMenu == mGameSettingsMenu.get() || mCurrentMenu == mTournamentScoreboardMenu.get())
            mGameSettingsMenu->miMapField->LoadCurrentMap();
    }

    // On return from a game, refresh room list
    if (netplay.active) {
        if (netplay.currentRoom.roomID) {
            netplay.client.sendLeaveRoomMessage();
            netplay.currentRoom.roomID = 0;
            netplay.joinSuccessful = false;
            netplay.gameRunning = false;
        }
        netplay.client.requestRoomList();
    }
}

void MenuState::update()
{
    if (netplay.active)
        netplay.client.update();

    //Reset the keys that were down the last frame
    game_values.playerInput.ClearPressedKeys(1);

    //handle messages
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            Exit();
            return;
            break;

        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            JoystickManager::instance().handleEvent(event);
            break;

        case SDL_EVENT_KEY_DOWN: {
            if (event.key.key == SDLK_F1) {
                game_values.showfps = !game_values.showfps;
            }

#ifdef _DEBUG
            if (event.key.key == SDLK_F2) {
                game_values.frameadvance = !game_values.frameadvance;
            }
#endif
            if (event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                //ALT + F4 = close window
                if (event.key.key == SDLK_F4) {
                    Exit();
                    return;
                } //ALT + Enter = fullscreen/windowed toggle
                else if (event.key.key == SDLK_RETURN) {
                    game_values.fullscreen = !game_values.fullscreen;
					gfx_changefullscreen(game_values.fullscreen);
                    blitdest = screen;

                    continue;
                }
            }

            if (event.key.key == SDLK_INSERT) {
                gfx_take_screenshot();
            }
#ifdef _DEBUG
            //Pressing insert in debug mode turns on automated testing
            if (event.key.key == SDLK_F8) {
                g_fAutoTest = !g_fAutoTest;

                if (g_fAutoTest) {
                    mCurrentMenu = mMainMenu.get();
                    mCurrentMenu->ResetMenu();

                    LoadScript("Scripts/StartMenuAutomation.txt");
                }
            }

            if (event.key.key == SDLK_HOME) {
                g_fRecordTest = !g_fRecordTest;

                if (g_fRecordTest) {
                    StartRecordScript();
                } else {
                    SaveScript("Scripts/SavedScript.txt");
                }
            }

            if (event.key.key == SDLK_END) {
                AddEmtpyLineToScript();
            }

#endif
            break;

        }

        default:
            break;
        }

        game_values.playerInput.Update(event, 1);
    }

    //If AI is controlling the tournament menu, select the options
    if (game_values.matchtype == MatchType::Tournament && iTournamentAITimer > 0 && mCurrentMenu == mGameSettingsMenu.get() && mGameSettingsMenu->IsOnStartBtn()) {
        if (--iTournamentAITimer == 0) {
            iTournamentAIStep++;

            if (iTournamentAIStep == 1) {
                mGameSettingsMenu->miMapField->ChooseRandomMap();

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 2) {
                currentgamemode = RANDOM_INT(GAMEMODE_LAST);
                game_values.gamemode = gamemodes[currentgamemode];
                mGameSettingsMenu->GameModeChanged(currentgamemode);
                game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 3) {
                SModeOption * options = game_values.gamemode->GetOptions();

                //Choose a goal from the lower values for a quicker game
                short iRandOption = (RANDOM_INT(6)) + 1;
                game_values.gamemode->goal  = options[iRandOption].iValue;

                mGameSettingsMenu->miGoalField[currentgamemode]->setCurrentValue(gamemodes[currentgamemode]->goal);

                mModeOptionsMenu->SetRandomGameModeSettings(game_values.gamemode->gamemode);

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 4) {
                iTournamentAIStep = 0;
                StartGame();
            }
        }
    }

    //Watch for the konami code to unlock the minigames match type

#ifdef _DEBUG
    if (g_fAutoTest)
        GetNextScriptOperation();

    if (g_fRecordTest)
        SetNextScriptOperation();
#endif

    bool fGenerateMapThumbs = false;
    if (AppState::Menu == game_values.appstate) {
        MenuCodeEnum code = MENU_CODE_NONE;
        if (cmd::g_autotest.enabled)
            code = AutoTestMenuUpdate();
        if (MENU_CODE_NONE == code)
            code = mCurrentMenu->SendInput(&game_values.playerInput);

        // Shortcut to game start
        if (netplay.active) {
            uint8_t lastSendType = netplay.client.lastSentMessage.packageType;
            uint8_t lastRecvType = netplay.client.lastReceivedMessage.packageType;

            if (lastSendType == NET_P2G_SYNC_OK
                && lastRecvType == NET_G2E_GAME_START)
            code = MENU_CODE_NET_ROOM_GO;
        }

        if (MENU_CODE_EXIT_APPLICATION == code) {
            Exit();
            return;
        } else if (MENU_CODE_TO_MAIN_MENU == code) {
            iDisplayError = DISPLAY_ERROR_NONE;
            iDisplayErrorTimer = 0;
            net_endSession();

            mCurrentMenu = mMainMenu.get();
        } else if (MENU_CODE_BACK_TO_MATCH_SELECTION_MENU == code) {
            mCurrentMenu = mMatchSelectionMenu.get();
            iUnlockMinigameOptionIndex = 0;
        } else if (MENU_CODE_TO_MATCH_SELECTION_MENU == code) {
            mMatchSelectionMenu->WorldMapChanged();

            mCurrentMenu = mMatchSelectionMenu.get();
            mCurrentMenu->ResetMenu();
            iUnlockMinigameOptionIndex = 0;
        } else if (MENU_CODE_MATCH_SELECTION_START == code || MENU_CODE_QUICK_GAME_START == code) {
            printf("itt vagyok\n");

            if (MENU_CODE_QUICK_GAME_START == code)
                game_values.matchtype = MatchType::QuickGame;
            else
                game_values.matchtype = mMatchSelectionMenu->GetSelectedMatchType();

            mTeamSelectMenu->ResetTeamSelect();
            mCurrentMenu = mTeamSelectMenu.get();
            mCurrentMenu->ResetMenu();
            printf("Hello\n");

            if (game_values.matchtype != MatchType::Tournament) {
                game_values.tournamentcontrolteam = -1;
                SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
            }
        } else if (MENU_CODE_MATCH_SELECTION_MATCH_CHANGED == code) {
            mMatchSelectionMenu->SelectionChanged();
        } else if (MENU_CODE_WORLD_MAP_CHANGED == code) {
            mMatchSelectionMenu->WorldMapChanged();
        } else if (MENU_CODE_TO_OPTIONS_MENU == code) {
            mCurrentMenu = mOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_BACK_TO_OPTIONS_MENU == code) {
            mCurrentMenu = mOptionsMenu.get();
        } else if (MENU_CODE_TO_NET_SERVERS_MENU == code) {
            mCurrentMenu = mNetServersMenu.get();
            mCurrentMenu->ResetMenu();
            net_startSession();
        } else if (MENU_CODE_BACK_TO_GRAPHIC_OPTIONS_MENU == code) {
            mCurrentMenu = mGraphicsOptionsMenu.get();
        } else if (MENU_CODE_TO_CONTROLS_MENU == code) {
            mCurrentMenu = mPlayerControlsSelectMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_BACK_TO_CONTROLS_MENU == code) {
            mCurrentMenu = mPlayerControlsSelectMenu.get();
        } else if (MENU_CODE_TO_PLAYER_1_CONTROLS == code) {
            mPlayerControlsMenu->SetPlayer(0);
            mCurrentMenu = mPlayerControlsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_PLAYER_2_CONTROLS == code) {
            mPlayerControlsMenu->SetPlayer(1);
            mCurrentMenu = mPlayerControlsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_PLAYER_3_CONTROLS == code) {
            mPlayerControlsMenu->SetPlayer(2);
            mCurrentMenu = mPlayerControlsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_PLAYER_4_CONTROLS == code) {
            mPlayerControlsMenu->SetPlayer(3);
            mCurrentMenu = mPlayerControlsMenu.get();
            mCurrentMenu->ResetMenu();
        }
        else if (MENU_CODE_TOGGLE_FULLSCREEN == code) {
			gfx_changefullscreen(game_values.fullscreen);
            blitdest = screen;
        }
        else if (MENU_CODE_TO_GAME_SETUP_MENU == code) {
            printf("MENU_CODE_TO_GAME_SETUP_MENU\n");
            //Moves teams to the first arrays in the list and counts the number of teams
            g_sim.score_cnt = mTeamSelectMenu->GetTeamCount();
            iDisplayError = DISPLAY_ERROR_NONE;
            iDisplayErrorTimer = 0;
            bool fErrorReadingTourFile = false;

            if (MatchType::MiniGame == game_values.matchtype) {
                printf(" Match type: Minigame\n");
                const Minigame minigame = mMatchSelectionMenu->GetMinigame();
                switch (minigame) {
                    case Minigame::PipeCoin:
                        pipegamemode->goal = 50;
                        game_values.gamemode = pipegamemode;
                        break;
                    case Minigame::HammerBoss:
                    case Minigame::BombBoss:
                    case Minigame::FireBoss:
                        game_values.gamemodemenusettings.boss.difficulty = 2;
                        game_values.gamemodemenusettings.boss.hitpoints = 5;

                        bossgamemode->goal = 5;
                        game_values.gamemode = bossgamemode;
                        break;
                    case Minigame::Boxes:
                        boxesgamemode->goal = 10;
                        game_values.gamemode = boxesgamemode;
                        break;
                }
                switch (minigame) {
                    case Minigame::HammerBoss:
                        game_values.gamemodemenusettings.boss.bosstype = Boss::Hammer;
                        break;
                    case Minigame::BombBoss:
                        game_values.gamemodemenusettings.boss.bosstype = Boss::Bomb;
                        break;
                    case Minigame::FireBoss:
                        game_values.gamemodemenusettings.boss.bosstype = Boss::Fire;
                        break;
                    default:
                        break;
                }
                StartGame();
            } else if (MatchType::QuickGame == game_values.matchtype) {
                printf(" Match type: Quick game\n");
                short iRandomMode = RANDOM_INT( GAMEMODE_LAST);
                game_values.gamemode = gamemodes[iRandomMode];

                SModeOption * options = game_values.gamemode->GetOptions();

                //Choose a goal from the lower values for a quicker game
                short iRandOption = (RANDOM_INT(6)) + 1;
                game_values.gamemode->goal  = options[iRandOption].iValue;

                game_values.tournamentwinner = -1;

                StartGame();
            } else {

                //Load the tour here if one was selected
                if (game_values.matchtype == MatchType::Tour) {
                    printf("  Match type: Tour\n");
                    if (!ReadTourFile()) {
                        iDisplayError = DISPLAY_ERROR_READ_TOUR_FILE;
                        iDisplayErrorTimer = 120;
                        fErrorReadingTourFile = true;
                    } else {
                        mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tourstops.size(), &rm->spr_tour_markers);
                    }
                } else if (game_values.matchtype == MatchType::Tournament) {
                    printf("  Match type: Tournament\n");
                    //Set who is controlling the tournament menu
                    if (game_values.tournamentcontrolstyle == TournamentControlStyle::Random || game_values.tournamentcontrolstyle == TournamentControlStyle::RandomLoser) //Random
                        game_values.tournamentcontrolteam = RANDOM_INT( g_sim.score_cnt);
                    else if (game_values.tournamentcontrolstyle == TournamentControlStyle::RoundRobin) //Round robin
                        game_values.tournamentcontrolteam = 0;
                    else //The first game of the tournament is controlled by all players
                        game_values.tournamentcontrolteam = -1;

                    game_values.tournamentnextcontrol = 1;  //if round robin is selected, choose the next team next

                    mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tournamentgames, &rm->menu_mode_large);
                } else if (game_values.matchtype == MatchType::World) {
                    printf("  Match type: World\n");
                    try {
                        g_worldmap = loadWorldMap(worldlist->at(game_values.worldindex).string(), TILESIZE);

                        mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, 0, &rm->spr_tour_markers);

                        g_worldmap.SetInitialPowerups();

                        //If a player had a stored powerup from another game, add it to their inventory
                        for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
                            if (game_values.storedpowerups[iPlayer] != -1) {
                                short iTeamId = LookupTeamID(iPlayer);
                                if (game_values.worldpowerupcount[iTeamId] < 32)
                                    game_values.worldpowerups[iTeamId][game_values.worldpowerupcount[iTeamId]++] = game_values.storedpowerups[iPlayer];

                                game_values.storedpowerups[iPlayer] = -1;
                            }
                        }

                        mWorldMenu->miWorld->Init();
                        mWorldMenu->miWorld->SetControllingTeam(RANDOM_INT( g_sim.score_cnt));
                    }
                    catch (const std::runtime_error& err) {
                        iDisplayError = DISPLAY_ERROR_READ_WORLD_FILE;
                        iDisplayErrorTimer = 120;
                        fErrorReadingTourFile = true;
                    }
                }

                if (!fErrorReadingTourFile) {
                    printf("  !fErrorReadingTourFile\n");
                    mTournamentScoreboardMenu->ClearEyeCandy();

                    //Initialize tournament values
                    game_values.tournamentwinner = -1;

                    //Setup wins counters for tournament/tour
                    for (int k = 0; k < 4; k++) {
                        game_values.tournament_scores[k].wins = 0;
                        game_values.tournament_scores[k].total = 0;
                    }

                    if (MatchType::SingleGame == game_values.matchtype || MatchType::Tournament == game_values.matchtype || MatchType::NetGame == game_values.matchtype) {
                        printf("  MatchType::SingleGame || MatchType::Tournament\n");
                        printf("current map: %s\n", szCurrentMapName);
                        maplist->findexact(szCurrentMapName, false);
                        mGameSettingsMenu->miMapField->LoadCurrentMap();

                        game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];

                        for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++) {
                            gamemodes[iMode]->goal = mGameSettingsMenu->miGoalField[iMode]->currentValue();
                        }

                        if (mGameSettingsMenu->GetCurrentGameModeID() == game_mode_owned)
                            mGameSettingsMenu->HideGMSettingsBtn();

                        mCurrentMenu = mGameSettingsMenu.get();
                        mCurrentMenu->ResetMenu();

                        //If it is a tournament, then set the controlling team
                        if (MatchType::Tournament == game_values.matchtype)
                            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, true);
                        } else if (MatchType::Tour == game_values.matchtype) {
                            mCurrentMenu = mTourStopMenu.get();
                            mCurrentMenu->ResetMenu();
                        } else if (MatchType::World == game_values.matchtype) {
                            game_values.screenfadespeed = 8;
                            game_values.screenfade = 8;
                            game_values.appstate = AppState::StartWorld;

                            //Play enter world sound
                            rm->sfx_enterstage.play();
                        }

                    //Setup items on next menu
                    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
                        mGameSettingsMenu->miGoalField[iGameMode]->hideItem(-1, game_values.matchtype == MatchType::Tournament);
                    }

                    if (game_values.matchtype == MatchType::World)
                        mGameSettingsMenu->SetHeaderText("World Game Menu");
                    else if (game_values.matchtype == MatchType::Tour)
                        mGameSettingsMenu->SetHeaderText("Tour Game Menu");
                    else if (game_values.matchtype == MatchType::Tournament)
                        mGameSettingsMenu->SetHeaderText("Tournament Game Menu");
                    else
                        mGameSettingsMenu->SetHeaderText("Single Game Menu");

                }
            }
        } else if (MENU_CODE_BACK_TO_GAME_SETUP_MENU == code) {
            bool fNeedTeamAnnouncement = false;
            if (game_values.matchtype == MatchType::World) {
                if (game_values.tournamentwinner == -2 || (game_values.tournamentwinner >= 0 && game_values.bonuswheel == BonusWheelStyle::Off)) {
                    ResetTournamentBackToMainMenu();
                } else if (game_values.tournamentwinner >= 0) {
                    mBonusWheelMenu->miBonusWheel->Reset(true);
                    mCurrentMenu = mBonusWheelMenu.get();
                } else {
                    mCurrentMenu = mWorldMenu.get();
                    mWorldMenu->miWorldStop->Refresh(game_values.tourstopcurrent);

                    fNeedTeamAnnouncement = true;
                }
            } else {
                if (game_values.tournamentwinner == -2) { //Tied Tour Result
                    ResetTournamentBackToMainMenu();
                } else if (game_values.tournamentwinner >= 0) { //Tournament/Tour Won and Bonus Wheel will be spun
                    if (game_values.bonuswheel == BonusWheelStyle::Off || (game_values.matchtype == MatchType::Tour && !game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType)) {
                        ResetTournamentBackToMainMenu();
                    } else {
                        mBonusWheelMenu->miBonusWheel->Reset(true);
                        mCurrentMenu = mBonusWheelMenu.get();
                    }
                } else { //Next Tour/Tourament Game
                    if (game_values.matchtype == MatchType::Tour) {
                        mCurrentMenu = mTourStopMenu.get();
                    } else {
                        mCurrentMenu = mGameSettingsMenu.get();
                    }
                }
            }

            mCurrentMenu->ResetMenu();

            //Set the controlling team for tournament mode
            if (MatchType::Tournament == game_values.matchtype && game_values.tournamentwinner == -1)
                SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, true);

            if (fNeedTeamAnnouncement)
                mWorldMenu->miWorld->DisplayTeamControlAnnouncement();
        } else if (MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS == code) {
            if (netplay.active)
                mCurrentMenu = mNetNewRoomSettingsMenu.get();
            else
                mCurrentMenu = mGameSettingsMenu.get();
        } else if (MENU_CODE_MODE_CHANGED == code) {
            game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
            mGameSettingsMenu->RefreshGameModeButtons();
            mNetNewRoomSettingsMenu->RefreshGameModeButtons();
        } else if (MENU_CODE_BACK_TEAM_SELECT_MENU == code) {
            if (game_values.matchtype == MatchType::World) {
                mWorldMenu->OpenExitDialog();
            } else if (game_values.matchtype == MatchType::Tour) {
                mTourStopMenu->OpenExitDialog();
            } else if (game_values.matchtype == MatchType::Tournament) {
                mGameSettingsMenu->OpenExitDialog();

                if (iTournamentAITimer > 0)
                    SetControllingTeamForSettingsMenu(-1, false);
            } else {
                mCurrentMenu = mTeamSelectMenu.get();
                mCurrentMenu->ResetMenu();
            }
        } else if (MENU_CODE_SOUND_VOLUME_CHANGED == code) {
            game_values.sound = game_values.soundvolume > 0;
            sfx_setsoundvolume(game_values.soundvolume);
            ifSoundOnPlay(rm->sfx_coin);
        } else if (MENU_CODE_MUSIC_VOLUME_CHANGED == code) {
            sfx_setmusicvolume(game_values.musicvolume);

            if (game_values.musicvolume == 0)
                rm->backgroundmusic[2].stop();
            else if (game_values.musicvolume > 0 && !game_values.music)
                rm->backgroundmusic[2].play(false, false);

            game_values.music = game_values.musicvolume > 0;
        } else if (MENU_CODE_START_GAME == code) {
            StartGame();
        } else if (MENU_CODE_EXIT_TOURNAMENT_YES == code || MENU_CODE_EXIT_TOURNAMENT_NO == code) {
            mGameSettingsMenu->CloseExitDialog();

            if (MENU_CODE_EXIT_TOURNAMENT_YES == code)
                ResetTournamentBackToMainMenu();
            else
                SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
        } else if (MENU_CODE_EXIT_TOUR_YES == code || MENU_CODE_EXIT_TOUR_NO == code) {
            mTourStopMenu->CloseExitDialog();

            if (MENU_CODE_EXIT_TOUR_YES == code)
                ResetTournamentBackToMainMenu();
        } else if (MENU_CODE_EXIT_WORLD_YES == code || MENU_CODE_EXIT_WORLD_NO == code) {
            mWorldMenu->CloseExitDialog();

            if (MENU_CODE_EXIT_WORLD_YES == code) {
                //Clear out any stored items a player might have
                for (short iPlayer = 0; iPlayer < 4; iPlayer++)
                    game_values.storedpowerups[iPlayer] = -1;

                ResetTournamentBackToMainMenu();
                mWorldMenu->miWorldStop->setVisible(false);
            }
        } else if (MENU_CODE_BONUS_DONE == code) {
            if (mBonusWheelMenu->miBonusWheel->GetPowerupSelectionDone()) {
                if (game_values.matchtype == MatchType::World) {
                    if (game_values.tournamentwinner == -1)
                        mCurrentMenu = mTournamentScoreboardMenu.get();
                    else
                        ResetTournamentBackToMainMenu();
                } else {
                    if ((game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament) && game_values.tournamentwinner == -1 &&
                            ((game_values.matchtype != MatchType::Tour && game_values.bonuswheel == BonusWheelStyle::EveryGame) || (game_values.matchtype == MatchType::Tour && game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType))) {
                        mCurrentMenu = mTournamentScoreboardMenu.get();
                    } else if (game_values.matchtype == MatchType::SingleGame) {
                        mCurrentMenu = mGameSettingsMenu.get();
                        mCurrentMenu->ResetMenu();
                        mGameSettingsMenu->miMapField->LoadCurrentMap();
                    } else {
                        ResetTournamentBackToMainMenu();
                    }
                }
            }
        } else if (MENU_CODE_TO_POWERUP_SELECTION_MENU == code) {
            mCurrentMenu = mPowerupDropRatesMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_POWERUP_SETTINGS_MENU == code) {
            mCurrentMenu = mPowerupSettingsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_GRAPHICS_OPTIONS_MENU == code) {
            mCurrentMenu = mGraphicsOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_EYECANDY_OPTIONS_MENU == code) {
            mCurrentMenu = mEyeCandyOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_SOUND_OPTIONS_MENU == code) {
            mCurrentMenu = mSoundOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU == code) {
            mCurrentMenu = mGameplayOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_TEAM_OPTIONS_MENU == code) {
            mCurrentMenu = mTeamOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_PROJECTILES_OPTIONS_MENU == code) {
            mCurrentMenu = mProjectileOptionsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_PROJECTILES_LIMITS_MENU == code) {
            mCurrentMenu = mProjectileLimitsMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_MODE_SETTINGS_MENU == code) {
            if (netplay.active) {
                mCurrentMenu = mModeOptionsMenu->GetOptionsMenu(mNetNewRoomSettingsMenu->getSelectedGameModeID());
                mCurrentMenu->ResetMenu();
            }
            else {
                for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
                    if (mGameSettingsMenu->miGoalField[iGameMode]->IsVisible()) {
                        mCurrentMenu = mModeOptionsMenu->GetOptionsMenu(iGameMode);
                        mCurrentMenu->ResetMenu();
                        break;
                    }
                }
            }
        } else if (MENU_CODE_MENU_GRAPHICS_PACK_CHANGED == code) {
            rm->loadMenuGraphics();

            blitdest = rm->menu_backdrop.getSurface();
            rm->menu_shade.setalpha(App::menuTransparency);
            rm->menu_shade.draw(0, 0);
            blitdest = screen;
        } else if (MENU_CODE_WORLD_GRAPHICS_PACK_CHANGED == code) {
            rm->loadWorldGraphics();
        } else if (MENU_CODE_GAME_GRAPHICS_PACK_CHANGED == code) {
            try {
                gfx_loadpalette(convertPath("gfx/packs/palette.png", gamegraphicspacklist->currentPath()));
            } catch (const std::string& err) {
                printf("\nwarning: %s -> falling back to BMP\n", err.c_str());
                gfx_loadpalette(convertPath("gfx/packs/palette.bmp", gamegraphicspacklist->currentPath()));
            }
            rm->loadGameGraphics();
        } else if (MENU_CODE_SOUND_PACK_CHANGED == code) {
            rm->LoadGameSounds();

            if (!game_values.soundcapable) {
                game_values.sound = false;
                game_values.music = false;
                game_values.soundvolume = 0;
                game_values.musicvolume = 0;
            }
        } else if (MENU_CODE_WORLD_STAGE_START == code) {
            mWorldMenu->OpenStageStart();
        } else if (MENU_CODE_WORLD_STAGE_NO_START == code) {
            mWorldMenu->CloseStageStart();
        } else if (MENU_CODE_WORLD_MUSIC_CHANGED == code) {
            worldmusiclist->setCurrent(mSoundOptionsMenu->GetCurrentWorldMusicID());
        } else if (MENU_CODE_TOUR_STOP_CONTINUE == code || MENU_CODE_TOUR_STOP_CONTINUE_FORCED == code) {
            //If this tour stop is forced, we need to load the map first
            if (MENU_CODE_TOUR_STOP_CONTINUE_FORCED == code)
                mWorldMenu->miWorldStop->Refresh(game_values.tourstopcurrent);

            mWorldMenu->miWorld->ClearCloud();

            //Tour bonus house
            if (game_values.matchtype == MatchType::World && game_values.tourstops[game_values.tourstopcurrent]->iStageType == 1) {
                bonushousemode->goal = 0;
                game_values.gamemode = bonushousemode;
            } else {
                short iGameMode = game_values.tourstops[game_values.tourstopcurrent]->iMode;

                if (iGameMode == game_mode_pipe_minigame)
                    game_values.gamemode = pipegamemode;
                else if (iGameMode == game_mode_boss_minigame)
                    game_values.gamemode = bossgamemode;
                else if (iGameMode == game_mode_boxes_minigame)
                    game_values.gamemode = boxesgamemode;
                else
                    game_values.gamemode = gamemodes[iGameMode];

                game_values.gamemode->goal = game_values.tourstops[game_values.tourstopcurrent]->iGoal;
            }

            StartGame();
        } else if (MENU_CODE_RESET_STORED_POWERUPS == code) {
            for (short iPlayer = 0; iPlayer < 4; iPlayer++)
                game_values.storedpowerups[iPlayer] = -1;
        } else if (MENU_CODE_MAP_CHANGED == code) {
            if (mCurrentMenu == mNetNewRoomSettingsMenu.get()) {
                assert(netplay.active);
                netplay.mapfilepath = mNetNewRoomSettingsMenu->getCurrentMapPath();
                printf("[net] Selected map: %s\n", netplay.mapfilepath.c_str());
                mNetRoomMenu->SetPreviewMapPath(netplay.mapfilepath);
            }
            else if (game_values.matchtype != MatchType::Tour)
                szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
        } else if (MENU_CODE_MAP_FILTER_EXIT == code) {
            maplist->ApplyFilters(game_values.pfFilters);

            //If the filtered map list has at least 1 map in it, then allow exiting the filter menu
            if (maplist->MapInFilteredSet()) {
                mGameSettingsMenu->miMapField->LoadCurrentMap();
                szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();

                mGameSettingsMenu->CloseMapFilters();

                iDisplayError = DISPLAY_ERROR_NONE;
            } else { //otherwise display a message
                iDisplayError = DISPLAY_ERROR_MAP_FILTER;
                iDisplayErrorTimer = 120;
            }
        } else if (MENU_CODE_TO_MAP_FILTERS == code) {
            mGameSettingsMenu->OpenMapFilters();
        } else if (MENU_CODE_TO_MAP_FILTER_EDIT == code) {
            mMapFilterEditMenu->miMapBrowser->Reset(0);

            mCurrentMenu = mMapFilterEditMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_MAP_BROWSER_EXIT == code) {
            mGameSettingsMenu->miMapField->LoadCurrentMap();
            szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();

            mCurrentMenu = mGameSettingsMenu.get();
            //mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_TO_MAP_BROWSER_THUMBNAILS == code) {
            mMapFilterEditMenu->miMapBrowser->Reset(1);

            mCurrentMenu = mMapFilterEditMenu.get();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_SAVE_ALL_MAP_THUMBNAILS == code) {
            (static_cast<UI_OptionsMenu*>(mCurrentMenu))->showThumbnailsPopup();
            mCurrentMenu->ResetMenu();
        } else if (MENU_CODE_GENERATE_THUMBS_RESET_YES == code || MENU_CODE_GENERATE_THUMBS_RESET_NO == code) {
            (static_cast<UI_OptionsMenu*>(mCurrentMenu))->hideThumbnailsPopup();

            if (MENU_CODE_GENERATE_THUMBS_RESET_YES == code) {
                fGenerateMapThumbs = true;
            }
        } else if (MENU_CODE_HEALTH_MODE_START_LIFE_CHANGED == code) {
            mModeOptionsMenu->HealthModeStartLifeChanged();
        } else if (MENU_CODE_HEALTH_MODE_MAX_LIFE_CHANGED == code) {
            mModeOptionsMenu->HealthModeMaxLifeChanged();
        }

        if (netplay.active) {

            LastMessage lastSent = netplay.client.lastSentMessage;
            LastMessage lastRecv = netplay.client.lastReceivedMessage;

            // Override menu code if response has arrived
            if (netplay.operationInProgress)
            {
                MenuCodeEnum previousCode = code;

                //printf("\rlastSendType: %d, lastRecvType: %d", lastSent.packageType, lastRecv.packageType);

                // if sent connect request,
                // received connect_ok
                // then sent skin change
                if (lastSent.packageType == NET_NOTICE_SKIN_CHANGE
                        && lastRecv.packageType == NET_RESPONSE_CONNECT_OK
                        && lastSent.timestamp >= lastRecv.timestamp)
                    code = MENU_CODE_TO_NET_LOBBY_MENU;

                else if (lastSent.packageType == NET_REQUEST_JOIN_ROOM
                        && lastRecv.packageType == NET_RESPONSE_JOIN_OK
                        && lastSent.timestamp < lastRecv.timestamp) // ensure that the incoming message arrived after the request
                    code = MENU_CODE_TO_NET_ROOM_MENU;

                else if (lastSent.packageType == NET_NOTICE_GAMEMODESETTINGS
                        && lastRecv.packageType == NET_RESPONSE_CREATE_OK)
                    code = MENU_CODE_TO_NET_ROOM_MENU;

                // ide a hibakezelést
                // if lastmessage ez és lastresponse emez
                // control.settext...

                // If we have finished the waiting
                if (code != previousCode)
                    netplay.operationInProgress = false;
            }

            if (MENU_CODE_TO_NET_SERVERLIST == code) {
                mNetServersMenu->OpenServerList();
            } else if (MENU_CODE_TO_NET_ADDREMOVE_SERVER_MENU == code) {
                mCurrentMenu = mNetEditServersMenu.get();
                mCurrentMenu->ResetMenu();
                mNetEditServersMenu->Restore();
            } else if (MENU_CODE_NET_ADDREMOVE_SERVER_ON_ADD_BTN == code) {
                mNetEditServersMenu->onPressAdd();
            } else if (MENU_CODE_NET_ADDREMOVE_SERVER_ON_EDIT_BTN == code) {
                mNetEditServersMenu->onPressEdit();
            } else if (MENU_CODE_NET_ADDREMOVE_SERVER_ON_DELETE_BTN == code) {
                mNetEditServersMenu->onPressDelete();
            } else if (MENU_CODE_NET_ADDREMOVE_SERVER_ON_SELECT == code) {
                mNetEditServersMenu->onEntrySelect();
                mNetServersMenu->RefreshScroll();
            } else if (MENU_CODE_NET_ADDREMOVE_SERVER_ON_DIALOG_OK_BTN == code) {
                mNetEditServersMenu->onDialogOk();
                mNetServersMenu->RefreshScroll();
            } else if (MENU_CODE_NET_SERVERLIST_EXIT == code || MENU_CODE_NET_CONNECT_ABORT == code) {
                if (MENU_CODE_NET_SERVERLIST_EXIT == code)
                    netplay.currentMenuChanged = true;
                else {
                    net_startSession(); // release & restart socket
                    netplay.operationInProgress = false;
                }

                mNetServersMenu->Restore();
                iDisplayError = DISPLAY_ERROR_NONE;
            } else if (MENU_CODE_NET_CONNECT_IN_PROGRESS == code) {
                mNetServersMenu->ConnectInProgress();
            } else if (MENU_CODE_TO_NET_LOBBY_MENU == code) {
                // If we are leaving a room
                if (netplay.currentRoom.roomID) {
                    netplay.client.sendLeaveRoomMessage();
                    netplay.currentRoom.roomID = 0;
                    netplay.joinSuccessful = false;
                    netplay.gameRunning = false;
                }
                netplay.client.requestRoomList();

                // Restore layouts
                    mNetServersMenu->Restore();
                    mNetLobbyMenu->Restore();
                    mNetNewRoomMenu->Restore();

                mCurrentMenu = mNetLobbyMenu.get();
                mCurrentMenu->ResetMenu();
            } else if (MENU_CODE_TO_NET_NEW_ROOM_LEVEL_SELECT_MENU == code) {
                mCurrentMenu = mNetNewRoomSettingsMenu.get();
                mCurrentMenu->ResetMenu();
            } else if (MENU_CODE_TO_NET_NEW_ROOM_SETTINGS_MENU == code) {
                mCurrentMenu = mNetNewRoomMenu.get();
                mCurrentMenu->ResetMenu();
            } else if (MENU_CODE_NET_CHAT_SEND == code) {
                if (!netplay.mychatmessage.empty())
                    netplay.client.sendChatMessage(netplay.mychatmessage.c_str());
                else
                    code = MENU_CODE_TO_NET_ROOM_MENU;
            } else if (MENU_CODE_TO_NET_ROOM_MENU == code) {
                mCurrentMenu = mNetRoomMenu.get();
                mCurrentMenu->ResetMenu();
                netplay.currentMenuChanged = true;
                netplay.operationInProgress = false;

                mNetRoomMenu->Restore(); // Restore Room layout
                mNetLobbyMenu->Restore(); // Restore Lobby layout
                mNetNewRoomMenu->Restore(); // remove 'in progress' dialog
            } else if (MENU_CODE_NET_JOIN_ROOM_IN_PROGRESS == code) {
                mNetLobbyMenu->JoinInProgress();
            } else if (MENU_CODE_NET_JOIN_ROOM_ABORT == code) {
                mNetLobbyMenu->AbortJoin();
                iDisplayError = DISPLAY_ERROR_NONE;
            } else if (MENU_CODE_TO_NET_NEW_ROOM_CREATE_IN_PROGRESS == code) {
                mNetNewRoomMenu->CreateInProgress();
            } else if (MENU_CODE_TO_NET_NEW_ROOM_CREATE_ABORT == code) {
                mNetNewRoomMenu->AbortCreate();
                iDisplayError = DISPLAY_ERROR_NONE;
            } else if (MENU_CODE_TO_NET_ROOM_START_IN_PROGRESS == code) {
                mNetRoomMenu->StartInProgress();
            } else if (MENU_CODE_NET_ROOM_GO == code) {
                netplay.operationInProgress = false;
                game_values.matchtype = MatchType::NetGame;

                mTeamSelectMenu->ResetTeamSelect();
                mTeamSelectMenu->ResetMenu();
                g_sim.score_cnt = mTeamSelectMenu->GetTeamCount();

                game_values.tournamentcontrolteam = -1;
                game_values.tournamentwinner = -1;

                iDisplayError = DISPLAY_ERROR_NONE;
                iDisplayErrorTimer = 0;

                //game_values.noexit = true;
                StartGame();
            }

            // on room change
            if (netplay.currentMenuChanged) {
                //printf("menuChanged\n");

                mNetServersMenu->Refresh();
                mNetRoomMenu->Refresh();

                netplay.currentMenuChanged = false;
            }
        }

    }

    //--------------- draw everything ----------------------

    //Don't draw backdrop for world
    if (mCurrentMenu != mWorldMenu.get())
        rm->menu_backdrop.draw(0, 0);
    else
        SDL_FillSurfaceRect(screen, nullptr, SDL_MapSurfaceRGB(screen, 0, 0, 0));

    mCurrentMenu->Update();
    mCurrentMenu->Draw();

    if (iDisplayError > DISPLAY_ERROR_NONE) {
        rm->spr_selectfield.draw(70, 400, {0, 0, 484, 32});
        rm->spr_selectfield.draw(554, 400, {496, 0, 16, 32});

        if (iDisplayError == DISPLAY_ERROR_READ_TOUR_FILE)
            rm->menu_font_large.drawCentered(320, 405, "Error Reading Tour File!");
        if (iDisplayError == DISPLAY_ERROR_READ_WORLD_FILE)
            rm->menu_font_large.drawCentered(320, 405, "Error Reading World File!");
        else if (iDisplayError == DISPLAY_ERROR_MAP_FILTER)
            rm->menu_font_large.drawCentered(320, 405, "No Maps Meet All Filter Conditions!");

        if (--iDisplayErrorTimer == 0)
            iDisplayError = DISPLAY_ERROR_NONE;
    }

    if (game_values.screenfadespeed != 0) {
        game_values.screenfade += game_values.screenfadespeed;

        if (game_values.screenfade <= 0) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 0;
        } else if (game_values.screenfade >= 255) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 255;
        }
    }

    if (game_values.screenfade > 0) {
        rm->menu_shade.setalpha(static_cast<Uint8>(game_values.screenfade));
        rm->menu_shade.draw(0, 0);
    }

    if (game_values.screenfade == 255) {
        if (AppState::StartGame == game_values.appstate) {
            if (game_values.matchtype == MatchType::QuickGame)
                mModeOptionsMenu->SetRandomGameModeSettings(game_values.gamemode->gamemode);
            else if (game_values.matchtype == MatchType::NetGame)
                // TODO: set from network
                mModeOptionsMenu->SetRandomGameModeSettings(game_values.gamemode->gamemode);
            else
                SetGameModeSettingsFromMenu();

            if (game_values.matchtype == MatchType::World && game_values.tourstops[game_values.tourstopcurrent]->iStageType == 1) {
                g_map->loadMap(convertPath("maps/special/two52_special_bonushouse.map"), read_type_full);
                LoadCurrentMapBackground();

                if (game_values.music) {
                    rm->backgroundmusic[0] = sfxMusic(worldmusiclist->currentMusic(WorldMusicCategory::Bonus, ""));
                    rm->backgroundmusic[0].play(false, false);
                }
            } else {
                std::string sShortMapName = "";

                bool fMiniGameMapFound = false;

                if (game_values.matchtype == MatchType::World) {
                    if (game_values.gamemode->gamemode == game_mode_pipe_minigame ||
                            game_values.gamemode->gamemode == game_mode_boss_minigame ||
                            game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                        fMiniGameMapFound = maplist->findexact(game_values.tourstops[game_values.tourstopcurrent]->pszMapFile.c_str(), true);

                        if (fMiniGameMapFound) {
                            g_map->loadMap(maplist->currentFilename(), read_type_full);
                            sShortMapName = maplist->currentShortmapname();
                        }
                    }
                }

                if (game_values.gamemode->gamemode == game_mode_pipe_minigame) {
                    if (!fMiniGameMapFound) {
                        g_map->loadMap(convertPath("maps/special/two52_special_pipe_minigame.map"), read_type_full);
                        sShortMapName = "minigamepipe";
                    }
                } else if (game_values.gamemode->gamemode == game_mode_boss_minigame) {
                    if (!fMiniGameMapFound) {
                        Boss bossType = game_values.gamemodesettings.boss.bosstype;
                        bossgamemode->SetBossType(bossType);
                        switch (bossType) {
                            case Boss::Hammer:
                                g_map->loadMap(convertPath("maps/special/two52_special_hammerboss_minigame.map"), read_type_full);
                                break;
                            case Boss::Bomb:
                                g_map->loadMap(convertPath("maps/special/two52_special_bombboss_minigame.map"), read_type_full);
                                break;
                            case Boss::Fire:
                                g_map->loadMap(convertPath("maps/special/two52_special_fireboss_minigame.map"), read_type_full);
                                break;
                        }
                        sShortMapName = "minigameboss";
                    }
                } else if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                    if (!fMiniGameMapFound) {
                        g_map->loadMap(convertPath("maps/special/two52_special_boxes_minigame.map"), read_type_full);
                        sShortMapName = "minigameboxes";
                    }
                } else if (game_values.matchtype == MatchType::QuickGame) {
                    //Load a random map for the quick game
                    std::string szMapName = maplist->randomFilename();
                    g_map->loadMap(szMapName, read_type_full);
                    sShortMapName = stripPathAndExtension(szMapName);

                    printf("  State: GS_START_GAME, Match type: MatchType::QuickGame\n");
                } else if (netplay.active) {
                    // NOTE: for the host, netplay.mapfilepath will be ./data/something
                    // while for the other players, it's ~/.smw/net_last.map
                    g_map->loadMap(netplay.mapfilepath, read_type_full);
                    // TODO: this is used for setting background music, but
                    // since the filename is always the same, this will fall
                    // back to a random music in the loaded music category
                    sShortMapName = stripPathAndExtension(netplay.mapfilepath);
                } else {
                    g_map->loadMap(maplist->currentFilename(), read_type_full);
                    sShortMapName = maplist->currentShortmapname();
                }

                LoadCurrentMapBackground();

                //Allows all players to start the game
                game_values.singleplayermode = -1;

                if (game_values.music) {
                    musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), sShortMapName.c_str(), g_map->szBackgroundFile);
                    rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
                    rm->backgroundmusic[0].play(game_values.playnextmusic, false);
                }
            }

            game_values.appstate = AppState::Game;
            printf("  GS_GAME\n");

            g_map->predrawbackground(rm->spr_background, rm->spr_backmap[0]);
            g_map->predrawforeground(rm->spr_frontmap[0]);

            g_map->predrawbackground(rm->spr_background, rm->spr_backmap[1]);
            g_map->predrawforeground(rm->spr_frontmap[1]);

            g_map->SetupAnimatedTiles();
            LoadMapObjects(false);

            GameStateManager::instance().changeStateTo(&GameplayState::instance());
            return;
        } else if (AppState::StartWorld == game_values.appstate) { //Fade to world match type
            game_values.screenfadespeed = -8;

            mCurrentMenu = mWorldMenu.get();
            mCurrentMenu->ResetMenu();

            rm->backgroundmusic[2].stop();
            rm->backgroundmusic[5] = sfxMusic(worldmusiclist->currentMusic(g_worldmap.GetMusicCategory(), g_worldmap.GetWorldName()));
            rm->backgroundmusic[5].play(false, false);
            fNeedMenuMusicReset = true;

            mWorldMenu->miWorld->DisplayTeamControlAnnouncement();

            game_values.appstate = AppState::Menu;
        }
    }

    if (fGenerateMapThumbs) {
        rm->menu_dialog.draw(160, 176, {0, 0, 160, 64});
		rm->menu_dialog.draw(App::screenWidth/2, 176, {352, 0, 160, 64});
        rm->menu_dialog.draw(160, 240, {0, 416, 160, 64});
		rm->menu_dialog.draw(App::screenWidth/2, App::screenHeight/2, {352, 416, 160, 64});
		rm->menu_font_large.drawCentered(App::screenWidth/2, 215, "Refreshing Map Thumbnails");
		rm->menu_font_large.drawCentered(App::screenWidth/2, 245, "Please Wait...");
    }


#ifdef _DEBUG
    if (g_fAutoTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Auto");

    if (g_fRecordTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Recording...");
#endif

    if (fGenerateMapThumbs) {
        fGenerateMapThumbs = false;
        rm->backgroundmusic[2].togglePause();

        //Reload map auto filters from live map files (don't use the cache)
        maplist->ReloadMapAutoFilters();

        //Write out all the map thumbnails for the map browser and filter editor
        std::map<std::string, MapListNode>::iterator itr = maplist->GetIteratorAt(0, false);

        short iMapCount = maplist->count();
        for (short iMap = 0; iMap < iMapCount; iMap++) {
            std::string szThumbnail("maps/cache/");
            szThumbnail += GetNameFromFileName(itr->second.filename);
            szThumbnail += ".png";

            g_map->loadMap((*itr).second.filename, read_type_preview);
            g_map->saveThumbnail(convertPath(szThumbnail), false);

            itr++;
        }

        rm->backgroundmusic[2].togglePause();
    }
}

void MenuState::SetControllingTeamForSettingsMenu(short iControlTeam, bool fDisplayMessage)
{
    mGameSettingsMenu->SetControllingTeam(iControlTeam);

    mModeOptionsMenu->SetControllingTeam(iControlTeam);

    mGameSettingsMenu->miMapFilterScroll->SetControllingTeam(iControlTeam);
    mMapFilterEditMenu->miMapBrowser->SetControllingTeam(iControlTeam);

    if (fDisplayMessage)
        DisplayControllingTeamMessage(iControlTeam);

    //Scan controlling team members and if they are only bots, then let the cpu control the selection
    iTournamentAITimer = 0;
    iTournamentAIStep = 0;
    mGameSettingsMenu->SetAllowExit(false);

    if (iControlTeam >= 0) {
        bool fNeedAI = true;
        for (short iPlayer = 0; iPlayer < game_values.teamcounts[iControlTeam]; iPlayer++) {
            if (game_values.playercontrol[game_values.teamids[iControlTeam][iPlayer]] == 1) {
                fNeedAI = false;
                break;
            }
        }

        if (fNeedAI) {
            iTournamentAITimer = 60;
            mGameSettingsMenu->SetAllowExit(true);
        }
    }
}

void MenuState::DisplayControllingTeamMessage(short iControlTeam)
{
    //Display the team that is in control of selecting the next game
    std::string szMessage;
    if (iControlTeam < 0)
        szMessage = "All Teams Are In Control";
    else if (game_values.teamcounts[iControlTeam] <= 1)
        szMessage = std::format("Player {} Is In Control", game_values.teamids[iControlTeam][0] + 1);
    else
        szMessage = std::format("Team {} Is In Control", iControlTeam + 1);

    mCurrentMenu->AddEyeCandy<EC_Announcement>(&rm->menu_font_large, &rm->spr_announcementicons, szMessage, iControlTeam < 0 ? 4 : game_values.colorids[game_values.teamids[iControlTeam][0]], 120, 100);
}

void MenuState::Exit()
{
#ifdef __EMSCRIPTEN__
    return;
#endif
    game_values.appstate = AppState::Quit;
    game_values.WriteConfig();
}

void MenuState::ResetTournamentBackToMainMenu()
{
    mCurrentMenu = mMainMenu.get();
    mCurrentMenu->ResetMenu();

    if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame && game_values.matchtype != MatchType::NetGame) {
        if (fNeedMenuMusicReset) {
            rm->backgroundmusic[3].stop();
            rm->backgroundmusic[2].play(false, false);
            fNeedMenuMusicReset = false;
        }
    }
}

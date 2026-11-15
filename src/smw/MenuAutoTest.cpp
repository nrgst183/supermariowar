#include "GSMenu.h"

#include "rollback/NetGame.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "Game.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "menu/MatchHistoryMenu.h"
#ifdef SMW_HAS_EDITORS
#include "editors/EditorInput.h"
#endif
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
#include "menu/MatchHistoryMenu.h"
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


#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"

#include <SDL3_image/SDL_image.h>

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

    if (at.kind == Kind::Shots)
        return ShotsMenuUpdate();

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

    //Online (--net-*): the host picks the matches; the joiners are ready for them
    rollback::NetGame& net = rollback::NetGame::instance();
    static bool sawSeries = false;
    if (net.active() || net.finished()) {
        //(a series that ended runs its last world map ticks first)
        if (net.finished() && !net.seriesActive())
            return net.canQuit() ? MENU_CODE_EXIT_APPLICATION : MENU_CODE_NONE;
        if (net.seriesActive()) {
            //A tournament, tour or world: its screens (the host ends it after --at-matches matches)
            sawSeries = true;
            if (net.isHost() && g_autoTestMatchesDone >= at.matches) {
                net.endSeries("autotest: " + std::to_string(at.matches) + " matches played");
                OnlineSeriesEnded("");
                return MENU_CODE_NONE;
            }
            return AutoTestSeriesUpdate();
        }
        if (!net.isHost()) {
            if (net.lobby()->phase() == rollback::NetLobby::Phase::Lobby && !net.lobby()->peer(net.localPeer()).ready)
                net.lobby()->setReady(true);
            return MENU_CODE_NONE;
        }
        if (g_autoTestMatchesDone >= at.matches || !net.lastMatchError().empty() || sawSeries) {
            //Done, or a peer is gone (disconnect tests): close the lobby
            if (net.matchDrained())
                net.endSession(net.lastMatchError().empty() ? "autotest done" : "autotest: a match was aborted");
            return MENU_CODE_NONE;
        }
        if (phase == 0 && !net.readyForMatch())
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
            //CPU players (2), or "humans" (1) driven by GameplayState's random input
            for (int k = 0; k < 4; k++)
                game_values.playercontrol[k] = k < players ? (k < at.humans ? 1 : 2) : 0;
            if (net.active()) {
                //Online the lobby has the players: the peers', then CPU players
                int humans = 0;
                for (int k = 0; k < 4; k++)
                    humans += net.lobby()->slot(k).kind == rollback::SlotKind::Human;
                net.lobby()->setCpuCount(players - humans);
            }

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
                    //--at-map: a tour by its file name
                    for (size_t i = 0; !at.map.empty() && i < tourlist->count(); i++) {
                        if (tourlist->at(i).stem().string() == at.map)
                            game_values.tourindex = static_cast<short>(i);
                    }
                    break;
                case Kind::World:
                    game_values.matchtype = MatchType::World;
                    game_values.worldindex = static_cast<short>(idx % worldlist->count());
                    for (size_t i = 0; !at.map.empty() && i < worldlist->count(); i++) {
                        if (worldlist->at(i).stem().string() == at.map)
                            game_values.worldindex = static_cast<short>(i);
                    }
                    break;
                case Kind::Tournament:
                    game_values.matchtype = MatchType::Tournament;
                    game_values.tournamentgames = 2;
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

            //Tournament: its screens, deterministically
            if (at.kind == Kind::Tournament)
                return AutoTestSeriesUpdate();

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


//---------------------------------------------------------------
// --autotest --at-match shots: screenshots of the online menus
//---------------------------------------------------------------
// A scripted walk through the main menu, the Online menu and the lobby, saving PNGs of the screen
// (see scripts/menu-screenshots.sh, which runs a host and a joiner at once). Every step waits for
// what it needs (the other peer, the match...) and times out, so a broken run still ends.

namespace {
std::string g_pendingShot;
}

void MenuState::AutoTestAfterDraw()
{
    if (g_pendingShot.empty())
        return;

    const std::string path = cmd::g_autotest.shotsDir + "/" + g_pendingShot + ".png";
    g_pendingShot.clear();
    if (IMG_SavePNG(screen, path.c_str()))
        printf("shots: saved %s\n", path.c_str());
    else
        printf("shots: couldn't save %s: %s\n", path.c_str(), SDL_GetError());
}

MenuCodeEnum MenuState::ShotsMenuUpdate()
{
    const cmd::AutoTestArgs& at = cmd::g_autotest;
    rollback::NetGame& net = rollback::NetGame::instance();
    using Phase = rollback::NetLobby::Phase;

    static int menuFrames = 0;
    static int step = 0;
    static int t = 0;
    static bool shotTaken = false;
    ++menuFrames;
    ++t;

    auto next = [&]() {
        ++step;
        t = 0;
        shotTaken = false;
    };
    auto shot = [&](const char* name) {
        printf("shots: %s\n", name);
        g_pendingShot = name;
    };
    auto exitSoon = [&]() {
        //Leave the lobby tidily, then quit
        if (net.active())
            net.leave();
        return MENU_CODE_EXIT_APPLICATION;
    };
    //A step that gives up after a while: takes a screenshot of what is on the screen and quits
    auto timedOut = [&](int limit, const char* what) {
        if (t < limit)
            return false;
        printf("shots: timed out waiting for %s (step %d)\n", what, step);
        shot("zz_timeout");
        step = 1000;
        t = 0;
        return true;
    };

    if (step == 1000)
        return t > 3 ? exitSoon() : MENU_CODE_NONE;
    if (menuFrames >= at.frames)
        return exitSoon();

    const std::string nick = at.shotsName.empty() ? (at.shotsRole == "join" ? "Luigi" : "Mario") : at.shotsName;
    rollback::NetLobby* lobby = net.lobby();

    //The main menu, then the Online menu: the same start for every role
    if (step == 0) {
        if (t == 25)
            shot("01_main_menu");
        if (t == 28)
            next();
        return MENU_CODE_NONE;
    }
#ifdef SMW_HAS_EDITORS
    //--at-role editors: the editors, played with a scripted controller (EditorInput.h), then a match
    if (at.shotsRole == "editors") {
        using editors::input::TestInput;
        //A button held for two frames from `from`
        auto tap = [](int f, int from) { return f >= from && f < from + 2; };
        auto levelScript = [tap](editors::Editor, int f) {
            TestInput t;
            if (f == 10) t.shot = "e1_level_start";
            if (f >= 20 && f < 45) { t.lx = 0.8f; t.ly = 0.5f; }
            t.a = (f >= 50 && f < 75);              //paint a stroke
            if (f >= 55 && f < 75) t.lx = 1.0f;
            if (f == 80) t.shot = "e2_level_painted";
            t.start = tap(f, 90);
            if (f == 100) t.shot = "e3_level_actions";
            t.right = tap(f, 110);
            for (int k = 0; k < 7; k++)
                t.down = t.down || tap(f, 115 + 4 * k);
            if (f == 150) t.shot = "e4_level_save_as_selected";
            t.a = t.a || tap(f, 155);
            if (f == 180) t.shot = "e5_level_keyboard";
            t.down = t.down || tap(f, 185);
            t.right = t.right || tap(f, 190) || tap(f, 194);
            t.a = t.a || tap(f, 200) || tap(f, 204);
            if (f == 215) t.shot = "e6_level_typed";
            t.back = tap(f, 220);
            if (f == 240) t.shot = "e7_level_back_in_editor";
            t.back = t.back || tap(f, 250);
            if (f == 265) t.shot = "e8_level_exit_prompt";
            if (f >= 270) { t.px = 282; t.py = 266; }
            t.a = t.a || tap(f, 275);
            //(still here: try again)
            if (f > 300 && f % 40 == 0) t.back = true;
            if (f > 300 && f % 40 == 20) t.a = true;
            return t;
        };
        auto worldScript = [tap](editors::Editor, int f) {
            TestInput t;
            if (f == 10) t.shot = "w1_world_start";
            if (f >= 20 && f < 50) t.ry = 1.0f;     //scrolls the world (arrow keys)
            if (f >= 20 && f < 35) t.rx = 1.0f;
            if (f == 55) t.shot = "w2_world_scrolled";
            t.start = tap(f, 60);
            if (f == 70) t.shot = "w3_world_actions";
            //"Edit stages" (13 down), then a click on stage 1
            for (int k = 0; k < 13; k++)
                t.down = t.down || tap(f, 75 + 4 * k);
            t.a = tap(f, 130);
            if (f >= 140 && f < 150) { t.px = 16; t.py = 16; }
            t.b = tap(f, 150);    //(right click: edit the stage)
            if (f == 170) t.shot = "w5_world_stage_menu";
            t.back = tap(f, 180) || tap(f, 195) || tap(f, 210);
            if (f == 225) t.shot = "w4_world_exit_prompt";
            if (f >= 228) { t.px = 282; t.py = 266; }
            t.a = t.a || tap(f, 232);
            if (f > 260 && f % 40 == 0) t.back = true;
            if (f > 260 && f % 40 == 20) t.a = true;
            return t;
        };
        switch (step) {
            case 1:
                if (t == 1)
                    return MENU_CODE_TO_EDITORS_MENU;
                if (t == 15)
                    shot("e0_editors_menu");
                if (t == 18)
                    next();
                return MENU_CODE_NONE;
            case 2:
                if (t == 1) {
                    editors::input::setTestDriver(levelScript, at.shotsDir);
                    return MENU_CODE_RUN_LEVEL_EDITOR;
                }
                if (t == 6)
                    shot("e9_menu_after_level_editor");
                if (t == 8)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    editors::input::setTestDriver(worldScript, at.shotsDir);
                    return MENU_CODE_RUN_WORLD_EDITOR;
                }
                if (t == 6) {
                    editors::input::setTestDriver(nullptr, {});
                    shot("w9_menu_after_world_editor");
                }
                if (t == 8) {
                    //Both again: a second session starts over
                    static int rounds = 0;
                    if (++rounds < 2) {
                        step = 2;
                        t = 0;
                        return MENU_CODE_NONE;
                    }
                    next();
                }
                return MENU_CODE_NONE;
            case 4:
                //The game still plays (its graphics and settings came back)
                if (t == 1)
                    return MENU_CODE_EDITORS_EXIT;
                if (t == 5)
                    shot("g0_main_menu_after_editors");
                if (t == 8) {
                    //Two CPU players, a single game on the current map
                    for (int k = 0; k < 4; k++)
                        game_values.playercontrol[k] = k < 2 ? 2 : 0;
                    game_values.matchtype = MatchType::SingleGame;
                    mTeamSelectMenu->ResetTeamSelect();
                    game_values.tournamentcontrolteam = -1;
                    SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
                    return MENU_CODE_TO_GAME_SETUP_MENU;
                }
                if (t == 10) {
                    mGameSettingsMenu->GameModeChanged(0);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = 0;
                    mGameSettingsMenu->miMapField->LoadCurrentMap();
                    next();
                    return MENU_CODE_START_GAME;
                }
                return MENU_CODE_NONE;
            case 5:
                //(back from the match: it ran in the gameplay state)
                if (g_autoTestMatchesDone >= 1 && t > 3) {
                    shot("g9_menu_after_match");
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 60, "the match after the editors");
                return MENU_CODE_NONE;
        }
    }
#endif

    //--at-role history: the match history screen and a replay of its newest match
    if (at.shotsRole == "history" || at.shotsRole == "theater") {
        switch (step) {
            case 1:
                if (t == 1)
                    return MENU_CODE_TO_HISTORY_MENU;
                if (t == 20)
                    shot("h1_history");
                if (t == 24)
                    next();
                return MENU_CODE_NONE;
            case 2:
                if (t == 1) {
                    next();
                    return MENU_CODE_HISTORY_PLAY_REPLAY;
                }
                return MENU_CODE_NONE;
            case 3:
                //(the replay runs in the gameplay state; back here afterwards)
                if (mCurrentMenu == mMatchHistoryMenu.get() && t > 5) {
                    shot("h3_history_after_replay");
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 120, "the end of the replay");
                return MENU_CODE_NONE;
        }
    }

    if (step == 1) {
        if (t == 1) {
            game_values.onlinename = nick;
            if (at.shotsRole == "join")
                game_values.onlineaddress = at.shotsAddr;
            if (at.shotsRole == "host" && !at.shotsAddr.empty())
                game_values.onlineport = at.shotsAddr;
            return MENU_CODE_TO_ONLINE_MENU;
        }
        if (t == 15)
            shot(at.shotsRole == "join" ? "13_online_menu_join_filled" : "02_online_menu");
        if (t == 18)
            next();
        return MENU_CODE_NONE;
    }

    if (at.shotsRole == "fuzz") {
        //Random keys in the lobby of a host (with a second process joining it, if there is one): must not crash
        static std::mt19937 rng(777);
        if (step == 2) {
            if (t == 1)
                return MENU_CODE_ONLINE_HOST;
            if (lobby && lobby->phase() == Phase::Lobby && t > 20)
                next();
            timedOut(60 * 20, "the lobby");
            return MENU_CODE_NONE;
        }
        if (step == 3 && lobby && lobby->phase() != Phase::Closed && mCurrentMenu != mOnlineMenu.get()) {
            COutputControl& keys = game_values.playerInput.outputControls[rng() % 2];
            if (rng() % 3 == 0) {
                switch (rng() % 8) {
                    case 0: keys.menu_up.fPressed = true; break;
                    case 1: keys.menu_down.fPressed = true; break;
                    case 2: keys.menu_left.fPressed = true; break;
                    case 3: keys.menu_right.fPressed = true; break;
                    case 4:
                    case 5: keys.menu_select.fPressed = true; break;
                    case 6: keys.menu_random.fPressed = true; break;
                    default: if (rng() % 4 == 0) keys.menu_cancel.fPressed = true; break;
                }
            }
            if (t % 600 == 0 && lobby)
                lobby->setCpuCount(static_cast<int>(rng() % 4));
            return MENU_CODE_NONE;
        }
        if (step == 3 && t > 30)
            return exitSoon();
        return MENU_CODE_NONE;
    }

    if (at.shotsRole == "orig") {
        //The original offline screens the lobby borrows from: match selection, team select, game settings
        switch (step) {
            case 2:
                if (t == 1)
                    return MENU_CODE_TO_MATCH_SELECTION_MENU;
                if (t == 20)
                    shot("o1_match_selection");
                if (t == 23)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    for (int k = 0; k < 4; k++)
                        game_values.playercontrol[k] = k < 2 ? 1 : 2;
                    return MENU_CODE_MATCH_SELECTION_START;
                }
                if (t == 30)
                    shot("o2_team_select");
                if (t == 33)
                    next();
                return MENU_CODE_NONE;
            case 4:
                if (t == 1) {
                    game_values.matchtype = MatchType::SingleGame;
                    const short mode = static_cast<short>(at.mode >= 0 ? at.mode : 0);
                    mGameSettingsMenu->GameModeChanged(mode);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = mode;
                    return MENU_CODE_TO_GAME_SETUP_MENU;
                }
                if (t == 30)
                    shot("o3_game_settings");
                if (t == 33)
                    next();
                return MENU_CODE_NONE;
            default:
                return exitSoon();
        }
    }

    if (at.shotsRole == "errors") {
        //The messages of the Online menu: nothing entered, a bad address, a port in use (--at-addr), nobody there
        switch (step) {
            case 2:
                if (t == 1) {
                    game_values.onlineaddress.clear();
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 8)
                    shot("20_error_no_address");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    game_values.onlineaddress = "127.0.0.1:99999";
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 8)
                    shot("21_error_bad_address");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 4:
                if (t == 1) {
                    game_values.onlineport = at.shotsAddr;
                    return MENU_CODE_ONLINE_HOST;
                }
                if (t == 8)
                    shot("22_error_port_in_use");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 5:
                if (t == 1) {
                    game_values.onlineaddress = "127.0.0.1:1";
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 6)
                    shot("23_joining");
                if (mCurrentMenu == mOnlineMenu.get() && t > 10) {
                    next();
                    return MENU_CODE_NONE;
                }
                timedOut(60 * 40, "the connection to fail");
                return MENU_CODE_NONE;
            case 6:
                if (t == 8)
                    shot("24_error_cannot_connect");
                if (t == 12)
                    return exitSoon();
                return MENU_CODE_NONE;
            default:
                return exitSoon();
        }
    }

    //(the keys pressed here are read by the menu right after this)
    COutputControl& keys0 = game_values.playerInput.outputControls[0];

    if (at.shotsRole == "host") {
        switch (step) {
            case 2:
                if (t == 1)
                    return MENU_CODE_ONLINE_HOST;
                if (mCurrentMenu == mOnlineMenu.get() && t > 20) {
                    shot("zz_host_failed");
                    step = 1000;
                    t = 0;
                    return MENU_CODE_NONE;
                }
                if (lobby && lobby->phase() == Phase::Lobby && t > 20) {
                    shot("03_team_host_alone");
                    next();
                }
                return MENU_CODE_NONE;

            case 3:    //a joiner and a CPU player
                if (lobby && lobby->peer(1).present && lobby->pingsMeasured() && lobby->peer(1).localPlayers == 2 && t > 30) {
                    lobby->addCpu();
                    next();
                }
                timedOut(60 * 90, "a joiner");
                return MENU_CODE_NONE;

            case 4:    //the joiner is not ready yet
                if (t == 40)
                    shot("04_team_host_joiner_and_cpu");
                if (t == 44) {
                    //The host is ready too (the first player's select key): the buttons come into focus
                    keys0.menu_select.fPressed = true;
                }
                if (t > 50 && lobby && lobby->peer(1).ready)
                    next();
                timedOut(60 * 60, "the joiner to be ready");
                return MENU_CODE_NONE;

            case 5:
                if (t == 10)
                    shot("05_team_host_everyone_ready");
                if (t == 12) {
                    //The options page
                    return MENU_CODE_ONLINE_OPTIONS;
                }
                if (t == 24)
                    shot("05b_options_host");
                if (t == 26)
                    return MENU_CODE_ONLINE_OPTIONS_BACK;
                if (t == 30)
                    next();
                return MENU_CODE_NONE;

            case 6:    //the match menus
                if (t == 1)
                    return MENU_CODE_ONLINE_MATCH_SETTINGS;
                if (t == 20)
                    shot("06_match_type_menu");
                if (t == 23)
                    next();
                return MENU_CODE_NONE;

            case 7:
                if (t == 1)
                    return MENU_CODE_MATCH_SELECTION_START;
                if (t == 2 && lobby)
                    lobby->setInputDelay(3);    //(the joiner takes this for its cue)
                if (t == 60)
                    shot("07_host_game_settings");
                if (t == 63)
                    next();
                return MENU_CODE_NONE;

            case 8:    //the joiner looks at the settings, then may change them
                if (t == 300 && lobby)
                    lobby->setSettingsEveryone(true);
                if (t == 440 && lobby)
                    printf("shots: the host's goal after the joiner's change: %d (%s)\n", lobby->goal(), lobby->goalName().c_str());
                if (t == 480) {
                    //pick mode, goal and map
                    const short mode = static_cast<short>((at.mode >= 0 ? at.mode : 0) % GAMEMODE_LAST);
                    mGameSettingsMenu->GameModeChanged(mode);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = mode;
                    SModeOption* options = game_values.gamemode->GetOptions();
                    game_values.gamemode->goal = options[1].iValue;
                    mGameSettingsMenu->miGoalField[mode]->setCurrentValue(game_values.gamemode->goal);
                    if (!at.map.empty()) {
                        maplist->findexact(at.map.c_str(), false);
                        mGameSettingsMenu->miMapField->LoadCurrentMap();
                        szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
                    }
                }
                if (t == 560)
                    shot("08_host_game_settings_map_picked");
                if (t == 700)
                    next();
                return MENU_CODE_NONE;

            case 9: {
                std::string why;
                if (lobby && lobby->canStart(why)) {
                    next();
                    return MENU_CODE_START_GAME;
                }
                timedOut(60 * 20, "canStart");
                return MENU_CODE_NONE;
            }

            case 10:    //the match starts (the map is sent), is played, and ends
                if (lobby && lobby->mapTransfer().active && !shotTaken) {
                    shotTaken = true;
                    shot("09_host_map_upload");
                }
                if (g_autoTestMatchesDone >= 1 && lobby && lobby->phase() == Phase::Lobby && mCurrentMenu == mOnlineLobbyMenu.get() && t > 40) {
                    shot("10_team_host_after_match");
                    next();
                }
                timedOut(60 * 60, "the match to end");
                return MENU_CODE_NONE;

            case 11:
                if (t == 4)
                    return MENU_CODE_ONLINE_LEAVE;
                if (t == 12)
                    shot("11_leave_dialog_host");
                if (t == 16)
                    return MENU_CODE_ONLINE_LEAVE_YES;
                if (t == 40)
                    return exitSoon();
                return MENU_CODE_NONE;

            default:
                return exitSoon();
        }
    }

    //The joiner
    switch (step) {
        case 2:
            if (t == 1)
                return MENU_CODE_ONLINE_JOIN;
            if (t == 4 && lobby && lobby->phase() == Phase::Joining)
                shot("14_joining");
            if (lobby && lobby->phase() == Phase::Lobby && t > 10) {
                lobby->setLocalPlayers(2);
                next();
            }
            if (mCurrentMenu == mOnlineMenu.get() && t > 20) {
                shot("zz_join_failed");
                step = 1000;
                t = 0;
            }
            timedOut(60 * 40, "the lobby");
            return MENU_CODE_NONE;

        case 3: {
            bool cpu = false;
            for (int s = 0; lobby && s < rollback::LOBBY_SLOTS; s++)
                cpu |= lobby->slot(s).kind == rollback::SlotKind::Cpu;
            if (cpu && t > 40) {
                shot("15_team_joiner_others_locked");
                next();
            }
            timedOut(60 * 90, "the host's CPU player");
            return MENU_CODE_NONE;
        }

        case 4:    //the joiner picks another character (up/down), then is ready (select)
            if (t >= 4 && t <= 40 && (t % 4) == 0)
                keys0.menu_down.fPressed = true;    //(nine skins further)
            if (t == 4 + 36 + 4)
                keys0.menu_right.fPressed = true;   //(and another team)
            if (t == 70)
                shot("15b_team_joiner_changes_skin");
            if (t == 80)
                keys0.menu_select.fPressed = true;
            if (t == 120) {
                shot("16_team_joiner_ready");
                next();
            }
            return MENU_CODE_NONE;

        case 5:    //the host opens the settings (and sets the input delay as a sign): a look at them
            if (lobby && !lobby->inputDelayIsAuto() && lobby->inputDelay() == 3) {
                next();
                return MENU_CODE_ONLINE_VIEW_SETTINGS;
            }
            timedOut(60 * 90, "the host's settings");
            return MENU_CODE_NONE;

        case 6:
            if (t == 80)
                shot("17_joiner_settings_host_only");
            if (lobby && lobby->settingsEveryone() && t > 90) {
                next();
            }
            timedOut(60 * 60, "the host to let everyone change the settings");
            return MENU_CODE_NONE;

        case 7:    //everyone may change them: the joiner changes the goal, which the host takes over
            if (t == 20)
                shot("18_joiner_settings_everyone");
            //(the focus is on Back: two downs reach the goal field, select and right raise it)
            if (t == 30 || t == 34)
                keys0.menu_down.fPressed = true;
            if (t == 38 || t == 46)
                keys0.menu_select.fPressed = true;
            if (t == 42)
                keys0.menu_right.fPressed = true;
            if (t == 70)
                shot("18b_joiner_settings_edited_goal");
            if (!at.map.empty() && lobby && lobby->mapName() == at.map && t > 30) {
                next();
            }
            timedOut(60 * 60, "the host's map");
            return MENU_CODE_NONE;

        case 8: {    //the map is not here: the start downloads it
            if (t == 40)
                shot("19_joiner_settings_map_missing");
            const rollback::MapTransferInfo tr = lobby ? lobby->mapTransfer() : rollback::MapTransferInfo {};
            if (tr.active && !shotTaken && tr.bytesDone > 0) {
                shotTaken = true;
                shot("20_joiner_map_download");
            }
            if (g_autoTestMatchesDone >= 1 && lobby && lobby->phase() == Phase::Lobby && mCurrentMenu == mOnlineLobbyMenu.get() && t > 40) {
                shot("21_team_joiner_after_match");
                next();
            }
            timedOut(60 * 60, "the match to end");
            return MENU_CODE_NONE;
        }

        case 9:    //the host leaves: the lobby is closed
            if (mCurrentMenu == mOnlineMenu.get() && t > 12) {
                shot("22_online_menu_lobby_closed");
                next();
            }
            timedOut(60 * 60, "the host to close the lobby");
            return MENU_CODE_NONE;

        default:
            return t > 6 ? exitSoon() : MENU_CODE_NONE;
    }
}

#include "GSGameplay.h"

#include "CmdArgs.h"
#include "eyecandy.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "joystick.h"
#include "GSMenu.h"
#include "net.h"
#include "map.h"
#include "MapList.h"
#include "ObjectContainer.h"
#include "objecthazard.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "Score.h"
#include "sfx.h"
#include "SimulationState.h"
#include "WorldTourStop.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/Tag.h"
#include "objects/MysteryMushroomTempPlayer.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Spike.h"
#include "objects/carriable/CO_ThrowBlock.h"
#include "objects/carriable/CO_ThrowBox.h"
#include "objects/blocks/BounceBlock.h"
#include "objects/blocks/BreakableBlock.h"
#include "objects/blocks/DonutBlock.h"
#include "objects/blocks/FlipBlock.h"
#include "objects/blocks/NoteBlock.h"
#include "objects/blocks/OnOffSwitchBlock.h"
#include "objects/blocks/PowerupBlock.h"
#include "objects/blocks/SwitchBlock.h"
#include "objects/blocks/ThrowBlock.h"
#include "objects/blocks/ViewBlock.h"
#include "objects/blocks/WeaponBreakableBlock.h"
#include "objects/moving/MO_BulletBill.h"
#include "objects/moving/MO_Coin.h"
#include "objects/moving/MO_PirhanaPlant.h"
#include "objects/powerup/PU_BobombPowerup.h"
#include "objects/powerup/PU_BombPowerup.h"
#include "objects/powerup/PU_BoomerangPowerup.h"
#include "objects/powerup/PU_BulletBillPowerup.h"
#include "objects/powerup/PU_ClockPowerup.h"
#include "objects/powerup/PU_ExtraGuyPowerup.h"
#include "objects/powerup/PU_FeatherPowerup.h"
#include "objects/powerup/PU_FirePowerup.h"
#include "objects/powerup/PU_HammerPowerup.h"
#include "objects/powerup/PU_IceWandPowerup.h"
#include "objects/powerup/PU_LeafPowerup.h"
#include "objects/powerup/PU_ModPowerup.h"
#include "objects/powerup/PU_MysteryMushroomPowerup.h"
#include "objects/powerup/PU_PodoboPowerup.h"
#include "objects/powerup/PU_PoisonPowerup.h"
#include "objects/powerup/PU_PowPowerup.h"
#include "objects/powerup/PU_PWingsPowerup.h"
#include "objects/powerup/PU_StarPowerup.h"
#include "objects/powerup/PU_Tanooki.h"
#include "objects/walkingenemy/WalkingEnemy.h"

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

SimulationState g_sim;

CEyecandyContainer eyecandy[3];
SpotlightManager spotlightManager;

short g_iWinningPlayer;
short scorepowerupoffsets[3][3] = { {  37,  0,    0 },
                                    {  71,  89,   0 },
                                    { 105, 123, 141 } };

short GameplayState::scoreoffsets[3] = {2, 36, 70};
short GameplayState::g_iPowerupToIcon[8] = {80, 176, 272, 304, 336, 368, 384, 400};

GameplayState::GameplayState()
{
    for (short p = 0; p < 4; p++) {
        g_sim.respawnCount[p] = 0;
        respawnanimationtimer[p] = 0;
        respawnanimationframe[p] = 0;
    }


    //Vars that keep track of spinning the screen
    spinangle = 0.0f;
    spinspeed = 0.0f;
    spindirection = 1;
    spintimer = 0;
}

GameplayState& GameplayState::instance() {
    static GameplayState gps;
    return gps;
}

//-----------------------------------------------------------------------------
// THE GAME LOOP
//-----------------------------------------------------------------------------


constexpr short iCountDownTimes[28] = {3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 45, 3, 3, 3};
constexpr short iCountDownAnnounce[28] = {-1, -1, -1, 12, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, 14, -1, -1, -1, -1, -1, -1, 15, -1, -1, -1};

#ifdef _DEBUG
short iSledgeBrotherSetting = 0;
short iSledgeBrotherAttribute = 0;

bool g_fAutoTest = false;
bool g_fRecordTest = false;
int exitgametimer = 0;
#endif

// Opt-in --autotest harness state
int g_autoTestMatchesDone = 0;
static int autoTestFrames = 0;
static int autoTestOverFrames = 0;

//Must only be called after organizeteams() is called
short LookupTeamID(short id, short * teamID, short * subTeamID)
{
    for (short i = 0; i < g_sim.score_cnt; i++) {
        for (short j = 0; j < game_values.teamcounts[i]; j++) {
            if (game_values.teamids[i][j] == id) {
                if (teamID)
                    *teamID = i;

                if (subTeamID)
                    *subTeamID = j;

                return i;
            }
        }
    }

    if (teamID)
        *teamID = -1;

    if (subTeamID)
        *subTeamID = -1;

    return -1;
}

short LookupTeamID(short id)
{
    return LookupTeamID(id, nullptr, nullptr);
}

void SDLCALL musicfinished(void*, MIX_Track*)
{
    if (!game_values.music)
        return;

    if (game_values.appstate == AppState::Game && !game_values.gamemode->gameover)
        return;

    if (sfxMusic::fResumeMusic)
        rm->backgroundmusic[3].play(false, false);
}

short GetModeIconIndexFromMode(short iMode)
{
    if (iMode == game_mode_pipe_minigame)
        iMode = 25;
    else if (iMode == game_mode_boss_minigame)
        iMode = 26;
    else if (iMode == game_mode_boxes_minigame)
        iMode = 27;

    return iMode;
}

short CountAliveTeams(short * lastteam)
{
    short findlastteam = 0;

    bool teamalive[4] = {false, false, false, false};
    for (CPlayer* player : g_sim.players) {
        if (!player->isdead())
            teamalive[player->teamID] = true;
    }

    short numteams = 0;
    for (short k = 0; k < 4; k++) {
        if (teamalive[k]) {
            findlastteam = k;
            numteams++;
        }
    }

    if (lastteam != nullptr) {
        if (numteams == 1)
            *lastteam = findlastteam;
        else
            *lastteam = -1;
    }

    return numteams;
}

void GameplayState::CleanDeadPlayers()
{
    bool fCheckForGameOver = false;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);
    auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode);

    for (size_t i = 0; i < g_sim.players.size(); i++) {
        if (g_sim.players[i]->state == PlayerState::Dead) {
            fCheckForGameOver = true;

            if (g_sim.respawnCount[g_sim.players[i]->globalID] <= 0)
                g_sim.players[i]->die(PlayerDeathStyle::Jump, true, false);

            //Set this to zero so we don't display a spawn egg when player is removed from game
            g_sim.respawnCount[g_sim.players[i]->globalID] = 0;

            if (gmTag && gmTag->tagged() == g_sim.players[i])
                gmTag->setTagged(nullptr);

            if (gmChicken && gmChicken->chicken() == g_sim.players[i])
                gmChicken->clearChicken();

            // Note: the player that moves into slot i is only checked next frame
            g_sim.players.erase(i);

            for (size_t j = i; j < g_sim.players.size(); j++)
                g_sim.players[j]->localID = j;
        }
    }

    if (fCheckForGameOver && game_values.gamemode->gamemode != game_mode_bonus && game_values.gamemode->gamemode != game_mode_boss_minigame) {
        short lastteam = -1;
        if (!game_values.gamemode->gameover && CountAliveTeams(&lastteam) <= 1) {
            game_values.gamemode->gameover = true;
            game_values.gamemode->winningteam = lastteam;
            SetupScoreBoard(true);  //pass true because the order the players died in matters
            ShowScoreBoard();
        }
    }
}

void CleanUp()
{
    //delete object list
    g_sim.players.clear();

    eyecandy[0].clean();
    eyecandy[1].clean();
    eyecandy[2].clean();
    spotlightManager.ClearSpotlights();

    g_sim.noncolcontainer.clean();

    g_sim.objectcontainer[0].clean();
    g_sim.objectcontainer[1].clean();
    g_sim.objectcontainer[2].clean();

    LoadMapObjects(true);
    g_map->clearWarpLocks();
    g_map->resetPlatforms();

    //Stop all game sounds
    sfx_stopallsounds();
    rm->sfx_invinciblemusic.stop();
    rm->sfx_slowdownmusic.stop();

    x_shake = 0;
    y_shake = 0;
}

void SetGameModeSettingsFromMenu()
{
    //If this is a tour stop and the tour has settings in it, use those.  Otherwise use the menu settings.
    if (game_values.tourstopcurrent < game_values.tourstops.size() && game_values.tourstops[game_values.tourstopcurrent]->fUseSettings &&
            (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::World))
        memcpy(&game_values.gamemodesettings, &game_values.tourstops[game_values.tourstopcurrent]->gmsSettings, sizeof(GameModeSettings));
    else
        memcpy(&game_values.gamemodesettings, &game_values.gamemodemenusettings, sizeof(GameModeSettings));
}

void GameplayState::onEnterState()
{
    g_sim.countdownState = 0;
    g_sim.countdownTimer = 0;

    if (game_values.startgamecountdown && game_values.singleplayermode == -1) {
        g_sim.countdownState = 28;
        g_sim.countdownTimer = iCountDownTimes[0];
    }

    current_playerKeys = &game_values.playerInput.outputControls[0];
    previous_playerKeys = *current_playerKeys;

    initRunGame();

#ifdef _DEBUG
    exitgametimer = 0;
#endif
    autoTestFrames = 0;
    autoTestOverFrames = 0;
}

void GameplayState::handleInput()
{
    game_values.playerInput.ClearPressedKeys(game_values.flags.exitinggame ? 1 : 0);
    // netplay.netPlayerInput.ClearGameActionKeys();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: {
            CleanUp();
            game_values.appstate = AppState::Quit;
            return;
        }
        break;

        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            JoystickManager::instance().handleEvent(event);
            break;

        case SDL_EVENT_KEY_DOWN: {
            if (event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                if (event.key.key == SDLK_F4) {
                    CleanUp();
                    game_values.appstate = AppState::Quit;
                    return;
                } else if (event.key.key == SDLK_RETURN) {
                    game_values.fullscreen = !game_values.fullscreen;
                    gfx_changefullscreen(game_values.fullscreen);
                    blitdest = screen;

                    //Continue with input -> don't feed this event to the input
                    //otherwise it will pause the game when switching to full/windowed screen
                    continue;
                }
            }
            if (event.key.key == SDLK_F1) {
                game_values.showfps = !game_values.showfps;
            } else if (event.key.key == SDLK_ESCAPE) {
                game_values.playerInput.outputControls[0].game_cancel.fPressed = true;
            } else if (event.key.key == SDLK_TAB) {
                PlayNextMusicTrack();
            } else if (event.key.key == SDLK_INSERT) {
                gfx_take_screenshot();
            }
#ifdef _DEBUG
            else if (event.key.key == SDLK_LEFTBRACKET) {
                game_values.framelimiter++;
            } else if (event.key.key == SDLK_RIGHTBRACKET) {
                if (game_values.framelimiter > 0)
                    game_values.framelimiter--;
            } else if (event.key.key == SDLK_F2) {
                game_values.frameadvance = !game_values.frameadvance;
            } else if (event.key.key == SDLK_F5) {
                game_values.autokill = !game_values.autokill;
            } else if (event.key.key == SDLK_Z) {
                for (CPlayer* player : g_sim.players) {
                    player->DeathAwards();

                    if (game_values.gamemode->playerkilledself(*player, KillStyle::Environment) == PlayerKillType::Normal)
                        player->die(PlayerDeathStyle::Jump, false, false);
                }
            } else if (event.key.key == SDLK_X) {
                short iplayer = RANDOM_INT(g_sim.players.size());
                g_sim.players[iplayer]->makefrozen(300);
            } else if (event.key.key == SDLK_C) {
                short iplayer = RANDOM_INT(g_sim.players.size());
                g_sim.players[iplayer]->shield.type = (PlayerShieldType)(RANDOM_INT(3) + 1);
                g_sim.players[iplayer]->shield.timer = 620;
            } else if (event.key.key == SDLK_1) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_IceWandPowerup(&rm->spr_icewandpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BobombPowerup(&rm->spr_bobombpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_StarPowerup(&rm->spr_starpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 4, true, 2, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_2) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_BombPowerup(&rm->spr_bombpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_PowPowerup(&rm->spr_powpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 8, true, 8, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_1uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 1));
            } else if (event.key.key == SDLK_3) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_PodoboPowerup(&rm->spr_podobopowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BulletBillPowerup(&rm->spr_bulletbillpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_2uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 2));
            } else if (event.key.key == SDLK_4) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_Tanooki({static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Green, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true, true, true, false));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_3uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 3));
            } else if (event.key.key == SDLK_5) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_PWingsPowerup(&rm->spr_pwingspowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Red, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, true, true, false));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_5uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 5));
            } else if (event.key.key == SDLK_6) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_Spring(&rm->spr_spring, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Spiny, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, false, true, true));
                else
                    g_sim.objectcontainer[0].add(new PU_FirePowerup(&rm->spr_firepowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_7) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_ThrowBox(&rm->spr_throwbox, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, (RANDOM_INT(NUM_POWERUPS) + 3) - 3));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Buzzy, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, true, false, false));
                else
                    g_sim.objectcontainer[0].add(new PU_HammerPowerup(&rm->spr_hammerpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_8) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_Spike(&rm->spr_spike, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_ModPowerup(&rm->spr_modpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 8, true, 8, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_PoisonPowerup(&rm->spr_poisonpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_9) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_KuriboShoe(&rm->spr_kuriboshoe, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_FeatherPowerup(&rm->spr_featherpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ClockPowerup(&rm->spr_clockpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_0) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_LeafPowerup(&rm->spr_leafpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BoomerangPowerup(&rm->spr_boomerangpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_MysteryMushroomPowerup(&rm->spr_mysterymushroompowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_F8) {
                g_fAutoTest = !g_fAutoTest;
            }
#endif
            break;
        }

        default:
            break;
        }

        //Feed the player control structures with input data
        //Use menu controls when exit game dialog is up
        game_values.playerInput.Update(event, (game_values.flags.exitinggame ? 1 : 0));
        // net player inputs are not updated here,
        // it is handled in update()
    }
}

bool shouldUpdate()
{
    if (netplay.active)
        return true;

    if (game_values.flags.pausegame || game_values.flags.exitinggame)
        return false;

    return true;
}

void start_gameplay()
{
    CleanUp();
    SetGameModeSettingsFromMenu();
    game_values.appstate = AppState::Game;

    //secretBoss();

    if (game_values.music) {
        musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), "", "");
        rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
        rm->backgroundmusic[0].play(game_values.playnextmusic, false);
    }
}

void end_gameplay()
{
    CleanUp();
    game_values.appstate = AppState::Menu;
    game_values.screenfadespeed = -8;
    GameStateManager::instance().changeStateTo(&MenuState::instance());
}

void GameplayState::update_countdown_timer()
{
    //Count down start timer before each game
    if (g_sim.countdownState > 0 && --g_sim.countdownTimer <= 0) {
        //There is one extra count here so we hit all the numbers.  When we
        //reach 0 that means we are done counting (index 28 would be out of
        //bounds on this array)
        if (--g_sim.countdownState != 0) {
            //28 is the magic number as there are 28 frames of animation
            //spread over a few seconds for the countdown numbers
            g_sim.countdownTimer = iCountDownTimes[28 - g_sim.countdownState];

            short countDownAnnounce = iCountDownAnnounce[28 - g_sim.countdownState];
            if (countDownAnnounce >= 0)
                rm->sfx_announcer[countDownAnnounce].play();
        }
    }
}

void update_playerswap()
{
    if (game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players) {
            player->updateswap();
        }
    }
}

void GameplayState::update_world()
{
    shakeScreen();
    spinScreen();
    updateBulletBillPowerup();

    if (game_values.matchtype == MatchType::World &&
        game_values.gamemode->gameover &&
        game_values.flags.forceexittimer <= 0) {
        if (--game_values.flags.noexittimer <= 0)
            game_values.flags.noexit = false;
    }

    //------------- update objects -----------------------

#ifdef _DEBUG
    debugAutoKillEveryone();
#endif

    //Advance the cpu's turn (AI only calculates player's actions 1 out of 4 frames)
    if (++game_values.cputurn > 3)
        game_values.cputurn = 0;

    if (!netplay.active || (netplay.active && netplay.theHostIsMe))
        handleP2PCollisions();

    //Move platforms
    g_map->updatePlatforms();

    game_values.flags.playskidsound = false;
    game_values.flags.playinvinciblesound = false;
    game_values.flags.playflyingsound = false;

    for (CPlayer* player : g_sim.players)
        player->move();    //move all objects before doing object-object collision detection in
    //->think(), so we test against the new position after object-map collision detection

    playSFX();

    g_sim.noncolcontainer.update();
    g_sim.objectcontainer[0].update();
    g_sim.objectcontainer[1].update();
    g_sim.objectcontainer[2].update();

    handleP2ObjCollisions();
    if (game_values.flags.swapplayers) {
        update_playerswap();
        return;
    }

    handleObj2ObjCollisions();

    //Commit all player actions at this point (after we have collided with any objects
    //that the player might have picked up)
    for (CPlayer* player : g_sim.players)
        player->CommitAction();

    cleanDeadNonPlayerObjects();
    CleanDeadPlayers();

    eyecandy[0].update();
    eyecandy[1].update();
    eyecandy[2].update();

    game_values.gamemode->think();

    if (game_values.flags.slowdownon != -1 && ++game_values.flags.slowdowncounter > 580) {
        game_values.flags.slowdownon = -1;
        game_values.flags.slowdowncounter = 0;
    }

    g_map->update();

    updateScreenShake();

    updateScoreboardAnimation();
}

void GameplayState::update()
{
    read_network();

    if (!netplay.active) {
        checkWindEvent(g_sim.windTimer, g_sim.nextWind);
    }

    for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
        iScoreTextOffset[iTeam] = 34 * game_values.teamcounts[iTeam] + 1;
    }

#ifdef _DEBUG
    debug_gameplay();
#endif

    // Opt-in --autotest harness: end the match after a frame limit (or shortly after it is over)
    if (cmd::g_autotest.enabled) {
        ++autoTestFrames;
        if (game_values.gamemode->gameover)
            ++autoTestOverFrames;

        if (autoTestFrames >= cmd::g_autotest.frames || autoTestOverFrames >= 120) {
            if (game_values.gamemode->gameover && (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament))
                UpdateScoreBoard();

            CleanUp();
            game_values.flags.exitinggame = false;
            game_values.flags.pausegame = false;
            game_values.appstate = AppState::Menu;
            ++g_autoTestMatchesDone;
            printf("autotest: match %d ended after %d frames\n", g_autoTestMatchesDone, autoTestFrames);
            GameStateManager::instance().changeStateTo(&MenuState::instance());
            return;
        }
    }

    handleInput();
    network_send_local_input();

    if (updateExitPauseDialog(g_sim.countdownState)) {
        if (netplay.active)
            netplay.client.sendLeaveGameMessage();
        GameStateManager::instance().changeStateTo(&MenuState::instance());
        return;
    }

    if (shouldUpdate()) {
        if (!game_values.flags.swapplayers && game_values.screenfade == 0) {
            update_countdown_timer();

            //Make updates to background stuff (animate map while countdown is counting)
            if (g_sim.countdownState > COUNTDOWN_START_INDEX) {
                animateDuringCountdown();
            } else {
                update_world();
            }
        }
        update_playerswap();
        network_broadcast_game_state();

        if (game_values.screenfade == 255) {
            if (game_values.appstate == AppState::StartGame) {
                start_gameplay();
                return;
            } else if (game_values.appstate == AppState::EndGame) {
                end_gameplay();
                return;
            }
        }

        //--------------- draw everything ----------------------
        drawEverything(g_sim.countdownState, iScoreTextOffset);
    }

    if (game_values.flags.pausegame || game_values.flags.exitinggame) {
        drawExitPauseDialog();
    }

    playMusic();


#ifdef _DEBUG
    if (g_fAutoTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Auto");
#endif

}

bool SwapPlayers(short iUsingPlayerID)
{
    //Count available players to switch with
    short iNumAvailablePlayers = 0;
    for (CPlayer* player : g_sim.players) {
        if (player->isready()) {
            iNumAvailablePlayers++;
            player->fOldSwapX = player->leftX();
            player->fOldSwapY = player->topY();

            player->iNewPowerupX = player->score->x + scorepowerupoffsets[game_values.teamcounts[player->teamID] - 1][player->subTeamID];
            player->iNewPowerupY = player->score->y + 25;
        }
    }

    if (iNumAvailablePlayers <= 1)
        return false;

    if (game_values.swapstyle != MysteryMushroomSwapStyle::Instant) {
        game_values.flags.swapplayers = true;
        game_values.flags.swapplayersposition = 0.0f;

        if (game_values.swapstyle == MysteryMushroomSwapStyle::Blink) {
            game_values.flags.swapplayersblink = false;
            game_values.flags.swapplayersblinkcount = 0;
        } else {
            game_values.screenfade = App::menuTransparency;
        }
    }

    short iIncrement = RANDOM_INT (iNumAvailablePlayers - 1);

    MysteryMushroomTempPlayer spots[4];
    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
        CPlayer* player  = g_sim.players[iPlayer];

        if (!player->isready())
            continue;

        short iNewSpot = iPlayer + iIncrement;

        do {
            if (++iNewSpot >= static_cast<short>(g_sim.players.size()))
                iNewSpot = 0;
        } while (spots[iNewSpot].fUsed || !g_sim.players[iNewSpot]->isready());

        spots[iNewSpot].fUsed = true;
        spots[iPlayer].SetPlayer(g_sim.players[iNewSpot], game_values.gamepowerups[g_sim.players[iNewSpot]->getGlobalID()]);

        //Give credit for deaths to the player that used the mystery mushroom
        if (iUsingPlayerID == iNewSpot) {
            player->iSuicideCreditPlayerID = g_sim.players[iNewSpot]->globalID;
            player->iSuicideCreditTimer = 62;
        }
    }

    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
        CPlayer* player  = g_sim.players[iPlayer];

        if (!player->isready())
            continue;

        spots[iPlayer].GetPlayer(player, &game_values.gamepowerups[player->getGlobalID()]);

        if (game_values.swapstyle != MysteryMushroomSwapStyle::Blink)
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, static_cast<short>(player->fNewSwapX) + (HALFPW) - 16, static_cast<short>(player->fNewSwapY) + (HALFPH) - 16, 3, 8);

        if (game_values.swapstyle == MysteryMushroomSwapStyle::Instant) {
            player->setXf(player->fNewSwapX);
            player->setYf(player->fNewSwapY);

            if (player->carriedItem)
                player->carriedItem->MoveToOwner();
        }
    }

    return true;
}

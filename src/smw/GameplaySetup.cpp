#include "CpuDriver.h"
#include "LoadLog.h"
#include "GSGameplay.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "map.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Tag.h"

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

//
// INIT
//
void GameplayState::createPlayers()
{
    //Create players for this game
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        g_sim.respawnCount[iPlayer] = 0;

        if (game_values.singleplayermode == -1 || game_values.singleplayermode == iPlayer) {
            if (game_values.playercontrol[iPlayer] > 0) {
                short teamid, subteamid;
                LookupTeamID(iPlayer, &teamid, &subteamid);

                CPlayerAI * ai = nullptr;
                if (game_values.playercontrol[iPlayer] == 2)
                    ai = new CPlayerAI();

                g_sim.players.add(std::make_unique<CPlayer>(iPlayer, g_sim.players.size(), teamid, subteamid, game_values.colorids[iPlayer], rm->spr_player[iPlayer], &g_sim.score[teamid], &(g_sim.respawnCount[iPlayer]), ai));
            } else if (!game_values.keeppowerup) {
                //Reset off player's stored powerups if they are not playing
                game_values.storedpowerups[iPlayer] = -1;
            }
        }

        //If the gamemode allows stored powerups, then assign the game stored slot to the powerup this player has
        if (game_values.gamemode->HasStoredPowerups())
            game_values.gamepowerups[iPlayer] = game_values.storedpowerups[iPlayer];
        else {
            game_values.gamepowerups[iPlayer] = -1;
        }

        game_values.bulletbilltimer[iPlayer] = 0;
        game_values.bulletbillspawntimer[iPlayer] = 0;
    }
}

void GameplayState::initScoreDisplayPosition()
{
    short i;
    short totalspace = 0;
    for (i = 0; i < g_sim.score_cnt; i++) {
        totalspace += 56 + game_values.teamcounts[i] * 34;
    }
    totalspace += 20 * (g_sim.score_cnt - 1);

    for (i = 0; i < g_sim.score_cnt; i++) {
        if (game_values.scoreboardstyle == ScoreboardStyle::Top || game_values.scoreboardstyle == ScoreboardStyle::Bottom) {
            g_sim.score[i].x = ((App::screenWidth - totalspace) >> 1);

            for (short k = 0; k < i; k++)
                g_sim.score[i].x += 76 + game_values.teamcounts[k] * 34;

            short iScoreOffsetY = 0;
            if (game_values.gamemode->gamemode == game_mode_health || game_values.gamemode->gamemode == game_mode_collection)
                iScoreOffsetY = 18;

            g_sim.score[i].y = 5 + (game_values.scoreboardstyle == ScoreboardStyle::Bottom ? 429 - iScoreOffsetY : 0);
        } else {
            short iScoreOffsetY = 0;
            if (game_values.gamemode->gamemode == game_mode_health || game_values.gamemode->gamemode == game_mode_collection)
                iScoreOffsetY = 18;

            g_sim.score[i].x = 5 + (574 - (34 * game_values.teamcounts[i])) * (i % 2);
            g_sim.score[i].y = 5 + (429 - iScoreOffsetY) * (i > 1 ? 1 : 0);
        }

        g_sim.score[i].fromx = g_sim.score[i].x;
        g_sim.score[i].fromy = g_sim.score[i].y;
        g_sim.score[i].place = i;
        g_sim.score[i].order = -1;
    }
}

void GameplayState::initEyeCandy()
{
    short i;
    for (short iEyeCandyLayer = 0; iEyeCandyLayer < 3; iEyeCandyLayer++) {
        //Clouds
        if (g_map->eyecandy[iEyeCandyLayer] & 1) {
            for (i = 0; i < 4; i++) {
                float velx;         //speed of cloud, small clouds are slower than big ones
                short srcy, w, h;

                if (COSMETIC_RANDOM_BOOL()) {
                    velx = static_cast<short>(COSMETIC_RANDOM_INT(51) - 25) / 10.0f;    //big clouds: -3 - +3 pixel/frame
                    srcy = 0;
                    w = 60;
                    h = 28;
                } else {
                    velx = static_cast<short>(COSMETIC_RANDOM_INT(41) - 20) / 10.0f;    //small clouds: -2 - +2 pixel/frame
                    srcy = 28;
                    w = 28;
                    h = 12;
                }

                velx = velx < 0.5f && velx > -0.5f ? 1 : velx;  //no static clouds please

                //add cloud to eyecandy array
                eyecandy[iEyeCandyLayer].emplace<EC_Cloud>(&rm->spr_clouds, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), static_cast<float>(COSMETIC_RANDOM_INT(100)), velx, 0, srcy, w, h);
            }
        }

        //Ghosts
        if (g_map->eyecandy[iEyeCandyLayer] & 2) {
            for (i = 0; i < 8; i++) {
                short iGhostSrcY = static_cast<short>(COSMETIC_RANDOM_INT(3)) << 5; //ghost type
                float velx = static_cast<short>(COSMETIC_RANDOM_INT(51) - 25) / 10.0f;  //big clouds: -3 - +3 pixel/frame

                velx = velx < 0.5f && velx > -0.5f ? (COSMETIC_RANDOM_INT(1) ? 1.0f : -1.0f) : velx; //no static clouds please

                //add cloud to eyecandy array
                eyecandy[iEyeCandyLayer].emplace<EC_Ghost>(&rm->spr_ghosts, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), static_cast<float>(COSMETIC_RANDOM_INT(100)), velx, 8, 2, velx < 0.0f ? 64 : 0, iGhostSrcY, 32, 32);
            }
        }

        //Leaves
        if (g_map->eyecandy[iEyeCandyLayer] & 4) {
            for (i = 0; i < 15; i++)
                eyecandy[iEyeCandyLayer].emplace<EC_Leaf>(&rm->spr_leaves, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), static_cast<float>(COSMETIC_RANDOM_INT(App::screenHeight)));
        }

        //Snow
        if (g_map->eyecandy[iEyeCandyLayer] & 8) {
            for (i = 0; i < 15; i++)
                eyecandy[iEyeCandyLayer].emplace<EC_Snow>(&rm->spr_snow, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), static_cast<float>(COSMETIC_RANDOM_INT(App::screenHeight)), 0);
        }

        //Fish
        short iFishWeights[] = {20, 20, 15, 10, 10, 5, 10, 10};
        short iFishSettings[][4] = { {0, 0, 64, 44}, {0, 44, 64, 44}, {0, 44, 48, 44}, {32, 32, 16, 12}, {32, 44, 16, 12}, {32, 16, 16, 28}, {32, 0, 32, 28}, {32, 44, 32, 28}};
        if (g_map->eyecandy[iEyeCandyLayer] & 16) {
            for (i = 0; i < 8; i++) {
                float velx = static_cast<short>(COSMETIC_RANDOM_INT(41) - 20) / 10.0f;
                velx = velx < 0.5f && velx > -0.5f ? 1.0f : velx; //Keep fish from moving too slowly

                short srcx = iFishSettings[0][0], srcy = iFishSettings[0][1], w = iFishSettings[0][2], h = iFishSettings[0][3];

                short iRandomFish = COSMETIC_RANDOM_INT(100);

                short iFishWeightCount = 0;
                for (short iFish = 0; iFish < 8; iFish++) {
                    iFishWeightCount += iFishWeights[iFish];

                    if (iRandomFish < iFishWeightCount) {
                        srcx = iFishSettings[iFish][0];
                        srcy = iFishSettings[iFish][1];
                        w = iFishSettings[iFish][2];
                        h = iFishSettings[iFish][3];
                        break;
                    }
                }

                //add cloud to eyecandy array
                short iPossibleY = (App::screenHeight - h) / 10;
                float dDestY = static_cast<float>(COSMETIC_RANDOM_INT(iPossibleY) + iPossibleY * i);
                eyecandy[iEyeCandyLayer].emplace<EC_Cloud>(&rm->spr_fish, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), dDestY, velx, srcx + (velx > 0.0f ? 64 : 0), srcy, w, h);
            }
        }

        //Rain
        if (g_map->eyecandy[iEyeCandyLayer] & 32) {
            for (i = 0; i < 20; i++)
                eyecandy[iEyeCandyLayer].emplace<EC_Rain>(&rm->spr_rain, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), COSMETIC_RANDOM_INT(App::screenHeight));
        }

        //Bubbles
        if (g_map->eyecandy[iEyeCandyLayer] & 64) {
            for (i = 0; i < 10; i++)
                eyecandy[iEyeCandyLayer].emplace<EC_Bubble>(&rm->spr_rain, static_cast<float>(COSMETIC_RANDOM_INT(App::screenWidth)), COSMETIC_RANDOM_INT(App::screenHeight));
        }
    }
}

void GameplayState::initRunGame()
{
    loadlog::Scope timed("match setup (initRunGame)");
    y_shake = 0;
    x_shake = 0;

    //Reset the screen spin variables
    spinangle = 0.0f;
    spinspeed = 0.0f;
    spindirection = 1;
    spintimer = 0;

    game_values.resetSecretCounters();

    //Reset the keys each time we switch from menu to game and back
    game_values.playerInput.ResetKeys();
    resetTickInput();
    cpudriver::reset();
    g_sim.tick = 0;
    g_sim.gameoverTick = -1;

    createPlayers();
    game_values.resetGameplaySettings();
    initScoreDisplayPosition();
    initEyeCandy();

    g_sim.windTimer = 0;
    g_sim.nextWind = static_cast<float>(RANDOM_INT(41) - 20) / 4.0f;
    game_values.flags.gamewindx = static_cast<float>((RANDOM_INT(41)) - 20) / 4.0f;

    //Initialize players after game init has finished
    for (CPlayer* player : g_sim.players)
        player->Init();
}

//
// RUNNING
//

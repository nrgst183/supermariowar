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

constexpr SDL_Rect iCountDownNumbers[4][4][2] =
    {   {{{0, 0, 64, 64},{288, 208, 64, 64}},
        {{0, 64, 48, 48},{296, 216, 48, 48}},
        {{192, 64, 32, 32},{304, 224, 32, 32}},
        {{0, 112, 16, 16},{312, 232, 16, 16}}
    },

    {   {{64, 0, 64, 64},{288, 208, 64, 64}},
        {{48, 64, 48, 48},{296, 216, 48, 48}},
        {{224, 64, 32, 32},{304, 224, 32, 32}},
        {{16, 112, 16, 16},{312, 232, 16, 16}}
    },

    {   {{128, 0, 64, 64},{288, 208, 64, 64}},
        {{96, 64, 48, 48},{296, 216, 48, 48}},
        {{192, 96, 32, 32},{304, 224, 32, 32}},
        {{32, 112, 16, 16},{312, 232, 16, 16}}
    },

    {   {{192, 0, 64, 64},{288, 208, 64, 64}},
        {{144, 64, 48, 48},{296, 216, 48, 48}},
        {{224, 96, 32, 32},{304, 224, 32, 32}},
        {{48, 112, 16, 16},{312, 232, 16, 16}}
    }
};

constexpr short iCountDownRectSize[28] = {3, 2, 1, 0, 1, 2, 3, 3, 2, 1, 0, 1, 2, 3, 3, 2, 1, 0, 1, 2, 3, 3, 2, 1, 0, 1, 2, 3};
constexpr short iCountDownRectGroup[28] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3};
void GameplayState::drawScreenFade()
{
    if (game_values.screenfade > 0) {
        rm->drawShade(static_cast<Uint8>(game_values.screenfade));
    }
}

void GameplayState::drawScreenShakeBackground()
{
    //Draw black "behind" the game if we are shaking/moving the screen
    if (y_shake > 0) {
        SDL_Rect rect = {0, 0, App::screenWidth, y_shake};
        SDL_FillSurfaceRect(screen, &rect, 0x0);       //fill empty area with black
    } else if (y_shake < 0) {
        SDL_Rect rect = {0, App::screenHeight + y_shake, App::screenWidth, App::screenHeight};
        SDL_FillSurfaceRect(screen, &rect, 0x0);       //fill empty area with black
    }

    if (x_shake > 0) {
        SDL_Rect rect = {0, 0, x_shake, App::screenHeight};
        SDL_FillSurfaceRect(screen, &rect, 0x0);       //fill empty area with black
    } else if (x_shake < 0) {
        SDL_Rect rect = {App::screenWidth + x_shake, 0, App::screenWidth, App::screenHeight};
        SDL_FillSurfaceRect(screen, &rect, 0x0);       //fill empty area with black
    }
}

void GameplayState::drawPlayerSwap()
{
    if (game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players) {
            player->drawswap();

            short storedpowerupid = game_values.gamepowerups[player->getGlobalID()];

            if (storedpowerupid != -1) {
                short iPowerupX, iPowerupY;

                if (game_values.swapstyle == MysteryMushroomSwapStyle::Blink) {
                    iPowerupX = game_values.flags.swapplayersblink ? player->iOldPowerupX : player->iNewPowerupX;
                    iPowerupY = game_values.flags.swapplayersblink ? player->iOldPowerupY : player->iNewPowerupY;
                } else {
                    iPowerupX = static_cast<short>(static_cast<float>(player->iNewPowerupX - player->iOldPowerupX) * game_values.flags.swapplayersposition) + player->iOldPowerupX;
                    iPowerupY = static_cast<short>(static_cast<float>(player->iNewPowerupY - player->iOldPowerupY) * game_values.flags.swapplayersposition) + player->iOldPowerupY;
                }

                rm->spr_storedpowerupsmall.draw(iPowerupX, iPowerupY, {storedpowerupid * 16, 0, 16, 16});
            }
        }
    }
}

void GameplayState::drawBackLayer()
{
    rm->spr_backmap[g_iCurrentDrawIndex].draw(0, 0);

    //draw back eyecandy behind players
    g_map->drawPlatforms(0);

    eyecandy[0].draw();
    g_sim.noncolcontainer.draw();

    game_values.gamemode->draw_background();

    g_sim.objectcontainer[0].draw();
}

void GameplayState::drawMiddleLayer()
{
    g_map->drawPlatforms(1);

    if (!game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players)
            player->draw();
    }

    eyecandy[1].draw();

    g_sim.objectcontainer[1].draw();
}

void GameplayState::drawFrontLayer()
{
    g_map->drawPlatforms(2);

    if (game_values.toplayer)
        g_map->drawfrontlayer();

    g_map->drawWarpLocks();

    g_map->drawPlatforms(3);

    g_sim.objectcontainer[2].draw();
    eyecandy[2].draw();
    game_values.gamemode->draw_foreground();

    g_map->drawPlatforms(4);
}

void GameplayState::drawWindMeter()
{
    if (game_values.windaffectsplayers) {
        short iDisplayWindMeterY = game_values.scoreboardstyle == ScoreboardStyle::Bottom ? 8 : 440;
        rm->spr_windmeter.draw(210, iDisplayWindMeterY, {0, 0, 220, 32});
        rm->spr_windmeter.draw(static_cast<short>(game_values.flags.gamewindx * 20.0f) + App::screenWidth/2, iDisplayWindMeterY + 6, {220, 0, 12, 20});
    }
}

void GameplayState::drawCountdown()
{
    //Draw countdown start timer
    if (g_sim.countdownState > 0 && game_values.screenfade == 0) {
        const SDL_Rect * rects = iCountDownNumbers[iCountDownRectGroup[28 - g_sim.countdownState]][iCountDownRectSize[28 - g_sim.countdownState]];
        rm->spr_countdown_numbers.draw(rects[1].x, rects[1].y, rects[0]);
    }
}

void GameplayState::drawSpotlights()
{
    if (game_values.spotlights)
        spotlightManager.DrawSpotlights();
}

void GameplayState::drawOutOfScreenIndicators()
{
    //draw arrows for being above the top of the screen
    for (CPlayer* player : g_sim.players)
        player->drawOutOfScreenIndicators();
}

void GameplayState::drawEverything(short iCountDownState, short iScoreTextOffset[4])
{
    drawBackLayer();
    drawMiddleLayer();
    drawFrontLayer();

    drawSpotlights();
    //(the theater zoomed in on a player: it draws the scores over the zoomed picture, not zoomed)
    if (!theaterDrawsScores() && !matchedit::hidesScoreboard())
        drawScoreboard(iScoreTextOffset);
    drawWindMeter();
    drawOutOfScreenIndicators();
    drawCountdown();
    drawScreenFade();
    drawPlayerSwap();
    drawScreenShakeBackground();
}

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
    if (game_values.screenfadespeed != 0) {
        g_map->update();
        game_values.screenfade += game_values.screenfadespeed;

        if (game_values.screenfade <= 0) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 0;

            //display the mode and goal at the start of the game
            //if (game_values.matchtype == MatchType::QuickGame)
            if (game_values.startmodedisplay && game_values.singleplayermode == -1) {
                std::string szMode;
                if (game_values.gamemode->goal < 0)
                    szMode = std::format("{}  {}: X", game_values.gamemode->GetModeName(), game_values.gamemode->GetGoalName());
                else
                    szMode = std::format("{}  {}: {}", game_values.gamemode->GetModeName(), game_values.gamemode->GetGoalName(), game_values.gamemode->goal);

                short iMode = GetModeIconIndexFromMode(game_values.gamemode->gamemode);

                eyecandy[2].emplace<EC_Announcement>(&rm->game_font_large, &rm->menu_mode_large, szMode, iMode, 130, 90);
            }
        } else if (game_values.screenfade >= 255) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 255;
        }
    }

    if (game_values.screenfade > 0) {
        rm->menu_shade.setalpha(static_cast<Uint8>(game_values.screenfade));
        rm->menu_shade.draw(0, 0);
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

        if (game_values.swapstyle == MysteryMushroomSwapStyle::Walk) {
            if (!rm->sfx_skid.isPlaying())
                ifSoundOnPlay(rm->sfx_skid);
        }

        if (++game_values.flags.swapplayersblinkcount > 10) {
            game_values.flags.swapplayersblinkcount = 0;
            game_values.flags.swapplayersblink = !game_values.flags.swapplayersblink;
        }

        game_values.flags.swapplayersposition += 0.02f;
        if (game_values.flags.swapplayersposition >= 1.0f) {
            game_values.flags.swapplayersposition = 0.0f;
            game_values.flags.swapplayers = false;
            game_values.screenfade = 0;

            if (game_values.swapstyle == MysteryMushroomSwapStyle::Walk)
                rm->sfx_skid.stop();

            ifSoundOnPlay(rm->sfx_transform);

            if (game_values.swapstyle == MysteryMushroomSwapStyle::Blink) {
                for (CPlayer* player : g_sim.players)
                    eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, player->leftX() + (HALFPW) - 16, player->topY() + (HALFPH) - 16, 3, 8);
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
    drawScoreboard(iScoreTextOffset);
    drawWindMeter();
    drawOutOfScreenIndicators();
    drawCountdown();
    drawScreenFade();
    drawPlayerSwap();
    drawScreenShakeBackground();
}

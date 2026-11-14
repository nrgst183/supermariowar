#include "GSGameplay.h"

#include "eyecandy.h"
#include "GameMode.h"
#include "GameValues.h"
#include "InputFrame.h"
#include "map.h"
#include "player.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "sfx.h"

#include <format>
#include <string>

#include "GSGameplayInternal.h"

//-----------------------------------------------------------------------------
// ONE TICK OF GAMEPLAY
//
// simulateTick() advances the match by exactly one 60 Hz step. It reads the
// tick's InputFrame and the simulation state (g_sim, the runtime parts of
// game_values, g_map, the game mode), and nothing else: no SDL events, no
// input devices, no clock, no network, no drawing. Purely cosmetic effects
// it triggers (sounds, music, eyecandy, spotlights, screen shake) are skipped
// while sim::isResimulating() is set.
//
// Order of one tick (the same order the old per-frame update ran these in):
//   1. input:      rebuild each player's key state from the InputFrame
//   2. force exit: the world map's forced end-of-stage timer
//   3. world:      countdown, then the world update (or only the background
//                  while counting down); held while the screen fades or the
//                  players swap
//   4. swap:       move the players of a running mystery mushroom swap
//   5. the steps that used to run inside the draw functions: tail animation,
//      screen fade (with its map animation), the swap animation
//   6. wind:       the wind step of the next tick. The old frame ran it before
//                  everything else, ahead of the frame-level checks (autotest,
//                  match end); running it at the end of the previous tick keeps
//                  that exact order of random numbers.
//-----------------------------------------------------------------------------

void GameplayState::simulateTick(const InputFrame& input)
{
    applyTickInput(input);

    if (game_values.screenfade == 0 && g_sim.countdownState <= COUNTDOWN_START_INDEX)
        updateForceExitTimer();

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

    //Formerly done while drawing (drawMiddleLayer, drawScreenFade, drawPlayerSwap)
    if (!game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players)
            player->updateTail();
    }
    updateScreenFade();
    updatePlayerSwapAnimation();

    updateWind();

    ++g_sim.tick;
    if (g_sim.gameoverTick < 0 && game_values.gamemode->gameover)
        g_sim.gameoverTick = g_sim.tick;
}

void GameplayState::advanceTick(const InputFrame& input)
{
    interpolator.capture();
    simulateTick(input);
}

void GameplayState::applyTickInput(const InputFrame& input)
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        COutputControl& keys = g_sim.playerKeys[iPlayer];

        //"Pressed" only lasts one tick (CPU players set it again in their AI)
        for (CKeyState& key : keys.keys)
            key.fPressed = false;

        //CPU players keep the keys their AI holds down
        if (game_values.playercontrol[iPlayer] == 1)
            applyInputByte(input.players[iPlayer], g_sim.previousInput[iPlayer], keys);

        g_sim.previousInput[iPlayer] = input.players[iPlayer];
    }
}

void GameplayState::resetTickInput()
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        g_sim.playerKeys[iPlayer] = COutputControl {};
        g_sim.previousInput[iPlayer] = 0;
    }
}

void updateWind()
{
    checkWindEvent(g_sim.windTimer, g_sim.nextWind);
}

//The world map can end a stage on its own after a while
void updateForceExitTimer()
{
    if (game_values.flags.forceexittimer > 0) {
        if (--game_values.flags.forceexittimer <= 0) {
            game_values.appstate = AppState::EndGame;
            game_values.screenfade = 8;
            game_values.screenfadespeed = 8;
        }
    }
}

void GameplayState::updateScreenFade()
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
}

void GameplayState::updatePlayerSwapAnimation()
{
    if (!game_values.flags.swapplayers)
        return;

    for (CPlayer* player : g_sim.players)
        player->updateSwapAnimation();

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

#include "rollback/Replay.h"
#include "rollback/Snapshot.h"
#include "GSGameplay.h"
#include "CpuDriver.h"

#include "eyecandy.h"
#include "GameMode.h"
#include "GameValues.h"
#include "map.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "CmdArgs.h"

#include <algorithm>

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
    lastTickInput = input;
    //(a tick run ahead isn't part of the match yet: the replay records the real one)
    if (!runningAhead)
        replay::recordTick(input);
    interpolator.capture();
    simulateTick(input);
    if (!runningAhead)
        replay::afterTick();
}

//-----------------------------------------------------------------------------
// RUN-AHEAD (online matches only)
//
// After the frame's real ticks, the state is saved and the match runs a few
// ticks further with the players' last known input (ours the newest). That state is drawn, so a button's effect shows those ticks
// sooner, as in an emulator's run-ahead. The next frame goes back to the saved
// state before anything else and runs the real tick; when the player kept the
// same buttons (nearly always), the result is the one already shown.
//
// Sounds and other cosmetic effects come from the newest tick run ahead, the
// first time it runs: the ticks run again later (ahead again, or for real) are
// re-simulations (Resimulation.h), and a sound the real input made that wasn't
// heard ahead is played late.
//-----------------------------------------------------------------------------

int GameplayState::runAheadFrames() const
{
    const int frames = cmd::g_autotest.enabled ? cmd::g_autotest.runahead : game_values.runahead;
    return std::clamp(frames, 0, 3);
}

void GameplayState::leaveRunAhead()
{
    //(a new match: the ticks start over)
    if (sim::detail::aheadTick > g_sim.tick + 4)
        sim::detail::aheadTick = -1;
    if (!aheadShown)
        return;
    aheadShown = false;
    rollback::loadState(*aheadReal);
}

void GameplayState::runAhead(const InputFrame& input)
{
    const int frames = runAheadFrames();
    if (frames <= 0 || aheadShown || game_values.appstate != AppState::Game || game_values.flags.pausegame ||
        game_values.flags.exitinggame || replay::playing())
        return;
    if (!aheadReal)
        aheadReal = std::make_unique<rollback::Snapshot>();
    rollback::saveState(*aheadReal);
    const InputFrame real = lastTickInput;
    runningAhead = true;
    bool shown = true;
    for (int k = 0; k < frames; k++) {
        const int tick = g_sim.tick;
        if (k < frames - 1 || tick <= sim::detail::aheadTick) {
            //(run ahead before: seen and heard then)
            sim::ResimulationScope scope;
            sim::detail::rollbackTick = tick;
            advanceTick(input);
            sim::detail::rollbackTick = -1;
        } else {
            sim::detail::shownTick = tick;
            advanceTick(input);
            sim::detail::aheadTick = tick;
        }
        //(the match ends or changes ahead: that happens for real, not ahead)
        if (game_values.appstate != AppState::Game) {
            shown = false;
            break;
        }
    }
    runningAhead = false;
    lastTickInput = real;
    if (shown)
        aheadShown = true;
    else
        rollback::loadState(*aheadReal);
}

// A tick of the match run for real (offline). Offline nothing runs ahead, but a tick already
// shown ahead is a re-simulation (its sounds were heard then).
void GameplayState::runRealTick(const InputFrame& input)
{
    const int tick = g_sim.tick;
    if (tick <= sim::detail::aheadTick) {
        sim::ResimulationScope scope;
        sim::detail::rollbackTick = tick;
        advanceTick(input);
        sim::detail::rollbackTick = -1;
    } else {
        sim::detail::shownTick = tick;
        advanceTick(input);
    }
}

void GameplayState::applyTickInput(const InputFrame& input)
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        COutputControl& keys = g_sim.playerKeys[iPlayer];

        //"Pressed" only lasts one tick (CPU players set it again in their AI)
        for (CKeyState& key : keys.keys)
            key.fPressed = false;

        //CPU players play by input too (CpuDriver.h); one that thinks inside the simulation (it took
        //over a human, or an old replay) keeps the keys its AI holds down
        if (game_values.playercontrol[iPlayer] == 1 && !(g_sim.cpuTakeover & (1u << iPlayer)))
            applyInputByte(input.players[iPlayer], g_sim.previousInput[iPlayer], keys);
        else if (cpudriver::playsByInput(iPlayer))
            applyCpuInputByte(input.players[iPlayer], g_sim.previousInput[iPlayer], keys);

        g_sim.previousInput[iPlayer] = input.players[iPlayer];
    }
}

void GameplayState::resetTickInput()
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        g_sim.playerKeys[iPlayer] = COutputControl {};
        g_sim.previousInput[iPlayer] = 0;
    }
    g_sim.cpuTakeover = 0;
}

void GameplayState::takeOverByCpu(uint8_t slotMask)
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        const uint8_t bit = static_cast<uint8_t>(1u << iPlayer);
        //(online, the host's CPU players too: they played by its input)
        if (!(slotMask & bit) || (g_sim.cpuTakeover & bit) || game_values.playercontrol[iPlayer] == 0)
            continue;
        g_sim.cpuTakeover |= bit;
        //Let go of the keys the human held; the AI presses its own
        g_sim.playerKeys[iPlayer] = COutputControl {};

        //The same AI as a CPU player that started the match (in the arena,
        //like the player: rolled back with it)
        for (CPlayer* player : g_sim.players) {
            if (player->getGlobalID() != iPlayer || player->pPlayerAI)
                continue;
            player->pPlayerAI = new CPlayerAI();
            player->pPlayerAI->SetPlayer(player);
            player->pPlayerAI->Init();
        }
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

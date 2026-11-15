#pragma once

#include "input.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "Score.h"

#include <array>
#include <cstdint>


// The state of the gameplay simulation of the current match, in one place.
//
// The goal is that everything a frame of gameplay reads and writes, apart from
// loaded resources and settings, lives here, so that the netcode can snapshot
// and restore it (rollback) or send it over the network.
//
// ROLLBACK: the containers here allocate from the simulation arena
// (common/SimArena.h), like the players and objects they own, so that
// rollback::saveState() (smw/rollback/Snapshot.h) can copy this struct byte
// for byte together with the arena. The state that still lives elsewhere and
// that a snapshot also captures (game_values runtime fields, g_map runtime
// state, the active game mode, the object id counter) is listed there.
//
// WHAT LIVES HERE
//  - players:          the players of the match (owned)
//  - noncolcontainer:  map blocks (owned)
//  - objectcontainer:  every other simulated object, by collision layer (owned)
//  - score, score_cnt: team scores and the number of teams
//  - rng:              the gameplay random number generator. This is the
//                      instance behind RandomNumberGenerator::gameplay() and
//                      RANDOM_INT() (bound at startup in main.cpp).
//  - respawnCount:     respawn delay of each player (CPlayer points into it)
//  - countdown*:       the "3, 2, 1, Go" countdown that holds the match back
//  - windTimer, nextWind: wind changes (wind pushes players)
//  - playerKeys:       the key state each player reads (CPlayer::playerKeys
//                      points here). Players get it from the tick's
//                      InputFrame, CPU players too (CpuDriver.h); a CPU that
//                      took over a human (or in an old replay) from its AI,
//                      which keeps keys held across ticks.
//  - previousInput:    each player's InputFrame byte of the previous tick,
//                      to derive "just pressed"
//  - cpuTakeover:      online, the slots (bit s) whose human player the CPU
//                      took over because their peer is gone
//  - tick:             number of ticks simulated in this match
//  - gameoverTick:     the tick at which the game mode declared the match
//                      over (-1 while it isn't): a deterministic "time since
//                      game over" for decisions every peer must make at the
//                      same frame (the online match end, see rollback/NetGame.h)
//
// WHAT DELIBERATELY DOES NOT LIVE HERE (yet)
//  - Resources and settings: rm, the file/map/skin/music lists, the tileset
//    manager, game_values settings, menus.
//  - game_values: mixes settings with per-match runtime fields (flags, timers,
//    the active game mode pointer, ...). It is not split in this pass.
//  - Game mode state: the per-match fields of each game mode (gameover,
//    winningteam, timers, mode-specific object pointers) live in the CGameMode
//    objects themselves (gamemodes[] and the minigame modes in main.cpp),
//    mixed with their settings. Moving them needs each mode split into
//    settings and state.
//  - g_map: mostly loaded level data, but it also holds runtime state that a
//    snapshot needs: moving platform positions, blockdata (pointers to blocks
//    in noncolcontainer), switch block lists and states, warp locks, and tile
//    changes made by destroyed blocks. CMap is shared with the editors and the
//    loaders, so it stays a separate global until it is split into level data
//    and runtime state.
//  - Cosmetic state: eyecandy, spotlightManager, the screen shake/spin render
//    offsets (x_shake, y_shake), the scoreboard's winning-player highlight,
//    respawn egg animations, and the cosmetic random number generator.
//  - Input devices and the menu state.
struct SimulationState {
    SimulationState() = default;
    SimulationState(const SimulationState&) = delete;
    SimulationState& operator=(const SimulationState&) = delete;

    // Declared before the players so that, as in CleanUp(), the players are
    // destroyed before the objects.
    CObjectContainer noncolcontainer;
    CObjectContainer objectcontainer[3];

    PlayerList players;

    std::array<CScore, 4> score {CScore(0), CScore(1), CScore(2), CScore(3)};
    short score_cnt = 0;

    RandomNumberGenerator rng;

    short respawnCount[4] = {};

    short countdownState = 0;
    short countdownTimer = 0;

    short windTimer = 0;
    float nextWind = 0.0f;

    COutputControl playerKeys[4] {};
    uint8_t previousInput[4] {};
    uint8_t cpuTakeover = 0;

    int tick = 0;
    int gameoverTick = -1;
};

// Defined in smw/GSGameplay.cpp and separately in the editors
extern SimulationState g_sim;

#pragma once

#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "Score.h"

#include <array>


// The state of the gameplay simulation of the current match, in one place.
//
// The goal is that everything a frame of gameplay reads and writes, apart from
// loaded resources and settings, lives here, so that a later netcode pass can
// snapshot and restore it (rollback) or send it over the network.
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
//  - Network state (netplay), input devices and the menu state.
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
};

// Defined in smw/GSGameplay.cpp and separately in the editors
extern SimulationState g_sim;

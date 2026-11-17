#include "GSGameplay.h"

#include "GameValues.h"
#include "map.h"
#include "SimulationState.h"
#include "gamemodes/Tag.h"

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

#ifdef _DEBUG
void debugAutoKillEveryone()
{
    if (game_values.autokill) {
        for (CPlayer* player : g_sim.players) {
            player->DeathAwards();

            if (game_values.gamemode->playerkilledself(*player, KillStyle::Environment) != PlayerKillType::None)
                player->die(PlayerDeathStyle::Jump, false, false);
        }
    }
}

void debug_gameplay()
{
    static short endgametimer = static_cast<short>(COSMETIC_RANDOM_INT(200));
    if (g_fAutoTest && !game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players) {
            if (player->isready()) {
                //Detect player is in center of tile only
                short x = (player->centerX()) / TILESIZE;

                if (player->centerX() >= App::screenWidth)
                    x = (player->centerX() - App::screenWidth) / TILESIZE;

                short y = (player->centerY()) / TILESIZE;

                int tile = tile_flag_nonsolid;
                IO_Block * block = nullptr;
                short blocktype = -1;

                if (player->centerY() >= 0 && player->centerY() < App::screenHeight) {
                    tile = g_map->map(x, y);
                    block = g_map->block(x, y);
                    blocktype = g_map->blockat(x, y)->iType;
                }

                if ((tile & tile_flag_solid) ||
                        (block && blocktype != 3 && blocktype < 11)) {
                    game_values.flags.pausegame = true;
                    game_values.frameadvance = true;
                    g_fAutoTest = false;
                    break;
                }

                //Detect if any corner of player is in a tile
                short actualvalues[2][2];
                actualvalues[0][0] = player->leftX();

                if (actualvalues[0][0] < 0)
                    actualvalues[0][0] += App::screenWidth;

                actualvalues[0][1] = player->rightX();

                if (actualvalues[0][1] >= App::screenWidth)
                    actualvalues[0][1] -= App::screenWidth;

                actualvalues[1][0] = player->topY();
                actualvalues[1][1] = player->bottomY();

                short corners[2][2];
                corners[0][0] = player->leftX() / TILESIZE;

                if (player->leftX() < 0)
                    corners[0][0] = (player->leftX() + App::screenWidth) / TILESIZE;

                corners[0][1] = (player->rightX()) / TILESIZE;

                if (player->rightX() >= App::screenWidth)
                    corners[0][1] = (player->rightX() - App::screenWidth) / TILESIZE;

                corners[1][0] = player->topY() / TILESIZE;
                corners[1][1] = (player->bottomY()) / TILESIZE;

                for (short i = 0; i < 2; i++) {
                    for (short j = 0; j < 2; j++) {
                        int tile = tile_flag_nonsolid;
                        IO_Block * block = nullptr;
                        short blocktype = -1;

                        if (actualvalues[0][j] >= 0 && actualvalues[0][j] < App::screenWidth && actualvalues[1][i] > 0 && actualvalues[1][i] < App::screenHeight) {
                            tile = g_map->map(corners[0][j], corners[1][i]);
                            block = g_map->block(corners[0][j], corners[1][i]);
                            blocktype = g_map->blockat(corners[0][j], corners[1][i])->iType;
                        }

                        if ( (tile & tile_flag_solid) ||
                                (block && blocktype != 3 && blocktype < 11)) {
                            game_values.flags.pausegame = true;
                            game_values.frameadvance = true;
                            g_fAutoTest = false;
                            break;
                        }
                    }

                    if (game_values.flags.pausegame)
                        break;
                }
            }

            if (game_values.flags.pausegame)
                break;
        }

        //Automatically run menus
        if (game_values.flags.showscoreboard) {
            if (--endgametimer < 0) {
                endgametimer = static_cast<short>(COSMETIC_RANDOM_INT(200));

                if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame)
                    UpdateScoreBoard();

                CleanUp();
                game_values.appstate = AppState::Menu;

                return;
            }
        } else {
            if (++exitgametimer >= 8000) {
                CleanUp();
                game_values.flags.exitinggame = false;
                game_values.flags.exityes = false;
                game_values.appstate = AppState::Menu;

                return;
            }
        }
    }
}
#endif

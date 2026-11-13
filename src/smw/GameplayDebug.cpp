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

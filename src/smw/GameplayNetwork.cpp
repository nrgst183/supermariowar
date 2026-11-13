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

void GameplayState::read_network()
{
    if (!netplay.active)
        return;

    // React to network events
    // The host receives remote input
    netplay.gamestate_changed = false;
    netplay.client.update();

    float percent_new = std::min(1.0f, static_cast<float>(netplay.frames_since_last_gamestate) / NET_GAMESTATE_FRAMES_TO_SEND);
    float percent_old = 1.0f - percent_new;
    assert(0 <= percent_new && percent_new <= 1.0f);
    assert(0 <= percent_old && percent_old <= 1.0f);
    assert(0.99 < percent_old + percent_new && percent_old + percent_new <= 1.01);

    if (!netplay.theHostIsMe) {
        for (size_t p = 0; p < g_sim.players.size(); p++) {
            CPlayer* player = g_sim.players[p];

            // if local player
            if (p == netplay.remotePlayerNumber) {
                // save the player data that was cause by the input of the previous frame
                Net_IndexedPlayerData pdata(netplay.current_input_counter);
                pdata.x = player->fx;
                pdata.y = player->fy;
                pdata.xvel = player->velx;
                pdata.yvel = player->vely;
                netplay.local_playerdata_buffer.push_back(pdata);
                netplay.local_playerdata_store_time[netplay.current_input_counter] = std::chrono::system_clock::now();
                netplay.current_input_counter++;

                if (netplay.gamestate_changed) {
                    // remove old, saved player data
                    uint8_t confirmed_index = netplay.last_confirmed_input + 1;
                    nettimepoint confirmed_until = netplay.local_playerdata_store_time[confirmed_index];
                    uint8_t iid;
                    Net_IndexedPlayerData last_popped_local(0xFF);
                    while (!netplay.local_playerdata_buffer.empty()) {
                        iid = netplay.local_playerdata_buffer.front().input_id;
                        if (confirmed_until <= netplay.local_playerdata_store_time[iid])
                            break;

                        last_popped_local = netplay.local_playerdata_buffer.front();
                        netplay.local_playerdata_buffer.pop_front();
                    }

                    // check if we can use the local player data,
                    // or the difference is so big we have to fall back
                    // to the remote, confirmed positions
                    bool use_remote_pdata = false;
                    if (netplay.local_playerdata_buffer.empty()) {
                        use_remote_pdata = true;
                    }
                    else {
                        Net_PlayerData pd_remote = netplay.latest_playerdata.player[p];

                        if (std::abs(last_popped_local.x - pd_remote.x) > 0.45f ||
                            std::abs(last_popped_local.y - pd_remote.y) > 0.45f)
                        {
                            use_remote_pdata = true;
                        }
                    }

                    if (use_remote_pdata) {
                        // the buffered data is now invalid
                        netplay.local_playerdata_buffer.clear();

                        // set confirmed data
                        player->fx = netplay.latest_playerdata.player[p].x;
                        player->fy = netplay.latest_playerdata.player[p].y;
                        player->velx = netplay.latest_playerdata.player[p].xvel;
                        player->vely = netplay.latest_playerdata.player[p].yvel;
                    }
                }
            }
            // for remote players, interpolate
            else {
                player->fx   = percent_old * netplay.previous_playerdata.player[p].x    + percent_new * netplay.latest_playerdata.player[p].x;
                player->fy   = percent_old * netplay.previous_playerdata.player[p].y    + percent_new * netplay.latest_playerdata.player[p].y;
                player->velx = percent_old * netplay.previous_playerdata.player[p].xvel + percent_new * netplay.latest_playerdata.player[p].xvel;
                player->vely = percent_old * netplay.previous_playerdata.player[p].yvel + percent_new * netplay.latest_playerdata.player[p].yvel;
            }
        }

        netplay.frames_since_last_gamestate++;
    }


    if (previous_playerKeys != *current_playerKeys) {
        previous_playerKeys = *current_playerKeys;
    }

    // Consume the next input from the remote input buffer
    netplay.netPlayerInput.ClearGameActionKeys();
    if (netplay.theHostIsMe)
        netplay.client.local_gamehost.confirmCurrentInputs();
    for (size_t p = 0; p < g_sim.players.size(); p++) {
        if (netplay.remote_input_buffer[p].size() > 0) {
            netplay.netPlayerInput.outputControls[p] = netplay.remote_input_buffer[p].front().second;
            netplay.remote_input_buffer[p].pop_front();
        }
    }

    //printf("[%d;%d]\n", current_playerKeys->keys[0].fDown, current_playerKeys->keys[0].fPressed);
    //printf("%d -> %f\n", list_players[0]->leftX(), list_players[0]->fx);
}

void network_send_local_input()
{
    if (!netplay.active)
        return;

    netplay.client.storeLocalInput();
    netplay.client.sendLocalInput();
}

void network_broadcast_game_state()
{
    // The host sends the game state to clients
    if (netplay.active && netplay.theHostIsMe) {
        netplay.client.local_gamehost.sendCurrentGameStateIfNeeded();
        netplay.client.local_gamehost.update();
    }
}

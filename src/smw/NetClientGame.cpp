#include "net.h"

#include "GlobalConstants.h"
#include "GameMode.h"
#include "GameModeSettings.h"
#include "GameValues.h"
#include "FileList.h"
#include "path.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "ui/NetworkListScroll.h"

#include "network/FileCompressor.h"
#include "network/ProtocolGamePackages.h"
#include "network/NetConfigManager.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <ctime>
#include <string>

#include "NetInternal.h"

/****************************
    Phase 3: Play
****************************/

void NetClient::sendLeaveGameMessage()
{
    // FIXME: make this function better

    NetPkgs::LeaveGame pkg;
    if (netplay.theHostIsMe) {
        // disconnect all players
        local_gamehost.sendMessageToMyPeers(&pkg, sizeof(NetPkgs::LeaveGame));
        local_gamehost.update();
        // stop game host
        local_gamehost.stop();

        //apply to local client
        setAsLastReceivedMessage(pkg.packageType);
    } else {
        // diconnect from foreign game host
        sendMessageToGameHostReliable(&pkg, sizeof(NetPkgs::LeaveGame));
        assert(!local_gamehost.active);
    }

    //state change
    //...
    netplay.gameRunning = false;
}

void NetClient::storeLocalInput()
{
    if (netplay.theHostIsMe)
        netplay.local_input_buffer.push_back(game_values.playerInput.outputControls[0]);
}

void NetClient::sendLocalInput()
{
    if (netplay.theHostIsMe) {
        local_gamehost.sendLocalInput();
    }
    else {
        NetPkgs::ClientInput pkg(&game_values.playerInput.outputControls[0]);
        pkg.input_id = netplay.current_input_counter;
        sendMessageToGameHost(&pkg, sizeof(NetPkgs::ClientInput));
    }

    //game_values.playerInput.outputControls[iGlobalID];
}

// A client wants to use powerup, so asks the boss for validation
void NetClient::sendPowerupRequest()
{
    assert(!netplay.theHostIsMe);

    NetPkgs::RequestPowerup pkg;
    sendMessageToGameHostReliable(&pkg, sizeof(NetPkgs::RequestPowerup));
}

void NetClient::handlePowerupStart(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::StartPowerup), "StartPowerup"))
        return;

    NetPkgs::StartPowerup pkg(0xFF, 0, 0);
    memcpy(&pkg, data, sizeof(NetPkgs::StartPowerup));

    assert(pkg.player_id < 4);
    if (pkg.player_id > 3)
        return;

    assert(g_sim.players[pkg.player_id]);
    if (!g_sim.players[pkg.player_id])
        return;

    unsigned missed_frames = pkg.delay;
    if (!netplay.theHostIsMe)
        missed_frames += foreign_gamehost->averageRTT() / 2;
    missed_frames /= WAITTIME; // ms to frames

    // TODO: make this nicer
    // TODO: this might be overriden by a late packet, in tryReleasingPowerup()
    g_sim.players[pkg.player_id]->powerupused = static_cast<PowerupType>(pkg.powerup_id);
    g_sim.players[pkg.player_id]->powerupradius = 100.0f;
    for (; missed_frames > 0; missed_frames--) {
        g_sim.players[pkg.player_id]->powerupradius -= static_cast<float>(game_values.storedpowerupdelay) / 2.0f;
    }

    // TODO: replace this eventually with the powerup trigger package
    g_sim.players[pkg.player_id]->net_waitingForPowerupTrigger = false;
    printf("[net] P%d used powerup %d\n", pkg.player_id, pkg.powerup_id);
}


void NetClient::handleMapCollision(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::MapCollision), "MapCollision"))
        return;

    NetPkgs::MapCollision pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::MapCollision));

    assert(pkg.player_id < 4);
    if (pkg.player_id > 3)
        return;

    assert(g_sim.players[pkg.player_id]);
    if (!g_sim.players[pkg.player_id])
        return;

    //printf("[net] Map collision triggered for P%d.\n", pkg.player_id);
    float temp_px = g_sim.players[pkg.player_id]->fx;
    float temp_py = g_sim.players[pkg.player_id]->fy;
    float temp_velx = g_sim.players[pkg.player_id]->velx;
    float temp_vely = g_sim.players[pkg.player_id]->vely;
    g_sim.players[pkg.player_id]->setXf(pkg.player_x);
    g_sim.players[pkg.player_id]->setYf(pkg.player_y);
    g_sim.players[pkg.player_id]->velx = pkg.player_xvel;
    g_sim.players[pkg.player_id]->vely = pkg.player_yvel;

    netplay.allowMapCollisionEvent = true;
    g_sim.players[pkg.player_id]->collision_detection_map();
    netplay.allowMapCollisionEvent = false;

    if (g_sim.players[pkg.player_id]->isdead())
        return;

    g_sim.players[pkg.player_id]->setXf(temp_px);
    g_sim.players[pkg.player_id]->setYf(temp_py);
    g_sim.players[pkg.player_id]->velx = temp_velx;
    g_sim.players[pkg.player_id]->vely = temp_vely;

    printf("[net] Map block collision!\n");
}

void NetClient::handleP2PCollision(const uint8_t * data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::P2PCollision), "P2PCollision"))
        return;

    NetPkgs::P2PCollision pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::P2PCollision));

    assert(pkg.player_id[0] < 4);
    assert(pkg.player_id[1] < 4);
    if (pkg.player_id[0] > 3 || pkg.player_id[1] > 3)
        return;

    assert(g_sim.players[pkg.player_id[0]]);
    assert(g_sim.players[pkg.player_id[1]]);
    if (!g_sim.players[pkg.player_id[0]] || !g_sim.players[pkg.player_id[1]])
        return;

    CPlayer* player1 = g_sim.players[pkg.player_id[0]];
    CPlayer* player2 = g_sim.players[pkg.player_id[1]];

    // float temp_p1x = player1.fx;
    // float temp_p1y = player1.fy;
    player1->setXf(pkg.player_x[0]);
    player1->setYf(pkg.player_y[0]);
    player1->fOldY = pkg.player_oldy[0];

    // float temp_p2x = player2.fx;
    // float temp_p2y = player2.fy;
    player2->setXf(pkg.player_x[1]);
    player2->setYf(pkg.player_y[1]);
    player2->fOldY = pkg.player_oldy[1];

    player1->collidesWith(player2);

    // TODO: restore position?

    printf("P%d collided with P%d!\n", pkg.player_id[0], pkg.player_id[1]);
}

void NetClient::handleRemoteInput(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::RemoteInput), "RemoteInput"))
        return;

    NetPkgs::RemoteInput pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::RemoteInput));

    if (pkg.playerNumber > 3) {
        printf("[net][error] Corrupt RemoteInput package arrived: bad player number %u\n", pkg.playerNumber);
        return;
    }

    COutputControl keys;
    pkg.readKeys(&keys);
    netplay.remote_input_buffer[pkg.playerNumber].push_back(std::make_pair(0, keys));
}

void NetClient::handleRemoteGameState(const uint8_t* data, size_t dataLength) // for other clients
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::GameState), "GameState"))
        return;

    NetPkgs::GameState pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::GameState));

    netplay.previous_playerdata = netplay.latest_playerdata;

    for (uint8_t p = 0; p < g_sim.players.size(); p++) {
        pkg.getPlayerCoord(p, netplay.latest_playerdata.player[p].x, netplay.latest_playerdata.player[p].y);
        pkg.getPlayerVel(p, netplay.latest_playerdata.player[p].xvel, netplay.latest_playerdata.player[p].yvel);
    }

    netplay.gamestate_changed = true;
    netplay.frames_since_last_gamestate = 0;
    netplay.last_confirmed_input = pkg.last_confirmed_local_input_id;
}

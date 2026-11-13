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

NetGameHost::NetGameHost()
    : active(false)
    , current_server_tick(0)
    , foreign_lobbyserver(nullptr)
    , expected_client_count(0)
    , next_free_client_slot(0)
{
    //printf("NetGameHost::ctor\n");
    for (short p = 0; p < 3; p++) {
        clients[p] = nullptr;
        last_processed_input_id[p] = 0xFF;
    }

    preparedMapCollPkg = std::make_unique<NetPkgs::MapCollision>();
}

NetGameHost::~NetGameHost()
{
    //printf("NetGameHost::dtor\n");
    cleanup();
}

bool NetGameHost::init()
{
    //printf("NetGameHost::init\n");
    return true;
}

bool NetGameHost::start(NetPeer*& lobbyserver)
{
    assert(lobbyserver);

    if (active) {
        assert(lobbyserver == foreign_lobbyserver);
        return true;
    }

    assert(!foreign_lobbyserver);

    //printf("NetGameHost::start\n");
    foreign_lobbyserver = lobbyserver;

    if (!networkHandler.gamehost_restart())
        return false;

    active = true;
    printf("[net] GameHost started.\n");
    return true;
}

void NetGameHost::update()
{
    if (active)
        networkHandler.gamehost_listen(*this);
}

void NetGameHost::stop()
{
    if (!active)
        return;

    //printf("NetGameHost::stop\n");

    foreign_lobbyserver = nullptr; // NetClient may still be connected
    next_free_client_slot = 0;
    expected_client_count = 0;
    for (short p = 0; p < 3; p++) {
        if (clients[p]) {
            clients[p]->disconnect();
            clients[p] = nullptr;
        }
        expected_clients[p].reset();
        last_processed_input_id[p] = 0xFF;
    }

    networkHandler.gamehost_shutdown();
    active = false;
    printf("[net] GameHost stopped.\n");
}

void NetGameHost::cleanup()
{
    //printf("NetGameHost::cleanup\n");
    stop();
}

// A client connected to this host
void NetGameHost::onConnect(NetPeer* new_player)
{
    // GameHost must start after setExpectedPlayers
    assert(expected_client_count > 0);
    assert(clients[0] != new_player);
    assert(clients[1] != new_player);
    assert(clients[2] != new_player);

    if (next_free_client_slot >= 3) {
        new_player->disconnect();
        printf("[error] More than 3 players want to join.\n");
        return;
    }

    // Validation
    // Is this one of the clients we're expecting to join?
    RawPlayerAddress incoming;
    incoming.host = new_player->addressHost();
    incoming.port = new_player->addressPort();

    bool valid_address = false;
    for (unsigned short c = 0; c < expected_client_count && !valid_address; c++) {
        if (incoming == expected_clients[c])
            valid_address = true;
    }

    // Intruder alert
    if (!valid_address) {
        printf("[warning] An unexpected client wants to join the game.\n");
        new_player->disconnect();
        return;
    }

    // Succesful connection
    assert(clients[next_free_client_slot] == nullptr);
    clients[next_free_client_slot] = new_player;

    next_free_client_slot++; // placing this before printf makes the log nicer
    printf("%d/%d player connected.\n", next_free_client_slot, expected_client_count);

    // When all players connected,
    // sychronize them (eg. same RNG seed)
    if (next_free_client_slot == expected_client_count) {
        sendSyncMessages();
    }
}

void NetGameHost::onDisconnect(NetPeer& player)
{
    printf("[net] Client %s disconnected\n", player.addressAsString().c_str());
    for (short c = 0; c < expected_client_count; c++) {
        if (clients[c] && *clients[c] == player) {
            netplay.player_disconnected[c + 1] = true;
            return;
        }
    }
}

void NetGameHost::onReceive(NetPeer& player, const uint8_t* data, size_t dataLength)
{
    assert(data);
    assert(dataLength >= 3);
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::MessageHeader), "MessageHeader"))
        return;

    uint8_t protocolMajor = data[0];
    uint8_t protocolMinor = data[1];
    uint8_t packageType = data[2];
    if (protocolMajor != NET_PROTOCOL_VERSION_MAJOR
        || protocolMinor != NET_PROTOCOL_VERSION_MINOR) {
        printf("Not implemented: bad protocol version\n");
        return;
    }

    switch (packageType)
    {
        case NET_P2G_SYNC_OK:
            handleSyncOKMessage(player, data, dataLength);
            break;

        case NET_P2G_LOCAL_KEYS:
            handleRemoteInput(player, data, dataLength);
            break;

        case NET_P2G_REQ_POWERUP:
            handlePowerupRequest(player, data, dataLength);
            break;

        default:
            printf("GH Unknown: ");
            for (unsigned a = 0; a < dataLength; a++)
                printf("%3d ", data[a]);
            printf("\n");
            return; // do not set as last message
    }
}

void NetGameHost::sendStartRoomMessage()
{
    assert(foreign_lobbyserver);

    NetPkgs::StartRoom pkg;
    foreign_lobbyserver->sendReliable(&pkg, sizeof(NetPkgs::StartRoom));
    setAsLastSentMessage(pkg.packageType);
}

void NetGameHost::sendSyncMessages()
{
    printf("[net] Prepare launching the game...\n");
    assert(clients[0] || clients[1] || clients[2]);

    // send syncronization package
    RandomNumberGenerator::gameplay().seedFromTime();
    NetPkgs::StartSync pkg(RANDOM_INT(32767 /* RAND_MAX */));
    sendMessageToMyPeers(&pkg, sizeof(pkg));

    // apply syncronization package on local client
    netplay.client.handleStartSyncMessage(reinterpret_cast<uint8_t*>(&pkg), sizeof(pkg));
    netplay.client.setAsLastReceivedMessage(pkg.packageType);
}

void NetGameHost::handleSyncOKMessage(const NetPeer& player, const uint8_t* data, size_t dataLength)
{
    assert(player == *clients[0] || player == *clients[1] || player == *clients[2]);

    uint8_t readyPlayerCount = 0;
    for (unsigned short c = 0; c < expected_client_count; c++)
    {
        if (player == *clients[c]) {
            expected_clients[c].sync_ok = true;
            printf("Client %d/%d ready.\n", c + 1, expected_client_count);
        }

        if (expected_clients[c].sync_ok)
            readyPlayerCount++;
    }

    if (readyPlayerCount == expected_client_count) {
        sendStartGameMessage();
    }
}

void NetGameHost::setExpectedPlayers(uint8_t count, uint32_t* hosts, uint16_t* ports)
{
    assert(count);
    assert(hosts);
    assert(ports);

    expected_client_count = count;
    for (unsigned short c = 0; c < count; c++) {
        assert(hosts[c]);
        assert(ports[c]);
        expected_clients[c].host = hosts[c];
        expected_clients[c].port = ports[c];
    }

    printf("[net] Game starts soon, waiting for %d players.\n", expected_client_count);

    for (unsigned short c = 0; c < expected_client_count; c++)
        networkHandler.natPunch(expected_clients[c].host, expected_clients[c].port);
}

void NetGameHost::sendStartGameMessage()
{
    assert(foreign_lobbyserver);
    assert(clients[0] || clients[1] || clients[2]);

    NetPkgs::StartGame pkg;
    foreign_lobbyserver->sendReliable(&pkg, sizeof(NetPkgs::StartGame));
    sendMessageToMyPeers(&pkg, sizeof(NetPkgs::StartGame));

    netplay.client.handleGameStartMessage();
    netplay.client.setAsLastReceivedMessage(pkg.packageType);
}

void NetGameHost::sendLocalInput()
{
    if (netplay.local_input_buffer.empty())
        return;

    NetPkgs::RemoteInput pkg(0);
    pkg.writeKeys(netplay.local_input_buffer.front());
    netplay.local_input_buffer.pop_front();

    for (int c = 0; c < 3; c++) {
        if (clients[c]) {
            clients[c]->send(&pkg, sizeof(NetPkgs::RemoteInput));
        }
    }

    netplay.client.handleRemoteInput(reinterpret_cast<uint8_t*>(&pkg), sizeof(pkg));
    netplay.client.setAsLastReceivedMessage(pkg.packageType);
}

void NetGameHost::sendCurrentGameStateIfNeeded()
{
    if (current_server_tick % NET_GAMESTATE_FRAMES_TO_SEND == 0)
        sendCurrentGameStateNow();

    current_server_tick++;
}

void NetGameHost::sendCurrentGameStateNow()
{
    NetPkgs::GameState pkg;

    for (uint8_t p = 0; p < g_sim.players.size(); p++) {
        pkg.setPlayerCoord(p, g_sim.players[p]->fx, g_sim.players[p]->fy);
        pkg.setPlayerVel(p, g_sim.players[p]->velx, g_sim.players[p]->vely);
    }

    for (unsigned short c = 0; c < expected_client_count; c++) {
        if (clients[c]) {
            pkg.last_confirmed_local_input_id = last_processed_input_id[c];
            clients[c]->send(&pkg, sizeof(NetPkgs::GameState));
        }
    }

    netplay.client.handleRemoteGameState(reinterpret_cast<uint8_t*>(&pkg), sizeof(pkg));
    netplay.client.setAsLastReceivedMessage(pkg.packageType);
}

void NetGameHost::confirmCurrentInputs()
{
    for (unsigned short c = 0; c < expected_client_count; c++) {
        assert(clients[c]);
        if (netplay.remote_input_buffer[c + 1].empty())
            continue;

        // Careful, 255 < 0 !
        uint8_t input_id = netplay.remote_input_buffer[c + 1].front().first;
        last_processed_input_id[c] = (std::max)(last_processed_input_id[c], input_id);
        if (last_processed_input_id[c] > 200 && input_id < 50)
            last_processed_input_id[c] = input_id;
    }
}

void NetGameHost::handleRemoteInput(const NetPeer& player, const uint8_t* data, size_t dataLength) // only for room host
{
    assert(player == *clients[0] || player == *clients[1] || player == *clients[2]);

    if (!isPackageComplete(dataLength, sizeof(NetPkgs::ClientInput), "ClientInput"))
        return;

    NetPkgs::ClientInput* pkg = reinterpret_cast<NetPkgs::ClientInput*>(const_cast<uint8_t*>(data));

    for (unsigned short c = 0; c < expected_client_count; c++) {
        assert(clients[c]);

        // compare by content
        if (player == *clients[c]) {
            //printf("yey\n");

            // TODO: does this work if GH leaves?

            // TODO: Do not accept inputs from the past
            //if (last_processed_input[c] < pkg->counter) {
            //    last_processed_input[c] = pkg->counter;
                COutputControl keys;
                pkg->readKeys(&keys);
                netplay.remote_input_buffer[c + 1].push_back(std::make_pair(pkg->input_id, keys));
            //}

            NetPkgs::RemoteInput pkg_out(c + 1, pkg->input);
            for (unsigned short c_out = 0; c_out < expected_client_count; c_out++) {
                if (clients[c_out] && c != c_out) {
                    clients[c_out]->send(&pkg_out, sizeof(NetPkgs::RemoteInput));
                }
            }


            return;
        }
    }
}

// The boss wants to use powerup, so notifies the clients
void NetGameHost::sendPowerupStartByGH()
{
    assert(netplay.theHostIsMe);
    assert(g_sim.players[netplay.remotePlayerNumber]->powerupused.has_value());

    NetPkgs::StartPowerup pkg(netplay.remotePlayerNumber,
        static_cast<uint8_t>(g_sim.players[netplay.remotePlayerNumber]->powerupused.value()), 0);
    sendMessageToMyPeers(&pkg, sizeof(NetPkgs::StartPowerup));

    g_sim.players[netplay.remotePlayerNumber]->net_waitingForPowerupTrigger = false;
    printf("[net] P%d (host) used powerup %d\n", pkg.player_id, pkg.powerup_id);
}

// A player wants to use a stored powerup
void NetGameHost::handlePowerupRequest(const NetPeer& player, const uint8_t* data, size_t dataLength)
{
    assert(player == *clients[0] || player == *clients[1] || player == *clients[2]);

    // NetPkgs::RequestPowerup* in_pkg = (NetPkgs::RequestPowerup*) data; // TODO: check size!

    uint8_t playerID = 0xFF;
    for (unsigned short c = 0; c < expected_client_count; c++) {
        if (player == *clients[c]) {
            // TODO: does this work if GH leaves?
            playerID = c + 1;
            break;
        }
    }

    assert(playerID < 4);
    assert(g_sim.players[playerID]);
    if (playerID == 0xFF)
        return;
    if (!g_sim.players[playerID])
        return;

    // TODO: this was copied from player.cpp
    int powerupused = game_values.gamepowerups[playerID];
    assert(powerupused > 0);
    if (powerupused <= 0)
        return; // invalid request or de-sync

    game_values.gamepowerups[playerID] = -1;

    NetPkgs::StartPowerup pkg(playerID, powerupused, player.averageRTT() / 2);
    for (unsigned short c = 0; c < expected_client_count; c++) {
        if (clients[c]) {
            clients[c]->sendReliable(&pkg, sizeof(NetPkgs::StartPowerup));
        }
    }
    netplay.client.handlePowerupStart(reinterpret_cast<uint8_t*>(&pkg), sizeof(pkg));
    netplay.client.setAsLastReceivedMessage(pkg.packageType);
}


void NetGameHost::prepareMapCollisionEvent(CPlayer& player)
{
    assert(netplay.theHostIsMe);
    assert(preparedMapCollPkg);
    preparedMapCollPkg->fill(player);
}

void NetGameHost::sendMapCollisionEvent()
{
    assert(netplay.theHostIsMe);
    assert(preparedMapCollPkg);

    sendMessageToMyPeers(preparedMapCollPkg.get(), sizeof(NetPkgs::MapCollision));

    printf("[net] P%d collided with a map block\n", preparedMapCollPkg->player_id);
}

void NetGameHost::sendP2PCollisionEvent(CPlayer& p1, CPlayer& p2)
{
    assert(netplay.theHostIsMe);
    assert(!p1.isdead());
    assert(!p2.isdead());

    NetPkgs::P2PCollision pkg(p1, p2);
    sendMessageToMyPeers(&pkg, sizeof(NetPkgs::P2PCollision));

    printf("[net] P%d collided with P%d\n", p1.getGlobalID(), p2.getGlobalID());
}

bool NetGameHost::sendMessageToMyPeers(const void* data, size_t dataLength)
{
    assert(data);
    assert(dataLength >= 3);

    for (unsigned short c = 0; c < 3; c++) {
        if (clients[c])
            clients[c]->sendReliable(data, dataLength);
    }

    setAsLastSentMessage(static_cast<const uint8_t*>(data)[2]);
    return true;
}

void NetGameHost::setAsLastSentMessage(uint8_t packageType)
{
    lastSentMessage.packageType = packageType;
    lastSentMessage.timestamp = SDL_GetTicks();
}

void NetGameHost::setAsLastReceivedMessage(uint8_t packageType)
{
    lastReceivedMessage.packageType = packageType;
    lastReceivedMessage.timestamp = SDL_GetTicks();
}

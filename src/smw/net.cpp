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

NetworkHandler networkHandler;
Networking netplay;

short backup_playercontrol[4];

Net_PlayerData::Net_PlayerData()
    : x(0), y(0), xvel(0), yvel(0)
{}

Net_IndexedPlayerData::Net_IndexedPlayerData(uint8_t id)
    : input_id(id)
{}

bool net_init()
{
    netplay.active = false;
    netplay.connectSuccessful = false;
    netplay.joinSuccessful = false;
    netplay.gameRunning = false;

    netplay.myPlayerName = "Player";
    netplay.currentMenuChanged = false;
    netplay.theHostIsMe = false;
    netplay.selectedRoomIndex = 0;
    netplay.selectedServerIndex = 0;
    netplay.roomFilter.clear();
    netplay.newroom_name.clear();
    netplay.newroom_password.clear();
    netplay.mychatmessage.clear();
    netplay.allowMapCollisionEvent = false;

    if (!networkHandler.init())
        return false;

    if (!netplay.client.init())
        return false;

    net_loadServerList();

    printf("[net] Network system initialized.\n");
    return true;
}

void net_close()
{
    net_saveServerList();

    net_endSession();
    netplay.client.cleanup();

    networkHandler.cleanup();
}

void net_saveServerList()
{
    NetConfigManager config;
    config.save();
}

void net_loadServerList()
{
    NetConfigManager config;
    config.load();
}

/****************************
    Session
****************************/

bool net_startSession()
{
    net_endSession(); // Finish previous network session if active

    printf("[net] Session start.\n");
    netplay.active = true;
    netplay.connectSuccessful = false;
    netplay.gamestate_changed = false;
    netplay.frames_since_last_gamestate = 0;
    std::fill_n(netplay.player_disconnected, 4, false);

    COutputControl nullinput {}; // set every field to false
    for (uint8_t p = 0; p < 4; p++) {
        // backup singleplayer settings
        backup_playercontrol[p] = game_values.playercontrol[p];

        netplay.latest_playerdata.player_input[p].clear();
        netplay.latest_playerdata.player_input[p].push_back(nullinput);

        netplay.local_input_buffer.clear();
        netplay.remote_input_buffer[p].clear();
    }

    return netplay.client.start();
}

void net_endSession()
{
    if (netplay.active) {
        printf("[net] Session end.\n");

        netplay.client.stop();

        netplay.active = false;
        netplay.connectSuccessful = false;

        // restore singleplayer settings
        for (uint8_t p = 0; p < 4; p++)
            game_values.playercontrol[p] = backup_playercontrol[p];
    }
}

/********************************************************************
 * NetClient
 ********************************************************************/

// Every sender sends the full package struct, so anything shorter is corrupt
// and must not be read past its end.
bool isPackageComplete(size_t dataLength, size_t expectedLength, const char* packageName)
{
    if (dataLength >= expectedLength)
        return true;

    printf("[net][error] Corrupt %s package arrived (%zu bytes, expected %zu)\n",
        packageName, dataLength, expectedLength);
    return false;
}

NetClient::NetClient()
    : foreign_lobbyserver(nullptr)
    , foreign_gamehost(nullptr)
    , uiRoomList(nullptr)
{
    //printf("NetClient::ctor\n");
}

NetClient::~NetClient()
{
    //printf("NetClient::dtor\n");
    cleanup();
}

bool NetClient::init()
{
    //printf("NetClient::init\n");
    // init client stuff first
    // ...

    // then finally gamehost
    if (!local_gamehost.init())
        return false;

    return true;
}

bool NetClient::start() // startSession
{
    //printf("NetClient::start\n");
    if (!networkHandler.client_restart())
        return false;

    return true;
}

void NetClient::update()
{
    local_gamehost.update();
    networkHandler.client_listen(*this);
}

void NetClient::stop() // endSession
{
    //printf("NetClient::stop\n");
    local_gamehost.stop();

    if (netplay.connectSuccessful)
        sendGoodbye();

    if (foreign_lobbyserver) {
        foreign_lobbyserver->disconnect();
        foreign_lobbyserver = nullptr;
    }
    if (foreign_gamehost) {
        foreign_gamehost->disconnect();
        foreign_gamehost = nullptr;
    }

    networkHandler.client_shutdown();
}

void NetClient::cleanup()
{
    //printf("NetClient::cleanup\n");
    stop();

    // ...
}

void NetClient::setRoomListUIControl(MI_NetworkListScroll* control)
{
    uiRoomList = control;
}

/****************
    Listening
****************/

// This client sucesfully connected to a host
void NetClient::onConnect(NetPeer* newPeer)
{
    assert(newPeer);

    // TODO: add state checks
    if (!foreign_lobbyserver) {
        foreign_lobbyserver = newPeer;

        NetPkgs::ClientConnection message(netplay.myPlayerName.c_str());
        sendMessageToLobbyServer(&message, sizeof(NetPkgs::ClientConnection));
    }
    else if (!foreign_gamehost) {
        foreign_gamehost = newPeer;
    }
    else {
        // The client must not have more than 2 connections
        newPeer->disconnect();
        assert(0);
    }
}

void NetClient::onDisconnect(NetPeer& client)
{
    // Beware of the null pointers!

    if (foreign_gamehost) {
        if (client == *foreign_gamehost) {
            printf("[net] Disconnected from game host.\n");
            netplay.player_disconnected[netplay.remotePlayerNumber] = true;
            return;
        }
    }

    if (foreign_lobbyserver) {
        if (client == *foreign_lobbyserver) {
            printf("[net] Disconnected from lobby server.\n");
            return;
        }
    }

    printf("[net] Client %s disconnected\n", client.addressAsString().c_str());
}

void NetClient::onReceive(NetPeer& client, const uint8_t* data, size_t dataLength)
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

    // TODO: Add state checks too.
    switch (packageType)
    {
        //
        // Query
        //
        case NET_RESPONSE_BADPROTOCOL:
            printf("Not implemented: NET_RESPONSE_BADPROTOCOL\n");
            break;

        case NET_RESPONSE_SERVERINFO:
            handleServerinfoAndClose(data, dataLength);
            break;

        case NET_RESPONSE_SERVER_MOTD:
            printf("Not implemented: NET_RESPONSE_SERVER_MOTD\n");
            break;

        //
        // Connect
        //
        case NET_RESPONSE_CONNECT_OK:
            //printf("[net] Connection attempt successful.\n");
            netplay.connectSuccessful = true;
            sendSkinChange();
            break;

        case NET_RESPONSE_CONNECT_DENIED:
            printf("Not implemented: NET_RESPONSE_CONNECT_DENIED\n");
            netplay.connectSuccessful = false;
            break;

        case NET_RESPONSE_CONNECT_SERVERFULL:
            printf("Not implemented: NET_RESPONSE_CONNECT_SERVERFULL\n");
            netplay.connectSuccessful = false;
            break;

        case NET_RESPONSE_CONNECT_NAMETAKEN:
            printf("Not implemented: NET_RESPONSE_CONNECT_NAMETAKEN\n");
            netplay.connectSuccessful = false;
            break;

        //
        // Rooms
        //
        case NET_RESPONSE_ROOM_LIST_ENTRY:
            handleNewRoomListEntry(data, dataLength);
            break;

        case NET_RESPONSE_NO_ROOMS:
            printf("Not implemented: [net] There are no rooms currently on the server.\n");
            break;

        //
        // Join
        //
        case NET_RESPONSE_JOIN_OK:
            printf("Not implemented: [net] Joined to room.\n");
            netplay.joinSuccessful = true;
            netplay.theHostIsMe = false;
            break;

        case NET_RESPONSE_ROOM_FULL:
            printf("Not implemented: [net] The room is full\n");
            netplay.joinSuccessful = false;
            break;

        case NET_NOTICE_ROOM_CHANGE:
            handleRoomChangedMessage(data, dataLength);
            break;

        case NET_NOTICE_MAP_CHANGE:
            handleMapChangeMessage(data, dataLength);
            break;

        case NET_NOTICE_SKIN_CHANGE:
            handleSkinChangeMessage(data, dataLength);
            break;

        case NET_NOTICE_GAMEMODESETTINGS:
            handleGameModeSettingsChangeMessage(data, dataLength);
            break;

        case NET_NOTICE_ROOM_CHAT_MSG:
            handleRoomChatMessage(data, dataLength);
            break;

        //
        // Create
        //
        case NET_RESPONSE_CREATE_OK:
            handleRoomCreatedMessage(data, dataLength);
            break;

        case NET_RESPONSE_CREATE_ERROR:
            printf("Not implemented: NET_RESPONSE_CREATE_ERROR\n");
            break;

        //
        // Pre-game
        //
        case NET_L2P_GAMEHOST_INFO:
            handleRoomStartMessage(client, data, dataLength);
            break;

        case NET_L2G_CLIENTS_INFO:
            handleExpectedClientsMessage(client, data, dataLength);
            break;

        case NET_G2P_SYNC:
            handleStartSyncMessage(data, dataLength);
            break;

        case NET_G2E_GAME_START:
            handleGameStartMessage();
            break;

        //
        // Game
        //

        case NET_G2P_REMOTE_KEYS:
            handleRemoteInput(data, dataLength);
            break;

        case NET_G2P_GAME_STATE:
            handleRemoteGameState(data, dataLength);
            break;

        case NET_G2P_START_POWERUP:
            handlePowerupStart(data, dataLength);
            break;


        case NET_G2P_TRIGGER_MAPCOLL:
            handleMapCollision(data, dataLength);
            break;

        case NET_G2P_TRIGGER_P2PCOLL:
            handleP2PCollision(data, dataLength);
            break;

        //
        // Default
        //

        default:
            printf("Unknown: ");
            for (unsigned a = 0; a < dataLength; a++)
                printf("%3d ", data[a]);
            printf("\n");
            return; // do not set as last message
    }

    setAsLastReceivedMessage(packageType);
    if (packageType == NET_RESPONSE_CREATE_OK)
        printf("last: %d, %d\n", lastSentMessage.packageType, lastReceivedMessage.packageType);
}

/****************************
    Network Communication
****************************/

bool NetClient::connectLobby(const char* hostname, const uint16_t port)
{
    netplay.connectSuccessful = false;
    if (!networkHandler.connectToLobbyServer(hostname, port))
        return false;

    return true;
    // now we wait for CONNECT_OK
    // connectSuccessful will be set to 'true' there
}

bool NetClient::connectGameHost(const char* hostname, const uint16_t port)
{
    netplay.connectSuccessful = false;
    if (!networkHandler.connectToForeignGameHost(hostname, port))
        return false;

    return true;

    // now we wait for sync package
}

bool NetClient::sendTo(NetPeer*& peer, const void* data, int dataLength, bool reliable)
{
    assert(peer);
    assert(data);
    assert(dataLength >= 3);

    if (reliable) {
        if (!peer->send(data, dataLength))
            return false;
    }
    else {
        if (!peer->sendReliable(data, dataLength))
            return false;
    }

    setAsLastSentMessage(static_cast<const uint8_t*>(data)[2]);
    return true;
}

bool NetClient::sendMessageToLobbyServer(const void* data, int dataLength)
{
    return sendTo(foreign_lobbyserver, data, dataLength, true);
}

bool NetClient::sendMessageToGameHost(const void* data, int dataLength)
{
    return sendTo(foreign_gamehost, data, dataLength);
}

bool NetClient::sendMessageToGameHostReliable(const void* data, int dataLength)
{
    return sendTo(foreign_gamehost, data, dataLength, true);
}

void NetClient::setAsLastSentMessage(uint8_t packageType)
{
    //printf("setAsLastSentMessage: %d.\n", packageType);
    lastSentMessage.packageType = packageType;
    lastSentMessage.timestamp = SDL_GetTicks();
}

void NetClient::setAsLastReceivedMessage(uint8_t packageType)
{
    //printf("setAsLastReceivedMessage: %d.\n", packageType);
    lastReceivedMessage.packageType = packageType;
    lastReceivedMessage.timestamp = SDL_GetTicks();
}

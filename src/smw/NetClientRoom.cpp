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

union GameModeSettingsUnion {
    ClassicGameModeSettings classic;
    FragGameModeSettings frag;
    TimeGameModeSettings time;
    JailGameModeSettings jail;
    CoinGameModeSettings coins;
    StompGameModeSettings stomp;
    EggGameModeSettings egg;
    FlagGameModeSettings flag;
    ChickenGameModeSettings chicken;
    TagGameModeSettings tag;
    StarGameModeSettings star;
    DominationGameModeSettings domination;
    KingOfTheHillModeSettings kingofthehill;
    RaceGameModeSettings race;
    FrenzyGameModeSettings frenzy;
    SurvivalGameModeSettings survival;
    GreedGameModeSettings greed;
    HealthGameModeSettings health;
    CollectionGameModeSettings collection;
    ChaseGameModeSettings chase;
    ShyGuyTagGameModeSettings shyguytag;

    GameModeSettingsUnion() { memset(static_cast<void*>(this), 0, sizeof(GameModeSettingsUnion)); }

    void set_global(GameModeType mode) {
        switch (mode) {
        case game_mode_classic:     game_values.gamemodesettings.classic = classic; break;
        case game_mode_frag:        game_values.gamemodesettings.frag = frag; break;
        case game_mode_timelimit:   game_values.gamemodesettings.time = time; break;
        case game_mode_jail:        game_values.gamemodesettings.jail = jail; break;
        case game_mode_coins:       game_values.gamemodesettings.coins = coins; break;
        case game_mode_stomp:       game_values.gamemodesettings.stomp = stomp; break;
        case game_mode_eggs:        game_values.gamemodesettings.egg = egg; break;
        case game_mode_ctf:         game_values.gamemodesettings.flag = flag; break;
        case game_mode_chicken:     game_values.gamemodesettings.chicken = chicken; break;
        case game_mode_tag:         game_values.gamemodesettings.tag = tag; break;
        case game_mode_star:        game_values.gamemodesettings.star = star; break;
        case game_mode_domination:  game_values.gamemodesettings.domination = domination; break;
        case game_mode_koth:        game_values.gamemodesettings.kingofthehill = kingofthehill; break;
        case game_mode_race:        game_values.gamemodesettings.race = race; break;
        case game_mode_frenzy:      game_values.gamemodesettings.frenzy = frenzy; break;
        case game_mode_survival:    game_values.gamemodesettings.survival = survival; break;
        case game_mode_greed:       game_values.gamemodesettings.greed = greed; break;
        case game_mode_health:      game_values.gamemodesettings.health = health; break;
        case game_mode_collection:  game_values.gamemodesettings.collection = collection; break;
        case game_mode_chase:       game_values.gamemodesettings.chase = chase; break;
        case game_mode_shyguytag:   game_values.gamemodesettings.shyguytag = shyguytag; break;
        default:
            assert(false);
        }
    }

    void read_global(GameModeType mode) {
        switch (mode) {
        case game_mode_classic:     classic = game_values.gamemodesettings.classic; break;
        case game_mode_frag:        frag = game_values.gamemodesettings.frag; break;
        case game_mode_timelimit:   time = game_values.gamemodesettings.time; break;
        case game_mode_jail:        jail = game_values.gamemodesettings.jail; break;
        case game_mode_coins:       coins = game_values.gamemodesettings.coins; break;
        case game_mode_stomp:       stomp = game_values.gamemodesettings.stomp; break;
        case game_mode_eggs:        egg = game_values.gamemodesettings.egg; break;
        case game_mode_ctf:         flag = game_values.gamemodesettings.flag; break;
        case game_mode_chicken:     chicken = game_values.gamemodesettings.chicken; break;
        case game_mode_tag:         tag = game_values.gamemodesettings.tag; break;
        case game_mode_star:        star = game_values.gamemodesettings.star; break;
        case game_mode_domination:  domination = game_values.gamemodesettings.domination; break;
        case game_mode_koth:        kingofthehill = game_values.gamemodesettings.kingofthehill; break;
        case game_mode_race:        race = game_values.gamemodesettings.race; break;
        case game_mode_frenzy:      frenzy = game_values.gamemodesettings.frenzy; break;
        case game_mode_survival:    survival = game_values.gamemodesettings.survival; break;
        case game_mode_greed:       greed = game_values.gamemodesettings.greed; break;
        case game_mode_health:      health = game_values.gamemodesettings.health; break;
        case game_mode_collection:  collection = game_values.gamemodesettings.collection; break;
        case game_mode_chase:       chase = game_values.gamemodesettings.chase; break;
        case game_mode_shyguytag:   shyguytag = game_values.gamemodesettings.shyguytag; break;
        default:
            assert(false);
        }
    }
};

namespace {
// Strings from the network are not trusted to be NUL-terminated
template<size_t N>
void forceNulTerminated(char (&str)[N])
{
    str[N - 1] = '\0';
}
} // namespace

/****************************
    Phase 1: Connect
****************************/

bool NetClient::sendConnectRequestToSelectedServer()
{
    ServerAddress* selectedServer = &netplay.savedServers[netplay.selectedServerIndex];
    if (!connectLobby(selectedServer->hostname.c_str()))
        return false;

    netplay.operationInProgress = true;
    return true;
}

void NetClient::sendGoodbye()
{
    //printf("sendGoodbye\n");
    NetPkgs::ClientDisconnection msg;
    sendMessageToLobbyServer(&msg, sizeof(NetPkgs::ClientDisconnection));
}

void NetClient::handleServerinfoAndClose(const uint8_t* data, size_t dataLength)
{
    // TODO: Remove copies.

    if (isPackageComplete(dataLength, sizeof(NetPkgs::ServerInfo), "ServerInfo")) {
        NetPkgs::ServerInfo serverInfo;
        memcpy(&serverInfo, data, sizeof(NetPkgs::ServerInfo));
        forceNulTerminated(serverInfo.name);

        printf("[net] Server information: Name: %s, Protocol version: %u.%u  Players/Max: %d / %d\n",
            serverInfo.name, serverInfo.protocolMajorVersion, serverInfo.protocolMinorVersion,
            serverInfo.currentPlayerCount, serverInfo.maxPlayerCount);
    }

    foreign_lobbyserver->disconnect();
    foreign_lobbyserver = nullptr;
}

/****************************
    Phase 2: Join room
****************************/

void NetClient::requestRoomList()
{
    NetPkgs::RoomList msg;
    sendMessageToLobbyServer(&msg, sizeof(NetPkgs::RoomList));

    netplay.currentRooms.clear();
    if (uiRoomList)
        uiRoomList->Clear();
}

void NetClient::handleNewRoomListEntry(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::RoomInfo), "RoomInfo"))
        return;

    NetPkgs::RoomInfo roomInfo;
    memcpy(&roomInfo, data, sizeof(NetPkgs::RoomInfo));
    forceNulTerminated(roomInfo.name);

    RoomListEntry newRoom;
    newRoom.roomID = roomInfo.roomID;
    newRoom.name = roomInfo.name;
    newRoom.playerCount = roomInfo.currentPlayerCount;
    netplay.currentRooms.push_back(newRoom);
    printf("  Incoming room entry: [%u] %s (%d/4)\n", newRoom.roomID, newRoom.name.c_str(), newRoom.playerCount);

    if (uiRoomList) {
        // TODO: strings would be better
        char playerCountString[4] = {static_cast<char>('0' + roomInfo.currentPlayerCount), '/', '4', '\0'};
        uiRoomList->Add(newRoom.name, playerCountString);
    }
}

void NetClient::sendCreateRoomMessage()
{
    NetPkgs::NewRoom msg(netplay.newroom_name.c_str(), netplay.newroom_password.c_str());
    msg.gamemodeID = currentgamemode;
    game_values.gamemode = gamemodes[currentgamemode];
    msg.gamemodeGoal = game_values.gamemode->goal;

    sendMessageToLobbyServer(&msg, sizeof(NetPkgs::NewRoom));
    netplay.operationInProgress = true;
}

void NetClient::handleRoomCreatedMessage(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::NewRoomCreated), "NewRoomCreated"))
        return;

    printf("room created!\n");
    NetPkgs::NewRoomCreated pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::NewRoomCreated));

    assert(netplay.myPlayerName.size() <= NET_MAX_PLAYER_NAME_LENGTH);
    assert(netplay.newroom_name.size() <= NET_MAX_ROOM_NAME_LENGTH);

    netplay.currentRoom.roomID = pkg.roomID;
    netplay.currentRoom.hostPlayerNumber = 0;
    netplay.currentRoom.name = netplay.newroom_name;
    netplay.currentRoom.playerNames[0] = netplay.myPlayerName;
    netplay.currentRoom.playerNames[1] = "(empty)";
    netplay.currentRoom.playerNames[2] = "(empty)";
    netplay.currentRoom.playerNames[3] = "(empty)";

    //printf("Room created, ID: %u, %d\n", pkg.roomID, pkg.roomID);
    netplay.currentMenuChanged = true;
    netplay.joinSuccessful = true;
    netplay.theHostIsMe = true;

    game_values.playercontrol[0] = 1;
    game_values.playercontrol[1] = 0;
    game_values.playercontrol[2] = 0;
    game_values.playercontrol[3] = 0;

    sendMapChangeMessage();
    sendGameModeSettingsChangeMessage();

    local_gamehost.start(foreign_lobbyserver);
}

void NetClient::sendJoinRoomMessage()
{
    assert(netplay.selectedRoomIndex < netplay.currentRooms.size());

    printf("currentRooms[%d] = {id=%d, name='%s', cnt=%d}, össz: %lu\n",
        netplay.selectedRoomIndex,
        netplay.currentRooms.at(netplay.selectedRoomIndex).roomID,
        netplay.currentRooms.at(netplay.selectedRoomIndex).name.c_str(),
        netplay.currentRooms.at(netplay.selectedRoomIndex).playerCount,
        netplay.currentRooms.size());

    // TODO: implement password
    NetPkgs::JoinRoom msg(netplay.currentRooms.at(netplay.selectedRoomIndex).roomID, "");
    sendMessageToLobbyServer(&msg, sizeof(NetPkgs::JoinRoom));
    netplay.operationInProgress = true;
}

void NetClient::sendLeaveRoomMessage()
{
    local_gamehost.stop();

    NetPkgs::LeaveRoom msg;
    sendMessageToLobbyServer(&msg, sizeof(NetPkgs::LeaveRoom));
}

void NetClient::handleRoomChangedMessage(const uint8_t* data, size_t dataLength)
{
    //printf("ROOM CHANGED\n");
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::CurrentRoom), "CurrentRoom"))
        return;

    NetPkgs::CurrentRoom pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::CurrentRoom));
    forceNulTerminated(pkg.name);
    for (auto& playerName : pkg.playerName)
        forceNulTerminated(playerName);

    if (pkg.gamemodeID >= GAMEMODE_LAST) {
        printf("[net][error] Corrupt CurrentRoom package arrived: bad game mode %u\n", pkg.gamemodeID);
        return;
    }

    assert(strlen(pkg.name) <= NET_MAX_ROOM_NAME_LENGTH);
    netplay.currentRoom.roomID = pkg.roomID;
    netplay.currentRoom.name = pkg.name;

    printf("Room %u (%s) changed:\n", pkg.roomID, pkg.name);
    // printf("  host: %d\n", pkg.hostPlayerNumber);
    for (uint8_t p = 0; p < 4; p++) {
        assert(strlen(pkg.playerName[p]) <= NET_MAX_PLAYER_NAME_LENGTH);
        netplay.currentRoom.playerNames[p] = pkg.playerName[p];
        printf("  player %d: %s", p+1, netplay.currentRoom.playerNames[p].c_str());
        if (p == pkg.remotePlayerNumber)
            printf(" (me)");
        if (p == pkg.hostPlayerNumber)
            printf(" (HOST)");
        printf("\n");


        if (netplay.currentRoom.playerNames[p].compare("(empty)") == 0)
            game_values.playercontrol[p] = 0;
        //else if (strcmp(netplay.currentRoom.playerNames[p], "(bot)") == 0)
        //    game_values.playercontrol[p] = 2;
        //    game_values.playercontrol[p] = 1;
        else
            game_values.playercontrol[p] = 1; // valid player
    }

    // set the game mode
    assert(pkg.gamemodeID < GAMEMODE_LAST);
    currentgamemode = pkg.gamemodeID;
    game_values.gamemode = gamemodes[currentgamemode];
    game_values.gamemode->goal = pkg.gamemodeGoal;

    printf("  Game mode #%d: %s with %s: %d\n",
        currentgamemode, gamemodes[currentgamemode]->GetModeName().c_str(),
        gamemodes[currentgamemode]->GetGoalName().c_str(), game_values.gamemode->goal);

    netplay.theHostIsMe = false;
    netplay.currentRoom.hostPlayerNumber = pkg.hostPlayerNumber;
    netplay.remotePlayerNumber = pkg.remotePlayerNumber;
    netplay.hostPlayerNumber = pkg.hostPlayerNumber;
    if (netplay.remotePlayerNumber == pkg.hostPlayerNumber) {
        netplay.theHostIsMe = true;
        local_gamehost.start(foreign_lobbyserver);
    } else {
        local_gamehost.stop();
    }

    netplay.currentMenuChanged = true;
}

void NetClient::sendChatMessage(const char* message)
{
    assert(message);
    assert(strlen(message) > 0);
    assert(strlen(message) <= NET_MAX_CHAT_MSG_LENGTH);

    NetPkgs::RoomChatMsg pkg(netplay.remotePlayerNumber, message);
    sendMessageToLobbyServer(&pkg, sizeof(NetPkgs::RoomChatMsg));
}

void NetClient::sendMapChangeMessage()
{
    printf("[net] Sending map: %s\n", netplay.mapfilepath.c_str());
    assert(netplay.mapfilepath.length() > 4);

    // NOTE: This is just an alert
    // Make sure you didn't broke something by changing the package headers!
    // You can safely update this line then.
    static_assert(sizeof(NetPkgs::MessageHeader) == 3, "The size of Net_MessageHeader should be 3");

    CompressedData compressed = FileCompressor::compress(netplay.mapfilepath, sizeof(NetPkgs::MessageHeader));
    assert(compressed.data);
    assert(compressed.size > 4 + sizeof(NetPkgs::MessageHeader));
    if (!compressed.is_valid())
        return;
    if (compressed.size <= 4 + sizeof(NetPkgs::MessageHeader))
        return;

    NetPkgs::MessageHeader header(NET_NOTICE_MAP_CHANGE);
    memcpy(compressed.data, &header, sizeof(NetPkgs::MessageHeader));

    sendMessageToLobbyServer(compressed.data, compressed.size);
}

void NetClient::handleMapChangeMessage(const uint8_t* data, size_t dataLength)
{
    if (dataLength <= sizeof(NetPkgs::MessageHeader) + 4 /* un-/compressed size 2*2B */
        || dataLength > 20000
        || !data) {
        printf("[error] Corrupt map arrived\n");
        return;
    }

    // read
    NetPkgs::MessageHeader pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::MessageHeader));

    uint16_t full_size, compressed_size;
    memcpy(&full_size, data + sizeof(NetPkgs::MessageHeader), 2);
    memcpy(&compressed_size, data + sizeof(NetPkgs::MessageHeader) + 2, 2);

    printf("[net] Map arrived (C:%d unC:%d)\n", compressed_size, full_size);

    netplay.mapfilepath = GetHomeDirectory() + "net_last.map";
    if (!FileCompressor::decompress(data + sizeof(NetPkgs::MessageHeader), netplay.mapfilepath))
        return;
}

void NetClient::sendGameModeSettingsChangeMessage()
{
    size_t data_size = sizeof(NetPkgs::MessageHeader) + sizeof(GameModeSettingsUnion);
    CompressedData blob(static_cast<uint8_t*>(calloc(data_size, 1)), data_size);

    NetPkgs::MessageHeader header(NET_NOTICE_GAMEMODESETTINGS);
    memcpy(blob.data, &header, sizeof(NetPkgs::MessageHeader));

    GameModeSettingsUnion modesettings;
    modesettings.read_global((GameModeType) currentgamemode);
    memcpy(blob.data + sizeof(NetPkgs::MessageHeader), &modesettings, sizeof(GameModeSettingsUnion));

    sendMessageToLobbyServer(blob.data, blob.size);
}

void NetClient::handleGameModeSettingsChangeMessage(const uint8_t* data, size_t dataLength)
{
    if (dataLength != sizeof(NetPkgs::MessageHeader) + sizeof(GameModeSettingsUnion)
        || !data) {
        printf("[error] Corrupt game mode settings arrived\n");
        return;
    }

    // read
    NetPkgs::MessageHeader pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::MessageHeader));

    GameModeSettingsUnion modesettings;
    memcpy(&modesettings, data + sizeof(NetPkgs::MessageHeader), sizeof(GameModeSettingsUnion));
    modesettings.set_global((GameModeType) currentgamemode);

    printf("[net] Game mode settings changed\n");
}

void NetClient::handleRoomChatMessage(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::RoomChatMsg), "RoomChatMsg"))
        return;

    NetPkgs::RoomChatMsg pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::RoomChatMsg));
    forceNulTerminated(pkg.message);

    assert(pkg.senderNum < 4);
    if (pkg.senderNum > 3)
        return;
    printf("Not implemented: [net] %s: %s\n",
        netplay.currentRoom.playerNames[pkg.senderNum].c_str(), pkg.message);
}

void NetClient::sendSkinChange()
{
    std::string skinpath = skinlist->at(game_values.skinids[netplay.remotePlayerNumber]).path.generic_string();
    assert(skinpath.length() > 4);
    printf("[net] Sending skin: %s\n", skinpath.c_str());

    // NOTE: see sendMapChangeMessage() for similar code
    static_assert(sizeof(NetPkgs::MessageHeader) == 3, "The size of Net_MessageHeader should be 3");

    CompressedData compressed = FileCompressor::compress(skinpath, sizeof(NetPkgs::MessageHeader) + 1);
    assert(compressed.data);
    assert(compressed.size > 1 + sizeof(NetPkgs::MessageHeader));
    if (!compressed.is_valid())
        return;
    if (compressed.size <= 1 + sizeof(NetPkgs::MessageHeader))
        return;

    NetPkgs::MessageHeader header(NET_NOTICE_SKIN_CHANGE);
    memcpy(compressed.data, &header, sizeof(NetPkgs::MessageHeader));
    compressed.data[sizeof(NetPkgs::MessageHeader)] = 0xFF; // the player's id in a room (0-3), or 0xFF

    sendMessageToLobbyServer(compressed.data, compressed.size);
}

void NetClient::handleSkinChangeMessage(const uint8_t* data, size_t dataLength)
{
    if (dataLength <= sizeof(NetPkgs::MessageHeader) + 5 /* player num 1B + un-/compressed size 2*2B */
        || dataLength > 20000
        || !data) {
        printf("[error] Corrupt skin arrived\n");
        return;
    }

    // read
    NetPkgs::MessageHeader pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::MessageHeader));

    int playerID = data[3];
    if (playerID > 3) {
        printf("[error] Corrupt skin arrived: bad player id\n");
        return;
    }

    uint16_t full_size, compressed_size;
    memcpy(&full_size, data + sizeof(NetPkgs::MessageHeader) + 1, 2);
    memcpy(&compressed_size, data + sizeof(NetPkgs::MessageHeader) + 3, 2);

    printf("[net] Skin arrived (from: %d, C:%d unC:%d)\n", playerID, compressed_size, full_size);

    std::ostringstream path;
    path << GetHomeDirectory() << "net_skin" << playerID << ".bmp";
    if (!FileCompressor::decompress(data + sizeof(NetPkgs::MessageHeader) + 1, path.str()))
        return;

    if (!rm->LoadMenuSkin(playerID, path.str(), playerID, false)) {
        printf("[warning] Could not load netplay skin of player %d, using default\n", playerID);
        rm->LoadMenuSkin(playerID, game_values.skinids[playerID], playerID, false);
    }
}

/****************************
    Phase 2.5: Pre-game
****************************/

// This package goes to normal players
void NetClient::handleRoomStartMessage(NetPeer& client, const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::GameHostInfo), "GameHostInfo"))
        return;

    NetPkgs::GameHostInfo pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::GameHostInfo));

    // If the server and a player is on the same machine,
    // then the player's address will be 127.0.0.1.
    // Use the server's address in this case.
    if (pkg.host == 0x100007F)
        pkg.host = client.addressHost();

    // pretty print
    uint8_t* host_bytes = reinterpret_cast<uint8_t*>(&pkg.host);
    char host_str[17];
    snprintf(host_str, sizeof(host_str), "%d.%d.%d.%d",
        host_bytes[0], host_bytes[1], host_bytes[2], host_bytes[3]);

    printf("[net] Connecting to game host... [%s:%d]\n", host_str, NET_GAMEHOST_PORT);
    if (!connectGameHost(host_str)) {
        printf("[net][error] Could not connect to game host.\n");
        return;
    }

    netplay.operationInProgress = true;
}

// This package goes to the game host player
void NetClient::handleExpectedClientsMessage(NetPeer& client, const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::PlayerInfo), "PlayerInfo"))
        return;

    NetPkgs::PlayerInfo pkg;
    memcpy(&pkg, data, sizeof(NetPkgs::PlayerInfo));

    uint8_t playerCount = 0;
    uint32_t hosts[3];
    uint16_t ports[3];

    printf("[net] Expecting the following clients:\n");
    for (uint8_t p = 0; p < 3; p++) {
        if (pkg.host[p]) { // this is 0 for empty player slots
            hosts[playerCount] = pkg.host[p];
            ports[playerCount] = pkg.port[p];

            // If the server and a player is on the same machine,
            // then the player's address will be 127.0.0.1.
            // Use the server's address in this case.
            if (hosts[playerCount] == 0x100007F)
                hosts[playerCount] = client.addressHost();

            uint8_t* host_bytes = reinterpret_cast<uint8_t*>(&pkg.host[p]);
            char host_str[17];
            snprintf(host_str, sizeof(host_str), "%d.%d.%d.%d",
                host_bytes[0], host_bytes[1], host_bytes[2], host_bytes[3]);

            printf("  Client %d is %s:%u\n", p, host_str, pkg.port[p]);
            playerCount++;
        }
    }

    local_gamehost.setExpectedPlayers(playerCount, hosts, ports);
}

void NetClient::handleStartSyncMessage(const uint8_t* data, size_t dataLength)
{
    if (!isPackageComplete(dataLength, sizeof(NetPkgs::StartSync), "StartSync"))
        return;

    // read
    NetPkgs::StartSync pkg(0);
    memcpy(&pkg, data, sizeof(NetPkgs::StartSync));

    // apply
    printf("reseed: %d\n", pkg.commonRandomSeed);
    RandomNumberGenerator::gameplay().seed(pkg.commonRandomSeed);

    std::fill_n(netplay.player_disconnected, 4, false);
    netplay.last_confirmed_input = 0xFF;
    netplay.current_input_counter = 0;
    std::fill_n(netplay.local_playerdata_store_time, 256, std::chrono::system_clock::now());

    // respond
    if (netplay.theHostIsMe)
        setAsLastSentMessage(NET_P2G_SYNC_OK);
    else {
        NetPkgs::SyncOK respond_pkg;
        sendMessageToGameHostReliable(&respond_pkg, sizeof(NetPkgs::SyncOK));
    }
}

void NetClient::handleGameStartMessage()
{
    printf("[net] Game start!\n");
    netplay.gameRunning = true;
}

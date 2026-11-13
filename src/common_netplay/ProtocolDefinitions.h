#ifndef NETWORK_PROTOCOL_DEFINITIONS_H
#define NETWORK_PROTOCOL_DEFINITIONS_H

constexpr int NET_PROTOCOL_VERSION_MAJOR = 0;
constexpr int NET_PROTOCOL_VERSION_MINOR = 5;
constexpr int NET_MAX_MESSAGE_SIZE = 128;
constexpr int NET_LOBBYSERVER_PORT = 12521;
constexpr int NET_GAMEHOST_PORT = 12522;
constexpr int NET_MAX_PLAYER_NAME_LENGTH = 16;
constexpr int NET_MAX_ROOM_NAME_LENGTH = 32;
constexpr int NET_MAX_ROOM_PASSWORD_LENGTH = 16;
constexpr int NET_MAX_CHAT_MSG_LENGTH = 100;
constexpr int NET_GAMESTATE_FRAMES_TO_SEND = 3;         // send gamestate every Nth frame only

constexpr int NET_REQUEST_SERVERINFO = 1;
constexpr int NET_RESPONSE_BADPROTOCOL = 2;
constexpr int NET_RESPONSE_SERVERINFO = 3;
constexpr int NET_RESPONSE_SERVER_MOTD = 4;             // Message of the Day
constexpr int NET_NOTICE_SERVER_STOPPED = 5;

constexpr int NET_REQUEST_CONNECT = 10;
constexpr int NET_REQUEST_LEAVE_SERVER = 11;
constexpr int NET_RESPONSE_CONNECT_OK = 12;
constexpr int NET_RESPONSE_CONNECT_DENIED = 13;
constexpr int NET_RESPONSE_CONNECT_SERVERFULL = 14;
constexpr int NET_RESPONSE_CONNECT_NAMETAKEN = 15;

constexpr int NET_REQUEST_ROOM_LIST = 20;
constexpr int NET_RESPONSE_NO_ROOMS = 21;
constexpr int NET_RESPONSE_ROOM_LIST_ENTRY = 22;

constexpr int NET_REQUEST_JOIN_ROOM = 30;
constexpr int NET_REQUEST_LEAVE_ROOM = 31;
constexpr int NET_RESPONSE_JOIN_OK = 32;
constexpr int NET_RESPONSE_ROOM_FULL = 33;
constexpr int NET_NOTICE_ROOM_CHANGE = 34;
constexpr int NET_NOTICE_MAP_CHANGE = 35;               // (after everyone connected) Game Host -> Players: "Here's the map file"
constexpr int NET_NOTICE_GAMEMODESETTINGS = 36;
constexpr int NET_NOTICE_SKIN_CHANGE = 37;
constexpr int NET_NOTICE_ROOM_CHAT_MSG = 38;

constexpr int NET_REQUEST_CREATE_ROOM = 40;
constexpr int NET_RESPONSE_CREATE_OK = 41;
constexpr int NET_RESPONSE_CREATE_ERROR = 42;           // TODO: What kind of error?

// L: lobby server
// G: game host player
// P: regular players

constexpr int NET_G2L_START_ROOM = 50;                  // Game Host -> Server: "OK, we're ready to go!"
constexpr int NET_L2P_GAMEHOST_INFO = 51;               // Server -> Normal Players: "Connect to this guy to play"
constexpr int NET_L2G_CLIENTS_INFO = 52;                // Server -> Game Host: "These players want to connect you"
constexpr int NET_G2P_SYNC = 55;                        // Game Host -> Players: "Prepare gameplay with this data (eg. RNG seed)"
constexpr int NET_P2G_SYNC_OK = 56;                     // Normal Player -> Game Host: "I'm ready!"
constexpr int NET_G2E_GAME_START = 57;                  // Game Host -> Everyone: "Everybody connected successfully!"
constexpr int NET_G2L_GAME_RESULTS = 58;                // GH -> Server

constexpr int NET_P2G_LEAVE_GAME = 70;                  // Normal players -> Game host
constexpr int NET_P2G_LOCAL_KEYS = 71;                  // Normal players -> Game host
constexpr int NET_G2P_REMOTE_KEYS = 72;                 // Game host -> Normal players
constexpr int NET_G2P_GAME_STATE = 73;                  // GH -> Players
constexpr int NET_P2G_REQ_POWERUP = 74;                 // Player wants to use powerup
constexpr int NET_G2P_START_POWERUP = 75;               // Another player wants to use powerup
constexpr int NET_G2P_TRIGGER_POWERUP = 76;             // Trigger powerup effect
constexpr int NET_G2P_TRIGGER_MAPCOLL = 77;             // Trigger map collision for clients
constexpr int NET_G2P_TRIGGER_P2PCOLL = 78;             // Trigger player-to-player collision for clients

#endif // NETWORK_PROTOCOL_DEFINITIONS_H

#ifndef PLAYER_H
#define PLAYER_H

#include <memory>
#include <string>

#include "Blob.h"
#include "Clock.h"
#include "NetworkLayer.h"

struct PlayerSkin: public Blob {
    PlayerSkin() : Blob() {}
    void setPlayerID(uint8_t id);
};

struct Player {
    std::string     name;
    std::unique_ptr<NetPeer> network_client;  // owned

    uint32_t        currentRoomID;
    bool            isPlaying;
    uint8_t         playerNumberInRoom;
    bool            synchOK;

    PlayerSkin      skinPackage;

    TimePoint       joinTime;
    TimePoint       lastActivityTime;

    Player();
    ~Player();

    // Owns its network connection, so a Player cannot be copied
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = default;
    Player& operator=(Player&&) = default;

    // Takes ownership of `client`
    void setClient(NetPeer* client);
    void setName(const std::string& name);
    void setSkin(const void*, size_t);

    bool sendData(const void*, size_t);
    bool sendCode(uint8_t);
    bool sendConnectOK();

    std::string toString();
    std::string address_to_string();
};

#endif // PLAYER_H

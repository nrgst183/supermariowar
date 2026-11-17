#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// SYSTEM LINK: FINDING THE GAMES HOSTED ON THE LOCAL NETWORK
//
// A host answers on the UDP port DISCOVERY_PORT. A joiner looking for games
// (the Online menu's System Link list) broadcasts a query to the local
// network every second and lists who answers: the host's name, its lobby
// port, how many are in, whether a match runs, and its build (only the same
// build can join); then (newer builds: an answer may end before) the mode, the
// map and how many players the next match has. Games that stop answering drop
// off the list.
//
// Any build with the network does both (Xboxes and PCs find each other). The
// sockets are ENet's; nothing here touches the game.
namespace rollback::lan {

constexpr uint16_t DISCOVERY_PORT = 7301;

// What a host tells about its game
struct HostInfo {
    uint16_t port = 0;          // the lobby's UDP port
    std::string name;           // the host player's name
    uint8_t members = 0;        // machines in the lobby
    uint8_t maxMembers = 0;
    bool inMatch = false;       // a match runs (joining waits for its end)
    std::string build;          // GIT_REVISION
    // (the details: empty / 0 in the answers of builds before them)
    std::string mode;           // the game mode the host has chosen ("Classic")
    std::string map;            // and the map
    uint8_t players = 0;        // the players of the next match, CPUs included
};

// A game found on the network
struct FoundGame {
    uint32_t ipv4 = 0;          // network byte order
    HostInfo info;
    uint32_t lastSeenMs = 0;
    int pingMs = -1;            // how long it took to answer (-1: not known)
    // "192.168.1.20:7300"
    std::string address() const;
};

// ---- the messages (the parts without the network: unit-tested)
std::vector<uint8_t> encodeQuery();
bool isQuery(const uint8_t* data, size_t size);
std::vector<uint8_t> encodeAnswer(const HostInfo& info);
bool decodeAnswer(const uint8_t* data, size_t size, HostInfo& info);
std::string ipv4Text(uint32_t ipv4);

// The host's side: answers the queries
class Responder {
public:
    ~Responder();
    // Listens on DISCOVERY_PORT (false if it can't: the game still runs, unseen)
    bool open();
    void close();
    bool isOpen() const { return m_socket != INVALID; }
    // Answers the queries that came (once per frame)
    void answer(const HostInfo& info);

private:
    static constexpr uintptr_t INVALID = ~uintptr_t(0);
    uintptr_t m_socket = INVALID;
};

// The joiner's side: asks, and keeps the list of the games that answer
class Browser {
public:
    ~Browser();
    bool open();
    void close();
    bool isOpen() const { return m_socket != INVALID; }
    // Sends a query every second, reads the answers, drops the games that went silent
    void update(uint32_t nowMs);
    // The games found, in the order they first answered
    const std::vector<FoundGame>& games() const { return m_games; }

private:
    static constexpr uintptr_t INVALID = ~uintptr_t(0);
    uintptr_t m_socket = INVALID;
    uint32_t m_lastQueryMs = 0;
    bool m_queried = false;
    std::vector<FoundGame> m_games;
};

} // namespace rollback::lan

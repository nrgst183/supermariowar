#pragma once

#include "NetProtocol.h"
#include "Transport.h"

#include <array>
#include <cstdint>
#include <string>

namespace rollback {

// THE CONNECTIONS OF A LOBBY: keeps every peer connected to every other peer
// (full mesh) while peers come and go. The host is only special here: it
// accepts the joiners, numbers them and tells each new one where the others
// are; during a match all peers are equal. What travels over the mesh is the
// business of the layers above (NetLobby, RollbackSession): every message
// that isn't one of the handshake messages below is handed out as is.
//
// Peer ids: the host is 0, a joiner gets the lowest free id of 1..3. An id is
// free again once its peer has left.
//
// Handshake, on every new connection, both ways:
//   HELLO  u8 type, u32 magic "SMWR", u16 protocol version, str build id
//          (git revision), u64 hash of the simulation data (tile types),
//          u8 sender's peer id (0xff: a joiner that has none yet)
// A peer that doesn't match closes the connection with
//   REJECT u8 type, str reason
// The host answers a new joiner's HELLO with its own, then
//   WELCOME u8 type, u8 your peer id,
//           u8 n, n x (u8 peer id, u32 ipv4, u16 port) of the other members
// The joiner connects to each of those and, connected to all of them, sends
//   MESH_READY u8 type
// and is a member from then on. The host lets one joiner in at a time (the
// others wait for their WELCOME), so each new joiner's list is complete.
// Existing members accept the new joiner's connection by its HELLO.
class NetMesh {
public:
    struct Options {
        bool host = false;
        std::string joinHost;        // joiner: where the host is
        uint16_t joinPort = 0;
        std::string buildId;
        uint64_t dataHash = 0;
        uint32_t joinTimeoutMs = 15000;   // joining (connecting to everyone) takes at most this long
    };

    enum class State : uint8_t {
        Connecting,    // joiner: not a member yet
        Ready,         // a member (the host always is)
        Failed,        // joiner: couldn't join, or lost the host (error())
    };

    struct Event {
        enum class Type : uint8_t {
            None,
            Joined,      // joiner: we are a member now
            PeerJoined,  // host: a joiner is a member now (peer)
            PeerLost,    // the connection to a peer is gone (peer)
            Message,     // a message from a peer (peer, data, size, channel)
        };
        Type type = Type::None;
        int peer = -1;
        Channel channel = Channel::Reliable;
        const uint8_t* data = nullptr;    // valid until the next poll()
        size_t size = 0;
    };

    NetMesh(IMeshTransport& transport, const Options& options, uint32_t nowMs);

    // Handles transport events; hands out the ones for the layers above.
    // Doesn't block.
    bool poll(Event& event, uint32_t nowMs);

    State state() const { return m_state; }
    const std::string& error() const { return m_error; }
    bool isHost() const { return m_options.host; }
    int localPeer() const { return m_localPeer; }
    int connectionOf(int peer) const { return peer >= 0 && peer < net::MAX_PEERS ? m_peerConnection[peer] : -1; }
    std::array<int, net::MAX_PEERS> peerConnections() const { return m_peerConnection; }
    bool peerConnected(int peer) const { return connectionOf(peer) >= 0; }
    // Host: a joiner that is connecting to the others right now (-1: none)
    int joiningPeer() const { return m_joiningPeer; }

    void send(int peer, Channel channel, const uint8_t* data, size_t size);
    // To every connected peer
    void sendToAll(Channel channel, const uint8_t* data, size_t size);
    // Closes the connection to a peer (after what is queued on it)
    void dropPeer(int peer);
    // Host: refuse new joiners with this reason (empty: accept them again)
    void setRefuseJoins(const std::string& reason) { m_refuseJoins = reason; }
    // Host: peer ids a new joiner doesn't get (bit p), while there are others: the peers of the match
    // being played, lost ones too (a lost peer's id stays lost for the rest of its match)
    void setReservedIds(uint8_t mask) { m_reservedIds = mask; }


    IMeshTransport& transport() { return m_transport; }

private:
    void onConnected(int connection);
    void onDisconnected(int connection, Event& event, bool& emit);
    void onMessage(int connection, Channel channel, const uint8_t* data, size_t size, Event& event, bool& emit);
    void onHello(int connection, net::ByteReader& r);
    void onWelcome(int connection, net::ByteReader& r);
    void admit(int connection, uint32_t nowMs);
    void admitNext(uint32_t nowMs);
    void sendHello(int connection);
    void reject(int connection, const std::string& reason);
    void checkJoinerReady();
    void fail(const std::string& error);
    int peerOf(int connection) const;

    IMeshTransport& m_transport;
    Options m_options;
    State m_state = State::Connecting;
    std::string m_error;
    uint32_t m_startMs = 0;
    uint32_t m_nowMs = 0;
    std::string m_refuseJoins;

    int m_localPeer = -1;
    // Host: the joiner being let in, since when, and the others that wait (connections)
    int m_joiningPeer = -1;
    uint32_t m_joiningSinceMs = 0;
    std::array<int, 8> m_waiting {{-1, -1, -1, -1, -1, -1, -1, -1}};
    // Joiner
    bool m_sentReady = false;
    int m_hostConnection = -1;
    std::array<int, net::MAX_PEERS> m_peerConnection = net::noConnections();
    std::array<int, net::MAX_PEERS> m_expectedConnection = net::noConnections();   // joiner: members it connects to
    uint8_t m_reservedIds = 0;    // host: ids not to give a new joiner (setReservedIds)
    std::array<uint8_t, 512> m_buffer {};
};

} // namespace rollback

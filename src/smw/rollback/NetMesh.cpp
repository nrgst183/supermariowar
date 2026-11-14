#include "NetMesh.h"

#include <algorithm>
#include <cstdio>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;
using net::MsgType;

namespace {
constexpr uint8_t NO_PEER_ID = 0xff;

std::string ipString(uint32_t ipv4, uint16_t port)
{
    //enet keeps the address in network byte order
    const auto* b = reinterpret_cast<const uint8_t*>(&ipv4);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]) + ":" + std::to_string(port);
}
} // namespace

NetMesh::NetMesh(IMeshTransport& transport, const Options& options, uint32_t nowMs)
    : m_transport(transport)
    , m_options(options)
    , m_startMs(nowMs)
    , m_nowMs(nowMs)
{
    if (m_options.host) {
        m_localPeer = 0;
        m_state = State::Ready;
        printf("netplay: hosting a lobby on port %u\n", m_transport.localPort());
    } else {
        m_hostConnection = m_transport.connect(m_options.joinHost, m_options.joinPort);
        if (m_hostConnection < 0)
            fail("could not connect to " + m_options.joinHost + ":" + std::to_string(m_options.joinPort));
        else
            printf("netplay: joining %s:%u from port %u\n", m_options.joinHost.c_str(), m_options.joinPort, m_transport.localPort());
    }
}

int NetMesh::peerOf(int connection) const
{
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (m_peerConnection[p] == connection && connection >= 0)
            return p;
    }
    return -1;
}

void NetMesh::fail(const std::string& error)
{
    if (m_state == State::Failed)
        return;
    //Not being able to join is an error; losing the host later is just the end
    if (m_state == State::Connecting)
        printf("netplay: ERROR: %s\n", error.c_str());
    else
        printf("netplay: %s\n", error.c_str());
    m_state = State::Failed;
    m_error = error;
}

void NetMesh::send(int peer, Channel channel, const uint8_t* data, size_t size)
{
    if (peer != m_localPeer && connectionOf(peer) >= 0)
        m_transport.send(m_peerConnection[peer], channel, data, size);
}

void NetMesh::sendToAll(Channel channel, const uint8_t* data, size_t size)
{
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (p != m_joiningPeer)
            send(p, channel, data, size);
    }
}

void NetMesh::dropPeer(int peer)
{
    if (peer == m_localPeer || connectionOf(peer) < 0)
        return;
    m_transport.disconnect(m_peerConnection[peer]);
    m_peerConnection[peer] = -1;
    if (peer == m_joiningPeer) {
        m_joiningPeer = -1;
        admitNext(m_nowMs);
    }
}

void NetMesh::sendHello(int connection)
{
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Hello));
    w.u32(net::PROTOCOL_MAGIC);
    w.u16(net::PROTOCOL_VERSION);
    w.str(m_options.buildId);
    w.u64(m_options.dataHash);
    w.u8(m_localPeer >= 0 ? static_cast<uint8_t>(m_localPeer) : NO_PEER_ID);
    m_transport.send(connection, Channel::Reliable, w.data(), w.size());
}

void NetMesh::reject(int connection, const std::string& reason)
{
    printf("netplay: refusing a connection: %s\n", reason.c_str());
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Reject));
    w.str(reason);
    m_transport.send(connection, Channel::Reliable, w.data(), w.size());
    m_transport.disconnect(connection);
}

void NetMesh::onConnected(int connection)
{
    //Whoever starts a connection introduces itself first
    if (connection == m_hostConnection) {
        sendHello(connection);
        return;
    }
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (m_expectedConnection[p] == connection) {
            sendHello(connection);
            return;
        }
    }
    //An incoming connection: wait for its HELLO
}

void NetMesh::admit(int connection, uint32_t nowMs)
{
    int id = -1;
    for (int p = 1; p < net::MAX_PEERS && id < 0; p++) {
        if (m_peerConnection[p] < 0)
            id = p;
    }
    if (id < 0) {
        reject(connection, "the lobby is full");
        return;
    }
    if (!m_refuseJoins.empty()) {
        reject(connection, m_refuseJoins);
        return;
    }

    m_peerConnection[id] = connection;
    m_joiningPeer = id;
    m_joiningSinceMs = nowMs;
    sendHello(connection);

    //Tell it where the other members are
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Welcome));
    w.u8(static_cast<uint8_t>(id));
    int count = 0;
    for (int p = 1; p < net::MAX_PEERS; p++)
        count += p != id && m_peerConnection[p] >= 0;
    w.u8(static_cast<uint8_t>(count));
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (p == id || m_peerConnection[p] < 0)
            continue;
        uint32_t ip = 0;
        uint16_t port = 0;
        m_transport.address(m_peerConnection[p], ip, port);
        w.u8(static_cast<uint8_t>(p));
        w.u32(ip);
        w.u16(port);
    }
    m_transport.send(connection, Channel::Reliable, w.data(), w.size());

    uint32_t ip = 0;
    uint16_t port = 0;
    m_transport.address(connection, ip, port);
    printf("netplay: peer %d is joining from %s\n", id, ipString(ip, port).c_str());
}

void NetMesh::admitNext(uint32_t nowMs)
{
    if (!m_options.host || m_joiningPeer >= 0)
        return;
    for (int& connection : m_waiting) {
        if (connection >= 0) {
            const int next = connection;
            connection = -1;
            admit(next, nowMs);
            if (m_joiningPeer >= 0)
                return;
        }
    }
}

void NetMesh::onHello(int connection, ByteReader& r)
{
    const uint32_t magic = r.u32();
    const uint16_t version = r.u16();
    const std::string build = r.str();
    const uint64_t dataHash = r.u64();
    const uint8_t peerId = r.u8();
    if (!r.ok() || magic != net::PROTOCOL_MAGIC) {
        reject(connection, "not a Super Mario War rollback peer");
        return;
    }
    if (version != net::PROTOCOL_VERSION) {
        reject(connection, "protocol version " + std::to_string(version) + ", expected " + std::to_string(net::PROTOCOL_VERSION));
        return;
    }
    if (build != m_options.buildId) {
        reject(connection, "different game version: " + build + ", this is " + m_options.buildId);
        return;
    }
    if (dataHash != m_options.dataHash) {
        reject(connection, "different game data (tile types), e.g. another graphics pack");
        return;
    }

    if (m_options.host) {
        if (peerOf(connection) >= 0)
            return;
        if (peerId != NO_PEER_ID) {
            reject(connection, "unexpected peer id");
            return;
        }
        if (!m_refuseJoins.empty()) {
            reject(connection, m_refuseJoins);
            return;
        }
        if (m_joiningPeer < 0) {
            admit(connection, m_nowMs);
            return;
        }
        //Someone else is joining right now: this one waits for its turn
        for (int& waiting : m_waiting) {
            if (waiting < 0) {
                waiting = connection;
                return;
            }
        }
        reject(connection, "too many players are joining at once, try again");
        return;
    }

    //Joiner: the host's answer, or another member's
    if (connection == m_hostConnection) {
        if (peerId != 0) {
            fail("the host sent a wrong peer id");
            return;
        }
        m_peerConnection[0] = connection;
    } else if (peerId > 0 && peerId < net::MAX_PEERS && peerId != m_localPeer && m_localPeer >= 0) {
        if (m_expectedConnection[peerId] == connection) {
            m_peerConnection[peerId] = connection;    //the answer of a member we connected to
        } else {
            //A new joiner connects to us. A connection we still have to that
            //id belongs to a peer that left (its id was given out again).
            if (m_peerConnection[peerId] >= 0 && m_peerConnection[peerId] != connection)
                m_transport.disconnect(m_peerConnection[peerId]);
            m_peerConnection[peerId] = connection;
            sendHello(connection);
        }
        printf("netplay: connected to peer %d\n", peerId);
    } else {
        reject(connection, "unexpected peer id");
        return;
    }
    checkJoinerReady();
}

void NetMesh::onWelcome(int connection, ByteReader& r)
{
    if (connection != m_hostConnection || m_localPeer >= 0)
        return;

    m_localPeer = r.u8();
    const int count = r.u8();
    if (!r.ok() || m_localPeer <= 0 || m_localPeer >= net::MAX_PEERS) {
        fail("bad welcome message");
        return;
    }
    printf("netplay: joining as peer %d\n", m_localPeer);

    for (int i = 0; i < count; i++) {
        const int peer = r.u8();
        const uint32_t ip = r.u32();
        const uint16_t port = r.u16();
        if (!r.ok() || peer <= 0 || peer >= net::MAX_PEERS || peer == m_localPeer)
            continue;
        m_expectedConnection[peer] = m_transport.connect(ip, port);
        if (m_expectedConnection[peer] < 0)
            fail("could not connect to peer " + std::to_string(peer) + " at " + ipString(ip, port));
    }
    checkJoinerReady();
}

void NetMesh::checkJoinerReady()
{
    if (m_options.host || m_sentReady || m_localPeer < 0 || m_peerConnection[0] < 0 || m_state == State::Failed)
        return;
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (m_expectedConnection[p] >= 0 && m_peerConnection[p] < 0)
            return;
    }

    m_sentReady = true;
    m_state = State::Ready;
    const uint8_t msg = static_cast<uint8_t>(MsgType::MeshReady);
    m_transport.send(m_hostConnection, Channel::Reliable, &msg, 1);
}

void NetMesh::onDisconnected(int connection, Event& event, bool& emit)
{
    for (int& waiting : m_waiting) {
        if (waiting == connection)
            waiting = -1;
    }

    const int peer = peerOf(connection);
    if (peer < 0) {
        if (m_state == State::Connecting) {
            if (connection == m_hostConnection)
                fail("could not reach the host");
            for (int p = 1; p < net::MAX_PEERS; p++) {
                if (m_expectedConnection[p] == connection)
                    fail("could not connect to peer " + std::to_string(p));
            }
        }
        return;
    }
    m_peerConnection[peer] = -1;

    if (m_options.host && peer == m_joiningPeer) {
        printf("netplay: peer %d didn't finish joining\n", peer);
        m_joiningPeer = -1;
        admitNext(m_nowMs);
        return;
    }
    if (m_state == State::Connecting) {
        fail("peer " + std::to_string(peer) + " disconnected while joining");
        return;
    }
    printf("netplay: peer %d disconnected\n", peer);
    if (peer == 0)
        fail("lost the connection to the host");
    event = Event();
    event.type = Event::Type::PeerLost;
    event.peer = peer;
    emit = true;
}

void NetMesh::onMessage(int connection, Channel channel, const uint8_t* data, size_t size, Event& event, bool& emit)
{
    if (size == 0)
        return;
    ByteReader r(data + 1, size - 1);
    const auto type = static_cast<MsgType>(data[0]);

    if (type == MsgType::Hello) {
        onHello(connection, r);
        return;
    }
    if (type == MsgType::Reject) {
        if (m_state == State::Connecting)
            fail("refused: " + r.str());
        return;
    }

    //Everything else only comes from known peers
    const int peer = peerOf(connection);
    if (peer < 0)
        return;

    if (type == MsgType::Welcome) {
        onWelcome(connection, r);
        return;
    }
    if (type == MsgType::MeshReady) {
        if (m_options.host && peer == m_joiningPeer) {
            m_joiningPeer = -1;
            printf("netplay: peer %d joined\n", peer);
            event = Event();
            event.type = Event::Type::PeerJoined;
            event.peer = peer;
            emit = true;
            admitNext(m_nowMs);
        }
        return;
    }

    event = Event();
    event.type = Event::Type::Message;
    event.peer = peer;
    event.channel = channel;
    event.data = data;
    event.size = size;
    emit = true;
}

bool NetMesh::poll(Event& event, uint32_t nowMs)
{
    m_nowMs = nowMs;
    if (m_state == State::Connecting && static_cast<int32_t>(nowMs - m_startMs) > static_cast<int32_t>(m_options.joinTimeoutMs))
        fail("timed out while joining");
    if (m_options.host && m_joiningPeer >= 0 && static_cast<int32_t>(nowMs - m_joiningSinceMs) > static_cast<int32_t>(m_options.joinTimeoutMs)) {
        printf("netplay: peer %d took too long to join\n", m_joiningPeer);
        dropPeer(m_joiningPeer);
    }

    TransportEvent te;
    while (m_transport.poll(te)) {
        bool emit = false;
        const bool wasReady = m_state == State::Ready;
        switch (te.type) {
            case TransportEvent::Type::Connected:
                onConnected(te.connection);
                break;
            case TransportEvent::Type::Disconnected:
                onDisconnected(te.connection, event, emit);
                break;
            case TransportEvent::Type::Received:
                onMessage(te.connection, te.channel, te.data, te.size, event, emit);
                break;
            default:
                break;
        }
        if (emit)
            return true;
        if (!m_options.host && !wasReady && m_state == State::Ready) {
            printf("netplay: joined as peer %d\n", m_localPeer);
            event = Event();
            event.type = Event::Type::Joined;
            event.peer = m_localPeer;
            return true;
        }
    }
    return false;
}

} // namespace rollback

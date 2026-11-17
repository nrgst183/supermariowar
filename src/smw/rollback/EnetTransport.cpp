#include "EnetTransport.h"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace rollback {

namespace {
constexpr size_t CHANNEL_COUNT = 2;

bool initEnet()
{
    static bool initialized = false;
    if (!initialized)
        initialized = enet_initialize() == 0;
    return initialized;
}

int connectionOf(const ENetPeer* peer)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(peer->data)) - 1;
}
} // namespace

std::string EnetTransport::localAddress()
{
    if (!initEnet())
        return {};
    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL)
        return {};

    //"Connecting" a UDP socket sends nothing: the system only picks the route, and with it the
    //address of the interface the packets would leave from
    ENetAddress remote;
    remote.host = ENET_HOST_TO_NET_32(0x08080808);    //(any address outside the network)
    remote.port = 53;
    ENetAddress local {};
    char text[64] = {};
    const bool found = enet_socket_connect(socket, &remote) == 0 && enet_socket_get_address(socket, &local) == 0
        && local.host != ENET_HOST_ANY && enet_address_get_host_ip(&local, text, sizeof(text)) == 0;
    enet_socket_destroy(socket);
    if (!found || std::string(text).rfind("127.", 0) == 0)
        return {};
    return text;
}

std::unique_ptr<EnetTransport> EnetTransport::create(uint16_t port, const NetConditions& conditions, std::string& error)
{
    if (!initEnet()) {
        error = "could not initialize enet";
        return nullptr;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    std::unique_ptr<EnetTransport> transport(new EnetTransport());
    transport->m_host = enet_host_create(&address, MAX_CONNECTIONS, CHANNEL_COUNT, 0, 0);
    if (!transport->m_host) {
        error = "could not open UDP port " + std::to_string(port);
        return nullptr;
    }

    if (conditions.active())
        transport->m_sim = std::make_unique<NetConditionSim>(conditions);
    return transport;
}

EnetTransport::~EnetTransport()
{
    if (m_received)
        enet_packet_destroy(m_received);

    if (m_host) {
        for (ENetPeer* peer : m_peers) {
            if (peer)
                enet_peer_disconnect_now(peer, 0);
        }
        enet_host_flush(m_host);
        enet_host_destroy(m_host);
    }
}

int EnetTransport::addPeer(ENetPeer* peer)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!m_peers[i]) {
            m_peers[i] = peer;
            peer->data = reinterpret_cast<void*>(static_cast<intptr_t>(i + 1));
            //Notice a dead connection within seconds, not the default 30
            enet_peer_timeout(peer, 0, 3000, 8000);
            return i;
        }
    }
    return -1;
}

int EnetTransport::connect(uint32_t ipv4, uint16_t port)
{
    ENetAddress address;
    address.host = ipv4;
    address.port = port;
    ENetPeer* peer = enet_host_connect(m_host, &address, CHANNEL_COUNT, 0);
    if (!peer)
        return -1;

    const int connection = addPeer(peer);
    if (connection < 0)
        enet_peer_reset(peer);
    return connection;
}

int EnetTransport::connect(const std::string& host, uint16_t port)
{
    ENetAddress address;
    if (enet_address_set_host(&address, host.c_str()) != 0)
        return -1;
    return connect(address.host, port);
}

bool EnetTransport::address(int connection, uint32_t& ipv4, uint16_t& port) const
{
    if (connection < 0 || connection >= MAX_CONNECTIONS || !m_peers[connection])
        return false;
    ipv4 = m_peers[connection]->address.host;
    port = m_peers[connection]->address.port;
    return true;
}

uint16_t EnetTransport::localPort() const
{
    ENetAddress address;
    if (enet_socket_get_address(m_host->socket, &address) == 0)
        return address.port;
    return m_host->address.port;
}

void EnetTransport::sendNow(int connection, Channel channel, const uint8_t* data, size_t size)
{
    if (connection < 0 || connection >= MAX_CONNECTIONS || !m_peers[connection])
        return;

    const enet_uint32 flags = channel == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* packet = enet_packet_create(data, size, flags);
    if (packet && enet_peer_send(m_peers[connection], static_cast<enet_uint8>(channel), packet) != 0)
        enet_packet_destroy(packet);
}

void EnetTransport::send(int connection, Channel channel, const uint8_t* data, size_t size)
{
    if (m_sim && m_sim->push(monotonicMs(), connection, channel, data, size))
        return;
    sendNow(connection, channel, data, size);
}

void EnetTransport::flush()
{
    if (m_sim) {
        m_sim->release(monotonicMs(), [this](int connection, Channel channel, const uint8_t* data, size_t size) { sendNow(connection, channel, data, size); });
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (m_disconnectLater[i] && !m_sim->pendingReliable(i)) {
                m_disconnectLater[i] = false;
                if (m_peers[i])
                    enet_peer_disconnect_later(m_peers[i], 0);
            }
        }
    }
    enet_host_flush(m_host);
}

bool EnetTransport::poll(TransportEvent& event)
{
    if (m_received) {
        enet_packet_destroy(m_received);
        m_received = nullptr;
    }

    //Delayed packets that are due go out first
    if (m_sim)
        m_sim->release(monotonicMs(), [this](int connection, Channel channel, const uint8_t* data, size_t size) { sendNow(connection, channel, data, size); });

    ENetEvent ev;
    while (enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                int connection = connectionOf(ev.peer);
                if (connection < 0)
                    connection = addPeer(ev.peer);    //an incoming connection
                if (connection < 0) {
                    enet_peer_reset(ev.peer);
                    continue;
                }
                //No packet throttle: ENet drops a share of the unreliable packets while the round trip
                //time jitters (its congestion control), and those are the inputs, a few bytes each:
                //every dropped one is a late input (a rollback or a wait) exactly when the connection
                //is bad. (Once connected: a command queued while connecting breaks the handshake.)
                enet_peer_throttle_configure(ev.peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_SCALE, 0);
                event = TransportEvent();
                event.type = TransportEvent::Type::Connected;
                event.connection = connection;
                return true;
            }

            case ENET_EVENT_TYPE_DISCONNECT: {
                const int connection = connectionOf(ev.peer);
                if (connection < 0)
                    continue;
                m_peers[connection] = nullptr;
                m_disconnectLater[connection] = false;
                ev.peer->data = nullptr;
                event = TransportEvent();
                event.type = TransportEvent::Type::Disconnected;
                event.connection = connection;
                return true;
            }

            case ENET_EVENT_TYPE_RECEIVE: {
                const int connection = connectionOf(ev.peer);
                if (connection < 0) {
                    enet_packet_destroy(ev.packet);
                    continue;
                }
                m_received = ev.packet;
                event = TransportEvent();
                event.type = TransportEvent::Type::Received;
                event.connection = connection;
                event.channel = ev.channelID == 0 ? Channel::Unreliable : Channel::Reliable;
                event.data = ev.packet->data;
                event.size = ev.packet->dataLength;
                return true;
            }

            default:
                break;
        }
    }
    return false;
}

void EnetTransport::close(uint32_t timeoutMs)
{
    const uint32_t start = monotonicMs();
    TransportEvent event;
    auto service = [&] {
        flush();
        while (poll(event)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };

    //What the condition simulator still holds back goes out first
    while (!idle() && monotonicMs() - start < timeoutMs)
        service();

    for (ENetPeer* peer : m_peers) {
        if (peer)
            enet_peer_disconnect_later(peer, 0);
    }
    while (monotonicMs() - start < timeoutMs && std::any_of(m_peers.begin(), m_peers.end(), [](const ENetPeer* p) { return p != nullptr; }))
        service();
}

void EnetTransport::disconnect(int connection)
{
    if (connection < 0 || connection >= MAX_CONNECTIONS || !m_peers[connection])
        return;
    //A goodbye still held back by the condition simulator goes out first
    if (m_sim && m_sim->pendingReliable(connection)) {
        m_disconnectLater[connection] = true;
        return;
    }
    enet_peer_disconnect_later(m_peers[connection], 0);
}

} // namespace rollback

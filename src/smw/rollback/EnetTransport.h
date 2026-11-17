#pragma once

#include "NetConditionSim.h"
#include "Transport.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
typedef struct _ENetPacket ENetPacket;

// ITransport over UDP with enet: one enet host (one socket) per process,
// used both to accept connections and to make them, so the address another
// peer sees for us is also the one it can connect to (full mesh).
//
// Channel 0 sends unsequenced packets, channel 1 reliable ones. With
// network conditions given, outgoing packets go through NetConditionSim.
namespace rollback {

class EnetTransport : public IMeshTransport {
public:
    static constexpr int MAX_CONNECTIONS = 8;

    // Binds to `port` on all interfaces (0: any free port). Null on failure.
    static std::unique_ptr<EnetTransport> create(uint16_t port, const NetConditions& conditions, std::string& error);
    // This computer's IPv4 address on its network (the one its traffic leaves from), e.g.
    // "192.168.1.20"; empty if there's none (no network)
    static std::string localAddress();
    ~EnetTransport() override;

    // Starts connecting; the Connected event follows. -1 on failure.
    int connect(const std::string& host, uint16_t port) override;
    int connect(uint32_t ipv4, uint16_t port) override;    // ipv4 as enet stores it (network order)
    // The remote address of a connection
    bool address(int connection, uint32_t& ipv4, uint16_t& port) const override;
    uint16_t localPort() const override;

    void send(int connection, Channel channel, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& event) override;
    void flush() override;
    void disconnect(int connection) override;

    const NetConditionSim* conditionSim() const { return m_sim.get(); }
    bool idle() const override { return !m_sim || m_sim->pending() == 0; }
    void close(uint32_t timeoutMs) override;

private:
    EnetTransport() = default;

    void sendNow(int connection, Channel channel, const uint8_t* data, size_t size);
    int addPeer(ENetPeer* peer);

    ENetHost* m_host = nullptr;
    std::array<ENetPeer*, MAX_CONNECTIONS> m_peers {};
    ENetPacket* m_received = nullptr;    // the packet of the last Received event
    // Connections to close once the condition simulator has sent what it holds for them
    std::array<bool, MAX_CONNECTIONS> m_disconnectLater {};
    std::unique_ptr<NetConditionSim> m_sim;
};

} // namespace rollback

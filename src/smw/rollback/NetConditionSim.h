#pragma once

#include "Transport.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Network condition simulator, for testing netplay on one machine: delays
// outgoing packets by a latency with random jitter, and drops a percentage
// of the unreliable ones (reliable packets are never dropped: the real
// network would make the transport resend them, which looks like extra
// latency). Each peer simulates its own sending direction, so the round
// trip gets the latency twice.
//
// Only active when one of the --net-sim-* options is given; otherwise the
// transport sends directly and this class isn't even created.
namespace rollback {

struct NetConditions {
    int latencyMs = 0;       // one way
    int jitterMs = 0;        // +- around the latency
    float lossPercent = 0;   // unreliable packets only
    uint32_t seed = 1;

    bool active() const { return latencyMs > 0 || jitterMs > 0 || lossPercent > 0; }
};

class NetConditionSim {
public:
    static constexpr size_t MAX_PACKET = 2048;

    explicit NetConditionSim(const NetConditions& conditions);

    // Takes a packet to send later. Returns false if it doesn't fit (too big,
    // or too many in flight): the caller sends it directly.
    bool push(uint32_t nowMs, int connection, Channel channel, const uint8_t* data, size_t size);

    // Calls deliver(connection, channel, data, size) for each packet that is due
    template <typename Deliver>
    void release(uint32_t nowMs, Deliver&& deliver)
    {
        for (Packet& packet : m_unreliable) {
            if (packet.used && static_cast<int32_t>(nowMs - packet.due) >= 0) {
                deliver(packet.connection, Channel::Unreliable, packet.data, packet.size);
                packet.used = false;
                m_unreliableCount--;
            }
        }
        while (m_reliableCount > 0) {
            Packet& packet = m_reliable[m_reliableHead];
            if (static_cast<int32_t>(nowMs - packet.due) < 0)
                break;
            deliver(packet.connection, Channel::Reliable, packet.data, packet.size);
            packet.used = false;
            m_reliableHead = (m_reliableHead + 1) % m_reliable.size();
            m_reliableCount--;
        }
    }

    size_t pending() const { return m_unreliableCount + m_reliableCount; }
    // Reliable packets still held back for this connection
    bool pendingReliable(int connection) const;

    struct Stats {
        int64_t delayed = 0;
        int64_t dropped = 0;
    };
    const Stats& stats() const { return m_stats; }
    const NetConditions& conditions() const { return m_conditions; }

private:
    struct Packet {
        uint32_t due = 0;
        int connection = -1;
        uint16_t size = 0;
        bool used = false;
        uint8_t data[MAX_PACKET];
    };

    uint32_t random();
    uint32_t delay();

    NetConditions m_conditions;
    uint32_t m_rng;
    // Unreliable packets may overtake each other (jitter reorders them)
    std::vector<Packet> m_unreliable;
    size_t m_unreliableCount = 0;
    // Reliable packets stay in order: a queue whose due times never decrease
    std::vector<Packet> m_reliable;
    size_t m_reliableHead = 0;
    size_t m_reliableCount = 0;
    uint32_t m_lastReliableDue = 0;
    Stats m_stats;
};

} // namespace rollback

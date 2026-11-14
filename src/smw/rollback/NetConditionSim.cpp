#include "NetConditionSim.h"

#include <algorithm>
#include <cstring>

namespace rollback {

namespace {
constexpr size_t UNRELIABLE_SLOTS = 512;
constexpr size_t RELIABLE_SLOTS = 256;
} // namespace

NetConditionSim::NetConditionSim(const NetConditions& conditions)
    : m_conditions(conditions)
    , m_rng(conditions.seed ? conditions.seed : 1)
    , m_unreliable(UNRELIABLE_SLOTS)
    , m_reliable(RELIABLE_SLOTS)
{}

uint32_t NetConditionSim::random()
{
    //xorshift32
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}

uint32_t NetConditionSim::delay()
{
    int ms = m_conditions.latencyMs;
    if (m_conditions.jitterMs > 0)
        ms += static_cast<int>(random() % static_cast<uint32_t>(2 * m_conditions.jitterMs + 1)) - m_conditions.jitterMs;
    return static_cast<uint32_t>(std::max(0, ms));
}

bool NetConditionSim::push(uint32_t nowMs, int connection, Channel channel, const uint8_t* data, size_t size)
{
    if (size > MAX_PACKET)
        return false;

    if (channel == Channel::Unreliable) {
        if (m_conditions.lossPercent > 0 && static_cast<float>(random() % 10000) < m_conditions.lossPercent * 100.0f) {
            m_stats.dropped++;
            return true;
        }

        if (m_unreliableCount == m_unreliable.size())
            return false;
        auto it = std::find_if(m_unreliable.begin(), m_unreliable.end(), [](const Packet& p) { return !p.used; });
        it->due = nowMs + delay();
        it->connection = connection;
        it->size = static_cast<uint16_t>(size);
        it->used = true;
        std::memcpy(it->data, data, size);
        m_unreliableCount++;
    } else {
        if (m_reliableCount == m_reliable.size())
            return false;

        //Never before an earlier reliable packet: they must stay in order
        uint32_t due = nowMs + delay();
        if (m_reliableCount > 0 && static_cast<int32_t>(due - m_lastReliableDue) < 0)
            due = m_lastReliableDue;
        m_lastReliableDue = due;

        Packet& packet = m_reliable[(m_reliableHead + m_reliableCount) % m_reliable.size()];
        packet.due = due;
        packet.connection = connection;
        packet.size = static_cast<uint16_t>(size);
        packet.used = true;
        std::memcpy(packet.data, data, size);
        m_reliableCount++;
    }

    m_stats.delayed++;
    return true;
}

bool NetConditionSim::pendingReliable(int connection) const
{
    for (size_t i = 0; i < m_reliableCount; i++) {
        if (m_reliable[(m_reliableHead + i) % m_reliable.size()].connection == connection)
            return true;
    }
    return false;
}

} // namespace rollback

#include "NetSeries.h"

#include <algorithm>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;
using net::MsgType;

const char* seriesScreenName(SeriesScreen screen)
{
    switch (screen) {
        case SeriesScreen::None: return "none";
        case SeriesScreen::Choosing: return "choosing";
        case SeriesScreen::Scoreboard: return "scoreboard";
        case SeriesScreen::BonusWheel: return "bonus wheel";
        case SeriesScreen::TourStop: return "tour stop";
        case SeriesScreen::World: return "world";
    }
    return "?";
}

// ---- messages

void writeSeriesBegin(ByteWriter& w, const SeriesBeginMsg& m)
{
    w.u8(static_cast<uint8_t>(MsgType::SeriesBegin));
    w.u8(m.matchType);
    w.i16(m.tournamentGames);
    w.str(m.file);
    w.u64(m.fileHash);
    w.u32(m.seed);
    for (int s = 0; s < SERIES_SLOTS; s++) {
        w.i8(m.slotPeer[static_cast<size_t>(s)]);
        w.u8(m.control[static_cast<size_t>(s)]);
        w.i16(m.color[static_cast<size_t>(s)]);
    }
    w.i16(m.teamCount);
    for (const auto& team : m.teamIds) {
        for (int16_t id : team)
            w.i16(id);
    }
    for (int16_t count : m.teamCounts)
        w.i16(count);
    w.u16(static_cast<uint16_t>(m.settings.size()));
    w.bytes(m.settings.data(), m.settings.size());
}

bool readSeriesBegin(ByteReader& r, SeriesBeginMsg& m)
{
    m.matchType = r.u8();
    m.tournamentGames = r.i16();
    m.file = r.str();
    m.fileHash = r.u64();
    m.seed = r.u32();
    for (int s = 0; s < SERIES_SLOTS; s++) {
        m.slotPeer[static_cast<size_t>(s)] = r.i8();
        m.control[static_cast<size_t>(s)] = r.u8();
        m.color[static_cast<size_t>(s)] = r.i16();
    }
    m.teamCount = r.i16();
    for (auto& team : m.teamIds) {
        for (int16_t& id : team)
            id = r.i16();
    }
    for (int16_t& count : m.teamCounts)
        count = r.i16();
    const uint16_t size = r.u16();
    const uint8_t* bytes = r.bytes(size);
    if (!r.ok() || r.remaining() != 0)
        return false;
    m.settings.assign(bytes, bytes + size);
    if (m.teamCount < 1 || m.teamCount > 4)
        return false;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        const int peer = m.slotPeer[static_cast<size_t>(s)];
        if (peer < -1 || peer >= net::MAX_PEERS || m.control[static_cast<size_t>(s)] > 2)
            return false;
    }
    for (int t = 0; t < 4; t++) {
        if (m.teamCounts[t] < 0 || m.teamCounts[t] > 3)
            return false;
        for (int k = 0; k < m.teamCounts[t]; k++) {
            if (m.teamIds[t][k] < 0 || m.teamIds[t][k] >= SERIES_SLOTS)
                return false;
        }
    }
    return true;
}

void writeSeriesScreen(ByteWriter& w, const SeriesScreenMsg& m)
{
    w.u8(static_cast<uint8_t>(MsgType::SeriesScreen));
    w.u8(static_cast<uint8_t>(m.screen));
    w.u16(m.step);
    w.u8(m.humanMask);
}

bool readSeriesScreen(ByteReader& r, SeriesScreenMsg& m)
{
    const uint8_t screen = r.u8();
    m.step = r.u16();
    m.humanMask = r.u8();
    m.screen = static_cast<SeriesScreen>(screen);
    return r.ok() && r.remaining() == 0 && screen <= static_cast<uint8_t>(SeriesScreen::World);
}

void writeSeriesEnd(ByteWriter& w, const std::string& reason)
{
    w.u8(static_cast<uint8_t>(MsgType::SeriesEnd));
    w.str(reason);
}

bool readSeriesEnd(ByteReader& r, std::string& reason)
{
    reason = r.str();
    return r.ok() && r.remaining() == 0;
}

void writeWheelPress(ByteWriter& w, uint16_t step)
{
    w.u8(static_cast<uint8_t>(MsgType::WheelPress));
    w.u16(step);
}

bool readWheelPress(ByteReader& r, uint16_t& step)
{
    step = r.u16();
    return r.ok() && r.remaining() == 0;
}

void writeWheelStop(ByteWriter& w, const WheelStopMsg& m)
{
    w.u8(static_cast<uint8_t>(MsgType::WheelStop));
    w.u16(m.step);
    w.f32(m.angle);
    w.f32(m.speed);
    w.f32(m.winddown);
    w.i16(m.powerup);
}

bool readWheelStop(ByteReader& r, WheelStopMsg& m)
{
    m.step = r.u16();
    m.angle = r.f32();
    m.speed = r.f32();
    m.winddown = r.f32();
    m.powerup = r.i16();
    return r.ok() && r.remaining() == 0;
}

void writeWorldInput(ByteWriter& w, const WorldInputMsg& m)
{
    w.u8(static_cast<uint8_t>(MsgType::WorldInput));
    w.u16(m.epoch);
    w.u32(m.tick);
    w.u8(m.slotMask);
    for (uint8_t k : m.keys)
        w.u8(k);
}

bool readWorldInput(ByteReader& r, WorldInputMsg& m)
{
    m.epoch = r.u16();
    m.tick = r.u32();
    m.slotMask = r.u8();
    for (uint8_t& k : m.keys)
        k = r.u8();
    return r.ok() && r.remaining() == 0;
}

void writeWorldTick(ByteWriter& w, const WorldTickMsg& m)
{
    w.u8(static_cast<uint8_t>(MsgType::WorldTick));
    w.u16(m.epoch);
    w.u32(m.tick);
    w.u8(m.humanMask);
    for (uint8_t k : m.keys)
        w.u8(k);
}

bool readWorldTick(ByteReader& r, WorldTickMsg& m)
{
    m.epoch = r.u16();
    m.tick = r.u32();
    m.humanMask = r.u8();
    for (uint8_t& k : m.keys)
        k = r.u8();
    return r.ok() && r.remaining() == 0;
}

// ---- the players

uint8_t SeriesSlots::slotsOf(int peerId) const
{
    uint8_t mask = 0;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (control[static_cast<size_t>(s)] == 1 && peer[static_cast<size_t>(s)] == peerId)
            mask |= static_cast<uint8_t>(1u << s);
    }
    return mask;
}

uint8_t SeriesSlots::humanMask() const
{
    uint8_t mask = 0;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (control[static_cast<size_t>(s)] == 1 && peer[static_cast<size_t>(s)] >= 0)
            mask |= static_cast<uint8_t>(1u << s);
    }
    return mask;
}

bool SeriesSlots::dropPeer(int peerId)
{
    bool had = false;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (control[static_cast<size_t>(s)] == 1 && peer[static_cast<size_t>(s)] == peerId) {
            control[static_cast<size_t>(s)] = 2;
            peer[static_cast<size_t>(s)] = -1;
            had = true;
        }
    }
    return had;
}

bool SeriesSlots::peerInTeam(int peerId, int team) const
{
    if (team < 0 || team >= 4)
        return false;
    for (int k = 0; k < teamCounts[team] && k < 3; k++) {
        const int s = teamIds[team][k];
        if (s >= 0 && s < SERIES_SLOTS && control[static_cast<size_t>(s)] == 1 && peer[static_cast<size_t>(s)] == peerId)
            return true;
    }
    return false;
}

bool SeriesSlots::teamHasHuman(int team) const
{
    if (team < 0 || team >= 4)
        return false;
    for (int k = 0; k < teamCounts[team] && k < 3; k++) {
        const int s = teamIds[team][k];
        if (s >= 0 && s < SERIES_SLOTS && control[static_cast<size_t>(s)] == 1 && peer[static_cast<size_t>(s)] >= 0)
            return true;
    }
    return false;
}

// ---- the world map lockstep

void WorldLockstep::begin(uint16_t epoch, bool host, int localPeer, const SeriesSlots& slots, int window)
{
    m_active = true;
    m_host = host;
    m_localPeer = localPeer;
    m_epoch = epoch;
    m_window = std::max(1, window);
    m_slots = slots;
    m_humanMask = slots.humanMask();
    m_nextTick = 0;
    m_added = 0;
    m_held = {};
    for (auto& queue : m_inputs)
        queue.clear();
    m_ticks.clear();
    m_received = 0;
}

bool WorldLockstep::addLocal(const std::array<uint8_t, SERIES_SLOTS>& keys, WorldInputMsg* toHost)
{
    if (!m_active)
        return false;
    const uint8_t mine = static_cast<uint8_t>(m_slots.slotsOf(m_localPeer) & m_humanMask);
    if (!mine)
        return false;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (mine & (1u << s))
            m_held[static_cast<size_t>(s)] |= keys[static_cast<size_t>(s)];
    }
    if (m_added - m_nextTick >= static_cast<uint32_t>(m_window))
        return false;

    std::array<uint8_t, SERIES_SLOTS> input {};
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (mine & (1u << s))
            input[static_cast<size_t>(s)] = m_held[static_cast<size_t>(s)];
    }
    m_held = {};
    if (m_host) {
        m_inputs[static_cast<size_t>(m_localPeer)].push_back(input);
    } else if (toHost) {
        toHost->epoch = m_epoch;
        toHost->tick = m_added;
        toHost->slotMask = mine;
        toHost->keys = input;
    }
    m_added++;
    return true;
}

void WorldLockstep::onInput(int peer, const WorldInputMsg& m)
{
    if (!m_active || !m_host || m.epoch != m_epoch || peer <= 0 || peer >= net::MAX_PEERS)
        return;
    auto& queue = m_inputs[static_cast<size_t>(peer)];
    //Reliable and in order: the next one is for the tick after the queued ones
    if (m.tick != m_nextTick + queue.size())
        return;
    std::array<uint8_t, SERIES_SLOTS> input {};
    const uint8_t mine = m_slots.slotsOf(peer);
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (mine & (1u << s))
            input[static_cast<size_t>(s)] = m.keys[static_cast<size_t>(s)];
    }
    queue.push_back(input);
}

void WorldLockstep::peerLost(int peer)
{
    if (peer < 0 || peer >= net::MAX_PEERS)
        return;
    m_humanMask = static_cast<uint8_t>(m_humanMask & ~m_slots.slotsOf(peer));
    m_inputs[static_cast<size_t>(peer)].clear();
}

void WorldLockstep::onTick(const WorldTickMsg& m)
{
    if (!m_active || m_host || m.epoch != m_epoch || m.tick != m_received)
        return;
    m_ticks.push_back(m);
    m_received++;
}

bool WorldLockstep::next(WorldTickMsg& tick)
{
    if (!m_active)
        return false;
    if (!m_host) {
        if (m_ticks.empty())
            return false;
        tick = m_ticks.front();
        m_ticks.pop_front();
        m_nextTick++;
        //A tick without one of our players: they were dropped
        m_humanMask &= tick.humanMask;
        return true;
    }

    //Host: the keys of every peer that still has players
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if ((m_slots.slotsOf(p) & m_humanMask) && m_inputs[static_cast<size_t>(p)].empty())
            return false;
    }
    tick = WorldTickMsg();
    tick.epoch = m_epoch;
    tick.tick = m_nextTick;
    tick.humanMask = m_humanMask;
    for (int p = 0; p < net::MAX_PEERS; p++) {
        const uint8_t slots = static_cast<uint8_t>(m_slots.slotsOf(p) & m_humanMask);
        if (!slots)
            continue;
        const std::array<uint8_t, SERIES_SLOTS> input = m_inputs[static_cast<size_t>(p)].front();
        m_inputs[static_cast<size_t>(p)].pop_front();
        for (int s = 0; s < SERIES_SLOTS; s++) {
            if (slots & (1u << s))
                tick.keys[static_cast<size_t>(s)] = input[static_cast<size_t>(s)];
        }
    }
    m_nextTick++;
    return true;
}

} // namespace rollback

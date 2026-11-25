// NetGame's part of the multi-match modes online (tournament, tour, world):
// see NetSeries.h. The menus' part is MenuOnlineSeries.cpp.
#include "NetGame.h"

#include "GameValues.h"
#include "RandomNumberGenerator.h"
#include "SeriesState.h"
#include "SimulationState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rollback {

void NetGame::sendSeries(int peer, const net::ByteWriter& w)
{
    if (!m_lobby || !w.ok() || m_lobby->phase() == NetLobby::Phase::Closed)
        return;
    m_lobby->sendGameMessage(peer, w.data(), w.size());
}

void NetGame::sendMusic(const std::string& song)
{
    if (!m_lobby || !isHost() || !inMatch())
        return;
    std::vector<uint8_t> buffer(song.size() + 16);
    net::ByteWriter w(buffer.data(), buffer.size());
    w.u8(static_cast<uint8_t>(net::MsgType::Music));
    w.u8(m_matchId);
    w.str(song);
    sendSeries(-1, w);
}

void NetGame::addSeriesEvent(const SeriesEvent& event)
{
    if (m_seriesEvents.size() < 64)
        m_seriesEvents.push_back(event);
}

bool NetGame::pollSeriesEvent(SeriesEvent& event)
{
    if (m_seriesEvents.empty())
        return false;
    event = m_seriesEvents.front();
    m_seriesEvents.pop_front();
    return true;
}

bool NetGame::beginSeries(SeriesBeginMsg& begin, std::string& error)
{
    if (!m_lobby || !isHost()) {
        error = "only the host starts matches";
        return false;
    }
    if (m_series.active) {
        error = "a tournament, tour or world is already running";
        return false;
    }
    if (!m_lobby->canStart(error))
        return false;

    std::array<int8_t, INPUT_MAX_PLAYERS> peerOfSlot {{-1, -1, -1, -1}};
    applyLobbySlots(peerOfSlot);

    begin.matchType = static_cast<uint8_t>(game_values.matchtype);
    begin.tournamentGames = game_values.tournamentgames;
    begin.seed = static_cast<uint32_t>(RandomNumberGenerator::gameplay().getInteger(0x7fffffff));
    for (int s = 0; s < SERIES_SLOTS; s++) {
        begin.slotPeer[static_cast<size_t>(s)] = peerOfSlot[static_cast<size_t>(s)];
        begin.control[static_cast<size_t>(s)] = static_cast<uint8_t>(game_values.playercontrol[s]);
        begin.color[static_cast<size_t>(s)] = game_values.colorids[s];
    }
    begin.teamCount = g_sim.score_cnt;
    for (int t = 0; t < 4; t++) {
        begin.teamCounts[t] = game_values.teamcounts[t];
        for (int m = 0; m < 3; m++)
            begin.teamIds[t][m] = game_values.teamids[t][m];
    }
    begin.settings = captureSimulationSettings();

    std::vector<uint8_t> buffer(begin.settings.size() + 512);
    net::ByteWriter w(buffer.data(), buffer.size());
    writeSeriesBegin(w, begin);
    if (!w.ok()) {
        error = "the settings don't fit in a message";
        return false;
    }
    sendSeries(-1, w);
    m_lobby->setSeriesLocked(true);
    m_transport->flush();
    printf("netplay: series: %s (%s) starts, seed %u\n", seriesMatchName().c_str(), begin.file.empty() ? "-" : begin.file.c_str(), begin.seed);
    return true;
}

void NetGame::seriesSetUp(const SeriesBeginMsg& begin)
{
    m_series = Series();
    m_series.active = true;
    m_series.matchType = begin.matchType;
    m_series.seed = begin.seed;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        m_series.slots.peer[static_cast<size_t>(s)] = begin.slotPeer[static_cast<size_t>(s)];
        m_series.slots.control[static_cast<size_t>(s)] = begin.control[static_cast<size_t>(s)];
    }
    m_series.slots.teamCount = begin.teamCount;
    for (int t = 0; t < 4; t++) {
        m_series.slots.teamCounts[t] = begin.teamCounts[t];
        for (int m = 0; m < 3; m++)
            m_series.slots.teamIds[t][m] = begin.teamIds[t][m];
    }
    //Peers that went away while it started
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (m_lobby && !m_lobby->peer(p).present)
            m_series.slots.dropPeer(p);
    }
    m_world.end();
    printf("modesum: series begin type %d seed %u %s\n", begin.matchType, begin.seed, seriesStandings().c_str());
}

void NetGame::endSeries(const std::string& reason)
{
    if (!m_series.active)
        return;
    if (isHost()) {
        uint8_t buffer[300];
        net::ByteWriter w(buffer, sizeof(buffer));
        writeSeriesEnd(w, reason);
        sendSeries(-1, w);
        if (m_transport)
            m_transport->flush();
    }
    printf("netplay: series over%s%s\n", reason.empty() ? "" : ": ", reason.c_str());
    m_series.active = false;
    m_world.end();
    if (m_lobby)
        m_lobby->setSeriesLocked(false);
}

void NetGame::sendSeriesScreen(const SeriesScreenMsg& screen)
{
    if (!isHost() || !m_series.active)
        return;
    uint8_t buffer[16];
    net::ByteWriter w(buffer, sizeof(buffer));
    writeSeriesScreen(w, screen);
    sendSeries(-1, w);
}

void NetGame::sendWheelPress(uint16_t step)
{
    if (isHost() || !m_series.active)
        return;
    uint8_t buffer[8];
    net::ByteWriter w(buffer, sizeof(buffer));
    writeWheelPress(w, step);
    sendSeries(0, w);
}

void NetGame::sendWheelStop(const WheelStopMsg& stop)
{
    if (!isHost() || !m_series.active)
        return;
    uint8_t buffer[32];
    net::ByteWriter w(buffer, sizeof(buffer));
    writeWheelStop(w, stop);
    sendSeries(-1, w);
}

void NetGame::worldBegin(uint16_t epoch, uint8_t humanMask)
{
    SeriesSlots slots = m_series.slots;
    for (int s = 0; s < SERIES_SLOTS; s++) {
        if (slots.control[static_cast<size_t>(s)] == 1 && !(humanMask & (1u << s))) {
            slots.control[static_cast<size_t>(s)] = 2;
            slots.peer[static_cast<size_t>(s)] = -1;
        }
    }
    //A joiner's keys must reach the host before it runs their tick: a window of the round trip and some
    int window = 3;
    if (!isHost() && m_lobby) {
        const int ping = std::max(m_lobby->peer(0).pingMs, 0);
        window = std::clamp(static_cast<int>(std::ceil(static_cast<float>(ping) / 16.0f)) + 6, 6, 40);
    }
    m_world.begin(epoch, isHost(), localPeer(), slots, window);
}

bool NetGame::worldStep(const std::array<uint8_t, SERIES_SLOTS>& localKeys, WorldTickMsg& tick)
{
    if (!m_world.active())
        return false;
    WorldInputMsg input;
    if (m_world.addLocal(localKeys, &input) && !isHost()) {
        uint8_t buffer[16];
        net::ByteWriter w(buffer, sizeof(buffer));
        writeWorldInput(w, input);
        sendSeries(0, w);
    }
    if (!m_world.next(tick))
        return false;
    if (isHost()) {
        uint8_t buffer[16];
        net::ByteWriter w(buffer, sizeof(buffer));
        writeWorldTick(w, tick);
        sendSeries(-1, w);
    }
    return true;
}

void NetGame::seriesResultsApplied()
{
    if (!m_series.active)
        return;
    printf("modesum: match %d after %016llx %s\n", m_matchId, static_cast<unsigned long long>(seriesStateHash(captureSeriesState())),
        seriesStandings().c_str());
}

void NetGame::printSeriesFinal()
{
    printf("modesum: series final %s state %016llx\n", seriesStandings().c_str(), static_cast<unsigned long long>(seriesStateHash(captureSeriesState())));
}

void NetGame::seriesPeerGone(int peer)
{
    if (m_series.slots.dropPeer(peer))
        printf("netplay: series: %s is gone, their players are CPU players now\n", peerName(peer).c_str());
    if (isHost())
        m_world.peerLost(peer);
}

void NetGame::seriesUpdate()
{
    if (!m_series.active || !m_series.endPending || (m_world.active() && m_world.pending() > 0))
        return;
    const std::string reason = m_series.endReason;
    endSeries(reason);
    SeriesEvent event;
    event.type = SeriesEvent::Type::End;
    event.text = reason;
    addSeriesEvent(event);
}

void NetGame::seriesSyncPeers()
{
    //Host: a peer that isn't in the lobby any more (however it went) plays no more
    if (!isHost() || !m_lobby || !m_series.active)
        return;
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (!m_lobby->peer(p).present && m_series.slots.slotsOf(p))
            seriesPeerGone(p);
    }
}

void NetGame::applySeriesSlots(std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot)
{
    seriesSyncPeers();
    const SeriesSlots& slots = m_series.slots;
    g_sim.score_cnt = slots.teamCount;
    for (int t = 0; t < 4; t++) {
        game_values.teamcounts[t] = slots.teamCounts[t];
        for (int m = 0; m < 3; m++)
            game_values.teamids[t][m] = slots.teamIds[t][m];
    }
    for (int s = 0; s < SERIES_SLOTS; s++) {
        game_values.playercontrol[s] = slots.control[static_cast<size_t>(s)];
        peerOfSlot[static_cast<size_t>(s)] = slots.control[static_cast<size_t>(s)] == 1 ? slots.peer[static_cast<size_t>(s)] : -1;
    }
}

void NetGame::handleSeriesMessage(int peer, const uint8_t* data, size_t size)
{
    net::ByteReader r(data + 1, size - 1);
    const bool fromHost = peer == 0 && !isHost();
    SeriesEvent event;
    event.peer = peer;
    switch (static_cast<net::MsgType>(data[0])) {
        case net::MsgType::SeriesBegin:
            if (!fromHost || !readSeriesBegin(r, event.begin))
                break;
            event.type = SeriesEvent::Type::Begin;
            addSeriesEvent(event);
            break;

        case net::MsgType::SeriesScreen:
            if (!fromHost || !m_series.active || !readSeriesScreen(r, event.screen))
                break;
            //The world map's ticks follow right away: be ready for them
            if (event.screen.screen == SeriesScreen::World)
                worldBegin(event.screen.step, event.screen.humanMask);
            event.type = SeriesEvent::Type::Screen;
            addSeriesEvent(event);
            break;

        case net::MsgType::SeriesEnd: {
            std::string reason;
            if (!fromHost || !m_series.active || !readSeriesEnd(r, reason))
                break;
            //(after the world map's last ticks: then the state is the host's)
            m_series.endPending = true;
            m_series.endReason = reason;
            seriesUpdate();
            break;
        }

        case net::MsgType::WheelPress:
            if (!isHost() || !m_series.active || !readWheelPress(r, event.step))
                break;
            event.type = SeriesEvent::Type::WheelPress;
            addSeriesEvent(event);
            break;

        case net::MsgType::WheelStop:
            if (!fromHost || !m_series.active || !readWheelStop(r, event.stop))
                break;
            event.type = SeriesEvent::Type::WheelStop;
            addSeriesEvent(event);
            break;

        case net::MsgType::WorldInput: {
            WorldInputMsg input;
            if (isHost() && m_series.active && readWorldInput(r, input))
                m_world.onInput(peer, input);
            break;
        }

        case net::MsgType::WorldTick: {
            WorldTickMsg tick;
            if (fromHost && m_series.active && readWorldTick(r, tick))
                m_world.onTick(tick);
            break;
        }

        default:
            break;
    }
}

} // namespace rollback

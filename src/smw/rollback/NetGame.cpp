#include "NetGame.h"

#include "CmdArgs.h"
#include "GameMode.h"
#include "GameValues.h"
#include "GSGameplay.h"
#include "MapList.h"
#include "Resimulation.h"
#include "SimulationState.h"
#include "StateChecksum.h"

#include "RandomNumberGenerator.h"

#ifndef NETWORK_DISABLED
#include "EnetTransport.h"
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace rollback {

namespace {
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Online, a finished match goes back to the menus by itself after this long
// (offline, a player presses a key); --autotest uses its own, shorter delay.
constexpr int16_t GAME_OVER_TICKS = 310;
constexpr int16_t AUTOTEST_GAME_OVER_TICKS = 120;
// Waiting for the last inputs to be acknowledged before quitting
constexpr uint32_t QUIT_WAIT_MS = 3000;
// The host gives up starting a match if a peer doesn't accept it within this long
constexpr uint32_t START_TIMEOUT_MS = 60000;
} // namespace

NetGame& NetGame::instance()
{
    static NetGame game;
    return game;
}

// ---- opening and closing

bool NetGame::active() const
{
    return m_lobby && m_lobby->phase() != NetLobby::Phase::Closed;
}

bool NetGame::finished() const
{
    return m_lobby && m_lobby->phase() == NetLobby::Phase::Closed;
}

bool NetGame::isHost() const
{
    return m_lobby && m_lobby->isHost();
}

int NetGame::localPeer() const
{
    return m_lobby ? m_lobby->localPeer() : -1;
}

bool NetGame::isLocalSlot(int slot) const
{
    return m_session && m_session->config().slotPeer[static_cast<size_t>(slot)] == m_session->config().localPeer;
}

std::string NetGame::peerName(int peer) const
{
    if (m_lobby && peer >= 0 && peer < net::MAX_PEERS && !m_lobby->peer(peer).name.empty())
        return m_lobby->peer(peer).name;
    return "Player " + std::to_string(peer + 1);
}

bool NetGame::open(bool asHost, uint16_t port, const std::string& joinHost, uint16_t joinPort, std::string& error)
{
    if (inMatch()) {
        error = "a match is running";
        return false;
    }
    shutdown();

#ifdef NETWORK_DISABLED
    (void)asHost;
    (void)port;
    (void)joinHost;
    (void)joinPort;
    error = "this build has no network support";
    return false;
#else
    const cmd::NetArgs& args = cmd::g_net;
    NetConditions conditions;
    conditions.latencyMs = std::max(0, args.simLatencyMs);
    conditions.jitterMs = std::max(0, args.simJitterMs);
    conditions.lossPercent = std::clamp(args.simLossPercent, 0.0f, 100.0f);
    conditions.seed = 0x5eed0000u ^ static_cast<uint32_t>(args.hostPort * 31 + args.port + static_cast<int>(monotonicMs()));

    std::unique_ptr<EnetTransport> transport = EnetTransport::create(asHost ? port : static_cast<uint16_t>(std::max(0, args.port)), conditions, error);
    if (!transport)
        return false;
    if (conditions.active())
        printf("netplay: simulating %d ms latency, +-%d ms jitter, %.1f%% loss on packets sent\n", conditions.latencyMs, conditions.jitterMs, conditions.lossPercent);

    NetLobby::Options options;
    options.mesh.host = asHost;
    options.mesh.joinHost = joinHost;
    options.mesh.joinPort = joinPort;
    options.mesh.buildId = GIT_REVISION;
    options.mesh.dataHash = simulationDataHash();
    options.name = args.name;
    options.localPlayers = 1;
    options.downloadDirs = mapDownloadDirs();
    m_lobby = std::make_unique<NetLobby>(*transport, options, monotonicMs());
    m_transport = std::move(transport);
    if (m_lobby->phase() == NetLobby::Phase::Closed) {
        error = m_lobby->statusText();
        return false;
    }
    if (asHost && args.delay >= 0)
        m_lobby->setInputDelay(args.delay);
    m_wasClosed = false;
    m_lastMatchError.clear();
    return true;
#endif
}

bool NetGame::host(uint16_t port, std::string& error)
{
    return open(true, port ? port : DEFAULT_LOBBY_PORT, "", 0, error);
}

bool NetGame::join(const std::string& address, std::string& error)
{
    std::string hostName = address;
    int port = DEFAULT_LOBBY_PORT;
    const size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        hostName = address.substr(0, colon);
        port = atoi(address.c_str() + colon + 1);
    }
    if (hostName.empty() || port <= 0 || port > 65535) {
        error = "expected an address like 192.168.1.20:" + std::to_string(DEFAULT_LOBBY_PORT);
        return false;
    }
    return open(false, 0, hostName, static_cast<uint16_t>(port), error);
}

bool NetGame::init(const cmd::NetArgs& args)
{
    if (m_cliDone || !args.enabled())
        return true;
    m_cliDone = true;

    std::string error;
    const bool ok = args.hostPort > 0 ? open(true, static_cast<uint16_t>(args.hostPort), "", 0, error)
                                      : open(false, 0, args.joinHost, static_cast<uint16_t>(args.joinPort), error);
    if (!ok)
        printf("netplay: ERROR: %s\n", error.c_str());
    return ok;
}

void NetGame::leave()
{
    if (!m_lobby)
        return;
    if (inMatch()) {
        leaveMatch("left");
        return;
    }
    m_lobby->leave("left", false);
    if (m_transport)
        m_transport->flush();
}

void NetGame::shutdown()
{
    if (m_transport)
        m_transport->close(500);
    m_inMatch = false;
    m_session.reset();
    m_lobby.reset();
    m_transport.reset();    //disconnects and flushes
    m_pendingConfig = false;
    m_configAccepted = false;
    m_waitingForMap = false;
    m_mapFile.reset();
    m_closeAfterMatch.clear();
    m_abortText.clear();
}

// ---- the main loop

void NetGame::pump()
{
    if (!m_lobby)
        return;

    const uint32_t now = monotonicMs();
    LobbyGameEvent event;
    while (m_lobby->poll(event, now))
        handle(event, now);

    if (m_lobby->phase() == NetLobby::Phase::Closed) {
        if (!m_wasClosed) {
            m_wasClosed = true;
            m_closeMs = now;
        }
    } else {
        //Joiner: a config that arrived while the previous match was still ending
        if (m_pendingConfig && !inMatch())
            processConfig();
        if (isHost() && m_lobby->phase() == NetLobby::Phase::Starting)
            hostUpdateStart(now);
        if (isHost() && m_lobby->phase() == NetLobby::Phase::Lobby)
            updateMatchInfo(now);
    }

    //After a match: keep sending until the others have every input they need
    if (m_session && m_session->state() == RollbackSession::State::Ended && !inMatch())
        m_session->idle(now);

    m_lobby->update(now);
    m_transport->flush();
}

void NetGame::updateMatchInfo(uint32_t nowMs)
{
    if (nowMs - m_lastInfoMs < 250 || !game_values.gamemode)
        return;
    m_lastInfoMs = nowMs;
    const bool minigame = game_values.matchtype == MatchType::MiniGame;
    m_lobby->setMatchInfo(game_values.gamemode->GetModeName(), minigame ? "(minigame)" : maplist->currentShortmapname());
}

void NetGame::handle(const LobbyGameEvent& event, uint32_t nowMs)
{
    using Type = LobbyGameEvent::Type;
    switch (event.type) {
        case Type::Message:
            handleMessage(event.peer, event.data, event.size, nowMs);
            break;

        case Type::PeerGone: {
            const int peer = event.peer;
            const bool inThisMatch = (matchPeers() >> peer) & 1;
            if (inMatch() && inThisMatch) {
                if (m_abortText.empty())
                    m_abortText = peerName(peer) + " " + event.text;
                m_session->onPeerDisconnected(peer, nowMs, "left the match");
            } else if (m_lobby->phase() == NetLobby::Phase::Starting && inThisMatch) {
                if (isHost())
                    abortStart(peer, peerName(peer) + " " + event.text);
                else
                    m_lobby->sendMatchAbort(m_matchId, peer, peerName(localPeer()) + " lost the connection to " + peerName(peer));
            }
            break;
        }

        case Type::MapReceived:
            if (!m_waitingForMap || isHost())
                break;
            m_waitingForMap = false;
            if (event.ok) {
                recheckConfig();
            } else {
                m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_REFUSED, "map download failed: " + event.text);
                returnToLobby("Map download failed: " + event.text);
            }
            break;

        case Type::Closed:
            if (inMatch()) {
                //A match the host finished first; otherwise it ends now
                if (!event.ok) {
                    m_abortText = event.text;
                    m_session->onPeerDisconnected(0, nowMs, "left the match");
                }
                m_closeAfterMatch = event.text;
            }
            break;

        default:
            break;
    }
}

void NetGame::handleMessage(int peer, const uint8_t* data, size_t size, uint32_t nowMs)
{
    net::ByteReader r(data + 1, size - 1);
    switch (static_cast<net::MsgType>(data[0])) {
        case net::MsgType::Config: {
            if (peer != 0 || isHost())
                break;
            const uint8_t matchId = r.u8();
            const uint16_t length = r.u16();
            const uint8_t* bytes = r.bytes(length);
            if (!r.ok())
                break;
            m_pendingConfig = true;
            m_configBytes.assign(bytes, bytes + length);
            m_matchId = matchId;
            m_configAccepted = false;
            m_waitingForMap = false;
            break;
        }

        case net::MsgType::ConfigAck: {
            const uint8_t matchId = r.u8();
            const uint8_t status = r.u8();
            const std::string reason = r.str();
            if (!r.ok() || !isHost() || m_lobby->phase() != NetLobby::Phase::Starting || matchId != m_matchId)
                break;
            if (status == NetLobby::ACK_OK) {
                m_ackedMask |= static_cast<uint8_t>(1u << peer);
            } else if (status == NetLobby::ACK_NEED_MAP) {
                if (!m_mapFile) {
                    std::ifstream file(matchMapPath(m_config), std::ios::binary);
                    m_mapFile = std::make_shared<const std::vector<uint8_t>>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                }
                if (m_mapFile->empty() || m_mapFile->size() > MAP_MAX_BYTES) {
                    abortStart(peer, "the map can't be sent (" + std::to_string(m_mapFile->size()) + " bytes, at most " + std::to_string(MAP_MAX_BYTES) + ")");
                    break;
                }
                const std::string name = !m_config.mapRef.empty() && m_config.mapRef[0] == '@' ? m_config.mapRef.substr(1) : m_config.mapRef;
                m_lobby->sendMap(peer, m_matchId, m_mapFile, m_config.mapHash, name);
                m_startMs = nowMs;    //the download gets its own time
            } else {
                printf("netplay: peer %d refused the match: %s\n", peer, reason.c_str());
                abortStart(peer, peerName(peer) + " can't play this match: " + reason);
            }
            break;
        }

        case net::MsgType::Start: {
            const uint8_t matchId = r.u8();
            if (r.ok() && peer == 0 && !isHost() && m_configAccepted && matchId == m_matchId && m_lobby->phase() == NetLobby::Phase::Starting)
                beginMatch();
            break;
        }

        case net::MsgType::MatchAbort: {
            const uint8_t matchId = r.u8();
            const int culprit = r.u8();
            const std::string reason = r.str();
            if (!r.ok() || matchId != m_matchId)
                break;
            const std::string& text = reason;    //a whole sentence, e.g. "Player 3 timed out"
            if (inMatch()) {
                if (m_abortText.empty())
                    m_abortText = text;
                //Whoever is at fault, the session ends like this
                const int gone = culprit != localPeer() && culprit < net::MAX_PEERS && m_session->config().peerConnection[culprit] >= 0 ? culprit : peer;
                m_session->onPeerDisconnected(gone, nowMs, "aborted the match");
            } else if (m_lobby->phase() == NetLobby::Phase::Starting) {
                if (isHost())
                    abortStart(culprit, text);
                else
                    returnToLobby(text);
            }
            break;
        }

        case net::MsgType::Input:
        case net::MsgType::Checksum:
            if (m_session)
                m_session->onMessage(peer, data, size, nowMs);
            break;

        default:
            break;
    }
}

// ---- starting a match

uint8_t NetGame::matchPeers() const
{
    if (inMatch()) {
        uint8_t mask = 0;
        for (int p = 0; p < net::MAX_PEERS; p++) {
            if (p == m_session->config().localPeer || m_session->config().peerConnection[static_cast<size_t>(p)] >= 0)
                mask |= static_cast<uint8_t>(1u << p);
        }
        return mask;
    }
    //The match being started: the host and every peer with a slot
    uint8_t mask = 1;
    for (const MatchSlot& slot : m_config.slots) {
        if (slot.peer >= 0 && slot.peer < net::MAX_PEERS)
            mask |= static_cast<uint8_t>(1u << slot.peer);
    }
    return mask;
}

void NetGame::applyLobbySlots(std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot)
{
    //The players and teams of the lobby, as the team select menu would set them
    for (int t = 0; t < 4; t++)
        game_values.teamcounts[t] = 0;
    short teams = 0;
    for (int team = 0; team < 4; team++) {
        short members = 0;
        for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
            const LobbySlot& slot = m_lobby->slot(k);
            if (slot.kind != SlotKind::Empty && slot.team == team && members < 3)
                game_values.teamids[teams][members++] = static_cast<short>(k);
        }
        if (members > 0)
            game_values.teamcounts[teams++] = members;
    }
    g_sim.score_cnt = teams;

    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const LobbySlot& slot = m_lobby->slot(k);
        game_values.playercontrol[k] = slot.kind == SlotKind::Human ? 1 : slot.kind == SlotKind::Cpu ? 2 : 0;
        peerOfSlot[static_cast<size_t>(k)] = slot.kind == SlotKind::Human ? slot.peer : -1;
        game_values.colorids[k] = game_values.teamcolors ? slot.team : slot.color;
    }
}

bool NetGame::startMatch(std::string& error)
{
    if (!m_lobby || !isHost()) {
        error = "only the host starts matches";
        return false;
    }
    if (!m_lobby->canStart(error))
        return false;

    SetGameModeSettingsFromMenu();
    std::array<int8_t, INPUT_MAX_PLAYERS> peerOfSlot {{-1, -1, -1, -1}};
    applyLobbySlots(peerOfSlot);

    //A fresh seed from the host's generator: every peer seeds with it
    const uint32_t seed = static_cast<uint32_t>(RandomNumberGenerator::gameplay().getInteger(0x7fffffff));
    if (!captureMatchConfig(m_config, peerOfSlot, seed, m_lobby->inputDelay(), error))
        return false;
    m_config.frameLimit = cmd::g_autotest.enabled ? cmd::g_autotest.frames : 0;
    m_config.gameOverTicks = cmd::g_autotest.enabled ? AUTOTEST_GAME_OVER_TICKS : GAME_OVER_TICKS;

    m_matchId++;
    m_ackedMask = 1;
    m_mapFile.reset();
    m_startMs = monotonicMs();
    m_lastMatchError.clear();
    m_lobby->setPhase(NetLobby::Phase::Starting);
    m_lobby->setStatus("Starting the match...");
    m_lobby->addEvent(LobbyEvent::Type::MatchStarting, -1, "");
    m_lobby->sendConfig(m_matchId, m_config.serialize());
    m_transport->flush();
    float rtt = 0;
    float jitter = 0;
    m_lobby->worstRtt(rtt, jitter);
    printf("netplay: match %d: sent the config to %d peer(s), input delay %d (%s; worst round trip %.0f ms, jitter %.1f ms)\n", m_matchId,
        std::popcount(matchPeers()) - 1, m_config.inputDelay, m_lobby->inputDelayIsAuto() ? "auto" : "fixed", rtt, jitter);
    return true;
}

void NetGame::hostUpdateStart(uint32_t nowMs)
{
    const uint8_t peers = matchPeers();
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (((peers >> p) & 1) && !m_lobby->peer(p).present) {
            abortStart(p, peerName(p) + " left");
            return;
        }
    }
    if ((m_ackedMask & peers) == peers) {
        m_lobby->sendStart(m_matchId);
        m_transport->flush();
        beginMatch();
        return;
    }
    if (nowMs - m_startMs > START_TIMEOUT_MS) {
        for (int p = 1; p < net::MAX_PEERS; p++) {
            if (((peers >> p) & 1) && !((m_ackedMask >> p) & 1)) {
                abortStart(p, peerName(p) + " didn't accept the match in time");
                return;
            }
        }
    }
}

void NetGame::abortStart(int culprit, const std::string& reason)
{
    printf("netplay: match %d doesn't start: %s\n", m_matchId, reason.c_str());
    if (isHost())
        m_lobby->sendMatchAbort(m_matchId, culprit, reason);
    m_mapFile.reset();
    returnToLobby(reason);
}

// Joiner: checks the config the host sent
void NetGame::processConfig()
{
    m_pendingConfig = false;
    m_configAccepted = false;
    m_waitingForMap = false;
    m_lobby->setPhase(NetLobby::Phase::Starting);
    m_lobby->addEvent(LobbyEvent::Type::MatchStarting, -1, "");
    m_lobby->setStatus("Starting the match...");

    if (!m_config.deserialize(m_configBytes.data(), m_configBytes.size())) {
        m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_REFUSED, "the match config is malformed (another game version?)");
        returnToLobby("The match config is malformed");
        return;
    }
    std::string error;
    bool needMap = false;
    if (!checkMatchConfig(m_config, error, &needMap)) {
        if (needMap) {
            printf("netplay: match %d: %s, asking the host for it\n", m_matchId, error.c_str());
            m_waitingForMap = true;
            m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_NEED_MAP, error);
            m_lobby->setStatus("Downloading the map...");
            return;
        }
        printf("netplay: refusing match %d: %s\n", m_matchId, error.c_str());
        m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_REFUSED, error);
        returnToLobby("Can't play this match: " + error);
        return;
    }
    recheckConfig();
}

// Joiner: the config is fine (the map was there, or has arrived): accept it
void NetGame::recheckConfig()
{
    std::string error;
    if (!checkMatchConfig(m_config, error)) {
        m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_REFUSED, error);
        returnToLobby("Can't play this match: " + error);
        return;
    }
    //Every peer of the match must be reachable from here
    for (const MatchSlot& slot : m_config.slots) {
        if (slot.peer >= 0 && slot.peer != localPeer() && !m_lobby->mesh().peerConnected(slot.peer)) {
            error = "no connection to " + peerName(slot.peer);
            m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_REFUSED, error);
            returnToLobby("Can't play this match: " + error);
            return;
        }
    }
    m_configAccepted = true;
    m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_OK, "");
}

void NetGame::returnToLobby(const std::string& abortReason)
{
    if (!m_lobby || m_lobby->phase() == NetLobby::Phase::Closed)
        return;
    const bool wasInMatch = m_lobby->phase() == NetLobby::Phase::InMatch;
    m_lobby->setPhase(NetLobby::Phase::Lobby);
    m_lobby->setStatus(abortReason);
    m_configAccepted = false;
    m_waitingForMap = false;
    if (!abortReason.empty()) {
        m_lastMatchError = abortReason;
        m_lobby->addEvent(LobbyEvent::Type::MatchAborted, -1, abortReason);
    }
    if (wasInMatch) {
        m_lobby->clearReady();
        m_lobby->addEvent(LobbyEvent::Type::ReturnedToLobby, -1, abortReason);
    }
}

void NetGame::beginMatch()
{
    //Parse the host's own config from the bytes too: every peer starts from the same copy
    const std::vector<uint8_t> bytes = m_config.serialize();
    MatchConfig config;
    config.deserialize(bytes.data(), bytes.size());
    m_config = config;

    //The peers of this match: the host and every peer that drives a slot
    SessionConfig session;
    session.peerCount = net::MAX_PEERS;
    session.localPeer = m_lobby->localPeer();
    const std::array<int, net::MAX_PEERS> connections = m_lobby->mesh().peerConnections();
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++)
        session.slotPeer[static_cast<size_t>(k)] = config.slots[static_cast<size_t>(k)].peer;
    int peers = 1;
    for (int p = 0; p < net::MAX_PEERS; p++) {
        bool inMatch = p == 0;
        for (const MatchSlot& slot : config.slots)
            inMatch |= slot.peer == p;
        if (p != session.localPeer && inMatch) {
            session.peerConnection[static_cast<size_t>(p)] = connections[static_cast<size_t>(p)];
            peers++;
        }
    }
    session.inputDelay = config.inputDelay;
    session.checksumInterval = std::max(0, cmd::g_net.checksumInterval);
    session.timeoutMs = static_cast<uint32_t>(std::max(500, cmd::g_net.timeoutMs));
    session.matchId = m_matchId;

    m_session.reset();
    m_matchesStarted++;
    m_abortText.clear();
    m_closeAfterMatch.clear();
    m_waiting = false;
    m_lastMatchError.clear();
    printf("netplay: match %d starts: mode %d, map %s, %d peers, local peer %d, input delay %d\n", m_matchId, config.gameMode,
        config.mapRef.c_str(), peers, session.localPeer, session.inputDelay);

    m_lobby->setPhase(NetLobby::Phase::InMatch);
    m_lobby->setRound(m_matchId);
    m_lobby->setStatus("");
    m_lobby->addEvent(LobbyEvent::Type::MatchStarted, -1, "");
    m_mapFile.reset();

    startConfiguredMatch(config);

    m_session = std::make_unique<RollbackSession>(session, *m_transport, *this, monotonicMs());
    m_inMatch = true;
    m_timing = Timing();
    m_timing.startMs = monotonicMs();
    m_timing.lastStatsMs = m_timing.startMs;
}

// ---- the match

NetGame::Outcome NetGame::advance(const InputFrame& localInput)
{
    Outcome outcome;
    if (!m_session || !inMatch())
        return outcome;

    const Clock::time_point start = Clock::now();
    pump();
    if (!inMatch())
        return outcome;
    const uint32_t now = monotonicMs();
    const FrameResult result = m_session->advanceFrame(localInput, now);
    m_transport->flush();

    const double seconds = secondsSince(start);
    m_timing.updateSeconds += seconds;
    m_timing.maxUpdateSeconds = std::max(m_timing.maxUpdateSeconds, seconds);
    m_timing.updates++;

    //What the overlay shows while the match waits for late inputs
    if (result.ticks > 0) {
        m_waiting = false;
    } else if (result.stalled == StallReason::WaitingForInputs || result.stalled == StallReason::Barrier) {
        if (!m_waiting) {
            m_waiting = true;
            m_waitStartMs = now;
        }
        m_waitPeers = m_session->laggingPeers();
    }

    outcome.ticks = result.ticks;
    const int frame = m_session->frame();
    if (cmd::g_net.statsInterval > 0 && result.ticks > 0 && frame % cmd::g_net.statsInterval == 0)
        printStats(false);

    if (result.matchEnded) {
        printf("netplay: match %d ended at confirmed frame %d\n", m_matchId, frame);
        m_matchEndMs = monotonicMs();
        printStats(true);
        outcome.ended = true;
        m_waiting = false;
        m_inMatch = false;
        returnToLobby("");
        if (!m_closeAfterMatch.empty())
            printf("netplay: session closed: %s\n", m_closeAfterMatch.c_str());
    } else if (m_session->state() == RollbackSession::State::Failed) {
        const int culprit = m_session->failedPeer();
        const std::string what = m_abortText.empty() ? peerName(culprit) + " " + m_session->failReason() : m_abortText;
        printf("netplay: peer %d %s at frame %d: the match ends (%s)\n", culprit, m_session->failReason(), frame, what.c_str());
        printStats(true);
        outcome.failed = true;
        m_waiting = false;
        m_inMatch = false;
        //Tell the others why (they may not have noticed yet)
        m_lobby->sendMatchAbort(m_matchId, culprit, what);
        if (isHost() && culprit > 0 && m_lobby->peer(culprit).present)
            m_lobby->removePeer(culprit, m_session->failReason(), true);    //timed out or gone: out of the lobby
        if (!isHost() && culprit == 0 && m_lobby->phase() != NetLobby::Phase::Closed)
            m_lobby->leave("lost the host", false);
        m_transport->flush();
        returnToLobby("Match aborted: " + what);
        if (m_lobby->phase() == NetLobby::Phase::Closed) {
            m_lastMatchError = "Match aborted: " + what;
            if (m_lobby->statusText().empty() || m_lobby->statusText() == "You left the lobby")
                m_lobby->setStatus(m_lastMatchError);
        }
    }
    return outcome;
}

void NetGame::onMatchLeft()
{
    setSimulationPlayerDevices(nullptr);
}

void NetGame::leaveMatch(const char* reason)
{
    printf("netplay: leaving the match at frame %d (%s)\n", m_session ? m_session->frame() : -1, reason);
    if (m_session)
        printStats(true);
    m_session.reset();
    m_inMatch = false;
    m_lastMatchError = std::string("You left the match (") + reason + ")";
    if (m_lobby) {
        m_lobby->leave(reason, true);
        m_lobby->setStatus(m_lastMatchError);
    }
    if (m_transport)
        m_transport->flush();
}

void NetGame::endSession(const std::string& reason)
{
    if (!m_lobby || m_lobby->phase() == NetLobby::Phase::Closed)
        return;
    m_lobby->leave(reason, false);
    m_transport->flush();
}

bool NetGame::readyForMatch() const
{
    if (!isHost() || m_lobby->phase() != NetLobby::Phase::Lobby || m_lobby->memberCount() < cmd::g_net.peers)
        return false;
    std::string why;
    return m_lobby->everyoneReady(why) && m_lobby->pingsMeasured();
}

bool NetGame::matchDrained()
{
    pump();
    if (!m_session || m_session->drained())
        return true;
    //Don't wait forever for a peer that stopped answering
    return monotonicMs() - m_matchEndMs > QUIT_WAIT_MS;
}

bool NetGame::canQuit()
{
    pump();
    if (monotonicMs() - m_closeMs >= QUIT_WAIT_MS)
        return true;
    //Messages still delayed by the network condition simulator (testing)
    if (m_transport && !m_transport->idle())
        return false;
    return !m_session || m_session->drained() || m_session->state() != RollbackSession::State::Ended;
}

// ---- overlays

NetGame::WaitInfo NetGame::waitInfo() const
{
    WaitInfo info;
    if (!inMatch() || !m_waiting)
        return info;
    info.waiting = true;
    info.peers = m_waitPeers;
    info.waitingMs = monotonicMs() - m_waitStartMs;
    return info;
}

int NetGame::peerPingMs(int peer) const
{
    if (peer < 0 || peer >= net::MAX_PEERS || peer == localPeer())
        return -1;
    if (inMatch()) {
        if (m_session->config().peerConnection[static_cast<size_t>(peer)] < 0)
            return -1;
        return static_cast<int>(m_session->stats().peers[static_cast<size_t>(peer)].rttMs + 0.5f);
    }
    return m_lobby ? m_lobby->peer(peer).pingMs : -1;
}

int NetGame::inputDelay() const
{
    if (inMatch())
        return m_session->config().inputDelay;
    return m_lobby ? m_lobby->inputDelay() : 0;
}

// ---- ISessionCallbacks

bool NetGame::matchEndRequested() const
{
    //The match fades out to leave (e.g. the world map's forced end)
    if (game_values.screenfade == 255 && (game_values.appstate == AppState::StartGame || game_values.appstate == AppState::EndGame))
        return true;

    //Settings of the match config: the same on every peer
    if (g_sim.gameoverTick >= 0 && g_sim.tick >= g_sim.gameoverTick + m_config.gameOverTicks)
        return true;

    return m_config.frameLimit > 0 && g_sim.tick >= m_config.frameLimit;
}

void NetGame::saveState(int frame)
{
    const Clock::time_point start = Clock::now();
    rollback::saveState(m_snapshots[static_cast<size_t>(frame) % SNAPSHOT_RING]);
    m_timing.saveSeconds += secondsSince(start);
    m_timing.saves++;
}

void NetGame::loadState(int frame)
{
    const Clock::time_point start = Clock::now();
    const Snapshot& snapshot = m_snapshots[static_cast<size_t>(frame) % SNAPSHOT_RING];
    if (snapshot.empty() || snapshot.tick() != frame)
        printf("netplay: ERROR: no snapshot of frame %d to roll back to\n", frame);
    rollback::loadState(snapshot);
    m_timing.loadSeconds += secondsSince(start);
    m_timing.loads++;
}

bool NetGame::advance(const InputFrame& input, bool resimulating)
{
    const Clock::time_point start = Clock::now();
    if (resimulating) {
        sim::ResimulationScope scope;
        GameplayState::instance().advanceTick(input);
    } else {
        GameplayState::instance().advanceTick(input);
    }
    m_timing.simSeconds += secondsSince(start);
    m_timing.simTicks++;
    return matchEndRequested();
}

uint64_t NetGame::checksum()
{
    return peerChecksum();
}

void NetGame::onConfirmedChecksum(int frame, uint64_t sum)
{
    printf("netsum: match %d frame %d %016llx\n", m_matchId, frame, static_cast<unsigned long long>(sum));
}

void NetGame::onDesync(int frame, int peer, uint64_t local, uint64_t remote)
{
    printf("netplay: DESYNC at frame %d with peer %d: local checksum %016llx, peer's %016llx\n", frame, peer,
        static_cast<unsigned long long>(local), static_cast<unsigned long long>(remote));
}

void NetGame::printStats(bool final)
{
    if (!m_session)
        return;

    const RollbackSession::Stats& st = m_session->stats();
    const SessionConfig& config = m_session->config();
    const uint32_t now = monotonicMs();
    auto us = [](double seconds, int64_t count) { return count > 0 ? seconds * 1e6 / static_cast<double>(count) : 0.0; };
    auto remote = [&config](int p) { return p != config.localPeer && config.peerConnection[static_cast<size_t>(p)] >= 0; };

    if (!final) {
        const double elapsed = std::max(0.001, static_cast<double>(now - m_timing.lastStatsMs) / 1000.0);
        printf("netstats: frame %d confirmed %d | rollbacks %.1f/s avg %.1f max %d | stalls %lld skips %lld |", m_session->frame(),
            std::min(m_session->confirmedFrame(), m_session->frame()), static_cast<double>(st.rollbacks - m_timing.lastRollbacks) / elapsed,
            st.rollbacks ? static_cast<double>(st.rollbackFrames) / static_cast<double>(st.rollbacks) : 0.0, st.maxRollback,
            static_cast<long long>(st.stallFrames), static_cast<long long>(st.skipFrames));
        for (int p = 0; p < config.peerCount; p++) {
            if (remote(p))
                printf(" p%d rtt %.0fms adv %+.1f", p, st.peers[static_cast<size_t>(p)].rttMs, st.peers[static_cast<size_t>(p)].advantage);
        }
        printf(" | up %.2f kB/s | cpu %.0f us/frame\n", static_cast<double>(st.bytesSent - m_timing.lastBytes) / elapsed / 1000.0,
            us(m_timing.updateSeconds, m_timing.updates));
        m_timing.lastStatsMs = now;
        m_timing.lastBytes = st.bytesSent;
        m_timing.lastRollbacks = st.rollbacks;
        return;
    }

    const double seconds = std::max(0.001, static_cast<double>(now - m_timing.startMs) / 1000.0);
    printf("netmatch: match %d frames %d in %.1fs | delay %d | rollbacks %lld (%.2f/s, avg %.2f frames, max %d) resimulated %lld | "
           "stalls %lld skips %lld barrier-waits %lld | checksums compared %lld, desyncs %lld | sent %lld packets %.2f kB/s\n",
        m_matchId, m_session->frame(), seconds, config.inputDelay, static_cast<long long>(st.rollbacks), static_cast<double>(st.rollbacks) / seconds,
        st.rollbacks ? static_cast<double>(st.rollbackFrames) / static_cast<double>(st.rollbacks) : 0.0, st.maxRollback,
        static_cast<long long>(st.resimulatedTicks), static_cast<long long>(st.stallFrames), static_cast<long long>(st.skipFrames),
        static_cast<long long>(st.barrierFrames), static_cast<long long>(st.checksumsCompared), static_cast<long long>(st.desyncs),
        static_cast<long long>(st.packetsSent), static_cast<double>(st.bytesSent) / seconds / 1000.0);
    printf("netcpu: match %d update %.1f us avg (max %.0f us) | tick %.1f us | save %.1f us | load %.1f us | ticks per update %.2f\n", m_matchId,
        us(m_timing.updateSeconds, m_timing.updates), m_timing.maxUpdateSeconds * 1e6, us(m_timing.simSeconds, m_timing.simTicks),
        us(m_timing.saveSeconds, m_timing.saves), us(m_timing.loadSeconds, m_timing.loads),
        m_timing.updates ? static_cast<double>(m_timing.simTicks) / static_cast<double>(m_timing.updates) : 0.0);
    for (int p = 0; p < config.peerCount; p++) {
        if (remote(p))
            printf("netpeer: match %d peer %d rtt %.0f ms advantage %+.2f\n", m_matchId, p, st.peers[static_cast<size_t>(p)].rttMs,
                st.peers[static_cast<size_t>(p)].advantage);
    }
}

} // namespace rollback

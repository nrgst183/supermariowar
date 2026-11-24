#include "NetGame.h"
#include "PortMapping.h"
#include "Replay.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "ResourceManager.h"
#include "GameMode.h"
#include "GameValues.h"
#include "FPSLimiter.h"
#include "GSGameplay.h"
#include "MapList.h"
#include "Resimulation.h"
#include "SimulationState.h"
#include "StateChecksum.h"

#include "RandomNumberGenerator.h"
#include "SeriesState.h"

#ifndef NETWORK_DISABLED
#include "EnetTransport.h"
#include "LanDiscovery.h"
#endif

#ifdef SMW_XBOX
#include <SDL3/SDL.h>
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

std::string NetGame::slotPlayerName(int slot) const
{
    if (slot < 0 || slot >= INPUT_MAX_PLAYERS)
        return {};
    const int peer = m_config.slots[static_cast<size_t>(slot)].peer;
    if (peer < 0 || peer >= net::MAX_PEERS)
        return {};
    return m_matchNames[static_cast<size_t>(peer)];
}

std::string NetGame::peerName(int peer) const
{
    if (m_lobby && peer >= 0 && peer < net::MAX_PEERS && !m_lobby->peer(peer).name.empty())
        return m_lobby->peer(peer).name;
    return "Player " + std::to_string(peer + 1);
}

#ifndef NETWORK_DISABLED
namespace {
// System link: a host answers the searches of the local network (LanDiscovery.h)
lan::Responder& lanResponder()
{
    static lan::Responder responder;
    return responder;
}
} // namespace
#endif

// The network of a lobby: ENet's UDP socket on the port (0: any free one), with the simulated
// conditions of the tests
std::unique_ptr<IMeshTransport> NetGame::createNetwork(uint16_t port, std::string& error) const
{
#ifdef NETWORK_DISABLED
    (void)port;
    error = "this build has no network support";
    return nullptr;
#else
    const cmd::NetArgs& args = cmd::g_net;
    NetConditions conditions;
    conditions.latencyMs = std::max(0, args.simLatencyMs);
    conditions.jitterMs = std::max(0, args.simJitterMs);
    conditions.lossPercent = std::clamp(args.simLossPercent, 0.0f, 100.0f);
    conditions.seed = 0x5eed0000u ^ static_cast<uint32_t>(args.hostPort * 31 + args.port + static_cast<int>(monotonicMs()));

    std::unique_ptr<EnetTransport> transport = EnetTransport::create(port, conditions, error);
    if (transport && conditions.active())
        printf("netplay: simulating %d ms latency, +-%d ms jitter, %.1f%% loss on packets sent\n", conditions.latencyMs, conditions.jitterMs, conditions.lossPercent);
    return transport;
#endif
}

// Friends join without a port forwarded by hand where the router lets us open it (UPnP)
void NetGame::openRouterPort(uint16_t port)
{
#ifndef NETWORK_DISABLED
    const cmd::NetArgs& args = cmd::g_net;
    if (args.upnp > 0 || (args.upnp < 0 && !cmd::g_autotest.enabled))
        upnp::openPort(port);
#else
    (void)port;
#endif
}

bool NetGame::open(bool asHost, uint16_t port, const std::string& joinHost, uint16_t joinPort, LobbyAccess access, std::string& error)
{
    if (inMatch()) {
        error = "a match is running";
        return false;
    }
    shutdown();
    setEditActivity(0, false, {});    //(a new lobby plays matches, unless told otherwise)

    //A host's lobby nobody can join has no network at all (no socket: no firewall prompt, and the
    //builds without the network have it too); the network comes when the host opens it
    const cmd::NetArgs& args = cmd::g_net;
    auto transport = std::make_unique<SwitchableTransport>();
    if (!asHost || access != LobbyAccess::Nobody) {
        std::unique_ptr<IMeshTransport> network = createNetwork(asHost ? port : static_cast<uint16_t>(std::max(0, args.port)), error);
        if (!network)
            return false;
        transport->setInner(std::move(network));
    }

    NetLobby::Options options;
    options.mesh.host = asHost;
    options.mesh.joinHost = joinHost;
    options.mesh.joinPort = joinPort;
    options.mesh.buildId = GIT_REVISION;
    options.mesh.dataHash = simulationDataHash();
    options.name = args.name;
    options.localPlayers = 1;
    options.downloadDirs = mapDownloadDirs();
    options.mapSlowdown = args.slowMap;
    options.skinSearchDirs = skinSearchDirs();
    options.skinDownloadDirs = skinDownloadDirs();
    m_skinsAnnounced = false;
    m_mapCheckKey.clear();
    m_skinRevision = 0;
    m_lobby = std::make_unique<NetLobby>(*transport, options, monotonicMs());
    m_transport = std::move(transport);
    if (m_lobby->phase() == NetLobby::Phase::Closed) {
        error = m_lobby->statusText();
        return false;
    }
    m_access = asHost ? access : LobbyAccess::Internet;
    m_hostPort = asHost ? port : 0;
    if (asHost && args.delay >= 0)
        m_lobby->setInputDelay(args.delay);
#ifndef NETWORK_DISABLED
    if (asHost && access == LobbyAccess::Internet)
        openRouterPort(port);
    //System link: the games on the local network find this one
    if (asHost && access != LobbyAccess::Nobody)
        lanResponder().open();
#endif
    m_wasClosed = false;
    m_lastMatchError.clear();
    return true;
}

bool NetGame::hasOthers() const
{
    if (!m_lobby)
        return false;
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (p != m_lobby->localPeer() && m_lobby->peer(p).present)
            return true;
    }
    return false;
}

bool NetGame::setAccess(LobbyAccess access, std::string& error)
{
    if (!m_lobby || !isHost()) {
        error = "only the host changes who can join";
        return false;
    }
    if (access == m_access)
        return true;
    if (inMatch()) {
        error = "a match is running";
        return false;
    }

    //Nobody: the network goes (only with nobody else in: they'd be cut off)
    if (access == LobbyAccess::Nobody) {
        if (hasOthers()) {
            error = "The others are in: kick them first";
            return false;
        }
#ifndef NETWORK_DISABLED
        upnp::closePort();
        lanResponder().close();
#endif
        m_transport->setInner(nullptr);
        m_access = access;
        return true;
    }

    //The network comes (from Nobody), or only the router's port changes
    if (!m_transport->hasInner()) {
        std::unique_ptr<IMeshTransport> network = createNetwork(m_hostPort, error);
        if (!network)
            return false;
        m_transport->setInner(std::move(network));
#ifndef NETWORK_DISABLED
        lanResponder().open();
#endif
    }
#ifndef NETWORK_DISABLED
    if (access == LobbyAccess::Internet)
        openRouterPort(m_hostPort);
    else
        upnp::closePort();
#endif
    m_access = access;
    return true;
}

std::string NetGame::routerNote(bool& good) const
{
    good = false;
#ifdef NETWORK_DISABLED
    return {};
#else
    if (!m_lobby || !isHost() || m_access == LobbyAccess::Nobody)
        return {};
#ifdef SMW_XBOX
    //No router to open on the Xbox: system link, on the local network
    char address[32];
    switch (SDL_XboxGetNetworkStatus(address, sizeof(address))) {
        case SDL_XBOX_NETWORK_READY:
            good = true;
            return "System link: others join at " + std::string(address) + ":" + std::to_string(m_transport ? m_transport->localPort() : DEFAULT_LOBBY_PORT);
        case SDL_XBOX_NETWORK_NO_CABLE:
            return "No network cable: plug one in for system link";
        case SDL_XBOX_NETWORK_PENDING:
            return "Getting a network address...";
        default:
            return "The network has no address (check the dashboard's network settings)";
    }
#endif
    const upnp::Status st = upnp::status();
    const std::string port = std::to_string(st.port);
    switch (st.state) {
        case upnp::State::Working:
            return "Opening port " + port + " on your router...";
        case upnp::State::Open:
            if (st.behindAnotherNat)
                return "Port " + port + " opened; your ISP's NAT may block it";
            good = true;
            return st.publicAddress.empty() ? "Router port " + port + " opened" : "Friends join at " + st.publicAddress + ":" + port;
        case upnp::State::Failed:
            //(why: in the log)
            return "Couldn't open port " + port + ": forward UDP " + port;
        default:
            return {};
    }
#endif
}

std::string NetGame::lanAddress()
{
#ifdef NETWORK_DISABLED
    return {};
#else
    //Asked every frame by the menus: looked up again every few seconds (the network may change)
    static std::string address;
    static auto askedAt = std::chrono::steady_clock::time_point();
    const auto now = std::chrono::steady_clock::now();
    if (askedAt == std::chrono::steady_clock::time_point() || now - askedAt > std::chrono::seconds(5)) {
        address = EnetTransport::localAddress();
        askedAt = now;
    }
    return address;
#endif
}

uint16_t NetGame::lobbyPort() const
{
    return m_transport ? m_transport->localPort() : 0;
}

bool NetGame::host(uint16_t port, std::string& error, LobbyAccess access)
{
    return open(true, port ? port : DEFAULT_LOBBY_PORT, "", 0, access, error);
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
    if (hostName.empty()) {
        error = "expected an address like 192.168.1.20:" + std::to_string(DEFAULT_LOBBY_PORT);
        return false;
    }
    if (port <= 0 || port > 65535) {
        error = "the port (after the ':') must be a number from 1 to 65535";
        return false;
    }
    return open(false, 0, hostName, static_cast<uint16_t>(port), LobbyAccess::Internet, error);
}

bool NetGame::init(const cmd::NetArgs& args)
{
    if (m_cliDone || !args.enabled())
        return true;
    m_cliDone = true;

    std::string error;
    const bool ok = args.hostPort > 0 ? open(true, static_cast<uint16_t>(args.hostPort), "", 0, LobbyAccess::Internet, error)
                                      : open(false, 0, args.joinHost, static_cast<uint16_t>(args.joinPort), LobbyAccess::Internet, error);
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
#ifndef NETWORK_DISABLED
    upnp::closePort();
    lanResponder().close();
#endif
    if (m_transport)
        m_transport->close(500);
    m_inMatch = false;
    FPSLimiter::instance().setTickStretch(1.0f);
    m_session.reset();
    m_lobby.reset();
    m_transport.reset();    //disconnects and flushes
    m_pendingConfig = false;
    m_configAccepted = false;
    m_waitingForMap = false;
    m_mapFile.reset();
    m_closeAfterMatch.clear();
    m_abortText.clear();
    m_series = Series();
    m_world.end();
    m_seriesEvents.clear();
}

// ---- the main loop

void NetGame::pump()
{
    if (!m_lobby)
        return;

    const uint32_t now = monotonicMs();
#ifndef NETWORK_DISABLED
    //System link: the searches of the local network get this game
    if (isHost() && lanResponder().isOpen() && m_lobby->phase() != NetLobby::Phase::Closed) {
        lan::HostInfo info;
        info.port = m_transport ? m_transport->localPort() : DEFAULT_LOBBY_PORT;
        info.name = m_lobby->peer(m_lobby->localPeer()).name;
        if (info.name.empty())
            info.name = "Player 1";
        info.members = static_cast<uint8_t>(m_lobby->memberCount());
        info.maxMembers = static_cast<uint8_t>(net::MAX_PEERS);
        info.inMatch = m_lobby->phase() != NetLobby::Phase::Lobby;
        info.build = GIT_REVISION;
        info.mode = m_lobby->modeName();
        info.map = m_lobby->mapName();
        for (int s = 0; s < 4; s++)
            info.players += m_lobby->slot(s).kind != SlotKind::Empty;
        //(a lobby that edits together: the list says so, and whether the host is in the editor now)
        info.activity = m_editActivity == 2 ? lan::ACTIVITY_WORLD_EDITOR : m_editActivity == 1 ? lan::ACTIVITY_MAP_EDITOR : lan::ACTIVITY_MATCHES;
        info.editing = m_editRunning;
        lanResponder().answer(info);
    }
#endif
    LobbyGameEvent event;
    while (m_lobby->poll(event, now))
        handle(event, now);

    if (m_lobby->phase() == NetLobby::Phase::Closed) {
        if (!m_wasClosed) {
            m_wasClosed = true;
            m_closeMs = now;
        }
    } else {
        updateSkins(now);
        updateMapCheck();
        //Joiner: a config that arrived while the previous match was still ending
        //(or while the world map still has ticks to run before it)
        if (m_pendingConfig && !inMatch() && !(m_world.active() && m_world.pending() > 0))
            processConfig();
        if (isHost() && m_lobby->phase() == NetLobby::Phase::Starting)
            hostUpdateStart(now);
        if (isHost() && m_lobby->phase() == NetLobby::Phase::Lobby)
            updateMatchInfo(now);
    }

    seriesUpdate();

    //After a match: keep sending until the others have every input they need
    if (m_session && m_session->state() == RollbackSession::State::Ended && !inMatch())
        m_session->idle(now);

    m_lobby->update(now);
    m_transport->flush();
}

// Skins are cosmetic: announces ours, and when a skin arrives during a match, the sprites are reloaded
void NetGame::updateSkins(uint32_t nowMs)
{
    if (!skinlist || skinlist->count() == 0)
        return;

    //--net-skin NAME[,NAME2]: the skins of the local players (testing)
    static bool commandLineApplied = false;
    if (!commandLineApplied) {
        commandLineApplied = true;
        size_t start = 0;
        for (int i = 0; i < MAX_LOCAL_PLAYERS && start <= cmd::g_net.skin.size() && !cmd::g_net.skin.empty(); i++) {
            size_t end = cmd::g_net.skin.find(',', start);
            if (end == std::string::npos)
                end = cmd::g_net.skin.size();
            const std::string name = cmd::g_net.skin.substr(start, end - start);
            start = end + 1;
            for (size_t s = 0; s < skinlist->count(); s++) {
                if (skinlist->at(s).name == name)
                    game_values.skinids[i] = static_cast<short>(s);
            }
        }
    }

    m_lobby->setSkinDownloads(game_values.skindownloads);

    const auto valid = [](short id) { return id >= 0 && id < static_cast<short>(skinlist->count()); };
    //(two at least, as builds with at most two players on a machine announce; more if more play here)
    const int announced = std::max(2, m_lobby->peer(std::max(0, m_lobby->localPeer())).localPlayers);
    uint64_t key = static_cast<uint64_t>(announced);
    for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
        key = (key << 16) | static_cast<uint16_t>(game_values.skinids[i]);
    //(flipping through the skins: the peers get where it stopped, and while it goes on, the skin now
    //a few times a second, not every skin passed by)
    constexpr uint32_t SKIN_SETTLE_MS = 150;
    constexpr uint32_t SKIN_EVERY_MS = 300;
    if (key != m_skinSeenKey) {
        m_skinSeenKey = key;
        m_skinSeenMs = nowMs;
    }
    const bool due = nowMs - m_skinSeenMs >= SKIN_SETTLE_MS || nowMs - m_skinSentMs >= SKIN_EVERY_MS;
    if (!m_skinsAnnounced || (key != m_skinKey && due)) {
        m_skinsAnnounced = true;
        m_skinSentMs = nowMs;
        m_skinKey = key;
        std::vector<LocalSkinFile> skins;
        for (int i = 0; i < announced; i++) {
            const short id = game_values.skinids[i];
            const SkinListNode& node = skinlist->at(valid(id) ? static_cast<size_t>(id) : 0);
            skins.push_back({node.name, node.path.string()});
        }
        m_lobby->setLocalSkins(skins);
    }

    const uint32_t revision = m_lobby->skinRevision();
    if (revision != m_skinRevision) {
        m_skinRevision = revision;
        if (inMatch()) {
            for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
                if (game_values.playercontrol[k] > 0)
                    loadMatchSkin(k, game_values.colorids[k]);
            }
        }
    }
}

bool NetGame::popSettingRequest(SettingRequest& request)
{
    if (m_settingRequests.empty())
        return false;
    request = std::move(m_settingRequests.front());
    m_settingRequests.pop_front();
    return true;
}

void NetGame::setEditActivity(int editor, bool running, const std::string& document)
{
    m_editActivity = editor;
    m_editRunning = editor != 0 && running;
    m_editDocument = document;
    m_lastInfoMs = 0;    //(shown at the next pump)
}

void NetGame::updateMatchInfo(uint32_t nowMs)
{
    if (nowMs - m_lastInfoMs < 250)
        return;
    //A lobby that edits together: the editor and the document (the game's menus may not be there:
    //the editor runs)
    if (m_editActivity != 0) {
        m_lastInfoMs = nowMs;
        m_lobby->setMatchInfo(m_editActivity == 1 ? "Map editor" : "World editor", m_editDocument);
        m_lobby->setMatchDetail(m_editActivity == 1 ? MODE_ID_MAP_EDITOR : MODE_ID_WORLD_EDITOR, m_editRunning ? 1 : 0, "");
        m_lobby->setMapCheck("", 0);
        return;
    }
    if (!game_values.gamemode)
        return;
    m_lastInfoMs = nowMs;
    if (isSeriesMatchType(game_values.matchtype) && !m_series.active) {
        m_lobby->setMatchInfo(seriesMatchName(), seriesFileName());
        m_lobby->setMatchDetail(-2, 0, "");
        m_lobby->setMapCheck("", 0);
        return;
    }
    const bool minigame = game_values.matchtype == MatchType::MiniGame;
    m_lobby->setMatchInfo(game_values.gamemode->GetModeName(), minigame ? "(minigame)" : maplist->currentShortmapname());
    m_lobby->setMatchDetail(game_values.gamemode->getgamemode(), game_values.gamemode->goal, game_values.gamemode->GetGoalName());
    std::string mapRef;
    uint64_t mapHash = 0;
    lobbyMapCheck(mapRef, mapHash);
    m_lobby->setMapCheck(mapRef, mapHash);
}

// Whether a map we lack is downloaded (the setting, or --net-map-downloads)
bool NetGame::mapDownloadsOn()
{
    return cmd::g_net.mapDownloads >= 0 ? cmd::g_net.mapDownloads != 0 : game_values.mapdownloads;
}

// Joiner: whether we have the lobby's map, when we won't download it (the others see it on our card)
void NetGame::updateMapCheck()
{
    if (isHost())
        return;
    const std::string key = m_lobby->mapRef() + "|" + std::to_string(m_lobby->mapHash()) + (mapDownloadsOn() ? "|1" : "|0");
    if (key == m_mapCheckKey)
        return;
    m_mapCheckKey = key;
    const uint64_t hash = m_lobby->mapHash();
    m_lobby->setLackedMap(!mapDownloadsOn() && !haveMap(m_lobby->mapRef(), hash) ? hash : 0);
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
            //The match goes on without it (also one that ended here: the others may still need us to agree)
            if (m_session && inThisMatch && (inMatch() || m_session->state() == RollbackSession::State::Ended))
                m_session->onPeerDisconnected(peer, nowMs, event.text == "left" ? "left the match" : "lost the connection");
            if (!inMatch() && m_lobby->phase() == NetLobby::Phase::Starting && inThisMatch) {
                if (isHost())
                    abortStart(peer, peerName(peer) + " " + event.text);
                else
                    m_lobby->sendMatchAbort(m_matchId, peer, peerName(localPeer()) + " lost the connection to " + peerName(peer));
            }
            if (m_series.active)
                seriesPeerGone(peer);
            break;
        }

        case Type::SettingsRequest:
            if (isHost() && m_settingRequests.size() < 16)
                m_settingRequests.push_back({event.peer, static_cast<int>(event.size), event.value, event.text});
            break;

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
                //The host closed the lobby after this match; or it is gone, and
                //the match goes on without it. (Removed from the lobby: the
                //others' PEER_DROP report, which comes first, ends our match.)
                if (!event.ok && event.text.rfind("You were removed", 0) != 0)
                    m_session->onPeerDisconnected(0, nowMs, "left the lobby");
                m_closeAfterMatch = event.text;
            }
            if (m_series.active && m_series.endPending) {
                //The series ended just before: it ends once the world map has run its last ticks (seriesUpdate)
            } else if (m_series.active) {
                //Without the lobby there is no series
                printf("netplay: the series ends: %s\n", event.text.c_str());
                m_series.active = false;
                m_world.end();
                SeriesEvent end;
                end.type = SeriesEvent::Type::End;
                end.text = event.text;
                addSeriesEvent(end);
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
            } else if (status == NetLobby::ACK_NO_MAP) {
                printf("netplay: peer %d lacks the map, its map downloads are off\n", peer);
                abortStart(peer, peerName(peer) + " doesn't have this map and has map downloads turned off");
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
                //In a running match: the sender can't go on and leaves it; we go on without it
                m_session->onPeerDisconnected(peer, nowMs, "left the match");
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
        case net::MsgType::PeerDrop:
            if (m_session)
                m_session->onMessage(peer, data, size, nowMs);
            break;

        case net::MsgType::EditStart:
        case net::MsgType::EditData:
            if (m_editHandler)
                m_editHandler(peer, data, size);
            break;

        default:
            if (data[0] >= static_cast<uint8_t>(net::MsgType::SeriesBegin) && data[0] < net::SERIES_MSG_END)
                handleSeriesMessage(peer, data, size);
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
    //A series keeps its players (the ready flags were for its first match)
    if (m_series.active ? m_lobby->phase() != NetLobby::Phase::Lobby : !m_lobby->canStart(error)) {
        if (m_series.active)
            error = "a match is already starting";
        return false;
    }

    SetGameModeSettingsFromMenu();
    std::array<int8_t, INPUT_MAX_PLAYERS> peerOfSlot {{-1, -1, -1, -1}};
    if (m_series.active)
        applySeriesSlots(peerOfSlot);
    else
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
    //(signed: a match started again during this pump has a start time after nowMs)
    if (static_cast<int32_t>(nowMs - m_startMs) > static_cast<int32_t>(START_TIMEOUT_MS)) {
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

    //A series goes on without a peer that is gone: the same match again, with its players as CPU players
    if (m_series.active && isHost() && culprit > 0 && culprit < net::MAX_PEERS && !m_series.startRetried) {
        //(a peer that another one lost: out of the lobby too, as when we lose it)
        m_series.startRetried = true;
        if (m_lobby->peer(culprit).present)
            m_lobby->removePeer(culprit, "lost the connection", true);
        seriesPeerGone(culprit);
        std::string error;
        if (startMatch(error))
            printf("netplay: series: starting the match again without %s\n", peerName(culprit).c_str());
        else
            endSeries("the match couldn't start: " + error);
    } else if (m_series.active && isHost()) {
        endSeries("the match couldn't start: " + reason);
    }
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
    if (!m_config.series.empty()) {
        //Our own series state, to compare with the host's when the match starts
        m_world.end();
        prepareSeriesStateForMatch(m_config.gameMode);
        m_series.localStateHash = seriesStateHash(captureSeriesState());
    }
    std::string error;
    bool needMap = false;
    if (!checkMatchConfig(m_config, error, &needMap)) {
        if (needMap && !mapDownloadsOn()) {
            printf("netplay: match %d: %s, and map downloads are off\n", m_matchId, error.c_str());
            m_lobby->sendConfigAck(m_matchId, NetLobby::ACK_NO_MAP, error);
            returnToLobby("You don't have this map, and your map downloads are turned off");
            return;
        }
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
        //(in a series, the menus go on with it)
        if (!m_series.active)
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
    //The host's machine plays the CPU players: their AI's buttons are its input (CpuDriver.h)
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const MatchSlot& slot = config.slots[static_cast<size_t>(k)];
        session.slotPeer[static_cast<size_t>(k)] = slot.control == 2 ? 0 : slot.peer;
    }
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
    session.maxPrediction = MAX_PREDICTION;
    //(time sync: the clock slows a little below this, see advance())
    session.skipThreshold = 3.0f;
    session.checksumInterval = std::max(0, cmd::g_net.checksumInterval);
    session.timeoutMs = static_cast<uint32_t>(std::max(500, cmd::g_net.timeoutMs));
    session.matchId = m_matchId;

    m_session.reset();
    m_matchesStarted++;
    for (int p = 0; p < net::MAX_PEERS; p++)
        m_matchNames[static_cast<size_t>(p)] = peerName(p);
    m_lostSeen = 0;
    m_dropsSeen = 0;
    m_dropNotice.clear();
    m_abortText.clear();
    m_closeAfterMatch.clear();
    m_waiting = false;
    m_lastMatchError.clear();
    printf("netplay: match %d starts: mode %d, map %s, %d peers, local peer %d, input delay %d\n", m_matchId, config.gameMode,
        config.mapRef.c_str(), peers, session.localPeer, session.inputDelay);
    if (!config.series.empty()) {
        //Every peer must have come to the same series state
        const uint64_t hostHash = seriesStateHash(config.series);
        const uint64_t localHash = isHost() ? hostHash : m_series.localStateHash;
        printf("modesum: series match %d before %016llx\n", m_series.matches + 1, static_cast<unsigned long long>(localHash));
        if (localHash != hostHash)
            printf("netplay: SERIES DESYNC before series match %d: local state %016llx, the host's %016llx (playing from the host's)\n",
                m_series.matches + 1, static_cast<unsigned long long>(localHash), static_cast<unsigned long long>(hostHash));
        m_series.startRetried = false;
        m_world.end();
    }

    m_lobby->setPhase(NetLobby::Phase::InMatch);
    m_lobby->setRound(m_matchId);
    m_lobby->setStatus("");
    m_lobby->addEvent(LobbyEvent::Type::MatchStarted, -1, "");
    m_mapFile.reset();

    //The replay (match history): the names and skins of the players
    std::array<std::string, INPUT_MAX_PLAYERS> names;
    std::array<std::string, INPUT_MAX_PLAYERS> skins;
    for (int k = 0; k < INPUT_MAX_PLAYERS; k++) {
        const MatchSlot& slot = config.slots[static_cast<size_t>(k)];
        if (slot.control == 0)
            continue;
        names[static_cast<size_t>(k)] = slot.peer >= 0 ? m_matchNames[static_cast<size_t>(slot.peer)] : "CPU";
        skins[static_cast<size_t>(k)] = m_lobby->slotSkinPath(k);
    }
    replay::beginOnline(bytes, names, skins);

    startConfiguredMatch(config);
    GameplayState::instance().correction().reset();
    FPSLimiter::instance().setTickStretch(1.0f);

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
    updateLostPeers(now);
    m_transport->flush();

    //Time sync, smoothly: while we run ahead of the others, our ticks get a little longer (up to 10%:
    //a frame regained in 10 ticks), so they catch up without a skipped tick (a visible hitch; the
    //session only skips when far ahead)
    {
        const float ahead = m_session->aheadFrames();
        const float stretch = ahead > 0.25f ? 1.0f + std::min(0.1f, (ahead - 0.25f) * 0.08f + 0.02f) : 1.0f;
        FPSLimiter::instance().setTickStretch(stretch);
    }

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
        if (m_series.active) {
            //The menus between the matches use the gameplay generator too: from the same seed everywhere
            m_series.matches++;
            RandomNumberGenerator::gameplay().seed(static_cast<uint32_t>(peerChecksum() ^ (peerChecksum() >> 32)) ^ m_series.seed);
        }
        m_matchEndMs = monotonicMs();
        printStats(true);
        outcome.ended = true;
        m_waiting = false;
        m_inMatch = false;
    FPSLimiter::instance().setTickStretch(1.0f);
        returnToLobby("");
        if (!m_closeAfterMatch.empty()) {
            printf("netplay: session closed: %s\n", m_closeAfterMatch.c_str());
            if (m_lobby->phase() == NetLobby::Phase::Closed)
                m_lobby->setStatus(m_closeAfterMatch + " (the match was played to its end)");
        }
    } else if (m_session->state() == RollbackSession::State::Failed) {
        onSessionFailed();
        outcome.failed = true;
    }
    return outcome;
}

// The lost peers of the running match (see LOST PEERS in RollbackSession.h):
// tells about them, and the host removes them from the lobby
void NetGame::updateLostPeers(uint32_t nowMs)
{
    const uint8_t lost = m_session->lostPeers();
    if (lost == m_lostSeen && m_dropsSeen == m_lostSeen)
        return;

    for (int p = 0; p < net::MAX_PEERS; p++) {
        const uint8_t bit = static_cast<uint8_t>(1u << p);
        const std::string& name = m_matchNames[static_cast<size_t>(p)];
        if ((lost & bit) && !(m_lostSeen & bit)) {
            m_lostSeen |= bit;
            const char* reason = m_session->lostReason(p);
            printf("netplay: peer %d (%s) %s at frame %d: the match goes on without it\n", p, name.c_str(), reason, m_session->frame());
            if (isHost() && p > 0 && m_lobby->peer(p).present)
                m_lobby->removePeer(p, std::string(reason) == "timed out" ? "timed out" : "left the match", true);
            if (!isHost() && p == 0 && m_closeAfterMatch.empty())
                m_closeAfterMatch = "Lost the connection to the host";
        }
        const int dropFrame = m_session->dropFrame(p);
        if (dropFrame >= 0 && !(m_dropsSeen & bit)) {
            m_dropsSeen |= bit;
            int slots = 0;
            for (int8_t slotPeer : m_session->config().slotPeer)
                slots += slotPeer == p;
            printf("netplay: peer %d dropped from frame %d: %d player(s) played by the CPU\n", p, dropFrame, slots);
            m_dropNotice = name + (slots > 0 ? " disconnected - CPU takes over" : " disconnected");
            m_dropNoticeMs = nowMs;
        }
    }
}

// Our session can't go on: the others dropped us (we were cut off or too
// late), or inputs of a lost peer are missing here. We leave the match and
// the lobby; the others play on without us.
void NetGame::onSessionFailed()
{
    const int culprit = m_session->failedPeer();
    const std::string what = culprit == localPeer() ? std::string("You ") + m_session->failReason()
                                                    : "Can't go on: " + std::string(m_session->failReason());
    printf("netplay: leaving the match at frame %d: %s\n", m_session->frame(), what.c_str());
    printStats(true);
    m_waiting = false;
    m_inMatch = false;
    FPSLimiter::instance().setTickStretch(1.0f);
    if (m_series.active) {
        //The match doesn't count: its state isn't the same everywhere. Back to the series state it started from.
        applySeriesState(m_config.series);
        game_values.gamemode->gameover = false;
        game_values.gamemode->winningteam = -1;
        printf("netplay: series: the aborted match doesn't count\n");
    }
    m_lobby->sendMatchAbort(m_matchId, localPeer(), what);
    m_lobby->leave(culprit == localPeer() ? "was dropped from the match" : "left the match", true);
    m_transport->flush();
    m_lastMatchError = what;
    m_lobby->setStatus(what);
}

NetGame::DropNotice NetGame::dropNotice() const
{
    DropNotice notice;
    if (!inMatch() || m_dropNotice.empty())
        return notice;
    notice.text = m_dropNotice;
    notice.ageMs = monotonicMs() - m_dropNoticeMs;
    return notice;
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
    FPSLimiter::instance().setTickStretch(1.0f);
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
    //(where the players were drawn: a correction glides from there, GameplayCorrection.h)
    GameplayState::instance().correction().beforeRollback();
    if (snapshot.empty() || snapshot.tick() != frame)
        printf("netplay: ERROR: no snapshot of frame %d to roll back to\n", frame);
    rollback::loadState(snapshot);
    m_timing.loadSeconds += secondsSince(start);
    m_timing.loads++;
}

bool NetGame::advance(const InputFrame& input, bool resimulating)
{
    const Clock::time_point start = Clock::now();
    //(a tick shown before by the run-ahead: seen and heard then, see GameplayTick.cpp)
    const bool shownAhead = !resimulating && g_sim.tick <= sim::detail::aheadTick;
    if (resimulating || shownAhead) {
        sim::ResimulationScope scope;
        sim::detail::rollbackTick = g_sim.tick;    //(its late sounds: Resimulation.h)
        GameplayState::instance().advanceTick(input);
        sim::detail::rollbackTick = -1;
        if (resimulating)
            GameplayState::instance().correction().afterResimulatedTick();
        else
            GameplayState::instance().correction().afterTick();
    } else {
        sim::detail::shownTick = g_sim.tick;
        GameplayState::instance().advanceTick(input);
        GameplayState::instance().correction().afterTick();
    }
    m_timing.simSeconds += secondsSince(start);
    m_timing.simTicks++;
    return matchEndRequested();
}

uint64_t NetGame::checksum()
{
    return peerChecksum();
}

void NetGame::takeOverSlots(uint8_t slotMask)
{
    GameplayState::instance().takeOverByCpu(slotMask);
}

void NetGame::onConfirmedChecksum(int frame, uint64_t sum)
{
    replay::recordChecksum(frame, sum);
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
    printf("netfeel: match %d corrections smoothed %d drawn at once %d\n", m_matchId, GameplayState::instance().correction().smoothed(),
        GameplayState::instance().correction().snapped());
    for (int p = 0; p < config.peerCount; p++) {
        if (remote(p))
            printf("netpeer: match %d peer %d rtt %.0f ms advantage %+.2f\n", m_matchId, p, st.peers[static_cast<size_t>(p)].rttMs,
                st.peers[static_cast<size_t>(p)].advantage);
    }
}

} // namespace rollback

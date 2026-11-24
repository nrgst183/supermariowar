#include "NetLobby.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace rollback {

using net::ByteReader;
using net::ByteWriter;
using net::MsgType;

namespace {
constexpr float TICK_MS = 16.0f;
constexpr int ROLLBACK_FRAMES = 4;    // (a correction glides: GameplayCorrection.h)
constexpr int MIN_DELAY = 0;    // (on a LAN the inputs arrive within a frame: rollback alone hides that)
constexpr int MAX_DELAY = 6;
constexpr uint32_t STATE_INTERVAL_MS = 100;    // at most this often (host)

enum class ByeKind : uint8_t { Leave = 0, Close = 1, Kick = 2 };

std::string cleanName(const std::string& name)
{
    std::string out;
    for (char c : name) {
        if (c >= 32 && c < 127 && out.size() < MAX_NAME_LENGTH)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out;
}

uint16_t clampMs(float ms)
{
    return static_cast<uint16_t>(std::clamp(ms + 0.5f, 0.0f, 65535.0f));
}
} // namespace

int autoInputDelay(float rttMs, float jitterMs)
{
    const float lateMs = std::max(0.0f, rttMs) * 0.5f + std::max(0.0f, jitterMs);
    const int frames = static_cast<int>(std::ceil(lateMs / TICK_MS - 0.001f));
    return std::clamp(frames - ROLLBACK_FRAMES, MIN_DELAY, MAX_DELAY);
}

void RttEstimate::add(float sampleMs)
{
    m_window[m_next] = std::max(0.0f, sampleMs);
    m_next = (m_next + 1) % WINDOW;
    samples = std::min(samples + 1, 255);

    const int n = std::min(samples, WINDOW);
    float sorted[WINDOW];
    std::copy(m_window, m_window + n, sorted);
    std::sort(sorted, sorted + n);
    const int kept = n - n / 4;
    float sum = 0;
    for (int i = 0; i < kept; i++)
        sum += sorted[i];
    rttMs = sum / static_cast<float>(kept);
    float deviation = 0;
    for (int i = 0; i < kept; i++)
        deviation += std::fabs(sorted[i] - rttMs);
    jitterMs = deviation / static_cast<float>(kept);
}

NetLobby::NetLobby(IMeshTransport& transport, const Options& options, uint32_t nowMs)
    : m_mesh(transport, options.mesh, nowMs)
    , m_options(options)
    , m_skins(SkinExchange::Options {options.skinSearchDirs, options.skinDownloadDirs},
          [this](int peer, const uint8_t* data, size_t size) { m_mesh.send(peer, Channel::Reliable, data, size); }, -1)
    , m_lastPingMs(nowMs)
    , m_lastReportMs(nowMs)
    , m_lastChunkMs(nowMs)
{
    //(the host may have none: CPUs only, a game on this machine; one who joins plays)
    m_options.localPlayers = std::clamp(m_options.localPlayers, m_mesh.isHost() ? 0 : 1, MAX_LOCAL_PLAYERS);
    m_options.name = cleanName(m_options.name);
    if (m_mesh.isHost()) {
        m_phase = Phase::Lobby;
        addMember(0);
        m_peers[0].localPlayers = m_options.localPlayers;
        m_peers[0].ready = true;
        rebuildSlots();
        updateInputDelay();
    } else if (m_mesh.state() == NetMesh::State::Failed) {
        m_phase = Phase::Closed;
        m_status = m_mesh.error();
        addEvent(LobbyEvent::Type::Closed, -1, m_status);
    } else {
        m_status = "Joining " + options.mesh.joinHost + ":" + std::to_string(options.mesh.joinPort) + "...";
    }
}

std::string NetLobby::defaultName(int peer) const
{
    return "Player " + std::to_string(peer + 1);
}

int NetLobby::memberCount() const
{
    int count = 0;
    for (const LobbyPeer& p : m_peers)
        count += p.present;
    return count;
}

uint8_t NetLobby::memberMask() const
{
    uint8_t mask = 0;
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (m_peers[static_cast<size_t>(p)].present)
            mask |= static_cast<uint8_t>(1u << p);
    }
    return mask;
}

void NetLobby::addEvent(LobbyEvent::Type type, int peer, const std::string& text)
{
    if (m_eventCount == m_events.size()) {
        m_eventHead = (m_eventHead + 1) % m_events.size();
        m_eventCount--;
    }
    LobbyEvent& e = m_events[(m_eventHead + m_eventCount) % m_events.size()];
    e.type = type;
    e.peer = peer;
    e.text = text;
    m_eventCount++;
}

bool NetLobby::pollEvent(LobbyEvent& event)
{
    if (m_eventCount == 0)
        return false;
    event = m_events[m_eventHead];
    m_eventHead = (m_eventHead + 1) % m_events.size();
    m_eventCount--;
    return true;
}

void NetLobby::setStatus(const std::string& text)
{
    m_status = text;
}

void NetLobby::sendTo(int peer, const ByteWriter& w, Channel channel)
{
    m_mesh.send(peer, channel, w.data(), w.size());
}

void NetLobby::sendToAll(const ByteWriter& w, Channel channel)
{
    m_mesh.sendToAll(channel, w.data(), w.size());
}

// ---- members and slots

void NetLobby::addMember(int peer)
{
    LobbyPeer& p = m_peers[static_cast<size_t>(peer)];
    p = LobbyPeer();
    p.present = true;
    p.name = peer == localPeer() && !m_options.name.empty() ? m_options.name : defaultName(peer);
    p.localPlayers = 1;
    m_rtt[static_cast<size_t>(peer)] = RttEstimate();
    m_reports[static_cast<size_t>(peer)] = Report();
    m_readyFlag[static_cast<size_t>(peer)] = false;

    //A team (and the same color) that nobody has yet
    for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
        bool used[4] = {};
        for (const LobbySlot& s : m_slots) {
            if (s.kind != SlotKind::Empty)
                used[s.team & 3] = true;
        }
        if (i > 0)
            used[m_human[static_cast<size_t>(peer)].team[0] & 3] = true;
        int team = 0;
        while (team < 3 && used[team])
            team++;
        m_human[static_cast<size_t>(peer)].team[i] = static_cast<uint8_t>(team);
        m_human[static_cast<size_t>(peer)].color[i] = static_cast<uint8_t>(team);
    }
}

void NetLobby::rebuildSlots()
{
    m_slots = {};
    int s = 0;
    for (int p = 0; p < net::MAX_PEERS; p++) {
        const LobbyPeer& peer = m_peers[static_cast<size_t>(p)];
        if (!peer.present)
            continue;
        for (int i = 0; i < peer.localPlayers && s < LOBBY_SLOTS; i++) {
            LobbySlot& slot = m_slots[static_cast<size_t>(s++)];
            slot.kind = SlotKind::Human;
            slot.peer = static_cast<int8_t>(p);
            slot.localIndex = static_cast<uint8_t>(i);
            slot.team = m_human[static_cast<size_t>(p)].team[i];
            slot.color = m_human[static_cast<size_t>(p)].color[i];
        }
    }
    m_cpuCount = std::clamp(m_cpuCount, 0, LOBBY_SLOTS - s);
    for (int c = 0; c < m_cpuCount; c++) {
        LobbySlot& slot = m_slots[static_cast<size_t>(s++)];
        slot.kind = SlotKind::Cpu;
        slot.team = m_cpuTeam[c];
        slot.color = m_cpuColor[c];
    }
}

void NetLobby::removePeer(int peer, const std::string& reason, bool tellIt)
{
    if (!isHost() || peer <= 0 || peer >= net::MAX_PEERS)
        return;
    LobbyPeer& p = m_peers[static_cast<size_t>(peer)];
    const bool wasMember = p.present;
    const std::string name = p.name.empty() ? defaultName(peer) : p.name;

    if (tellIt) {
        ByteWriter w(m_buffer.data(), m_buffer.size());
        w.u8(static_cast<uint8_t>(MsgType::Bye));
        w.u8(static_cast<uint8_t>(ByeKind::Kick));
        w.str(reason);
        sendTo(peer, w);
    }
    m_mesh.dropPeer(peer);
    m_skins.dropPeer(peer);
    p = LobbyPeer();
    m_rtt[static_cast<size_t>(peer)] = RttEstimate();
    m_reports[static_cast<size_t>(peer)] = Report();
    m_mapSend[static_cast<size_t>(peer)] = MapSend();
    if (!wasMember)
        return;

    printf("netplay: peer %d (%s) left the lobby: %s\n", peer, name.c_str(), reason.c_str());
    rebuildSlots();
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::PeerLeft));
    w.u8(static_cast<uint8_t>(peer));
    w.str(reason);
    sendToAll(w);
    addEvent(LobbyEvent::Type::PeerLeft, peer, name + " " + reason);
    markDirty();
}

void NetLobby::clearReady()
{
    //The host doesn't clear the others' flags: a ready flag counts for the
    //round it was given in (setRound), so a peer that finished its match
    //earlier and is ready again isn't cleared by mistake
    if (!isHost() && localPeer() >= 0) {
        m_peers[static_cast<size_t>(localPeer())].ready = false;
        m_peerInfoDirty = true;
    }
}

void NetLobby::setRound(uint8_t round)
{
    if (round == m_round)
        return;
    m_round = round;
    if (isHost()) {
        for (int p = 1; p < net::MAX_PEERS; p++)
            m_peers[static_cast<size_t>(p)].ready = m_readyFlag[static_cast<size_t>(p)] && m_readyRound[static_cast<size_t>(p)] == m_round;
        markDirty();
    } else {
        m_peerInfoDirty = true;
    }
}

// ---- actions

void NetLobby::setName(const std::string& name)
{
    const std::string clean = cleanName(name);
    m_options.name = clean;
    if (localPeer() < 0)
        return;
    m_peers[static_cast<size_t>(localPeer())].name = clean.empty() ? defaultName(localPeer()) : clean;
    if (isHost())
        markDirty();
    else
        m_peerInfoDirty = true;
}

void NetLobby::setLocalPlayers(int count)
{
    //(the host may have none: CPUs only, a game on this machine; one who joins plays)
    m_options.localPlayers = std::clamp(count, isHost() ? 0 : 1, MAX_LOCAL_PLAYERS);
    if (localPeer() < 0)
        return;
    LobbyPeer& self = m_peers[static_cast<size_t>(localPeer())];
    self.localPlayers = m_options.localPlayers;
    if (isHost()) {
        rebuildSlots();
        markDirty();
    } else {
        m_peerInfoDirty = true;
    }
}

void NetLobby::setReady(bool ready)
{
    if (isHost() || localPeer() < 0)
        return;
    LobbyPeer& self = m_peers[static_cast<size_t>(localPeer())];
    if (self.ready != ready) {
        self.ready = ready;
        m_peerInfoDirty = true;
    }
}

void NetLobby::setSlotTeam(int slot, int team)
{
    if (slot < 0 || slot >= LOBBY_SLOTS || team < 0 || team > 3)
        return;
    const LobbySlot& s = m_slots[static_cast<size_t>(slot)];
    if (s.kind == SlotKind::Empty)
        return;
    //A team has at most three players
    int members = 0;
    for (int k = 0; k < LOBBY_SLOTS; k++)
        members += k != slot && m_slots[static_cast<size_t>(k)].kind != SlotKind::Empty && m_slots[static_cast<size_t>(k)].team == team;
    if (members >= 3)
        return;

    if (!isHost()) {
        if (s.kind != SlotKind::Human || s.peer != localPeer())
            return;
        ByteWriter w(m_buffer.data(), m_buffer.size());
        w.u8(static_cast<uint8_t>(MsgType::SlotRequest));
        w.u8(static_cast<uint8_t>(slot));
        w.u8(static_cast<uint8_t>(team));
        w.u8(0xff);
        sendTo(0, w);
        m_slots[static_cast<size_t>(slot)].team = static_cast<uint8_t>(team);    //shown until the host's answer
        return;
    }
    if (s.kind == SlotKind::Human) {
        m_human[static_cast<size_t>(s.peer)].team[s.localIndex] = static_cast<uint8_t>(team);
    } else {
        int humans = 0;
        for (const LobbySlot& k : m_slots)
            humans += k.kind == SlotKind::Human;
        m_cpuTeam[slot - humans] = static_cast<uint8_t>(team);
    }
    rebuildSlots();
    markDirty();
}

void NetLobby::setSlotColor(int slot, int color)
{
    if (slot < 0 || slot >= LOBBY_SLOTS || color < 0 || color > 3)
        return;
    const LobbySlot& s = m_slots[static_cast<size_t>(slot)];
    if (s.kind == SlotKind::Empty)
        return;
    if (!isHost()) {
        if (s.kind != SlotKind::Human || s.peer != localPeer())
            return;
        ByteWriter w(m_buffer.data(), m_buffer.size());
        w.u8(static_cast<uint8_t>(MsgType::SlotRequest));
        w.u8(static_cast<uint8_t>(slot));
        w.u8(0xff);
        w.u8(static_cast<uint8_t>(color));
        sendTo(0, w);
        m_slots[static_cast<size_t>(slot)].color = static_cast<uint8_t>(color);
        return;
    }
    if (s.kind == SlotKind::Human) {
        m_human[static_cast<size_t>(s.peer)].color[s.localIndex] = static_cast<uint8_t>(color);
    } else {
        int humans = 0;
        for (const LobbySlot& k : m_slots)
            humans += k.kind == SlotKind::Human;
        m_cpuColor[slot - humans] = static_cast<uint8_t>(color);
    }
    rebuildSlots();
    markDirty();
}

void NetLobby::addCpu()
{
    if (!isHost())
        return;
    int used = 0;
    bool teams[4] = {};
    for (const LobbySlot& s : m_slots) {
        if (s.kind != SlotKind::Empty) {
            used++;
            teams[s.team & 3] = true;
        }
    }
    if (used >= LOBBY_SLOTS)
        return;
    int team = 0;
    while (team < 3 && teams[team])
        team++;
    m_cpuTeam[m_cpuCount] = static_cast<uint8_t>(team);
    m_cpuColor[m_cpuCount] = static_cast<uint8_t>(team);
    m_cpuCount++;
    rebuildSlots();
    markDirty();
}

void NetLobby::removeCpu(int slot)
{
    if (!isHost() || slot < 0 || slot >= LOBBY_SLOTS || m_slots[static_cast<size_t>(slot)].kind != SlotKind::Cpu)
        return;
    int humans = 0;
    for (const LobbySlot& k : m_slots)
        humans += k.kind == SlotKind::Human;
    for (int c = slot - humans; c + 1 < m_cpuCount; c++) {
        m_cpuTeam[c] = m_cpuTeam[c + 1];
        m_cpuColor[c] = m_cpuColor[c + 1];
    }
    m_cpuCount--;
    rebuildSlots();
    markDirty();
}

void NetLobby::setCpuCount(int count)
{
    if (!isHost())
        return;
    count = std::clamp(count, 0, LOBBY_SLOTS);
    while (m_cpuCount > count) {
        int last = -1;
        for (int s = 0; s < LOBBY_SLOTS; s++) {
            if (m_slots[static_cast<size_t>(s)].kind == SlotKind::Cpu)
                last = s;
        }
        if (last < 0)
            break;
        removeCpu(last);
    }
    for (int guard = 0; m_cpuCount < count && guard < LOBBY_SLOTS; guard++) {
        const int before = m_cpuCount;
        addCpu();
        if (m_cpuCount == before)
            break;
    }
}

void NetLobby::kick(int peer)
{
    if (!isHost() || peer <= 0 || peer >= net::MAX_PEERS || m_phase == Phase::InMatch)
        return;
    removePeer(peer, "was removed by the host", true);
}

void NetLobby::setInputDelay(int delay)
{
    if (!isHost())
        return;
    m_delayOverride = delay < 0 ? -1 : std::clamp(delay, 0, 8);
    updateInputDelay();
    markDirty();
}

void NetLobby::setSettingsEveryone(bool everyone)
{
    if (!isHost() || everyone == m_settingsEveryone)
        return;
    m_settingsEveryone = everyone;
    markDirty();
}

void NetLobby::requestSetting(uint8_t what, int value, const std::string& text)
{
    if (isHost() || localPeer() < 0 || !m_settingsEveryone || m_phase != Phase::Lobby)
        return;
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::SettingsRequest));
    w.u8(what);
    w.i16(static_cast<int16_t>(value));
    w.str(text);
    sendTo(0, w);
}

void NetLobby::setMatchInfo(const std::string& mode, const std::string& map)
{
    if (mode != m_modeName || map != m_mapName) {
        m_modeName = mode;
        m_mapName = map;
        markDirty();
    }
}

void NetLobby::setMapCheck(const std::string& ref, uint64_t hash)
{
    if (ref != m_mapRef || hash != m_mapHash) {
        m_mapRef = ref;
        m_mapHash = hash;
        markDirty();    //(the peers' lacking it is about this map now: LOBBY_STATE says)
    }
}

void NetLobby::setLackedMap(uint64_t hash)
{
    if (hash == m_lackedMap)
        return;
    m_lackedMap = hash;
    m_peerInfoDirty = true;
}

bool NetLobby::peerLacksMap(int peer) const
{
    if (peer < 0 || peer >= net::MAX_PEERS || !m_peers[static_cast<size_t>(peer)].present)
        return false;
    if (peer == localPeer())
        return m_lackedMap != 0 && m_lackedMap == m_mapHash;
    //(the host: what it said it lacks is still the lobby's map; the others: what the host says)
    const LobbyPeer& p = m_peers[static_cast<size_t>(peer)];
    return isHost() ? p.lackedMap != 0 && p.lackedMap == m_mapHash : p.lacksMap;
}

void NetLobby::setMatchDetail(int modeId, int goal, const std::string& goalName)
{
    if (modeId != m_modeId || goal != m_goal || goalName != m_goalName) {
        m_modeId = modeId;
        m_goal = goal;
        m_goalName = goalName;
        markDirty();
    }
}

void NetLobby::setPhase(Phase phase)
{
    if (m_phase == Phase::Closed || phase == m_phase)
        return;
    m_phase = phase;
    if (isHost())
        m_mesh.setRefuseJoins(phase == Phase::Lobby && !m_seriesLocked ? "" : "a match is in progress, try again in a moment");
    if (phase != Phase::Starting)
        cancelMapTransfers();
}

void NetLobby::setSeriesLocked(bool locked)
{
    m_seriesLocked = locked;
    if (isHost() && m_phase != Phase::Closed)
        m_mesh.setRefuseJoins(m_phase == Phase::Lobby && !locked ? "" : "a match is in progress, try again in a moment");
}

void NetLobby::sendGameMessage(int peer, const uint8_t* data, size_t size)
{
    if (peer < 0)
        m_mesh.sendToAll(Channel::Reliable, data, size);
    else
        m_mesh.send(peer, Channel::Reliable, data, size);
}

void NetLobby::leave(const std::string& reason, bool abortMatch)
{
    if (m_phase == Phase::Closed)
        return;
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Bye));
    w.u8(static_cast<uint8_t>(isHost() && !abortMatch ? ByeKind::Close : ByeKind::Leave));
    w.str(reason);
    sendToAll(w);
    for (int p = 0; p < net::MAX_PEERS; p++)
        m_mesh.dropPeer(p);
    printf("netplay: leaving the lobby (%s)\n", reason.c_str());
    m_phase = Phase::Closed;
    m_status = isHost() ? "You closed the lobby" : "You left the lobby";
    cancelMapTransfers();
    addEvent(LobbyEvent::Type::Closed, -1, m_status);
}

void NetLobby::close(const std::string& reason, LobbyGameEvent& event, bool finishMatch)
{
    printf("netplay: the lobby closed: %s\n", reason.c_str());
    m_phase = Phase::Closed;
    m_status = reason;
    cancelMapTransfers();
    addEvent(LobbyEvent::Type::Closed, -1, reason);
    event = LobbyGameEvent();
    event.type = LobbyGameEvent::Type::Closed;
    event.text = reason;
    event.ok = finishMatch;
}

// ---- checks

bool NetLobby::everyoneReady(std::string& why) const
{
    if (!isHost()) {
        why = "only the host starts matches";
        return false;
    }
    if (m_phase != Phase::Lobby) {
        why = "a match is already starting";
        return false;
    }
    if (m_mesh.joiningPeer() >= 0) {
        why = "a player is joining";
        return false;
    }
    for (int p = 1; p < net::MAX_PEERS; p++) {
        const LobbyPeer& peer = m_peers[static_cast<size_t>(p)];
        if (!peer.present)
            continue;
        if (!peer.ready) {
            why = "waiting for " + peer.name + " to be ready";
            return false;
        }
        const Report& report = m_reports[static_cast<size_t>(p)];
        if (!report.valid) {
            why = "checking the connection of " + peer.name;
            return false;
        }
        for (int q = 1; q < net::MAX_PEERS; q++) {
            if (q != p && m_peers[static_cast<size_t>(q)].present && !(report.connectedMask & (1u << q))) {
                why = peer.name + " can't reach " + m_peers[static_cast<size_t>(q)].name;
                return false;
            }
        }
    }
    why.clear();
    return true;
}

bool NetLobby::canStart(std::string& why) const
{
    //(first: the host chooses another map, nobody needs to get ready for it)
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (peerLacksMap(p)) {
            const std::string& name = m_peers[static_cast<size_t>(p)].name;
            why = (name.empty() ? defaultName(p) : name) + " doesn't have this map and has map downloads turned off";
            return false;
        }
    }
    if (!everyoneReady(why))
        return false;
    int players = 0;
    int firstTeam = -1;
    bool twoTeams = false;
    for (const LobbySlot& s : m_slots) {
        if (s.kind == SlotKind::Empty)
            continue;
        players++;
        if (firstTeam < 0)
            firstTeam = s.team;
        else if (s.team != firstTeam)
            twoTeams = true;
    }
    if (players < 2) {
        why = "at least two players are needed (add a CPU player?)";
        return false;
    }
    //(CPUs only: a game on this machine; a match with others needs the host's players in it too)
    if (localPeer() >= 0 && m_peers[static_cast<size_t>(localPeer())].localPlayers == 0) {
        for (int p = 0; p < net::MAX_PEERS; p++) {
            if (p != localPeer() && m_peers[static_cast<size_t>(p)].present) {
                why = "set My players to 1 or more to play with the others (CPUs only is for a game here)";
                return false;
            }
        }
    }
    if (!twoTeams) {
        why = "all players are on the same team";
        return false;
    }
    why.clear();
    return true;
}

bool NetLobby::pingsMeasured() const
{
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (!m_peers[static_cast<size_t>(p)].present)
            continue;
        if (m_rtt[static_cast<size_t>(p)].samples < MIN_PING_SAMPLES)
            return false;
        const Report& report = m_reports[static_cast<size_t>(p)];
        for (int q = 1; q < net::MAX_PEERS; q++) {
            if (q != p && m_peers[static_cast<size_t>(q)].present && (!report.valid || report.rtt[q].samples < MIN_PING_SAMPLES))
                return false;
        }
    }
    return true;
}

void NetLobby::worstRtt(float& rttMs, float& jitterMs) const
{
    rttMs = 0;
    jitterMs = 0;
    auto take = [&](const RttEstimate& e) {
        if (e.samples > 0) {
            rttMs = std::max(rttMs, e.rttMs);
            jitterMs = std::max(jitterMs, e.jitterMs);
        }
    };
    for (int p = 1; p < net::MAX_PEERS; p++) {
        if (!m_peers[static_cast<size_t>(p)].present)
            continue;
        take(m_rtt[static_cast<size_t>(p)]);
        const Report& report = m_reports[static_cast<size_t>(p)];
        if (!report.valid)
            continue;
        for (int q = 1; q < net::MAX_PEERS; q++) {
            if (q != p && m_peers[static_cast<size_t>(q)].present)
                take(report.rtt[q]);
        }
    }
}

void NetLobby::updateInputDelay()
{
    if (!isHost())
        return;
    int delay;
    if (m_delayOverride >= 0) {
        delay = m_delayOverride;
    } else {
        float rtt = 0;
        float jitter = 0;
        worstRtt(rtt, jitter);
        delay = autoInputDelay(rtt, jitter);
    }
    const bool isAuto = m_delayOverride < 0;
    if (delay != m_inputDelay || isAuto != m_delayIsAuto) {
        m_inputDelay = delay;
        m_delayIsAuto = isAuto;
        markDirty();
    }
}

// ---- messages out

void NetLobby::sendPeerInfo()
{
    if (localPeer() < 0)
        return;
    const LobbyPeer& self = m_peers[static_cast<size_t>(localPeer())];
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::PeerInfo));
    w.str(self.name);
    w.u8(static_cast<uint8_t>(m_options.localPlayers));
    w.u8(self.ready ? 1 : 0);
    w.u8(m_round);
    w.u64(m_lackedMap);
    sendTo(0, w);
    m_peerInfoDirty = false;
}

void NetLobby::sendLobbyState()
{
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::LobbyState));
    w.u8(m_round);
    w.u8(static_cast<uint8_t>(m_inputDelay));
    w.u8(m_delayIsAuto ? 1 : 0);
    w.str(m_modeName);
    w.str(m_mapName);
    w.u8(memberMask());
    for (const LobbyPeer& p : m_peers) {
        if (!p.present)
            continue;
        w.str(p.name);
        w.u8(static_cast<uint8_t>(p.localPlayers));
        w.u8(p.ready ? 1 : 0);
    }
    for (const LobbySlot& s : m_slots) {
        w.u8(static_cast<uint8_t>(s.kind));
        w.i8(s.peer);
        w.u8(s.localIndex);
        w.u8(s.team);
        w.u8(s.color);
    }
    uint8_t lacking = 0;
    for (int p = 0; p < net::MAX_PEERS; p++)
        lacking |= static_cast<uint8_t>((peerLacksMap(p) ? 1u : 0u) << p);
    w.u8(lacking);
    sendToAll(w);

    ByteWriter extra(m_buffer.data(), m_buffer.size());
    extra.u8(static_cast<uint8_t>(MsgType::LobbyExtra));
    extra.i16(static_cast<int16_t>(m_modeId));
    extra.i16(static_cast<int16_t>(m_goal));
    extra.str(m_goalName);
    extra.u8(m_settingsEveryone ? 1 : 0);
    extra.str(m_mapRef);
    extra.u64(m_mapHash);
    sendToAll(extra);
    m_dirty = false;
}

void NetLobby::sendConfig(uint8_t matchId, const std::vector<uint8_t>& config)
{
    std::vector<uint8_t> buffer(config.size() + 4);
    ByteWriter w(buffer.data(), buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Config));
    w.u8(matchId);
    w.u16(static_cast<uint16_t>(config.size()));
    w.bytes(config.data(), config.size());
    sendToAll(w);
}

void NetLobby::sendConfigAck(uint8_t matchId, uint8_t status, const std::string& reason)
{
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::ConfigAck));
    w.u8(matchId);
    w.u8(status);
    w.str(reason);
    sendTo(0, w);
}

void NetLobby::sendStart(uint8_t matchId)
{
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::Start));
    w.u8(matchId);
    sendToAll(w);
}

void NetLobby::sendMatchAbort(uint8_t matchId, int culprit, const std::string& reason)
{
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::MatchAbort));
    w.u8(matchId);
    w.u8(static_cast<uint8_t>(culprit));
    w.str(reason);
    sendToAll(w);
}

void NetLobby::sendMap(int peer, uint8_t matchId, std::shared_ptr<const std::vector<uint8_t>> file, uint64_t hash, const std::string& name)
{
    if (!isHost() || peer <= 0 || peer >= net::MAX_PEERS || !file || file->empty() || file->size() > MAP_MAX_BYTES)
        return;
    ByteWriter w(m_buffer.data(), m_buffer.size());
    w.u8(static_cast<uint8_t>(MsgType::MapBegin));
    w.u8(matchId);
    w.u32(static_cast<uint32_t>(file->size()));
    w.u64(hash);
    w.str(name);
    sendTo(peer, w);

    MapSend& send = m_mapSend[static_cast<size_t>(peer)];
    send.active = true;
    send.matchId = matchId;
    send.file = std::move(file);
    send.offset = 0;
    printf("netplay: sending the map %s (%zu bytes) to peer %d\n", name.c_str(), send.file->size(), peer);
    updateTransferInfo();
    m_transferInfo.mapName = name;
}

void NetLobby::cancelMapTransfers()
{
    for (MapSend& send : m_mapSend)
        send = MapSend();
    if (m_mapReceive.active) {
        m_mapReceive = MapReceive();
    }
    m_transferInfo = MapTransferInfo();
}

void NetLobby::sendMapChunks(uint32_t nowMs)
{
    const uint32_t slowdown = static_cast<uint32_t>(std::max(1, m_options.mapSlowdown));
    const uint32_t frames = (nowMs - m_lastChunkMs) / 16;
    if (frames < slowdown)
        return;
    m_lastChunkMs = nowMs;
    const int budget = slowdown > 1 ? 1 : MAP_CHUNKS_PER_FRAME * static_cast<int>(std::min<uint32_t>(frames, 4));

    bool any = false;
    for (int p = 1; p < net::MAX_PEERS; p++) {
        MapSend& send = m_mapSend[static_cast<size_t>(p)];
        if (!send.active)
            continue;
        any = true;
        uint8_t chunk[MAP_CHUNK_BYTES + 16];
        for (int i = 0; i < budget && send.offset < send.file->size(); i++) {
            const size_t len = std::min(MAP_CHUNK_BYTES, send.file->size() - send.offset);
            ByteWriter w(chunk, sizeof(chunk));
            w.u8(static_cast<uint8_t>(MsgType::MapChunk));
            w.u8(send.matchId);
            w.u32(send.offset);
            w.u16(static_cast<uint16_t>(len));
            w.bytes(send.file->data() + send.offset, len);
            sendTo(p, w);
            send.offset += static_cast<uint32_t>(len);
        }
        if (send.offset >= send.file->size())
            send.active = false;
    }
    if (any)
        updateTransferInfo();
}

void NetLobby::updateTransferInfo()
{
    if (isHost()) {
        uint32_t done = 0;
        uint32_t total = 0;
        bool active = false;
        for (const MapSend& send : m_mapSend) {
            if (!send.active)
                continue;
            active = true;
            done += send.offset;
            total += static_cast<uint32_t>(send.file->size());
        }
        m_transferInfo.active = active;
        m_transferInfo.sending = true;
        m_transferInfo.bytesDone = done;
        m_transferInfo.bytesTotal = total;
    } else {
        m_transferInfo.active = m_mapReceive.active;
        m_transferInfo.sending = false;
        m_transferInfo.bytesDone = static_cast<uint32_t>(m_mapReceive.bytes.size());
        m_transferInfo.bytesTotal = m_mapReceive.size;
    }
}

// ---- messages in

void NetLobby::onPeerInfo(int peer, ByteReader& r)
{
    const std::string name = cleanName(r.str());
    int localPlayers = r.u8();
    const bool ready = r.u8() != 0;
    const uint8_t round = r.u8();
    LobbyPeer& p = m_peers[static_cast<size_t>(peer)];
    if (!r.ok() || !p.present)
        return;
    const uint64_t lacked = r.u64();    //(last: a build without it doesn't send it)
    p.lackedMap = r.ok() ? lacked : 0;
    m_readyFlag[static_cast<size_t>(peer)] = ready;
    m_readyRound[static_cast<size_t>(peer)] = round;

    //Only as many players as there are free slots
    int others = 0;
    for (int q = 0; q < net::MAX_PEERS; q++) {
        if (q != peer && m_peers[static_cast<size_t>(q)].present)
            others += m_peers[static_cast<size_t>(q)].localPlayers;
    }
    localPlayers = std::clamp(localPlayers, 1, std::max(1, std::min(MAX_LOCAL_PLAYERS, LOBBY_SLOTS - others)));

    p.name = name.empty() ? defaultName(peer) : name;
    p.localPlayers = localPlayers;
    p.ready = ready && round == m_round;
    rebuildSlots();
    markDirty();
}

void NetLobby::onSlotRequest(int peer, ByteReader& r)
{
    const int slot = r.u8();
    const int team = r.u8();
    const int color = r.u8();
    if (!r.ok() || slot >= LOBBY_SLOTS)
        return;
    const LobbySlot& s = m_slots[static_cast<size_t>(slot)];
    if (s.kind != SlotKind::Human || s.peer != peer)
        return;
    if (team != 0xff)
        setSlotTeam(slot, team);
    if (color != 0xff)
        setSlotColor(slot, color);
}

void NetLobby::onLobbyState(ByteReader& r)
{
    const uint8_t round = r.u8();
    const int delay = r.u8();
    const bool isAuto = r.u8() != 0;
    std::string mode = r.str();
    std::string map = r.str();
    const uint8_t mask = r.u8();

    std::array<LobbyPeer, net::MAX_PEERS> peers {};
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (!(mask & (1u << p)))
            continue;
        LobbyPeer& peer = peers[static_cast<size_t>(p)];
        peer.present = true;
        peer.name = r.str();
        peer.localPlayers = r.u8();
        peer.ready = r.u8() != 0;
    }
    std::array<LobbySlot, LOBBY_SLOTS> slots {};
    for (LobbySlot& s : slots) {
        s.kind = static_cast<SlotKind>(std::min<uint8_t>(r.u8(), 2));
        s.peer = r.i8();
        s.localIndex = r.u8();
        s.team = static_cast<uint8_t>(r.u8() & 3);
        s.color = static_cast<uint8_t>(r.u8() & 3);
    }
    if (!r.ok())
        return;
    const uint8_t lackingByte = r.u8();
    const uint8_t lacking = r.ok() ? lackingByte : 0;

    //A new joiner learns which round it is (at a match start, NetGame sets it)
    if (m_phase == Phase::Lobby && static_cast<int8_t>(round - m_round) > 0) {
        m_round = round;
        m_peerInfoDirty = true;
    }
    m_inputDelay = delay;
    m_delayIsAuto = isAuto;
    m_modeName = std::move(mode);
    m_mapName = std::move(map);
    for (int p = 0; p < net::MAX_PEERS; p++) {
        LobbyPeer& mine = m_peers[static_cast<size_t>(p)];
        LobbyPeer& theirs = peers[static_cast<size_t>(p)];
        if (theirs.present && !mine.present && p != localPeer())
            addEvent(LobbyEvent::Type::PeerJoined, p, theirs.name);
        if (p == localPeer()) {
            //Our own name and ready flag are what we last sent
            theirs.name = mine.name;
            theirs.ready = mine.ready;
            theirs.present = true;
        }
        theirs.lacksMap = ((lacking >> p) & 1) != 0;
        theirs.pingMs = mine.pingMs;
        mine = std::move(theirs);
    }
    m_slots = slots;
}

bool NetLobby::onMapBegin(ByteReader& r, LobbyGameEvent& event)
{
    const uint8_t matchId = r.u8();
    const uint32_t size = r.u32();
    const uint64_t hash = r.u64();
    const std::string name = r.str();
    if (!r.ok())
        return false;
    if (size == 0 || size > MAP_MAX_BYTES) {
        event = LobbyGameEvent();
        event.type = LobbyGameEvent::Type::MapReceived;
        event.ok = false;
        event.text = "the map is too large (" + std::to_string(size) + " bytes, at most " + std::to_string(MAP_MAX_BYTES) + ")";
        return true;
    }
    m_mapReceive = MapReceive();
    m_mapReceive.active = true;
    m_mapReceive.matchId = matchId;
    m_mapReceive.size = size;
    m_mapReceive.hash = hash;
    m_mapReceive.name = name;
    m_mapReceive.bytes.reserve(size);
    m_transferInfo.mapName = name;
    m_status = "Downloading the map " + safeMapFileName(name);
    printf("netplay: downloading the map %s (%u bytes)\n", name.c_str(), size);
    updateTransferInfo();
    return false;
}

bool NetLobby::onMapChunk(ByteReader& r, LobbyGameEvent& event)
{
    const uint8_t matchId = r.u8();
    const uint32_t offset = r.u32();
    const uint16_t len = r.u16();
    const uint8_t* data = r.bytes(len);
    if (!r.ok() || !m_mapReceive.active || matchId != m_mapReceive.matchId)
        return false;

    MapReceive& rx = m_mapReceive;
    event = LobbyGameEvent();
    event.type = LobbyGameEvent::Type::MapReceived;
    if (offset != rx.bytes.size() || len == 0 || rx.bytes.size() + len > rx.size) {
        event.text = "the map transfer went wrong";
        rx = MapReceive();
        updateTransferInfo();
        return true;
    }
    rx.bytes.insert(rx.bytes.end(), data, data + len);
    updateTransferInfo();
    if (rx.bytes.size() < rx.size)
        return false;

    //Complete
    std::string error = "nowhere to save it";
    std::string path;
    if (net::fnv1a64(rx.bytes.data(), rx.bytes.size()) != rx.hash) {
        error = "the received map is damaged (wrong hash)";
    } else {
        for (const std::string& dir : m_options.downloadDirs) {
            if (saveDownloadedMap(dir, rx.name, rx.bytes, rx.hash, path, error))
                break;
            path.clear();
        }
    }
    event.ok = !path.empty();
    event.text = event.ok ? path : error;
    if (event.ok)
        printf("netplay: saved the map as %s\n", path.c_str());
    else
        printf("netplay: ERROR: map download: %s\n", error.c_str());
    m_status = event.ok ? "" : "Map download failed: " + error;
    rx = MapReceive();
    updateTransferInfo();
    return true;
}

bool NetLobby::handleMessage(int peer, Channel channel, const uint8_t* data, size_t size, LobbyGameEvent& event, uint32_t nowMs)
{
    if (size == 0)
        return false;
    ByteReader r(data + 1, size - 1);
    const auto type = static_cast<MsgType>(data[0]);
    const bool fromHost = peer == 0;

    switch (type) {
        case MsgType::Ping: {
            const uint32_t time = r.u32();
            if (!r.ok())
                break;
            uint8_t msg[5];
            ByteWriter w(msg, sizeof(msg));
            w.u8(static_cast<uint8_t>(MsgType::Pong));
            w.u32(time);
            sendTo(peer, w, Channel::Unreliable);
            break;
        }
        case MsgType::Pong: {
            const uint32_t time = r.u32();
            if (!r.ok() || static_cast<int32_t>(nowMs - time) < 0 || nowMs - time > 10000)
                break;
            RttEstimate& rtt = m_rtt[static_cast<size_t>(peer)];
            rtt.add(static_cast<float>(nowMs - time));
            m_peers[static_cast<size_t>(peer)].pingMs = static_cast<int>(rtt.rttMs + 0.5f);
            break;
        }
        case MsgType::RttReport: {
            if (!isHost())
                break;
            Report report;
            report.connectedMask = r.u8();
            const int n = r.u8();
            for (int i = 0; i < n; i++) {
                const int q = r.u8();
                RttEstimate e;
                e.rttMs = r.u16();
                e.jitterMs = r.u16();
                e.samples = r.u8();
                if (q < net::MAX_PEERS)
                    report.rtt[q] = e;
            }
            if (!r.ok())
                break;
            report.valid = true;
            report.receivedMs = nowMs;
            m_reports[static_cast<size_t>(peer)] = report;
            break;
        }
        case MsgType::PeerInfo:
            if (isHost())
                onPeerInfo(peer, r);
            break;
        case MsgType::SlotRequest:
            if (isHost() && m_phase == Phase::Lobby)
                onSlotRequest(peer, r);
            break;
        case MsgType::LobbyState:
            if (fromHost && !isHost())
                onLobbyState(r);
            break;
        case MsgType::PeerLeft: {
            if (!fromHost || isHost())
                break;
            const int gone = r.u8();
            const std::string reason = r.str();
            if (!r.ok() || gone <= 0 || gone >= net::MAX_PEERS || gone == localPeer())
                break;
            const std::string name = m_peers[static_cast<size_t>(gone)].name;
            m_mesh.dropPeer(gone);
            m_skins.dropPeer(gone);
            m_peers[static_cast<size_t>(gone)] = LobbyPeer();
            m_rtt[static_cast<size_t>(gone)] = RttEstimate();
            addEvent(LobbyEvent::Type::PeerLeft, gone, (name.empty() ? defaultName(gone) : name) + " " + reason);
            event = LobbyGameEvent();
            event.type = LobbyGameEvent::Type::PeerGone;
            event.peer = gone;
            event.text = reason;
            return true;
        }
        case MsgType::Bye: {
            const auto kind = static_cast<ByeKind>(r.u8());
            const std::string reason = r.str();
            if (kind == ByeKind::Leave) {
                if (isHost()) {
                    removePeer(peer, "left", false);
                } else if (fromHost) {
                    close("The host left the lobby", event, false);
                    return true;
                }
                event = LobbyGameEvent();
                event.type = LobbyGameEvent::Type::PeerGone;
                event.peer = peer;
                event.text = "left";
                return true;
            }
            if (!fromHost || isHost())
                break;
            if (kind == ByeKind::Kick)
                close("You were removed from the lobby (" + reason + ")", event, false);
            else
                close("The host closed the lobby", event, true);
            return true;
        }
        case MsgType::MapBegin:
            if (fromHost && !isHost())
                return onMapBegin(r, event);
            break;
        case MsgType::MapChunk:
            if (fromHost && !isHost())
                return onMapChunk(r, event);
            break;

        case MsgType::LobbyExtra:
            if (fromHost && !isHost()) {
                const int modeId = r.i16();
                const int goal = r.i16();
                std::string goalName = r.str();
                const bool everyone = r.u8() != 0;
                if (r.ok()) {
                    m_settingsEveryone = everyone;
                    m_modeId = modeId;
                    m_goal = goal;
                    m_goalName = std::move(goalName);
                    std::string mapRef = r.str();
                    const uint64_t mapHash = r.u64();
                    if (r.ok()) {
                        m_mapRef = std::move(mapRef);
                        m_mapHash = mapHash;
                    }
                }
            }
            break;

        case MsgType::SettingsRequest: {
            const uint8_t what = r.u8();
            const int number = r.i16();
            const std::string text = r.str();
            if (!r.ok() || !isHost() || !m_settingsEveryone || m_phase != Phase::Lobby || !m_peers[static_cast<size_t>(peer & 3)].present)
                break;
            if (what < SETTING_MODE || what > SETTING_MAP)
                break;
            event = LobbyGameEvent();
            event.type = LobbyGameEvent::Type::SettingsRequest;
            event.peer = peer;
            event.size = what;
            event.value = number;
            event.text = text.substr(0, 120);
            return true;
        }

        case MsgType::SkinInfo:
        case MsgType::SkinRequest:
        case MsgType::SkinBegin:
        case MsgType::SkinChunk:
            //Cosmetic and best effort, in the lobby and during matches
            m_skins.handle(peer, type, r);
            break;

        case MsgType::Config:
        case MsgType::ConfigAck:
        case MsgType::Start:
        case MsgType::MatchAbort:
        case MsgType::Input:
        case MsgType::Checksum:
        case MsgType::PeerDrop:
            event = LobbyGameEvent();
            event.type = LobbyGameEvent::Type::Message;
            event.peer = peer;
            event.data = data;
            event.size = size;
            (void)channel;
            return true;

        default:
            //The multi-match modes (NetSeries.h): NetGame's
            if (data[0] >= static_cast<uint8_t>(MsgType::SeriesBegin) && data[0] < net::SERIES_MSG_END) {
                event = LobbyGameEvent();
                event.type = LobbyGameEvent::Type::Message;
                event.peer = peer;
                event.data = data;
                event.size = size;
                return true;
            }
            break;
    }
    return false;
}

bool NetLobby::handleMesh(const NetMesh::Event& me, LobbyGameEvent& event, uint32_t nowMs)
{
    using Type = NetMesh::Event::Type;
    switch (me.type) {
        case Type::Joined: {
            m_phase = Phase::Lobby;
            addMember(localPeer());
            m_peers[static_cast<size_t>(localPeer())].localPlayers = m_options.localPlayers;
            m_status.clear();
            sendPeerInfo();
            m_lastReportMs = nowMs - REPORT_INTERVAL_MS / 2;
            addEvent(LobbyEvent::Type::Joined, localPeer(), "");
            return false;
        }
        case Type::PeerJoined:
            addMember(me.peer);
            rebuildSlots();
            markDirty();
            addEvent(LobbyEvent::Type::PeerJoined, me.peer, m_peers[static_cast<size_t>(me.peer)].name);
            return false;

        case Type::PeerLost:
            if (!isHost() && me.peer == 0) {
                close("Lost the connection to the host", event, false);
                return true;
            }
            m_rtt[static_cast<size_t>(me.peer)] = RttEstimate();
            m_peers[static_cast<size_t>(me.peer)].pingMs = -1;
            m_skins.dropPeer(me.peer);
            if (isHost()) {
                if (!m_peers[static_cast<size_t>(me.peer)].present)
                    return false;
                removePeer(me.peer, "lost the connection", false);
            }
            event = LobbyGameEvent();
            event.type = LobbyGameEvent::Type::PeerGone;
            event.peer = me.peer;
            event.text = "lost the connection";
            return true;

        case Type::Message:
            return handleMessage(me.peer, me.channel, me.data, me.size, event, nowMs);

        default:
            return false;
    }
}

// The lobby is closed, a match may still run: its messages, and the peers that leave it
bool NetLobby::pollClosed(const NetMesh::Event& me, LobbyGameEvent& event)
{
    if (me.type == NetMesh::Event::Type::PeerLost) {
        event = LobbyGameEvent();
        event.type = LobbyGameEvent::Type::PeerGone;
        event.peer = me.peer;
        event.text = "lost the connection";
        return true;
    }
    if (me.type != NetMesh::Event::Type::Message || me.size == 0)
        return false;
    const auto type = static_cast<MsgType>(me.data[0]);
    if (type == MsgType::Bye) {
        ByteReader r(me.data + 1, me.size - 1);
        if (static_cast<ByeKind>(r.u8()) != ByeKind::Leave || !r.ok())
            return false;
        event = LobbyGameEvent();
        event.type = LobbyGameEvent::Type::PeerGone;
        event.peer = me.peer;
        event.text = "left";
        return true;
    }
    if (type != MsgType::Input && type != MsgType::Checksum && type != MsgType::PeerDrop && type != MsgType::MatchAbort)
        return false;
    event = LobbyGameEvent();
    event.type = LobbyGameEvent::Type::Message;
    event.peer = me.peer;
    event.data = me.data;
    event.size = me.size;
    return true;
}

bool NetLobby::poll(LobbyGameEvent& event, uint32_t nowMs)
{
    NetMesh::Event me;
    if (m_phase == Phase::Closed) {
        //Keep the transport serviced (what is queued still goes out). A match
        //that goes on (the host let it finish, or is gone) still gets its
        //messages, and hears of the peers that leave it.
        while (m_mesh.poll(me, nowMs)) {
            if (pollClosed(me, event))
                return true;
        }
        return false;
    }
    while (m_mesh.poll(me, nowMs)) {
        if (handleMesh(me, event, nowMs))
            return true;
        if (m_phase == Phase::Closed)
            return false;
    }
    if (m_mesh.state() == NetMesh::State::Failed) {
        close(m_mesh.error().empty() ? "Lost the connection" : "Could not join: " + m_mesh.error(), event, false);
        return true;
    }
    return false;
}

void NetLobby::update(uint32_t nowMs)
{
    if (m_phase == Phase::Closed)
        return;

    if (localPeer() >= 0) {
        uint8_t connected = 0;
        for (int p = 0; p < net::MAX_PEERS; p++) {
            if (p != localPeer() && m_mesh.peerConnected(p))
                connected |= static_cast<uint8_t>(1u << p);
        }
        m_skins.setLocalPeer(localPeer());
        m_skins.update(nowMs, connected);
    }

    //Round trips, measured outside matches (the session measures its own)
    if (m_phase != Phase::InMatch && nowMs - m_lastPingMs >= PING_INTERVAL_MS) {
        m_lastPingMs = nowMs;
        uint8_t msg[5];
        ByteWriter w(msg, sizeof(msg));
        w.u8(static_cast<uint8_t>(MsgType::Ping));
        w.u32(nowMs);
        for (int p = 0; p < net::MAX_PEERS; p++) {
            if (p != localPeer() && m_mesh.peerConnected(p))
                sendTo(p, w, Channel::Unreliable);
        }
    }

    if (!isHost()) {
        if (m_phase == Phase::Joining)
            return;
        if (m_peerInfoDirty)
            sendPeerInfo();
        if (m_phase != Phase::InMatch && nowMs - m_lastReportMs >= REPORT_INTERVAL_MS) {
            m_lastReportMs = nowMs;
            ByteWriter w(m_buffer.data(), m_buffer.size());
            w.u8(static_cast<uint8_t>(MsgType::RttReport));
            uint8_t mask = 0;
            int n = 0;
            for (int p = 0; p < net::MAX_PEERS; p++) {
                if (p != localPeer() && m_mesh.peerConnected(p)) {
                    mask |= static_cast<uint8_t>(1u << p);
                    n++;
                }
            }
            w.u8(mask);
            w.u8(static_cast<uint8_t>(n));
            for (int p = 0; p < net::MAX_PEERS; p++) {
                if (!(mask & (1u << p)))
                    continue;
                const RttEstimate& e = m_rtt[static_cast<size_t>(p)];
                w.u8(static_cast<uint8_t>(p));
                w.u16(clampMs(e.rttMs));
                w.u16(clampMs(e.jitterMs));
                w.u8(static_cast<uint8_t>(e.samples));
            }
            sendTo(0, w);
        }
        return;
    }

    if (m_phase != Phase::InMatch)
        updateInputDelay();
    if (m_dirty && nowMs - m_lastStateMs >= STATE_INTERVAL_MS) {
        m_lastStateMs = nowMs;
        sendLobbyState();
    }
    sendMapChunks(nowMs);
}

std::string NetLobby::slotSkinPath(int slot) const
{
    const LobbySlot& s = m_slots[static_cast<size_t>(slot & 3)];
    if (s.kind != SlotKind::Human || s.peer < 0)
        return {};
    return m_skins.pathOf(s.peer, s.localIndex);
}

bool NetLobby::slotSkinDownloading(int slot) const
{
    const LobbySlot& s = m_slots[static_cast<size_t>(slot & 3)];
    return s.kind == SlotKind::Human && s.peer >= 0 && m_skins.downloading(s.peer, s.localIndex);
}

// ---- map files

std::string safeMapFileName(const std::string& hint)
{
    //Only the last path component, without its extension
    std::string base = hint;
    const size_t slash = base.find_last_of("/\\:");
    if (slash != std::string::npos)
        base = base.substr(slash + 1);
    const size_t dot = base.rfind('.');
    if (dot != std::string::npos)
        base = base.substr(0, dot);

    std::string out;
    for (char c : base) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        if (ok && out.size() < 40)
            out += c;
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '-'))
        out.erase(out.begin());
    return out.empty() ? "map" : out;
}

uint64_t hashFile(const std::string& path, uint64_t maxBytes)
{
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes)
        return 0;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return 0;
    std::vector<char> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(bytes.data(), static_cast<std::streamsize>(size)))
        return 0;
    return net::fnv1a64(bytes.data(), bytes.size());
}

std::string findMapByHash(const std::string& dir, uint64_t hash)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (hash == 0 || !fs::is_directory(dir, ec))
        return {};
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".map")
            continue;
        const std::string path = entry.path().string();
        if (hashFile(path, MAP_MAX_BYTES) == hash)
            return path;
    }
    return {};
}

bool saveDownloadedMap(const std::string& dir, const std::string& nameHint, const std::vector<uint8_t>& bytes, uint64_t hash, std::string& path,
    std::string& error)
{
    namespace fs = std::filesystem;
    if (bytes.empty() || bytes.size() > MAP_MAX_BYTES || net::fnv1a64(bytes.data(), bytes.size()) != hash) {
        error = "the map data doesn't match its hash";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        error = "can't create the folder " + dir;
        return false;
    }

    const std::string base = safeMapFileName(nameHint);
    fs::path target;
    for (int n = 1; n < 100 && target.empty(); n++) {
        const fs::path candidate = fs::path(dir) / (n == 1 ? base + ".map" : base + " (" + std::to_string(n) + ").map");
        if (!fs::exists(candidate, ec)) {
            target = candidate;
        } else if (hashFile(candidate.string(), MAP_MAX_BYTES) == hash) {
            path = candidate.string();    //we have it already
            return true;
        }
    }
    if (target.empty()) {
        error = "too many maps named " + base;
        return false;
    }

    //Written next to it, then renamed: never a half-written .map
    const fs::path part = fs::path(target.string() + ".part");
    {
        std::ofstream file(part, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            error = "can't write " + part.string();
            file.close();
            fs::remove(part, ec);
            return false;
        }
    }
    fs::rename(part, target, ec);
    if (ec) {
        error = "can't rename " + part.string();
        fs::remove(part, ec);
        return false;
    }
    path = target.string();
    return true;
}

} // namespace rollback

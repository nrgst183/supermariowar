#include "RollbackSession.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace rollback {

namespace {
constexpr float TICK_MS = 16.0f;
// Time sync decides this often whether to skip ticks
constexpr int TIME_SYNC_INTERVAL = 60;
constexpr int MAX_SKIPS_PER_SYNC = 8;
// Weight of a new sample in the running averages (ping, frame advantage)
constexpr float AVERAGE_WEIGHT = 0.1f;
// "Everything known" for slots no peer drives
constexpr int ALL_KNOWN = INT_MAX / 2;

// a - b for wrapping millisecond clocks
int32_t msDiff(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b);
}
} // namespace

const char* stallReasonName(StallReason reason)
{
    switch (reason) {
        case StallReason::None: return "none";
        case StallReason::WaitingForInputs: return "waiting for inputs";
        case StallReason::TimeSync: return "time sync";
        case StallReason::Barrier: return "barrier";
        case StallReason::Ended: return "ended";
    }
    return "?";
}

RollbackSession::RollbackSession(const SessionConfig& config, ITransport& transport, ISessionCallbacks& callbacks, uint32_t nowMs)
    : m_config(config)
    , m_transport(transport)
    , m_callbacks(callbacks)
{
    m_config.inputDelay = std::clamp(m_config.inputDelay, 0, 16);
    m_config.maxPrediction = std::clamp(m_config.maxPrediction, 1, 8);

    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        const int peer = m_config.slotPeer[slot];
        if (peer >= 0 && peer < m_config.peerCount) {
            Peer& p = m_peers[peer];
            p.slots[p.slotCount++] = slot;
        } else {
            m_config.slotPeer[slot] = -1;
        }
    }

    //Inputs below the input delay are zero on every peer
    m_localReceived = m_config.inputDelay;
    for (Peer& peer : m_peers) {
        peer.received = m_config.inputDelay;
        peer.acked = m_config.inputDelay;
        peer.lastReceiveMs = nowMs;
    }
}

int RollbackSession::knownFrames(int slot) const
{
    const int peer = m_config.slotPeer[slot];
    if (peer < 0)
        return ALL_KNOWN;
    if (peer == m_config.localPeer)
        return m_localReceived;
    return m_peers[peer].received;
}

int RollbackSession::confirmedFrame() const
{
    int confirmed = ALL_KNOWN;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++)
        confirmed = std::min(confirmed, knownFrames(slot));
    return confirmed;
}

uint8_t RollbackSession::laggingPeers() const
{
    const int confirmed = confirmedFrame();
    uint8_t mask = 0;
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].slotCount > 0 && m_peers[p].received <= confirmed)
            mask |= static_cast<uint8_t>(1u << p);
    }
    return mask;
}

InputFrame RollbackSession::tickInput(int frame)
{
    InputFrame in;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        if (m_config.slotPeer[slot] < 0)
            continue;

        const int known = knownFrames(slot);
        if (frame < known)
            in.players[slot] = input(frame, slot);
        else if (known > 0)
            in.players[slot] = input(known - 1, slot);    //prediction: the last known input again
    }
    return in;
}

void RollbackSession::runTick(bool resimulating)
{
    const InputFrame in = tickInput(m_frame);
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++)
        used(m_frame, slot) = in.players[slot];

    m_atBarrier = m_callbacks.advance(in, resimulating);
    m_frame++;
    m_stats.ticks++;

    if (m_config.checksumInterval > 0 && m_frame % m_config.checksumInterval == 0) {
        ChecksumEntry& entry = checksumEntry(m_frame);
        entry.local = m_callbacks.checksum();
        entry.localFinal = false;
    }
}

void RollbackSession::rollbackIfNeeded(FrameResult& result)
{
    const int from = m_firstWrongFrame;
    m_firstWrongFrame = -1;
    if (from < 0 || from >= m_frame)
        return;

    const int target = m_frame;
    m_callbacks.loadState(from);
    m_frame = from;
    while (m_frame < target) {
        if (m_frame != from)
            m_callbacks.saveState(m_frame);
        runTick(true);
        result.resimulated++;

        //Never run past an irreversible event (see the header)
        if (m_atBarrier)
            break;
    }

    result.rollbackFrom = from;
    m_stats.rollbacks++;
    m_stats.rollbackFrames += target - from;
    m_stats.maxRollback = std::max(m_stats.maxRollback, target - from);
    m_stats.resimulatedTicks += result.resimulated;
}

float RollbackSession::localAdvantage(const Peer& peer, uint32_t nowMs) const
{
    //The peer has advanced since its report was sent: half a round trip plus the time since it arrived
    const float elapsedMs = static_cast<float>(std::max(0, msDiff(nowMs, peer.lastReceiveMs))) + peer.rttMs * 0.5f;
    const float remoteFrameNow = static_cast<float>(peer.remoteFrame) + elapsedMs / TICK_MS;
    return static_cast<float>(m_frame) - remoteFrameNow;
}

void RollbackSession::updateTimeSync(uint32_t nowMs)
{
    for (int p = 0; p < m_config.peerCount; p++) {
        if (!isRemote(p))
            continue;
        Peer& peer = m_peers[p];
        peer.avgLocalAdvantage += (localAdvantage(peer, nowMs) - peer.avgLocalAdvantage) * AVERAGE_WEIGHT;
        m_stats.peers[p].advantage = (peer.avgLocalAdvantage - peer.avgRemoteAdvantage) * 0.5f;
    }

    if (++m_framesSinceSync < TIME_SYNC_INTERVAL || m_skipBudget > 0)
        return;
    m_framesSinceSync = 0;

    //Each side's estimate is off by the same latency error in opposite
    //directions: half their difference is how far ahead we really are
    float ahead = 0.0f;
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p))
            ahead = std::max(ahead, (m_peers[p].avgLocalAdvantage - m_peers[p].avgRemoteAdvantage) * 0.5f);
    }
    if (ahead >= 1.0f)
        m_skipBudget = std::min(static_cast<int>(ahead), MAX_SKIPS_PER_SYNC);
}

FrameResult RollbackSession::advanceFrame(const InputFrame& localInput, uint32_t nowMs)
{
    FrameResult result;
    if (m_state != State::Running) {
        idle(nowMs);
        result.stalled = StallReason::Ended;
        return result;
    }

    checkTimeouts(nowMs);
    if (m_state != State::Running) {
        result.stalled = StallReason::Ended;
        return result;
    }

    rollbackIfNeeded(result);
    result.ticks = result.resimulated;

    if (m_atBarrier) {
        if (confirmedFrame() >= m_frame) {
            //Every peer agrees: the event happens at this frame
            m_state = State::Ended;
            m_endFrame = m_frame;
            result.matchEnded = true;
            result.stalled = StallReason::Ended;
        } else {
            result.stalled = StallReason::Barrier;
            m_stats.barrierFrames++;
        }
    } else if (m_frame >= confirmedFrame() + m_config.maxPrediction) {
        result.stalled = StallReason::WaitingForInputs;
        m_stats.stallFrames++;
    } else if (m_skipBudget > 0 && ++m_skipSpacing >= 2) {
        //Skip every other tick until the budget is used up
        m_skipSpacing = 0;
        m_skipBudget--;
        result.stalled = StallReason::TimeSync;
        m_stats.skipFrames++;
    } else {
        //After a rollback stopped early at a barrier, these inputs exist already
        const int inputFrame = m_frame + m_config.inputDelay;
        if (m_localReceived == inputFrame) {
            const Peer& local = m_peers[m_config.localPeer];
            for (int i = 0; i < local.slotCount; i++)
                input(inputFrame, local.slots[i]) = localInput.players[local.slots[i]] & InputBit::MASK;
            m_localReceived++;
        }

        m_callbacks.saveState(m_frame);
        runTick(false);
        result.ticks++;
    }

    //Measured at the same frame as the advantage sent to the peers
    updateTimeSync(nowMs);
    sendInputs(nowMs);
    finalizeChecksums();
    return result;
}

void RollbackSession::idle(uint32_t nowMs)
{
    if (m_state == State::Ended)
        sendInputs(nowMs);
}

bool RollbackSession::drained() const
{
    if (m_state != State::Ended)
        return m_state == State::Failed;

    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].connected && m_peers[p].acked < std::min(m_localReceived, m_endFrame))
            return false;
    }
    return true;
}

void RollbackSession::sendInputs(uint32_t nowMs)
{
    const Peer& local = m_peers[m_config.localPeer];
    const uint32_t sendTime = nowMs ? nowMs : 1;    //0 means "no time" in an echo

    for (int p = 0; p < m_config.peerCount; p++) {
        if (!isRemote(p) || !m_peers[p].connected)
            continue;
        Peer& peer = m_peers[p];

        const int start = std::max(peer.acked, m_localReceived - INPUT_HISTORY + 1);
        const int count = std::clamp(m_localReceived - start, 0, net::MAX_INPUT_FRAMES_PER_PACKET);
        const int advantage = static_cast<int>(std::lround(std::clamp(localAdvantage(peer, nowMs), -127.0f, 127.0f)));
        const uint32_t hold = peer.echoTime ? static_cast<uint32_t>(std::max(0, msDiff(nowMs, peer.echoReceivedMs))) : 0;

        net::ByteWriter w(m_packet, sizeof(m_packet));
        w.u8(static_cast<uint8_t>(net::MsgType::Input));
        w.u8(m_config.matchId);
        w.u32(static_cast<uint32_t>(start));
        w.u8(static_cast<uint8_t>(count));
        w.u32(static_cast<uint32_t>(peer.received));
        w.u32(static_cast<uint32_t>(m_frame));
        w.i8(static_cast<int8_t>(advantage));
        w.u32(sendTime);
        w.u32(peer.echoTime);
        w.u16(static_cast<uint16_t>(std::min<uint32_t>(hold, 0xffff)));
        for (int f = start; f < start + count; f++) {
            for (int i = 0; i < local.slotCount; i++)
                w.u8(input(f, local.slots[i]));
        }

        m_transport.send(m_config.peerConnection[p], Channel::Unreliable, w.data(), w.size());
        m_stats.packetsSent++;
        m_stats.bytesSent += static_cast<int64_t>(w.size());
    }
}

void RollbackSession::onMessage(int peer, const uint8_t* data, size_t size, uint32_t nowMs)
{
    if (!isRemote(peer) || size < 2 || data[1] != m_config.matchId)
        return;

    m_peers[peer].lastReceiveMs = nowMs;
    m_stats.packetsReceived++;

    if (data[0] == static_cast<uint8_t>(net::MsgType::Input))
        receiveInput(peer, data, size, nowMs);
    else if (data[0] == static_cast<uint8_t>(net::MsgType::Checksum))
        receiveChecksum(peer, data, size);
}

void RollbackSession::receiveInput(int p, const uint8_t* data, size_t size, uint32_t nowMs)
{
    Peer& peer = m_peers[p];

    net::ByteReader r(data, size);
    r.u8();
    r.u8();
    const int start = static_cast<int>(r.u32());
    const int count = r.u8();
    const int ack = static_cast<int>(r.u32());
    const int remoteFrame = static_cast<int>(r.u32());
    const int advantage = r.i8();
    const uint32_t sendTime = r.u32();
    const uint32_t echoTime = r.u32();
    const uint32_t echoHold = r.u16();
    const uint8_t* inputs = r.bytes(static_cast<size_t>(count) * static_cast<size_t>(peer.slotCount));
    if (!r.ok() || r.remaining() != 0)
        return;

    //What it has of ours (packets may arrive out of order: only ever grows)
    if (ack <= m_localReceived)
        peer.acked = std::max(peer.acked, ack);

    //Timing: only from the newest packet
    if (peer.echoTime == 0 || msDiff(sendTime, peer.echoTime) > 0) {
        peer.echoTime = sendTime;
        peer.echoReceivedMs = nowMs;
        peer.remoteFrame = remoteFrame;
        peer.remoteAdvantage = advantage;
        peer.avgRemoteAdvantage += (static_cast<float>(advantage) - peer.avgRemoteAdvantage) * AVERAGE_WEIGHT;

        if (echoTime != 0) {
            const int32_t rtt = msDiff(nowMs, echoTime) - static_cast<int32_t>(echoHold);
            if (rtt >= 0 && rtt < 10000)
                peer.rttMs = peer.rttMs == 0 ? static_cast<float>(rtt) : peer.rttMs + (static_cast<float>(rtt) - peer.rttMs) * AVERAGE_WEIGHT;
        }

        PeerStats& ps = m_stats.peers[p];
        ps.rttMs = peer.rttMs;
        ps.remoteFrame = peer.remoteFrame;
        ps.lastReceiveMs = nowMs;
    }

    //The inputs, in order: older ones are known already, a gap waits for a
    //resend. Inputs too far ahead would overwrite history that is still needed.
    const int limit = std::min(confirmedFrame(), m_frame) + INPUT_HISTORY / 2;
    for (int i = 0; i < count; i++) {
        const int frame = start + i;
        if (frame < peer.received)
            continue;
        if (frame > peer.received || frame >= limit)
            break;

        for (int k = 0; k < peer.slotCount; k++) {
            const int slot = peer.slots[k];
            const uint8_t value = inputs[i * peer.slotCount + k] & InputBit::MASK;
            input(frame, slot) = value;
            if (frame < m_frame && used(frame, slot) != value && (m_firstWrongFrame < 0 || frame < m_firstWrongFrame))
                m_firstWrongFrame = frame;
        }
        peer.received++;
    }
}

RollbackSession::ChecksumEntry& RollbackSession::checksumEntry(int frame)
{
    const int interval = std::max(1, m_config.checksumInterval);
    ChecksumEntry& entry = m_checksums[static_cast<size_t>(frame / interval) % CHECKSUM_HISTORY];
    if (entry.frame != frame) {
        entry = ChecksumEntry();
        entry.frame = frame;
    }
    return entry;
}

void RollbackSession::receiveChecksum(int peer, const uint8_t* data, size_t size)
{
    net::ByteReader r(data, size);
    r.u8();
    r.u8();
    const int frame = static_cast<int>(r.u32());
    const uint64_t sum = r.u64();
    if (!r.ok() || m_config.checksumInterval <= 0 || frame <= 0 || frame % m_config.checksumInterval != 0)
        return;

    //Too old to compare (shouldn't happen: peers are only a few frames apart)
    if (frame <= m_lastFinalizedChecksum - (CHECKSUM_HISTORY - 2) * m_config.checksumInterval)
        return;

    ChecksumEntry& entry = checksumEntry(frame);
    entry.remote[peer] = sum;
    entry.hasRemote[peer] = true;
    compareChecksums(entry);
}

void RollbackSession::compareChecksums(ChecksumEntry& entry)
{
    if (!entry.localFinal)
        return;

    for (int p = 0; p < m_config.peerCount; p++) {
        if (!entry.hasRemote[p] || entry.compared[p])
            continue;
        entry.compared[p] = true;
        m_stats.checksumsCompared++;
        if (entry.remote[p] != entry.local) {
            m_stats.desyncs++;
            if (m_stats.firstDesyncFrame < 0)
                m_stats.firstDesyncFrame = entry.frame;
            m_callbacks.onDesync(entry.frame, p, entry.local, entry.remote[p]);
        }
    }
}

void RollbackSession::finalizeChecksums()
{
    const int interval = m_config.checksumInterval;
    if (interval <= 0)
        return;

    //A frame is final once all inputs before it are known and it was simulated with them
    const int limit = std::min(confirmedFrame(), m_frame);
    for (int frame = m_lastFinalizedChecksum + interval; frame <= limit; frame += interval) {
        m_lastFinalizedChecksum = frame;

        ChecksumEntry& entry = checksumEntry(frame);
        if (entry.localFinal)
            continue;
        entry.localFinal = true;
        m_callbacks.onConfirmedChecksum(frame, entry.local);

        uint8_t buffer[16];
        net::ByteWriter w(buffer, sizeof(buffer));
        w.u8(static_cast<uint8_t>(net::MsgType::Checksum));
        w.u8(m_config.matchId);
        w.u32(static_cast<uint32_t>(frame));
        w.u64(entry.local);
        for (int p = 0; p < m_config.peerCount; p++) {
            if (isRemote(p) && m_peers[p].connected)
                m_transport.send(m_config.peerConnection[p], Channel::Reliable, w.data(), w.size());
        }

        compareChecksums(entry);
    }
}

void RollbackSession::checkTimeouts(uint32_t nowMs)
{
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].connected && msDiff(nowMs, m_peers[p].lastReceiveMs) > static_cast<int32_t>(m_config.timeoutMs)) {
            fail(p, "timed out");
            return;
        }
    }
}

void RollbackSession::onPeerDisconnected(int peer, uint32_t /*nowMs*/, const char* reason)
{
    if (!isRemote(peer) || !m_peers[peer].connected)
        return;

    m_peers[peer].connected = false;
    if (m_state == State::Running)
        fail(peer, reason);
}

void RollbackSession::fail(int peer, const char* reason)
{
    m_state = State::Failed;
    m_failedPeer = peer;
    m_failReason = reason;
}

} // namespace rollback

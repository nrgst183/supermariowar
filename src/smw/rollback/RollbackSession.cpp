#include "RollbackSession.h"

#include <algorithm>
#include <cstring>
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

    m_cpuFrom.fill(INT_MAX);

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
    if (peer < 0 || m_cpuFrom[slot] != INT_MAX)
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
        if (isRemote(p) && m_peers[p].slotCount > 0 && m_peers[p].dropFrame < 0 && m_peers[p].received <= confirmed)
            mask |= static_cast<uint8_t>(1u << p);
    }
    return mask;
}

InputFrame RollbackSession::tickInput(int frame)
{
    InputFrame in;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        if (m_config.slotPeer[slot] < 0 || frame >= m_cpuFrom[slot])
            continue;    //nobody's, or a lost peer's slot that the CPU plays: zero

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

    //From a lost peer's drop frame on, the CPU plays its slots (every tick: a
    //rollback to before the drop frame undoes the takeover)
    uint8_t cpu = 0;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        if (m_frame >= m_cpuFrom[slot])
            cpu |= static_cast<uint8_t>(1u << slot);
    }
    if (cpu)
        m_callbacks.takeOverSlots(cpu);

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
        if (!isRemote(p) || m_peers[p].lost)
            continue;
        Peer& peer = m_peers[p];
        peer.avgLocalAdvantage += (localAdvantage(peer, nowMs) - peer.avgLocalAdvantage) * AVERAGE_WEIGHT;
        m_stats.peers[p].advantage = (peer.avgLocalAdvantage - peer.avgRemoteAdvantage) * 0.5f;
    }

    if (++m_framesSinceSync < TIME_SYNC_INTERVAL || m_skipBudget > 0)
        return;
    m_framesSinceSync = 0;

    const float ahead = aheadFrames();
    if (ahead >= std::max(1.0f, m_config.skipThreshold))
        m_skipBudget = std::min(static_cast<int>(ahead), MAX_SKIPS_PER_SYNC);
}

float RollbackSession::aheadFrames() const
{
    //Each side's estimate is off by the same latency error in opposite
    //directions: half their difference is how far ahead we really are
    float ahead = 0.0f;
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && !m_peers[p].lost)
            ahead = std::max(ahead, (m_peers[p].avgLocalAdvantage - m_peers[p].avgRemoteAdvantage) * 0.5f);
    }
    return ahead;
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
    updateDrops();
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
                input(inputFrame, local.slots[i]) = localInput.players[local.slots[i]];
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
    if (m_state == State::Ended) {
        //The others may still be agreeing on a lost peer: take part (but
        //don't look for lost peers ourselves, the match is over here)
        updateDrops();
        sendInputs(nowMs);
    }
}

bool RollbackSession::drained() const
{
    if (m_state != State::Ended)
        return m_state == State::Failed;

    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].connected && !m_peers[p].lost && m_peers[p].acked < std::min(m_localReceived, m_endFrame))
            return false;
    }
    return true;
}

void RollbackSession::sendInputs(uint32_t nowMs)
{
    const Peer& local = m_peers[m_config.localPeer];
    //(the low 16 bits of the clock: enough for a round trip; 0 means "no time" in an echo)
    const uint16_t sendTime = static_cast<uint16_t>(nowMs) ? static_cast<uint16_t>(nowMs) : 1;

    for (int p = 0; p < m_config.peerCount; p++) {
        if (!isRemote(p) || !m_peers[p].connected || m_peers[p].lost)
            continue;
        Peer& peer = m_peers[p];

        const int start = std::max(peer.acked, m_localReceived - INPUT_HISTORY + 1);
        const int count = std::clamp(m_localReceived - start, 0, net::MAX_INPUT_FRAMES_PER_PACKET);
        //(in 1/64 frames: time sync slows the clock for fractions of a frame)
        const int advantage = static_cast<int>(std::lround(std::clamp(localAdvantage(peer, nowMs), -500.0f, 500.0f) * 64.0f));
        const uint32_t hold = peer.echoTime ? static_cast<uint32_t>(std::max(0, msDiff(nowMs, peer.echoReceivedMs))) : 0;

        //The inputs: as they are, or as runs of the same input when that is shorter
        const int slots = local.slotCount;
        auto sameAsBefore = [&](int f) {
            for (int i = 0; i < slots; i++) {
                if (input(f, local.slots[i]) != input(f - 1, local.slots[i]))
                    return false;
            }
            return true;
        };
        int runs = 0;
        for (int f = start; f < start + count; f++) {
            if (f == start || !sameAsBefore(f) || runs == 0)
                runs++;
        }
        //(a run is at most 255 frames: count is at most MAX_INPUT_FRAMES_PER_PACKET)
        const bool coded = count > 0 && runs * (1 + slots) < count * slots;

        net::ByteWriter w(m_packet, sizeof(m_packet));
        w.u8(static_cast<uint8_t>(net::MsgType::Input));
        w.u8(m_config.matchId);
        w.u32(static_cast<uint32_t>(m_frame));
        w.i16(static_cast<int16_t>(start - m_frame));
        w.u8(static_cast<uint8_t>(count | (coded ? net::INPUT_RUNS : 0)));
        w.i16(static_cast<int16_t>(std::clamp(peer.received - m_frame, -32768, 32767)));
        w.i16(static_cast<int16_t>(advantage));
        w.u16(sendTime);
        w.u16(peer.echoTime);
        w.u16(static_cast<uint16_t>(std::min<uint32_t>(hold, 0xffff)));
        if (coded) {
            for (int f = start; f < start + count;) {
                int run = 1;
                while (f + run < start + count && sameAsBefore(f + run))
                    run++;
                w.u8(static_cast<uint8_t>(run));
                for (int i = 0; i < slots; i++)
                    w.u8(input(f, local.slots[i]));
                f += run;
            }
        } else {
            for (int f = start; f < start + count; f++) {
                for (int i = 0; i < slots; i++)
                    w.u8(input(f, local.slots[i]));
            }
        }

        m_transport.send(m_config.peerConnection[p], Channel::Unreliable, w.data(), w.size());
        m_stats.packetsSent++;
        m_stats.bytesSent += static_cast<int64_t>(w.size());
    }
}

void RollbackSession::onMessage(int peer, const uint8_t* data, size_t size, uint32_t nowMs)
{
    //Nothing counts from a lost peer: what it sent before is all there is
    if (!isRemote(peer) || isLost(peer) || size < 2 || data[1] != m_config.matchId)
        return;

    m_peers[peer].lastReceiveMs = nowMs;
    m_stats.packetsReceived++;

    if (data[0] == static_cast<uint8_t>(net::MsgType::Input))
        receiveInput(peer, data, size, nowMs);
    else if (data[0] == static_cast<uint8_t>(net::MsgType::Checksum))
        receiveChecksum(peer, data, size);
    else if (data[0] == static_cast<uint8_t>(net::MsgType::PeerDrop))
        receiveDrop(peer, data, size);
}

void RollbackSession::receiveInput(int p, const uint8_t* data, size_t size, uint32_t nowMs)
{
    Peer& peer = m_peers[p];

    net::ByteReader r(data, size);
    r.u8();
    r.u8();
    const int remoteFrame = static_cast<int>(r.u32());
    const int start = remoteFrame + r.i16();
    const uint8_t countByte = r.u8();
    const int count = countByte & ~net::INPUT_RUNS;
    const int ack = remoteFrame + r.i16();
    const float advantage = static_cast<float>(r.i16()) / 64.0f;
    const uint16_t sendTime = r.u16();
    const uint16_t echoTime = r.u16();
    const uint32_t echoHold = r.u16();
    if (!r.ok() || count > net::MAX_INPUT_FRAMES_PER_PACKET)
        return;
    const size_t slots = static_cast<size_t>(peer.slotCount);
    uint8_t decoded[net::MAX_INPUT_FRAMES_PER_PACKET * INPUT_MAX_PLAYERS];
    const uint8_t* inputs = nullptr;
    if (countByte & net::INPUT_RUNS) {
        //Runs: u8 frames + the slots' inputs, until every frame is there
        int frames = 0;
        while (frames < count) {
            const int run = r.u8();
            const uint8_t* values = r.bytes(slots);
            if (!r.ok() || run == 0 || frames + run > count)
                return;
            for (int f = 0; f < run; f++)
                std::memcpy(decoded + static_cast<size_t>(frames + f) * slots, values, slots);
            frames += run;
        }
        inputs = decoded;
    } else {
        inputs = r.bytes(static_cast<size_t>(count) * slots);
    }
    if (!r.ok() || r.remaining() != 0)
        return;

    //What it has of ours (packets may arrive out of order: only ever grows)
    if (ack <= m_localReceived)
        peer.acked = std::max(peer.acked, ack);

    //Timing: only from the newest packet
    //(16 bit times: newer when less than half the range ahead)
    if (peer.echoTime == 0 || static_cast<int16_t>(static_cast<uint16_t>(sendTime - peer.echoTime)) > 0) {
        peer.echoTime = sendTime;
        peer.echoReceivedMs = nowMs;
        peer.remoteFrame = remoteFrame;
        peer.remoteAdvantage = advantage;
        peer.avgRemoteAdvantage += (advantage - peer.avgRemoteAdvantage) * AVERAGE_WEIGHT;

        if (echoTime != 0) {
            const int32_t rtt = static_cast<int32_t>(static_cast<uint16_t>(static_cast<uint16_t>(nowMs) - echoTime)) - static_cast<int32_t>(echoHold);
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
            const uint8_t value = inputs[i * peer.slotCount + k];
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
        //A lost peer's checksums of frames past its known inputs may be of
        //a state the others never had (it played on with its own inputs)
        if (m_peers[p].lost && entry.frame > m_peers[p].lostKnown)
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
            if (isRemote(p) && m_peers[p].connected && !m_peers[p].lost)
                m_transport.send(m_config.peerConnection[p], Channel::Reliable, w.data(), w.size());
        }

        compareChecksums(entry);
    }
}

void RollbackSession::checkTimeouts(uint32_t nowMs)
{
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].connected && !m_peers[p].lost && msDiff(nowMs, m_peers[p].lastReceiveMs) > static_cast<int32_t>(m_config.timeoutMs))
            markLost(p, "timed out");
    }
}

void RollbackSession::onPeerDisconnected(int peer, uint32_t /*nowMs*/, const char* reason)
{
    if (!isRemote(peer) || !m_peers[peer].connected)
        return;

    m_peers[peer].connected = false;
    if (m_state == State::Running)
        markLost(peer, reason);
}

void RollbackSession::fail(int peer, const char* reason)
{
    m_state = State::Failed;
    m_failedPeer = peer;
    m_failReason = reason;
}

// ---- lost peers (see LOST PEERS in the header)

uint8_t RollbackSession::cpuSlots() const
{
    uint8_t mask = 0;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        if (m_cpuFrom[slot] != INT_MAX)
            mask |= static_cast<uint8_t>(1u << slot);
    }
    return mask;
}

void RollbackSession::markLost(int p, const char* reason)
{
    if (!isRemote(p) || m_peers[p].lost)
        return;
    Peer& peer = m_peers[p];
    peer.lost = true;
    peer.lostReason = reason;
    peer.lostKnown = peer.received;
    m_lostMask |= static_cast<uint8_t>(1u << p);
    m_dropDirty = true;
}

bool RollbackSession::mergeLostInputs(int p, int known, int count, const uint8_t* inputs)
{
    Peer& peer = m_peers[p];
    if (known <= peer.received)
        return true;
    const int start = known - count;
    if (start > peer.received)
        return false;    //a gap: inputs nobody relayed

    for (int frame = peer.received; frame < known; frame++) {
        for (int k = 0; k < peer.slotCount; k++) {
            const int slot = peer.slots[k];
            const uint8_t value = inputs[(frame - start) * peer.slotCount + k];
            input(frame, slot) = value;
            if (frame < m_frame && used(frame, slot) != value && (m_firstWrongFrame < 0 || frame < m_firstWrongFrame))
                m_firstWrongFrame = frame;
        }
    }
    peer.received = known;
    m_dropDirty = true;
    return true;
}

void RollbackSession::decideDrop(int p, int frame)
{
    Peer& peer = m_peers[p];
    if (peer.dropFrame >= 0)
        return;
    peer.dropFrame = frame;
    peer.received = std::max(peer.received, frame);
    for (int k = 0; k < peer.slotCount; k++)
        m_cpuFrom[peer.slots[k]] = frame;
    //Frames from there on were simulated with the human player: again, with the CPU
    if (m_state == State::Running && peer.slotCount > 0 && frame < m_frame && (m_firstWrongFrame < 0 || frame < m_firstWrongFrame))
        m_firstWrongFrame = frame;
    m_dropDirty = true;
}

void RollbackSession::receiveDrop(int from, const uint8_t* data, size_t size)
{
    if (m_state == State::Failed)
        return;

    net::ByteReader r(data, size);
    r.u8();
    r.u8();
    const uint8_t mask = r.u8();
    struct Entry {
        int decided;
        int known;
        int count;
        const uint8_t* inputs;
    };
    Entry entries[net::MAX_PEERS] = {};
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (!(mask & (1u << p)))
            continue;
        Entry& e = entries[p];
        e.decided = r.i32();
        e.known = static_cast<int>(r.u32());
        e.count = r.u8();
        const int slots = p < m_config.peerCount ? m_peers[p].slotCount : 0;
        e.inputs = r.bytes(static_cast<size_t>(e.count) * static_cast<size_t>(slots));
        if (e.known < 0 || e.decided < -1 || (e.decided >= 0 && e.known != e.decided) || e.count > e.known)
            return;
    }
    if (!r.ok() || r.remaining() != 0)
        return;

    //The others don't count us in any more
    if (mask & (1u << m_config.localPeer)) {
        if (m_state == State::Running)
            fail(m_config.localPeer, "was dropped by the other players");
        return;
    }

    Peer& sender = m_peers[from];
    sender.hasReport = true;
    sender.reportMask = mask;

    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (!(mask & (1u << p)) || !isRemote(p))
            continue;
        markLost(p, "is gone (seen by another player)");
        const Entry& e = entries[p];
        if (m_peers[p].dropFrame >= 0)
            continue;    //decided already (and the decision is the same everywhere)
        if (!mergeLostInputs(p, e.known, e.count, e.inputs)) {
            if (m_state == State::Running)
                fail(p, "inputs of a lost player are missing here");
            return;
        }
        if (e.decided >= 0)
            decideDrop(p, e.decided);
    }
}

void RollbackSession::updateDrops()
{
    if (m_lostMask == 0 || m_state == State::Failed)
        return;

    //The coordinator: the lowest peer id that isn't lost
    int coordinator = -1;
    for (int p = 0; p < m_config.peerCount && coordinator < 0; p++) {
        if ((p == m_config.localPeer || isRemote(p)) && !isLost(p))
            coordinator = p;
    }

    if (coordinator == m_config.localPeer) {
        bool agreed = true;
        for (int p = 0; p < m_config.peerCount; p++) {
            if (isRemote(p) && !isLost(p) && (!m_peers[p].hasReport || m_peers[p].reportMask != m_lostMask))
                agreed = false;
        }
        if (agreed) {
            //Every input any survivor had of the lost peers is here: the drop
            //frame is the highest known one (merged from all the reports)
            for (int p = 0; p < m_config.peerCount; p++) {
                if (isLost(p) && m_peers[p].dropFrame < 0)
                    decideDrop(p, m_peers[p].received);
            }
        }
    }

    if (m_dropDirty)
        sendDropReport();
}

void RollbackSession::sendDropReport()
{
    m_dropDirty = false;

    net::ByteWriter w(m_dropPacket, sizeof(m_dropPacket));
    w.u8(static_cast<uint8_t>(net::MsgType::PeerDrop));
    w.u8(m_config.matchId);
    w.u8(m_lostMask);
    for (int p = 0; p < net::MAX_PEERS; p++) {
        if (!(m_lostMask & (1u << p)))
            continue;
        const Peer& peer = m_peers[p];
        const int known = peer.dropFrame >= 0 ? peer.dropFrame : peer.received;
        const int count = std::clamp(known - m_config.inputDelay, 0, DROP_RELAY_FRAMES);
        w.i32(peer.dropFrame);
        w.u32(static_cast<uint32_t>(known));
        w.u8(static_cast<uint8_t>(count));
        for (int f = known - count; f < known; f++) {
            for (int k = 0; k < peer.slotCount; k++)
                w.u8(input(f, peer.slots[k]));
        }
    }
    if (!w.ok())
        return;

    //Also to the lost peers that may still listen: a peer that was only cut
    //off learns from it that it was dropped
    for (int p = 0; p < m_config.peerCount; p++) {
        if (isRemote(p) && m_peers[p].connected)
            m_transport.send(m_config.peerConnection[p], Channel::Reliable, w.data(), w.size());
    }
}

} // namespace rollback

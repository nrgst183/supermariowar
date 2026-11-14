#include "doctest.h"

#include "NetConditionSim.h"
#include "NetProtocol.h"
#include "RollbackSession.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <vector>

using namespace rollback;

namespace {

// The input player `slot` reads at local frame `frame` (deterministic, so a
// reference run knows every input)
uint8_t testInput(int slot, int frame)
{
    uint32_t x = static_cast<uint32_t>(frame / 7 + 1) * 2654435761u ^ static_cast<uint32_t>(slot + 1) * 40503u;
    x ^= x >> 13;
    return static_cast<uint8_t>(x & InputBit::MASK);
}

// A fake simulation: the state is a hash of every input it has seen
struct FakeState {
    int frame = 0;
    uint64_t acc = 0;
    uint8_t cpu = 0;    // slots the CPU took over (takeOverSlots)
};

// What the game does in takeOverSlots(): part of the state, done once
void takeOver(FakeState& s, uint8_t mask)
{
    if (mask & ~s.cpu) {
        s.cpu |= mask;
        s.acc = (s.acc ^ (0x9e3779b97f4a7c15ull * s.cpu)) * 0x100000001b3ull;
    }
}

FakeState step(FakeState s, const InputFrame& in)
{
    for (uint8_t b : in.players)
        s.acc = (s.acc ^ b) * 0x100000001b3ull;
    s.acc += static_cast<uint64_t>(s.frame);
    s.frame++;
    return s;
}

// Barrier rule: the match ends at a fixed frame, or (if set) at the first frame
// from `inputBarrierFrom` on where the state's hash has its low bits clear
// (depends on the inputs, so a wrong prediction can show a barrier that isn't one)
struct BarrierRule {
    int fixedFrame = -1;
    int inputBarrierFrom = -1;

    bool operator()(const FakeState& s) const {
        if (fixedFrame >= 0 && s.frame >= fixedFrame)
            return true;
        return inputBarrierFrom >= 0 && s.frame >= inputBarrierFrom && (s.acc & 15) == 0;
    }
};

struct FakeSim : ISessionCallbacks {
    FakeState state;
    std::array<FakeState, 16> saved {};
    std::array<bool, 16> savedValid {};
    BarrierRule barrier;
    std::map<int, uint64_t> confirmed;
    std::vector<int> desyncFrames;
    int corruptFrame = -1;    // checksum() lies at this frame (desync test)
    int ticks = 0;
    int resimTicks = 0;

    void saveState(int frame) override {
        REQUIRE(state.frame == frame);
        saved[frame % 16] = state;
        savedValid[frame % 16] = true;
    }
    void loadState(int frame) override {
        REQUIRE(savedValid[frame % 16]);
        REQUIRE(saved[frame % 16].frame == frame);
        state = saved[frame % 16];
    }
    bool advance(const InputFrame& input, bool resimulating) override {
        state = step(state, input);
        ticks++;
        if (resimulating)
            resimTicks++;
        return barrier(state);
    }
    uint64_t checksum() override {
        return state.acc + (state.frame == corruptFrame ? 1 : 0);
    }
    void takeOverSlots(uint8_t mask) override {
        takeOver(state, mask);
    }
    void onConfirmedChecksum(int frame, uint64_t sum) override {
        REQUIRE(confirmed.count(frame) == 0);
        confirmed[frame] = sum;
    }
    void onDesync(int frame, int, uint64_t, uint64_t) override {
        desyncFrames.push_back(frame);
    }
};

// Delivers packets between the sessions after a latency, with jitter and loss
struct FakeNetwork {
    struct Packet {
        uint32_t due;
        int from, to;
        Channel channel;
        std::vector<uint8_t> data;
    };

    int latencyMs = 0;
    int jitterMs = 0;
    int lossPercent = 0;
    uint32_t rng = 12345;
    uint32_t now = 0;
    std::vector<Packet> inFlight;
    std::vector<bool> silenced = std::vector<bool>(net::MAX_PEERS, false);
    bool blocked[net::MAX_PEERS][net::MAX_PEERS] = {};    // [from][to]: everything on this link is lost

    uint32_t random() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void send(int from, int to, Channel channel, const uint8_t* data, size_t size) {
        if (silenced[from] || blocked[from][to])
            return;
        if (channel == Channel::Unreliable && lossPercent > 0 && static_cast<int>(random() % 100) < lossPercent)
            return;
        int delay = latencyMs;
        if (jitterMs > 0)
            delay += static_cast<int>(random() % static_cast<uint32_t>(2 * jitterMs + 1)) - jitterMs;
        // reliable messages keep their order: the same delay as the slowest before them
        inFlight.push_back({now + static_cast<uint32_t>(std::max(0, channel == Channel::Reliable ? latencyMs + jitterMs : delay)), from, to, channel,
            std::vector<uint8_t>(data, data + size)});
    }
};

struct FakeTransport : ITransport {
    FakeNetwork& network;
    int self;

    FakeTransport(FakeNetwork& n, int s) : network(n), self(s) {}

    void send(int connection, Channel channel, const uint8_t* data, size_t size) override {
        network.send(self, connection, channel, data, size);
    }
    bool poll(TransportEvent&) override { return false; }
    void flush() override {}
    void disconnect(int) override {}
};

struct Peer {
    std::unique_ptr<FakeTransport> transport;
    FakeSim sim;
    std::unique_ptr<RollbackSession> session;
    int startStep = 0;
    bool running = true;     // keeps calling advanceFrame
    bool dead = false;       // killed: does nothing, receives nothing
    int endedAt = -1;        // frame of the confirmed barrier
};

// N peers, one human slot each (slot = peer); optional CPU slots stay zero
struct Harness {
    FakeNetwork network;
    std::vector<Peer> peers;
    SessionConfig base;

    explicit Harness(int count, int delay = 2) {
        peers.resize(static_cast<size_t>(count));
        base.peerCount = count;
        base.inputDelay = delay;
        base.checksumInterval = 10;
        for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++)
            base.slotPeer[static_cast<size_t>(slot)] = static_cast<int8_t>(slot < count ? slot : -1);
        for (int p = 0; p < net::MAX_PEERS; p++)
            base.peerConnection[static_cast<size_t>(p)] = p;
    }

    void start() {
        for (int i = 0; i < static_cast<int>(peers.size()); i++) {
            Peer& peer = peers[static_cast<size_t>(i)];
            SessionConfig config = base;
            config.localPeer = i;
            peer.transport = std::make_unique<FakeTransport>(network, i);
            peer.session = std::make_unique<RollbackSession>(config, *peer.transport, peer.sim, network.now);
        }
    }

    void deliver() {
        std::vector<FakeNetwork::Packet> due;
        auto it = std::stable_partition(network.inFlight.begin(), network.inFlight.end(),
            [this](const FakeNetwork::Packet& p) { return static_cast<int32_t>(network.now - p.due) < 0; });
        due.assign(std::make_move_iterator(it), std::make_move_iterator(network.inFlight.end()));
        network.inFlight.erase(it, network.inFlight.end());
        for (const FakeNetwork::Packet& p : due) {
            Peer& to = peers[static_cast<size_t>(p.to)];
            if (to.dead)
                continue;
            to.session->onMessage(p.from, p.data.data(), p.data.size(), network.now);
        }
    }

    // One 16 ms step of every peer
    void stepAll(int stepIndex) {
        deliver();
        for (int i = 0; i < static_cast<int>(peers.size()); i++) {
            Peer& peer = peers[static_cast<size_t>(i)];
            if (stepIndex < peer.startStep || peer.dead)
                continue;
            if (!peer.running) {
                peer.session->idle(network.now);
                continue;
            }
            InputFrame local;
            local.players[static_cast<size_t>(i)] = testInput(i, peer.session->frame());
            const FrameResult r = peer.session->advanceFrame(local, network.now);
            CHECK(r.resimulated <= base.maxPrediction);
            if (r.matchEnded)
                peer.endedAt = peer.session->frame();
        }
        network.now += 16;
    }

    void run(int steps) {
        for (int s = 0; s < steps; s++)
            stepAll(s);
    }

    // Kills a peer: it stops, and what it hasn't sent yet is lost
    void kill(int p) {
        peers[static_cast<size_t>(p)].dead = true;
        network.silenced[static_cast<size_t>(p)] = true;
    }

    // The state each frame should have, from the real inputs. `dropFrame[p]`
    // (>= 0): from there on the CPU plays peer p's slot
    std::vector<FakeState> reference(int frames, const std::vector<int>& dropFrame = {}) const {
        std::vector<FakeState> states(1);
        auto dropOf = [&dropFrame](int slot) { return slot < static_cast<int>(dropFrame.size()) && dropFrame[slot] >= 0 ? dropFrame[slot] : INT_MAX; };
        for (int f = 0; f < frames; f++) {
            InputFrame in;
            uint8_t cpu = 0;
            for (int slot = 0; slot < static_cast<int>(peers.size()); slot++) {
                if (f >= dropOf(slot))
                    cpu |= static_cast<uint8_t>(1u << slot);
                else
                    in.players[static_cast<size_t>(slot)] = f >= base.inputDelay ? testInput(slot, f - base.inputDelay) : 0;
            }
            FakeState s = states.back();
            if (cpu)
                takeOver(s, cpu);
            states.push_back(step(s, in));
        }
        return states;
    }

    // The drop frame of each peer (one slot each), as peer `viewer` agreed on it
    std::vector<int> dropFrames(int viewer) const {
        std::vector<int> frames;
        for (int p = 0; p < static_cast<int>(peers.size()); p++)
            frames.push_back(peers[static_cast<size_t>(viewer)].session->dropFrame(p));
        return frames;
    }

    // The survivors agree on every lost peer's drop frame, and every frame
    // they confirmed has the reference state of the inputs up to there and
    // the CPU from there on
    void checkSurvivorsAgree(const std::vector<int>& survivors, uint8_t lost) {
        REQUIRE(!survivors.empty());
        const std::vector<int> drops = dropFrames(survivors[0]);
        int maxFrame = 0;
        for (int s : survivors) {
            const Peer& peer = peers[static_cast<size_t>(s)];
            CAPTURE(s);
            CHECK(peer.session->state() != RollbackSession::State::Failed);
            CHECK(peer.session->lostPeers() == lost);
            CHECK(dropFrames(s) == drops);
            maxFrame = std::max(maxFrame, peer.session->frame());
        }
        for (int p = 0; p < static_cast<int>(peers.size()); p++)
            CHECK((drops[static_cast<size_t>(p)] >= 0) == (((lost >> p) & 1) != 0));

        const std::vector<FakeState> ref = reference(maxFrame + 1, drops);
        for (int s : survivors) {
            const Peer& peer = peers[static_cast<size_t>(s)];
            CAPTURE(s);
            CHECK(peer.sim.confirmed.size() > 5);
            for (const auto& [frame, sum] : peer.sim.confirmed) {
                CAPTURE(frame);
                CHECK(sum == ref[static_cast<size_t>(frame)].acc);
            }
            CHECK(peer.sim.desyncFrames.empty());
        }
    }

    // Every confirmed checksum of every peer is the reference one
    void checkConfirmedAgainstReference() {
        int maxFrame = 0;
        for (const Peer& peer : peers)
            maxFrame = std::max(maxFrame, peer.session->frame());
        const std::vector<FakeState> ref = reference(maxFrame + 1);
        for (const Peer& peer : peers) {
            CHECK(!peer.sim.confirmed.empty());
            for (const auto& [frame, sum] : peer.sim.confirmed)
                CHECK(sum == ref[static_cast<size_t>(frame)].acc);
            CHECK(peer.sim.desyncFrames.empty());
            CHECK(peer.session->stats().desyncs == 0);
        }
    }
};

} // namespace


TEST_CASE("ByteWriter and ByteReader round-trip little-endian values") {
    uint8_t buffer[64];
    net::ByteWriter w(buffer, sizeof(buffer));
    w.u8(0xab);
    w.i8(-3);
    w.u16(0x1234);
    w.i16(-2);
    w.u32(0xdeadbeef);
    w.u64(0x0102030405060708ull);
    w.f32(1.5f);
    w.str("hello");
    CHECK(w.ok());
    CHECK(buffer[2] == 0x34);    // little-endian
    CHECK(buffer[3] == 0x12);

    net::ByteReader r(buffer, w.size());
    CHECK(r.u8() == 0xab);
    CHECK(r.i8() == -3);
    CHECK(r.u16() == 0x1234);
    CHECK(r.i16() == -2);
    CHECK(r.u32() == 0xdeadbeef);
    CHECK(r.u64() == 0x0102030405060708ull);
    CHECK(r.f32() == 1.5f);
    CHECK(r.str() == "hello");
    CHECK(r.ok());
    CHECK(r.remaining() == 0);
    r.u32();
    CHECK(!r.ok());    // read past the end

    net::ByteWriter small(buffer, 3);
    small.u32(1);
    CHECK(!small.ok());
}

TEST_CASE("Two peers without latency agree, with few or no rollbacks") {
    Harness h(2);
    h.start();
    h.run(300);
    for (const Peer& peer : h.peers) {
        CHECK(peer.session->frame() >= 290);
        CHECK(peer.session->state() == RollbackSession::State::Running);
        CHECK(peer.session->stats().stallFrames == 0);
    }
    h.checkConfirmedAgainstReference();
}

TEST_CASE("Peers under latency, jitter and loss predict, roll back and still agree") {
    for (int count = 2; count <= 4; count++) {
        CAPTURE(count);
        Harness h(count);
        h.network.latencyMs = 40;
        h.network.jitterMs = 15;
        h.network.lossPercent = 10;
        h.start();
        h.run(800);

        h.checkConfirmedAgainstReference();
        for (const Peer& peer : h.peers) {
            const RollbackSession::Stats& st = peer.session->stats();
            CHECK(st.rollbacks > 0);
            CHECK(st.maxRollback <= h.base.maxPrediction);
            CHECK(peer.session->frame() > 600);
            CHECK(st.peers[static_cast<size_t>((&peer - h.peers.data() + 1) % count)].rttMs > 50.0f);
        }
    }
}

TEST_CASE("Only remote inputs that differ from the prediction cause a rollback") {
    Harness h(2, 0);
    h.network.latencyMs = 50;
    h.start();
    h.run(400);
    h.checkConfirmedAgainstReference();
    // testInput changes every 7 frames: rollbacks happen at those changes, not every frame
    const RollbackSession::Stats& st = h.peers[0].session->stats();
    CHECK(st.rollbacks > 0);
    CHECK(st.rollbacks < 400 / 4);
}

TEST_CASE("A peer stalls at the prediction window, then the last one plays on with the CPU") {
    Harness h(2);
    h.base.timeoutMs = 2000;
    h.start();
    h.kill(1);
    h.run(60);

    Peer& a = h.peers[0];
    // inputs below the delay are known: it can run delay + maxPrediction frames
    CHECK(a.session->frame() == h.base.inputDelay + h.base.maxPrediction);
    CHECK(a.session->stats().stallFrames > 0);
    CHECK(a.session->state() == RollbackSession::State::Running);
    CHECK(a.session->lostPeers() == 0);    // lagging is not lost: only after the timeout

    h.run(200);
    CHECK(a.session->state() == RollbackSession::State::Running);
    CHECK(a.session->lostPeers() == 2);
    CHECK(std::string(a.session->lostReason(1)) == "timed out");
    // nothing of it is known after the input delay: the CPU plays from there
    CHECK(a.session->dropFrame(1) == h.base.inputDelay);
    CHECK(a.session->cpuSlots() == 2);
    CHECK(a.session->frame() > 100);
    h.checkSurvivorsAgree({0}, 2);
}

TEST_CASE("A disconnected peer is dropped: the others agree on its drop frame and go on") {
    for (int count = 3; count <= 4; count++) {
        CAPTURE(count);
        Harness h(count);
        h.network.latencyMs = 30;
        h.network.jitterMs = 10;
        h.network.lossPercent = 5;
        h.start();
        h.run(50);
        h.kill(2);
        h.run(10);
        // the transport tells everyone (at different times)
        for (int p = 0; p < count; p++) {
            if (p != 2)
                h.peers[static_cast<size_t>(p)].session->onPeerDisconnected(2, h.network.now, "disconnected");
            h.run(3);
        }
        h.run(300);

        std::vector<int> survivors;
        for (int p = 0; p < count; p++) {
            if (p != 2)
                survivors.push_back(p);
        }
        h.checkSurvivorsAgree(survivors, 4);
        for (int s : survivors) {
            CHECK(h.peers[static_cast<size_t>(s)].session->frame() > 300);
            CHECK(h.peers[static_cast<size_t>(s)].session->stats().maxRollback <= h.base.maxPrediction);
        }
    }
}

TEST_CASE("A lost peer is dropped at the same frame whenever it is lost, rollbacks included") {
    // Killed at many points, under latency: survivors are often in the middle
    // of a rollback window (inputs of the lost peer predicted, some wrongly)
    int rollbacksBeforeDecision = 0;
    for (int killStep = 20; killStep < 120; killStep += 7) {
        for (int count = 2; count <= 4; count++) {
            CAPTURE(killStep);
            CAPTURE(count);
            Harness h(count);
            h.base.timeoutMs = 600;
            h.network.latencyMs = 50;
            h.network.jitterMs = 20;
            h.network.lossPercent = 10;
            h.network.rng = static_cast<uint32_t>(killStep * 31 + count);
            h.start();
            h.run(killStep);
            const int victim = killStep % count;
            h.kill(victim);
            h.run(250);

            std::vector<int> survivors;
            for (int p = 0; p < count; p++) {
                if (p != victim)
                    survivors.push_back(p);
            }
            h.checkSurvivorsAgree(survivors, static_cast<uint8_t>(1u << victim));
            for (int s : survivors) {
                const RollbackSession::Stats& st = h.peers[static_cast<size_t>(s)].session->stats();
                CHECK(st.maxRollback <= h.base.maxPrediction);
                rollbacksBeforeDecision += st.rollbacks > 0;
            }
        }
    }
    CHECK(rollbacksBeforeDecision > 0);
}

TEST_CASE("A survivor that has fewer of the lost peer's inputs gets them from the others") {
    Harness h(3);
    h.base.timeoutMs = 800;
    h.network.latencyMs = 20;
    h.start();
    h.run(100);
    // peer 2's packets to peer 1 are lost, then peer 2 dies: peer 0 has more of its inputs
    h.network.blocked[2][1] = true;
    h.run(4);
    h.kill(2);
    h.run(200);

    h.checkSurvivorsAgree({0, 1}, 4);
    // the drop frame is past what peer 1 had: it took peer 0's
    CHECK(h.peers[0].session->dropFrame(2) > 100);
}

TEST_CASE("Two peers lost at once, and the coordinator lost while they agree") {
    SUBCASE("two at once") {
        Harness h(4);
        h.base.timeoutMs = 600;
        h.network.latencyMs = 40;
        h.network.jitterMs = 10;
        h.start();
        h.run(80);
        h.kill(1);
        h.kill(3);
        h.run(300);
        h.checkSurvivorsAgree({0, 2}, 0b1010);
    }
    SUBCASE("the coordinator (peer 0) goes, then the next one, during the agreement") {
        for (int gap = 0; gap < 60; gap += 6) {
            CAPTURE(gap);
            Harness h(4);
            h.base.timeoutMs = 500;
            h.network.latencyMs = 40;
            h.network.jitterMs = 10;
            h.network.rng = static_cast<uint32_t>(gap + 3);
            h.start();
            h.run(70);
            h.kill(0);
            h.run(32 + gap);    // around the timeout: the others are agreeing on peer 0
            h.kill(1);
            h.run(300);
            h.checkSurvivorsAgree({2, 3}, 0b0011);
        }
    }
}

TEST_CASE("A peer that is cut off but still runs learns that it was dropped") {
    Harness h(3);
    h.base.timeoutMs = 500;
    h.network.latencyMs = 20;
    h.start();
    h.run(60);
    // nothing from peer 2 reaches anyone; it still hears the others
    h.network.silenced[2] = true;
    h.run(200);
    h.checkSurvivorsAgree({0, 1}, 4);
    const Peer& cut = h.peers[2];
    CHECK(cut.session->state() == RollbackSession::State::Failed);
    CHECK(cut.session->failedPeer() == 2);
}

TEST_CASE("A peer lost at the end: the match still ends at the same frame for the others") {
    for (int killStep = 180; killStep <= 215; killStep += 5) {
        CAPTURE(killStep);
        Harness h(3);
        h.base.timeoutMs = 500;
        h.network.latencyMs = 60;
        h.network.jitterMs = 10;
        for (Peer& peer : h.peers)
            peer.sim.barrier.fixedFrame = 200;
        h.start();
        h.run(killStep);
        h.kill(1);
        h.run(300);
        for (int s : {0, 2}) {
            const Peer& peer = h.peers[static_cast<size_t>(s)];
            CAPTURE(s);
            CHECK(peer.endedAt == 200);
            CHECK(peer.session->state() == RollbackSession::State::Ended);
        }
        CHECK(h.peers[0].sim.confirmed == h.peers[2].sim.confirmed);
        CHECK(h.peers[0].sim.confirmed.rbegin()->first == 200);
        // agreed (or not needed) the same way on both
        CHECK(h.peers[0].session->dropFrame(1) == h.peers[2].session->dropFrame(1));
        h.checkSurvivorsAgree({0, 2}, h.peers[0].session->lostPeers());
    }
}

TEST_CASE("Time sync lets a peer that started early wait for the other") {
    Harness h(2);
    h.network.latencyMs = 30;
    h.peers[1].startStep = 6;    // 6 frames late
    h.start();
    h.run(60);
    const int gapEarly = h.peers[0].session->frame() - h.peers[1].session->frame();
    CHECK(gapEarly >= 4);

    h.run(900);
    const int gap = h.peers[0].session->frame() - h.peers[1].session->frame();
    CHECK(gap <= 2);
    CHECK(gap >= -2);
    CHECK(h.peers[0].session->stats().skipFrames >= 2);
    CHECK(h.peers[1].session->stats().skipFrames <= h.peers[0].session->stats().skipFrames);
    CHECK(h.peers[0].session->stats().peers[1].advantage < 2.0f);
    h.checkConfirmedAgainstReference();
}

TEST_CASE("A fixed barrier ends the match at the same frame on every peer") {
    Harness h(3);
    h.network.latencyMs = 80;    // more than the input delay: the end is predicted before it is confirmed
    h.network.lossPercent = 5;
    for (Peer& peer : h.peers)
        peer.sim.barrier.fixedFrame = 200;
    h.start();
    h.run(400);
    for (Peer& peer : h.peers) {
        CHECK(peer.endedAt == 200);
        CHECK(peer.session->state() == RollbackSession::State::Ended);
        CHECK(peer.session->frame() == 200);
        CHECK(peer.session->stats().barrierFrames > 0);
        CHECK(peer.session->drained());
    }
    h.checkConfirmedAgainstReference();
    // every peer confirmed the same checksums, up to the end
    CHECK(h.peers[0].sim.confirmed == h.peers[1].sim.confirmed);
    CHECK(h.peers[0].sim.confirmed == h.peers[2].sim.confirmed);
    CHECK(h.peers[0].sim.confirmed.rbegin()->first == 200);
}

TEST_CASE("An input dependent barrier ends at the real first barrier frame, even when one was predicted") {
    int predictedBarriers = 0;
    for (uint32_t seed = 1; seed <= 6; seed++) {
        CAPTURE(seed);
        Harness h(2);
        h.network.latencyMs = 60;
        h.network.jitterMs = 20;
        h.network.rng = seed * 7919;
        for (Peer& peer : h.peers)
            peer.sim.barrier.inputBarrierFrom = 30;
        h.start();
        h.run(600);

        // The first frame >= 30 where the real state is a barrier
        const std::vector<FakeState> ref = h.reference(600);
        int expected = -1;
        for (int f = 30; f < 600 && expected < 0; f++) {
            if (h.peers[0].sim.barrier(ref[static_cast<size_t>(f)]))
                expected = f;
        }
        REQUIRE(expected > 0);
        for (Peer& peer : h.peers) {
            CHECK(peer.endedAt == expected);
            predictedBarriers += static_cast<int>(peer.session->stats().barrierFrames > 0);
        }
        h.checkConfirmedAgainstReference();
    }
    CHECK(predictedBarriers > 0);
}

TEST_CASE("A checksum mismatch is reported with its frame") {
    Harness h(2);
    h.network.latencyMs = 20;
    h.peers[1].sim.corruptFrame = 120;
    h.start();
    h.run(300);
    REQUIRE(h.peers[0].sim.desyncFrames.size() == 1);
    CHECK(h.peers[0].sim.desyncFrames[0] == 120);
    REQUIRE(h.peers[1].sim.desyncFrames.size() == 1);
    CHECK(h.peers[1].sim.desyncFrames[0] == 120);
    CHECK(h.peers[0].session->stats().firstDesyncFrame == 120);
    CHECK(h.peers[0].session->stats().checksumsCompared >= 25);
}

TEST_CASE("CPU slots and several local players per peer") {
    // peer 0 drives slots 0 and 2, peer 1 drives slot 1, slot 3 is a CPU player
    Harness h(2);
    h.base.slotPeer = {{0, 1, 0, -1}};
    h.network.latencyMs = 35;
    h.start();
    for (int s = 0; s < 500; s++) {
        h.deliver();
        for (int i = 0; i < 2; i++) {
            Peer& peer = h.peers[static_cast<size_t>(i)];
            InputFrame local;
            for (int slot = 0; slot < 4; slot++)
                local.players[static_cast<size_t>(slot)] = testInput(slot, peer.session->frame());
            peer.session->advanceFrame(local, h.network.now);    // non-local bytes must be ignored
        }
        h.network.now += 16;
    }

    const int frames = std::min(h.peers[0].session->frame(), h.peers[1].session->frame());
    std::vector<FakeState> ref(1);
    for (int f = 0; f < frames; f++) {
        InputFrame in;
        for (int slot = 0; slot < 3; slot++)
            in.players[static_cast<size_t>(slot)] = f >= 2 ? testInput(slot, f - 2) : 0;
        ref.push_back(step(ref.back(), in));
    }
    for (const Peer& peer : h.peers) {
        CHECK(peer.sim.confirmed.size() > 40);
        for (const auto& [frame, sum] : peer.sim.confirmed) {
            if (frame <= frames)
                CHECK(sum == ref[static_cast<size_t>(frame)].acc);
        }
    }
}

TEST_CASE("NetConditionSim delays, drops unreliable packets and keeps reliable ones in order") {
    NetConditions conditions;
    conditions.latencyMs = 100;
    conditions.jitterMs = 30;
    conditions.lossPercent = 20;
    NetConditionSim sim(conditions);

    int sentUnreliable = 0;
    for (uint8_t i = 0; i < 200; i++) {
        CHECK(sim.push(1000, 0, Channel::Unreliable, &i, 1));
        sentUnreliable++;
        CHECK(sim.push(1000 + i, 0, Channel::Reliable, &i, 1));
    }

    std::vector<uint8_t> reliable;
    int unreliable = 0;
    auto deliver = [&](int, Channel channel, const uint8_t* data, size_t size) {
        REQUIRE(size == 1);
        if (channel == Channel::Reliable)
            reliable.push_back(data[0]);
        else
            unreliable++;
    };
    sim.release(1069, deliver);
    CHECK(unreliable == 0);    // nothing before latency - jitter
    CHECK(reliable.empty());
    sim.release(2000, deliver);

    REQUIRE(reliable.size() == 200);
    for (size_t i = 0; i < reliable.size(); i++)
        CHECK(reliable[i] == i);
    CHECK(unreliable > 130);
    CHECK(unreliable < 185);
    CHECK(sim.stats().dropped == sentUnreliable - unreliable);
}

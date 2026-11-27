#pragma once

#include "InputFrame.h"
#include "NetProtocol.h"
#include "Transport.h"

#include <array>
#include <cstdint>

// ROLLBACK SESSION (after GGPO's Peer-to-peer session)
//
// Runs one online match: exchanges inputs with the other peers, predicts the
// inputs that haven't arrived, and when a prediction turns out wrong, rolls
// the simulation back to the first wrong frame and runs it again up to the
// present. It knows nothing about the game: it drives the simulation through
// ISessionCallbacks and the network through ITransport, so it is tested with
// a fake of both (tests/rollback/test_RollbackSession.cpp).
//
// FRAMES. Frame f is the state after f ticks; tick f reads the inputs of
// frame f and produces frame f + 1. Every peer starts at frame 0 from the same
// state. Inputs of frames below the input delay are zero for everyone.
//
// ONE CALL OF advanceFrame() (once per 16 ms):
//  1. If inputs that arrived since the last call contradict a prediction,
//     load the snapshot of the first wrong frame and re-simulate up to the
//     current frame (inside the game's re-simulation scope).
//  2. Unless it must wait, add the local players' input for frame
//     current + delay, save a snapshot, and run one tick. It waits when it is
//     `maxPrediction` frames past the last frame whose inputs are all known
//     (stall), when time sync asks it to let the others catch up (skip), or
//     at a barrier (below).
//  3. Send each peer every local input it hasn't acknowledged yet.
//  4. Report the checksums of frames that became confirmed, and compare them
//     with the peers' (desync detection).
//
// PREDICTION. A missing input repeats the last input received from that
// player. CPU players are a peer's players like the humans (the game gives
// them to the host, whose machine runs their AI: CpuDriver.h); empty slots
// have no input (zero).
//
// BARRIERS: IRREVERSIBLE EVENTS. Leaving the match is not part of the
// simulation and can't be rolled back, so it must only happen at a frame
// that every peer agrees on. advance() returns true when the state it
// produced asks for such an event (the game decides what that is, e.g. the
// match is over). The session never runs a tick past such a frame; a
// re-simulation stops at the first one. Once every input up to it is known
// (the frame is confirmed) and no rollback is pending, advanceFrame() reports
// matchEnded: the event really happens there, on every peer at the same
// frame. If a rollback shows that the barrier was predicted wrongly, the
// match simply goes on.
//
// TIME SYNC. Each peer estimates how many frames it runs ahead of each other
// peer (its frame minus the other's last reported frame, corrected by the
// time the report took to arrive) and sends that estimate. Averaging both
// views and halving their difference cancels the latency error; when the
// result says it is at least a frame ahead, the session skips ticks, one at
// a time, so the peers converge without big rollbacks or stalls.
//
// LOST PEERS: THE MATCH GOES ON. A peer that disconnects, says it leaves or
// is silent for `timeoutMs` is *lost*; the others play on and the CPU takes
// over its player slots. Every remaining peer must make that change at the
// same frame and with the same inputs before it, or they drift apart. The
// agreement (PEER_DROP in NetProtocol.h) works like this:
//  - Lost set. Each peer keeps the set of peers it counts as gone: the ones
//    it noticed itself, plus every set it receives from another peer that is
//    still in (so the sets only grow and every survivor ends up with the
//    union). Once a peer is lost, nothing more is accepted from it, so how
//    many of its inputs each survivor has (`known`) is frozen.
//  - Reports. Whenever its set, its known inputs or a decision change, a
//    peer sends every survivor a report: its set and, per lost peer, `known`
//    with that peer's last inputs (up to DROP_RELAY_FRAMES). A survivor that
//    has fewer of them takes the missing ones from the report (they are the
//    same everywhere: the lost peer sent the same inputs to all), and rolls
//    back if it predicted them wrongly; one that has more reports them.
//  - Decision. The coordinator is the lowest peer id that isn't lost. When
//    the latest report of every survivor shows the same lost set as its own,
//    every input any survivor had of a lost peer has reached it, and it
//    decides that peer's *drop frame* D: the highest `known` of all, so no
//    survivor ever has to forget an input it used (and none has to roll back
//    further than for an ordinary late input: at most maxPrediction frames,
//    which the snapshots cover). A report with a decision is final: every
//    peer that gets one adopts it, also from a coordinator it later loses,
//    and a new coordinator adopts any decision a survivor reports, so a
//    lost peer never gets two different drop frames.
//  - Takeover. Until the decision, the lost peer's inputs past `known` are
//    unknown (the confirmed frame waits, the session stalls as for late
//    inputs). From frame D on, its slots are known and zero, and before every
//    tick from D on the session calls ISessionCallbacks::takeOverSlots(): the
//    game turns those players into CPU players at tick D, inside the
//    simulation, so a rollback before D undoes it and the re-simulation
//    does it again. If a survivor already simulated past D, it rolls back to
//    D. A lost peer that is still running (it was only cut off) learns from a
//    report that it was dropped: its session fails (failedPeer() is itself).
//  - Timeouts and the end. A lagging peer is only lost after the usual
//    timeout. An Ended session (its barrier was confirmed) still takes part
//    in the reports (idle()); its drop frames are at or after its end.
// When every other peer is lost, the last one plays on against the CPUs.
//
// PAUSE: THE HOST STOPS EVERYONE. The host (peer 0) can hold the match at a
// frame H: no peer runs a tick past it until the host lets go. H is the next
// frame of the host's own input, and the hold travels in every input packet
// the host sends (with a counter, so an old packet can't undo a newer
// change): any peer that has the host's input for H knows of the hold, so no
// peer confirms H without it. A peer that ran past H on predicted input rolls
// back to H. Letting go needs no agreement: nobody is past H.
//
// JOINING DURING THE MATCH. Someone who joins while the match is played
// first catches up on the match's confirmed inputs (NetGame), then the host
// hands it the slot of a CPU player that the host drives, from a frame J:
// the next frame of the host's own input. As with the pause, the handover
// travels in every input packet the host sends from then on, so no peer
// confirms J without knowing it, and from J on the slot's input is the new
// peer's (the host's bytes for it are ignored). The new peer's own session
// starts at the frame S it caught up to (SessionConfig::startFrame); the
// others send it their inputs from S on. A peer that predicted the slot past
// J rolls back as for any late input. The simulation plays the slot as a
// human's from J (ISessionCallbacks::handOverSlots).
//
// REJOINING. A player who was lost (the CPU plays their slot from its drop frame D) can come back
// during the same match: they join again (as a new peer: a lost peer's id stays lost), catch up as
// above, and the host hands them their old slot from a frame J after D. The slot then has three
// spans: the old peer's inputs up to D, the CPU from D to J, the new peer's from J (and the CPU
// again from its own drop frame, should it be lost too). A peer that joins during the match learns
// the drops decided before it came from the host (SessionConfig::dropFrames), not from the reports.
//
// LIGHTNESS. Everything is fixed-size: no allocation after construction.
namespace rollback {

struct SessionConfig {
    int peerCount = 1;
    int localPeer = 0;
    // The peer driving each player slot; -1 for CPU players and empty slots
    std::array<int8_t, INPUT_MAX_PLAYERS> slotPeer {{-1, -1, -1, -1}};
    // The transport connection of each remote peer (-1: the peer id isn't in this match)
    std::array<int, net::MAX_PEERS> peerConnection = net::noConnections();

    int inputDelay = 2;        // ticks between reading local input and simulating it
    int maxPrediction = 8;     // ticks the simulation may run ahead of the confirmed inputs
    int checksumInterval = 60; // compare peer checksums every N frames; 0 = off
    uint32_t timeoutMs = 5000; // a peer that sends nothing for this long is gone
    // Time sync skips ticks once we run this many frames ahead. Below it, the
    // game may instead slow its clock a little (aheadFrames()): that is smooth,
    // a skipped tick is a visible hitch.
    float skipThreshold = 1.0f;
    uint8_t matchId = 0;

    // A peer that joined during the match (see JOINING): the frame its session starts at (its
    // state is that frame's), and the input of the frame before (what the prediction repeats)
    int startFrame = 0;
    InputFrame startInput {};
    // The handovers known when the session starts (a joiner's own, and earlier ones)
    struct Handover {
        int8_t peer = -1;    // -1: none
        int from = 0;        // J: the new peer's inputs from this frame on
        int start = 0;       // S: the frame its session started at
    };
    std::array<Handover, INPUT_MAX_PLAYERS> handovers {};
    // The drop frames decided before it joined (see REJOINING), per peer (-1: not lost)
    std::array<int, net::MAX_PEERS> dropFrames = []() {
        std::array<int, net::MAX_PEERS> none {};
        none.fill(-1);
        return none;
    }();
};

class ISessionCallbacks {
public:
    virtual ~ISessionCallbacks() = default;

    // The current state is the state at `frame`: keep a copy
    virtual void saveState(int frame) = 0;
    // Restore the copy kept for `frame`
    virtual void loadState(int frame) = 0;
    // Run one tick with these inputs. `resimulating`: the tick was shown
    // before (no sounds or effects). Returns true if the new state asks for an
    // irreversible event (a barrier, see above).
    virtual bool advance(const InputFrame& input, bool resimulating) = 0;
    // Cross-machine checksum of the current state (peerChecksum())
    virtual uint64_t checksum() = 0;
    // Called before every tick from a lost peer's drop frame on (see LOST
    // PEERS): the CPU plays these slots (bit s) from this tick on. Must be
    // part of the simulation (rolled back with it) and harmless when repeated.
    virtual void takeOverSlots(uint8_t /*slotMask*/) {}
    // Called before every tick from a handover's frame on (see JOINING): players who joined the
    // match play these CPU slots (bit s) from this tick on. Part of the simulation, like the above
    virtual void handOverSlots(uint8_t /*slotMask*/) {}
    // The transport connection of a peer that joined during the match (-1: none yet)
    virtual int connectionOf(int /*peer*/) { return -1; }

    // A frame's state is confirmed; `sum` is its checksum (the "netsum:" lines)
    virtual void onConfirmedChecksum(int /*frame*/, uint64_t /*sum*/) {}
    // A peer's checksum for a confirmed frame differs from ours
    virtual void onDesync(int /*frame*/, int /*peer*/, uint64_t /*local*/, uint64_t /*remote*/) {}
};

enum class StallReason : uint8_t {
    None,
    WaitingForInputs,   // too far ahead of the confirmed inputs
    TimeSync,           // letting slower peers catch up
    Held,               // the host paused the match (see PAUSE)
    Barrier,            // waiting until an irreversible event is confirmed
    Ended,              // the match is over (or failed)
};

struct FrameResult {
    int ticks = 0;            // ticks run, re-simulated ones included
    int rollbackFrom = -1;    // the frame that was loaded to roll back, or -1
    int resimulated = 0;      // re-simulated ticks
    StallReason stalled = StallReason::None;
    bool matchEnded = false;  // the barrier frame is confirmed: act on it now
};

class RollbackSession {
public:
    enum class State : uint8_t {
        Running,
        Ended,     // the barrier was confirmed; inputs are still sent until acknowledged
        Failed,    // we can't go on: the others dropped us (see LOST PEERS)
    };

    RollbackSession(const SessionConfig& config, ITransport& transport, ISessionCallbacks& callbacks, uint32_t nowMs);

    // A message of this match's types (Input, Checksum, PeerDrop) from a peer
    void onMessage(int peer, const uint8_t* data, size_t size, uint32_t nowMs);
    // The transport lost the connection to a peer (or it said it leaves): it
    // is lost, the match goes on without it (see LOST PEERS)
    void onPeerDisconnected(int peer, uint32_t nowMs, const char* reason = "disconnected");

    // Runs this frame (see above). `localInput` holds the local players'
    // bytes at their slot index; the other bytes are ignored.
    FrameResult advanceFrame(const InputFrame& localInput, uint32_t nowMs);
    // After the match: keeps sending until every peer has every input it needs
    void idle(uint32_t nowMs);
    // Every peer has acknowledged all local inputs up to the end
    bool drained() const;

    // ---- the host's pause (see PAUSE)
    // The host: hold every peer at the next frame of its input (true), or let go (false)
    void setHold(bool hold);
    // ---- players who join during the match (see JOINING)
    // The host: hand a CPU slot it drives to `peer`, whose session starts at `start`; or a lost
    // peer's slot that the CPU plays (see REJOINING). The frame it plays from (J), or -1 if it can't
    // be handed over (neither, handed over already, `start` too old for the input history). `lead`:
    // that many frames later than the next of our input (the time the new peer needs to catch up:
    // until then the slot is still ours, no one waits for it)
    int handOver(int slot, int peer, int start, uint32_t nowMs, int lead = 0);
    // A lost peer's slot that the CPU plays and that can be handed to someone who comes back
    bool reclaimable(int slot) const;
    // The peer that drives a slot from the latest handover on (the slot's peer if none)
    int slotOwner(int slot) const;
    // The frame a slot was handed over at (-1: never), and to whom
    int handoverFrame(int slot) const { return slot >= 0 && slot < INPUT_MAX_PLAYERS ? m_handFrom[slot] : -1; }
    int handoverStart(int slot) const { return slot >= 0 && slot < INPUT_MAX_PLAYERS ? m_handStart[slot] : 0; }
    // How many frames we run behind the furthest ahead peer (0: not behind): a peer that joined
    // runs extra ticks to catch up
    float behindFrames(uint32_t nowMs) const;

    // The frame every peer is held at (-1: none)
    int holdFrame() const { return m_holdFrame; }
    // Held: at the hold frame, not ticking
    bool held() const { return m_holdFrame >= 0 && m_frame >= m_holdFrame; }

    State state() const { return m_state; }
    int frame() const { return m_frame; }
    // All inputs below this frame are known
    int confirmedFrame() const;
    // The remote peers whose inputs are the oldest known ones (bit p): while
    // the session waits for inputs, it waits for these
    uint8_t laggingPeers() const;
    const SessionConfig& config() const { return m_config; }
    // Set when the session failed: which peer and why
    int failedPeer() const { return m_failedPeer; }
    const char* failReason() const { return m_failReason; }

    // ---- lost peers (see LOST PEERS)
    // The peers counted as gone (bit p)
    uint8_t lostPeers() const { return m_lostMask; }
    // Why a lost peer is gone ("timed out", "disconnected", ...)
    const char* lostReason(int peer) const { return (peer >= 0 && peer < net::MAX_PEERS) ? m_peers[peer].lostReason : ""; }
    // The frame from which the CPU plays a lost peer's slots, -1 until agreed
    int dropFrame(int peer) const { return (peer >= 0 && peer < net::MAX_PEERS) ? m_peers[peer].dropFrame : -1; }
    // How many frames we run ahead of the furthest behind peer (time sync's
    // estimate; 0 or less: we aren't ahead)
    float aheadFrames() const;
    // The slots the CPU has taken over (bit s; once their drop frame is agreed)
    uint8_t cpuSlots() const;

    struct PeerStats {
        float rttMs = 0;          // smoothed round trip time
        float advantage = 0;      // frames we run ahead of this peer (averaged, both views)
        int remoteFrame = 0;      // its latest reported frame
        uint32_t lastReceiveMs = 0;
    };
    struct Stats {
        int64_t ticks = 0;
        int64_t rollbacks = 0;
        int64_t rollbackFrames = 0;    // sum of the rollback lengths
        int maxRollback = 0;
        int64_t resimulatedTicks = 0;
        int64_t stallFrames = 0;       // calls that waited for inputs
        int64_t skipFrames = 0;        // calls skipped by time sync
        int64_t barrierFrames = 0;     // calls that waited at a barrier
        int64_t packetsSent = 0;
        int64_t bytesSent = 0;
        int64_t packetsReceived = 0;
        int64_t checksumsCompared = 0;
        int64_t desyncs = 0;
        int firstDesyncFrame = -1;
        std::array<PeerStats, net::MAX_PEERS> peers {};
    };
    const Stats& stats() const { return m_stats; }

    static constexpr int INPUT_HISTORY = 256;     // frames of input kept (power of two)
    static constexpr int CHECKSUM_HISTORY = 32;   // checksum frames kept
    // Inputs of a lost peer relayed in a PEER_DROP report. The survivors'
    // counts of its inputs differ by far less (the stall at maxPrediction
    // keeps everyone within a few input delays of each other); a survivor
    // that still finds a gap can't go on and fails.
    static constexpr int DROP_RELAY_FRAMES = 64;

private:
    struct Peer {
        int slotCount = 0;            // player slots it drives
        int slots[INPUT_MAX_PLAYERS] = {};
        int received = 0;             // all its inputs below this frame are known
        int acked = 0;                // it has all our inputs below this frame
        int remoteFrame = 0;
        float remoteAdvantage = 0;    // its estimate of how far it runs ahead of us
        uint32_t lastReceiveMs = 0;
        uint16_t echoTime = 0;        // its latest send time (low 16 bits; 0: none yet)
        uint32_t echoReceivedMs = 0;  // when we got it
        float rttMs = 0;
        float avgLocalAdvantage = 0;
        float avgRemoteAdvantage = 0;
        bool connected = true;
        // Lost (see LOST PEERS)
        bool lost = false;
        const char* lostReason = "";
        int lostKnown = 0;            // `received` when it was lost (its later checksums aren't compared)
        int dropFrame = -1;           // agreed: the CPU plays its slots from here
        // Its latest PEER_DROP report: its lost set
        bool hasReport = false;
        uint8_t reportMask = 0;
    };

    struct ChecksumEntry {
        int frame = -1;
        uint64_t local = 0;
        bool localFinal = false;
        uint64_t remote[net::MAX_PEERS] = {};
        bool hasRemote[net::MAX_PEERS] = {};
        bool compared[net::MAX_PEERS] = {};
    };

    uint8_t& input(int frame, int slot) { return m_inputs[frame & (INPUT_HISTORY - 1)][slot]; }
    uint8_t& used(int frame, int slot) { return m_used[frame & (INPUT_HISTORY - 1)][slot]; }
    ChecksumEntry& checksumEntry(int frame);

    // A peer of this match other than us (peer ids may have gaps: peers
    // without a connection aren't in the match)
    bool isRemote(int peer) const { return peer >= 0 && peer < m_config.peerCount && peer != m_config.localPeer && m_config.peerConnection[peer] >= 0; }
    int knownFrames(int slot) const;
    int knownOf(int peer) const { return peer == m_config.localPeer ? m_localReceived : m_peers[peer].received; }
    // Whose input a slot's is at a frame (a handover: the new peer's from its frame on)
    int ownerAt(int slot, int frame) const { return m_handFrom[slot] >= 0 && frame >= m_handFrom[slot] ? m_handPeer[slot] : m_config.slotPeer[slot]; }
    // Whether the CPU plays a slot at a frame (a lost peer's: from its drop frame on; until it was
    // handed to someone who came back, see REJOINING)
    bool cpuAt(int slot, int frame) const { return (frame >= m_cpuFrom[slot] && frame < m_cpuUntil[slot]) || frame >= m_cpuAgain[slot]; }
    // The end of a slot's first CPU span: the handover to someone who came back after its drop
    void updateCpuSpan(int slot);
    void addHandover(int slot, int peer, int from, int start, uint32_t nowMs, int received, int acked);
    InputFrame tickInput(int frame);
    void runTick(bool resimulating);
    void rollbackIfNeeded(FrameResult& result);
    void updateTimeSync(uint32_t nowMs);
    float localAdvantage(const Peer& peer, uint32_t nowMs) const;
    void sendInputs(uint32_t nowMs);
    void finalizeChecksums();
    void compareChecksums(ChecksumEntry& entry);
    void receiveInput(int peer, const uint8_t* data, size_t size, uint32_t nowMs);
    void receiveChecksum(int peer, const uint8_t* data, size_t size);
    void checkTimeouts(uint32_t nowMs);
    void fail(int peer, const char* reason);

    // Lost peers (see LOST PEERS)
    bool isLost(int peer) const { return peer >= 0 && peer < net::MAX_PEERS && m_peers[peer].lost; }
    void markLost(int peer, const char* reason);
    void receiveDrop(int peer, const uint8_t* data, size_t size);
    bool mergeLostInputs(int peer, int known, int count, const uint8_t* inputs);
    void decideDrop(int peer, int frame);
    void updateDrops();
    void sendDropReport();

    SessionConfig m_config;
    ITransport& m_transport;
    ISessionCallbacks& m_callbacks;

    State m_state = State::Running;
    int m_frame = 0;
    int m_localReceived = 0;        // local inputs below this frame are known
    int m_firstWrongFrame = -1;     // earliest frame simulated with a wrong prediction
    bool m_atBarrier = false;       // the current state is a barrier
    int m_endFrame = -1;            // the confirmed barrier frame

    int m_skipBudget = 0;           // ticks time sync still wants to skip
    int m_framesSinceSync = 0;
    int m_skipSpacing = 0;

    std::array<Peer, net::MAX_PEERS> m_peers {};
    uint8_t m_inputs[INPUT_HISTORY][INPUT_MAX_PLAYERS] = {};
    uint8_t m_used[INPUT_HISTORY][INPUT_MAX_PLAYERS] = {};
    std::array<ChecksumEntry, CHECKSUM_HISTORY> m_checksums {};
    int m_lastFinalizedChecksum = 0;

    uint8_t m_packet[net::INPUT_HEADER_SIZE + 1 + INPUT_MAX_PLAYERS * 10 + net::MAX_INPUT_FRAMES_PER_PACKET * INPUT_MAX_PLAYERS] = {};

    int m_failedPeer = -1;
    const char* m_failReason = "";
    Stats m_stats;

    uint8_t m_lostMask = 0;
    bool m_dropDirty = false;       // our report changed: send it
    int m_holdFrame = -1;           // the host's pause: no tick past this frame (-1: none)
    std::array<int, INPUT_MAX_PLAYERS> m_handFrom {{-1, -1, -1, -1}};    // handovers (see JOINING): from frame J,
    std::array<int, INPUT_MAX_PLAYERS> m_handPeer {{-1, -1, -1, -1}};    // this peer drives the slot (whose session
    std::array<int, INPUT_MAX_PLAYERS> m_handStart {};                   // started at S)
    uint8_t m_holdSerial = 0;       // its changes, counted (the newest wins)
    std::array<int, INPUT_MAX_PLAYERS> m_cpuFrom {};    // the drop frame of each slot's peer (INT_MAX: none)
    std::array<int, INPUT_MAX_PLAYERS> m_cpuUntil {};   // ... the CPU plays it until here (INT_MAX: for good; see REJOINING)
    std::array<int, INPUT_MAX_PLAYERS> m_cpuAgain {};   // the drop frame of the one who came back (INT_MAX: none)
    uint8_t m_dropPacket[3 + (net::MAX_PEERS - 1) * (9 + DROP_RELAY_FRAMES * INPUT_MAX_PLAYERS)] = {};
};

} // namespace rollback

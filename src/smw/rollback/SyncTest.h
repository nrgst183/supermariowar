#pragma once

#include "InputFrame.h"
#include "Snapshot.h"

#include <cstdint>
#include <functional>
#include <vector>

// The sync test (after GGPO's SyncTestSession): proves that rollback is exact
// by doing one on every tick.
//
// Every tick: save the state, run the tick, save the result. Then load the
// state from up to `distance` ticks ago and run those ticks again as
// re-simulations, with the same recorded inputs, checking after each one that
// the full saved state is byte for byte the one of the first run. A mismatch
// means that some state is not captured by the snapshot (a static, heap
// memory, cosmetic code writing simulation state, ...), or that the
// simulation reads state it shouldn't. It is reported with the regions and
// objects that differ, then the game continues from the first run's state.
//
// Enabled with --autotest --at-synctest N.
namespace rollback {

class SyncTest {
public:
    using AdvanceFn = std::function<void(const InputFrame&)>;

    explicit SyncTest(int distance);

    // Runs the tick with this input through the test. `advance` runs one tick.
    void runTick(const InputFrame& input, const AdvanceFn& advance);

    // Forgets the recorded ticks (a new match starts)
    void reset();

    struct Stats {
        int64_t ticks = 0;
        int64_t resimulatedTicks = 0;
        int64_t mismatches = 0;
        int64_t saves = 0;
        int64_t loads = 0;
        int64_t hashes = 0;
        double saveSeconds = 0;
        double loadSeconds = 0;
        double hashSeconds = 0;
        double peerChecksumSeconds = 0;
        size_t maxSnapshotBytes = 0;
        size_t maxArenaBytes = 0;
    };
    const Stats& stats() const { return m_stats; }
    void resetStats() { m_stats = Stats(); }

private:
    struct Slot {
        Snapshot before;    // the state before the tick
        Snapshot after;     // the state after its first run
        uint64_t hash = 0;  // after.hash()
        InputFrame input;
    };

    Slot& slot(int tick) { return m_slots[static_cast<size_t>(tick) % m_slots.size()]; }

    void timedSave(Snapshot& snapshot);
    void timedLoad(const Snapshot& snapshot);
    uint64_t timedHash(const Snapshot& snapshot);

    int m_distance;
    std::vector<Slot> m_slots;
    Snapshot m_scratch;
    int m_nextTick = -1;
    int m_history = 0;
    int m_lastReportedTick = -1;
    Stats m_stats;
};

} // namespace rollback

#include "SyncTest.h"

#include "Resimulation.h"
#include "SimulationState.h"
#include "StateChecksum.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace rollback {

namespace {
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}
} // namespace

SyncTest::SyncTest(int distance)
    : m_distance(std::max(1, distance))
    , m_slots(static_cast<size_t>(m_distance) + 1)
{}

void SyncTest::reset()
{
    m_nextTick = -1;
    m_history = 0;
}

void SyncTest::timedSave(Snapshot& snapshot)
{
    const Clock::time_point start = Clock::now();
    saveState(snapshot);
    m_stats.saveSeconds += secondsSince(start);
    m_stats.saves++;
    m_stats.maxSnapshotBytes = std::max(m_stats.maxSnapshotBytes, snapshot.size());
    m_stats.maxArenaBytes = std::max(m_stats.maxArenaBytes, snapshot.arenaSize());
}

void SyncTest::timedLoad(const Snapshot& snapshot)
{
    const Clock::time_point start = Clock::now();
    loadState(snapshot);
    m_stats.loadSeconds += secondsSince(start);
    m_stats.loads++;
}

uint64_t SyncTest::timedHash(const Snapshot& snapshot)
{
    const Clock::time_point start = Clock::now();
    const uint64_t hash = snapshot.hash();
    m_stats.hashSeconds += secondsSince(start);
    m_stats.hashes++;
    return hash;
}

void SyncTest::runTick(const InputFrame& input, const AdvanceFn& advance)
{
    const int tick = g_sim.tick;
    if (tick == 0 || tick != m_nextTick)
        reset();

    // The first run of the tick
    Slot& current = slot(tick);
    timedSave(current.before);
    current.input = input;
    advance(input);
    timedSave(current.after);
    current.hash = timedHash(current.after);

    {
        const Clock::time_point start = Clock::now();
        volatile uint64_t peer = peerChecksum();
        (void)peer;
        m_stats.peerChecksumSeconds += secondsSince(start);
    }

    m_stats.ticks++;
    m_nextTick = tick + 1;
    m_history = std::min(m_history + 1, m_distance);

    // Roll back and run the recorded ticks again
    const int first = tick - m_history + 1;
    timedLoad(slot(first).before);

    sim::ResimulationScope resimulating;
    for (int k = first; k <= tick; k++) {
        const Slot& recorded = slot(k);
        advance(recorded.input);
        m_stats.resimulatedTicks++;

        timedSave(m_scratch);
        if (timedHash(m_scratch) == recorded.hash)
            continue;

        //Later rollbacks over the same tick usually fail the same way: report it once
        m_stats.mismatches++;
        if (k != m_lastReportedTick) {
            m_lastReportedTick = k;
            printf("synctest: MISMATCH at tick %d (re-simulated from tick %d, distance %d)\n%s",
                k, first, tick - first + 1, describeDifference(recorded.after, m_scratch).c_str());
        }

        // Continue the match from the first run's state
        timedLoad(current.after);
        break;
    }
}

} // namespace rollback

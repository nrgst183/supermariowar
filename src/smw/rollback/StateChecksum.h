#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Checksum of the simulation state that two machines running the same match
// can compare to detect a desync (and that the sync test also reports).
//
// Unlike the byte checksum of a rollback Snapshot, it only hashes values,
// field by field: positions, velocities, states, timers, scores, ids, the
// random number generator. No pointers or addresses, no padding bytes, no
// compiler-specific names. It covers the state that decides the outcome of a
// match, not every byte, so it is cheap enough to send every few ticks.
namespace rollback {

uint64_t peerChecksum();

/// The parts of peerChecksum, a value each (a float as its bits): what a determinism test compares
/// to say what differs first, not only when (src/smw/Determinism.h)
struct StateField {
    std::string name;
    uint64_t value = 0;
    bool isFloat = false;    // value: a float's bits
};
std::vector<StateField> stateFields();

} // namespace rollback

#pragma once

#include <cstdint>

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

} // namespace rollback

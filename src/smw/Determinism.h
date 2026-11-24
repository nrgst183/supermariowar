#pragma once

// The determinism test (--autotest --at-determinism N): a fixed suite of matches (maps with moving
// platforms and circling hazards, in the modes whose objects move by sin, cos and atan2), CPU players
// only, the default settings and a seed of their own for each match. The state checksum every 60
// ticks of each.
//
// The suite runs N times (2: the second must give the first's checksums: the same machine, the same
// results), and the checksums are compared with a reference (--at-determinism-ref FILE, written by
// another build with --at-determinism-out: the PC's, for the Xbox). The result is printed, written
// to determinism_result.txt in the settings folder and, on the Xbox, shown on the screen (it has
// no console); a PC run's exit code is 0 if everything matched.
//
// The Xbox has no command line: a build with -DSMW_XBOX_DETERMINISM_TEST=ON puts the arguments
// (tests/determinism/xbox_args.txt) and the PC's reference on its disc.

#include <cstdint>
#include <string>
#include <vector>

namespace determinism {

/// Whether the test runs (--at-determinism): sets the auto-play's options for it (call after the
/// command line is read)
bool configure();
bool active();

/// The match `index` (0-based, the passes one after the other): its map and mode, and its seed
/// (the gameplay random numbers start from it)
std::string mapOf(int index);
int modeOf(int index);
void matchStarting(int index);

/// The FPU modes found at the start of a tick (before being set as the simulation needs them):
/// the report lists the ones seen
void noteFpu(unsigned short x87, unsigned int mxcsr);

/// A state checksum of the match being played, `frame` ticks in
void sample(int index, int frame, uint64_t sum);

/// All matches played: compares, writes and prints the result
void finish();
bool finished();
bool passed();
/// The result, a line each (for the screen)
const std::vector<std::string>& report();

} // namespace determinism

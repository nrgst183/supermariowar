#pragma once

#include <cstdint>

// CPU PLAYERS PLAY BY INPUT
//
// A CPU player's AI doesn't run inside the simulation: one machine runs it
// before each tick, on the current state, and turns the buttons it chose
// into the slot's input byte, which goes through the same input system as a
// human's (offline: straight into the tick; online: the host sends it to the
// other peers like its own players' input; a replay records it). So the AI
// can change, and use its own random numbers, without a replay or another
// peer playing differently.
//
// The byte keeps what the AI used to do to the keys directly:
//  - It thinks on the tick it used to (every 4th tick per player, when
//    game_values.cputurn reaches it), and holds its buttons in between.
//  - Held keys change only on a change of bit (applyCpuInputByte). Where the
//    simulation let go of a key the AI wants held (jump, on the way down),
//    the byte lets go too and holds it again the next tick, as a human would.
//  - "Just pressed" travels in bits 6-7 (jump, turbo): the AI takes hold of
//    turbo to run without firing.
//  - Unlocking fire (the AI's lockfire = false) is a tick without turbo, then
//    the press. Throwing a star away (throw_star = 30) is a wait inside the AI.
// The AI's memory isn't simulation state either: the driver keeps its own AI
// object per slot, on the normal heap (sim::OutsideScope), so snapshots and
// rollbacks never touch it. The player's AI object in the simulation is only
// for a CPU that takes over a human (it never thinks otherwise).
//
// Old replays (before version 4) and CPUs that took over a human that left an
// online match still think inside the simulation (CPlayer::move).
namespace cpudriver {

// A match begins: forget the last bytes, seed the AI's random numbers
void reset();

// The CPU player of `slot` plays by input in this tick (the simulation's side)
bool playsByInput(int slot);

// The byte of a CPU slot for the next tick, decided from the current state.
// Called again before the state moves on (online, waiting for the others) it
// returns the same byte without thinking again.
uint8_t next(int slot);

}  // namespace cpudriver

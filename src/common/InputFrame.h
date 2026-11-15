#pragma once

#include <array>
#include <cstdint>

struct COutputControl;
class CPlayerInput;

// The input of one simulation tick: one byte per player.
//
// Each byte holds the "held" state of the six gameplay keys, using the key
// order of COutputControl (bit k = keys[k]). The simulation derives "just
// pressed" itself from the previous tick's byte (see applyInputByte). Start
// and cancel (pause and exit) are not simulation input.
//
// CPU players send bytes too: their AI runs outside the simulation, on one
// machine (online: the host), and its buttons go through the same input
// system as a human's (see CpuDriver.h). Their bits 6-7 say which keys the
// AI pressed this tick (only jump and turbo are ever pressed alone); a
// human's bits 6-7 are always zero.
namespace InputBit {
constexpr uint8_t Left = 1 << 0;
constexpr uint8_t Right = 1 << 1;
constexpr uint8_t Jump = 1 << 2;
constexpr uint8_t Down = 1 << 3;
constexpr uint8_t Turbo = 1 << 4;
constexpr uint8_t Powerup = 1 << 5;

constexpr int COUNT = 6;
constexpr uint8_t MASK = (1 << COUNT) - 1;

// CPU players' bytes only (see applyCpuInputByte): "just pressed" of jump and turbo
constexpr uint8_t JumpPress = 1 << 6;
constexpr uint8_t TurboPress = 1 << 7;
} // namespace InputBit

constexpr int INPUT_MAX_PLAYERS = 4;

struct InputFrame {
    std::array<uint8_t, INPUT_MAX_PLAYERS> players {};

    bool operator==(const InputFrame&) const = default;
};

// The held keys of one device state. A key that went down and up again
// within the same frame (fPressed without fDown) counts as held, so a short
// tap still reaches the simulation as a one tick press.
uint8_t toInputByte(const COutputControl& keys);

// The input of the local players: one byte per human player (playercontrol
// == 1), zero for CPU and empty slots (CpuDriver fills those).
InputFrame gatherLocalInput(const CPlayerInput& devices, const short playercontrol[INPUT_MAX_PLAYERS]);

// Simulation side: updates a player's key state from the tick's input byte
// and the previous tick's byte, the same way device events used to: a key
// that goes down sets fDown and fPressed, a key that goes up clears fDown,
// and an unchanged key keeps its fDown (the simulation may have cleared it,
// e.g. to require a new press). fPressed only lasts one tick.
void applyInputByte(uint8_t current, uint8_t previous, COutputControl& keys);

// The same for a CPU player: held keys as above, but "just pressed" only
// where the AI pressed (bits 6-7), so that taking hold of a key (running
// with turbo, say) doesn't press it. A turbo press also unlocks firing again
// (the AI's lockfire = false).
void applyCpuInputByte(uint8_t current, uint8_t previous, COutputControl& keys);

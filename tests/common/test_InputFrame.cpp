#include "doctest.h"

#include "input.h"
#include "InputFrame.h"
#include "RandomNumberGenerator.h"
#include "Resimulation.h"


namespace {
// The old per-frame device semantics: pressed flags are cleared at the start
// of a frame, then each key event updates the key state.
struct OldDeviceKey {
    CKeyState state {false, false};

    void startFrame() { state.fPressed = false; }
    void keyDown() {
        if (!state.fDown)
            state.fPressed = true;
        state.fDown = true;
    }
    void keyUp() { state.fDown = false; }
};
} // namespace


TEST_CASE("toInputByte maps the six gameplay keys to bits") {
    COutputControl keys {};
    CHECK(toInputByte(keys) == 0);

    keys.game_left.fDown = true;
    keys.game_powerup.fDown = true;
    CHECK(toInputByte(keys) == (InputBit::Left | InputBit::Powerup));

    // start and cancel are not simulation input
    keys.game_start.fDown = true;
    keys.game_cancel.fPressed = true;
    CHECK(toInputByte(keys) == (InputBit::Left | InputBit::Powerup));

    // a tap within one frame (pressed, already released) still counts
    keys = COutputControl {};
    keys.game_jump.fPressed = true;
    CHECK(toInputByte(keys) == InputBit::Jump);
}

TEST_CASE("applyInputByte derives pressed from the previous tick") {
    COutputControl keys {};

    applyInputByte(InputBit::Jump, 0, keys);
    CHECK(keys.game_jump.fDown);
    CHECK(keys.game_jump.fPressed);

    applyInputByte(InputBit::Jump, InputBit::Jump, keys);
    CHECK(keys.game_jump.fDown);
    CHECK_FALSE(keys.game_jump.fPressed);

    applyInputByte(0, InputBit::Jump, keys);
    CHECK_FALSE(keys.game_jump.fDown);
    CHECK_FALSE(keys.game_jump.fPressed);
}

TEST_CASE("applyInputByte keeps a key released by the simulation released until it is pressed again") {
    COutputControl keys {};
    applyInputByte(InputBit::Powerup, 0, keys);

    // e.g. PlayerCollisions clears the powerup key after a stored powerup swap
    keys.game_powerup.fDown = false;

    applyInputByte(InputBit::Powerup, InputBit::Powerup, keys);
    CHECK_FALSE(keys.game_powerup.fDown);
    CHECK_FALSE(keys.game_powerup.fPressed);

    applyInputByte(0, InputBit::Powerup, keys);
    applyInputByte(InputBit::Powerup, 0, keys);
    CHECK(keys.game_powerup.fDown);
    CHECK(keys.game_powerup.fPressed);
}

TEST_CASE("The input byte pipeline matches the old device key state") {
    // Random key presses and releases (at most one change per key and frame)
    // and random releases by the simulation itself. The old code kept the key
    // state in the device state, which the simulation wrote to; now the device
    // state only feeds the input bytes, and the simulation keeps its own key
    // state. Both must agree every frame.
    RandomNumberGenerator rng(7);

    OldDeviceKey oldDevice[InputBit::COUNT];
    OldDeviceKey newDevice[InputBit::COUNT];
    COutputControl simKeys {};
    uint8_t previous = 0;

    for (int frame = 0; frame < 20000; frame++) {
        COutputControl deviceKeys {};
        for (int k = 0; k < InputBit::COUNT; k++) {
            oldDevice[k].startFrame();
            newDevice[k].startFrame();
            if (rng.getInteger(4) == 0) {
                if (newDevice[k].state.fDown) {
                    oldDevice[k].keyUp();
                    newDevice[k].keyUp();
                } else {
                    oldDevice[k].keyDown();
                    newDevice[k].keyDown();
                }
            }
            deviceKeys.keys[k] = newDevice[k].state;
        }

        for (CKeyState& key : simKeys.keys)
            key.fPressed = false;
        const uint8_t current = toInputByte(deviceKeys);
        applyInputByte(current, previous, simKeys);
        previous = current;

        for (int k = 0; k < InputBit::COUNT; k++) {
            CHECK(simKeys.keys[k].fDown == oldDevice[k].state.fDown);
            CHECK(simKeys.keys[k].fPressed == oldDevice[k].state.fPressed);
        }

        // the simulation sometimes releases a key itself (e.g. the powerup key)
        if (rng.getInteger(20) == 0) {
            const int k = rng.getInteger(InputBit::COUNT);
            simKeys.keys[k].fDown = false;
            oldDevice[k].state.fDown = false;
        }
    }
}

TEST_CASE("ResimulationScope sets and restores the resimulation flag") {
    CHECK_FALSE(sim::isResimulating());
    {
        sim::ResimulationScope outer;
        CHECK(sim::isResimulating());
        {
            sim::ResimulationScope inner;
            CHECK(sim::isResimulating());
        }
        CHECK(sim::isResimulating());
    }
    CHECK_FALSE(sim::isResimulating());
}

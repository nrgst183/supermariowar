#include "InputFrame.h"

#include "input.h"


uint8_t toInputByte(const COutputControl& keys)
{
    uint8_t bits = 0;
    for (int k = 0; k < InputBit::COUNT; k++) {
        if (keys.keys[k].fDown || keys.keys[k].fPressed)
            bits |= static_cast<uint8_t>(1 << k);
    }
    return bits;
}

InputFrame gatherLocalInput(const CPlayerInput& devices, const short playercontrol[INPUT_MAX_PLAYERS])
{
    InputFrame frame;
    for (int p = 0; p < INPUT_MAX_PLAYERS; p++) {
        if (playercontrol[p] == 1)
            frame.players[p] = toInputByte(devices.outputControls[p]);
    }
    return frame;
}

void applyInputByte(uint8_t current, uint8_t previous, COutputControl& keys)
{
    for (int k = 0; k < InputBit::COUNT; k++) {
        const bool down = current & (1 << k);
        const bool wasDown = previous & (1 << k);

        keys.keys[k].fPressed = down && !wasDown;
        if (down != wasDown)
            keys.keys[k].fDown = down;
    }
}

void applyCpuInputByte(uint8_t current, uint8_t previous, COutputControl& keys)
{
    for (int k = 0; k < InputBit::COUNT; k++) {
        const bool down = current & (1 << k);
        const bool wasDown = previous & (1 << k);
        keys.keys[k].fPressed = false;
        if (down != wasDown)
            keys.keys[k].fDown = down;
    }
    keys.game_jump.fPressed = current & InputBit::JumpPress;
    keys.game_turbo.fPressed = current & InputBit::TurboPress;
}

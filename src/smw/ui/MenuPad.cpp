#include "MenuPad.h"

#include "GameValues.h"
#include "input.h"
#include "ui/InputPrompts.h"

#include <SDL3/SDL.h>

#include <array>
#include <map>

namespace menupad {
namespace {
// The menu keys a controller works (in COutputControl's order), and the shoulders
enum Key { UP, DOWN, LEFT, RIGHT, SELECT, CANCEL, RANDOM, SCROLL, SHOULDER_L, SHOULDER_R, KEY_COUNT };
using Keys = std::array<bool, KEY_COUNT>;

constexpr float STICK = 0.5f;    // (past the dead zone: a direction)

prompts::Gamepads g_pads;
bool g_open = false;
bool g_resync = false;
std::map<SDL_JoystickID, Keys> g_previous;    // each controller's keys the frame before
int g_shoulder = 0;
int g_keyShoulder = 0;

// The player whose controls are set to the controller, or -1
int ownerOf(const CPlayerInput& input, SDL_JoystickID id)
{
    for (int p = 0; p < 4; p++) {
        if (input.inputControls[p] && input.inputControls[p]->iDevice == id)
            return p;
    }
    return -1;
}

Keys keysOf(const prompts::PadState& pad, bool owned)
{
    using prompts::Button;
    Keys k {};
    k[UP] = pad.buttons[Button::UP];
    k[DOWN] = pad.buttons[Button::DOWN];
    k[LEFT] = pad.buttons[Button::LEFT];
    k[RIGHT] = pad.buttons[Button::RIGHT];
    k[SELECT] = pad.buttons[Button::START];
    k[CANCEL] = pad.buttons[Button::BACK];
    //(a player's own controller: the stick and the face buttons as its controls have them)
    if (!owned) {
        k[UP] = k[UP] || pad.ly < -STICK;
        k[DOWN] = k[DOWN] || pad.ly > STICK;
        k[LEFT] = k[LEFT] || pad.lx < -STICK;
        k[RIGHT] = k[RIGHT] || pad.lx > STICK;
        k[SELECT] = k[SELECT] || pad.buttons[Button::A];
        k[CANCEL] = k[CANCEL] || pad.buttons[Button::B];
        k[RANDOM] = pad.buttons[Button::X];
        k[SCROLL] = pad.buttons[Button::Y];
    }
    k[SHOULDER_L] = pad.buttons[Button::LB];
    k[SHOULDER_R] = pad.buttons[Button::RB];
    return k;
}
} // namespace

void devicesChanged()
{
    g_open = false;
}

void resync()
{
    g_resync = true;
}

void noteKey(int key)
{
    if (key == SDLK_PAGEUP)
        g_keyShoulder = -1;
    else if (key == SDLK_PAGEDOWN)
        g_keyShoulder = 1;
}

int shoulder()
{
    return g_shoulder;
}

void update(CPlayerInput& input)
{
    g_shoulder = g_keyShoulder;
    g_keyShoulder = 0;

    if (!g_open) {
        g_pads.open();
        g_previous.clear();
        g_open = true;
    }
    const bool resyncing = g_resync;
    g_resync = false;
    if (!g_pads.any())
        return;

    for (const prompts::PadState& pad : g_pads.read()) {
        const int owner = ownerOf(input, pad.id);
        const Keys now = keysOf(pad, owner >= 0);
        Keys& before = g_previous[pad.id];
        if (resyncing) {
            before = now;
            continue;
        }
        COutputControl& out = input.outputControls[owner >= 0 ? owner : 0];
        //(as a key: pressed the frame it goes down, unless it's down already, from the player's controls)
        for (int k = 0; k < SHOULDER_L; k++) {
            CKeyState& key = out.keys[k];
            if (now[k] && !before[k]) {
                if (!key.fDown)
                    key.fPressed = true;
                key.fDown = true;
            } else if (!now[k] && before[k]) {
                key.fDown = false;
            }
        }
        if (now[SHOULDER_L] && !before[SHOULDER_L])
            g_shoulder = -1;
        if (now[SHOULDER_R] && !before[SHOULDER_R])
            g_shoulder = 1;
        before = now;
    }
}

} // namespace menupad

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

// INPUT PROMPTS AND CONTROLLERS for the theater and the editors
//
// Prompts show how to do something with the device that was used last: a
// controller's buttons (A, B, X, Y in their colors, the shoulders and
// triggers, Start, Back, the d-pad), or a keyboard key cap, or the mouse.
// Every glyph is 16 pixels high and drawn on blitdest; the draw functions
// return the width they took.
//
// Gamepads reads every connected controller directly (not through the
// players' input settings), so any controller works in these screens.
namespace prompts {

enum class Device { Keyboard, Controller };
// The device of the latest input (feed every SDL event to note())
Device lastDevice();
void note(const SDL_Event& event);
void setLastDevice(Device device);

enum class Pad { A, B, X, Y, LB, RB, LT, RT, Start, Back, Up, Down, Left, Right, DPad, LStick, RStick };

constexpr int HEIGHT = 16;
int drawPad(int x, int y, Pad pad);
int drawKey(int x, int y, const std::string& label);
int drawMouse(int x, int y, bool left, bool right);
// The pad glyph on a controller, else the key cap ("" key: the mouse);
// then the label. Returns the total width.
int drawPrompt(int x, int y, Pad pad, const std::string& key, const std::string& label);
int promptWidth(Pad pad, const std::string& key, const std::string& label);

// Shapes (on blitdest)
void fill(int x, int y, int w, int h, SDL_Color c);
void frame(int x, int y, int w, int h, SDL_Color border, SDL_Color inside);
void disc(int cx, int cy, int r, SDL_Color c);
void triangle(int x, int y, int w, int h, bool right, SDL_Color c);    // pointing left or right
void line(int x0, int y0, int x1, int y1, int thickness, SDL_Color c);

// ---- the controllers

enum Button { A, B, X, Y, LB, RB, BACK, START, UP, DOWN, LEFT, RIGHT, BUTTON_COUNT };

struct PadState {
    std::array<bool, BUTTON_COUNT> buttons {};
    float lx = 0, ly = 0, rx = 0, ry = 0;    // -1..1, with a dead zone
    float lt = 0, rt = 0;                    // 0..1
};

class Gamepads {
public:
    ~Gamepads() { close(); }
    void open();     // every connected controller (again after one was plugged in)
    void close();
    bool any() const { return !m_gamepads.empty() || !m_joysticks.empty(); }
    std::vector<PadState> read() const;    // one per controller
    PadState merged() const;               // all of them together

private:
    std::vector<SDL_Gamepad*> m_gamepads;
    std::vector<SDL_Joystick*> m_joysticks;    // the ones SDL has no gamepad mapping for
};

} // namespace prompts

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
// Every glyph is 16 pixels high, drawn as pixel art in the game's doubled
// pixels on blitdest; the draw functions return the width they took.
//
// Two controller styles: the Xbox one (dark buttons with colored letters, LB
// RB LT RT, View and Menu) and the original Xbox's (the Duke and the
// Controller S: colored gem buttons, White and Black, the L and R triggers,
// START and BACK). The "Button prompts" option picks one, or Auto: the style
// of the controller used last (an original Xbox controller is known by its
// USB ids or its name, see isOriginalXbox()).
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

enum class Style { Xbox, OriginalXbox };
Style style();    // the option, or (Auto) the controller used last
bool isOriginalXbox(Uint16 vendor, Uint16 product, const char* name);

constexpr int HEIGHT = 16;
int drawPad(int x, int y, Pad pad);
int padWidth(Pad pad);
int drawKey(int x, int y, const std::string& label);
int drawMouse(int x, int y, bool left, bool right, bool middle = false);
// The pad glyph on a controller, else the key cap ("" key: the mouse);
// then the label. Returns the total width.
int drawPrompt(int x, int y, Pad pad, const std::string& key, const std::string& label);
// The shoulders' and the triggers' names in text, as the style draws them: LB RB LT RT, or (the
// original Xbox) White Black L R
std::string padName(Pad pad);
int promptWidth(Pad pad, const std::string& key, const std::string& label);

// Player 1's keyboard keys of the menus (the game's Controls), as short names
// for key caps: 0 up, 1 down, 2 left, 3 right, 4 select, 5 cancel, 6 random,
// 7 scroll fast ("" if none)
enum MenuKey { MENU_UP, MENU_DOWN, MENU_LEFT, MENU_RIGHT, MENU_SELECT, MENU_CANCEL, MENU_RANDOM, MENU_SCROLL };
std::string menuKey(MenuKey key);

// A prompt with several buttons or keys (the up and down of a list, say): the
// controller's buttons or the keys (whichever was used last), then the label
struct Prompt {
    std::vector<Pad> pads;
    std::vector<std::string> keys;
    std::string label;
};
int promptsWidth(const std::vector<Prompt>& row, int gap);
// The labels in the menus' large font (the title bar's and the footer's: read from a couch), not the
// small one; until set back
void setLargeLabels(bool large);
// drawPrompts() with the large labels if the row fits in maxW that way, else the small ones
int drawPromptsLarge(int cx, int y, const std::vector<Prompt>& row, int gap, int maxW);
// Centered on cx; returns the width
int drawPrompts(int cx, int y, const std::vector<Prompt>& row, int gap = 14);

// Shapes (on blitdest)
void fill(int x, int y, int w, int h, SDL_Color c);
void frame(int x, int y, int w, int h, SDL_Color border, SDL_Color inside);
void disc(int cx, int cy, int r, SDL_Color c);
void line(int x0, int y0, int x1, int y1, int thickness, SDL_Color c);

// Pixel art: one string per row, one character per pixel (drawn `scale`
// pixels wide); each character's color from `inks`, others are see-through.
struct Ink {
    char c;
    SDL_Color color;
};
void art(int x, int y, const std::vector<const char*>& rows, const std::vector<Ink>& inks, int scale = 2);
// Small capitals and digits (3x5 pixels, times `scale`); returns the width
int tinyText(int x, int y, const std::string& s, SDL_Color c, int scale = 2);
int tinyWidth(const std::string& s, int scale = 2);

// ---- the controllers

enum Button { A, B, X, Y, LB, RB, BACK, START, UP, DOWN, LEFT, RIGHT, BUTTON_COUNT };

struct PadState {
    SDL_JoystickID id = 0;    // the controller's (its joystick instance ID)
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

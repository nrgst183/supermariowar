#include "EditorInput.h"

#include "CoEdit.h"
#include "LevelShell.h"
#include "ui/InputPrompts.h"
#include "ui/SmwTheme.h"
#include "gfx.h"
#include "joystick.h"
#include "ResourceManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

extern CResourceManager* rm;

// The editors' documents for editing together (EmbeddedLevelEditor.cpp, EmbeddedWorldEditor.cpp)
namespace leveleditor { std::unique_ptr<editors::coedit::Document> makeCoeditDocument(); }
namespace worldeditor { std::unique_ptr<editors::coedit::Document> makeCoeditDocument(); }

namespace editors::input {
namespace {

constexpr int SCREEN_W = 640;
constexpr int SCREEN_H = 480;
constexpr int TILE = 32;
constexpr float DEADZONE = 0.22f;
constexpr int REPEAT_DELAY = 16;    // frames before a held direction repeats
constexpr int REPEAT_EVERY = 4;

// ---- the commands of the action menu: the editors' keyboard shortcuts

struct Action {
    const char* label;
    SDL_Keycode key;
    bool shift = false;
    bool ctrl = false;
    const char* group = "";    // a heading when it changes
};

const std::vector<Action> LEVEL_ACTIONS = {
    {"Tiles", SDLK_T, false, false, "Modes"}, {"Blocks", SDLK_I}, {"Map items", SDLK_O}, {"Warps", SDLK_W},
    {"Move", SDLK_M}, {"Tile types", SDLK_L}, {"Platforms", SDLK_P}, {"Hazards", SDLK_H},
    {"Animated tiles", SDLK_A}, {"Race goals", SDLK_J}, {"Block properties", SDLK_K}, {"No player spawn", SDLK_X},
    {"No item spawn", SDLK_Z},
    {"Active layer", SDLK_Y, false, false, "Layers"}, {"Hide other layers", SDLK_U}, {"Hide blocks", SDLK_V}, {"Optimize layers", SDLK_END},
    {"Backgrounds", SDLK_G, false, false, "Look"}, {"Thumbnails", SDLK_B}, {"Music category", SDLK_R}, {"Eyecandy", SDLK_E},
    {"New map", SDLK_N, false, false, "File"}, {"Save", SDLK_S}, {"Save as...", SDLK_S, true}, {"Find map...", SDLK_F},
    {"New search...", SDLK_F, true}, {"Previous map", SDLK_PAGEUP}, {"Next map", SDLK_PAGEDOWN},
    {"Clear all", SDLK_DELETE, false, true, "Other"}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1}, {"Leave", SDLK_ESCAPE},
};

const std::vector<Action> WORLD_ACTIONS = {
    {"Water", SDLK_1, false, false, "Modes"}, {"Land", SDLK_2}, {"Stage objects", SDLK_3}, {"Paths", SDLK_4},
    {"Objects", SDLK_5}, {"Bridges", SDLK_6}, {"Connections", SDLK_P}, {"Warps", SDLK_W},
    {"Vehicles", SDLK_V}, {"Copy vehicle", SDLK_C}, {"Start and doors", SDLK_T}, {"Vehicle bounds", SDLK_B},
    {"Start powerups", SDLK_I}, {"Edit stages", SDLK_E},
    {"Auto path/land", SDLK_A, false, false, "World"}, {"Stage previews", SDLK_SPACE}, {"Music category", SDLK_R}, {"Resize world", SDLK_K},
    {"New world", SDLK_N, false, false, "File"}, {"Save", SDLK_S}, {"Save as...", SDLK_S, true}, {"Find world...", SDLK_F},
    {"New search...", SDLK_F, true}, {"Previous world", SDLK_PAGEUP}, {"Next world", SDLK_PAGEDOWN},
    {"Clear all", SDLK_DELETE, false, true, "Other"}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1}, {"Leave", SDLK_ESCAPE},
};

// The key cap of an action ("Shift+S", "PgUp")
std::string keyName(const Action& action)
{
    std::string name;
    switch (action.key) {
        case SDLK_PAGEUP: name = "PgUp"; break;
        case SDLK_PAGEDOWN: name = "PgDn"; break;
        case SDLK_DELETE: name = "Del"; break;
        case SDLK_INSERT: name = "Ins"; break;
        case SDLK_ESCAPE: name = "Esc"; break;
        case SDLK_SPACE: name = "Space"; break;
        case SDLK_END: name = "End"; break;
        default: name = SDL_GetKeyName(action.key); break;
    }
    if (action.shift)
        name = "Shift+" + name;
    if (action.ctrl)
        name = "Ctrl+" + name;
    return name;
}

// ---- the on-screen keyboard (the characters the editors' name dialogs take)

const std::array<const char*, 4> OSK_ROWS = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_= "};
const std::array<const char*, 5> OSK_KEYS = {"Shift", "Space", "Delete", "Done", "Cancel"};
constexpr int OSK_COLS = 10;
constexpr int OSK_TEXT_ROWS = 4;

// ---- the controllers

enum Button { A, B, X, Y, LB, RB, BACK, START, UP, DOWN, LEFT, RIGHT, BUTTON_COUNT };

struct Pad {
    std::array<bool, BUTTON_COUNT> buttons {};
    float lx = 0, ly = 0, rx = 0, ry = 0;
};

float axis(Sint16 value)
{
    const float v = std::clamp(value / 32767.0f, -1.0f, 1.0f);
    return std::fabs(v) < DEADZONE ? 0.0f : v;
}

struct Controllers {
    std::vector<SDL_Gamepad*> gamepads;
    std::vector<SDL_Joystick*> joysticks;    // the ones SDL has no gamepad mapping for

    void open()
    {
        close();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetJoysticks(&count);
        for (int k = 0; ids && k < count; k++) {
            if (SDL_IsGamepad(ids[k])) {
                if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[k]))
                    gamepads.push_back(pad);
            } else if (SDL_Joystick* joy = SDL_OpenJoystick(ids[k])) {
                joysticks.push_back(joy);
            }
        }
        SDL_free(ids);
    }
    void close()
    {
        for (SDL_Gamepad* pad : gamepads)
            SDL_CloseGamepad(pad);
        for (SDL_Joystick* joy : joysticks)
            SDL_CloseJoystick(joy);
        gamepads.clear();
        joysticks.clear();
    }
    bool any() const { return !gamepads.empty() || !joysticks.empty(); }

    // One pad per controller
    std::vector<Pad> read() const
    {
        std::vector<Pad> pads;
        for (SDL_Gamepad* pad : gamepads) {
            Pad p;
            const std::array<SDL_GamepadButton, BUTTON_COUNT> map = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
            for (int b = 0; b < BUTTON_COUNT; b++)
                p.buttons[b] = SDL_GetGamepadButton(pad, map[b]);
            p.lx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
            p.ly = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
            p.rx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
            p.ry = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
            pads.push_back(p);
        }
        //Without a mapping: the usual layout (buttons 0-7, the first hat, axes 0-3)
        for (SDL_Joystick* joy : joysticks) {
            Pad p;
            const int buttons = SDL_GetNumJoystickButtons(joy);
            for (int b = 0; b < 8 && b < buttons; b++)
                p.buttons[b] = SDL_GetJoystickButton(joy, b);
            if (SDL_GetNumJoystickHats(joy) > 0) {
                const Uint8 hat = SDL_GetJoystickHat(joy, 0);
                p.buttons[UP] = hat & SDL_HAT_UP;
                p.buttons[DOWN] = hat & SDL_HAT_DOWN;
                p.buttons[LEFT] = hat & SDL_HAT_LEFT;
                p.buttons[RIGHT] = hat & SDL_HAT_RIGHT;
            }
            const int axes = SDL_GetNumJoystickAxes(joy);
            if (axes >= 2) {
                p.lx = axis(SDL_GetJoystickAxis(joy, 0));
                p.ly = axis(SDL_GetJoystickAxis(joy, 1));
            }
            if (axes >= 4) {
                p.rx = axis(SDL_GetJoystickAxis(joy, 2));
                p.ry = axis(SDL_GetJoystickAxis(joy, 3));
            }
            pads.push_back(p);
        }
        return pads;
    }
};

// A direction held down: once, then repeating
struct Repeat {
    int held = 0;
    bool fire(bool down)
    {
        if (!down) {
            held = 0;
            return false;
        }
        ++held;
        return held == 1 || (held > REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_EVERY == 0);
    }
};

// A controller's cursor
struct Pointer {
    float x = SCREEN_W / 2.0f;
    float y = SCREEN_H / 2.0f;
    Pad pad;
    Pad last;
    bool shown = false;                 // it was used: its cursor is drawn
    SDL_MouseButtonFlags buttons = 0;   // the mouse buttons it holds
    Repeat up, down, left, right;       // d-pad (cursor, overlays)
    Repeat arrowUp, arrowDown, arrowLeft, arrowRight;    // right stick (arrow keys)

    bool pressed(Button b) const { return pad.buttons[b] && !last.buttons[b]; }
    bool released(Button b) const { return !pad.buttons[b] && last.buttons[b]; }
};

enum class Overlay { None, Window, Keyboard };

struct State {
    bool active = false;
    Editor editor = Editor::Level;
    Controllers controllers;
    std::vector<Pointer> pointers;
    int owner = 0;                     // the pointer the editor's mouse is at (-1: the real mouse)
    std::deque<SDL_Event> queue;
    bool cycleStart = true;

    //Where the editor's mouse is
    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f;
    bool cursorWasVisible = true;
    bool mouseUsed = false;

    //Keys the controllers hold (on top of the keyboard's)
    std::array<bool, SDL_SCANCODE_COUNT> keys {};
    int shiftFrames = 0;    // a chord's Shift (an action), a few frames
    int ctrlFrames = 0;

    Overlay overlay = Overlay::None;
    int overlayOwner = 0;              // the controller that opened it (-1: keyboard / mouse)
    int hoverKey = -1;                 // the on-screen keyboard's key under the mouse
    bool hints = true;                 // the controller's button bar (LB)
    Uint64 startMs = 0;
    int actionIndex = 0;
    int textEntries = 0;
    int oskCol = 0;
    int oskRow = 0;
    bool oskShift = false;

    //The editor window (Tab / Start)
    int category = 1;                  // its page (Blocks first)
    bool dockLeft = false;             // at the left of the screen (else the right)
    int hover = -1;                    // the widget under the pointer
    std::string tip;                   // what the widget under the pointer is
    int tool = 0;                      // Tool (level editor)
    int paintMode = 0;                 // the editor's mode before Move
    bool pickRelease = false;
    int scroll = 0;                    // the page scrolled down by this many pixels
    int scrollMax = 0;                 // (as far as it goes, from the last frame's layout)          // the mouse button that picked: its release isn't the editor's

    //The editor's latest frame, without the overlay (redrawn while it waits)
    SDL_Surface* frame = nullptr;
    Uint64 lastPresentMs = 0;
    bool overlayDirty = false;
};
State s;
TestDriver g_testDriver;
std::string g_testShotDir;
int g_testPointers = 1;
int g_testFrame = 0;
std::string g_pendingShot;

SDL_WindowID windowId()
{
    SDL_Window* window = SDL_GetKeyboardFocus();
    return window ? SDL_GetWindowID(window) : 0;
}

// The players' colors (the first: ours online): P1 red, P2 green, P3 yellow, P4 blue, like the teams
SDL_Color pointerColor(int index)
{
    if (index == 0)
        return coedit::active() ? coedit::ourColor() : SDL_Color {232, 56, 40, 255};
    static const SDL_Color colors[] = {{40, 184, 56, 255}, {248, 200, 24, 255}, {48, 112, 232, 255}, {176, 72, 216, 255}};
    return colors[static_cast<size_t>((index - 1) % 4)];
}

// ---- synthetic events

void pushMotion(SDL_MouseButtonFlags state)
{
    SDL_Event e {};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = windowId();
    e.motion.state = state;
    e.motion.x = std::floor(s.cx);
    e.motion.y = std::floor(s.cy);
    s.queue.push_back(e);
}

// The editor's mouse goes to a pointer (a motion event if it moves)
void moveMouseTo(int index)
{
    Pointer& p = s.pointers[static_cast<size_t>(index)];
    const bool moved = std::floor(p.x) != std::floor(s.cx) || std::floor(p.y) != std::floor(s.cy) || s.owner != index;
    s.owner = index;
    s.cx = p.x;
    s.cy = p.y;
    if (moved)
        pushMotion(p.buttons);
}

void pushButton(int index, Uint8 button, bool down)
{
    Pointer& p = s.pointers[static_cast<size_t>(index)];
    moveMouseTo(index);
    SDL_Event e {};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = windowId();
    e.button.button = button;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = std::floor(s.cx);
    e.button.y = std::floor(s.cy);
    s.queue.push_back(e);
    const SDL_MouseButtonFlags mask = SDL_BUTTON_MASK(button);
    p.buttons = down ? (p.buttons | mask) : (p.buttons & ~mask);
}

void pushKey(SDL_Keycode key, bool down)
{
    SDL_Event e {};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = windowId();
    e.key.key = key;
    e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    e.key.mod = modState();
    e.key.down = down;
    s.queue.push_back(e);
}

void tapKey(SDL_Keycode key, bool shift = false, bool ctrl = false)
{
    //(held from now: the editor reads them when it handles the key)
    if (shift) {
        s.shiftFrames = 3;
        s.keys[SDL_SCANCODE_LSHIFT] = true;
    }
    if (ctrl) {
        s.ctrlFrames = 3;
        s.keys[SDL_SCANCODE_LCTRL] = true;
    }
    pushKey(key, true);
    pushKey(key, false);
}

// A key held while `down` (the right stick's arrows): its state, and repeating key presses
void holdKey(SDL_Keycode key, Repeat& repeat, bool down, bool& anyDown)
{
    const SDL_Scancode code = SDL_GetScancodeFromKey(key, nullptr);
    anyDown = anyDown || down;
    if (repeat.fire(down))
        pushKey(key, true);
    else if (repeat.held == 0 && s.keys[code] && !down)
        pushKey(key, false);
}

// ---- the overlays' input (the controller that opened it)

void moveInGrid(Pointer& p, int& col, int& row, int cols, int rows)
{
    //The d-pad, or the left stick pushed
    const bool up = p.pad.buttons[UP] || p.pad.ly < -0.6f;
    const bool down = p.pad.buttons[DOWN] || p.pad.ly > 0.6f;
    const bool left = p.pad.buttons[LEFT] || p.pad.lx < -0.6f;
    const bool right = p.pad.buttons[RIGHT] || p.pad.lx > 0.6f;
    if (p.up.fire(up))
        row = (row + rows - 1) % rows;
    if (p.down.fire(down))
        row = (row + 1) % rows;
    if (p.left.fire(left))
        col = (col + cols - 1) % cols;
    if (p.right.fire(right))
        col = (col + 1) % cols;
    if (up || down || left || right)
        s.overlayDirty = true;
}

// The action menu's layout: headings and actions in three columns
// ---- the editor window (Tab / Start): the editor's tools, palettes and commands
//
// Docked at one side of the screen, over the map (the map stays 640x480, one
// screen pixel a map pixel); it opens at the side away from the pointer, and
// the map at its other side can be edited while it is open. Every pointer
// clicks in it: the mouse, and each controller's cursor with A.

constexpr int WIN_W = 300;
constexpr int RAIL_W = 54;

enum Tool { TOOL_PAINT, TOOL_ERASE, TOOL_MOVE, TOOL_PICK, TOOL_COUNT };

// A clickable part of the window (laid out while it is drawn)
struct Widget {
    SDL_Rect r {};
    std::function<void()> act;
    std::string tip;
};
std::vector<Widget> g_widgets;

SDL_Rect windowRect()
{
    return SDL_Rect {s.dockLeft ? 4 : SCREEN_W - WIN_W - 4, 4, WIN_W, SCREEN_H - 8};
}

bool inWindow(float x, float y)
{
    if (s.overlay != Overlay::Window)
        return false;
    const SDL_Rect r = windowRect();
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int widgetAt(float x, float y)
{
    for (int k = static_cast<int>(g_widgets.size()) - 1; k >= 0; k--) {
        const SDL_Rect& r = g_widgets[static_cast<size_t>(k)].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return k;
    }
    return -1;
}

void hoverAt(float x, float y)
{
    const int at = widgetAt(x, y);
    if (at != s.hover) {
        s.hover = at;
        s.overlayDirty = true;
    }
}

void clickAt(float x, float y)
{
    const int at = widgetAt(x, y);
    if (at >= 0 && g_widgets[static_cast<size_t>(at)].act) {
        //(a copy: the action may change the widgets)
        const std::function<void()> act = g_widgets[static_cast<size_t>(at)].act;
        act();
    }
    s.overlayDirty = true;
}

bool levelMap()
{
    return s.editor == Editor::Level && leveleditor::shell::inMapView();
}

void setTool(int tool)
{
    if (s.editor != Editor::Level)
        return;
    namespace sh = leveleditor::shell;
    if (tool == TOOL_MOVE && sh::mode() != 3) {
        s.paintMode = sh::mode();
        tapKey(SDLK_M);
    } else if (tool != TOOL_MOVE && sh::mode() == 3) {
        sh::setMode(s.paintMode == 3 ? 0 : s.paintMode);
    }
    s.tool = tool;
    s.overlayDirty = true;
}

// The tool in use, as the editor's mode shows it (Move is the editor's mode 3)
int currentTool()
{
    if (s.editor != Editor::Level)
        return TOOL_PAINT;
    if (leveleditor::shell::mode() == 3)
        return TOOL_MOVE;
    return s.tool == TOOL_MOVE ? TOOL_PAINT : s.tool;
}

// The window's pages
int pageCount();

void openWindow(int owner)
{
    //(let go of anything held first)
    for (size_t k = 0; k < s.pointers.size(); k++) {
        if (s.pointers[k].buttons & SDL_BUTTON_LMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_LEFT, false);
        if (s.pointers[k].buttons & SDL_BUTTON_RMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_RIGHT, false);
    }
    //(at the side away from the pointer)
    float px = s.cx;
    if (owner >= 0 && owner < static_cast<int>(s.pointers.size()))
        px = s.pointers[static_cast<size_t>(owner)].x;
    s.dockLeft = px >= SCREEN_W / 2;
    s.overlay = Overlay::Window;
    s.overlayOwner = owner;
    s.hover = -1;
    s.overlayDirty = true;
}

void closeWindow()
{
    s.overlay = Overlay::None;
    s.hover = -1;
    s.overlayDirty = true;
}

void turnPage(int delta)
{
    const int n = pageCount();
    s.category = ((s.category + delta) % n + n) % n;
    s.scroll = 0;
    s.hover = -1;
    s.overlayDirty = true;
}

// A command of the editor: its key, with the window left open unless it opens a screen of its own
void runKey(SDL_Keycode key, bool shift = false, bool ctrl = false, bool keepOpen = false)
{
    if (!keepOpen)
        closeWindow();
    tapKey(key, shift, ctrl);
}


// The on-screen keyboard's keys: index row * OSK_COLS + col (the last row: 5 wide keys)
constexpr int OSK_KEY_W = 38;
constexpr int OSK_KEY_H = 24;
SDL_Rect oskKeyRect(int row, int col)
{
    const int w = OSK_COLS * OSK_KEY_W + 24;
    const int h = (OSK_TEXT_ROWS + 1) * OSK_KEY_H + 56;
    const int x = (SCREEN_W - w) / 2;
    const int y = SCREEN_H - h - 10;
    const bool special = row == OSK_TEXT_ROWS;
    const int c = special ? col / 2 * 2 : col;
    return SDL_Rect {x + 12 + c * OSK_KEY_W, y + 10 + row * OSK_KEY_H, special ? OSK_KEY_W * 2 - 4 : OSK_KEY_W - 4, OSK_KEY_H - 4};
}

int oskKeyAt(float x, float y)
{
    for (int row = 0; row <= OSK_TEXT_ROWS; row++) {
        for (int col = 0; col < OSK_COLS; col++) {
            const SDL_Rect r = oskKeyRect(row, col);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
                return row * OSK_COLS + col;
        }
    }
    return -1;
}

void oskPress(int row, int col)
{
    auto typeChar = [](char c) {
        const bool letter = c >= 'a' && c <= 'z';
        tapKey(static_cast<SDL_Keycode>(c), s.oskShift && letter);
    };
    if (row < OSK_TEXT_ROWS) {
        typeChar(OSK_ROWS[static_cast<size_t>(row)][col]);
    } else {
        switch (col / 2) {
            case 0: s.oskShift = !s.oskShift; break;
            case 1: typeChar(' '); break;
            case 2: tapKey(SDLK_BACKSPACE); break;
            case 3: tapKey(SDLK_RETURN); break;
            default: tapKey(SDLK_ESCAPE); break;
        }
    }
    s.overlayDirty = true;
}

void updateKeyboard(Pointer& p)
{
    moveInGrid(p, s.oskCol, s.oskRow, OSK_COLS, OSK_TEXT_ROWS + 1);
    if (p.pressed(A))
        oskPress(s.oskRow, s.oskCol);
    if (p.pressed(B))
        tapKey(SDLK_BACKSPACE);
    if (p.pressed(X)) {
        s.oskShift = !s.oskShift;
        s.overlayDirty = true;
    }
    if (p.pressed(Y))
        tapKey(SDLK_SPACE);
    if (p.pressed(START) || p.pressed(RB))
        tapKey(SDLK_RETURN);
    if (p.pressed(BACK))
        tapKey(SDLK_ESCAPE);
}

// ---- the pointers

void updatePointer(int index)
{
    Pointer& p = s.pointers[static_cast<size_t>(index)];
    float dx = 0;
    float dy = 0;
    //The stick: faster the further it's pushed
    if (p.pad.lx != 0 || p.pad.ly != 0) {
        const float speed = 1.5f + 9.0f * std::min(1.0f, std::hypot(p.pad.lx, p.pad.ly));
        dx += p.pad.lx * speed;
        dy += p.pad.ly * speed;
    }
    //The d-pad: one tile, from the tile's middle
    const bool up = p.up.fire(p.pad.buttons[UP]);
    const bool down = p.down.fire(p.pad.buttons[DOWN]);
    const bool left = p.left.fire(p.pad.buttons[LEFT]);
    const bool right = p.right.fire(p.pad.buttons[RIGHT]);
    if (up || down || left || right) {
        const float mx = std::floor(p.x / TILE) * TILE + TILE / 2.0f;
        const float my = std::floor(p.y / TILE) * TILE + TILE / 2.0f;
        dx += mx - p.x + (right ? TILE : 0) - (left ? TILE : 0);
        dy += my - p.y + (down ? TILE : 0) - (up ? TILE : 0);
    }
    if (dx != 0 || dy != 0) {
        p.x = std::clamp(p.x + dx, 0.0f, SCREEN_W - 1.0f);
        p.y = std::clamp(p.y + dy, 0.0f, SCREEN_H - 1.0f);
        p.shown = true;
        s.overlayDirty = true;
        //The editor's mouse follows it if it has it, or if it is painting (not into the window)
        if ((s.owner == index || p.buttons) && !inWindow(p.x, p.y))
            moveMouseTo(index);
    }

    //Over the window: its widgets get the buttons, not the editor
    if (inWindow(p.x, p.y)) {
        if (p.shown)
            hoverAt(p.x, p.y);
        if (p.pressed(A) || p.pressed(B))
            p.shown = true;
        if (p.pressed(A))
            clickAt(p.x, p.y);
        if (p.pressed(B))
            closeWindow();
    } else {
        //The buttons: the editor's mouse comes here first (the tools: erase is the right button, pick takes the cell)
        const int tool = currentTool();
        if (p.pressed(A) || p.pressed(B))
            p.shown = true;
        if (p.pressed(A) && tool == TOOL_PICK && levelMap()) {
            leveleditor::shell::pick(static_cast<int>(p.x) / TILE, static_cast<int>(p.y) / TILE);
            setTool(TOOL_PAINT);
        } else if (p.pressed(A)) {
            pushButton(index, tool == TOOL_ERASE && levelMap() ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, true);
        } else if (p.released(A) && (p.buttons & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) && !p.pad.buttons[B]) {
            pushButton(index, (p.buttons & SDL_BUTTON_LMASK) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, false);
        }
        if (p.pressed(B))
            pushButton(index, SDL_BUTTON_RIGHT, true);
        else if (p.released(B) && (p.buttons & SDL_BUTTON_RMASK))
            pushButton(index, SDL_BUTTON_RIGHT, false);
    }

    if (p.pressed(RB) && s.overlay == Overlay::Window)
        turnPage(1);
    else if (p.pressed(RB))
        tapKey(SDLK_RETURN);
    if (p.pressed(BACK)) {
        if (s.overlay == Overlay::Window)
            closeWindow();
        else
            tapKey(SDLK_ESCAPE);
    }
    if (p.pressed(START)) {
        if (s.overlay == Overlay::Window)
            closeWindow();
        else
            openWindow(index);
    }
    if (p.pressed(LB)) {
        if (s.overlay == Overlay::Window) {
            turnPage(-1);
        } else {
            s.hints = !s.hints;
            s.overlayDirty = true;
        }
    }
}

// The modifiers and the right stick's arrows, from every controller
void updateKeys()
{
    bool shift = s.shiftFrames > 0;
    bool ctrl = s.ctrlFrames > 0;
    bool up = false, down = false, left = false, right = false;
    for (Pointer& p : s.pointers) {
        const bool free = s.overlay != Overlay::Keyboard && !inWindow(p.x, p.y);
        if (inWindow(p.x, p.y) && std::fabs(p.pad.ry) > 0.3f) {
            s.scroll = std::clamp(s.scroll + static_cast<int>(p.pad.ry * 10.0f), 0, s.scrollMax);
            s.overlayDirty = true;
        }
        if (free) {
            shift = shift || p.pad.buttons[X];
            ctrl = ctrl || p.pad.buttons[Y];
        }
        holdKey(SDLK_UP, p.arrowUp, free && p.pad.ry < -0.5f, up);
        holdKey(SDLK_DOWN, p.arrowDown, free && p.pad.ry > 0.5f, down);
        holdKey(SDLK_LEFT, p.arrowLeft, free && p.pad.rx < -0.5f, left);
        holdKey(SDLK_RIGHT, p.arrowRight, free && p.pad.rx > 0.5f, right);
    }
    s.keys[SDL_SCANCODE_LSHIFT] = shift;
    s.keys[SDL_SCANCODE_LCTRL] = ctrl;
    s.keys[SDL_SCANCODE_UP] = up;
    s.keys[SDL_SCANCODE_DOWN] = down;
    s.keys[SDL_SCANCODE_LEFT] = left;
    s.keys[SDL_SCANCODE_RIGHT] = right;
}

// ---- drawing

void fill(int x, int y, int w, int h, SDL_Color c)
{
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(screen, &rect, SDL_MapSurfaceRGB(screen, c.r, c.g, c.b));
}

// A panel around an area
void frameBox(int x, int y, int w, int h)
{
    theme::panel(x - 10, y - 10, w + 26, h + 26);
}

void drawArrow(float fx, float fy, SDL_Color color, const std::string& label)
{
    //An arrow: outline (#) and fill (.)
    static const char* ARROW[] = {
        "#",
        "##",
        "#.#",
        "#..#",
        "#...#",
        "#....#",
        "#.....#",
        "#......#",
        "#.......#",
        "#........#",
        "#.....#####",
        "#..#..#",
        "#.# #..#",
        "##  #..#",
        "     #..#",
        "     ####",
    };
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    for (int row = 0; row < 16; row++) {
        for (int col = 0; ARROW[row][col]; col++) {
            const char c = ARROW[row][col];
            if (c == '#')
                fill(x0 + col, y0 + row, 1, 1, {0, 0, 0, 255});
            else if (c == '.')
                fill(x0 + col, y0 + row, 1, 1, color);
        }
    }
    if (!label.empty()) {
        const int w = rm->menu_font_small.getWidth(label) + 6;
        const int lx = std::min(x0 + 12, SCREEN_W - w);
        const int ly = std::min(y0 + 16, SCREEN_H - 16);
        fill(lx + 2, ly, w - 4, 16, {0, 0, 0, 255});
        fill(lx, ly + 2, w, 12, {0, 0, 0, 255});
        fill(lx + 2, ly + 2, w - 4, 12, color);
        fill(lx + 2, ly + 2, w - 4, 2, theme::shade(color, 140));
        theme::font().draw(lx + 3, ly + 1, label);
    }
}

// ---- the window's look

// 8x8 pixel art at twice the size: the tools' and the pages' icons
using Art = std::vector<const char*>;
const Art ART_BRUSH = {"......#.", ".....#w#", "....#ww#", "...#ww#.", "..#bb#..", ".#bbb#..", "#bbb#...", "###....."};
const Art ART_ERASER = {"....###.", "...#www#", "..#www#.", ".#rrw#..", "#rrr#...", "#rr#....", ".##.....", "........"};
const Art ART_MOVE = {"#.#.#.#.", "........", "#...###.", "....#ww#", "#...#ww#", ".....##.", "#.#.#.#.", "........"};
const Art ART_PICK = {"......##", ".....#ww", "....#ww#", "...#ww#.", "..#bw#..", ".#bb#...", ".##.....", "#......."};
const Art ART_EYE = {"........", "..####..", ".#wwww#.", "#ww##ww#", "#ww##ww#", ".#wwww#.", "..####..", "........"};
const Art ART_SWAP = {"..#.....", ".##.....", "#######.", ".##.....", "..#..#..", ".....##.", "#######.", ".....#.."};
const Art ART_CLOSE = {"........", ".##..##.", "..####..", "...##...", "..####..", ".##..##.", "........", "........"};
const Art ART_GROUND = {"gggggggg", "gGgGgGgg", "bbbbbbbb", "bdbbbdbb", "bbbbbbbb", "bbbdbbbd", "bbbbbbbb", "dbbbbdbb"};
const Art ART_SPRING = {"..####..", ".#gggg#.", "..#..#..", ".#.##.#.", "..#..#..", ".#.##.#.", "..#..#..", ".######."};
const Art ART_TYPES = {"########", "#ww#cccc", "#ww#cccc", "########", "#rr#ssss", "#rr#ssss", "########", "........"};
const Art ART_ZONE = {"..####..", ".#rrrr#.", "#rr..rr#", "#r..rrr#", "#r.rr.r#", "#rrr..r#", ".#rrrr#.", "..####.."};
const Art ART_PIPE = {"########", "#ggGggg#", "########", ".#gGgg#.", ".#gGgg#.", ".#gGgg#.", ".#gGgg#.", ".######."};
const Art ART_PLATFORM = {"........", "........", "########", "#bbdbbd#", "########", "...##...", "...##...", "..####.."};
const Art ART_FIRE = {"...#....", "..#r#...", ".#rr#.#.", ".#ryr##.", "#ryyyr#.", "#ryyyr#.", ".#rrr#..", "..###..."};
const Art ART_FLAG = {"#.......", "#rrrr...", "#rrrrr..", "#rrrr...", "#.......", "#.......", "#.......", "##......"};
const Art ART_LOOK = {"########", "#cccccc#", "#ccwccc#", "#cc.ccc#", "#c...cc#", "#.....c#", "#gggggg#", "########"};
const Art ART_DISK = {"#######.", "#w###w##", "#w#.#ww#", "#wwwwww#", "#cccccc#", "#cwwwwc#", "#cwwwwc#", "########"};

void drawArt(int x, int y, const Art& art)
{
    prompts::art(x, y, art, {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}, {'b', {168, 96, 40, 255}}, {'d', {112, 56, 16, 255}},
                                {'g', {56, 184, 48, 255}}, {'G', {152, 232, 88, 255}}, {'r', {216, 40, 32, 255}}, {'y', {248, 208, 48, 255}},
                                {'c', {104, 168, 240, 255}}, {'s', {168, 168, 176, 255}}});
}

// ---- the level editor's palettes

struct Entry {
    int id;
    const char* name;
    const char* tip;
};

const Entry BLOCKS[] = {
    {0, "Brick block", "breaks when a big player hits it from below"},
    {1, "? block", "gives a power-up when hit; point at one and press K to choose what comes out"},
    {2, "Donut block", "falls a moment after a player stands on it"},
    {3, "Flip block", "spins when hit: players fall through it while it spins"},
    {4, "Bounce block", "bounces a player that hits it from below"},
    {5, "Note block", "bounces players that land on it"},
    {6, "Throw block", "pick it up with run and throw it"},
    {15, "View block", "a ? block that shows what is in it"},
    {16, "Red throw block", "pick it up with run and throw it"},
    {17, "Red note block", "a note block: a big bounce"},
    {18, "Blue note block", "a note block: a small bounce"},
    {19, "Gray throw block", "pick it up with run and throw it"},
};
const Entry BREAKABLE[] = {
    {20, "Fireball block", "only a fireball breaks it"}, {21, "Feather block", "only a feather's spin breaks it"},
    {22, "Shell block", "only a thrown shell breaks it"}, {23, "Bob-omb block", "only a Bob-omb's blast breaks it"},
    {24, "Boomerang block", "only a boomerang breaks it"}, {25, "Hammer block", "only a hammer breaks it"},
    {26, "Kuribo's shoe block", "only a Kuribo's shoe breaks it"}, {27, "P-wings block", "only a player with P-wings breaks it"},
    {28, "Star block", "only a player with a star breaks it"}, {29, "Leaf block", "only a leaf's tail breaks it"},
};
const char* COLORS[] = {"Red", "Green", "Yellow", "Blue"};
const Entry ITEMS[] = {
    {0, "Spring", "bounces whoever stands on it; can be carried"}, {1, "Spike", "hurts whoever touches it"},
    {2, "Kuribo's shoe", "jump in to stomp everything"}, {3, "Gold spring", "a spring with a much bigger bounce"},
    {4, "Sticky Kuribo's shoe", "a Kuribo's shoe you can't get out of"}, {5, "Throw box", "pick it up and throw it: it breaks on impact"},
};
const char* TILE_TYPES[] = {"Solid", "Solid on top", "Ice", "Death", "Death on top", "Death on bottom", "Death on left", "Death on right",
    "Ice on top", "Ice, death on bottom", "Ice, death on left", "Ice, death on right", "Super death", "Super death on top",
    "Super death on bottom", "Super death on left", "Super death on right", "Player death"};

// Where a block is in the block sheet (32 pixels a block)
SDL_Rect blockSrc(int id, bool on)
{
    if (id >= 0 && id <= 6)
        return {id * 32, 0, 32, 32};
    if (id >= 15 && id <= 19)
        return {(id - 15) * 32, 32, 32, 32};
    if (id >= 7 && id <= 10)
        return {224 + (id - 7) * 32, on ? 0 : 32, 32, 32};
    if (id >= 11 && id <= 14)
        return {352 + (id - 11) * 32, on ? 0 : 32, 32, 32};
    return {(id - 20) * 32, 64, 32, 32};
}

void drawBlock(int x, int y, int id, bool on)
{
    if (rm->spr_blocks[0])
        rm->spr_blocks[0].draw(x, y, blockSrc(id, on));
}

// ---- laying out and drawing the window: every widget drawn is also clickable

struct Layout {
    SDL_Rect win {};
    int x = 0;      // the page's left
    int w = 0;      // and width
    int y = 0;      // where the next thing goes
};

// While the page is drawn: its visible part (widgets outside it can't be clicked)
SDL_Rect g_clip {0, 0, SCREEN_W, SCREEN_H};

void addWidget(const SDL_Rect& r, std::function<void()> act, std::string tip)
{
    SDL_Rect visible {};
    if (!SDL_GetRectIntersection(&r, &g_clip, &visible))
        return;
    g_widgets.push_back(Widget {r, std::move(act), std::move(tip)});
}

bool hovered(const SDL_Rect& r)
{
    if (s.hover < 0 || s.hover >= static_cast<int>(g_widgets.size()))
        return false;
    const SDL_Rect& h = g_widgets[static_cast<size_t>(s.hover)].r;
    return h.x == r.x && h.y == r.y && h.w == r.w && h.h == r.h;
}

// Text wrapped to a width (at most maxLines lines: the last one ends in "..." if cut); returns the height used
int drawWrapped(int x, int y, int w, const std::string& text, int maxLines = 99)
{
    gfxFont& f = theme::font();
    const int lineH = f.getHeight();
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t space = text.find(' ', pos);
        if (space == std::string::npos)
            space = text.size();
        const std::string word = text.substr(pos, space - pos);
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.getWidth(trial) > w) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        pos = space + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<size_t>(maxLines));
        std::string& last = lines.back();
        while (!last.empty() && f.getWidth(last + "...") > w)
            last.pop_back();
        last += "...";
    }
    for (size_t k = 0; k < lines.size(); k++)
        f.draw(x, y + static_cast<int>(k) * lineH, lines[k]);
    return static_cast<int>(lines.size()) * lineH;
}

// A palette cell (a 32 pixel picture, framed): gold when selected, green under the pointer
void cell(const SDL_Rect& r, bool selected, const std::function<void(int, int)>& picture, std::function<void()> act, const std::string& tip)
{
    const bool hover = hovered(r);
    theme::block(r.x, r.y, r.w, r.h, selected ? theme::Tone::Gold : hover ? theme::Tone::Green : theme::Tone::Dark);
    picture(r.x + (r.w - 32) / 2, r.y + (r.h - 32) / 2);
    addWidget(r, std::move(act), tip);
}

// A button with a label (and an icon)
void button(const SDL_Rect& r, const std::string& label, bool selected, std::function<void()> act, const std::string& tip,
    const Art* icon = nullptr, const char* key = nullptr)
{
    const bool hover = hovered(r);
    theme::block(r.x, r.y, r.w, r.h, hover ? theme::Tone::Green : selected ? theme::Tone::Gold : theme::Tone::Gray);
    int tx = r.x + 8;
    if (icon) {
        drawArt(r.x + 5, r.y + (r.h - 16) / 2, *icon);
        tx = r.x + 26;
    }
    theme::font().draw(tx, r.y + (r.h - theme::font().getHeight()) / 2, label);
    if (key) {
        const int kw = std::max(16, theme::font().getWidth(key) + 8);
        prompts::drawKey(r.x + r.w - kw - 4, r.y + (r.h - 16) / 2, key);
    }
    addWidget(r, std::move(act), tip);
}

// A small labelled heading in the page
void section(Layout& l, const char* title)
{
    theme::font().draw(l.x, l.y, title);
    fill(l.x, l.y + theme::font().getHeight() + 1, l.w, 1, theme::shade(theme::scheme().edge.a ? theme::scheme().edge : theme::scheme().rim, 70));
    l.y += theme::font().getHeight() + 5;
}

// A grid of palette cells
template <typename F>
void grid(Layout& l, int count, F each, int wantCols = 0)
{
    const int size = 36;
    const int cols = wantCols > 0 ? wantCols : std::max(1, (l.w + 2) / (size + 2));
    for (int k = 0; k < count; k++) {
        const SDL_Rect r {l.x + (k % cols) * (size + 2), l.y + (k / cols) * (size + 2), size, size};
        each(k, r);
    }
    l.y += ((count + cols - 1) / cols) * (size + 2) + 6;
}

void pageButton(Layout& l, const std::string& label, std::function<void()> act, const std::string& tip, const Art* icon = nullptr,
    const char* key = nullptr, bool selected = false)
{
    button({l.x, l.y, l.w, 24}, label, selected, std::move(act), tip, icon, key);
    l.y += 27;
}

// ---- the pages

struct Page {
    const char* name;
    const Art* art;           // its icon (or a sprite, see drawPageIcon)
    const char* about;        // what it is
};

const Page LEVEL_PAGES[] = {
    {"Terrain", &ART_GROUND, "The ground, walls and scenery: tiles from the tilesets, painted on one of 4 layers."},
    {"Blocks", nullptr, "Things players can bump, break, bounce on and flip. Pick one, then click on the map."},
    {"Items", &ART_SPRING, "Springs, spikes, shoes and boxes that start on the map."},
    {"Types", &ART_TYPES, "How the tiles behave: solid, solid on top, ice, or deadly. Painted over the tiles."},
    {"Zones", &ART_ZONE, "Where nobody starts, or no item appears: paint the cells."},
    {"Warps", &ART_PIPE, "Pipes and doors that take players to another warp of the same color."},
    {"Platforms", &ART_PLATFORM, "Moving platforms: their tiles, path and speed."},
    {"Hazards", &ART_FIRE, "Fireballs, rotodiscs, bullet bills and other things that move on their own."},
    {"Modes", &ART_FLAG, "What some game modes need: race goals and flag bases."},
    {"Look", &ART_LOOK, "The background, the eyecandy and the music of the map."},
    {"File", &ART_DISK, "Maps: new, open, save, and leaving the editor."},
};
constexpr int LEVEL_PAGE_COUNT = static_cast<int>(sizeof(LEVEL_PAGES) / sizeof(LEVEL_PAGES[0]));

// The world editor's pages: its commands, by group
std::vector<std::string> worldGroups()
{
    std::vector<std::string> groups;
    for (const Action& a : WORLD_ACTIONS) {
        if (a.group[0])
            groups.push_back(a.group);
    }
    return groups;
}

int pageCount()
{
    return s.editor == Editor::Level ? LEVEL_PAGE_COUNT : static_cast<int>(worldGroups().size());
}

void drawPageIcon(int x, int y, int page)
{
    if (s.editor != Editor::Level) {
        static const Art* arts[] = {&ART_BRUSH, &ART_LOOK, &ART_DISK, &ART_CLOSE};
        drawArt(x, y, *arts[std::min(page, 3)]);
        return;
    }
    if (LEVEL_PAGES[page].art)
        drawArt(x, y, *LEVEL_PAGES[page].art);
    else
        theme::icon(x, y, theme::Icon::QuestionBlock);
}

void levelPage(Layout& l)
{
    namespace sh = leveleditor::shell;
    const int mode = sh::mode();
    switch (s.category) {
        case 0: {    //Terrain
            section(l, "Selected tiles");
            const SDL_Rect box {l.x, l.y, 72, 72};
            theme::inset(box.x, box.y, box.w, box.h);
            if (!sh::drawTiles(box.x + 4, box.y + 4, 64))
                theme::font().drawCentered(box.x + 36, box.y + 28, "none");
            theme::font().draw(l.x + 82, l.y + 4, mode == 1 ? "In use," : "Not in use");
            theme::font().draw(l.x + 82, l.y + 20, "layer " + std::to_string(sh::layer() + 1));
            l.y += 80;
            pageButton(l, "Choose tiles", [] { runKey(SDLK_T); }, "Opens the tilesets: drag over tiles to take several", &ART_GROUND, "T",
                mode == 1);
            pageButton(l, "Animated tiles", [] { runKey(SDLK_A); }, "Tiles that move: water, lava, coins...", &ART_LOOK, "A", mode == 8);
            pageButton(l, "Use the tiles", [] { sh::setMode(1); }, "Back to painting the selected tiles", &ART_BRUSH, nullptr, false);
            break;
        }
        case 1: {    //Blocks
            section(l, "Blocks");
            grid(l, 12, [&](int k, const SDL_Rect& r) {
                const Entry& e = BLOCKS[k];
                cell(r, mode == 0 && sh::block() == e.id, [&](int x, int y) { drawBlock(x, y, e.id, true); },
                    [id = e.id] { sh::setBlock(id, true); }, std::string(e.name) + ": " + e.tip);
            });
            section(l, "ON/OFF switches");
            grid(l, 8, [&](int k, const SDL_Rect& r) {
                const int color = k % 4;
                const bool on = k < 4;
                const int id = 7 + color;
                cell(r, mode == 0 && sh::block() == id && sh::switchOn(color) == on, [&](int x, int y) { drawBlock(x, y, id, on); },
                    [id, on] { sh::setBlock(id, on); },
                    std::string(COLORS[color]) + " switch, starts " + (on ? "ON" : "OFF") + ": hitting it flips every " + COLORS[color] + " switch block");
            }, 4);
            section(l, "Switch blocks");
            grid(l, 8, [&](int k, const SDL_Rect& r) {
                const int color = k % 4;
                const bool on = k < 4;
                const int id = 11 + color;
                cell(r, mode == 0 && sh::block() == id && sh::switchBlockOn() == on, [&](int x, int y) { drawBlock(x, y, id, on); },
                    [id, on] { sh::setBlock(id, on); },
                    std::string(COLORS[color]) + " switch block, " + (on ? "solid" : "an outline") + " at the start: the " + COLORS[color] + " switches flip it");
            }, 4);
            section(l, "Breakable only by...");
            grid(l, 10, [&](int k, const SDL_Rect& r) {
                const Entry& e = BREAKABLE[k];
                cell(r, mode == 0 && sh::block() == e.id, [&](int x, int y) { drawBlock(x, y, e.id, true); },
                    [id = e.id] { sh::setBlock(id, true); }, std::string(e.name) + ": " + e.tip);
            });
            break;
        }
        case 2:    //Items
            section(l, "Map items");
            grid(l, 6, [&](int k, const SDL_Rect& r) {
                const Entry& e = ITEMS[k];
                cell(r, mode == 7 && sh::item() == e.id,
                    [&](int x, int y) {
                        if (rm->spr_mapitems[0])
                            rm->spr_mapitems[0].draw(x, y, {e.id * 32, 0, 32, 32});
                    },
                    [id = e.id] { sh::setItem(id); }, std::string(e.name) + ": " + e.tip);
            });
            break;
        case 3:    //Types
            section(l, "Tile types");
            grid(l, 18, [&](int k, const SDL_Rect& r) {
                cell(r, mode == 6 && sh::tileType() == k + 1,
                    [&](int x, int y) {
                        if (rm->spr_transparenttiles)
                            rm->spr_transparenttiles.draw(x, y, {k * 32, 0, 32, 32});
                    },
                    [k] { sh::setTileType(k + 1); }, TILE_TYPES[k]);
            });
            l.y += drawWrapped(l.x, l.y, l.w, "Delete clears every tile type of the map.") + 6;
            break;
        case 4:    //Zones
            pageButton(l, "No player starts", [] { runKey(SDLK_X, false, false, true); }, "Cells where no player appears at the start or after dying",
                &ART_ZONE, "X", mode == 4);
            pageButton(l, "No items", [] { runKey(SDLK_Z, false, false, true); }, "Cells where no item appears", &ART_ZONE, "Z", mode == 5);
            l.y += 4;
            l.y += drawWrapped(l.x, l.y, l.w, "Click or drag on the map to mark cells, the right button clears them.") + 6;
            if (mode == 4)
                l.y += drawWrapped(l.x, l.y, l.w, "Keys: X everyone, 1-4 a team's own zone; A all cells, N none, I invert.") + 6;
            break;
        case 5:    //Warps
            pageButton(l, "Warps", [] { runKey(SDLK_W); }, "Pick a warp's color and direction, then click its cells", &ART_PIPE, "W", mode == 2);
            break;
        case 6:    //Platforms
            pageButton(l, "Platforms", [] { runKey(SDLK_P); }, "Draw a platform's tiles, then set its path and speed", &ART_PLATFORM, "P");
            break;
        case 7:    //Hazards
            pageButton(l, "Hazards", [] { runKey(SDLK_H); }, "Add, move and set up the hazards", &ART_FIRE, "H");
            break;
        case 8:    //Modes
            pageButton(l, "Race goals, flag bases", [] { runKey(SDLK_J); }, "Where the race goals and the flag bases are", &ART_FLAG, "J");
            pageButton(l, "? block contents", [] { closeWindow(); }, "Point at a ? block on the map and press K to choose what comes out of it",
                nullptr, "K");
            break;
        case 9:    //Look
            pageButton(l, "Background", [] { runKey(SDLK_G); }, "Pick the map's background picture", &ART_LOOK, "G");
            pageButton(l, "Eyecandy", [] { runKey(SDLK_E); }, "Clouds, ghosts, leaves, snow, bubbles...", &ART_LOOK, "E");
            pageButton(l, "Music", [] { runKey(SDLK_R, false, false, true); }, "Which music plays: changes the category", &ART_LOOK, "R");
            pageButton(l, "Thumbnails", [] { runKey(SDLK_B); }, "The pictures of the map in the menus", &ART_LOOK, "B");
            break;
        default: {    //File
            bool file = false;
            for (const Action& a : LEVEL_ACTIONS) {
                if (a.group[0])
                    file = std::string(a.group) == "File" || std::string(a.group) == "Other";
                if (!file)
                    continue;
                const std::string key = keyName(a);
                pageButton(l, a.label, [&a] { runKey(a.key, a.shift, a.ctrl); }, a.label, nullptr, nullptr);
                //(its key, drawn over the button's right end)
                const int kw = std::max(16, theme::font().getWidth(key) + 8);
                prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, key);
            }
            break;
        }
    }
}

void worldPage(Layout& l)
{
    const std::vector<std::string> groups = worldGroups();
    const std::string group = groups[static_cast<size_t>(std::clamp(s.category, 0, static_cast<int>(groups.size()) - 1))];
    bool in = false;
    for (const Action& a : WORLD_ACTIONS) {
        if (a.group[0])
            in = group == a.group;
        if (!in)
            continue;
        const std::string key = keyName(a);
        pageButton(l, a.label, [&a] { runKey(a.key, a.shift, a.ctrl); }, a.label);
        const int kw = std::max(16, theme::font().getWidth(key) + 8);
        prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, key);
    }
}

// What is selected now, small (the window's title row, the chip)
std::string selectionName()
{
    if (s.editor != Editor::Level)
        return {};
    namespace sh = leveleditor::shell;
    switch (sh::mode()) {
        case 0:
            for (const Entry& e : BLOCKS)
                if (e.id == sh::block())
                    return e.name;
            for (const Entry& e : BREAKABLE)
                if (e.id == sh::block())
                    return e.name;
            if (sh::block() >= 7 && sh::block() <= 10)
                return std::string(COLORS[sh::block() - 7]) + " switch";
            if (sh::block() >= 11 && sh::block() <= 14)
                return std::string(COLORS[sh::block() - 11]) + " switch block";
            return "Blocks";
        case 1: return "Tiles";
        case 2: return "Warps";
        case 3: return "Select and move";
        case 4: return "No player starts";
        case 5: return "No items";
        case 6: return sh::tileType() >= 1 && sh::tileType() <= 18 ? TILE_TYPES[sh::tileType() - 1] : "Tile types";
        case 7: return sh::item() >= 0 && sh::item() < 6 ? ITEMS[sh::item()].name : "Items";
        case 8: return "Animated tiles";
        default: return {};
    }
}

void drawSelection(int x, int y)
{
    namespace sh = leveleditor::shell;
    switch (sh::mode()) {
        case 0: drawBlock(x, y, sh::block(), sh::block() >= 11 && sh::block() <= 14 ? sh::switchBlockOn() : (sh::block() >= 7 && sh::block() <= 10 ? sh::switchOn(sh::block() - 7) : true)); break;
        case 1:
        case 8:
            if (!sh::drawTiles(x, y, 32))
                drawArt(x + 8, y + 8, ART_GROUND);
            break;
        case 2: drawArt(x + 8, y + 8, ART_PIPE); break;
        case 3: drawArt(x + 8, y + 8, ART_MOVE); break;
        case 4:
        case 5: drawArt(x + 8, y + 8, ART_ZONE); break;
        case 6:
            if (rm->spr_transparenttiles)
                rm->spr_transparenttiles.draw(x, y, {(sh::tileType() - 1) * 32, 0, 32, 32});
            break;
        case 7:
            if (rm->spr_mapitems[0])
                rm->spr_mapitems[0].draw(x, y, {sh::item() * 32, 0, 32, 32});
            break;
        default: break;
    }
}

void drawWindow()
{
    g_widgets.clear();
    const SDL_Rect win = windowRect();
    theme::panel(win.x - 6, win.y - 6, win.w + 12, win.h + 12);
    const bool level = s.editor == Editor::Level;
    namespace sh = leveleditor::shell;

    //Title: the editor, the map, and the window's own buttons
    theme::strip(win.x + 4, win.y + 4, win.w - 8, 30);
    theme::icon(win.x + 12, win.y + 11, level ? theme::Icon::QuestionBlock : theme::Icon::Star);
    theme::font().draw(win.x + 32, win.y + 12, level ? "Level Editor" : "World Editor");
    if (level) {
        std::string name = sh::mapName();
        const int room = win.w - 150 - theme::font().getWidth("Level Editor");
        while (!name.empty() && theme::font().getWidth(name) > room)
            name.pop_back();
        theme::font().drawRightJustified(win.x + win.w - 62, win.y + 12, name);
    }
    {
        const SDL_Rect swap {win.x + win.w - 56, win.y + 8, 24, 22};
        theme::block(swap.x, swap.y, swap.w, swap.h, hovered(swap) ? theme::Tone::Green : theme::Tone::Gray);
        drawArt(swap.x + 4, swap.y + 3, ART_SWAP);
        addWidget(swap, [] { s.dockLeft = !s.dockLeft; s.overlayDirty = true; }, "Move the window to the other side");
        const SDL_Rect close {win.x + win.w - 30, win.y + 8, 22, 22};
        theme::block(close.x, close.y, close.w, close.h, hovered(close) ? theme::Tone::Red : theme::Tone::Gray);
        drawArt(close.x + 3, close.y + 3, ART_CLOSE);
        addWidget(close, [] { closeWindow(); }, "Close (Tab)");
    }

    //Tools: paint, erase, move, pick; the layer; what is shown
    int ty = win.y + 40;
    if (level) {
        static const Art* toolArt[] = {&ART_BRUSH, &ART_ERASER, &ART_MOVE, &ART_PICK};
        static const char* toolTip[] = {"Paint: click or drag to place what is selected (the right button removes)",
            "Erase: click or drag to remove", "Select and move: drag over cells, then drag them somewhere else (M)",
            "Pick: click the map to take the block or tile there"};
        int x = win.x + 8;
        for (int t = 0; t < TOOL_COUNT; t++) {
            const SDL_Rect r {x, ty, 26, 26};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : currentTool() == t ? theme::Tone::Gold : theme::Tone::Gray);
            drawArt(r.x + 5, r.y + 5, *toolArt[t]);
            addWidget(r, [t] { setTool(t); }, toolTip[t]);
            x += 28;
        }
        x += 8;
        //The layers: four buttons under a tiny label
        prompts::tinyText(x + 1, ty - 1, "LAYER", {0, 0, 0, 255}, 1);
        prompts::tinyText(x, ty - 2, "LAYER", {255, 255, 255, 255}, 1);
        for (int layer = 0; layer < 4; layer++) {
            const SDL_Rect r {x, ty + 5, 20, 21};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : sh::layer() == layer ? theme::Tone::Gold : theme::Tone::Gray);
            theme::font().drawCentered(r.x + 10, r.y + 3, std::to_string(layer + 1));
            addWidget(r, [layer] { sh::setLayer(layer); }, "Paint on layer " + std::to_string(layer + 1) + " of 4 (Y goes through them)");
            x += 22;
        }
        x += 6;
        const SDL_Rect only {x, ty, 26, 26};
        theme::block(only.x, only.y, only.w, only.h, hovered(only) ? theme::Tone::Green : sh::othersHidden() ? theme::Tone::Gold : theme::Tone::Gray);
        drawArt(only.x + 5, only.y + 5, ART_EYE);
        addWidget(only, [] { sh::setOthersHidden(!sh::othersHidden()); }, "Show only the active layer (U)");
        x += 28;
        const SDL_Rect blocks {x, ty, 26, 26};
        theme::block(blocks.x, blocks.y, blocks.w, blocks.h, hovered(blocks) ? theme::Tone::Green : sh::blocksShown() ? theme::Tone::Gray : theme::Tone::Gold);
        theme::icon(blocks.x + 5, blocks.y + 5, theme::Icon::QuestionBlock);
        if (!sh::blocksShown())
            fill(blocks.x + 4, blocks.y + 12, 18, 2, {216, 40, 32, 255});
        addWidget(blocks, [] { sh::setBlocksShown(!sh::blocksShown()); }, "Hide the blocks, to see the tiles under them (V)");
        ty += 32;
    }

    //The pages: a rail of icons, the page beside it
    const int bodyTop = ty;
    const int footH = 66;
    const int bodyBottom = win.y + win.h - footH;
    const int count = pageCount();
    const int itemH = std::min(36, (bodyBottom - bodyTop) / count);
    const std::vector<std::string> groups = level ? std::vector<std::string>() : worldGroups();
    for (int k = 0; k < count; k++) {
        const SDL_Rect r {win.x + 8, bodyTop + k * itemH, RAIL_W - 6, itemH - 2};
        const bool sel = s.category == k;
        theme::block(r.x, r.y, r.w, r.h, sel ? theme::Tone::Gold : hovered(r) ? theme::Tone::Green : theme::Tone::Dark);
        const std::string label = level ? LEVEL_PAGES[k].name : groups[static_cast<size_t>(k)];
        //Its icon, and its name in small capitals (with a shadow)
        std::string caps = label;
        for (char& ch : caps)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const int iconY = r.y + std::max(2, (r.h - 25) / 2);
        drawPageIcon(r.x + (r.w - 16) / 2, iconY, k);
        const int lw = prompts::tinyWidth(caps, 1);
        prompts::tinyText(r.x + (r.w - lw) / 2 + 1, iconY + 19, caps, {0, 0, 0, 255}, 1);
        prompts::tinyText(r.x + (r.w - lw) / 2, iconY + 18, caps, {255, 255, 255, 255}, 1);
        addWidget(r, [k] { s.category = k; s.scroll = 0; s.hover = -1; s.overlayDirty = true; }, label);
    }

    Layout l;
    l.win = win;
    l.x = win.x + RAIL_W + 10;
    l.w = win.x + win.w - 16 - l.x;
    l.y = bodyTop + 2;
    const std::string title = level ? LEVEL_PAGES[std::clamp(s.category, 0, count - 1)].name : groups[static_cast<size_t>(std::clamp(s.category, 0, count - 1))];
    theme::bigFont().draw(l.x, l.y - 2, title);
    l.y += theme::bigFont().getHeight() + 2;
    //The page below the title scrolls (the wheel, or the right stick over the window)
    const int viewTop = l.y;
    const int viewH = bodyBottom - 4 - viewTop;
    g_clip = SDL_Rect {l.x - 2, viewTop, l.w + 4, viewH};
    SDL_SetSurfaceClipRect(screen, &g_clip);
    l.y -= s.scroll;
    const int contentTop = l.y;
    if (level)
        l.y += drawWrapped(l.x, l.y, l.w, LEVEL_PAGES[s.category].about) + 8;
    if (level)
        levelPage(l);
    else
        worldPage(l);
    SDL_SetSurfaceClipRect(screen, nullptr);
    g_clip = SDL_Rect {0, 0, SCREEN_W, SCREEN_H};
    s.scrollMax = std::max(0, l.y - contentTop - viewH);
    if (s.scroll > s.scrollMax)
        s.scroll = s.scrollMax;
    if (s.scrollMax > 0) {
        //A scroll bar at the page's right edge
        const int barX = l.x + l.w + 3;
        fill(barX, viewTop, 3, viewH, {0, 0, 0, 255});
        const int thumbH = std::max(16, viewH * viewH / (viewH + s.scrollMax));
        const int thumbY = viewTop + (viewH - thumbH) * s.scroll / s.scrollMax;
        fill(barX, thumbY, 3, thumbH, {255, 255, 255, 255});
    }

    //Footer: what is under the pointer, and how to close
    const int fy = win.y + win.h - footH + 4;
    theme::inset(win.x + 8, fy, win.w - 16, footH - 12);
    const std::string tip = s.hover >= 0 && s.hover < static_cast<int>(g_widgets.size()) ? g_widgets[static_cast<size_t>(s.hover)].tip
                                                                                           : (level ? "Selected: " + selectionName() : std::string("Click a command"));
    drawWrapped(win.x + 14, fy + 4, win.w - 28, tip, 2);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller)
        row = {{{prompts::Pad::A}, {}, "pick"}, {{prompts::Pad::LB, prompts::Pad::RB}, {}, "pages"}, {{prompts::Pad::Start}, {}, "close"}};
    else
        row = {{{}, {"Tab"}, "close"}, {{}, {"F1"}, "help"}};
    const int pw = prompts::promptsWidth(row, 14);
    prompts::drawPrompts(win.x + win.w - 14 - pw / 2, fy + footH - 33, row);
}

// A player's reticle on the cell it points at: corner brackets in its color, and its tag
void reticle(float px, float py, SDL_Color c, const std::string& tag)
{
    const int x = static_cast<int>(px) / TILE * TILE;
    const int y = static_cast<int>(py) / TILE * TILE;
    const SDL_Color black {0, 0, 0, 255};
    const int len = 10;
    //Each corner: black under, the color on top
    auto corner = [&](int cx, int cy, int dx, int dy) {
        const int hx = dx > 0 ? cx : cx - len + 1;
        const int vy = dy > 0 ? cy : cy - len + 1;
        fill(hx - 1, cy - 1 - (dy < 0 ? 2 : 0), len + 2, 5, black);
        fill((dx > 0 ? cx : cx - 2) - 1 - (dx < 0 ? 0 : 0), vy - 1, 5, len + 2, black);
        fill(hx, dy > 0 ? cy : cy - 2, len, 3, c);
        fill(dx > 0 ? cx : cx - 2, vy, 3, len, c);
    };
    corner(x - 2, y - 2, 1, 1);
    corner(x + TILE + 1, y - 2, -1, 1);
    corner(x - 2, y + TILE + 1, 1, -1);
    corner(x + TILE + 1, y + TILE + 1, -1, -1);
    //The center: a small dot
    fill(x + TILE / 2 - 2, y + TILE / 2 - 2, 4, 4, black);
    fill(x + TILE / 2 - 1, y + TILE / 2 - 1, 2, 2, c);
    if (!tag.empty()) {
        //The tag on the top left corner (inside the screen)
        const int w = theme::font().getWidth(tag) + 8;
        const int tx = std::clamp(x - 2, 0, SCREEN_W - w);
        const int ty = y - 20 >= 0 ? y - 20 : y + TILE + 4;
        fill(tx, ty, w, 16, black);
        fill(tx + 1, ty + 1, w - 2, 14, c);
        fill(tx + 1, ty + 1, w - 2, 2, theme::shade(c, 140));
        theme::font().draw(tx + 4, ty + 1, tag);
    }
}

// The level editor's chip when the window is closed: what is selected, the layer, the cell
void drawChip()
{
    namespace sh = leveleditor::shell;
    const std::string name = selectionName();
    const int col = static_cast<int>(s.cx) / TILE;
    const int row = static_cast<int>(s.cy) / TILE;
    const std::string info = "Layer " + std::to_string(sh::layer() + 1) + "   " + std::to_string(col) + "," + std::to_string(row);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const std::vector<prompts::Prompt> row1 = controller ? std::vector<prompts::Prompt> {{{prompts::Pad::Start}, {}, "editor"}}
                                                         : std::vector<prompts::Prompt> {{{}, {"Tab"}, "editor"}};
    const int pw = prompts::promptsWidth(row1, 14);
    const int w = 46 + std::max(theme::font().getWidth(name), theme::font().getWidth(info)) + 16 + pw + 10;
    const int h = 46;
    //(bottom left; at the top while a pointer is down there)
    bool low = s.cy > SCREEN_H - 90 && s.cx < w + 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - 90 && p.x < w + 30);
    const int x = 6;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::inset(x + 6, y + 6, 34, 34);
    drawSelection(x + 7, y + 7);
    theme::font().draw(x + 46, y + 7, name);
    theme::font().draw(x + 46, y + 24, info);
    prompts::drawPrompts(x + w - pw / 2 - 8, y + 14, row1);
}


void drawKeyboard()
{
    const SDL_Rect first = oskKeyRect(0, 0);
    const int w = OSK_COLS * OSK_KEY_W + 24;
    const int h = (OSK_TEXT_ROWS + 1) * OSK_KEY_H + 56;
    //(wide enough for the buttons' row too)
    int rowW = 0;
    if (prompts::lastDevice() == prompts::Device::Controller) {
        for (prompts::Pad pad : {prompts::Pad::A, prompts::Pad::B, prompts::Pad::X, prompts::Pad::Y, prompts::Pad::Start, prompts::Pad::Back})
            rowW += prompts::padWidth(pad) + 13;
        rowW += theme::font().getWidth("typedeleteshiftspacedonecancel");
    }
    const int bw = std::max(w, rowW + 24);
    frameBox(SCREEN_W / 2 - bw / 2, first.y - 10, bw, h);
    for (int row = 0; row <= OSK_TEXT_ROWS; row++) {
        for (int col = 0; col < OSK_COLS; col++) {
            const bool special = row == OSK_TEXT_ROWS;
            if (special && col % 2)
                continue;
            const SDL_Rect r = oskKeyRect(row, col);
            const bool selected = s.oskRow == row && (special ? s.oskCol / 2 == col / 2 : s.oskCol == col);
            const bool hover = s.hoverKey == row * OSK_COLS + col || (special && s.hoverKey == row * OSK_COLS + col + 1);
            const bool lit = special && col / 2 == 0 && s.oskShift;
            theme::Tone tone = theme::Tone::Gray;
            if (lit)
                tone = theme::Tone::Gold;
            if (hover || (selected && prompts::lastDevice() == prompts::Device::Controller))
                tone = theme::Tone::Green;
            theme::block(r.x, r.y, r.w, r.h, tone);
            std::string label;
            if (special) {
                label = OSK_KEYS[static_cast<size_t>(col / 2)];
            } else {
                char ch = OSK_ROWS[static_cast<size_t>(row)][col];
                if (s.oskShift && ch >= 'a' && ch <= 'z')
                    ch = static_cast<char>(ch - 32);
                label = ch == ' ' ? std::string("spc") : std::string(1, ch);
            }
            theme::font().drawCentered(r.x + r.w / 2, r.y + 2, label);
        }
    }
    //The buttons, with the device in use
    const int fy = first.y - 10 + h - 24;
    std::vector<std::tuple<prompts::Pad, std::string, std::string>> row = {
        {prompts::Pad::A, "", "type"}, {prompts::Pad::B, "", "delete"}, {prompts::Pad::X, "", "shift"},
        {prompts::Pad::Y, "", "space"}, {prompts::Pad::Start, "", "done"}, {prompts::Pad::Back, "", "cancel"}};
    if (prompts::lastDevice() == prompts::Device::Controller) {
        int tw = 0;
        for (const auto& [pad, key, label] : row)
            tw += prompts::promptWidth(pad, key, label) + 10;
        int x = SCREEN_W / 2 - tw / 2;
        for (const auto& [pad, key, label] : row)
            x += prompts::drawPrompt(x, fy, pad, key, label) + 10;
    } else {
        theme::font().drawCentered(SCREEN_W / 2, fy, "Type, or click the keys");
    }
}

// Where the hint bar is: shown, and its top
bool hintsShown(int* top)
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const bool shown = controller ? s.hints : SDL_GetTicks() - s.startMs <= 8000;
    //(at the bottom, or at the top while a cursor is down there)
    bool low = s.cy > SCREEN_H - 70;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - 70);
    if (top)
        *top = low ? 20 : SCREEN_H - 22;
    return shown && s.overlay == Overlay::None;
}

// The controller's buttons (LB hides it); on the keyboard, how to open the actions
void drawHints()
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    int y = 0;
    if (!hintsShown(&y))
        return;
    std::vector<std::tuple<prompts::Pad, std::string, std::string>> items;
    if (controller)
        items = {{prompts::Pad::A, "", "place"}, {prompts::Pad::B, "", "remove"}, {prompts::Pad::X, "", "shift"},
            {prompts::Pad::Y, "", "ctrl"}, {prompts::Pad::RB, "", "ok"}, {prompts::Pad::Start, "", "editor"},
            {prompts::Pad::Back, "", "back"}, {prompts::Pad::LB, "", "hide"}};
    else
        items = {{prompts::Pad::Start, "Tab", "editor"}, {prompts::Pad::Back, "F1", "help"}, {prompts::Pad::B, "Esc", "back"}};
    const int gap = 7;
    int w = 8;
    for (const auto& [pad, key, label] : items)
        w += prompts::promptWidth(pad, key, label) + gap;
    const int x = (SCREEN_W - w) / 2;
    theme::strip(x - 6, y - 6, w + 12, 28);
    int ix = x + 4;
    for (const auto& [pad, key, label] : items)
        ix += prompts::drawPrompt(ix, y, pad, key, label) + gap;
}

// Who is editing (online), and the latest notice
void drawSession()
{
    const std::vector<coedit::Person> people = coedit::people();
    if (!people.empty()) {
        const int lineH = 18;
        int w = theme::font().getWidth("Editing together") + 28;
        for (const coedit::Person& p : people)
            w = std::max(w, theme::font().getWidth(p.name + (p.you ? " (you)" : "")) + (p.host ? 64 : 46));
        const int h = lineH * static_cast<int>(people.size()) + 46;
        const int x = 2;
        //(above the hint bar when it is at the bottom)
        int hintsTop = 0;
        const int bottom = hintsShown(&hintsTop) && hintsTop > SCREEN_H / 2 ? hintsTop - 6 : SCREEN_H;
        const int y = bottom - h;
        theme::panel(x, y, w, h);
        theme::font().draw(x + 14, y + 10, "Editing together");
        int py = y + 30;
        for (const coedit::Person& p : people) {
            //A coin in their color
            prompts::art(x + 14, py + 1, {
                ".####.",
                "#llcc#",
                "#lcDc#",
                "#ccDc#",
                "#ccDd#",
                "#cccd#",
                "#ccdd#",
                ".####.",
            }, {{'#', {0, 0, 0, 255}}, {'l', theme::shade(p.color, 150)}, {'c', p.color}, {'d', theme::shade(p.color, 60)},
                {'D', theme::shade(p.color, 75)}}, 2);
            const std::string name = p.name + (p.you ? " (you)" : "");
            theme::font().draw(x + 32, py, name);
            if (p.host)
                theme::crown(x + 36 + theme::font().getWidth(name), py + 1);
            py += lineH;
        }
    }
    const std::string notice = coedit::notice();
    if (!notice.empty()) {
        //An announcement (the card: a 1-up for who came, else a Bob-omb)
        const bool came = notice.find("joined") != std::string::npos;
        theme::announce(SCREEN_W / 2, 36, notice, [came](int ix, int iy) {
            theme::statusIcon(ix + 8, iy + 8, came ? theme::Status::Good : theme::Status::Warn);
        });
    }
}

bool overlayWanted()
{
    if (s.overlay != Overlay::None || coedit::active() || !coedit::notice().empty() || levelMap() || s.mouseUsed)
        return true;
    if (prompts::lastDevice() == prompts::Device::Controller ? s.hints && !s.pointers.empty() : SDL_GetTicks() - s.startMs < 8000)
        return true;
    for (const Pointer& p : s.pointers) {
        if (p.shown)
            return true;
    }
    return false;
}

void drawOverlay()
{
    if (!rm)
        return;
    const bool mapView = s.editor != Editor::Level || leveleditor::shell::inMapView();
    const bool keyboard = s.overlay == Overlay::Keyboard;
    const std::vector<coedit::Cursor> remote = coedit::remoteCursors();
    //Where everyone edits: a reticle on each player's cell
    if (mapView && !keyboard) {
        const bool several = s.pointers.size() > 1 || coedit::active();
        for (const coedit::Cursor& c : remote) {
            if (!inWindow(c.x, c.y))
                reticle(c.x, c.y, c.color, c.name);
        }
        if (s.owner < 0 && s.mouseUsed && !inWindow(s.cx, s.cy))
            reticle(s.cx, s.cy, pointerColor(0), several ? (coedit::active() ? "You" : "P1") : std::string());
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown && !inWindow(p.x, p.y))
                reticle(p.x, p.y, pointerColor(static_cast<int>(k)), several ? "P" + std::to_string(k + 1) : std::string());
        }
    }
    if (s.overlay == Overlay::None) {
        if (levelMap())
            drawChip();
        else
            drawHints();
    }
    drawSession();
    if (s.overlay == Overlay::Window)
        drawWindow();
    else if (keyboard)
        drawKeyboard();
    //The cursors on top (the controllers' drawn pointers; the others' online)
    for (const coedit::Cursor& c : remote)
        drawArrow(c.x, c.y, c.color, mapView ? std::string() : c.name);
    if (!keyboard) {
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                drawArrow(p.x, p.y, pointerColor(static_cast<int>(k)), std::string());
        }
    }
}

// Presents the frame with the overlay; the screen keeps the editor's frame
void present()
{
    if (!screen)
        return;
    if (!s.frame || s.frame->w != screen->w || s.frame->h != screen->h || s.frame->format != screen->format) {
        if (s.frame)
            SDL_DestroySurface(s.frame);
        s.frame = SDL_CreateSurface(screen->w, screen->h, screen->format);
    }
    const bool overlay = overlayWanted();
    if (overlay && s.frame) {
        SDL_BlitSurface(screen, nullptr, s.frame, nullptr);
        drawOverlay();
    }
    if (!g_pendingShot.empty()) {
        gfx_save_screenshot(g_testShotDir + "/" + g_pendingShot + ".png");
        printf("shots: saved %s/%s.png\n", g_testShotDir.c_str(), g_pendingShot.c_str());
        g_pendingShot.clear();
    }
    gfx_flipscreen();
    if (overlay && s.frame)
        SDL_BlitSurface(s.frame, nullptr, screen, nullptr);
    s.lastPresentMs = SDL_GetTicks();
    s.overlayDirty = false;
}

Pad fromTest(const TestInput& t)
{
    Pad p;
    p.buttons = {t.a, t.b, t.x, t.y, t.lb, t.rb, t.back, t.start, t.up, t.down, t.left, t.right};
    p.lx = t.lx;
    p.ly = t.ly;
    p.rx = t.rx;
    p.ry = t.ry;
    return p;
}

// Once per frame (the first poll of the editor's event loop)
void update()
{
    //Editing together: the network, and our document once the editor has loaded it
    coedit::pump();
    if (coedit::active() && !coedit::attached())
        coedit::attach(s.editor == Editor::Level ? leveleditor::makeCoeditDocument() : worldeditor::makeCoeditDocument());
    if (!coedit::notice().empty())
        s.overlayDirty = true;

    if (s.shiftFrames > 0)
        s.shiftFrames--;
    if (s.ctrlFrames > 0)
        s.ctrlFrames--;

    //The controllers (or the test's), one pointer each
    std::vector<Pad> pads = s.controllers.any() ? s.controllers.read() : std::vector<Pad>();
    if (g_testDriver) {
        pads.clear();
        for (int k = 0; k < g_testPointers; k++) {
            const TestInput t = g_testDriver(s.editor, g_testFrame, k);
            pads.push_back(fromTest(t));
            if (k < static_cast<int>(s.pointers.size()) && t.px >= 0 && t.py >= 0) {
                Pointer& p = s.pointers[static_cast<size_t>(k)];
                p.x = t.px;
                p.y = t.py;
                p.shown = true;
                if (s.owner == k || p.buttons)
                    moveMouseTo(k);
            }
            if (t.key)
                tapKey(t.key);
            if (!t.shot.empty()) {
                g_pendingShot = t.shot;
                s.overlayDirty = true;
            }
        }
        g_testFrame++;
    }
    while (s.pointers.size() < pads.size()) {
        Pointer p;
        //(a new one, a bit beside the others)
        p.x = std::clamp(SCREEN_W / 2.0f + 48.0f * static_cast<float>(s.pointers.size()), 0.0f, SCREEN_W - 1.0f);
        p.y = s.pointers.empty() ? s.cy : SCREEN_H / 2.0f;
        if (s.pointers.empty())
            p.x = s.cx;
        s.pointers.push_back(p);
    }
    for (size_t k = 0; k < s.pointers.size(); k++) {
        Pointer& p = s.pointers[k];
        p.last = p.pad;
        p.pad = k < pads.size() ? pads[k] : Pad();
        //(a controller in use: its buttons in the prompts)
        bool used = p.pad.lx != 0 || p.pad.ly != 0 || p.pad.rx != 0 || p.pad.ry != 0;
        for (int b = 0; b < BUTTON_COUNT; b++)
            used = used || p.pressed(static_cast<Button>(b));
        if (used && prompts::lastDevice() != prompts::Device::Controller) {
            prompts::setLastDevice(prompts::Device::Controller);
            s.overlayDirty = true;
        }
    }

    //Text wanted and a controller in use: the on-screen keyboard
    const bool keyboardWanted = s.textEntries > 0 && (s.controllers.any() || g_testDriver);
    if (keyboardWanted && s.overlay != Overlay::Keyboard) {
        s.overlay = Overlay::Keyboard;
        //(for the controller that is pointing)
        s.overlayOwner = std::max(0, s.owner);
        s.overlayDirty = true;
    } else if (!keyboardWanted && s.overlay == Overlay::Keyboard) {
        s.overlay = Overlay::None;
        s.overlayDirty = true;
    }

    if (s.overlay == Overlay::Keyboard) {
        if (!s.pointers.empty()) {
            const int index = std::clamp<int>(s.overlayOwner, 0, static_cast<int>(s.pointers.size()) - 1);
            updateKeyboard(s.pointers[static_cast<size_t>(index)]);
        }
    } else {
        for (size_t k = 0; k < s.pointers.size(); k++)
            updatePointer(static_cast<int>(k));
    }
    updateKeys();

    bool shown = false;
    for (const Pointer& p : s.pointers)
        shown = shown || p.shown;
    if (shown && !s.mouseUsed && SDL_CursorVisible())
        SDL_HideCursor();

    //An editor waiting in a dialog only draws when something changes: show
    //the overlay's changes anyway
    if (s.overlayDirty && s.frame && (SDL_GetTicks() - s.lastPresentMs > 30 || !g_pendingShot.empty())) {
        SDL_BlitSurface(s.frame, nullptr, screen, nullptr);
        present();
    }
}

} // namespace

void setTestDriver(TestDriver driver, std::string shotDir, int pointers)
{
    g_testDriver = std::move(driver);
    g_testShotDir = std::move(shotDir);
    g_testPointers = std::max(1, pointers);
    g_testFrame = 0;
}

void begin(Editor editor)
{
    g_testFrame = 0;
    s = State();
    s.active = true;
    s.editor = editor;
    s.category = editor == Editor::Level ? 1 : 0;    //(the level editor's blocks; the world editor's first group)
    s.controllers.open();
    s.startMs = SDL_GetTicks();
    s.cursorWasVisible = SDL_CursorVisible();
    float mx = 0;
    float my = 0;
    gfx_mousestate(&mx, &my);
    s.cx = std::clamp(mx, 0.0f, SCREEN_W - 1.0f);
    s.cy = std::clamp(my, 0.0f, SCREEN_H - 1.0f);
}

void end()
{
    SDL_CaptureMouse(false);
    coedit::detach();
    s.controllers.close();
    if (s.frame)
        SDL_DestroySurface(s.frame);
    if (s.cursorWasVisible)
        SDL_ShowCursor();
    s = State();
}

bool pollEvent(SDL_Event* event)
{
    if (!s.active) {
        if (!SDL_PollEvent(event))
            return false;
        gfx_eventtoscreen(event);
        return true;
    }
    if (s.cycleStart) {
        s.cycleStart = false;
        update();
    }
    for (;;) {
        if (!s.queue.empty()) {
            *event = s.queue.front();
            s.queue.pop_front();
            //(joiners: the host's keys)
            if (event->type == SDL_EVENT_KEY_DOWN && coedit::blockedKey(event->key.key, event->key.mod))
                continue;
            return true;
        }
        if (!SDL_PollEvent(event))
            break;
        //The mouse in the screen's pixels (the window may be any size, or fullscreen), on the screen
        gfx_eventtoscreen(event);
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            event->motion.x = std::clamp(event->motion.x, 0.0f, SCREEN_W - 1.0f);
            event->motion.y = std::clamp(event->motion.y, 0.0f, SCREEN_H - 1.0f);
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
            event->button.x = std::clamp(event->button.x, 0.0f, SCREEN_W - 1.0f);
            event->button.y = std::clamp(event->button.y, 0.0f, SCREEN_H - 1.0f);
            //(painting keeps going when the mouse leaves the window while held)
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                SDL_CaptureMouse(true);
            else if (gfx_mousestate(nullptr, nullptr) == 0)
                SDL_CaptureMouse(false);
        }
        prompts::note(*event);
        //The window: Tab or Esc closes it; the mouse over it is the window's
        if (s.overlay == Overlay::Window) {
            if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && (event->key.key == SDLK_TAB || event->key.key == SDLK_ESCAPE)) {
                closeWindow();
                continue;
            }
            if (event->type == SDL_EVENT_KEY_UP && (event->key.key == SDLK_TAB || event->key.key == SDLK_ESCAPE))
                continue;
            const bool motion = event->type == SDL_EVENT_MOUSE_MOTION;
            const bool button = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP;
            const float mx = motion ? event->motion.x : event->button.x;
            const float my = motion ? event->motion.y : event->button.y;
            if ((motion || button) && inWindow(mx, my)) {
                s.cx = mx;
                s.cy = my;
                s.owner = -1;
                if (!s.mouseUsed) {
                    s.mouseUsed = true;
                    SDL_ShowCursor();
                }
                if (motion)
                    hoverAt(mx, my);
                else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
                    clickAt(mx, my);
                s.overlayDirty = true;
                continue;
            }
            if (motion && s.hover >= 0) {
                s.hover = -1;
                s.overlayDirty = true;
            }
        }
        if (s.overlay == Overlay::Window && event->type == SDL_EVENT_MOUSE_WHEEL && inWindow(s.cx, s.cy)) {
            s.scroll = std::clamp(s.scroll - static_cast<int>(event->wheel.y * 36.0f), 0, s.scrollMax);
            s.overlayDirty = true;
            continue;
        }
        //Tab: the window, for the keyboard and the mouse
        if (s.overlay == Overlay::None && event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_TAB && !event->key.repeat) {
            openWindow(-1);
            continue;
        }
        //The level editor's tools on the map: erase is the right button, pick takes the cell
        if (levelMap() && s.overlay != Overlay::Keyboard && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            event->button.button == SDL_BUTTON_LEFT) {
            const int tool = currentTool();
            if (tool == TOOL_PICK) {
                if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    leveleditor::shell::pick(static_cast<int>(event->button.x) / TILE, static_cast<int>(event->button.y) / TILE);
                    setTool(TOOL_PAINT);
                    s.pickRelease = true;
                }
                continue;
            }
            if (s.pickRelease && event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                s.pickRelease = false;
                continue;
            }
            if (tool == TOOL_ERASE)
                event->button.button = SDL_BUTTON_RIGHT;
        }
        if (levelMap() && currentTool() == TOOL_ERASE && event->type == SDL_EVENT_MOUSE_MOTION && (event->motion.state & SDL_BUTTON_LMASK))
            event->motion.state = (event->motion.state & ~SDL_BUTTON_LMASK) | SDL_BUTTON_RMASK;
        //The on-screen keyboard: the mouse clicks its keys (typing goes to the editor as always)
        if (s.overlay == Overlay::Keyboard) {
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                const int key = oskKeyAt(event->motion.x, event->motion.y);
                if (key != s.hoverKey) {
                    s.hoverKey = key;
                    s.overlayDirty = true;
                }
            } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
                const int key = oskKeyAt(event->button.x, event->button.y);
                if (key >= 0) {
                    oskPress(key / OSK_COLS, key % OSK_COLS);
                    continue;
                }
            }
        }
        switch (event->type) {
            case SDL_EVENT_JOYSTICK_ADDED:
            case SDL_EVENT_JOYSTICK_REMOVED:
                //(the game's controller list too)
                JoystickManager::instance().handleEvent(*event);
                s.controllers.open();
                continue;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
            case SDL_EVENT_GAMEPAD_REMAPPED:
                s.controllers.open();
                continue;
            case SDL_EVENT_MOUSE_MOTION:
                //The mouse again: its own cursor
                s.cx = std::clamp(event->motion.x, 0.0f, SCREEN_W - 1.0f);
                s.cy = std::clamp(event->motion.y, 0.0f, SCREEN_H - 1.0f);
                s.owner = -1;
                if (!s.mouseUsed) {
                    s.mouseUsed = true;
                    SDL_ShowCursor();
                }
                return true;
            case SDL_EVENT_KEY_DOWN:
                if (coedit::blockedKey(event->key.key, event->key.mod))
                    continue;
                return true;
            default:
                if (event->type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && event->type < SDL_EVENT_FINGER_DOWN)
                    continue;    //(raw controller input: read above)
                return true;
        }
    }
    s.cycleStart = true;
    return false;
}

const bool* keyboardState(int* numkeys)
{
    int count = 0;
    const bool* real = SDL_GetKeyboardState(&count);
    static std::array<bool, SDL_SCANCODE_COUNT> merged {};
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
        merged[static_cast<size_t>(k)] = (k < count && real[k]) || (s.active && s.keys[static_cast<size_t>(k)]);
    if (numkeys)
        *numkeys = SDL_SCANCODE_COUNT;
    return merged.data();
}

SDL_Keymod modState()
{
    SDL_Keymod mod = SDL_GetModState();
    if (s.active && s.keys[SDL_SCANCODE_LSHIFT])
        mod |= SDL_KMOD_LSHIFT;
    if (s.active && s.keys[SDL_SCANCODE_LCTRL])
        mod |= SDL_KMOD_LCTRL;
    return mod;
}

SDL_MouseButtonFlags mouseState(float* x, float* y)
{
    float mx = 0;
    float my = 0;
    SDL_MouseButtonFlags buttons = gfx_mousestate(&mx, &my);
    if (s.active && levelMap() && currentTool() == TOOL_ERASE && (buttons & SDL_BUTTON_LMASK))
        buttons = (buttons & ~SDL_BUTTON_LMASK) | SDL_BUTTON_RMASK;
    if (s.active && (s.overlay == Overlay::Window && inWindow(s.cx, s.cy)))
        buttons &= ~(SDL_BUTTON_LMASK | SDL_BUTTON_RMASK);    //(clicks in the window aren't the editor's)
    if (s.active) {
        for (const Pointer& p : s.pointers)
            buttons |= p.buttons;
        mx = s.cx;
        my = s.cy;
    }
    if (x)
        *x = mx;
    if (y)
        *y = my;
    return buttons;
}

void flip()
{
    if (!s.active) {
        gfx_flipscreen();
        return;
    }
    //Editing together: this frame's changes go out
    coedit::afterFrame(s.cx, s.cy, true);
    present();
}

TextEntry::TextEntry()
{
    s.textEntries++;
}

TextEntry::~TextEntry()
{
    s.textEntries = std::max(0, s.textEntries - 1);
}

} // namespace editors::input

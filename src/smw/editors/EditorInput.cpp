#include "EditorInput.h"

#include "CoEdit.h"
#include "ui/InputPrompts.h"
#include "ui/SmwTheme.h"
#include "gfx.h"
#include "joystick.h"
#include "ResourceManager.h"

#include <algorithm>
#include <array>
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

enum class Overlay { None, Actions, Keyboard };

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

const std::vector<Action>& actions()
{
    return s.editor == Editor::Level ? LEVEL_ACTIONS : WORLD_ACTIONS;
}

SDL_WindowID windowId()
{
    SDL_Window* window = SDL_GetKeyboardFocus();
    return window ? SDL_GetWindowID(window) : 0;
}

// The colors of the local cursors (the first: ours online)
SDL_Color pointerColor(int index)
{
    if (index == 0)
        return coedit::active() ? coedit::ourColor() : SDL_Color {255, 255, 255, 255};
    static const SDL_Color colors[] = {{40, 184, 56, 255}, {48, 112, 232, 255}, {248, 200, 24, 255}, {176, 72, 216, 255}};
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
struct MenuEntry {
    int action = -1;    // -1: a heading
    const char* text = "";
    int col = 0;
    int x = 0, y = 0, w = 0;
};
constexpr int MENU_W = 616;
constexpr int MENU_LINE = 17;
constexpr int MENU_COLS = 3;

struct MenuLayout {
    std::vector<MenuEntry> entries;
    int x = 0, y = 0, h = 0;
};

MenuLayout menuLayout()
{
    const std::vector<Action>& list = actions();
    //(lines: a heading before every group)
    std::vector<MenuEntry> lines;
    for (int k = 0; k < static_cast<int>(list.size()); k++) {
        if (list[static_cast<size_t>(k)].group[0])
            lines.push_back(MenuEntry {-1, list[static_cast<size_t>(k)].group});
        lines.push_back(MenuEntry {k, list[static_cast<size_t>(k)].label});
    }
    const int perCol = (static_cast<int>(lines.size()) + MENU_COLS - 1) / MENU_COLS;
    MenuLayout layout;
    layout.h = perCol * MENU_LINE + 92;
    layout.x = (SCREEN_W - MENU_W) / 2;
    layout.y = std::max(6, (SCREEN_H - layout.h) / 2);
    const int colW = (MENU_W - 16) / MENU_COLS;
    for (size_t k = 0; k < lines.size(); k++) {
        MenuEntry e = lines[k];
        int index = static_cast<int>(k);
        e.col = index / perCol;
        //(a heading doesn't end a column)
        if (e.action < 0 && index % perCol == perCol - 1)
            e.col++;
        e.x = layout.x + 10 + e.col * colW;
        e.w = colW - 8;
        layout.entries.push_back(e);
    }
    //The rows of each column
    std::array<int, MENU_COLS + 1> rows {};
    for (MenuEntry& e : layout.entries) {
        const int col = std::min(e.col, MENU_COLS);
        e.y = layout.y + 34 + rows[static_cast<size_t>(col)] * MENU_LINE;
        rows[static_cast<size_t>(col)]++;
    }
    return layout;
}

// The action next to `index` in a direction (dx: column, dy: row)
int menuNeighbor(int index, int dx, int dy)
{
    const MenuLayout layout = menuLayout();
    const MenuEntry* current = nullptr;
    for (const MenuEntry& e : layout.entries) {
        if (e.action == index)
            current = &e;
    }
    if (!current)
        return 0;
    const int count = static_cast<int>(actions().size());
    if (dy != 0)
        return (index + dy + count) % count;
    //Left / right: the nearest action in the next column
    const int col = (current->col + dx + MENU_COLS) % MENU_COLS;
    int best = index;
    int bestDistance = 1 << 30;
    for (const MenuEntry& e : layout.entries) {
        if (e.action < 0 || e.col != col)
            continue;
        const int d = std::abs(e.y - current->y);
        if (d < bestDistance) {
            bestDistance = d;
            best = e.action;
        }
    }
    return best;
}

int menuActionAt(float x, float y)
{
    for (const MenuEntry& e : menuLayout().entries) {
        if (e.action >= 0 && x >= e.x - 4 && x < e.x + e.w && y >= e.y - 2 && y < e.y + MENU_LINE - 2)
            return e.action;
    }
    return -1;
}

void runAction(int index)
{
    if (index < 0 || index >= static_cast<int>(actions().size()))
        return;
    const Action& action = actions()[static_cast<size_t>(index)];
    s.overlay = Overlay::None;
    s.overlayDirty = true;
    tapKey(action.key, action.shift, action.ctrl);
}

void openActions(int owner)
{
    //(let go of anything held first)
    for (size_t k = 0; k < s.pointers.size(); k++) {
        if (s.pointers[k].buttons & SDL_BUTTON_LMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_LEFT, false);
        if (s.pointers[k].buttons & SDL_BUTTON_RMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_RIGHT, false);
    }
    s.overlay = Overlay::Actions;
    s.overlayOwner = owner;
    s.overlayDirty = true;
}

void updateActions(Pointer& p)
{
    const bool up = p.pad.buttons[UP] || p.pad.ly < -0.6f;
    const bool down = p.pad.buttons[DOWN] || p.pad.ly > 0.6f;
    const bool left = p.pad.buttons[LEFT] || p.pad.lx < -0.6f;
    const bool right = p.pad.buttons[RIGHT] || p.pad.lx > 0.6f;
    if (p.up.fire(up))
        s.actionIndex = menuNeighbor(s.actionIndex, 0, -1);
    if (p.down.fire(down))
        s.actionIndex = menuNeighbor(s.actionIndex, 0, 1);
    if (p.left.fire(left))
        s.actionIndex = menuNeighbor(s.actionIndex, -1, 0);
    if (p.right.fire(right))
        s.actionIndex = menuNeighbor(s.actionIndex, 1, 0);
    if (up || down || left || right)
        s.overlayDirty = true;

    if (p.pressed(B) || p.pressed(START) || p.pressed(BACK)) {
        s.overlay = Overlay::None;
        s.overlayDirty = true;
    } else if (p.pressed(A)) {
        runAction(s.actionIndex);
    }
}

// The keyboard and the mouse in the action menu (the editor doesn't get them)
void actionsEvent(const SDL_Event& e)
{
    s.overlayDirty = true;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        switch (e.key.key) {
            case SDLK_UP: s.actionIndex = menuNeighbor(s.actionIndex, 0, -1); break;
            case SDLK_DOWN: s.actionIndex = menuNeighbor(s.actionIndex, 0, 1); break;
            case SDLK_LEFT: s.actionIndex = menuNeighbor(s.actionIndex, -1, 0); break;
            case SDLK_RIGHT: s.actionIndex = menuNeighbor(s.actionIndex, 1, 0); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE: runAction(s.actionIndex); break;
            case SDLK_ESCAPE:
            case SDLK_TAB: s.overlay = Overlay::None; break;
            default: break;
        }
    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const int at = menuActionAt(e.motion.x, e.motion.y);
        if (at >= 0)
            s.actionIndex = at;
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int at = menuActionAt(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT && at >= 0)
            runAction(at);
        else
            s.overlay = Overlay::None;    //(a click elsewhere, or the right button)
    }
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
        //The editor's mouse follows it if it has it, or if it is painting
        if (s.owner == index || p.buttons)
            moveMouseTo(index);
    }

    //The buttons: the editor's mouse comes here first
    if (p.pressed(A) || p.pressed(B))
        p.shown = true;
    if (p.pressed(A))
        pushButton(index, SDL_BUTTON_LEFT, true);
    else if (p.released(A) && (p.buttons & SDL_BUTTON_LMASK))
        pushButton(index, SDL_BUTTON_LEFT, false);
    if (p.pressed(B))
        pushButton(index, SDL_BUTTON_RIGHT, true);
    else if (p.released(B) && (p.buttons & SDL_BUTTON_RMASK))
        pushButton(index, SDL_BUTTON_RIGHT, false);

    if (p.pressed(RB))
        tapKey(SDLK_RETURN);
    if (p.pressed(BACK))
        tapKey(SDLK_ESCAPE);
    if (p.pressed(START))
        openActions(index);
    if (p.pressed(LB)) {
        s.hints = !s.hints;
        s.overlayDirty = true;
    }
}

// The modifiers and the right stick's arrows, from every controller
void updateKeys()
{
    bool shift = s.shiftFrames > 0;
    bool ctrl = s.ctrlFrames > 0;
    bool up = false, down = false, left = false, right = false;
    for (Pointer& p : s.pointers) {
        if (s.overlay == Overlay::None) {
            shift = shift || p.pad.buttons[X];
            ctrl = ctrl || p.pad.buttons[Y];
        }
        holdKey(SDLK_UP, p.arrowUp, s.overlay == Overlay::None && p.pad.ry < -0.5f, up);
        holdKey(SDLK_DOWN, p.arrowDown, s.overlay == Overlay::None && p.pad.ry > 0.5f, down);
        holdKey(SDLK_LEFT, p.arrowLeft, s.overlay == Overlay::None && p.pad.rx < -0.5f, left);
        holdKey(SDLK_RIGHT, p.arrowRight, s.overlay == Overlay::None && p.pad.rx > 0.5f, right);
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

void drawActions()
{
    const MenuLayout layout = menuLayout();
    frameBox(layout.x, layout.y, MENU_W, layout.h);
    //(its title: SMB3's status bar and an item card)
    theme::announce(SCREEN_W / 2, std::max(0, layout.y - 34), s.editor == Editor::Level ? "Level Editor" : "World Editor",
        [](int ix, int iy) { theme::icon(ix + 8, iy + 8, s.editor == Editor::Level ? theme::Icon::QuestionBlock : theme::Icon::Star); });
    for (const MenuEntry& e : layout.entries) {
        if (e.action < 0) {
            //A heading: the menus' gray pipe, an item (SMB3's power-ups) for each group
            theme::block(e.x - 4, e.y - 1, e.w + 4, MENU_LINE, theme::Tone::Gray);
            static const std::pair<const char*, int> items[] = {
                {"Modes", 5}, {"Layers", 12}, {"Look", 6}, {"File", 26}, {"Other", 9}, {"World", 25}};
            int item = 27;
            for (const auto& [name, index] : items) {
                if (std::string(e.text) == name)
                    item = index;
            }
            if (theme::res().spr_storedpowerupsmall)
                theme::res().spr_storedpowerupsmall.draw(e.x - 1, e.y - 1, {item * 16, 0, 16, 16});
            theme::font().draw(e.x + 18, e.y - 1, e.text);
            continue;
        }
        const Action& action = actions()[static_cast<size_t>(e.action)];
        const bool selected = e.action == s.actionIndex;
        if (selected)
            theme::block(e.x - 4, e.y - 2, e.w + 4, MENU_LINE + 1, theme::Tone::Green);
        theme::font().draw(e.x + 2, e.y - 1, action.label);
        //(its key, right-aligned)
        const std::string key = keyName(action);
        const int kw = std::max(16, theme::font().getWidth(key) + 8);
        if (theme::font().getWidth(action.label) + kw + 8 < e.w)
            prompts::drawKey(e.x + e.w - kw - 2, e.y - 1, key);
    }
    //How to use it, with the device in use
    const int fy = layout.y + layout.h - 40;
    std::vector<std::tuple<prompts::Pad, std::string, std::string>> row1 = {
        {prompts::Pad::DPad, "Arrows", "choose"}, {prompts::Pad::A, "Enter", "do it"}, {prompts::Pad::B, "Esc", "close"}};
    int w = 0;
    for (const auto& [pad, key, label] : row1)
        w += prompts::promptWidth(pad, key, label) + 18;
    theme::inset(SCREEN_W / 2 - w / 2 - 8, fy - 4, w + 8, 24);
    int x = SCREEN_W / 2 - w / 2;
    for (const auto& [pad, key, label] : row1)
        x += prompts::drawPrompt(x, fy, pad, key, label) + 18;
    const char* tip = prompts::lastDevice() == prompts::Device::Controller ? "Or press the keys shown, on a keyboard" : "Or click one; the keys work in the editor too";
    theme::font().drawCentered(SCREEN_W / 2, fy + 22, tip);
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

// A tile's outline (where a cursor edits)
void tileOutline(float px, float py, SDL_Color c)
{
    const int x = static_cast<int>(px) / TILE * TILE;
    const int y = static_cast<int>(py) / TILE * TILE;
    //(black around, so it shows on any tile)
    const SDL_Color black {0, 0, 0, 255};
    fill(x - 2, y - 2, TILE + 4, 2, black);
    fill(x - 2, y + TILE, TILE + 4, 2, black);
    fill(x - 2, y, 2, TILE, black);
    fill(x + TILE, y, 2, TILE, black);
    fill(x + 2, y + 2, TILE - 4, 2, black);
    fill(x + 2, y + TILE - 4, TILE - 4, 2, black);
    fill(x + 2, y + 4, 2, TILE - 8, black);
    fill(x + TILE - 4, y + 4, 2, TILE - 8, black);
    fill(x, y, TILE, 2, c);
    fill(x, y + TILE - 2, TILE, 2, c);
    fill(x, y, 2, TILE, c);
    fill(x + TILE - 2, y, 2, TILE, c);
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
            {prompts::Pad::Y, "", "ctrl"}, {prompts::Pad::RB, "", "ok"}, {prompts::Pad::Start, "", "menu"},
            {prompts::Pad::Back, "", "back"}, {prompts::Pad::LB, "", "hide"}};
    else
        items = {{prompts::Pad::Start, "Tab", "all actions"}, {prompts::Pad::Back, "F1", "help"}, {prompts::Pad::B, "Esc", "back"}};
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
    if (s.overlay != Overlay::None || coedit::active() || !coedit::notice().empty())
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
    //Where everyone edits: the tile under each cursor, then the cursors
    const std::vector<coedit::Cursor> remote = coedit::remoteCursors();
    for (const coedit::Cursor& c : remote)
        tileOutline(c.x, c.y, c.color);
    if (s.overlay == Overlay::None) {
        for (size_t k = 0; k < s.pointers.size(); k++) {
            if (s.pointers[k].shown)
                tileOutline(s.pointers[k].x, s.pointers[k].y, pointerColor(static_cast<int>(k)));
        }
    }
    for (const coedit::Cursor& c : remote)
        drawArrow(c.x, c.y, c.color, c.name);
    if (s.overlay == Overlay::None) {
        const bool several = s.pointers.size() > 1;
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                drawArrow(p.x, p.y, pointerColor(static_cast<int>(k)), several ? "P" + std::to_string(k + 1) : std::string());
        }
        drawHints();
    }
    drawSession();
    if (s.overlay == Overlay::Actions)
        drawActions();
    else if (s.overlay == Overlay::Keyboard)
        drawKeyboard();
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

    if (s.overlay != Overlay::None) {
        if (!s.pointers.empty()) {
            const int index = std::clamp<int>(s.overlayOwner, 0, static_cast<int>(s.pointers.size()) - 1);
            Pointer& owner = s.pointers[static_cast<size_t>(index)];
            if (s.overlay == Overlay::Actions)
                updateActions(owner);
            else
                updateKeyboard(owner);
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
        //The action menu has the keyboard and the mouse while it is open
        if (s.overlay == Overlay::Actions && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP ||
                event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)) {
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                s.cx = std::clamp(event->motion.x, 0.0f, SCREEN_W - 1.0f);
                s.cy = std::clamp(event->motion.y, 0.0f, SCREEN_H - 1.0f);
                s.owner = -1;
                if (!s.mouseUsed) {
                    s.mouseUsed = true;
                    SDL_ShowCursor();
                }
            }
            if (event->type != SDL_EVENT_KEY_UP && event->type != SDL_EVENT_MOUSE_BUTTON_UP)
                actionsEvent(*event);
            continue;
        }
        //Tab: the action menu, for the keyboard and the mouse
        if (s.overlay == Overlay::None && event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_TAB && !event->key.repeat) {
            openActions(-1);
            continue;
        }
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

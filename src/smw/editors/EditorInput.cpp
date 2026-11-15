#include "EditorInput.h"

#include "CoEdit.h"
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
};

const std::vector<Action> LEVEL_ACTIONS = {
    {"Tiles", SDLK_T}, {"Blocks", SDLK_I}, {"Map items", SDLK_O}, {"Warps", SDLK_W},
    {"Move", SDLK_M}, {"Tile types", SDLK_L}, {"Platforms", SDLK_P}, {"Hazards", SDLK_H},
    {"Animated tiles", SDLK_A}, {"Race goals", SDLK_J}, {"Block properties", SDLK_K}, {"No player spawn", SDLK_X},
    {"No item spawn", SDLK_Z}, {"Active tile layer", SDLK_Y}, {"Hide other layers", SDLK_U}, {"Hide blocks", SDLK_V},
    {"Optimize layers", SDLK_END}, {"Backgrounds", SDLK_G}, {"Background thumbnails", SDLK_B}, {"Music category", SDLK_R},
    {"Floating eyecandy", SDLK_E}, {"New map", SDLK_N}, {"Save", SDLK_S}, {"Save as...", SDLK_S, true},
    {"Find map...", SDLK_F}, {"New search...", SDLK_F, true}, {"Previous map", SDLK_PAGEUP}, {"Next map", SDLK_PAGEDOWN},
    {"Clear all", SDLK_DELETE, false, true}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1}, {"Leave the editor", SDLK_ESCAPE},
};

const std::vector<Action> WORLD_ACTIONS = {
    {"Water", SDLK_1}, {"Land", SDLK_2}, {"Stage objects", SDLK_3}, {"Paths", SDLK_4},
    {"Objects", SDLK_5}, {"Bridges", SDLK_6}, {"Connections", SDLK_P}, {"Warps", SDLK_W},
    {"Vehicles", SDLK_V}, {"Copy vehicle", SDLK_C}, {"Start and doors", SDLK_T}, {"Vehicle boundaries", SDLK_B},
    {"Initial powerups", SDLK_I}, {"Edit stages", SDLK_E}, {"Automatic path/land", SDLK_A}, {"Stage previews", SDLK_SPACE},
    {"Music category", SDLK_R}, {"Resize world", SDLK_K}, {"New world", SDLK_N}, {"Save", SDLK_S},
    {"Save as...", SDLK_S, true}, {"Find world...", SDLK_F}, {"New search...", SDLK_F, true}, {"Previous world", SDLK_PAGEUP},
    {"Next world", SDLK_PAGEDOWN}, {"Clear all", SDLK_DELETE, false, true}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1},
    {"Leave the editor", SDLK_ESCAPE},
};

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
    int overlayOwner = 0;
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

void updateActions(Pointer& p)
{
    const int count = static_cast<int>(actions().size());
    const int rows = (count + 1) / 2;
    int col = s.actionIndex / rows;
    int row = s.actionIndex % rows;
    moveInGrid(p, col, row, 2, rows);
    s.actionIndex = std::min(count - 1, col * rows + row);

    if (p.pressed(B) || p.pressed(START) || p.pressed(BACK)) {
        s.overlay = Overlay::None;
        s.overlayDirty = true;
    } else if (p.pressed(A)) {
        const Action& action = actions()[static_cast<size_t>(s.actionIndex)];
        s.overlay = Overlay::None;
        s.overlayDirty = true;
        tapKey(action.key, action.shift, action.ctrl);
    }
}

void updateKeyboard(Pointer& p)
{
    moveInGrid(p, s.oskCol, s.oskRow, OSK_COLS, OSK_TEXT_ROWS + 1);

    auto typeChar = [](char c) {
        const bool letter = c >= 'a' && c <= 'z';
        tapKey(static_cast<SDL_Keycode>(c), s.oskShift && letter);
    };
    if (p.pressed(A)) {
        if (s.oskRow < OSK_TEXT_ROWS) {
            typeChar(OSK_ROWS[static_cast<size_t>(s.oskRow)][s.oskCol]);
        } else {
            switch (s.oskCol / 2) {
                case 0: s.oskShift = !s.oskShift; break;
                case 1: typeChar(' '); break;
                case 2: tapKey(SDLK_BACKSPACE); break;
                case 3: tapKey(SDLK_RETURN); break;
                default: tapKey(SDLK_ESCAPE); break;
            }
        }
        s.overlayDirty = true;
    }
    if (p.pressed(B))
        tapKey(SDLK_BACKSPACE);
    if (p.pressed(X)) {
        s.oskShift = !s.oskShift;
        s.overlayDirty = true;
    }
    if (p.pressed(Y))
        typeChar(' ');
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
    if (p.pressed(START)) {
        //(let go of anything held first)
        if (p.buttons & SDL_BUTTON_LMASK)
            pushButton(index, SDL_BUTTON_LEFT, false);
        if (p.buttons & SDL_BUTTON_RMASK)
            pushButton(index, SDL_BUTTON_RIGHT, false);
        s.overlay = Overlay::Actions;
        s.overlayOwner = index;
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

void frameBox(int x, int y, int w, int h)
{
    fill(x - 2, y - 2, w + 4, h + 4, {255, 255, 255, 255});
    fill(x, y, w, h, {24, 24, 48, 255});
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
        fill(lx, ly, w, 15, {0, 0, 0, 255});
        fill(lx + 1, ly + 1, w - 2, 13, color);
        rm->menu_font_small.draw(lx + 3, ly + 1, label);
    }
}

void drawActions()
{
    const std::vector<Action>& list = actions();
    const int rows = static_cast<int>(list.size() + 1) / 2;
    const int lineH = 18;
    const int w = 560;
    const int h = rows * lineH + 80;
    const int x = (SCREEN_W - w) / 2;
    const int y = std::max(8, (SCREEN_H - h) / 2);
    frameBox(x, y, w, h);
    rm->menu_font_large.drawCentered(SCREEN_W / 2, y + 6, s.editor == Editor::Level ? "Level Editor" : "World Editor");
    for (int k = 0; k < static_cast<int>(list.size()); k++) {
        const int col = k / rows;
        const int row = k % rows;
        const int ix = x + 12 + col * (w / 2);
        const int iy = y + 32 + row * lineH;
        if (k == s.actionIndex)
            fill(ix - 4, iy - 2, w / 2 - 16, lineH, {72, 112, 200, 255});
        rm->menu_font_small.draw(ix, iy, list[static_cast<size_t>(k)].label);
    }
    rm->menu_font_small.drawCentered(SCREEN_W / 2, y + h - 38, "A: do it    B: close");
    rm->menu_font_small.drawCentered(SCREEN_W / 2, y + h - 20, "In the editor: X shift   Y ctrl   RB enter   Back escape");
}

void drawKeyboard()
{
    const int keyW = 38;
    const int keyH = 24;
    const int w = OSK_COLS * keyW + 24;
    const int h = (OSK_TEXT_ROWS + 1) * keyH + 56;
    const int x = (SCREEN_W - w) / 2;
    const int y = SCREEN_H - h - 10;
    frameBox(x, y, w, h);
    for (int row = 0; row <= OSK_TEXT_ROWS; row++) {
        for (int col = 0; col < OSK_COLS; col++) {
            const bool special = row == OSK_TEXT_ROWS;
            if (special && col % 2)
                continue;
            const int kx = x + 12 + col * keyW;
            const int ky = y + 10 + row * keyH;
            const int kw = special ? keyW * 2 - 4 : keyW - 4;
            const bool selected = s.oskRow == row && (special ? s.oskCol / 2 == col / 2 : s.oskCol == col);
            const bool lit = special && col / 2 == 0 && s.oskShift;
            fill(kx, ky, kw, keyH - 4, selected ? SDL_Color {72, 112, 200, 255} : (lit ? SDL_Color {60, 90, 60, 255} : SDL_Color {44, 44, 80, 255}));
            std::string label;
            if (special) {
                label = OSK_KEYS[static_cast<size_t>(col / 2)];
            } else {
                char c = OSK_ROWS[static_cast<size_t>(row)][col];
                if (s.oskShift && c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 32);
                label = c == ' ' ? std::string("spc") : std::string(1, c);
            }
            rm->menu_font_small.drawCentered(kx + kw / 2, ky + 3, label);
        }
    }
    rm->menu_font_small.drawCentered(SCREEN_W / 2, y + h - 22, "A: type   B: delete   X: shift   Y: space   Start: done   Back: cancel");
}

// Who is editing (online), and the latest notice
void drawSession()
{
    const std::vector<std::pair<std::string, SDL_Color>> people = coedit::people();
    if (!people.empty()) {
        int y = SCREEN_H - 16 * static_cast<int>(people.size()) - 4;
        for (const auto& [name, color] : people) {
            const int w = rm->menu_font_small.getWidth(name) + 18;
            fill(2, y, w, 15, {0, 0, 0, 255});
            fill(4, y + 3, 9, 9, color);
            rm->menu_font_small.draw(16, y + 1, name);
            y += 16;
        }
    }
    const std::string notice = coedit::notice();
    if (!notice.empty()) {
        const int w = rm->menu_font_small.getWidth(notice) + 16;
        const int x = (SCREEN_W - w) / 2;
        frameBox(x, 40, w, 18);
        rm->menu_font_small.draw(x + 8, 42, notice);
    }
}

bool overlayWanted()
{
    if (s.overlay != Overlay::None || coedit::active() || !coedit::notice().empty())
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
    for (const coedit::Cursor& c : coedit::remoteCursors())
        drawArrow(c.x, c.y, c.color, c.name);
    if (s.overlay == Overlay::None) {
        const bool several = s.pointers.size() > 1;
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                drawArrow(p.x, p.y, pointerColor(static_cast<int>(k)), several ? "P" + std::to_string(k + 1) : std::string());
        }
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
    s.cursorWasVisible = SDL_CursorVisible();
    float mx = 0;
    float my = 0;
    SDL_GetMouseState(&mx, &my);
    s.cx = std::clamp(mx, 0.0f, SCREEN_W - 1.0f);
    s.cy = std::clamp(my, 0.0f, SCREEN_H - 1.0f);
}

void end()
{
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
    if (!s.active)
        return SDL_PollEvent(event);
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
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
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

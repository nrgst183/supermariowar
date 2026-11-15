#include "EditorInput.h"

#include "gfx.h"
#include "joystick.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

extern CResourceManager* rm;

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

    // Every controller together: any button, the strongest stick
    Pad read() const
    {
        Pad p;
        auto stick = [](float& out, float v) {
            if (std::fabs(v) > std::fabs(out))
                out = v;
        };
        for (SDL_Gamepad* pad : gamepads) {
            const std::array<SDL_GamepadButton, BUTTON_COUNT> map = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
            for (int b = 0; b < BUTTON_COUNT; b++)
                p.buttons[b] = p.buttons[b] || SDL_GetGamepadButton(pad, map[b]);
            stick(p.lx, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)));
            stick(p.ly, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)));
            stick(p.rx, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)));
            stick(p.ry, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)));
        }
        //Without a mapping: the usual layout (buttons 0-7, the first hat, axes 0-3)
        for (SDL_Joystick* joy : joysticks) {
            const int buttons = SDL_GetNumJoystickButtons(joy);
            for (int b = 0; b < 8 && b < buttons; b++)
                p.buttons[b] = p.buttons[b] || SDL_GetJoystickButton(joy, b);
            if (SDL_GetNumJoystickHats(joy) > 0) {
                const Uint8 hat = SDL_GetJoystickHat(joy, 0);
                p.buttons[UP] = p.buttons[UP] || (hat & SDL_HAT_UP);
                p.buttons[DOWN] = p.buttons[DOWN] || (hat & SDL_HAT_DOWN);
                p.buttons[LEFT] = p.buttons[LEFT] || (hat & SDL_HAT_LEFT);
                p.buttons[RIGHT] = p.buttons[RIGHT] || (hat & SDL_HAT_RIGHT);
            }
            const int axes = SDL_GetNumJoystickAxes(joy);
            if (axes >= 2) {
                stick(p.lx, axis(SDL_GetJoystickAxis(joy, 0)));
                stick(p.ly, axis(SDL_GetJoystickAxis(joy, 1)));
            }
            if (axes >= 4) {
                stick(p.rx, axis(SDL_GetJoystickAxis(joy, 2)));
                stick(p.ry, axis(SDL_GetJoystickAxis(joy, 3)));
            }
        }
        return p;
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

enum class Overlay { None, Actions, Keyboard };

struct State {
    bool active = false;
    Editor editor = Editor::Level;
    Controllers controllers;
    Pad pad;
    Pad last;
    std::deque<SDL_Event> queue;
    bool cycleStart = true;

    //The pointer
    float cx = SCREEN_W / 2.0f;
    float cy = SCREEN_H / 2.0f;
    bool controllerPointer = false;    // the drawn cursor is shown (the controller moved last)
    SDL_MouseButtonFlags buttons = 0;  // the mouse buttons the controller holds
    bool cursorWasVisible = true;

    //Keys the controller holds (on top of the keyboard's)
    std::array<bool, SDL_SCANCODE_COUNT> keys {};
    int shiftFrames = 0;    // a chord's Shift (an action), a few frames
    int ctrlFrames = 0;

    Repeat repeatUp, repeatDown, repeatLeft, repeatRight;
    Repeat arrowUp, arrowDown, arrowLeft, arrowRight;

    Overlay overlay = Overlay::None;
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

// ---- synthetic events

void pushMotion()
{
    SDL_Event e {};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = windowId();
    e.motion.state = s.buttons;
    e.motion.x = std::floor(s.cx);
    e.motion.y = std::floor(s.cy);
    s.queue.push_back(e);
}

void pushButton(Uint8 button, bool down)
{
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
    s.buttons = down ? (s.buttons | mask) : (s.buttons & ~mask);
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
void holdKey(SDL_Keycode key, Repeat& repeat, bool down)
{
    const SDL_Scancode code = SDL_GetScancodeFromKey(key, nullptr);
    const bool was = s.keys[code];
    s.keys[code] = down;
    if (repeat.fire(down))
        pushKey(key, true);
    else if (was && !down)
        pushKey(key, false);
}

bool pressed(Button b)
{
    return s.pad.buttons[b] && !s.last.buttons[b];
}
bool released(Button b)
{
    return !s.pad.buttons[b] && s.last.buttons[b];
}

// ---- the overlays' input

void moveInGrid(int& col, int& row, int cols, int rows)
{
    //The d-pad, or the left stick pushed
    const bool up = s.pad.buttons[UP] || s.pad.ly < -0.6f;
    const bool down = s.pad.buttons[DOWN] || s.pad.ly > 0.6f;
    const bool left = s.pad.buttons[LEFT] || s.pad.lx < -0.6f;
    const bool right = s.pad.buttons[RIGHT] || s.pad.lx > 0.6f;
    if (s.repeatUp.fire(up))
        row = (row + rows - 1) % rows;
    if (s.repeatDown.fire(down))
        row = (row + 1) % rows;
    if (s.repeatLeft.fire(left))
        col = (col + cols - 1) % cols;
    if (s.repeatRight.fire(right))
        col = (col + 1) % cols;
    if (up || down || left || right)
        s.overlayDirty = true;
}

void updateActions()
{
    const int count = static_cast<int>(actions().size());
    const int rows = (count + 1) / 2;
    int col = s.actionIndex / rows;
    int row = s.actionIndex % rows;
    moveInGrid(col, row, 2, rows);
    s.actionIndex = std::min(count - 1, col * rows + row);

    if (pressed(B) || pressed(START) || pressed(BACK)) {
        s.overlay = Overlay::None;
        s.overlayDirty = true;
    } else if (pressed(A)) {
        const Action& action = actions()[static_cast<size_t>(s.actionIndex)];
        s.overlay = Overlay::None;
        s.overlayDirty = true;
        tapKey(action.key, action.shift, action.ctrl);
    }
}

void updateKeyboard()
{
    moveInGrid(s.oskCol, s.oskRow, OSK_COLS, OSK_TEXT_ROWS + 1);

    auto typeChar = [](char c) {
        const bool letter = c >= 'a' && c <= 'z';
        tapKey(static_cast<SDL_Keycode>(c), s.oskShift && letter);
    };
    if (pressed(A)) {
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
    if (pressed(B))
        tapKey(SDLK_BACKSPACE);
    if (pressed(X)) {
        s.oskShift = !s.oskShift;
        s.overlayDirty = true;
    }
    if (pressed(Y))
        typeChar(' ');
    if (pressed(START) || pressed(RB))
        tapKey(SDLK_RETURN);
    if (pressed(BACK))
        tapKey(SDLK_ESCAPE);
}

// ---- the pointer

void updatePointer()
{
    float dx = 0;
    float dy = 0;
    //The stick: faster the further it's pushed
    if (s.pad.lx != 0 || s.pad.ly != 0) {
        const float speed = 1.5f + 9.0f * std::min(1.0f, std::hypot(s.pad.lx, s.pad.ly));
        dx += s.pad.lx * speed;
        dy += s.pad.ly * speed;
    }
    //The d-pad: one tile, from the tile's middle
    const bool up = s.repeatUp.fire(s.pad.buttons[UP]);
    const bool down = s.repeatDown.fire(s.pad.buttons[DOWN]);
    const bool left = s.repeatLeft.fire(s.pad.buttons[LEFT]);
    const bool right = s.repeatRight.fire(s.pad.buttons[RIGHT]);
    if (up || down || left || right) {
        const float mx = std::floor(s.cx / TILE) * TILE + TILE / 2.0f;
        const float my = std::floor(s.cy / TILE) * TILE + TILE / 2.0f;
        dx += mx - s.cx + (right ? TILE : 0) - (left ? TILE : 0);
        dy += my - s.cy + (down ? TILE : 0) - (up ? TILE : 0);
    }
    if (dx != 0 || dy != 0) {
        s.cx = std::clamp(s.cx + dx, 0.0f, SCREEN_W - 1.0f);
        s.cy = std::clamp(s.cy + dy, 0.0f, SCREEN_H - 1.0f);
        s.controllerPointer = true;
        s.overlayDirty = true;
        pushMotion();
    }

    //The buttons (a click shows the drawn cursor too)
    if (pressed(A) || pressed(B))
        s.controllerPointer = true;
    if (pressed(A))
        pushButton(SDL_BUTTON_LEFT, true);
    else if (released(A))
        pushButton(SDL_BUTTON_LEFT, false);
    if (pressed(B))
        pushButton(SDL_BUTTON_RIGHT, true);
    else if (released(B))
        pushButton(SDL_BUTTON_RIGHT, false);

    //Modifiers (X, Y held), arrows, Enter, Escape
    s.keys[SDL_SCANCODE_LSHIFT] = s.pad.buttons[X] || s.shiftFrames > 0;
    s.keys[SDL_SCANCODE_LCTRL] = s.pad.buttons[Y] || s.ctrlFrames > 0;
    holdKey(SDLK_UP, s.arrowUp, s.pad.ry < -0.5f);
    holdKey(SDLK_DOWN, s.arrowDown, s.pad.ry > 0.5f);
    holdKey(SDLK_LEFT, s.arrowLeft, s.pad.rx < -0.5f);
    holdKey(SDLK_RIGHT, s.arrowRight, s.pad.rx > 0.5f);
    if (pressed(RB))
        tapKey(SDLK_RETURN);
    if (pressed(BACK))
        tapKey(SDLK_ESCAPE);
    if (pressed(START)) {
        //(let go of anything held first)
        if (s.buttons & SDL_BUTTON_LMASK)
            pushButton(SDL_BUTTON_LEFT, false);
        if (s.buttons & SDL_BUTTON_RMASK)
            pushButton(SDL_BUTTON_RIGHT, false);
        s.overlay = Overlay::Actions;
        s.overlayDirty = true;
    }
}

// ---- drawing

void fill(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b)
{
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(screen, &rect, SDL_MapSurfaceRGB(screen, r, g, b));
}

void frameBox(int x, int y, int w, int h)
{
    fill(x - 2, y - 2, w + 4, h + 4, 255, 255, 255);
    fill(x, y, w, h, 24, 24, 48);
}

void drawCursor()
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
    const int x0 = static_cast<int>(s.cx);
    const int y0 = static_cast<int>(s.cy);
    for (int row = 0; row < 16; row++) {
        for (int col = 0; ARROW[row][col]; col++) {
            const char c = ARROW[row][col];
            if (c == '#')
                fill(x0 + col, y0 + row, 1, 1, 0, 0, 0);
            else if (c == '.')
                fill(x0 + col, y0 + row, 1, 1, 255, 255, 255);
        }
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
            fill(ix - 4, iy - 2, w / 2 - 16, lineH, 72, 112, 200);
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
            fill(kx, ky, kw, keyH - 4, selected ? 72 : (lit ? 60 : 44), selected ? 112 : (lit ? 90 : 44), selected ? 200 : (lit ? 60 : 80));
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

void drawOverlay()
{
    if (!rm)
        return;
    if (s.overlay == Overlay::Actions)
        drawActions();
    else if (s.overlay == Overlay::Keyboard)
        drawKeyboard();
    else if (s.controllerPointer)
        drawCursor();
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
    const bool overlay = s.overlay != Overlay::None || s.controllerPointer;
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

// Once per frame (the first poll of the editor's event loop)
void update()
{
    if (s.shiftFrames > 0)
        s.shiftFrames--;
    if (s.ctrlFrames > 0)
        s.ctrlFrames--;

    s.last = s.pad;
    s.pad = s.controllers.any() ? s.controllers.read() : Pad();
    if (g_testDriver) {
        const TestInput t = g_testDriver(s.editor, g_testFrame++);
        const std::array<bool, BUTTON_COUNT> b = {t.a, t.b, t.x, t.y, t.lb, t.rb, t.back, t.start, t.up, t.down, t.left, t.right};
        s.pad.buttons = b;
        s.pad.lx = t.lx;
        s.pad.ly = t.ly;
        s.pad.rx = t.rx;
        s.pad.ry = t.ry;
        if (t.px >= 0 && t.py >= 0) {
            s.cx = t.px;
            s.cy = t.py;
            s.controllerPointer = true;
            pushMotion();
        }
        if (!t.shot.empty()) {
            g_pendingShot = t.shot;
            s.overlayDirty = true;
        }
    }

    //Text wanted and a controller in use: the on-screen keyboard
    const bool keyboardWanted = s.textEntries > 0 && (s.controllers.any() || g_testDriver);
    if (keyboardWanted && s.overlay != Overlay::Keyboard) {
        s.overlay = Overlay::Keyboard;
        s.overlayDirty = true;
    } else if (!keyboardWanted && s.overlay == Overlay::Keyboard) {
        s.overlay = Overlay::None;
        s.overlayDirty = true;
    }

    //(an overlay's chords: their modifiers until they run out)
    if (s.overlay != Overlay::None) {
        s.keys[SDL_SCANCODE_LSHIFT] = s.shiftFrames > 0;
        s.keys[SDL_SCANCODE_LCTRL] = s.ctrlFrames > 0;
    }

    if (s.overlay == Overlay::Actions)
        updateActions();
    else if (s.overlay == Overlay::Keyboard)
        updateKeyboard();
    else
        updatePointer();

    if (s.controllerPointer && SDL_CursorVisible())
        SDL_HideCursor();

    //An editor waiting in a dialog only draws when something changes: show
    //the overlay's changes anyway
    if (s.overlayDirty && s.frame && (SDL_GetTicks() - s.lastPresentMs > 30 || !g_pendingShot.empty())) {
        SDL_BlitSurface(s.frame, nullptr, screen, nullptr);
        present();
    }
}

} // namespace

void setTestDriver(TestDriver driver, std::string shotDir)
{
    g_testDriver = std::move(driver);
    g_testShotDir = std::move(shotDir);
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
                if (s.controllerPointer) {
                    s.controllerPointer = false;
                    SDL_ShowCursor();
                }
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
        buttons |= s.buttons;
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

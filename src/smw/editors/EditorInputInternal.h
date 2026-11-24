#pragma once

// The editors' controller input (EditorInput.h): what its two files share. EditorInput.cpp turns
// the controllers into the editors' events and runs the overlays; EditorInputDraw.cpp draws the
// cursors and the overlays (the window, the chip, the tools, the keyboard).

#include "EditorInput.h"

#include "CoEdit.h"
#include "LevelShell.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace editors::input::detail {

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

inline const std::vector<Action> LEVEL_ACTIONS = {
    {"Play this map", SDLK_F5, false, false, "File"},
    {"Tiles", SDLK_T, false, false, "Modes"}, {"Blocks", SDLK_I}, {"Map items", SDLK_O}, {"Warps", SDLK_W},
    {"Move", SDLK_M}, {"Tile types", SDLK_L}, {"Platforms", SDLK_P}, {"Hazards", SDLK_H},
    {"Animated tiles", SDLK_A}, {"Race goals", SDLK_J}, {"? block contents", SDLK_K}, {"No player starts", SDLK_X},
    {"No items", SDLK_Z},
    {"Active layer", SDLK_Y, false, false, "Layers"}, {"Hide other layers", SDLK_U}, {"Hide blocks", SDLK_V},
    {"Backgrounds", SDLK_B, false, false, "Look"}, {"Next background", SDLK_G}, {"Music category", SDLK_R}, {"Eyecandy", SDLK_E},
    {"New map", SDLK_N, false, false, "File"}, {"Save", SDLK_S}, {"Save as...", SDLK_S, true}, {"Find map...", SDLK_F},
    {"New search...", SDLK_F, true}, {"Previous map", SDLK_PAGEUP}, {"Next map", SDLK_PAGEDOWN},
    {"Clear all", SDLK_DELETE, false, true, "Other"}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1}, {"Leave", SDLK_ESCAPE},
};

inline const std::vector<Action> WORLD_ACTIONS = {
    {"Water", SDLK_1, false, false, "Modes"}, {"Land", SDLK_2}, {"Stage objects", SDLK_3}, {"Paths", SDLK_4},
    {"Objects", SDLK_5}, {"Bridges", SDLK_6}, {"Connections", SDLK_P}, {"Warps", SDLK_W},
    {"Vehicles", SDLK_V}, {"Copy vehicle", SDLK_C}, {"Start and doors", SDLK_T}, {"Vehicle bounds", SDLK_B},
    {"Start powerups", SDLK_I}, {"Edit stages", SDLK_E},
    {"Auto path/land", SDLK_A, false, false, "World"}, {"Stage previews", SDLK_SPACE}, {"Music category", SDLK_R}, {"Resize world", SDLK_K},
    {"New world", SDLK_N, false, false, "File"}, {"Save", SDLK_S}, {"Save as...", SDLK_S, true}, {"Find world...", SDLK_F},
    {"New search...", SDLK_F, true}, {"Previous world", SDLK_PAGEUP}, {"Next world", SDLK_PAGEDOWN},
    {"Clear all", SDLK_DELETE, false, true, "Other"}, {"Screenshot", SDLK_INSERT}, {"Help", SDLK_F1}, {"Leave", SDLK_ESCAPE},
};

// ---- the controllers

enum Button { A, B, X, Y, LB, RB, BACK, START, UP, DOWN, LEFT, RIGHT, LT, RT, LS, RS, BUTTON_COUNT };

struct Pad {
    std::array<bool, BUTTON_COUNT> buttons {};
    float lx = 0, ly = 0, rx = 0, ry = 0;
};

// A stick's two axes: a round dead zone (a diagonal push doesn't stick to an axis), and from its edge
// the push grows from 0 smoothly (no jump)
inline void stick(Sint16 x, Sint16 y, float& ox, float& oy)
{
    const float vx = std::clamp(x / 32767.0f, -1.0f, 1.0f);
    const float vy = std::clamp(y / 32767.0f, -1.0f, 1.0f);
    const float m = std::hypot(vx, vy);
    if (m < DEADZONE) {
        ox = oy = 0.0f;
        return;
    }
    const float scale = (std::min(1.0f, m) - DEADZONE) / (1.0f - DEADZONE) / m;
    ox = vx * scale;
    oy = vy * scale;
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
            const std::array<SDL_GamepadButton, RIGHT + 1> map = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
            for (int b = 0; b <= RIGHT; b++)
                p.buttons[b] = SDL_GetGamepadButton(pad, map[b]);
            //(the triggers: pulled halfway)
            p.buttons[LT] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
            p.buttons[RT] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
            p.buttons[LS] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
            p.buttons[RS] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
            stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY), p.lx, p.ly);
            stick(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY), p.rx, p.ry);
            pads.push_back(p);
        }
        //Without a mapping: the usual layout (buttons 0-7, the first hat, axes 0-3)
        for (SDL_Joystick* joy : joysticks) {
            Pad p;
            const int buttons = SDL_GetNumJoystickButtons(joy);
            for (int b = 0; b < 8 && b < buttons; b++)
                p.buttons[b] = SDL_GetJoystickButton(joy, b);
            //(the sticks pressed: XInput's 8 and 9)
            if (buttons > 9) {
                p.buttons[LS] = SDL_GetJoystickButton(joy, 8);
                p.buttons[RS] = SDL_GetJoystickButton(joy, 9);
            }
            if (SDL_GetNumJoystickHats(joy) > 0) {
                const Uint8 hat = SDL_GetJoystickHat(joy, 0);
                p.buttons[UP] = hat & SDL_HAT_UP;
                p.buttons[DOWN] = hat & SDL_HAT_DOWN;
                p.buttons[LEFT] = hat & SDL_HAT_LEFT;
                p.buttons[RIGHT] = hat & SDL_HAT_RIGHT;
            }
            const int axes = SDL_GetNumJoystickAxes(joy);
            if (axes >= 2) {
                stick(SDL_GetJoystickAxis(joy, 0), SDL_GetJoystickAxis(joy, 1), p.lx, p.ly);
            }
            if (axes >= 4) {
                stick(SDL_GetJoystickAxis(joy, 2), SDL_GetJoystickAxis(joy, 3), p.rx, p.ry);
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
    int drag = -1;                      // a rectangle being drawn: the button (A, B), and its first cell
    int dragCol = 0, dragRow = 0;

    bool pressed(Button b) const { return pad.buttons[b] && !last.buttons[b]; }
    bool released(Button b) const { return !pad.buttons[b] && last.buttons[b]; }
};

enum class Overlay { None, Window, Keyboard, Tools };

// A tool waiting for the next click on the map (chosen in the tools menu)
enum class Armed { None, Copy, Paste, Fill, Properties };    // (Properties: the next ? block chosen opens)

// One undo: each value of the document that changed, before and after
struct UndoStep {
    std::vector<std::tuple<uint32_t, coedit::Bytes, coedit::Bytes>> changes;
};

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
    float mouseX = -1, mouseY = -1;    // the real mouse (drawn as a glove: the system cursor is hidden)

    //Keys the controllers hold (on top of the keyboard's)
    std::array<bool, SDL_SCANCODE_COUNT> keys {};
    int shiftFrames = 0;    // a chord's Shift (an action), a few frames
    int ctrlFrames = 0;

    Overlay overlay = Overlay::None;
    int overlayOwner = 0;              // the controller that opened it (-1: keyboard / mouse)
    int hoverKey = -1;                 // the on-screen keyboard's key under the mouse
    int oskKey = 0;                    // its selected key (the d-pad's)
    int oskPressedKey = -1;            // a key just pressed (drawn down for a moment)
    Uint64 oskPressedMs = 0;
    bool hints = true;                 // the controller's button bar (LB)
    Uint64 startMs = 0;
    int textEntries = 0;
    bool oskShift = false;
    bool oskCaps = false;
    const char* oskTitle = nullptr;    // the dialog's title, instructions and text (TextEntry)
    const char* oskPrompt = nullptr;
    const char* oskText = nullptr;
    bool oskTyped = false;             // (its text shows once something is typed: until then the buffer is the dialog's own)

    //The editor window (Tab / Start)
    int category = 1;                  // its page (Blocks first)
    bool dockLeft = false;             // at the left of the screen (else the right)
    int hover = -1;                    // the widget under the pointer
    SDL_Rect hoverRect {};             // (its place: the widgets are laid out again each frame)
    bool nav = false;                  // a controller moves a highlight through the window (its d-pad, A, B)
    leveleditor::shell::Screen screen; // the level editor's other screen showing now (title empty: none, or the map)
    SDL_Rect focusRect {};             // the highlighted widget
    bool focusValid = false;           // (else: the page's selected item, or its first)
    std::string tip;                   // what the widget under the pointer is
    int tool = 0;                      // Tool (level editor)
    int paintMode = 0;                 // the editor's mode before Move
    bool pickRelease = false;
    int scroll = 0;                    // the page scrolled down by this many pixels
    int scrollMax = 0;                 // (as far as it goes, from the last frame's layout)          // the mouse button that picked: its release isn't the editor's

    //Undo: the document as it was after the last change, and the changes
    std::unique_ptr<coedit::Document> undoDoc;
    std::unordered_map<uint32_t, coedit::Bytes> undoBase;
    std::string undoTitle;             // (another map or world: the history starts over)
    std::vector<UndoStep> undos, redos;
    bool undoWasHeld = false;          // (a button held: one undo for the whole stroke)
    bool undoFresh = false;
    uint64_t undoApplied = 0;          // (others' changes online: not ours to undo)

    //The tools menu (the right stick pressed), and the tool it armed
    int toolsIndex = 0;
    Uint64 toolsOpenedMs = 0;
    bool toolsMoved = false;
    float toolsX = 0, toolsY = 0;      // (beside the pointer that opened it)
    Armed armed = Armed::None;
    bool mouseDrag = false;            // (the mouse drawing the area to copy)
    int mouseDragCol = 0, mouseDragRow = 0;
    std::string confirmId;             // (an action that throws work away, chosen once: chosen again soon, it happens)
    Uint64 confirmMs = 0;
    bool mouseRect = false;            // (the mouse drawing a rectangle to fill: Shift and drag; from mouseDragCol, Row)
    bool mouseRectErase = false;       // (with the right button: to clear)

    //The last things picked (LB / RB on the level editor's map)
    std::vector<leveleditor::shell::Brush> recents;
    leveleditor::shell::Brush lastBrush;
    leveleditor::shell::Brush stepped;    // (picked again with LB / RB: it keeps its place)

    //A short message at the top (undone, copied...)
    std::string toast;
    Uint64 toastMs = 0;

    //The whole map (the left stick held)
    bool preview = false;
    SDL_Surface* worldPicture = nullptr;

    //The editor's latest frame, without the overlay (redrawn while it waits)
    SDL_Surface* frame = nullptr;
    Uint64 lastPresentMs = 0;
    bool overlayDirty = false;
};

extern State s;
extern TestDriver g_testDriver;
extern std::string g_testShotDir;
extern int g_testPointers;
extern int g_testFrame;
extern std::string g_pendingShot;

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
enum Zone { ZONE_TITLE, ZONE_TOOLS, ZONE_RAIL, ZONE_PAGE };

struct Widget {
    SDL_Rect r {};
    std::function<void()> act;
    std::string tip;
    SDL_Rect hit {};          // its visible part (the page scrolls)
    bool visible = true;
    Zone zone = ZONE_PAGE;
    bool selected = false;    // what is in use (where the highlight starts)
    bool pick = false;        // choosing it with the d-pad goes back to the map
};

extern std::vector<Widget> g_widgets;

// The window's pages
int pageCount();

// The on-screen keyboard (the original Xbox dashboard's, in the game's look): a panel with the
// dialog's title and the text typed; a column of pipes (Done, Shift, Caps Lock, Clear,
// Cancel) beside an alphabetical grid of block keys; Space and Backspace pipes under it
enum OskKind { OSK_CHAR, OSK_SHIFT, OSK_CAPS, OSK_CLEAR, OSK_SPACE, OSK_BACKSPACE, OSK_ENTER, OSK_CANCEL };

struct OskKey {
    SDL_Rect r;
    OskKind kind;
    char ch;
};

constexpr int OSK_PANEL_W = 600;
constexpr int OSK_PANEL_H = 360;
constexpr int OSK_PANEL_X = (SCREEN_W - OSK_PANEL_W) / 2;
constexpr int OSK_PANEL_Y = SCREEN_H - OSK_PANEL_H - 6;
constexpr int OSK_ROW = 40;                         // the rows' pitch
constexpr int OSK_TOP = OSK_PANEL_Y + 104;          // the first row
constexpr int OSK_LEFT_X = OSK_PANEL_X + 20;        // the column of pipes
constexpr int OSK_LEFT_W = 150;
constexpr int OSK_GRID_X = OSK_PANEL_X + 184;       // the keys
constexpr int OSK_COL = 39;

// ---- the level editor's quick palette (LB and RB on the map): the blocks, the items, the tile types

struct Quick {
    int kind;    // 0 a block, 1 an item, 2 a tile type
    int id;
    bool on;
};

// ---- the tools menu (the right stick pressed): undo, redo, and on the level editor's map copy, paste, fill

// (the last three only in move mode: its selection copied, deleted, and moving it replacing or merging)
enum ToolItem { T_UNDO, T_REDO, T_COPY, T_PASTE, T_FILL, T_BASIC_COUNT, T_MOVE_COPY = T_BASIC_COUNT, T_MOVE_DELETE, T_MOVE_REPLACE, T_ITEM_COUNT };

constexpr int TOOLS_W = 210;
constexpr int TOOLS_ROW = 30;

// ---- the window's highlight: the controller that opened it

int focusIndex();
void moveFocus(int dx, int dy);

// ---- shared by the two files: the input part (EditorInput.cpp) and the drawing (EditorInputDraw.cpp)

std::string keyName(const Action& action);
SDL_Color pointerColor(int index);
bool toastShown();
SDL_Rect windowRect();
bool inWindow(float x, float y);
bool levelMap();
void setTool(int tool);
int currentTool();
void closeWindow();
void runKey(SDL_Keycode key, bool shift = false, bool ctrl = false, bool keepOpen = false);
bool padUsed();
void armBlockContents();
bool confirmed(const std::string& id, const std::string& warning);
const std::vector<OskKey>& oskKeys();
int toolsCount();
SDL_Rect toolsRect();
SDL_Rect toolsItemRect(int k);
bool toolUsable(int item, std::string* why = nullptr);

// (EditorInputDraw.cpp) Presents the frame with the overlay; the screen keeps the editor's frame
void present();

} // namespace editors::input::detail

#include "EditorInput.h"

#include "CoEdit.h"
#include "LevelShell.h"
#include "ui/MI_MenuPanel.h"
#include "WorldShell.h"
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
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
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


// ---- the controllers

enum Button { A, B, X, Y, LB, RB, BACK, START, UP, DOWN, LEFT, RIGHT, LT, RT, LS, RS, BUTTON_COUNT };

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
    int drag = -1;                      // a rectangle being drawn: the button (A, B), and its first cell
    int dragCol = 0, dragRow = 0;

    bool pressed(Button b) const { return pad.buttons[b] && !last.buttons[b]; }
    bool released(Button b) const { return !pad.buttons[b] && last.buttons[b]; }
};

enum class Overlay { None, Window, Keyboard, Tools };

// A tool waiting for the next click on the map (chosen in the tools menu)
enum class Armed { None, Copy, Paste, Fill };

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
    int actionIndex = 0;
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
    if (down && s.textEntries > 0)
        s.oskTyped = true;
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

// ---- messages

void toast(const std::string& text)
{
    s.toast = text;
    s.toastMs = SDL_GetTicks();
    s.overlayDirty = true;
}

bool toastShown()
{
    return !s.toast.empty() && SDL_GetTicks() - s.toastMs < 1600;
}

// ---- undo and redo
//
// The editor's document (the keyed values of CoEdit.h) is compared once a frame
// with how it was: what changed is one undo (while a button is held, a stroke or a
// rectangle, all of it). Undoing puts the values back, where nobody has changed them
// since; online, that goes out to the others as any change does.

bool anyHeld()
{
    for (const Pointer& p : s.pointers) {
        if (p.buttons || p.drag >= 0)
            return true;
    }
    return s.mouseDrag || (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) != 0;
}

std::unique_ptr<coedit::Document> makeDocument()
{
    return s.editor == Editor::Level ? leveleditor::makeCoeditDocument() : worldeditor::makeCoeditDocument();
}

// Each frame (before its changes go out online): what changed since the last
void undoTrack()
{
    if (!s.undoDoc)
        s.undoDoc = makeDocument();
    std::unordered_map<uint32_t, coedit::Bytes> now;
    s.undoDoc->capture(now);
    const std::string title = s.undoDoc->title();
    if (title != s.undoTitle) {
        //(another map or world: its own history)
        s.undoTitle = title;
        s.undos.clear();
        s.redos.clear();
        s.undoBase = std::move(now);
        return;
    }
    UndoStep step;
    for (const auto& kv : now) {
        auto it = s.undoBase.find(kv.first);
        if (it == s.undoBase.end())
            step.changes.emplace_back(kv.first, coedit::Bytes(), kv.second);
        else if (it->second != kv.second)
            step.changes.emplace_back(kv.first, it->second, kv.second);
    }
    for (const auto& kv : s.undoBase) {
        if (!now.count(kv.first))
            step.changes.emplace_back(kv.first, kv.second, coedit::Bytes());
    }
    const bool held = anyHeld();
    if (held && !s.undoWasHeld)
        s.undoFresh = true;
    if (!step.changes.empty()) {
        if ((held || s.undoWasHeld) && !s.undoFresh && !s.undos.empty()) {
            //(the same stroke: into its undo; each value as it was before the stroke)
            UndoStep& last = s.undos.back();
            for (auto& c : step.changes) {
                auto it = std::find_if(last.changes.begin(), last.changes.end(),
                    [&c](const std::tuple<uint32_t, coedit::Bytes, coedit::Bytes>& o) { return std::get<0>(o) == std::get<0>(c); });
                if (it != last.changes.end())
                    std::get<2>(*it) = std::get<2>(c);
                else
                    last.changes.push_back(std::move(c));
            }
        } else {
            s.undos.push_back(std::move(step));
            if (s.undos.size() > 100)
                s.undos.erase(s.undos.begin());
        }
        s.undoFresh = false;
        s.redos.clear();
    }
    s.undoWasHeld = held;
    s.undoBase = std::move(now);
}

// The document changed by others (online): their changes are not ours to undo
void undoRebase()
{
    if (!s.undoDoc || coedit::stats().opsApplied == s.undoApplied)
        return;
    s.undoApplied = coedit::stats().opsApplied;
    s.undoBase.clear();
    s.undoDoc->capture(s.undoBase);
}

void undoApply(bool redo)
{
    std::vector<UndoStep>& from = redo ? s.redos : s.undos;
    if (!s.undoDoc || from.empty()) {
        toast(redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    UndoStep step = std::move(from.back());
    from.pop_back();
    std::unordered_map<uint32_t, coedit::Bytes> now;
    s.undoDoc->capture(now);
    //(the sections first, as when joining)
    std::sort(step.changes.begin(), step.changes.end(),
        [](const std::tuple<uint32_t, coedit::Bytes, coedit::Bytes>& a, const std::tuple<uint32_t, coedit::Bytes, coedit::Bytes>& b) {
            return std::get<0>(a) < std::get<0>(b);
        });
    int kept = 0;
    for (const auto& c : step.changes) {
        const coedit::Bytes& is = redo ? std::get<1>(c) : std::get<2>(c);
        const coedit::Bytes& back = redo ? std::get<2>(c) : std::get<1>(c);
        auto it = now.find(std::get<0>(c));
        const coedit::Bytes empty;
        //(changed since, by someone else: theirs stays)
        if ((it == now.end() ? empty : it->second) != is) {
            kept++;
            continue;
        }
        s.undoDoc->apply(std::get<0>(c), back);
    }
    s.undoDoc->applied();
    (redo ? s.undos : s.redos).push_back(std::move(step));
    s.undoBase.clear();
    s.undoDoc->capture(s.undoBase);
    s.undoWasHeld = false;
    const size_t left = redo ? s.redos.size() : s.undos.size();
    std::string text = redo ? "Redone" : "Undone";
    if (left > 0)
        text += " (" + std::to_string(left) + " more)";
    if (kept > 0)
        text += ": others changed some of it since";
    toast(text);
}

// ---- the overlays' input (the controller that opened it)

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
        const Widget& w = g_widgets[static_cast<size_t>(k)];
        const SDL_Rect& r = w.hit;
        if (w.visible && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return k;
    }
    return -1;
}

void hoverAt(float x, float y)
{
    const int at = widgetAt(x, y);
    if (at != s.hover) {
        s.hover = at;
        s.hoverRect = at >= 0 ? g_widgets[static_cast<size_t>(at)].r : SDL_Rect {};
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
    s.toast.clear();
    s.hover = -1;
    //(opened with a controller: its d-pad moves a highlight; the pointer stays on the map)
    s.nav = owner >= 0;
    s.focusValid = false;
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
    s.focusValid = false;
    s.overlayDirty = true;
}

// A command of the editor: its key, with the window left open unless it opens a screen of its own
void runKey(SDL_Keycode key, bool shift = false, bool ctrl = false, bool keepOpen = false)
{
    if (!keepOpen)
        closeWindow();
    tapKey(key, shift, ctrl);
}


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

const std::vector<OskKey>& oskKeys()
{
    static const std::vector<OskKey> keys = [] {
        std::vector<OskKey> k;
        const OskKind left[] = {OSK_ENTER, OSK_SHIFT, OSK_CAPS, OSK_CLEAR, OSK_CANCEL};
        for (int row = 0; row < 5; row++)
            k.push_back({{OSK_LEFT_X, OSK_TOP + row * OSK_ROW, OSK_LEFT_W, 32}, left[row], 0});
        const char* rows[] = {"1234567890", "abcdefghij", "klmnopqrst", "uvwxyz-_="};
        for (int row = 0; row < 4; row++) {
            for (int col = 0; rows[row][col]; col++)
                k.push_back({{OSK_GRID_X + col * OSK_COL, OSK_TOP + row * OSK_ROW - 1, OSK_COL - 4, 34}, OSK_CHAR, rows[row][col]});
        }
        k.push_back({{OSK_GRID_X, OSK_TOP + 4 * OSK_ROW, 6 * OSK_COL - 4, 32}, OSK_SPACE, ' '});
        k.push_back({{OSK_GRID_X + 6 * OSK_COL, OSK_TOP + 4 * OSK_ROW, 4 * OSK_COL - 4, 32}, OSK_BACKSPACE, 0});
        return k;
    }();
    return keys;
}

int oskKeyAt(float x, float y)
{
    const std::vector<OskKey>& keys = oskKeys();
    for (size_t i = 0; i < keys.size(); i++) {
        const SDL_Rect& r = keys[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return static_cast<int>(i);
    }
    return -1;
}

void oskPress(int index)
{
    const std::vector<OskKey>& keys = oskKeys();
    if (index < 0 || index >= static_cast<int>(keys.size()))
        return;
    const OskKey& k = keys[static_cast<size_t>(index)];
    s.oskTyped = true;
    s.oskPressedKey = index;
    s.oskPressedMs = SDL_GetTicks();
    switch (k.kind) {
        case OSK_CHAR:
        case OSK_SPACE: {
            const bool letter = k.ch >= 'a' && k.ch <= 'z';
            tapKey(static_cast<SDL_Keycode>(k.ch), (s.oskShift != s.oskCaps) && letter);
            //(Shift: for one letter)
            if (letter)
                s.oskShift = false;
            break;
        }
        case OSK_SHIFT: s.oskShift = !s.oskShift; break;
        case OSK_CAPS: s.oskCaps = !s.oskCaps; break;
        case OSK_CLEAR:
            for (int n = 0; n < 64; n++)
                pushKey(SDLK_BACKSPACE, true), pushKey(SDLK_BACKSPACE, false);
            break;
        case OSK_BACKSPACE: tapKey(SDLK_BACKSPACE); break;
        case OSK_ENTER: tapKey(SDLK_RETURN); break;
        case OSK_CANCEL: tapKey(SDLK_ESCAPE); break;
    }
    s.overlayDirty = true;
}

// A key pressed some other way (the controller's B / Y / Start, the keyboard): drawn down too
void oskShowPressed(OskKind kind, char ch = 0)
{
    const std::vector<OskKey>& keys = oskKeys();
    for (size_t i = 0; i < keys.size(); i++) {
        if (keys[i].kind == kind && (kind != OSK_CHAR || keys[i].ch == ch)) {
            s.oskPressedKey = static_cast<int>(i);
            s.oskPressedMs = SDL_GetTicks();
            s.overlayDirty = true;
            return;
        }
    }
}

// The d-pad: to the nearest key that way (around at the edges)
void oskMove(int dx, int dy)
{
    const std::vector<OskKey>& keys = oskKeys();
    const SDL_Rect& a = keys[static_cast<size_t>(s.oskKey)].r;
    const float ax = a.x + a.w / 2.0f;
    const float ay = a.y + a.h / 2.0f;
    int best = -1;
    float bestScore = 1e9f;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (size_t i = 0; i < keys.size(); i++) {
            if (static_cast<int>(i) == s.oskKey)
                continue;
            const SDL_Rect& b = keys[i].r;
            float bx = b.x + b.w / 2.0f;
            float by = b.y + b.h / 2.0f;
            //(the second pass: around, as if the keyboard repeated beyond its edge)
            if (pass == 1) {
                if (dx > 0) bx += 2000;
                if (dx < 0) bx -= 2000;
                if (dy > 0) by += 2000;
                if (dy < 0) by -= 2000;
            }
            const float along = dx != 0 ? (bx - ax) * static_cast<float>(dx) : (by - ay) * static_cast<float>(dy);
            if (along <= 4.0f)
                continue;
            const float across = dx != 0 ? std::fabs(by - ay) : std::fabs(bx - ax);
            if (dx != 0 && across > 8.0f)
                continue;    //(left and right stay in the row)
            const float score = along + across * 2.0f;
            if (score < bestScore) {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
    }
    if (best >= 0) {
        s.oskKey = best;
        s.overlayDirty = true;
    }
}

void updateKeyboard(Pointer& p)
{
    const bool up = p.pad.buttons[UP] || p.pad.ly < -0.6f;
    const bool down = p.pad.buttons[DOWN] || p.pad.ly > 0.6f;
    const bool left = p.pad.buttons[LEFT] || p.pad.lx < -0.6f;
    const bool right = p.pad.buttons[RIGHT] || p.pad.lx > 0.6f;
    if (p.up.fire(up))
        oskMove(0, -1);
    if (p.down.fire(down))
        oskMove(0, 1);
    if (p.left.fire(left))
        oskMove(-1, 0);
    if (p.right.fire(right))
        oskMove(1, 0);
    if (p.pressed(A))
        oskPress(s.oskKey);
    if (p.pressed(B)) {
        tapKey(SDLK_BACKSPACE);
        oskShowPressed(OSK_BACKSPACE);
    }
    if (p.pressed(X)) {
        s.oskShift = !s.oskShift;
        oskShowPressed(OSK_SHIFT);
    }
    if (p.pressed(Y)) {
        tapKey(SDLK_SPACE);
        oskShowPressed(OSK_SPACE);
    }
    if (p.pressed(START) || p.pressed(RB)) {
        tapKey(SDLK_RETURN);
        oskShowPressed(OSK_ENTER);
    }
    if (p.pressed(BACK))
        tapKey(SDLK_ESCAPE);
}

// ---- the level editor's quick palette (LB and RB on the map): the blocks, the items, the tile types

struct Quick {
    int kind;    // 0 a block, 1 an item, 2 a tile type
    int id;
    bool on;
};

std::vector<Quick> quickPalette()
{
    std::vector<Quick> q;
    for (int id : {0, 1, 2, 3, 4, 5, 6, 15, 16, 17, 18, 19})
        q.push_back({0, id, true});
    for (int c = 0; c < 4; c++) {
        q.push_back({0, 7 + c, true});
        q.push_back({0, 7 + c, false});
    }
    for (int c = 0; c < 4; c++) {
        q.push_back({0, 11 + c, true});
        q.push_back({0, 11 + c, false});
    }
    for (int id = 20; id < 30; id++)
        q.push_back({0, id, true});
    for (int id = 0; id < 6; id++)
        q.push_back({1, id, true});
    for (int id = 1; id <= 18; id++)
        q.push_back({2, id, true});
    return q;
}

void quickStep(int delta)
{
    namespace sh = leveleditor::shell;
    const std::vector<Quick> q = quickPalette();
    const int n = static_cast<int>(q.size());
    int at = -1;
    for (int k = 0; k < n && at < 0; k++) {
        const Quick& e = q[static_cast<size_t>(k)];
        if (e.kind == 0 && sh::mode() == 0 && sh::block() == e.id) {
            const bool on = e.id >= 11 && e.id <= 14 ? sh::switchBlockOn() : e.id >= 7 && e.id <= 10 ? sh::switchOn(e.id - 7) : true;
            if (on == e.on)
                at = k;
        } else if (e.kind == 1 && sh::mode() == 7 && sh::item() == e.id) {
            at = k;
        } else if (e.kind == 2 && sh::mode() == 6 && sh::tileType() == e.id) {
            at = k;
        }
    }
    at = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
    const Quick& e = q[static_cast<size_t>(at)];
    if (e.kind == 0)
        sh::setBlock(e.id, e.on);
    else if (e.kind == 1)
        sh::setItem(e.id);
    else
        sh::setTileType(e.id);
    if (currentTool() != TOOL_ERASE)
        setTool(TOOL_PAINT);
    s.overlayDirty = true;
}

// ---- the last things picked (LB / RB on the level editor's map)

// Each frame: what is selected now goes first (unless LB / RB just went to it)
void trackRecent()
{
    if (!levelMap())
        return;
    const leveleditor::shell::Brush b = leveleditor::shell::brush();
    if (b.mode < 0 || b == s.lastBrush)
        return;
    s.lastBrush = b;
    if (b != s.stepped) {
        s.recents.erase(std::remove(s.recents.begin(), s.recents.end(), b), s.recents.end());
        s.recents.insert(s.recents.begin(), b);
        if (s.recents.size() > 8)
            s.recents.resize(8);
        s.stepped = leveleditor::shell::Brush();
    }
    s.overlayDirty = true;
}

void recentStep(int delta)
{
    namespace sh = leveleditor::shell;
    if (s.recents.size() < 2) {
        //(nothing to go back to yet: through the blocks, items and tile types)
        quickStep(delta);
        return;
    }
    const sh::Brush now = sh::brush();
    const int n = static_cast<int>(s.recents.size());
    int at = static_cast<int>(std::find(s.recents.begin(), s.recents.end(), now) - s.recents.begin());
    at = at >= n ? 0 : ((at + delta) % n + n) % n;
    s.stepped = s.recents[static_cast<size_t>(at)];
    sh::setBrush(s.stepped);
    if (currentTool() != TOOL_ERASE)
        setTool(TOOL_PAINT);
    s.overlayDirty = true;
}

// ---- the tools menu (the right stick pressed): undo, redo, and on the level editor's map copy, paste, fill

enum ToolItem { T_UNDO, T_REDO, T_COPY, T_PASTE, T_FILL, T_ITEM_COUNT };
constexpr int TOOLS_W = 210;
constexpr int TOOLS_ROW = 30;

// The editor shows its map (the level's or the world's), not one of its other screens
bool mapShown()
{
    return levelMap() || (s.editor == Editor::World && worldeditor::shell::inMapView());
}

int toolsCount()
{
    return levelMap() ? T_ITEM_COUNT : 2;
}

SDL_Rect toolsRect()
{
    const int h = 40 + toolsCount() * TOOLS_ROW + 32;
    //(beside the pointer, on the screen)
    const int x = std::clamp(static_cast<int>(s.toolsX) + 24, 6, SCREEN_W - TOOLS_W - 6);
    const int y = std::clamp(static_cast<int>(s.toolsY) - 20, 6, SCREEN_H - h - 6);
    return {x, y, TOOLS_W, h};
}

SDL_Rect toolsItemRect(int k)
{
    const SDL_Rect r = toolsRect();
    return {r.x + 10, r.y + 38 + k * TOOLS_ROW, r.w - 20, TOOLS_ROW - 3};
}

// Whether it can be used now (else: why not)
bool toolUsable(int item, std::string* why = nullptr)
{
    namespace sh = leveleditor::shell;
    std::string reason;
    switch (item) {
        case T_UNDO: reason = s.undos.empty() ? "Nothing to undo" : ""; break;
        case T_REDO: reason = s.redos.empty() ? "Nothing to redo" : ""; break;
        case T_COPY: break;
        case T_PASTE: reason = sh::clipboard(nullptr, nullptr) ? "" : "Copy an area first"; break;
        case T_FILL: reason = sh::paintsCells() ? "" : "Pick blocks, tiles, tile types or zones to fill with"; break;
        default: break;
    }
    if (why)
        *why = reason;
    return reason.empty();
}

void openTools(int owner)
{
    for (size_t k = 0; k < s.pointers.size(); k++) {
        if (s.pointers[k].buttons & SDL_BUTTON_LMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_LEFT, false);
        if (s.pointers[k].buttons & SDL_BUTTON_RMASK)
            pushButton(static_cast<int>(k), SDL_BUTTON_RIGHT, false);
        s.pointers[k].drag = -1;
    }
    s.overlay = Overlay::Tools;
    s.overlayOwner = owner;
    s.toolsX = owner >= 0 && owner < static_cast<int>(s.pointers.size()) ? s.pointers[static_cast<size_t>(owner)].x : s.cx;
    s.toolsY = owner >= 0 && owner < static_cast<int>(s.pointers.size()) ? s.pointers[static_cast<size_t>(owner)].y : s.cy;
    s.toolsIndex = 0;
    s.toolsOpenedMs = SDL_GetTicks();
    s.toolsMoved = false;
    s.overlayDirty = true;
}

void closeTools()
{
    s.overlay = Overlay::None;
    s.overlayDirty = true;
}

void chooseTool(int item)
{
    std::string why;
    if (!toolUsable(item, &why)) {
        toast(why);
        return;
    }
    switch (item) {
        case T_UNDO: undoApply(false); return;    //(the menu stays: undo again)
        case T_REDO: undoApply(true); return;
        case T_COPY: s.armed = Armed::Copy; toast("Copy: hold A and drag over the cells"); break;
        case T_PASTE: s.armed = Armed::Paste; toast("Paste: A puts it down, B stops"); break;
        case T_FILL: s.armed = Armed::Fill; toast("Fill: A fills the cells joined to that one"); break;
        default: break;
    }
    closeTools();
}

void updateTools(Pointer& p)
{
    const bool up = p.pad.buttons[UP] || p.pad.ly < -0.6f;
    const bool down = p.pad.buttons[DOWN] || p.pad.ly > 0.6f;
    const int n = toolsCount();
    if (p.up.fire(up)) {
        s.toolsIndex = (s.toolsIndex + n - 1) % n;
        s.toolsMoved = true;
        s.overlayDirty = true;
    }
    if (p.down.fire(down)) {
        s.toolsIndex = (s.toolsIndex + 1) % n;
        s.toolsMoved = true;
        s.overlayDirty = true;
    }
    if (p.pressed(A))
        chooseTool(s.toolsIndex);
    else if (p.pressed(B) || p.pressed(BACK) || p.pressed(START) || p.pressed(RS))
        closeTools();
    else if (p.released(RS) && s.toolsMoved && SDL_GetTicks() - s.toolsOpenedMs > 300)
        chooseTool(s.toolsIndex);    //(held, moved and let go: that one)
}

// ---- the window's highlight: the controller that opened it

int focusIndex();
void moveFocus(int dx, int dy);

void updateWindowNav(Pointer& p)
{
    const bool up = p.pad.buttons[UP] || p.pad.ly < -0.6f;
    const bool down = p.pad.buttons[DOWN] || p.pad.ly > 0.6f;
    const bool left = p.pad.buttons[LEFT] || p.pad.lx < -0.6f;
    const bool right = p.pad.buttons[RIGHT] || p.pad.lx > 0.6f;
    if (p.up.fire(up))
        moveFocus(0, -1);
    if (p.down.fire(down))
        moveFocus(0, 1);
    if (p.left.fire(left))
        moveFocus(-1, 0);
    if (p.right.fire(right))
        moveFocus(1, 0);
    if (p.pressed(A)) {
        const int k = focusIndex();
        if (k >= 0 && g_widgets[static_cast<size_t>(k)].act) {
            const Widget w = g_widgets[static_cast<size_t>(k)];
            w.act();
            //(a block, an item, a tool: back to the map with it)
            if (w.pick)
                closeWindow();
        }
        s.overlayDirty = true;
    }
    if (p.pressed(B) || p.pressed(START) || p.pressed(BACK))
        closeWindow();
    if (p.pressed(RB))
        turnPage(1);
    if (p.pressed(LB))
        turnPage(-1);
}

// A key held as long as a button is (an editor screen's X / Y / RB)
void holdButton(Pointer& p, Button b, SDL_Keycode key)
{
    if (!key)
        return;
    const SDL_Scancode code = SDL_GetScancodeFromKey(key, nullptr);
    if (p.pressed(b)) {
        s.keys[code] = true;
        pushKey(key, true);
    } else if (p.released(b) && s.keys[code]) {
        s.keys[code] = false;
        pushKey(key, false);
    }
}

// ---- the pointers

void updatePointer(int index)
{
    Pointer& p = s.pointers[static_cast<size_t>(index)];
    if (s.overlay == Overlay::Window && s.nav && index == s.overlayOwner) {
        updateWindowNav(p);
        return;
    }
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
    leveleditor::shell::TilePicker picker;
    const bool picking = s.editor == Editor::Level && s.overlay == Overlay::None && leveleditor::shell::tilePicker(&picker);
    if (up || down || left || right) {
        const float mx = std::floor(p.x / TILE) * TILE + TILE / 2.0f;
        const float my = std::floor(p.y / TILE) * TILE + TILE / 2.0f;
        int stepX = (right ? 1 : 0) - (left ? 1 : 0);
        int stepY = (down ? 1 : 0) - (up ? 1 : 0);
        if (picking) {
            //The tileset: tile by tile; at the screen's edge the tileset scrolls instead (the arrow keys)
            const int c = static_cast<int>(p.x) / TILE;
            const int r = static_cast<int>(p.y) / TILE;
            const int lastCol = std::min(SCREEN_W / TILE, picker.cols - picker.viewCol) - 1;
            const int lastRow = std::min(SCREEN_H / TILE, picker.rows - picker.viewRow) - 1;
            if (stepX < 0 && c == 0) {
                if (picker.viewCol > 0)
                    tapKey(SDLK_LEFT);
                stepX = 0;
            } else if (stepX > 0 && c >= lastCol) {
                if (picker.viewCol + SCREEN_W / TILE < picker.cols)
                    tapKey(SDLK_RIGHT);
                stepX = 0;
            }
            if (stepY < 0 && r == 0) {
                if (picker.viewRow > 0)
                    tapKey(SDLK_UP);
                stepY = 0;
            } else if (stepY > 0 && r >= lastRow) {
                if (picker.viewRow + SCREEN_H / TILE < picker.rows)
                    tapKey(SDLK_DOWN);
                stepY = 0;
            }
            s.overlayDirty = true;
        }
        dx += mx - p.x + static_cast<float>(stepX * TILE);
        dy += my - p.y + static_cast<float>(stepY * TILE);
    } else if (picking && (p.last.lx != 0 || p.last.ly != 0) && p.pad.lx == 0 && p.pad.ly == 0) {
        //(the stick let go: onto the middle of its tile)
        dx += std::floor(p.x / TILE) * TILE + TILE / 2.0f - p.x;
        dy += std::floor(p.y / TILE) * TILE + TILE / 2.0f - p.y;
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
        namespace sh = leveleditor::shell;
        const int tool = currentTool();
        const bool onMap = levelMap() && s.overlay != Overlay::Keyboard;
        const int col = std::clamp(static_cast<int>(p.x) / TILE, 0, sh::MAP_COLS - 1);
        const int row = std::clamp(static_cast<int>(p.y) / TILE, 0, sh::MAP_ROWS - 1);
        //(a rectangle: held and moved, then let go; the area to copy, or what the mode paints cell by cell)
        const bool rectangles = onMap && (s.armed == Armed::Copy || (s.armed == Armed::None && sh::paintsCells() && tool != TOOL_PICK));
        if (p.pressed(A) || p.pressed(B))
            p.shown = true;
        if (onMap && s.armed != Armed::None && (p.pressed(B) || p.pressed(BACK))) {
            //(B: the armed tool put away)
            s.armed = Armed::None;
            p.drag = -1;
            toast("Stopped");
            //(used up: not the editor's right button or Escape too)
            p.last.buttons[B] = p.pad.buttons[B];
            p.last.buttons[BACK] = p.pad.buttons[BACK];
        } else if (onMap && s.armed == Armed::Paste && p.pressed(A)) {
            sh::paste(col, row);
        } else if (onMap && s.armed == Armed::Fill && p.pressed(A)) {
            sh::floodFill(col, row);
            s.armed = Armed::None;
        } else if (rectangles && p.drag < 0 && (p.pressed(A) || p.pressed(B))) {
            p.drag = p.pressed(A) ? A : B;
            p.dragCol = col;
            p.dragRow = row;
            moveMouseTo(index);
        } else if (p.drag >= 0) {
            if (p.released(static_cast<Button>(p.drag))) {
                const bool erase = p.drag == B || tool == TOOL_ERASE;
                if (s.armed == Armed::Copy) {
                    sh::copyArea(p.dragCol, p.dragRow, col, row);
                    s.armed = Armed::None;
                    int w = 0, h = 0;
                    sh::clipboard(&w, &h);
                    toast("Copied " + std::to_string(w) + " x " + std::to_string(h) + ": Paste is in the tools");
                } else if (col == p.dragCol && row == p.dragRow) {
                    //(one cell: a click, as the editor does it)
                    pushButton(index, erase ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, true);
                    pushButton(index, erase ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, false);
                } else {
                    sh::fillRect(p.dragCol, p.dragRow, col, row, erase);
                }
                p.drag = -1;
                s.overlayDirty = true;
            }
        } else if (p.pressed(A) && tool == TOOL_PICK && levelMap()) {
            sh::pick(col, row);
            setTool(TOOL_PAINT);
        } else if (p.pressed(A)) {
            pushButton(index, tool == TOOL_ERASE && levelMap() ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, true);
        } else if (p.released(A) && (p.buttons & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) && !p.pad.buttons[B]) {
            pushButton(index, (p.buttons & SDL_BUTTON_LMASK) ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, false);
        }
        if (p.drag < 0 && !rectangles && s.armed == Armed::None) {
            if (p.pressed(B))
                pushButton(index, SDL_BUTTON_RIGHT, true);
            else if (p.released(B) && (p.buttons & SDL_BUTTON_RMASK))
                pushButton(index, SDL_BUTTON_RIGHT, false);
        }
        //The level editor's map: X takes what is under the reticle, Y the next layer
        if (levelMap() && s.overlay == Overlay::None) {
            namespace sh = leveleditor::shell;
            if (p.pressed(X)) {
                p.shown = true;
                sh::pick(static_cast<int>(p.x) / TILE, static_cast<int>(p.y) / TILE);
                if (currentTool() != TOOL_ERASE)
                    setTool(TOOL_PAINT);
                s.overlayDirty = true;
            }
            if (p.pressed(Y)) {
                sh::setLayer((sh::layer() + 1) % 4);
                s.overlayDirty = true;
            }
        }
    }

    //(on the level editor's map, the shoulders go through the blocks, items and tile types)
    const bool quick = levelMap() && s.overlay == Overlay::None;
    //(on the level editor's other screens: what they want of the shoulders, and keys held with X, Y, RB)
    const leveleditor::shell::Screen& sc = s.screen;
    const bool onScreen = s.overlay == Overlay::None && !sc.title.empty();
    if (onScreen) {
        holdButton(p, X, sc.xHold);
        holdButton(p, Y, sc.yHold);
        holdButton(p, RB, sc.rbHold);
    }
    if (p.pressed(RB) && s.overlay == Overlay::Window)
        turnPage(1);
    else if (p.pressed(RB) && quick)
        recentStep(1);
    else if (p.pressed(RB) && onScreen && sc.rbHold)
        ;
    else if (p.pressed(RB) && onScreen && sc.rb)
        tapKey(sc.rb);
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
    //The right stick pressed: the tools (undo, copy, paste, fill)
    if (p.pressed(RS) && s.overlay == Overlay::None && mapShown())
        openTools(index);
    if (p.pressed(LB)) {
        if (s.overlay == Overlay::Window) {
            turnPage(-1);
        } else if (quick) {
            recentStep(-1);
        } else if (onScreen && sc.lb) {
            tapKey(sc.lb);
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
        const bool navigating = s.overlay == Overlay::Window && s.nav && &p == &s.pointers[static_cast<size_t>(std::max(0, s.overlayOwner))];
        const bool free = s.overlay != Overlay::Keyboard && !inWindow(p.x, p.y) && !navigating;
        if ((inWindow(p.x, p.y) || navigating) && std::fabs(p.pad.ry) > 0.3f) {
            s.scroll = std::clamp(s.scroll + static_cast<int>(p.pad.ry * 10.0f), 0, s.scrollMax);
            s.overlayDirty = true;
        }
        if (free) {
            //(the triggers; X and Y too, except on the level editor's map where they pick and change the layer)
            shift = shift || p.pad.buttons[LT] || (p.pad.buttons[X] && !levelMap() && !s.screen.xHold);
            ctrl = ctrl || p.pad.buttons[RT] || (p.pad.buttons[Y] && !levelMap() && !s.screen.yHold);
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
// A cursor: a white glove pointing up and left (Mario Paint's), its cuff in the player's color;
// the fingertip is the hot spot
// A glove's animation: when it last moved, and its button (held, and when it was pressed)
struct Glove {
    float x = -1, y = -1;
    Uint64 movedMs = 0;
    Uint64 pressMs = 0;
    bool down = false;
};
std::map<int, Glove> g_gloves;    // (by pointer: the controllers 0.., the mouse -1, the others online 1000..)

// A cursor: a white glove pointing up and left (Mario Paint's), its cuff in the player's color;
// the fingertip is the hot spot. Still for a moment, it bobs; its button held, the finger bends
// and the hand dips; a press, a little burst at the fingertip.
void drawArrow(float fx, float fy, SDL_Color color, const std::string& label, int id = -2, bool down = false)
{
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const Uint64 now = SDL_GetTicks();
    Glove dummy;
    Glove& g = id == -2 ? dummy : g_gloves[id];
    if (g.x != fx || g.y != fy) {
        g.x = fx;
        g.y = fy;
        g.movedMs = now;
    }
    if (down && !g.down)
        g.pressMs = now;
    g.down = down;
    const bool idle = !down && now - g.movedMs > 700;
    const int bob = idle ? static_cast<int>(((now - g.movedMs) / 350) % 2) * 2 : 0;
    static const std::vector<const char*> GLOVE = {
        "..##.........",
        ".#ww#........",
        ".#ww#........",
        ".#ww#........",
        ".#ww####.....",
        ".#ww#ww####..",
        "##ww#ww#ww##.",
        "#w#wwwwwwwgw#",
        "#ww#wwwwwwww#",
        "#wwwwwwwwwgw#",
        ".#wwwwwwwgw#.",
        "..#wwwwwgg#..",
        "..#ccccccc#..",
        "..#CCCCCCC#..",
        "...#######...",
    };
    const std::vector<prompts::Ink> inks = {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}, {'g', {184, 184, 200, 255}}, {'c', color},
        {'C', theme::shade(color, 70)}};
    if (down) {
        //(pressing: the finger bent down to the spot, the hand lower)
        std::vector<const char*> pressed = {"..##.........", ".#ww#........"};
        pressed.insert(pressed.end(), GLOVE.begin() + 4, GLOVE.end());
        prompts::art(x0 - 5, y0 + 3, pressed, inks, 2);
    } else {
        prompts::art(x0 - 5, y0 - 1 + bob, GLOVE, inks, 2);
    }
    //A press: a burst of short white lines around the fingertip
    if (now - g.pressMs < 180 && g.pressMs > 0) {
        const int r = 6 + static_cast<int>((now - g.pressMs) / 40);
        const SDL_Color black {0, 0, 0, 255};
        const SDL_Color white {255, 255, 255, 255};
        const int dirs[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}};
        for (const auto& d : dirs) {
            const int px = x0 + d[0] * r;
            const int py = y0 + d[1] * r;
            fill(px - 2, py - 2, 4, 4, black);
            fill(px - 1, py - 1, 2, 2, white);
        }
    }
    if (idle || now - g.pressMs < 200)
        s.overlayDirty = true;    //(it moves by itself)
    if (!label.empty()) {
        const int w = rm->menu_font_small.getWidth(label) + 6;
        const int lx = std::min(x0 + 22, SCREEN_W - w);
        const int ly = std::min(y0 + 26, SCREEN_H - 16);
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

void drawArt(int x, int y, const Art& art, int scale = 2)
{
    prompts::art(x, y, art, {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}, {'b', {168, 96, 40, 255}}, {'d', {112, 56, 16, 255}},
                                {'g', {56, 184, 48, 255}}, {'G', {152, 232, 88, 255}}, {'r', {216, 40, 32, 255}}, {'y', {248, 208, 48, 255}},
                                {'c', {104, 168, 240, 255}}, {'s', {168, 168, 176, 255}}}, scale);
}

// Some 32 pixel drawing, at another size: drawn on the side, then scaled (as pixel art)
template <typename Draw>
void drawScaled(int x, int y, int size, Draw draw)
{
    static SDL_Surface* canvas = nullptr;
    if (!canvas) {
        canvas = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!canvas)
            return;
        SDL_SetSurfaceBlendMode(canvas, SDL_BLENDMODE_BLEND);
    }
    SDL_FillSurfaceRect(canvas, nullptr, SDL_MapSurfaceRGBA(canvas, 0, 0, 0, 0));
    SDL_Surface* const was = screen;
    SDL_Surface* const wasDest = blitdest;
    screen = canvas;
    blitdest = canvas;
    draw(0, 0);
    screen = was;
    blitdest = wasDest;
    const SDL_Rect dst {x, y, size, size};
    SDL_BlitSurfaceScaled(canvas, nullptr, screen, &dst, SDL_SCALEMODE_PIXELART);
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
SDL_Rect g_pageView {};    // the scrolling part of the page (the last frame's)

// (widgets scrolled out of the page stay in the list: the d-pad can go to them, and the page scrolls)
void addWidget(const SDL_Rect& r, std::function<void()> act, std::string tip, Zone zone = ZONE_PAGE, bool selected = false, bool pick = false)
{
    Widget w;
    w.r = r;
    w.act = std::move(act);
    w.tip = std::move(tip);
    w.visible = SDL_GetRectIntersection(&r, &g_clip, &w.hit);
    w.zone = zone;
    w.selected = selected;
    w.pick = pick;
    g_widgets.push_back(std::move(w));
}

bool sameRect(const SDL_Rect& a, const SDL_Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool hovered(const SDL_Rect& r)
{
    return s.hover >= 0 && sameRect(s.hoverRect, r);
}

// ---- the highlight (a controller's d-pad in the window)

int focusIndex()
{
    if (!s.focusValid)
        return -1;
    for (size_t k = 0; k < g_widgets.size(); k++) {
        if (sameRect(g_widgets[k].r, s.focusRect))
            return static_cast<int>(k);
    }
    return -1;
}

// The page scrolled so the highlight shows
void scrollToFocus()
{
    const int k = focusIndex();
    if (k < 0 || g_widgets[static_cast<size_t>(k)].zone != ZONE_PAGE || g_pageView.h <= 0)
        return;
    const SDL_Rect& r = g_widgets[static_cast<size_t>(k)].r;
    int delta = 0;
    if (r.y < g_pageView.y)
        delta = r.y - g_pageView.y - 4;
    else if (r.y + r.h > g_pageView.y + g_pageView.h)
        delta = r.y + r.h - (g_pageView.y + g_pageView.h) + 4;
    const int scroll = std::clamp(s.scroll + delta, 0, s.scrollMax);
    s.focusRect.y -= scroll - s.scroll;
    s.scroll = scroll;
}

void focusOn(int k)
{
    s.focusRect = g_widgets[static_cast<size_t>(k)].r;
    s.focusValid = true;
    scrollToFocus();
    s.hover = k;
    s.hoverRect = s.focusRect;
    s.overlayDirty = true;
}

// Once the window is laid out: the highlight on a widget (the page's selected item, else its first)
void settleFocus()
{
    if (focusIndex() >= 0) {
        s.hover = focusIndex();
        s.hoverRect = s.focusRect;
        return;
    }
    int best = -1;
    for (int pass = 0; pass < 4 && best < 0; pass++) {
        for (size_t k = 0; k < g_widgets.size() && best < 0; k++) {
            const Widget& w = g_widgets[k];
            const bool ok = pass == 0 ? w.zone == ZONE_PAGE && w.selected
                          : pass == 1 ? w.zone == ZONE_PAGE
                          : pass == 2 ? w.zone == ZONE_RAIL && w.selected
                                      : true;
            if (ok)
                best = static_cast<int>(k);
        }
    }
    if (best >= 0)
        focusOn(best);
}

// The highlight to the nearest widget that way
void moveFocus(int dx, int dy)
{
    const int from = focusIndex();
    if (from < 0)
        return;
    const SDL_Rect& a = g_widgets[static_cast<size_t>(from)].r;
    const float ax = a.x + a.w / 2.0f;
    const float ay = a.y + a.h / 2.0f;
    int best = -1;
    float bestScore = 1e9f;
    for (size_t k = 0; k < g_widgets.size(); k++) {
        if (static_cast<int>(k) == from)
            continue;
        const SDL_Rect& b = g_widgets[k].r;
        //(beyond the highlight's edge that way)
        const bool beyond = dx > 0 ? b.x >= a.x + a.w - 2 : dx < 0 ? b.x + b.w <= a.x + 2 : dy > 0 ? b.y >= a.y + a.h - 2 : b.y + b.h <= a.y + 2;
        if (!beyond)
            continue;
        const float bx = b.x + b.w / 2.0f;
        const float by = b.y + b.h / 2.0f;
        const float along = dx != 0 ? (bx - ax) * static_cast<float>(dx) : (by - ay) * static_cast<float>(dy);
        //(how far off the line: rows and columns that overlap count as on it)
        const float across = dx != 0 ? std::max(0.0f, std::max(static_cast<float>(a.y - (b.y + b.h)), static_cast<float>(b.y - (a.y + a.h))))
                                     : std::max(0.0f, std::max(static_cast<float>(a.x - (b.x + b.w)), static_cast<float>(b.x - (a.x + a.w))));
        const float score = along + across * 4.0f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(k);
        }
    }
    if (best >= 0)
        focusOn(best);
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
    addWidget(r, std::move(act), tip, ZONE_PAGE, selected, true);
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
    addWidget(r, std::move(act), tip, ZONE_PAGE, selected);
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

void drawPageIcon(int x, int y, int page, int size = 16)
{
    //(the pixel art at a whole number of times its 8 pixels; the sprites scaled)
    const int scale = std::max(2, size / 8);
    const int pad = (size - 8 * scale) / 2;
    if (s.editor != Editor::Level) {
        static const Art* arts[] = {&ART_BRUSH, &ART_LOOK, &ART_DISK, &ART_CLOSE};
        drawArt(x + pad, y + pad, *arts[std::min(page, 3)], scale);
        return;
    }
    if (LEVEL_PAGES[page].art)
        drawArt(x + pad, y + pad, *LEVEL_PAGES[page].art, scale);
    else if (size == 16)
        theme::icon(x, y, theme::Icon::QuestionBlock);
    else
        drawScaled(x, y, size, [](int ix, int iy) { theme::icon(ix + 8, iy + 8, theme::Icon::QuestionBlock); });
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

// Something picked before (the recently used), 32 pixels
void drawBrush(const leveleditor::shell::Brush& b, int x, int y)
{
    switch (b.mode) {
        case 0: drawBlock(x, y, b.id, b.on); break;
        case 1: leveleditor::shell::drawBrushTiles(b, x, y, 32); break;
        case 6:
            if (rm->spr_transparenttiles)
                rm->spr_transparenttiles.draw(x, y, {(b.id - 1) * 32, 0, 32, 32});
            break;
        case 7:
            if (rm->spr_mapitems[0])
                rm->spr_mapitems[0].draw(x, y, {b.id * 32, 0, 32, 32});
            break;
        default: break;
    }
}

std::string brushName(const leveleditor::shell::Brush& b)
{
    switch (b.mode) {
        case 0:
            for (const Entry& e : BLOCKS)
                if (e.id == b.id)
                    return e.name;
            for (const Entry& e : BREAKABLE)
                if (e.id == b.id)
                    return e.name;
            if (b.id >= 7 && b.id <= 10)
                return std::string(COLORS[b.id - 7]) + " switch, " + (b.on ? "ON" : "OFF");
            if (b.id >= 11 && b.id <= 14)
                return std::string(COLORS[b.id - 11]) + " switch block, " + (b.on ? "solid" : "an outline");
            return "A block";
        case 1: return b.cols * b.rows > 1 ? std::to_string(b.cols) + " x " + std::to_string(b.rows) + " tiles" : "A tile";
        case 6: return b.id >= 1 && b.id <= 18 ? TILE_TYPES[b.id - 1] : "A tile type";
        case 7: return b.id >= 0 && b.id < 6 ? ITEMS[b.id].name : "A map item";
        default: return {};
    }
}

// A key's short name (for a key cap)
std::string keyCap(SDL_Keycode key, bool shift)
{
    std::string name = key == SDLK_ESCAPE ? "Esc" : key == SDLK_PAGEUP ? "PgUp" : key == SDLK_PAGEDOWN ? "PgDn" : key == SDLK_DELETE ? "Del"
                     : key == SDLK_GRAVE ? "~" : SDL_GetKeyName(key);
    return shift ? "Shift+" + name : name;
}

// The level editor's other screens: what the buttons do there, and its commands
void screenPage(Layout& l)
{
    const leveleditor::shell::Screen& sc = s.screen;
    l.y += drawWrapped(l.x, l.y, l.w, sc.title) + 6;
    for (const leveleditor::shell::Command& c : sc.commands) {
        const std::string cap = keyCap(c.key, c.shift);
        const SDL_Keycode key = c.key;
        const bool shift = c.shift;
        pageButton(l, c.label, [key, shift] { runKey(key, shift); }, c.label + " (" + cap + ")");
        const int kw = std::max(16, theme::font().getWidth(cap) + 8);
        prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, cap);
    }
    if (!sc.holds.empty())
        l.y += drawWrapped(l.x, l.y + 4, l.w, sc.holds) + 8;
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
        addWidget(swap, [] { s.dockLeft = !s.dockLeft; s.overlayDirty = true; s.focusValid = false; }, "Move the window to the other side", ZONE_TITLE);
        const SDL_Rect close {win.x + win.w - 30, win.y + 8, 22, 22};
        theme::block(close.x, close.y, close.w, close.h, hovered(close) ? theme::Tone::Red : theme::Tone::Gray);
        drawArt(close.x + 3, close.y + 3, ART_CLOSE);
        addWidget(close, [] { closeWindow(); }, "Close (Tab)", ZONE_TITLE);
    }

    //(on one of the level editor's other screens: just its commands)
    const bool screenMode = level && !sh::inMapView() && !s.screen.title.empty();

    //Tools: paint, erase, move, pick; the layer; what is shown
    int ty = win.y + 40;
    if (level && !screenMode) {
        static const Art* toolArt[] = {&ART_BRUSH, &ART_ERASER, &ART_MOVE, &ART_PICK};
        static const char* toolTip[] = {"Paint: click or drag to place what is selected (the right button removes)",
            "Erase: click or drag to remove", "Select and move: drag over cells, then drag them somewhere else (M)",
            "Pick: click the map to take the block or tile there"};
        int x = win.x + 8;
        for (int t = 0; t < TOOL_COUNT; t++) {
            const SDL_Rect r {x, ty, 26, 26};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : currentTool() == t ? theme::Tone::Gold : theme::Tone::Gray);
            drawArt(r.x + 5, r.y + 5, *toolArt[t]);
            addWidget(r, [t] { setTool(t); }, toolTip[t], ZONE_TOOLS, currentTool() == t, true);
            x += 28;
        }
        x += 4;
        //The layers: L1 to L4
        for (int layer = 0; layer < 4; layer++) {
            const SDL_Rect r {x, ty, 25, 26};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : sh::layer() == layer ? theme::Tone::Gold : theme::Tone::Gray);
            theme::font().drawCentered(r.x + 13, r.y + (r.h - theme::font().getHeight()) / 2, "L" + std::to_string(layer + 1));
            addWidget(r, [layer] { sh::setLayer(layer); }, "Layer " + std::to_string(layer + 1) + " of 4: where tiles are painted (Y goes through them)",
                ZONE_TOOLS, sh::layer() == layer);
            x += 26;
        }
        x += 4;
        const SDL_Rect only {x, ty, 26, 26};
        theme::block(only.x, only.y, only.w, only.h, hovered(only) ? theme::Tone::Green : sh::othersHidden() ? theme::Tone::Gold : theme::Tone::Gray);
        drawArt(only.x + 5, only.y + 5, ART_EYE);
        addWidget(only, [] { sh::setOthersHidden(!sh::othersHidden()); }, "Show only the active layer (U)", ZONE_TOOLS);
        x += 28;
        const SDL_Rect blocks {x, ty, 26, 26};
        theme::block(blocks.x, blocks.y, blocks.w, blocks.h, hovered(blocks) ? theme::Tone::Green : sh::blocksShown() ? theme::Tone::Gray : theme::Tone::Gold);
        theme::icon(blocks.x + 5, blocks.y + 5, theme::Icon::QuestionBlock);
        if (!sh::blocksShown())
            fill(blocks.x + 4, blocks.y + 12, 18, 2, {216, 40, 32, 255});
        addWidget(blocks, [] { sh::setBlocksShown(!sh::blocksShown()); }, "Hide the blocks, to see the tiles under them (V)", ZONE_TOOLS);
        ty += 32;
        //The last things picked (LB / RB on the map)
        if (!s.recents.empty()) {
            const bool controller = prompts::lastDevice() == prompts::Device::Controller;
            int rx = win.x + 8;
            if (controller)
                rx += prompts::drawPad(rx, ty + 7, prompts::Pad::LB) + 4;
            const sh::Brush now = sh::brush();
            for (const sh::Brush& b : s.recents) {
                const SDL_Rect r {rx, ty, 28, 28};
                const bool sel = b == now;
                theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : sel ? theme::Tone::Gold : theme::Tone::Dark);
                drawScaled(r.x + 2, r.y + 2, 24, [&b](int x, int y) { drawBrush(b, x, y); });
                addWidget(r, [b] { s.stepped = b; sh::setBrush(b); if (currentTool() != TOOL_ERASE) setTool(TOOL_PAINT); },
                    "Picked before: " + brushName(b), ZONE_TOOLS, sel, true);
                rx += 30;
            }
            if (controller)
                prompts::drawPad(rx + 2, ty + 7, prompts::Pad::RB);
            ty += 34;
        }
    }

    //The pages: a rail of icons, the page beside it
    const int bodyTop = ty;
    const int footH = 66;
    const int bodyBottom = win.y + win.h - footH;
    const int count = pageCount();
    const int itemH = std::min(36, (bodyBottom - bodyTop) / count);
    const std::vector<std::string> groups = level ? std::vector<std::string>() : worldGroups();
    for (int k = 0; k < count && !screenMode; k++) {
        const SDL_Rect r {win.x + 8, bodyTop + k * itemH, RAIL_W - 6, itemH - 2};
        const bool sel = s.category == k;
        theme::block(r.x, r.y, r.w, r.h, sel ? theme::Tone::Gold : hovered(r) ? theme::Tone::Green : theme::Tone::Dark);
        const std::string label = level ? LEVEL_PAGES[k].name : groups[static_cast<size_t>(k)];
        //Its icon, big (its name: the page's title, and in the footer)
        const int size = std::min(24, r.h - 4);
        drawPageIcon(r.x + (r.w - size) / 2, r.y + (r.h - size) / 2, k, size);
        addWidget(r, [k] { s.category = k; s.scroll = 0; s.hover = -1; s.focusValid = false; s.overlayDirty = true; },
            level ? label + ": " + LEVEL_PAGES[k].about : label, ZONE_RAIL, sel);
    }

    Layout l;
    l.win = win;
    l.x = screenMode ? win.x + 14 : win.x + RAIL_W + 10;
    l.w = win.x + win.w - 16 - l.x;
    l.y = bodyTop + 2;
    const std::string title = screenMode ? std::string("This screen")
                            : level      ? LEVEL_PAGES[std::clamp(s.category, 0, count - 1)].name
                                         : groups[static_cast<size_t>(std::clamp(s.category, 0, count - 1))];
    theme::bigFont().draw(l.x, l.y - 2, title);
    l.y += theme::bigFont().getHeight() + 2;
    //The page below the title scrolls (the wheel, or the right stick over the window)
    const int viewTop = l.y;
    const int viewH = bodyBottom - 4 - viewTop;
    g_clip = SDL_Rect {l.x - 2, viewTop, l.w + 4, viewH};
    g_pageView = g_clip;
    SDL_SetSurfaceClipRect(screen, &g_clip);
    l.y -= s.scroll;
    const int contentTop = l.y;
    if (screenMode) {
        screenPage(l);
    } else {
        if (level)
            l.y += drawWrapped(l.x, l.y, l.w, LEVEL_PAGES[s.category].about) + 8;
        if (level)
            levelPage(l);
        else
            worldPage(l);
    }
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
    std::string tip = level ? "Selected: " + selectionName() : std::string(s.nav ? "Choose a command" : "Click a command");
    if (s.hover >= 0) {
        for (const Widget& w : g_widgets) {
            if (sameRect(w.r, s.hoverRect))
                tip = w.tip;
        }
    }
    drawWrapped(win.x + 14, fy + 4, win.w - 28, tip, 2);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller && s.nav)
        row = {{{prompts::Pad::DPad}, {}, ""}, {{prompts::Pad::A}, {}, "choose"}, {{prompts::Pad::LB, prompts::Pad::RB}, {}, "pages"},
            {{prompts::Pad::B}, {}, "close"}};
    else if (controller)
        row = {{{prompts::Pad::A}, {}, "pick"}, {{prompts::Pad::LB, prompts::Pad::RB}, {}, "pages"}, {{prompts::Pad::Start}, {}, "close"}};
    else
        row = {{{}, {"Tab"}, "close"}, {{}, {"F1"}, "help"}};
    const int pw = prompts::promptsWidth(row, 14);
    prompts::drawPrompts(win.x + win.w - 14 - pw / 2, fy + footH - 33, row);
    if (s.nav) {
        settleFocus();
        //The highlight: a white frame (inside the page's view, for the page's widgets)
        const int k = focusIndex();
        if (k >= 0) {
            const Widget& w = g_widgets[static_cast<size_t>(k)];
            if (w.zone == ZONE_PAGE)
                SDL_SetSurfaceClipRect(screen, &g_pageView);
            const SDL_Rect r = w.r;
            const SDL_Color black {0, 0, 0, 255};
            const SDL_Color white {255, 255, 255, 255};
            fill(r.x - 4, r.y - 4, r.w + 8, 4, black);
            fill(r.x - 4, r.y + r.h, r.w + 8, 4, black);
            fill(r.x - 4, r.y, 4, r.h, black);
            fill(r.x + r.w, r.y, 4, r.h, black);
            fill(r.x - 3, r.y - 3, r.w + 6, 2, white);
            fill(r.x - 3, r.y + r.h + 1, r.w + 6, 2, white);
            fill(r.x - 3, r.y - 1, 2, r.h + 2, white);
            fill(r.x + r.w + 1, r.y - 1, 2, r.h + 2, white);
            SDL_SetSurfaceClipRect(screen, nullptr);
        }
    }
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

// Cells framed (a rectangle being drawn, the area to paste): a dashed frame in a color, and a tag
void cellFrame(int c0, int r0, int c1, int r1, SDL_Color c, const std::string& tag)
{
    const int x = std::min(c0, c1) * TILE;
    const int y = std::min(r0, r1) * TILE;
    const int w = (std::abs(c1 - c0) + 1) * TILE;
    const int h = (std::abs(r1 - r0) + 1) * TILE;
    const SDL_Color black {0, 0, 0, 255};
    fill(x - 1, y - 1, w + 2, 4, black);
    fill(x - 1, y + h - 3, w + 2, 4, black);
    fill(x - 1, y - 1, 4, h + 2, black);
    fill(x + w - 3, y - 1, 4, h + 2, black);
    //(dashes, marching)
    const int phase = static_cast<int>((SDL_GetTicks() / 80) % 8);
    for (int i = -phase; i < w; i += 8) {
        fill(x + std::max(0, i), y, std::min(4, w - std::max(0, i)), 2, c);
        fill(x + std::max(0, i), y + h - 2, std::min(4, w - std::max(0, i)), 2, c);
    }
    for (int i = -phase; i < h; i += 8) {
        fill(x, y + std::max(0, i), 2, std::min(4, h - std::max(0, i)), c);
        fill(x + w - 2, y + std::max(0, i), 2, std::min(4, h - std::max(0, i)), c);
    }
    s.overlayDirty = true;
    if (!tag.empty()) {
        const int tw = theme::font().getWidth(tag) + 8;
        const int tx = std::clamp(x, 0, SCREEN_W - tw);
        const int ty = y + h + 4 + 16 <= SCREEN_H ? y + h + 4 : std::max(0, y - 20);
        fill(tx, ty, tw, 16, black);
        fill(tx + 1, ty + 1, tw - 2, 14, c);
        fill(tx + 1, ty + 1, tw - 2, 2, theme::shade(c, 140));
        theme::font().draw(tx + 4, ty + 1, tag);
    }
}

// The rectangles being drawn, the area about to be pasted, the fill's cell
void drawCellTools()
{
    namespace sh = leveleditor::shell;
    auto cellOf = [](float px, float py, int* c, int* r) {
        *c = std::clamp(static_cast<int>(px) / TILE, 0, sh::MAP_COLS - 1);
        *r = std::clamp(static_cast<int>(py) / TILE, 0, sh::MAP_ROWS - 1);
    };
    const SDL_Color red {216, 40, 32, 255};
    for (size_t k = 0; k < s.pointers.size(); k++) {
        const Pointer& p = s.pointers[k];
        if (p.drag < 0)
            continue;
        int c = 0, r = 0;
        cellOf(p.x, p.y, &c, &r);
        const bool erase = p.drag == B || currentTool() == TOOL_ERASE;
        const std::string size = std::to_string(std::abs(c - p.dragCol) + 1) + " x " + std::to_string(std::abs(r - p.dragRow) + 1);
        cellFrame(p.dragCol, p.dragRow, c, r, s.armed == Armed::Copy ? SDL_Color {255, 255, 255, 255} : erase ? red : pointerColor(static_cast<int>(k)),
            s.armed == Armed::Copy ? "Copy " + size : erase ? "Clear " + size : size);
    }
    if (s.mouseDrag) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(s.mouseDragCol, s.mouseDragRow, c, r, {255, 255, 255, 255}, "Copy");
    }
    if (s.armed == Armed::Paste) {
        int w = 1, h = 1;
        sh::clipboard(&w, &h);
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(c, r, std::min(c + w - 1, sh::MAP_COLS - 1), std::min(r + h - 1, sh::MAP_ROWS - 1), {255, 255, 255, 255}, "Paste here (A)");
    } else if (s.armed == Armed::Fill) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(c, r, c, r, {255, 255, 255, 255}, "Fill from here (A)");
    }
}

// The tools menu, beside the pointer that opened it
void drawTools()
{
    const SDL_Rect r = toolsRect();
    theme::panel(r.x, r.y, r.w, r.h);
    theme::font().draw(r.x + 14, r.y + 14, "Tools");
    static const char* LABELS[] = {"Undo", "Redo", "Copy an area", "Paste", "Fill"};
    static const Art* ARTS[] = {nullptr, nullptr, &ART_MOVE, &ART_BRUSH, &ART_SPRING};
    static const Art ART_UNDO = {"..#.....", ".##.....", "#######.", ".##....#", "..#....#", ".......#", "..#####.", "........"};
    static const Art ART_REDO = {".....#..", ".....##.", ".#######", "#....##.", "#....#..", "#.......", ".#####..", "........"};
    static const Art ART_FILL = {"...#....", "..#w#...", ".#www#..", "#wwwww#.", ".#www#c.", "..#w#.cc", "...#..cc", "......c."};
    for (int k = 0; k < toolsCount(); k++) {
        const SDL_Rect ir = toolsItemRect(k);
        const bool sel = s.toolsIndex == k;
        const bool usable = toolUsable(k);
        theme::block(ir.x, ir.y, ir.w, ir.h, sel ? (usable ? theme::Tone::Gold : theme::Tone::Gray) : theme::Tone::Dark);
        const Art* art = k == T_UNDO ? &ART_UNDO : k == T_REDO ? &ART_REDO : k == T_FILL ? &ART_FILL : ARTS[k];
        drawArt(ir.x + 6, ir.y + (ir.h - 16) / 2, *art);
        std::string label = LABELS[k];
        if (k == T_UNDO && !s.undos.empty())
            label += " (" + std::to_string(s.undos.size()) + ")";
        if (k == T_REDO && !s.redos.empty())
            label += " (" + std::to_string(s.redos.size()) + ")";
        theme::font().draw(ir.x + 30, ir.y + (ir.h - theme::font().getHeight()) / 2, label);
        if (!usable)
            fill(ir.x + 28, ir.y + ir.h / 2, theme::font().getWidth(label) + 4, 2, {0, 0, 0, 255});
    }
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller)
        row = {{{prompts::Pad::A}, {}, "use"}, {{prompts::Pad::B}, {}, "close"}};
    else
        row = {{{}, {"Ctrl+Z"}, "undo"}, {{}, {"Ctrl+Y"}, "redo"}};
    prompts::drawPrompts(r.x + r.w / 2, r.y + r.h - 26, row, 10);
}

// The whole map, while a left stick is held: the level's (the screen, without the editor's
// panels) or the world, small, with where everyone's glove is
void drawPreview()
{
    const std::vector<coedit::Cursor> remote = coedit::remoteCursors();
    if (s.editor == Editor::World && s.worldPicture) {
        //(the world scaled to fit, over a dark screen)
        fill(0, 0, SCREEN_W, SCREEN_H, {16, 16, 24, 255});
        const int pw = s.worldPicture->w;
        const int ph = s.worldPicture->h;
        const float scale = std::min({1.0f, (SCREEN_W - 40.0f) / static_cast<float>(pw), (SCREEN_H - 90.0f) / static_cast<float>(ph)});
        const int w = static_cast<int>(static_cast<float>(pw) * scale);
        const int h = static_cast<int>(static_cast<float>(ph) * scale);
        const int x = (SCREEN_W - w) / 2;
        const int y = 50 + (SCREEN_H - 90 - h) / 2;
        theme::panel(x - 8, y - 8, w + 16, h + 16);
        const SDL_Rect dst {x, y, w, h};
        SDL_BlitSurfaceScaled(s.worldPicture, nullptr, screen, &dst, SDL_SCALEMODE_LINEAR);
        int ox = 0, oy = 0;
        worldeditor::shell::viewOrigin(&ox, &oy);
        //(the part on the screen now)
        const int vx = x + static_cast<int>(static_cast<float>(ox) * scale);
        const int vy = y + static_cast<int>(static_cast<float>(oy) * scale);
        const int vw = static_cast<int>(SCREEN_W * scale);
        const int vh = static_cast<int>(SCREEN_H * scale);
        const SDL_Color white {255, 255, 255, 255};
        fill(vx, vy, vw, 2, white);
        fill(vx, vy + vh - 2, vw, 2, white);
        fill(vx, vy, 2, vh, white);
        fill(vx + vw - 2, vy, 2, vh, white);
        auto mark = [&](float px, float py, SDL_Color c, const std::string& name, int id) {
            drawArrow(static_cast<float>(x) + (px + static_cast<float>(ox)) * scale, static_cast<float>(y) + (py + static_cast<float>(oy)) * scale, c, name, id);
        };
        for (const coedit::Cursor& c : remote)
            mark(c.x, c.y, c.color, c.name, 1000 + static_cast<int>(&c - remote.data()));
        for (size_t k = 0; k < s.pointers.size(); k++) {
            if (s.pointers[k].shown)
                mark(s.pointers[k].x, s.pointers[k].y, pointerColor(static_cast<int>(k)), "P" + std::to_string(k + 1), static_cast<int>(k));
        }
    } else {
        //(the level: the map is the screen; everyone's reticle and name on it)
        for (const coedit::Cursor& c : remote)
            reticle(c.x, c.y, c.color, c.name);
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                reticle(p.x, p.y, pointerColor(static_cast<int>(k)), coedit::active() && k == 0 ? "You" : "P" + std::to_string(k + 1));
        }
    }
    theme::announce(SCREEN_W / 2, 10, s.editor == Editor::World ? "The whole world" : "The whole map");
}

// The tileset screen: the tile under the cursor, framed, and which tileset it is
void drawTilePicker(const leveleditor::shell::TilePicker& picker)
{
    for (size_t k = 0; k < s.pointers.size(); k++) {
        const Pointer& p = s.pointers[k];
        if (!p.shown)
            continue;
        const int c = static_cast<int>(p.x) / TILE;
        const int r = static_cast<int>(p.y) / TILE;
        if (c + picker.viewCol >= picker.cols || r + picker.viewRow >= picker.rows)
            continue;
        const int x = c * TILE;
        const int y = r * TILE;
        const SDL_Color black {0, 0, 0, 255};
        const SDL_Color gold {248, 200, 24, 255};
        fill(x - 3, y - 3, TILE + 6, 3, black);
        fill(x - 3, y + TILE, TILE + 6, 3, black);
        fill(x - 3, y, 3, TILE, black);
        fill(x + TILE, y, 3, TILE, black);
        fill(x - 2, y - 2, TILE + 4, 3, gold);
        fill(x - 2, y + TILE - 1, TILE + 4, 3, gold);
        fill(x - 2, y, 3, TILE, gold);
        fill(x + TILE - 1, y, 3, TILE, gold);
    }
}

// The level editor's chip when the window is closed: what is selected, the layer, the cell; with a
// controller, its buttons on the map below
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
    //(the buttons; with a tool armed, what they do with it)
    std::vector<prompts::Prompt> buttons;
    if (s.armed == Armed::Copy)
        buttons = {{{prompts::Pad::A}, {}, "hold and drag over the cells to copy"}, {{prompts::Pad::B}, {}, "stop"}};
    else if (s.armed == Armed::Paste)
        buttons = {{{prompts::Pad::A}, {}, "paste here"}, {{prompts::Pad::B}, {}, "stop pasting"}};
    else if (s.armed == Armed::Fill)
        buttons = {{{prompts::Pad::A}, {}, "fill from this cell"}, {{prompts::Pad::B}, {}, "stop"}};
    else
        buttons = {{{prompts::Pad::A}, {}, "place"}, {{prompts::Pad::B}, {}, "remove"}, {{prompts::Pad::X}, {}, "take"},
            {{prompts::Pad::Y}, {}, "layer"}, {{prompts::Pad::RStick}, {}, "tools"}, {{prompts::Pad::LStick}, {}, "view"}};
    const int pw = prompts::promptsWidth(row1, 14);
    const int bw = controller && s.hints ? prompts::promptsWidth(buttons, 10) : 0;
    //(the last things picked, with LB and RB at their ends)
    const bool recents = controller && s.recents.size() >= 2;
    const int recentW = recents ? static_cast<int>(s.recents.size()) * 26 + 2 * prompts::padWidth(prompts::Pad::LB) + 12 : 0;
    const int w = std::max({46 + std::max(theme::font().getWidth(name), theme::font().getWidth(info)) + 16 + pw + 10, bw + 20, recentW + 20});
    const int h = 46 + (recents ? 28 : 0) + (bw > 0 ? 24 : 0);
    //(bottom left; at the top while a pointer is down there)
    bool low = s.cy > SCREEN_H - h - 44 && s.cx < w + 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 44 && p.x < w + 30);
    const int x = 6;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::inset(x + 6, y + 6, 34, 34);
    drawSelection(x + 7, y + 7);
    theme::font().draw(x + 46, y + 7, name);
    theme::font().draw(x + 46, y + 24, info);
    prompts::drawPrompts(x + w - pw / 2 - 8, y + 14, row1);
    int by = y + 47;
    if (recents) {
        int rx = x + 10;
        rx += prompts::drawPad(rx, by + 4, prompts::Pad::LB) + 4;
        const leveleditor::shell::Brush now = sh::brush();
        for (const leveleditor::shell::Brush& b : s.recents) {
            if (b == now) {
                fill(rx - 2, by - 2, 28, 28, {0, 0, 0, 255});
                fill(rx - 1, by - 1, 26, 26, {248, 200, 24, 255});
            }
            drawScaled(rx, by, 24, [&b](int ix, int iy) { drawBrush(b, ix, iy); });
            rx += 26;
        }
        prompts::drawPad(rx + 2, by + 4, prompts::Pad::RB);
        by += 28;
    }
    if (bw > 0)
        prompts::drawPrompts(x + 10 + bw / 2, by, buttons, 10);
}

// The world editor's chip: the mode, the world, and (with a controller) its buttons
void drawWorldChip()
{
    namespace sh = worldeditor::shell;
    const std::string name = sh::modeName() + (sh::autoPaint() ? "  (auto paint)" : "");
    const std::string info = sh::worldName() + "   " + std::to_string(static_cast<int>(s.cx) / TILE) + "," +
                             std::to_string(static_cast<int>(s.cy) / TILE);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const std::vector<prompts::Prompt> row1 = controller ? std::vector<prompts::Prompt> {{{prompts::Pad::Start}, {}, "editor"}}
                                                         : std::vector<prompts::Prompt> {{{}, {"Tab"}, "editor"}};
    const std::vector<prompts::Prompt> buttons = {{{prompts::Pad::A}, {}, "place"}, {{prompts::Pad::B}, {}, "remove"},
        {{prompts::Pad::LT, prompts::Pad::RT}, {}, "shift/ctrl"}, {{prompts::Pad::RStick}, {}, "undo"}, {{prompts::Pad::LStick}, {}, "whole world"}};
    const int pw = prompts::promptsWidth(row1, 14);
    const int bw = controller && s.hints ? prompts::promptsWidth(buttons, 10) : 0;
    const int w = std::max(46 + std::max(theme::font().getWidth(name), theme::font().getWidth(info)) + 16 + pw + 10, bw + 20);
    const int h = bw > 0 ? 70 : 46;
    bool low = s.cy > SCREEN_H - h - 44 && s.cx < w + 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 44 && p.x < w + 30);
    const int x = 6;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::inset(x + 6, y + 6, 34, 34);
    theme::icon(x + 15, y + 15, theme::Icon::Star);
    theme::font().draw(x + 46, y + 7, name);
    theme::font().draw(x + 46, y + 24, info);
    prompts::drawPrompts(x + w - pw / 2 - 8, y + 14, row1);
    if (bw > 0)
        prompts::drawPrompts(x + 10 + bw / 2, y + 47, buttons, 10);
}


void drawKeyboard()
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const int px = OSK_PANEL_X;
    const int py = OSK_PANEL_Y;
    //The panel (over the dialog: it shows the dialog's title and text itself)
    menupanel::box(px - 4, py - 4, OSK_PANEL_W + 8, OSK_PANEL_H + 8);
    theme::bigFont().drawCentered(SCREEN_W / 2, py + 10, s.oskTitle ? s.oskTitle : "Type");
    if (s.oskPrompt)
        rm->menu_font_small.drawCentered(SCREEN_W / 2, py + 40, s.oskPrompt);
    //The text so far, in a field, with a blinking cursor
    const SDL_Rect field {px + 20, py + 60, OSK_PANEL_W - 40, 36};
    theme::inset(field.x, field.y, field.w, field.h);
    const std::string text = s.oskTyped && s.oskText ? s.oskText : std::string();
    const int tw = theme::bigFont().getWidth(text);
    const int tx = field.x + 12 + std::min(0, field.w - 36 - tw);
    SDL_Rect clip {field.x + 4, field.y, field.w - 8, field.h};
    SDL_SetSurfaceClipRect(screen, &clip);
    theme::bigFont().draw(tx, field.y + 6, text);
    if ((SDL_GetTicks() / 400) % 2 == 0)
        fill(tx + tw + 2, field.y + 7, 3, field.h - 14, {255, 255, 255, 255});
    SDL_SetSurfaceClipRect(screen, nullptr);
    s.overlayDirty = true;    //(the cursor blinks)

    //The keys: the pipes (light green selected; Done green, Shift and Caps Lock green while on), the blocks
    //(gray; the selected one gold, a ? block's, in a white frame); a key pressed is drawn down for a moment
    const std::vector<OskKey>& keys = oskKeys();
    const bool justPressed = s.oskPressedKey >= 0 && SDL_GetTicks() - s.oskPressedMs < 140;
    //(drawn here: the game's menu graphics are put away while an editor runs)
    struct PipeColors {
        SDL_Color light, mid, dark, darker;
    };
    auto pipe = [](const SDL_Rect& r, const PipeColors& c) {
        const SDL_Color black {0, 0, 0, 255};
        //The body, between its lips: a light band on top, shade underneath
        const int bx = r.x + 8;
        const int bw = r.w - 16;
        fill(bx, r.y + 3, bw, 26, black);
        fill(bx, r.y + 4, bw, 24, c.mid);
        fill(bx, r.y + 6, bw, 4, c.light);
        fill(bx, r.y + 20, bw, 4, c.dark);
        fill(bx, r.y + 24, bw, 4, c.darker);
        //The lips at both ends: a little taller and wider
        for (const int lx : {r.x, r.x + r.w - 12}) {
            fill(lx, r.y, 12, 32, black);
            fill(lx + 1, r.y + 1, 10, 30, c.mid);
            fill(lx + 1, r.y + 1, 3, 30, c.light);
            fill(lx + 8, r.y + 1, 3, 30, c.dark);
            fill(lx + 1, r.y + 27, 10, 4, c.darker);
        }
    };
    static const PipeColors GRAY {{176, 192, 192, 255}, {144, 160, 160, 255}, {112, 128, 128, 255}, {53, 64, 64, 255}};
    static const PipeColors LIGHT_GRAY {{216, 228, 228, 255}, {184, 200, 200, 255}, {144, 160, 160, 255}, {80, 96, 96, 255}};
    static const PipeColors GREEN {{107, 203, 0, 255}, {57, 154, 0, 255}, {40, 128, 0, 255}, {9, 60, 0, 255}};
    static const PipeColors LIGHT_GREEN {{180, 255, 64, 255}, {148, 243, 0, 255}, {107, 203, 0, 255}, {16, 113, 0, 255}};
    for (size_t i = 0; i < keys.size(); i++) {
        const OskKey& k = keys[i];
        const bool selected = (controller && s.oskKey == static_cast<int>(i)) || (!controller && s.hoverKey == static_cast<int>(i));
        const bool down = justPressed && s.oskPressedKey == static_cast<int>(i);
        SDL_Rect r = k.r;
        if (down)
            r.y += 2;
        if (k.kind == OSK_CHAR) {
            theme::block(r.x, r.y, r.w, r.h, selected ? theme::Tone::Gold : down ? theme::Tone::Green : theme::Tone::Gray);
            char ch = k.ch;
            if ((s.oskShift != s.oskCaps) && ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 32);
            theme::bigFont().drawCentered(r.x + r.w / 2, r.y + (r.h - theme::bigFont().getHeight()) / 2 + 1, std::string(1, ch));
            if (selected) {
                const SDL_Color white {255, 255, 255, 255};
                fill(r.x - 3, r.y - 3, r.w + 6, 2, white);
                fill(r.x - 3, r.y + r.h + 1, r.w + 6, 2, white);
                fill(r.x - 3, r.y - 1, 2, r.h + 2, white);
                fill(r.x + r.w + 1, r.y - 1, 2, r.h + 2, white);
            }
            continue;
        }
        const bool on = (k.kind == OSK_SHIFT && s.oskShift) || (k.kind == OSK_CAPS && s.oskCaps);
        pipe(r, selected ? LIGHT_GREEN : (k.kind == OSK_ENTER || on) ? GREEN : down ? LIGHT_GRAY : GRAY);
        static const char* labels[] = {"", "SHIFT", "CAPS LOCK", "CLEAR", "SPACE", "BACKSPACE", "DONE", "CANCEL"};
        //(between the lips: the small font if the large one doesn't fit)
        const bool fits = rm->menu_font_large.getWidth(labels[k.kind]) <= r.w - 30;
        gfxFont& font = fits ? rm->menu_font_large : rm->menu_font_small;
        font.drawCentered(r.x + r.w / 2, r.y + (32 - font.getHeight()) / 2, labels[k.kind]);
    }
    if (justPressed)
        s.overlayDirty = true;    //(it comes up again)

    //The buttons, for the device in use
    std::vector<prompts::Prompt> row;
    if (controller)
        row = {{{prompts::Pad::A}, {}, "type"}, {{prompts::Pad::B}, {}, "delete"}, {{prompts::Pad::X}, {}, "shift"},
            {{prompts::Pad::Y}, {}, "space"}, {{prompts::Pad::Start}, {}, "done"}, {{prompts::Pad::Back}, {}, "cancel"}};
    else
        row = {{{}, {"Enter"}, "done"}, {{}, {"Esc"}, "cancel"}};
    prompts::drawPrompts(SCREEN_W / 2, py + OSK_PANEL_H - 26, row, 14);
    if (!controller)
        rm->menu_font_small.drawCentered(SCREEN_W / 2, py + OSK_PANEL_H - 44, "Type, or click the keys");
}

// The level editor's other screens: a strip with the screen's name and what the buttons do
void drawScreenStrip()
{
    const leveleditor::shell::Screen& sc = s.screen;
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller) {
        if (!sc.a.empty())
            row.push_back({{prompts::Pad::A}, {}, sc.a});
        if (!sc.b.empty())
            row.push_back({{prompts::Pad::B}, {}, sc.b});
        if (!sc.shoulders.empty())
            row.push_back({{prompts::Pad::LB, prompts::Pad::RB}, {}, sc.shoulders});
        row.push_back({{prompts::Pad::Start}, {}, "more"});
        row.push_back({{prompts::Pad::Back}, {}, "done"});
    } else {
        row.push_back({{}, {"Tab"}, "commands"});
        row.push_back({{}, {"Esc"}, "done"});
    }
    //(a long row: closer together)
    const int gap = prompts::promptsWidth(row, 12) > SCREEN_W - 40 ? 6 : 12;
    const int pw = prompts::promptsWidth(row, gap);
    const std::string hold = controller ? sc.holds : std::string();
    const int w = std::min(SCREEN_W - 12, std::max({pw + 24, theme::font().getWidth(sc.title) + 24, theme::font().getWidth(hold) + 24}));
    const int h = hold.empty() ? 48 : 66;
    //(at the bottom; at the top while a cursor is down there)
    bool low = s.cy > SCREEN_H - h - 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 30);
    const int x = (SCREEN_W - w) / 2;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::font().drawCentered(SCREEN_W / 2, y + 6, sc.title);
    prompts::drawPrompts(SCREEN_W / 2, y + 26, row, gap);
    if (!hold.empty())
        theme::font().drawCentered(SCREEN_W / 2, y + 46, hold);
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
    if (s.overlay != Overlay::None || coedit::active() || !coedit::notice().empty() || levelMap() || s.mouseUsed || !s.screen.title.empty() ||
        s.preview || toastShown())
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
    //The whole map: nothing else
    if (s.preview && !keyboard) {
        drawPreview();
        if (s.editor == Editor::Level) {
            for (const coedit::Cursor& c : remote)
                drawArrow(c.x, c.y, c.color, std::string(), 1000 + static_cast<int>(&c - remote.data()));
            for (size_t k = 0; k < s.pointers.size(); k++) {
                if (s.pointers[k].shown)
                    drawArrow(s.pointers[k].x, s.pointers[k].y, pointerColor(static_cast<int>(k)), std::string(), static_cast<int>(k));
            }
        }
        return;
    }
    //The tileset screen: the tile under each cursor
    leveleditor::shell::TilePicker picker;
    if (s.editor == Editor::Level && !keyboard && leveleditor::shell::tilePicker(&picker))
        drawTilePicker(picker);
    if (levelMap() && !keyboard)
        drawCellTools();
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
        else if (s.editor == Editor::World && worldeditor::shell::inMapView())
            drawWorldChip();
        else if (!s.screen.title.empty())
            drawScreenStrip();
        else
            drawHints();
    }
    drawSession();
    if (s.overlay == Overlay::Window)
        drawWindow();
    else if (keyboard)
        drawKeyboard();
    else if (s.overlay == Overlay::Tools)
        drawTools();
    if (toastShown() && !keyboard)
        theme::announce(SCREEN_W / 2, 36, s.toast);
    //The cursors on top (the controllers' drawn pointers; the others' online)
    for (const coedit::Cursor& c : remote)
        drawArrow(c.x, c.y, c.color, mapView ? std::string() : c.name, 1000 + static_cast<int>(&c - remote.data()));
    if (!keyboard) {
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                drawArrow(p.x, p.y, pointerColor(static_cast<int>(k)), std::string(), static_cast<int>(k), p.pad.buttons[A] || p.pad.buttons[B]);
        }
    }
    //The mouse: a glove too, on top
    if (s.mouseUsed && s.mouseX >= 0)
        drawArrow(s.mouseX, s.mouseY, pointerColor(0), std::string(), -1, (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) != 0);
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
    p.buttons = {t.a, t.b, t.x, t.y, t.lb, t.rb, t.back, t.start, t.up, t.down, t.left, t.right, t.lt, t.rt, t.ls, t.rs};
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
        coedit::attach(makeDocument());
    undoRebase();
    if (!coedit::notice().empty())
        s.overlayDirty = true;

    if (s.shiftFrames > 0)
        s.shiftFrames--;
    if (s.ctrlFrames > 0)
        s.ctrlFrames--;

    //The level editor's other screen showing now (what the buttons do there)
    {
        leveleditor::shell::Screen screen;
        if (s.editor == Editor::Level && !leveleditor::shell::inMapView())
            screen = leveleditor::shell::currentScreen();
        if (screen.title != s.screen.title || screen.a != s.screen.a)
            s.overlayDirty = true;
        s.screen = std::move(screen);
    }

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
                //(pointing into the window: the pointer clicks there, no highlight)
                if (k == s.overlayOwner && inWindow(p.x, p.y))
                    s.nav = false;
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
        for (size_t k = 0; k < s.pointers.size(); k++) {
            if (s.overlay == Overlay::Tools && static_cast<int>(k) == s.overlayOwner)
                updateTools(s.pointers[k]);
            else
                updatePointer(static_cast<int>(k));
        }
    }
    updateKeys();
    trackRecent();
    //(the tools' menu, or an armed tool, where they can't be used any more)
    if (!mapShown()) {
        if (s.overlay == Overlay::Tools)
            closeTools();
        s.armed = Armed::None;
    }

    //The whole map, while a left stick is held down
    bool preview = false;
    for (const Pointer& p : s.pointers)
        preview = preview || (p.pad.buttons[LS] && s.overlay != Overlay::Keyboard && mapShown());
    if (preview != s.preview) {
        s.preview = preview;
        if (s.worldPicture) {
            SDL_DestroySurface(s.worldPicture);
            s.worldPicture = nullptr;
        }
        if (preview && s.editor == Editor::World)
            s.worldPicture = worldeditor::shell::drawWhole();
        s.overlayDirty = true;
    }
    if (toastShown())
        s.overlayDirty = true;

    bool shown = false;
    for (const Pointer& p : s.pointers)
        shown = shown || p.shown;
    (void)shown;
    if (SDL_CursorVisible())
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
    SDL_HideCursor();
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
    if (s.worldPicture)
        SDL_DestroySurface(s.worldPicture);
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
                s.mouseUsed = true;
                s.mouseX = mx;
                s.mouseY = my;
                if (motion) {
                    s.nav = false;
                    hoverAt(mx, my);
                }
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
        //Ctrl+Z, Ctrl+Y (Ctrl+Shift+Z): undo, redo
        if (event->type == SDL_EVENT_KEY_DOWN && s.textEntries == 0 && (event->key.mod & SDL_KMOD_CTRL) &&
            (event->key.key == SDLK_Z || event->key.key == SDLK_Y)) {
            undoApply(event->key.key == SDLK_Y || (event->key.mod & SDL_KMOD_SHIFT));
            continue;
        }
        //The tools: the middle button opens them; with them open, the mouse picks one
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_MIDDLE && mapShown() &&
            (s.overlay == Overlay::None || s.overlay == Overlay::Tools)) {
            if (s.overlay == Overlay::Tools) {
                closeTools();
            } else {
                s.cx = event->button.x;
                s.cy = event->button.y;
                openTools(-1);
            }
            continue;
        }
        if (s.overlay == Overlay::Tools && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_MOTION)) {
            const float mx = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.x : event->button.x;
            const float my = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.y : event->button.y;
            s.cx = mx;
            s.cy = my;
            s.owner = -1;
            s.mouseUsed = true;
            s.mouseX = mx;
            s.mouseY = my;
            int at = -1;
            for (int k = 0; k < toolsCount(); k++) {
                const SDL_Rect r = toolsItemRect(k);
                if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
                    at = k;
            }
            if (at >= 0)
                s.toolsIndex = at;
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (at >= 0 && event->button.button == SDL_BUTTON_LEFT)
                    chooseTool(at);
                else
                    closeTools();
            }
            s.overlayDirty = true;
            continue;
        }
        if (s.overlay == Overlay::Tools && event->type == SDL_EVENT_MOUSE_BUTTON_UP)
            continue;
        //An armed tool: the mouse uses it on the map (the right button puts it away)
        if (levelMap() && s.armed != Armed::None && s.overlay != Overlay::Keyboard &&
            (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) && !inWindow(event->button.x, event->button.y)) {
            namespace sh = leveleditor::shell;
            const int col = std::clamp(static_cast<int>(event->button.x) / TILE, 0, sh::MAP_COLS - 1);
            const int row = std::clamp(static_cast<int>(event->button.y) / TILE, 0, sh::MAP_ROWS - 1);
            const bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (down && event->button.button == SDL_BUTTON_RIGHT) {
                s.armed = Armed::None;
                s.mouseDrag = false;
                toast("Stopped");
            } else if (event->button.button == SDL_BUTTON_LEFT) {
                if (s.armed == Armed::Paste && down) {
                    sh::paste(col, row);
                } else if (s.armed == Armed::Fill && down) {
                    sh::floodFill(col, row);
                    s.armed = Armed::None;
                } else if (s.armed == Armed::Copy && down) {
                    s.mouseDrag = true;
                    s.mouseDragCol = col;
                    s.mouseDragRow = row;
                } else if (s.armed == Armed::Copy && s.mouseDrag) {
                    sh::copyArea(s.mouseDragCol, s.mouseDragRow, col, row);
                    s.mouseDrag = false;
                    s.armed = Armed::None;
                    int w = 0, h = 0;
                    sh::clipboard(&w, &h);
                    toast("Copied " + std::to_string(w) + " x " + std::to_string(h) + ": Paste is in the tools");
                }
            }
            s.overlayDirty = true;
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
        //(an armed tool: the mouse held down isn't painting)
        if (levelMap() && s.armed != Armed::None && event->type == SDL_EVENT_MOUSE_MOTION)
            event->motion.state = 0;
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
                    oskPress(key);
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
                s.mouseUsed = true;
                s.mouseX = s.cx;
                s.mouseY = s.cy;
                s.overlayDirty = true;
                return true;
            case SDL_EVENT_KEY_DOWN:
                if (coedit::blockedKey(event->key.key, event->key.mod))
                    continue;
                if (s.textEntries > 0) {
                    s.oskTyped = true;    //(the dialog's text: from now on what is typed)
                    const SDL_Keycode k = event->key.key;
                    if (k == SDLK_BACKSPACE)
                        oskShowPressed(OSK_BACKSPACE);
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                        oskShowPressed(OSK_ENTER);
                    else if (k == SDLK_SPACE)
                        oskShowPressed(OSK_SPACE);
                    else if (k == SDLK_LSHIFT || k == SDLK_RSHIFT)
                        oskShowPressed(OSK_SHIFT);
                    else if (k < 128)
                        oskShowPressed(OSK_CHAR, static_cast<char>(k));
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
    //What changed this frame: an undo
    undoTrack();
    //Editing together: this frame's changes go out
    coedit::afterFrame(s.cx, s.cy, true);
    present();
}

TextEntry::TextEntry(const char* title, const char* prompt, const char* text)
{
    s.textEntries++;
    s.oskTitle = title;
    s.oskPrompt = prompt;
    s.oskText = text;
    s.oskTyped = false;
    s.oskShift = false;
    s.oskCaps = false;
    //(the highlight starts on the a, the dashboard's way)
    const std::vector<OskKey>& keys = oskKeys();
    for (size_t i = 0; i < keys.size(); i++) {
        if (keys[i].kind == OSK_CHAR && keys[i].ch == 'a')
            s.oskKey = static_cast<int>(i);
    }
}

TextEntry::~TextEntry()
{
    s.textEntries = std::max(0, s.textEntries - 1);
    s.oskTitle = s.oskPrompt = s.oskText = nullptr;
}

} // namespace editors::input

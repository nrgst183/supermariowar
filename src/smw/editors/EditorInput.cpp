#include "EditorInputInternal.h"

#include "CoEdit.h"
#include "LevelShell.h"
#include "WorldShell.h"
#include "ui/InputPrompts.h"
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

namespace editors::input::detail {

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
void runKey(SDL_Keycode key, bool shift, bool ctrl, bool keepOpen)
{
    if (!keepOpen)
        closeWindow();
    tapKey(key, shift, ctrl);
}

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

// The editor shows its map (the level's or the world's), not one of its other screens
bool mapShown()
{
    return levelMap() || (s.editor == Editor::World && worldeditor::shell::inMapView());
}

int toolsCount()
{
    return levelMap() ? (leveleditor::shell::mode() == 3 ? T_ITEM_COUNT : T_BASIC_COUNT) : 2;
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
bool toolUsable(int item, std::string* why)
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

// The controller was used last (else the mouse and the keyboard): what the tips say
bool padUsed()
{
    return prompts::lastDevice() == prompts::Device::Controller;
}

// An action that throws work away: true when it was chosen a moment ago too (else the warning, and
// choosing it again within 3 seconds does it)
bool confirmed(const std::string& id, const std::string& warning)
{
    const Uint64 now = SDL_GetTicks();
    if (s.confirmId == id && now - s.confirmMs < 3000) {
        s.confirmId.clear();
        return true;
    }
    s.confirmId = id;
    s.confirmMs = now;
    toast(warning + (padUsed() ? ". Press A again to confirm" : ". Click again to confirm"));
    return false;
}

// "? block contents": the next ? block chosen on the map opens its screen
void armBlockContents()
{
    closeWindow();
    s.armed = Armed::Properties;
    toast(padUsed() ? "Point at a ? block and press A (B to stop)" : "Click a ? block (right-click to stop)");
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
        case T_COPY: s.armed = Armed::Copy; toast(padUsed() ? "Copy: hold A and drag over an area" : "Copy: drag over an area"); break;
        case T_PASTE:
            s.armed = Armed::Paste;
            toast(padUsed() ? "Paste: press A to place it, B to stop" : "Paste: click to place it, right-click to stop");
            break;
        case T_FILL: s.armed = Armed::Fill; toast(padUsed() ? "Fill: press A to fill the connected area" : "Fill: click to fill the connected area"); break;
        //(move mode: its keys)
        case T_MOVE_COPY: closeTools(); tapKey(SDLK_C); return;
        case T_MOVE_DELETE: closeTools(); tapKey(SDLK_DELETE); return;
        case T_MOVE_REPLACE: tapKey(SDLK_M); s.overlayDirty = true; return;    //(the menu stays: the label changes)
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
    //The stick: a little push moves a pixel or so (to aim at a cell), a full one crosses the screen in
    //about a second (the speed grows with the square of the push)
    if (p.pad.lx != 0 || p.pad.ly != 0) {
        const float push = std::min(1.0f, std::hypot(p.pad.lx, p.pad.ly));
        const float speed = (0.8f + 12.0f * push * push) / push;
        dx += p.pad.lx * speed;
        dy += p.pad.ly * speed;
    }
    //(on the level editor's map, the right stick moves it slowly: a pixel or two at a time)
    if (levelMap() && s.overlay == Overlay::None && (p.pad.rx != 0 || p.pad.ry != 0)) {
        dx += p.pad.rx * 1.5f;
        dy += p.pad.ry * 1.5f;
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
    } else if ((picking || (levelMap() && s.overlay == Overlay::None && !inWindow(p.x, p.y))) && (p.last.lx != 0 || p.last.ly != 0) &&
               p.pad.lx == 0 && p.pad.ly == 0) {
        //(the stick let go, on the tileset or the map: onto the middle of its cell, so it is clear which one it is)
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

    //The exit prompt: A answers (Yes or No under the pointer, else the one chosen), B and Back stay,
    //left and right choose
    if (s.editor == Editor::Level && leveleditor::shell::exitPrompt()) {
        if (left)
            tapKey(SDLK_LEFT);
        if (right)
            tapKey(SDLK_RIGHT);
        if (p.pressed(A)) {
            //(the boxes, EditorMapEdit.cpp)
            const bool onYes = p.y >= 250 && p.y < 282 && p.x >= 250 && p.x < 314;
            const bool onNo = p.y >= 250 && p.y < 282 && p.x >= 326 && p.x < 390;
            if (onYes || onNo)
                tapKey(onYes ? SDLK_LEFT : SDLK_RIGHT);
            tapKey(SDLK_RETURN);
        }
        if (p.pressed(B) || p.pressed(BACK))
            tapKey(SDLK_ESCAPE);
        return;
    }
    //The tileset screen: B goes back to the map (its right button would change the tiles' types)
    if (picking && p.pressed(B)) {
        tapKey(SDLK_ESCAPE);
        p.last.buttons[B] = p.pad.buttons[B];
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
        //(LT held: no rectangle, A and B paint and erase freely as the pointer moves)
        const bool rectangles = onMap && (s.armed == Armed::Copy || (s.armed == Armed::None && sh::paintsCells() && tool != TOOL_PICK && !p.pad.buttons[LT]));
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
        } else if (onMap && s.armed == Armed::Properties && p.pressed(A)) {
            //(the ? block under the reticle: its screen, with K at the mouse)
            if (sh::hasProperties(col, row)) {
                moveMouseTo(index);
                tapKey(SDLK_K);
                s.armed = Armed::None;
            } else {
                toast("That isn't a ? block. Choose one, or press B to stop");
            }
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
                    toast("Copied " + std::to_string(w) + " x " + std::to_string(h) + ". Paste it from the tools");
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
        if (p.drag < 0 && !rectangles && s.armed == Armed::None && !picking) {
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
    //RT on the level editor's map: Ctrl pressed and let go too (move mode's freehand selection wants
    //the key itself, not only its being held)
    if (levelMap() && s.overlay == Overlay::None) {
        if (p.pressed(RT))
            pushKey(SDLK_LCTRL, true);
        else if (p.released(RT))
            pushKey(SDLK_LCTRL, false);
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

} // namespace editors::input::detail

namespace editors::input {

using namespace detail;

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
        //Ctrl+C, Ctrl+V: copy an area, paste it (the tools)
        if (event->type == SDL_EVENT_KEY_DOWN && s.textEntries == 0 && (event->key.mod & SDL_KMOD_CTRL) && levelMap() &&
            s.overlay == Overlay::None && (event->key.key == SDLK_C || event->key.key == SDLK_V)) {
            chooseTool(event->key.key == SDLK_C ? T_COPY : T_PASTE);
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
        //The map with the mouse: the wheel steps through what was used lately (with Ctrl: the layers),
        //Alt and a click picks up the cell, Shift and a drag fills a rectangle (the right button: clears it)
        if (levelMap() && s.overlay == Overlay::None && s.armed == Armed::None) {
            namespace sh = leveleditor::shell;
            if (event->type == SDL_EVENT_MOUSE_WHEEL && event->wheel.y != 0 && !inWindow(s.cx, s.cy)) {
                const int dir = event->wheel.y > 0 ? -1 : 1;
                if (SDL_GetModState() & SDL_KMOD_CTRL)
                    sh::setLayer((sh::layer() + 4 + dir) % 4);    //(down: the next layer)
                else
                    recentStep(dir);
                s.overlayDirty = true;
                continue;
            }
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT) &&
                !inWindow(event->button.x, event->button.y)) {
                const SDL_Keymod mod = SDL_GetModState();
                const int col = std::clamp(static_cast<int>(event->button.x) / TILE, 0, sh::MAP_COLS - 1);
                const int row = std::clamp(static_cast<int>(event->button.y) / TILE, 0, sh::MAP_ROWS - 1);
                if ((mod & SDL_KMOD_ALT) && event->button.button == SDL_BUTTON_LEFT) {
                    sh::pick(col, row);
                    setTool(TOOL_PAINT);
                    s.pickRelease = true;
                    s.overlayDirty = true;
                    continue;
                }
                if ((mod & SDL_KMOD_SHIFT) && sh::paintsCells() && currentTool() != TOOL_PICK) {
                    s.mouseRect = true;
                    s.mouseRectErase = event->button.button == SDL_BUTTON_RIGHT || currentTool() == TOOL_ERASE;
                    s.mouseDragCol = col;
                    s.mouseDragRow = row;
                    s.overlayDirty = true;
                    continue;
                }
            }
        }
        if (s.mouseRect) {
            if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                namespace sh = leveleditor::shell;
                const int col = std::clamp(static_cast<int>(event->button.x) / TILE, 0, sh::MAP_COLS - 1);
                const int row = std::clamp(static_cast<int>(event->button.y) / TILE, 0, sh::MAP_ROWS - 1);
                sh::fillRect(s.mouseDragCol, s.mouseDragRow, col, row, s.mouseRectErase);
                s.mouseRect = false;
                s.overlayDirty = true;
                continue;
            }
            //(the button held while it's drawn isn't painting)
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                event->motion.state = 0;
                s.overlayDirty = true;
            }
        }
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
                if (s.armed == Armed::Properties && down) {
                    if (sh::hasProperties(col, row)) {
                        //(the editor's mouse is there: K opens the block's screen)
                        s.armed = Armed::None;
                        tapKey(SDLK_K);
                    } else {
                        toast("That isn't a ? block. Click one, or right-click to stop");
                    }
                } else if (s.armed == Armed::Paste && down) {
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
                    toast("Copied " + std::to_string(w) + " x " + std::to_string(h) + ". Paste it from the tools");
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
    //Editing together: this frame's changes go out, with everyone's pointer here (the first
    //controller's is ours; each other one that was used is someone else here, "P2"...)
    std::vector<coedit::LocalCursor> locals;
    for (size_t k = 1; k < s.pointers.size() && k < 8; k++) {
        if (s.pointers[k].shown)
            locals.push_back({static_cast<uint8_t>(k), s.pointers[k].x, s.pointers[k].y});
    }
    const bool first = !s.pointers.empty() && s.pointers[0].shown && s.owner >= 0;
    coedit::afterFrame(first ? s.pointers[0].x : s.cx, first ? s.pointers[0].y : s.cy, true, locals);
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

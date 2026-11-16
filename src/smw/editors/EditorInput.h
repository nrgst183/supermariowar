#pragma once

#include "EditorLauncher.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>

// GAME CONTROLLERS IN THE EDITORS (EditorLauncher.h)
//
// The editors were made for a mouse and a keyboard. Built into the game, they
// get their events through here (the editor headers route SDL_PollEvent, the
// keyboard, modifier and mouse state queries and gfx_flipscreen to these
// functions), and a controller's input becomes the mouse and key events they
// already handle:
//
//   left stick       the mouse pointer (a drawn cursor; faster the further it's pushed)
//   d-pad            the pointer, one tile (32 pixels) per press
//   A / B            left / right mouse button: place / remove (hold A to paint)
//   X / Y (held)     Shift / Ctrl, for the keys and clicks that use them
//   right stick      the arrow keys (scrolls the world editor's map; menus)
//   RB               Enter (confirms prompts and menu choices)
//   Back             Escape (leaves a mode; in the main mode, asks to quit)
//   Start            the action menu: every mode and command of the editor
//                    (the keyboard shortcuts), picked with the d-pad and A
//
// When an editor asks for a name (save as, find), an on-screen keyboard
// opens: d-pad / left stick to pick, A types, B deletes, X shift, Y space,
// Start done, Back cancel.
//
// The mouse and keyboard keep working; moving the mouse hides the drawn cursor.
//
// Several controllers: every one gets its own cursor (local editing
// together): each paints and clicks where its cursor is, the editor's mode
// is shared. Online (CoEdit.h): the session runs from here too (its network,
// sending this frame's changes), and the others' cursors and names are drawn.
namespace editors::input {

void begin(Editor editor);
void end();

// The editors' SDL_PollEvent: the controller's events first, then SDL's
// (the raw controller events are used up here)
bool pollEvent(SDL_Event* event);
// The editors' SDL_GetKeyboardState / SDL_GetModState / SDL_GetMouseState:
// SDL's, plus what the controller holds down
const bool* keyboardState(int* numkeys);
SDL_Keymod modState();
SDL_MouseButtonFlags mouseState(float* x, float* y);
// The editors' gfx_flipscreen: draws the cursor and the open overlay on top
void flip();

// Testing (the --autotest screenshots): a scripted controller instead of the
// real ones; `shot` names a screenshot of the next presented frame
struct TestInput {
    bool a = false, b = false, x = false, y = false, lb = false, rb = false, back = false, start = false;
    bool up = false, down = false, left = false, right = false;
    bool lt = false, rt = false;    // the triggers
    float lx = 0, ly = 0, rx = 0, ry = 0;
    float px = -1, py = -1;    // >= 0: puts the pointer there
    SDL_Keycode key = 0;       // a key press
    std::string shot;
};
// (`pointer`: which controller; `pointers` of them, for local editing together)
using TestDriver = std::function<TestInput(Editor editor, int frame, int pointer)>;
void setTestDriver(TestDriver driver, std::string shotDir, int pointers = 1);

// While an editor asks for text (its dialog()): the on-screen keyboard
struct TextEntry {
    // (the dialog's title, its line of instructions, and the text as typed: the keyboard shows them)
    TextEntry(const char* title = nullptr, const char* prompt = nullptr, const char* text = nullptr);
    ~TextEntry();
};

} // namespace editors::input

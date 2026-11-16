#pragma once

#include <SDL3/SDL_keycode.h>

#include <string>
#include <vector>

// What the editor window (EditorInput.cpp) needs of the level editor: its
// current mode and selection, read and set directly (the same variables the
// editor's own picker screens set). Implemented beside the editor's code, in
// EmbeddedLevelEditor.cpp.
namespace leveleditor::shell {

// The editor shows the map (not one of its full-screen pickers or dialogs)
bool inMapView();

// The editor's edit_mode: 0 blocks, 1 tiles, 2 warps, 3 move, 4 no player spawn,
// 5 no item spawn, 6 tile types, 7 map items, 8 animated tiles
int mode();
void setMode(int mode);

// Blocks: 0-6, 15-19 plain blocks, 7-10 ON/OFF switches (red, green, yellow,
// blue), 11-14 switch blocks, 20-29 blocks only some weapons break.
// `on`: a switch starts ON / a switch block starts solid.
int block();
void setBlock(int block, bool on);
// Whether the switches of a color start ON (the map's setting)
bool switchOn(int color);
bool switchBlockOn();

// Map items (0-5: green spring, spike, Kuribo's shoe, gold spring, sticky shoe, throw box)
int item();
void setItem(int item);

// Tile types (1-18)
int tileType();
void setTileType(int type);

// Layers (0-3)
int layer();
void setLayer(int layer);
bool othersHidden();
void setOthersHidden(bool hidden);
bool blocksShown();
void setBlocksShown(bool shown);

std::string mapName();

// The selected tiles: draws them (at most 3x3 of them) in a square of `size`
// pixels at x, y; false if no tile is selected
bool drawTiles(int x, int y, int size);

// The eyedropper: the selection becomes what is at the cell (the block there in
// block mode, else the tile of the active layer, else the block)
void pick(int col, int row);

// ---- the editor's other screens (the tilesets, warps, platforms...): how a
// controller works them

// One of the screen's key commands
struct Command {
    std::string label;
    SDL_Keycode key = 0;
    bool shift = false;
};

struct Screen {
    std::string title;              // what the screen is (and what is in it now)
    std::string a, b;               // what A (the left button) and B (the right button) do; empty: nothing
    std::string shoulders;          // what LB / RB do (the keys lb and rb); empty: nothing
    SDL_Keycode lb = 0, rb = 0;
    SDL_Keycode xHold = 0, yHold = 0, rbHold = 0;    // keys held while X / Y / RB are (0: X and Y are Shift and Ctrl)
    std::string holds;              // what those do
    std::vector<Command> commands;  // the rest (the editor window lists them; Start)
};
// The screen showing now (when !inMapView())
Screen currentScreen();

}  // namespace leveleditor::shell

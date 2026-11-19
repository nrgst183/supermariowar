#pragma once

#include <SDL3/SDL_keycode.h>

#include <string>
#include <vector>

// What the editor window (EditorInput.cpp) needs of the level editor: its
// current mode and selection, read and set directly (the same variables the
// editor's own picker screens set). Implemented beside the editor's code, in
// EmbeddedLevelEditor.cpp.
namespace leveleditor::shell {

// The editor shows the map (not one of its full-screen pickers or dialogs, nor the exit prompt
// or the ? block screen over it)
bool inMapView();
// The "Exit? Yes / No" prompt is showing over the map (the arrow keys and Enter answer it)
bool exitPrompt();
// The ? block screen (what a ? block gives) is showing over the map
bool blockProperties();
// The block at the cell has contents to set (a ? block and the like: K opens them, at the mouse)
bool hasProperties(int col, int row);

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

// Move mode: moving a selection replaces what is under it (else merges with it)
bool moveReplace();

// The help screen (F1): what the controller's buttons, or the mouse and the keys, do (the overlay
// draws it, EditorInputDraw.cpp)
void drawHelp();
bool helpShown();

// The no-spawn zone being painted: 0 everyone's, 1-4 a team's
int zoneTeam();

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

// ---- painting many cells at once (the controller's rectangles, the fill, copy and paste)

constexpr int MAP_COLS = 20;
constexpr int MAP_ROWS = 15;

// The mode paints cells one by one (blocks, tiles, warps, zones, tile types, animated
// tiles): a rectangle of them can be painted at once
bool paintsCells();
// The cells from c0,r0 to c1,r1 (any corners): painted with what is selected, as
// clicking each would (the selected tiles repeat across it), or cleared (`erase`)
void fillRect(int c0, int r0, int c1, int r1, bool erase);
// The cells joined to this one (up, down, left, right) that are the same as it, in
// what the mode paints (the block, the active layer's tile, the tile type...): painted
void floodFill(int col, int row);
// Copy: the cells (every layer's tiles, the tile type, the block, the warp, the zones)
void copyArea(int c0, int r0, int c1, int r1);
// What was copied: its size (false: nothing)
bool clipboard(int* cols, int* rows);
// Pasted with its top left at the cell (what falls off the map is left out)
void paste(int col, int row);

// ---- what is selected, to come back to (the recently used)

struct Brush {
    int mode = -1;    // 0 a block, 1 tiles, 6 a tile type, 7 a map item (-1: none)
    int id = 0;       // the block, the item, the tile type; the tileset
    bool on = true;   // (a switch, a switch block: how it starts)
    int x = 0, y = 0, cols = 1, rows = 1;    // (tiles: where in the tileset)
    bool operator==(const Brush& o) const
    {
        return mode == o.mode && id == o.id && on == o.on && x == o.x && y == o.y && cols == o.cols && rows == o.rows;
    }
    bool operator!=(const Brush& o) const { return !(*this == o); }
};
// What is selected now (mode -1 if it is nothing to come back to)
Brush brush();
void setBrush(const Brush& brush);
// Tiles' picture (at most 3x3 of them) in a square of `size` pixels at x, y (the overlay draws the rest)
bool drawBrushTiles(const Brush& brush, int x, int y, int size);
// One of the tiles' cells, full size (32 pixels) at x, y: the pattern's cell dc, dr (repeating)
bool drawBrushTile(const Brush& brush, int dc, int dr, int x, int y);

// ---- the tileset screen (choosing tiles): what it shows

struct TilePicker {
    int viewCol = 0, viewRow = 0;    // the tileset's cell at the screen's top left
    int cols = 0, rows = 0;          // the tileset's size, in tiles
    int index = 0, count = 0;        // which tileset, of how many
    std::string name;
};
// False when the tileset screen isn't showing
bool tilePicker(TilePicker* picker);

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

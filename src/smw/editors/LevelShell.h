#pragma once

#include <string>

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

}  // namespace leveleditor::shell

#include "GSGameplay.h"

#include "CmdArgs.h"
#include "Determinism.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "Resimulation.h"
#include "SimulationState.h"
#include "TilesetManager.h"
#include "gamemodes/MapMaker.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "MapList.h"
#include "movingplatform.h"
#include "objecthazard.h"
#include "path.h"
#include "player.h"
#include "rollback/NetGame.h"
#include "rollback/Replay.h"
#include "ui/InputPrompts.h"
#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include <algorithm>
#include <tuple>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "GSGameplayInternal.h"

// THE MAP MAKER (game_mode_mapmaker): the match is a workshop. Everyone plays the map while the
// players with the right to build change it, live: a builder's character leaves play (it waits, as
// after a death) and a cursor of its own paints tiles, blocks and items into g_map, the match's own
// map. L+Up (Tab) starts building, the cursor where the character was, and ends it: the character spawns
// where the cursor is. The host's player always builds, and gives and takes the others' right (the pause
// screen; online, the online panel).
//
// Building is part of the simulation, so that it plays the same on every machine of an online match
// and in replays, and rolls back: a builder's buttons go in its slot's input byte (its character is
// out of play, the byte is free), and the tick applies them (matchedit::simulate, before the players'
// input). The builders' state is the Map Maker mode's, the level data the editing changes is in the
// rollback snapshots then (CMap::forEachLevelRegion). Nothing is drawn in the simulation: the layer
// pictures follow the map each frame drawn (syncVisuals), a rollback's undoing too.
//
// The input byte while building: Left, Right, Up (Jump's bit), Down, B erase (Turbo's), A paint
// (Powerup's), X palette (bit 6), Y layer (bit 7). Done (L+Up, Tab: playing, at the cursor): 0xC0 alone.
// Undo (RB, Backspace or Ctrl+Z): 0x0F alone. While playing, bit 6 (L+Up, Tab) starts building, the
// cursor where the character is. While playing, bit 6 (never a
// person's) starts building. The host's rights commands: 0xF8 | on << 2 | slot.

namespace {

constexpr int SLOTS = 4;
constexpr int BLOCK_TYPES = 15;    // the first row of the blocks' picture (gfx/packs/*/tilesets/blocks.png)
constexpr int ITEM_TYPES = 6;      // MapItemType: springs, spikes, shoes, throw boxes
constexpr int STRIP_ROWS = 3;      // the palette: a strip of the page, three rows of tiles

enum Brush : uint8_t { BrushTile, BrushBlock, BrushItem, BrushWarp, BrushHazard, BrushPlatform, BrushType, BrushSpawn, BrushModeObject };

// The Blocks page: the level editor's rows. The blocks, then the on/off and switch blocks on, then off,
// then the ones only a weapon breaks
constexpr int BLOCK_ROWS = 4;
constexpr int BLOCK_ROW_CELLS[BLOCK_ROWS] = {12, 8, 8, 10};
const char* const BLOCK_NAMES[30] = {"Brick", "? block", "Donut block", "Flip block", "Bounce block", "Note block", "Throw block",
    "Red on/off block", "Green on/off block", "Yellow on/off block", "Blue on/off block", "Red switch", "Green switch",
    "Yellow switch", "Blue switch", "View block", "Red throw block", "Red note block", "Blue note block", "Gray throw block",
    "Fireball breaks it", "Feather breaks it", "Shell breaks it", "Bomb breaks it", "Boomerang breaks it", "Hammer breaks it",
    "Shoe breaks it", "P-Wings break it", "Star breaks it", "Leaf breaks it"};

// A cell of the Blocks page: its block, and (on/off, switch) whether it starts on
short blockOfCell(int col, int row, bool& on)
{
    on = row != 2;
    if (row == 0)
        return static_cast<short>(col < 7 ? col : col + 8);
    if (row == 3)
        return static_cast<short>(20 + col);
    return static_cast<short>(7 + col);
}

void cellOfBlock(short type, bool on, short& col, short& row)
{
    if (type >= 7 && type <= 14) {
        col = static_cast<short>(type - 7);
        row = on ? 1 : 2;
    } else if (type >= 20) {
        col = static_cast<short>(type - 20);
        row = 3;
    } else {
        col = static_cast<short>(type < 7 ? type : type - 8);
        row = 0;
    }
}

// Where a block's picture is (the blocks' sheet: on/off and switch blocks on above, off under)
SDL_Rect blockPicture(short type, bool on)
{
    if (type >= 7 && type <= 10)
        return {224 + (type - 7) * TILESIZE, on ? 0 : TILESIZE, TILESIZE, TILESIZE};
    if (type >= 11 && type <= 14)
        return {352 + (type - 11) * TILESIZE, on ? 0 : TILESIZE, TILESIZE, TILESIZE};
    if (type >= 20)
        return {(type - 20) * TILESIZE, 2 * TILESIZE, TILESIZE, TILESIZE};
    if (type >= 15)
        return {(type - 15) * TILESIZE, TILESIZE, TILESIZE, TILESIZE};
    return {type * TILESIZE, 0, TILESIZE, TILESIZE};
}

// The blocks that can be hidden (the level editor's: ?, flip, bounce, note, view)
bool hideable(short type)
{
    return type == 1 || type == 3 || type == 4 || type == 5 || type == 15 || type == 17 || type == 18;
}

// The Mode objects page: the race goals (1..8), the flag bases (1..4)
constexpr int MODE_OBJECTS = 12;

// A cell's type set apart from its tiles' (the Types page): their names (their pictures: the level editor's)
constexpr int TILE_TYPES = 19;    // "from the tiles", then Solid (1) to Player Death (18)
const char* const TILE_TYPE_NAMES[TILE_TYPES] = {"From the tiles", "Solid", "Solid on top", "Ice", "Death", "Death on top",
    "Death on bottom", "Death on left", "Death on right", "Ice on top", "Ice, death on bottom", "Ice, death on left",
    "Ice, death on right", "Super death", "Super death on top", "Super death on bottom", "Super death on left",
    "Super death on right", "Player death"};

// The no-spawn zones (the Spawns page): who doesn't spawn where
constexpr int SPAWN_KINDS = 6;
const char* const SPAWN_NAMES[SPAWN_KINDS] = {"No player spawns here", "Team 1 doesn't spawn here", "Team 2 doesn't spawn here",
    "Team 3 doesn't spawn here", "Team 4 doesn't spawn here", "No items or mode objects here"};
constexpr int WARP_CONNECTIONS = 10;    // the warps' picture: a column a connection, a row a direction

constexpr uint8_t B_LEFT = 1 << 0, B_RIGHT = 1 << 1, B_UP = 1 << 2, B_DOWN = 1 << 3;
constexpr uint8_t B_ERASE = 1 << 4, B_PAINT = 1 << 5, B_PALETTE = 1 << 6, B_LAYER = 1 << 7;
constexpr uint8_t B_DONE = 0xC0;
constexpr uint8_t B_UNDO = 0x0F;    //(left, right, up and down at once: no pad's d-pad; the undo button)
constexpr uint8_t RIGHTS_CMD = 0xF8;

// ---- This machine's (not simulation state)
CPlayerInput s_menuKeys;           // the builders' keys, read as the menus' (the match reads the game's)
bool s_switchPressed[SLOTS] {};    // Tab (or the script): building starts or ends, by input device
bool s_lbHeld[SLOTS] {};           // a pad's LB held (L+Up: the switch), by input device
bool s_chordHeld[SLOTS] {};        // L+Up held last time (the switch on the press only)
bool s_undoPressed[SLOTS] {};      // the undo button (RB, Backspace, Ctrl+Z), by input device
bool s_undoHeld[SLOTS] {};         // ... held down (it undoes again and again after a moment)
int s_undoHeldTicks[SLOTS] {};
bool s_redoPressed[SLOTS] {};      // redo (LB+RB, Ctrl+Y, Ctrl+Shift+Z, the mouse's forward button), as undo's
bool s_redoHeld[SLOTS] {};
int s_redoHeldTicks[SLOTS] {};
bool s_toolPressed[SLOTS] {};      // T: the next tool (pencil, rectangle, fill)
int s_recentJump[SLOTS] {};        // 1..8: that many recent brushes on (the strip's numbers), a next a tick
bool s_recentJumpGap[SLOTS] {};
int s_tabStep[SLOTS] {};           // the triggers (Page Up/Down): the palette's tab before (-1) or after (1)
int s_recentStep[SLOTS] {};        // LB+Left/Right, Q/E, the mouse's wheel: the recent brush before (-1) or after (1)
bool s_pickPressed[SLOTS] {};      // LB+Down, R, Ctrl+click: the brush of what's under the cursor
bool s_grabPressed[SLOTS] {};      // LB+B, G, Ctrl+right click: what's under the cursor picked up (to move it)
bool s_lbChord[SLOTS][5] {};       // (LB with Left, Right, Down, B held last tick: each a press once)
bool s_clearPending = false;       // the pause menu's Clear map: the host's command, sent next tick
bool s_triggerHeld[SLOTS][2] {};   // (a trigger pressed in: one step until it's let go)
bool s_layerHeld[SLOTS] {};        // the layer button (Y, Shift) held on the map: Up/Down step the layer, not the cursor
bool s_layerStepped[SLOTS] {};     // ... and stepped it since it went down (let go: no "next" then)
bool s_layerDir[SLOTS][2] {};
bool s_xClosed[SLOTS] {};          // X shut the parts window: held on, it doesn't open it again
int s_windowStepGap[SLOTS] {};     // (a page step to the window's every other tick: each a press)      // (Up, Down held last tick with it: a step a press)
bool s_pausePressed[SLOTS] {};     // building: the pause (a pad's Start), by input device
bool s_escPressed[SLOTS] {};       // the keyboard's Escape: the pause (the palette open: back), by input device
bool s_escHeld = false;            // the keyboard's Escape, Delete held (Delete erases; Escape: the palette's back)
bool s_deleteHeld = false;
bool s_wantRights[SLOTS] {};       // the host's toggles on its panel, sent as commands until the match has them
bool s_wantPending[SLOTS] {};      // (only those: else the panel shows the match's rights)
int s_rightsSlot = 1;              // the panel: the player whose right the host gives or takes
int s_pauseRow = 0;                // the online panel: 0 the players' rights, 1 Save, 2 Clear map
int s_menuRow = 0;                 // the pause screen's menu: 0 Resume, 1 the rights, 2 Save, 3 Quit
bool s_menuWasOpen = false;        // (opened again: on Resume)
std::string s_savedNote;           // "Saved as ...", shown on the pause screen a while
Uint64 s_savedAt = 0;
bool s_savedThisPause = false;     // saved since the pause screen opened (nothing's changed since)
bool s_clearArmed = false;         // Clear map pressed once: pressed again, it clears
bool s_helpOpen = false;           // the controls sheet (F1, the pause menu's Controls): every control on a page
bool s_helpToggle = false;         // F1 pressed
std::string s_toast;               // a short message over the toolbar ("Undo", "Copied Brick"...), a moment
Uint64 s_toastAt = 0;
bool s_carrying[SLOTS] {};         // a builder's picked-up part, not put down yet (its cursor: a dashed frame)
constexpr int TOOLBAR_H = 44;      // the toolbar: the part in hand, the recent ones, the tools, the layer
int s_scriptByte[SLOTS] {-1, -1, -1, -1};    // --at-role matchedit: the bytes it plays
TilesetTile s_shown[MAPWIDTH][MAPHEIGHT][MAPLAYERS];    // the map the layer pictures show
bool s_shownValid = false;
short s_shownBackground = -1, s_shownMusic = -1;          // the background and music shown and playing
std::string s_mapBackground;                             // the map's own (the choice -1)
short s_mapMusic = 0;

CGM_MapMaker* mode()
{
    if (game_values.gamemode && game_values.gamemode->gamemode == game_mode_mapmaker)
        return static_cast<CGM_MapMaker*>(game_values.gamemode);
    return nullptr;
}

CPlayer* playerOf(int slot)
{
    for (CPlayer* player : g_sim.players) {
        if (player->getGlobalID() == slot)
            return player;
    }
    return nullptr;
}

bool building(int slot)
{
    CPlayer* player = playerOf(slot);
    return player && player->isEditing();
}

// A player at the controls of this machine (not a CPU, not another machine's)
bool localHuman(int slot)
{
    if (slot < 0 || slot >= SLOTS || game_values.playercontrol[slot] != 1 || !playerOf(slot))
        return false;
    rollback::NetGame& net = rollback::NetGame::instance();
    return !net.inMatch() || net.isLocalSlot(slot);
}

// The input device of a local player: its slot here; online, the local players use the first devices
int deviceOf(int slot)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.inMatch())
        return slot;
    int device = 0;
    for (int s = 0; s < slot; s++)
        if (net.isLocalSlot(s) && game_values.playercontrol[s] == 1)
            device++;
    return device;
}

size_t tilesetCount()
{
    return g_tilesetmanager ? g_tilesetmanager->count() : 0;
}

// ---- The hazards' and platforms' settings (the palette: a row of them under the types)

enum SettingFormat : uint8_t { Number, Spin, LeftRight, FlameFacing, PlantFacing, Seconds, Way8, Tiles, HalfTiles, MadeOf };

struct Setting {
    const char* name;
    short min, max, def;
    SettingFormat format;
};

// Each hazard type's (the level editor's values: fire bar length 5, speed 0.02, ...)
const Setting HAZARD_SETTING[HAZARD_TYPES][HAZARD_SETTINGS] = {
    {{"Length", 1, 9, 5, Number}, {"Speed", 1, 10, 4, Number}, {"Spin", 0, 1, 0, Spin}, {}},
    {{"Discs", 1, 8, 2, Number}, {"Radius", 2, 14, 7, HalfTiles}, {"Speed", 1, 10, 4, Number}, {"Spin", 0, 1, 0, Spin}},
    {{"Fires every", 1, 20, 4, Seconds}, {"Speed", 1, 10, 2, Number}, {"Facing", 0, 1, 0, LeftRight}, {}},
    {{"Fires every", 1, 20, 4, Seconds}, {"Facing", 0, 3, 0, FlameFacing}, {}, {}},
    {{"Comes up every", 1, 20, 10, Seconds}, {"Facing", 0, 3, 0, PlantFacing}, {}, {}},
    {{"Comes up every", 1, 20, 10, Seconds}, {"Facing", 0, 3, 0, PlantFacing}, {}, {}},
    {{"Comes up every", 1, 20, 10, Seconds}, {"Facing", 0, 3, 0, PlantFacing}, {}, {}},
    {{"Comes up every", 1, 20, 10, Seconds}, {"Facing", 0, 3, 0, PlantFacing}, {}, {}},
};

// Each platform path's
const Setting PLATFORM_SETTING[PLATFORM_KINDS][PLATFORM_SETTINGS] = {
    {{"Width", 1, 8, 3, Tiles}, {"Height", 1, 4, 1, Tiles}, {"Made of", 0, 1, 0, MadeOf}, {"Goes", 0, 7, 0, Way8},
        {"Distance", 1, 16, 4, Tiles}, {"Speed", 1, 10, 4, Number}, {}},
    {{"Width", 1, 8, 3, Tiles}, {"Height", 1, 4, 1, Tiles}, {"Made of", 0, 1, 0, MadeOf}, {"Goes", 0, 7, 0, Way8},
        {"Speed", 1, 10, 4, Number}, {}, {}},
    {{"Width", 1, 8, 3, Tiles}, {"Height", 1, 4, 1, Tiles}, {"Made of", 0, 1, 0, MadeOf}, {"Across", 0, 8, 3, Tiles},
        {"Up and down", 0, 6, 2, Tiles}, {"Speed", 1, 10, 4, Number}, {"Spin", 0, 1, 0, Spin}},
};

const char* const PLATFORM_KIND_NAMES[PLATFORM_KINDS] = {"back and forth", "on around the screen", "in a circle"};

int settingCount(const Setting* row, int max)
{
    int n = 0;
    while (n < max && row[n].name)
        n++;
    return n;
}

std::string settingText(const Setting& s, short v)
{
    static const char* const WAY8[8] = {"right", "down right", "down", "down left", "left", "up left", "up", "up right"};
    switch (s.format) {
    case Spin: return v ? "counterclockwise" : "clockwise";
    case MadeOf: return v ? "map tiles" : "brush tile";
    case LeftRight: return v ? "left" : "right";
    case FlameFacing: return v == 0 ? "right" : v == 1 ? "left" : v == 2 ? "up" : "down";
    case PlantFacing: return v == 0 ? "up" : v == 1 ? "down" : v == 2 ? "left" : "right";
    case Seconds: return std::to_string(v / 2) + (v % 2 ? ".5" : "") + " s";
    case Way8: return WAY8[v & 7];
    case Tiles: return std::to_string(v) + (v == 1 ? " tile" : " tiles");
    case HalfTiles: return std::to_string(v / 2) + (v % 2 ? ".5" : "") + " tiles";
    default: return std::to_string(v);
    }
}

// A setting changed by a step (-1, 1): a number held at its ends, a choice round and round
short stepSetting(const Setting& s, short v, int step)
{
    const bool choice = s.format == Spin || s.format == LeftRight || s.format == FlameFacing || s.format == PlantFacing || s.format == Way8
        || s.format == MadeOf;
    const int n = s.max - s.min + 1;
    if (choice)
        return static_cast<short>(s.min + ((v - s.min + step) % n + n) % n);
    return static_cast<short>(std::clamp<int>(v + step, s.min, s.max));
}

// The palette's pages: the tilesets, then these
enum ExtraPage { PageBlocks, PageContents, PageItems, PageWarps, PageHazards, PagePlatforms, PageBackgrounds, PageMusic, PageTypes, PageSpawns,
    PageAnimated, PageWeather, PageModeObjects, PageTools, EXTRA_PAGES };
constexpr int ANIMATIONS_ACROSS = 32, ANIMATIONS_DOWN = 8;    // the animated tiles' picture: 256 of them
constexpr int WEATHERS = 7;                                  // CMap::eyecandy's bits
const char* const WEATHER_NAMES[WEATHERS] = {"Clouds", "Ghosts", "Leaves", "Snow", "Fish", "Rain", "Bubbles"};
const char* const WEATHER_LAYERS[3] = {"behind everything", "behind the players", "in front of everything"};
constexpr int TOOLS = 5;    // pencil, rectangle, fill, copy, paste
const char* const TOOL_NAMES[TOOLS] = {"Pencil: a cell at a time", "Rectangle: A (or B) here, let go there", "Fill: the same tiles around",
    "Copy: A here, let go there (10 by 8 at most)", "Paste: what was copied, here"};

int pageCount()
{
    return static_cast<int>(tilesetCount()) + EXTRA_PAGES;
}

// The palette's tabs (X goes through them): the tiles (every tileset: Y goes through them), then the
// other pages, a tab each
constexpr int PALETTE_TABS = 1 + EXTRA_PAGES;
constexpr int CYCLE_TABS = PALETTE_TABS - 1;    // X goes round these: the tools, the last, are on the toolbar instead
const char* const TAB_NAMES[PALETTE_TABS] = {"Tiles", "Blocks", "?", "Items", "Warps", "Hazards", "Moving", "Backdrop", "Music",
    "Types", "Spawns", "Animated", "Weather", "Goals", "Tools"};
// The tabs in groups (next to each other, as X goes): a group's name along the top, its pages beside it
// when it's open
struct TabGroup {
    const char* name;
    int first, count;
};
const TabGroup TAB_GROUPS[] = {{"Tiles", 0, 1}, {"Blocks", 1, 2}, {"Items", 3, 1}, {"Warps", 4, 1}, {"Hazards", 5, 2},
    {"Scene", 7, 2}, {"Rules", 9, 2}, {"More", 11, 3}};

// The mouse's commands, in its builder's byte: left and right at once (not a d-pad's, and the keys'
// are let go of one of them), and a number in the rest: MOUSE_STEP.. a step of the cursor (a cell any
// way, or none) with A or B held; MOUSE_TAB.. a tab of the palette. (Not 3: the undo button's; not 62
// or 63: the host's rights commands)
// After the tabs, the builder's commands of the same kind: the brush of what's under the cursor (the
// eyedropper), the map cleared (the host's), the recent brushes' next and previous
constexpr int MOUSE_STEP = 4, MOUSE_TAB = 40;
constexpr int CMD_PICK = MOUSE_TAB + PALETTE_TABS, CMD_CLEAR = CMD_PICK + 1, CMD_RECENT_NEXT = CMD_PICK + 2, CMD_RECENT_PREV = CMD_PICK + 3,
              CMD_GRAB = CMD_PICK + 4, CMD_REDO = CMD_PICK + 5, CMD_TOOL = CMD_PICK + 6;
//(the eyedropper; Clear map; the recent brushes; pick up (to move); redo; the next tool: pencil,
//rectangle, fill. 62 and up would be RIGHTS_CMD's)
static_assert(MOUSE_STEP + 36 <= MOUSE_TAB && CMD_TOOL < 62, "the mouse's commands fit the byte");

bool mouseCommand(uint8_t byte)
{
    return (byte & 3) == 3 && (byte >> 2) >= MOUSE_STEP && (byte >> 2) <= CMD_TOOL;
}

uint8_t commandByte(int command)
{
    return static_cast<uint8_t>((command << 2) | 3);
}

uint8_t mouseStepByte(int dx, int dy, bool paint, bool erase)
{
    const int q = (dx + 1) + (dy + 1) * 3 + (paint ? 9 : 0) + (erase ? 18 : 0);
    return static_cast<uint8_t>(((MOUSE_STEP + q) << 2) | 3);
}

// The mouse's jumps: up and down at once (the keys' are let go of both, as left and right), the rest a
// number: 0..19 the cursor to that column, 20..34 to that row (the mouse's cell at once, a tick an
// axis, not a cell a tick). Not 0xFC and up: the host's rights commands
// After them, 35..39 a tool, 40..43 a layer (the toolbar's, clicked)
constexpr int JUMP_ROWS_AT = MAPWIDTH, JUMPS = MAPWIDTH + MAPHEIGHT;
constexpr int JUMP_TOOL_AT = JUMPS, JUMP_LAYER_AT = JUMP_TOOL_AT + TOOLS, JUMP_COUNT = JUMP_LAYER_AT + MAPLAYERS;
static_assert(JUMP_COUNT <= 45, "the jumps fit the bytes with up and down");

bool jumpByte(uint8_t byte)
{
    return (byte & 0x0C) == 0x0C && (byte & 3) != 3 && byte < 0xF0 && (byte & 3) + 3 * (byte >> 4) < JUMP_COUNT;
}

int jumpOf(uint8_t byte)
{
    return (byte & 3) + 3 * (byte >> 4);
}

uint8_t jumpTo(int jump)
{
    return static_cast<uint8_t>(((jump / 3) << 4) | 0x0C | (jump % 3));
}

uint8_t mouseTabByte(int tab)
{
    return static_cast<uint8_t>(((MOUSE_TAB + tab) << 2) | 3);
}

// A byte's buttons as the keys' would be: a mouse command's A and B (a tab's: none); another's as it is
uint8_t mouseButtons(uint8_t byte)
{
    if (jumpByte(byte))
        return 0;
    if (!mouseCommand(byte))
        return byte;
    if ((byte >> 2) >= MOUSE_TAB)
        return 0;
    const int q = (byte >> 2) - MOUSE_STEP;
    return static_cast<uint8_t>(((q / 9) & 1 ? B_PAINT : 0) | ((q / 18) & 1 ? B_ERASE : 0));
}

int tabOfPage(int page)
{
    const int tilesets = static_cast<int>(tilesetCount());
    return page < tilesets ? 0 : 1 + page - tilesets;
}

short pageOf(ExtraPage page)
{
    return static_cast<short>(tilesetCount() + page);
}

bool onPage(const MapMakerBuilder& b, ExtraPage page)
{
    return b.page == pageOf(page);
}

bool onWarpsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageWarps);
}

bool onTypesPage(const MapMakerBuilder& b)
{
    return onPage(b, PageTypes);
}

bool onSpawnsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageSpawns);
}

bool onAnimatedPage(const MapMakerBuilder& b)
{
    return onPage(b, PageAnimated);
}

bool onWeatherPage(const MapMakerBuilder& b)
{
    return onPage(b, PageWeather);
}

bool onModeObjectsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageModeObjects);
}

bool onToolsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageTools);
}

bool onHazardsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageHazards);
}

bool onPlatformsPage(const MapMakerBuilder& b)
{
    return onPage(b, PagePlatforms);
}

// The ? block page's item sets: the game's (MI_PowerupSelection's Item Set, after its custom ones)
constexpr int ITEM_SET_CELLS = 13;    // random, then 12 sets
constexpr short ITEM_SETS[ITEM_SET_CELLS - 1] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const char* const ITEM_SET_NAMES[ITEM_SET_CELLS - 1] = {"Balanced Set", "Weapons Only", "Koopa Bros Weapons", "Support Items",
    "Booms and Shakes", "Fly and Glide", "Shells Only", "Mushrooms Only", "Super Mario Bros 1", "Super Mario Bros 2",
    "Super Mario Bros 3", "Super Mario World"};

// The powerups in the order the item menus show them (MI_PowerupSelection's)
constexpr short POWERUP_ORDER[NUM_POWERUPS] = {1, 0, 2, 6, 3, 8, 4, 20, 18, 7, 5, 10, 11, 22, 19, 9, 23, 16, 21, 12, 17, 13, 24, 15, 25, 14};

bool onContentsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageContents);
}

const BackgroundList& backgrounds()
{
    static BackgroundList list;
    return list;
}

bool onBackgroundsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageBackgrounds);
}

bool onMusicPage(const MapMakerBuilder& b)
{
    return onPage(b, PageMusic);
}

bool onBlocksPage(const MapMakerBuilder& b)
{
    return onPage(b, PageBlocks);
}

bool onItemsPage(const MapMakerBuilder& b)
{
    return onPage(b, PageItems);
}

void pageSize(const MapMakerBuilder& b, int& w, int& h)
{
    if (onWarpsPage(b)) {
        w = WARP_CONNECTIONS;
        h = 4;
        return;
    }
    if (onPage(b, PageBlocks)) {
        h = BLOCK_ROWS;
        w = BLOCK_ROW_CELLS[std::clamp<int>(b.py, 0, BLOCK_ROWS - 1)];
        return;
    }
    if (onModeObjectsPage(b)) {
        h = 2;
        w = b.py == 0 ? 8 : 4;
        return;
    }
    if (onAnimatedPage(b) || onWeatherPage(b)) {
        w = onAnimatedPage(b) ? ANIMATIONS_ACROSS : WEATHERS;
        h = onAnimatedPage(b) ? ANIMATIONS_DOWN : 3;
        return;
    }
    if (onToolsPage(b) || onTypesPage(b) || onSpawnsPage(b)) {
        w = onToolsPage(b) ? TOOLS : onTypesPage(b) ? TILE_TYPES : SPAWN_KINDS;
        h = 1;
        return;
    }
    //(the hazards' and platforms': a row of the types, then the settings of the one there, a row each)
    if (onHazardsPage(b)) {
        w = HAZARD_TYPES;
        h = 1 + settingCount(HAZARD_SETTING[b.px % HAZARD_TYPES], HAZARD_SETTINGS);
        return;
    }
    if (onPlatformsPage(b)) {
        w = PLATFORM_KINDS;
        h = 1 + settingCount(PLATFORM_SETTING[b.px % PLATFORM_KINDS], PLATFORM_SETTINGS);
        return;
    }
    if (onContentsPage(b)) {
        //(the item sets above: random by the default weights, then the game's sets; the powerups
        //under them, a mix)
        w = b.py == 0 ? ITEM_SET_CELLS : NUM_POWERUPS;
        h = 2;
        return;
    }
    if (onBlocksPage(b) || onItemsPage(b) || onBackgroundsPage(b) || onMusicPage(b)) {
        w = onBlocksPage(b) ? BLOCK_TYPES
            : onItemsPage(b) ? ITEM_TYPES
            : onBackgroundsPage(b) ? std::max<int>(1, static_cast<int>(backgrounds().count()))
            : static_cast<int>(MusicCategory::COUNT);
        h = 1;
        return;
    }
    const CTileset* tileset = g_tilesetmanager->tilesetInfo(static_cast<size_t>(b.page));
    w = tileset ? std::max<int>(1, tileset->width()) : 1;
    h = tileset ? std::max<int>(1, tileset->height()) : 1;
}

// ---- The simulation: the edits

// The palette's tab (X: the next; the mouse: one): the tiles' is the brush's tileset
void openTab(MapMakerBuilder& b, int tab)
{
    const int tilesets = static_cast<int>(tilesetCount());
    const int tileset = b.tile.iID >= 0 && b.tile.iID < tilesets ? b.tile.iID : 0;
    b.page = static_cast<short>(tab == 0 ? tileset : tilesets + tab - 1);
    b.px = b.py = b.scrollX = b.scrollY = 0;
}

// A step of the mouse's: the cursor, or the palette's choice (to the edge, not round it)
void mouseMove(MapMakerBuilder& b, int dx, int dy)
{
    if (!b.palette) {
        b.cx = static_cast<short>(std::clamp(b.cx + dx, 0, MAPWIDTH - 1));
        b.cy = static_cast<short>(std::clamp(b.cy + dy, 0, MAPHEIGHT - 1));
        return;
    }
    if (b.adjusting)
        return;
    int w = 1, h = 1;
    pageSize(b, w, h);
    b.py = static_cast<short>(std::clamp(b.py + dy, 0, h - 1));
    pageSize(b, w, h);
    b.px = static_cast<short>(std::clamp(b.px + dx, 0, w - 1));
    b.scrollX = static_cast<short>(std::clamp<int>(b.scrollX, std::max(0, b.px - MAPWIDTH + 1), b.px));
    b.scrollY = static_cast<short>(std::clamp<int>(b.scrollY, std::max(0, b.py - STRIP_ROWS + 1), b.py));
}

void defaultBlockSettings(MapBlock& block, short contents, uint32_t mix = 0, bool on = true)
{
    //(as the level editor sets them: a ? block's powerups the default preset's, an item set's, the
    //mix's evenly, or the one chosen alone; a switch's on)
    if (block.iType == 1 || block.iType == 15) {
        for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++) {
            short weight;
            if (contents >= 0)
                weight = iSetting == contents ? 1 : 0;
            else if (contents == -2)
                weight = (mix >> iSetting) & 1 ? 1 : 0;
            else if (contents <= -10)
                weight = defaultPowerupSetting(static_cast<size_t>(-contents - 10), iSetting);
            else
                weight = defaultPowerupSetting(0, iSetting);
            block.iSettings[iSetting] = weight;
        }
    } else if (block.iType >= 11 && block.iType <= 14) {
        block.iSettings[0] = on ? 1 : 0;
    }
}

// The animated tiles' types (the level editor's table: the game doesn't load it), an animation each
const TileType* animatedTileTypes()
{
    static TileType types[256] {};
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (FILE* f = fopen(convertPath("gfx/packs/Classic/tilesets/tile_animation_tileset.tls").c_str(), "rb")) {
            for (int k = 0; k < 256; k++) {
                int32_t v = 0;
                if (fread(&v, sizeof(v), 1, f) != 1)
                    break;
                types[k] = v >= 0 && v < static_cast<int32_t>(TileType::Gap) ? static_cast<TileType>(v) : TileType::NonSolid;
            }
            fclose(f);
        }
    }
    return types;
}

// The type a cell's tiles give it: the topmost that has one
TileType tilesType(int x, int y)
{
    for (short k = MAPLAYERS - 1; k >= 0; k--) {
        const TilesetTile& tile = *g_map->tileAt(static_cast<short>(x), static_cast<short>(y), k);
        TileType type = TileType::NonSolid;
        if (tile.iID == TILESETANIMATED)
            type = animatedTileTypes()[(tile.iRow + (tile.iCol << 5)) & 255];
        else if (const CTileset* tileset = tile.iID >= 0 ? g_tilesetmanager->tilesetInfo(static_cast<size_t>(tile.iID)) : nullptr)
            type = tileset->tileType(tile.iCol, tile.iRow);
        if (type != TileType::NonSolid)
            return type;
    }
    return TileType::NonSolid;
}

// Whether a cell's type was set apart from its tiles' (the Types page: kept when its tiles change)
bool typeSetApart(int x, int y)
{
    TileType now = g_map->tileType(static_cast<short>(x), static_cast<short>(y));
    if (now == TileType::Gap)
        now = TileType::NonSolid;    //(a gap: worked out from the cells around)
    return now != tilesType(x, y);
}

// A cell's tiles changed: its tile type (unless set apart: `keepType`), the gaps around it, the spawn
// areas (its pictures: syncVisuals)
void tilesChanged(int x, int y, bool keepType = false)
{
    if (!keepType)
        g_map->setTileType(static_cast<short>(x), static_cast<short>(y), tilesType(x, y));
    for (int i = x - 1; i <= x + 1; i++)
        for (int j = y - 1; j <= y + 1; j++)
            if (i >= 0 && i < MAPWIDTH && j >= 0 && j < MAPHEIGHT)
                g_map->UpdateTileGap(static_cast<short>(i), static_cast<short>(j));
    g_map->refreshSpawnAreas();
}

int itemAt(const CGM_MapMaker& m, int x, int y)
{
    for (int k = 0; k < m.itemCount; k++)
        if (m.items[k].ix == x && m.items[k].iy == y)
            return k;
    return -1;
}

// A hazard of the brush's: its type, from its settings (in the level editor's terms: speed 4, 0.02
// radians a frame; every 4, 120 frames; ...)
MapHazard hazardOf(const MapMakerBuilder& b)
{
    MapHazard hazard;
    std::memset(static_cast<void*>(&hazard), 0, sizeof(hazard));    //(the padding too: compared byte for byte)
    const int type = b.hazard % HAZARD_TYPES;
    const short* s = b.hazardSet[type];
    hazard.itype = static_cast<MapHazardType>(type);
    hazard.ix = static_cast<short>(b.cx * 2);
    hazard.iy = static_cast<short>(b.cy * 2);
    switch (hazard.itype) {
    case MapHazardType::FireBar:
        hazard.iparam[0] = s[0];
        hazard.dparam[0] = (s[2] ? -1.0f : 1.0f) * 0.005f * static_cast<float>(s[1]);
        break;
    case MapHazardType::RotoDisc:
        hazard.iparam[0] = s[0];
        hazard.dparam[0] = (s[3] ? -1.0f : 1.0f) * 0.005f * static_cast<float>(s[2]);
        hazard.dparam[2] = 16.0f * static_cast<float>(s[1]);
        break;
    case MapHazardType::BulletBill:
        hazard.iparam[0] = static_cast<short>(s[0] * 30);
        hazard.dparam[0] = (s[2] ? -1.0f : 1.0f) * static_cast<float>(s[1]);
        break;
    default:    //(the flame cannon, the piranha plants)
        hazard.iparam[0] = static_cast<short>(s[0] * 30);
        hazard.iparam[1] = s[1];
        break;
    }
    return hazard;
}

// The hazard in a cell (a hazard is on the half-tile grid: its cell, where its corner is)
int hazardAt(const CGM_MapMaker& m, int x, int y)
{
    for (int k = 0; k < m.hazardCount; k++)
        if (m.hazards[k].ix / 2 == x && m.hazards[k].iy / 2 == y)
            return k;
    return -1;
}

// A made platform where a cell is: under it now, or starting there
int platformAt(const CGM_MapMaker& m, int x, int y)
{
    const int px = x * TILESIZE + TILESIZE / 2, py = y * TILESIZE + TILESIZE / 2;
    for (int k = 0; k < m.platformCount; k++) {
        const MapMakerPlatform& p = m.platforms[k];
        if (y >= p.y && y < p.y + std::max<short>(1, p.h) && x >= p.x && x < p.x + p.w)
            return k;
        const MovingPlatform* live = p.live;
        if (live && std::abs(px - static_cast<int>(live->centerX())) < live->width() / 2
            && std::abs(py - static_cast<int>(live->centerY())) < live->height() / 2)
            return k;
    }
    return -1;
}

// A made platform in the match: a temporary platform (the simulation's arena) on its path (the level
// editor's speeds: 4, 1.04 pixels a frame; a circle's 4, 0.012 radians a frame)
MovingPlatform* makePlatform(const MapMakerPlatform& p)
{
    const short h = std::max<short>(1, p.h);
    const float halfWidth = static_cast<float>(p.w * TILESIZE) / 2.0f;
    const Vec2f start(static_cast<float>(p.x * TILESIZE) + halfWidth, static_cast<float>(p.y * TILESIZE) + static_cast<float>(h * TILESIZE) / 2.0f);
    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    const int way = p.way & 7;
    MovingPlatformPath* path = nullptr;
    if (p.kind == 1) {
        path = new StraightPathContinuous(0.26f * static_cast<float>(p.speed), start, static_cast<float>(way) * 0.78539816f, false);
    } else if (p.kind == 2) {
        const Vec2f radius(static_cast<float>(p.rx * TILESIZE), static_cast<float>(p.ry * TILESIZE));
        path = new EllipsePath((p.spin ? -1.0f : 1.0f) * 0.003f * static_cast<float>(p.speed), 0.0f, radius, start, false);
    } else {
        const float distance = static_cast<float>(p.dist * TILESIZE);
        const Vec2f end(start.x + static_cast<float>(DX[way]) * distance, start.y + static_cast<float>(DY[way]) * distance);
        path = new StraightPath(0.26f * static_cast<float>(p.speed), start, end, false);
    }
    auto typeOf = [](const TilesetTile& tile, bool solid) {
        TileType type = solid ? TileType::Solid : TileType::NonSolid;
        if (const CTileset* tileset = tile.iID >= 0 ? g_tilesetmanager->tilesetInfo(static_cast<size_t>(tile.iID)) : nullptr)
            type = tileset->tileType(tile.iCol, tile.iRow);
        return type == TileType::NonSolid && solid ? TileType::Solid : type;
    };
    MovingPlatform* platform = nullptr;
    if (p.copied) {
        //(the map's tiles: their own types, the empty ones nothing)
        TileType types[PLATFORM_MAX_W * PLATFORM_MAX_H];
        for (int k = 0; k < p.w * h; k++)
            types[k] = typeOf(p.cells[k], false);
        platform = new MovingPlatform(p.cells, types, p.w, h, 2, path);
    } else {
        //(one tile: a platform to stand on, a tile with no type of its own solid)
        platform = new MovingPlatform(p.tile, typeOf(p.tile, true), p.w, h, 2, path);
    }
    g_map->AddTemporaryPlatform(platform);
    return platform;
}

void setSwitchStart(CGM_MapMaker& m, int color, short state);

void paint(CGM_MapMaker& m, const MapMakerBuilder& b)
{
    if (b.brush == BrushBlock) {
        MapBlock* block = g_map->blockat(b.cx, b.cy);
        MapBlock wanted {};
        wanted.iType = b.block;
        defaultBlockSettings(wanted, b.contents, b.mix, b.blockOn);
        const bool hidden = b.blockHidden && hideable(b.block);
        //(an on/off block: the map's switch of its color starts as it says)
        const bool toggle = b.block >= 7 && b.block <= 10;
        const short state = b.blockOn ? 1 : 0;
        const bool switchChanged = toggle && m.switches[b.block - 7] != state;
        if (!switchChanged && block->iType == b.block && block->fHidden == hidden
            && std::equal(std::begin(block->iSettings), std::end(block->iSettings), std::begin(wanted.iSettings)))
            return;
        block->iType = b.block;
        block->fHidden = hidden;
        std::copy(std::begin(wanted.iSettings), std::end(wanted.iSettings), std::begin(block->iSettings));
        if (switchChanged)
            setSwitchStart(m, b.block - 7, state);
        SetMapBlockAt(b.cx, b.cy);
        g_map->refreshSpawnAreas();
    } else if (b.brush == BrushHazard) {
        //(one a cell: another there replaced)
        const MapHazard hazard = hazardOf(b);
        int k = hazardAt(m, b.cx, b.cy);
        if (k >= 0 && std::memcmp(&m.hazards[k], &hazard, sizeof(hazard)) == 0)
            return;
        if (k < 0) {
            if (m.hazardCount >= MAXMAPHAZARDS)
                return;
            k = m.hazardCount++;
        }
        m.hazards[k] = hazard;
        RespawnMapHazards(m.hazards, m.hazardCount);
    } else if (b.brush == BrushPlatform) {
        //(where none is yet: one is made, of the tile brush's tile)
        if (platformAt(m, b.cx, b.cy) >= 0 || m.platformCount >= MADE_PLATFORMS)
            return;
        MapMakerPlatform& p = m.platforms[m.platformCount++];
        const int kind = b.platformKind % PLATFORM_KINDS;
        const short* s = b.platformSet[kind];
        p.tile = b.tile.iID >= 0 ? b.tile : TilesetTile {static_cast<short>(g_tilesetmanager->classicTilesetIndex()), 0, 0};
        p.w = static_cast<short>(std::min<int>(std::min<int>(s[0], PLATFORM_MAX_W), MAPWIDTH - b.cx));
        p.h = static_cast<short>(std::min<int>(std::min<int>(s[1], PLATFORM_MAX_H), MAPHEIGHT - b.cy));
        p.copied = s[2];
        p.x = b.cx;
        p.y = b.cy;
        p.kind = static_cast<short>(kind);
        if (kind == 0) {
            p.way = s[3];
            p.dist = s[4];
            p.speed = s[5];
        } else if (kind == 1) {
            p.way = s[3];
            p.speed = s[4];
        } else {
            p.rx = s[3];
            p.ry = s[4];
            p.speed = s[5];
            p.spin = s[6];
        }
        //(of the map's tiles there: the brush's layer's, copied; the map keeps them)
        if (p.copied) {
            for (short col = 0; col < p.w; col++)
                for (short row = 0; row < p.h; row++)
                    p.cells[col * p.h + row] = *g_map->tileAt(p.x + col, p.y + row, b.layer);
        }
        p.live = makePlatform(p);
    } else if (b.brush == BrushWarp) {
        Warp* warp = g_map->warp(b.cx, b.cy);
        if (warp->connection == b.warpConn && warp->direction == b.warpDir)
            return;
        warp->direction = static_cast<WarpEnterDirection>(b.warpDir);
        warp->connection = b.warpConn;
        g_map->recalcWarps();
    } else if (b.brush == BrushItem) {
        //(one item a cell, not in a block)
        if (itemAt(m, b.cx, b.cy) >= 0 || m.itemCount >= MAXMAPITEMS || g_map->blockat(b.cx, b.cy)->iType != -1)
            return;
        //(field by field: the padding stays zero, the state is compared byte for byte)
        MapItem& item = m.items[m.itemCount++];
        item.itype = static_cast<MapItemType>(b.item);
        item.ix = b.cx;
        item.iy = b.cy;
        SpawnMapItem(item);
    } else if (b.brush == BrushModeObject) {
        //(the first placed: all of its kind placed, the others where the level editor puts them)
        const bool goal = b.modeObject < 8;
        const int k = goal ? b.modeObject : b.modeObject - 8;
        short (*places)[2] = goal ? m.goals : m.bases;
        short& count = goal ? m.goalCount : m.baseCount;
        const short x = static_cast<short>(b.cx * TILESIZE), y = static_cast<short>(b.cy * TILESIZE);
        if (count > 0 && places[k][0] == x && places[k][1] == y)
            return;
        if (count == 0) {
            count = static_cast<short>(goal ? 8 : 4);
            for (int i = 0; i < count; i++) {
                places[i][0] = static_cast<short>(goal ? i * 80 + 20 : i * 160 + 64);
                places[i][1] = 18;
            }
        }
        places[k][0] = x;
        places[k][1] = y;
    } else if (b.brush == BrushType) {
        //(the type set apart from the tiles; "from the tiles": the tiles' again)
        const TileType want = b.typeBrush == 0 ? tilesType(b.cx, b.cy) : static_cast<TileType>(b.typeBrush);
        if (g_map->tileType(b.cx, b.cy) == want)
            return;
        g_map->setTileType(b.cx, b.cy, want);
        tilesChanged(b.cx, b.cy, true);
    } else if (b.brush == BrushSpawn) {
        //(no spawning here: everyone's (the teams' too, as the level editor marks it), a team's, the items')
        bool changed = false;
        for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
            const bool mark = b.spawnBrush == 0 ? t <= 4 : t == b.spawnBrush;
            if (mark && g_map->spawn(t, b.cx, b.cy)) {
                g_map->setNoSpawn(t, b.cx, b.cy, true);
                changed = true;
            }
        }
        if (!changed)
            return;
        g_map->refreshSpawnAreas();
    } else {
        TilesetTile* tile = g_map->tileAt(b.cx, b.cy, b.layer);
        if (tile->iID == b.tile.iID && tile->iCol == b.tile.iCol && tile->iRow == b.tile.iRow)
            return;
        const bool keep = typeSetApart(b.cx, b.cy);
        *tile = b.tile;
        tilesChanged(b.cx, b.cy, keep);
    }
    m.edited = true;
}

// Erasing: the block there, else the item, the hazard, the made platform, the warp, else the tile of
// the topmost layer that has one
void erase(CGM_MapMaker& m, const MapMakerBuilder& b)
{
    //(a mode object here: back where the level editor puts it; none placed of its kind: random again)
    if (b.brush == BrushModeObject) {
        bool changed = false;
        for (int kind = 0; kind < 2; kind++) {
            short (*places)[2] = kind == 0 ? m.goals : m.bases;
            short& count = kind == 0 ? m.goalCount : m.baseCount;
            bool placed = false;
            for (int i = 0; i < count; i++) {
                const short dx = static_cast<short>(kind == 0 ? i * 80 + 20 : i * 160 + 64), dy = 18;
                if (places[i][0] / TILESIZE == b.cx && places[i][1] / TILESIZE == b.cy && (places[i][0] != dx || places[i][1] != dy)) {
                    places[i][0] = dx;
                    places[i][1] = dy;
                    changed = true;
                }
                placed = placed || places[i][0] != dx || places[i][1] != dy;
            }
            if (count > 0 && !placed) {
                count = 0;
                changed = true;
            }
        }
        if (changed) {
            m.edited = true;
            return;
        }
        //(none of its kind here: what's there goes, as with any other brush)
    }
    //(the type brush: the type the tiles give; the spawn brush: spawning here again; else, nothing of
    //theirs here, what's there goes)
    if (b.brush == BrushType && typeSetApart(b.cx, b.cy)) {
        g_map->setTileType(b.cx, b.cy, tilesType(b.cx, b.cy));
        tilesChanged(b.cx, b.cy, true);
        m.edited = true;
        return;
    }
    if (b.brush == BrushSpawn) {
        bool changed = false;
        for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
            const bool mark = b.spawnBrush == 0 ? t <= 4 : t == b.spawnBrush;
            if (mark && !g_map->spawn(t, b.cx, b.cy)) {
                g_map->setNoSpawn(t, b.cx, b.cy, false);
                changed = true;
            }
        }
        if (changed) {
            g_map->refreshSpawnAreas();
            m.edited = true;
            return;
        }
    }
    MapBlock* block = g_map->blockat(b.cx, b.cy);
    if (block->iType != -1) {
        block->iType = -1;
        block->fHidden = false;
        SetMapBlockAt(b.cx, b.cy);
        g_map->refreshSpawnAreas();
        m.edited = true;
        return;
    }
    const int item = itemAt(m, b.cx, b.cy);
    if (item >= 0) {
        for (int k = item; k + 1 < m.itemCount; k++)
            m.items[k] = m.items[k + 1];
        m.itemCount--;
        RemoveMapItemObjectsAt(b.cx, b.cy);
        m.edited = true;
        return;
    }
    const int hazard = hazardAt(m, b.cx, b.cy);
    if (hazard >= 0) {
        for (int k = hazard; k + 1 < m.hazardCount; k++)
            m.hazards[k] = m.hazards[k + 1];
        m.hazardCount--;
        std::memset(static_cast<void*>(&m.hazards[m.hazardCount]), 0, sizeof(MapHazard));
        RespawnMapHazards(m.hazards, m.hazardCount);
        m.edited = true;
        return;
    }
    const int platform = platformAt(m, b.cx, b.cy);
    if (platform >= 0) {
        if (m.platforms[platform].live)
            m.platforms[platform].live->kill();    //(the map's update deletes it)
        for (int k = platform; k + 1 < m.platformCount; k++)
            m.platforms[k] = m.platforms[k + 1];
        m.platformCount--;
        std::memset(static_cast<void*>(&m.platforms[m.platformCount]), 0, sizeof(MapMakerPlatform));
        m.edited = true;
        return;
    }
    Warp* warp = g_map->warp(b.cx, b.cy);
    if (warp->connection != -1) {
        warp->connection = -1;
        warp->direction = WARP_UNDEFINED;
        g_map->recalcWarps();
        m.edited = true;
        return;
    }
    for (int layer = MAPLAYERS - 1; layer >= 0; layer--) {
        TilesetTile* tile = g_map->tileAt(b.cx, b.cy, layer);
        if (tile->iID == TILESETNONE)
            continue;
        const bool keep = typeSetApart(b.cx, b.cy);
        *tile = TilesetTile {TILESETNONE, 0, 0};
        tilesChanged(b.cx, b.cy, keep);
        m.edited = true;
        return;
    }
}

// ---- Undo

// The map's switch of a color: how it starts (the on/off blocks of that color made again)
void setSwitchStart(CGM_MapMaker& m, int color, short state)
{
    m.switches[color] = state;
    g_map->switches()[color] = state;
    for (short x = 0; x < MAPWIDTH; x++)
        for (short y = 0; y < MAPHEIGHT; y++)
            if (g_map->blockat(x, y)->iType == 7 + color)
                SetMapBlockAt(x, y);
}

// The map-wide things as they are, in an edit (x -1): the background, music, weather, switches, mode objects
void captureLevel(const CGM_MapMaker& m, MapMakerEdit& edit)
{
    edit.x = edit.y = -1;
    edit.background = m.background;
    edit.music = m.music;
    for (int k = 0; k < 3; k++)
        edit.eyecandy[k] = m.eyecandy[k];
    for (int k = 0; k < 4; k++)
        edit.switches[k] = m.switches[k];
    edit.goalCount = m.goalCount;
    edit.baseCount = m.baseCount;
    std::copy(&m.goals[0][0], &m.goals[0][0] + 16, &edit.goals[0][0]);
    std::copy(&m.bases[0][0], &m.bases[0][0] + 8, &edit.bases[0][0]);
}

void restoreLevel(CGM_MapMaker& m, const MapMakerEdit& edit)
{
    m.background = edit.background;
    m.music = edit.music;
    for (int k = 0; k < 3; k++)
        g_map->eyecandy[k] = m.eyecandy[k] = edit.eyecandy[k];
    for (int k = 0; k < 4; k++)
        if (m.switches[k] != edit.switches[k])
            setSwitchStart(m, k, edit.switches[k]);
    m.goalCount = edit.goalCount;
    m.baseCount = edit.baseCount;
    std::copy(&edit.goals[0][0], &edit.goals[0][0] + 16, &m.goals[0][0]);
    std::copy(&edit.bases[0][0], &edit.bases[0][0] + 8, &m.bases[0][0]);
}

// The cells just changed, each a moment's ring drawn round it (as Mario Maker's pieces pop in): put
// down (white), taken away (red), undone (yellow). Only drawn: not while the match is played again
struct CellFlash {
    short x, y;
    uint8_t kind;    // 0 put, 1 taken away, 2 undone
    Uint64 at;       // SDL_GetTicks()
};
std::vector<CellFlash> s_flashes;

void flashCell(int x, int y, int kind)
{
    if (sim::isResimulating() || x < 0 || y < 0 || s_flashes.size() >= 600)
        return;
    s_flashes.push_back({static_cast<short>(x), static_cast<short>(y), static_cast<uint8_t>(kind), SDL_GetTicks()});
}

const char* const TOOL_SHORT[TOOLS] = {"Pencil", "Rectangle", "Fill", "Copy", "Paste"};

// ---- The first-time hints: three, one after another, the first time anyone on this computer builds
// (painting, the parts palette, undo), each gone once it's been done; kept done in the settings folder
// ("mapmaker_tips.txt": a bit each)
constexpr int TIPS = 3;
int s_tipsDone = -1;    // (-1: not read yet)

std::string tipsPath()
{
    return GetHomeDirectory() + "mapmaker_tips.txt";
}

int tipsDone()
{
    if (s_tipsDone < 0) {
        s_tipsDone = 0;
        if (FILE* f = fopen(tipsPath().c_str(), "r")) {
            if (fscanf(f, "%d", &s_tipsDone) != 1)
                s_tipsDone = 0;
            fclose(f);
        }
    }
    return s_tipsDone;
}

void tipDone(int tip)
{
    if (sim::isResimulating() || (tipsDone() >> tip) & 1)
        return;
    s_tipsDone |= 1 << tip;
    if (FILE* f = fopen(tipsPath().c_str(), "w")) {
        fprintf(f, "%d\n", s_tipsDone);
        fclose(f);
    }
}

// A short message for this machine's builder, over its toolbar a moment (not while played again)
void toast(int slot, const std::string& text)
{
    if (sim::isResimulating() || !localHuman(slot))
        return;
    s_toast = text;
    s_toastAt = SDL_GetTicks();
}

// A sound for what a builder did this tick (Mario Maker's: every piece put down makes one): a piece
// put, taken away, undone, a brush taken. Not too often (a stroke drawn makes one every few ticks);
// not while the match is played again (a rollback)
void buildSound(const CGM_MapMaker& m, int slot, uint8_t byte, bool changed, bool undone, bool newBrush)
{
    static int lastTick[4] {-100, -100, -100, -100};
    if (sim::isResimulating() || !rm || slot < 0 || slot >= 4)
        return;
    sfxSound* sound = nullptr;
    if (undone)
        sound = &rm->sfx_spit;
    else if (changed && (mouseButtons(byte) & B_PAINT))
        sound = &rm->sfx_bump;
    else if (changed && (mouseButtons(byte) & B_ERASE))
        sound = &rm->sfx_kicksound;
    else if (newBrush && !m.builders[slot].palette)
        sound = &rm->sfx_inventory;
    if (!sound || (g_sim.tick - lastTick[slot] < 5 && g_sim.tick >= lastTick[slot]))
        return;
    lastTick[slot] = g_sim.tick;
    ifSoundOnPlay(*sound);
}

// ---- The recent brushes (the strip by the brush card): every brush taken goes first; the next and
// previous go round them
MapMakerRecent recentOf(const MapMakerBuilder& b)
{
    MapMakerRecent r {};
    r.brush = b.brush;
    switch (b.brush) {
    case BrushTile: r.a = b.tile.iID; r.b = b.tile.iCol; r.c = b.tile.iRow; break;
    case BrushBlock:
        r.a = b.block;
        r.flags = static_cast<uint8_t>((b.blockOn ? 1 : 0) | (b.blockHidden ? 2 : 0));
        r.c = b.contents;
        r.mix = b.mix;
        break;
    case BrushItem: r.a = b.item; break;
    case BrushWarp: r.a = b.warpConn; r.b = b.warpDir; break;
    case BrushHazard: r.a = b.hazard; break;
    case BrushPlatform: r.a = b.platformKind; break;
    case BrushType: r.a = b.typeBrush; break;
    case BrushSpawn: r.a = b.spawnBrush; break;
    default: r.a = b.modeObject; break;
    }
    return r;
}

bool sameRecent(const MapMakerRecent& x, const MapMakerRecent& y)
{
    return x.brush == y.brush && x.flags == y.flags && x.a == y.a && x.b == y.b && x.c == y.c && x.mix == y.mix;
}

void applyRecent(MapMakerBuilder& b, const MapMakerRecent& r)
{
    b.brush = r.brush;
    switch (r.brush) {
    case BrushTile: b.tile = TilesetTile {r.a, r.b, r.c}; break;
    case BrushBlock:
        b.block = r.a;
        b.blockOn = r.flags & 1;
        b.blockHidden = (r.flags & 2) != 0;
        b.contents = r.c;
        b.mix = r.mix;
        break;
    case BrushItem: b.item = r.a; break;
    case BrushWarp: b.warpConn = r.a; b.warpDir = r.b; break;
    case BrushHazard: b.hazard = r.a; break;
    case BrushPlatform: b.platformKind = r.a; break;
    case BrushType: b.typeBrush = r.a; break;
    case BrushSpawn: b.spawnBrush = r.a; break;
    default: b.modeObject = r.a; break;
    }
}

// The brush now first in the recent ones (taken from the palette, picked up, gone back to)
void noteRecent(MapMakerBuilder& b)
{
    const MapMakerRecent now = recentOf(b);
    if (b.recentCount > 0 && sameRecent(b.recent[0], now))
        return;
    int k = 0;
    while (k < b.recentCount && !sameRecent(b.recent[k], now))
        k++;
    if (k == b.recentCount && b.recentCount < MAP_MAKER_RECENT)
        b.recentCount++;
    k = std::min(k, MAP_MAKER_RECENT - 1);
    for (; k > 0; k--)
        b.recent[k] = b.recent[k - 1];
    b.recent[0] = now;
}

// The next (1) or previous (-1) of the recent brushes: they go round, the first is the brush
void stepRecent(MapMakerBuilder& b, int dir)
{
    if (b.recentCount < 2)
        return;
    if (dir > 0) {
        const MapMakerRecent first = b.recent[0];
        for (int k = 0; k + 1 < b.recentCount; k++)
            b.recent[k] = b.recent[k + 1];
        b.recent[b.recentCount - 1] = first;
    } else {
        const MapMakerRecent last = b.recent[b.recentCount - 1];
        for (int k = b.recentCount - 1; k > 0; k--)
            b.recent[k] = b.recent[k - 1];
        b.recent[0] = last;
    }
    applyRecent(b, b.recent[0]);
}

// A ? block's contents as the brush gives them (defaultBlockSettings's other way): the default
// weights, an item set's, one powerup, or a mix of those it can give
void blockContentsOf(const MapBlock& block, short& contents, uint32_t& mix)
{
    auto gives = [&](short c, uint32_t x) {
        MapBlock made {};
        made.iType = block.iType;
        defaultBlockSettings(made, c, x);
        return std::equal(std::begin(block.iSettings), std::end(block.iSettings), std::begin(made.iSettings));
    };
    mix = 0;
    contents = -1;
    if (gives(-1, 0))
        return;
    for (const short set : ITEM_SETS) {
        if (gives(static_cast<short>(-10 - set), 0)) {
            contents = static_cast<short>(-10 - set);
            return;
        }
    }
    int count = 0, one = 0;
    for (int i = 0; i < NUM_BLOCK_SETTINGS && i < 32; i++) {
        if (block.iSettings[i] > 0) {
            mix |= 1u << i;
            count++;
            one = i;
        }
    }
    if (count == 1) {
        contents = static_cast<short>(one);
        mix = 0;
    } else if (count > 1) {
        contents = -2;
    }
}

// (a setting from a hazard's or platform's number, within its range)
short settingFrom(const Setting& setting, float value)
{
    return static_cast<short>(std::clamp(static_cast<int>(value + 0.5f), static_cast<int>(setting.min), static_cast<int>(setting.max)));
}

// The eyedropper: the brush of what's under the cursor (what B would erase first: a block, an item, a
// hazard, a platform, a warp, the topmost tile, whose layer the brush goes on), with its settings (a ?
// block's contents, a fire bar's length, a platform's path...): painted, the same again. Nothing
// there: as it was (false)
bool pickBrush(const CGM_MapMaker& m, MapMakerBuilder& b)
{
    const short x = b.cx, y = b.cy;
    const MapBlock& block = *g_map->blockat(x, y);
    if (block.iType != -1) {
        b.brush = BrushBlock;
        b.block = block.iType;
        b.blockOn = true;
        b.blockHidden = block.fHidden;
        b.contents = -1;
        b.mix = 0;
        if (block.iType >= 7 && block.iType <= 10)
            b.blockOn = m.switches[block.iType - 7] != 0;
        else if (block.iType >= 11 && block.iType <= 14)
            b.blockOn = block.iSettings[0] != 0;
        else if (block.iType == 1 || block.iType == 15)
            blockContentsOf(block, b.contents, b.mix);
        return true;
    }
    if (const int item = itemAt(m, x, y); item >= 0) {
        b.brush = BrushItem;
        b.item = static_cast<short>(m.items[item].itype);
        return true;
    }
    if (const int k = hazardAt(m, x, y); k >= 0) {
        const MapHazard& h = m.hazards[k];
        const int type = static_cast<int>(h.itype) % HAZARD_TYPES;
        const Setting* set = HAZARD_SETTING[type];
        short* s = b.hazardSet[type];
        b.brush = BrushHazard;
        b.hazard = static_cast<short>(type);
        switch (h.itype) {
        case MapHazardType::FireBar:
            s[0] = settingFrom(set[0], h.iparam[0]);
            s[1] = settingFrom(set[1], std::fabs(h.dparam[0]) / 0.005f);
            s[2] = h.dparam[0] < 0 ? 1 : 0;
            break;
        case MapHazardType::RotoDisc:
            s[0] = settingFrom(set[0], h.iparam[0]);
            s[1] = settingFrom(set[1], h.dparam[2] / 16.0f);
            s[2] = settingFrom(set[2], std::fabs(h.dparam[0]) / 0.005f);
            s[3] = h.dparam[0] < 0 ? 1 : 0;
            break;
        case MapHazardType::BulletBill:
            s[0] = settingFrom(set[0], h.iparam[0] / 30.0f);
            s[1] = settingFrom(set[1], std::fabs(h.dparam[0]));
            s[2] = h.dparam[0] < 0 ? 1 : 0;
            break;
        default:
            s[0] = settingFrom(set[0], h.iparam[0] / 30.0f);
            s[1] = settingFrom(set[1], h.iparam[1]);
            break;
        }
        return true;
    }
    if (const int k = platformAt(m, x, y); k >= 0) {
        const MapMakerPlatform& p = m.platforms[k];
        const int kind = p.kind % PLATFORM_KINDS;
        short* s = b.platformSet[kind];
        b.brush = BrushPlatform;
        b.platformKind = static_cast<short>(kind);
        s[0] = p.w;
        s[1] = p.h;
        s[2] = p.copied;
        if (kind == 0) {
            s[3] = p.way;
            s[4] = p.dist;
            s[5] = p.speed;
        } else if (kind == 1) {
            s[3] = p.way;
            s[4] = p.speed;
        } else {
            s[3] = p.rx;
            s[4] = p.ry;
            s[5] = p.speed;
            s[6] = p.spin;
        }
        if (!p.copied)
            b.tile = p.tile;    //(what it's made of: the tile brush's)
        return true;
    }
    const Warp& warp = *g_map->warp(x, y);
    if (warp.connection != -1) {
        b.brush = BrushWarp;
        b.warpConn = warp.connection;
        b.warpDir = warp.direction;
        return true;
    }
    for (int layer = MAPLAYERS - 1; layer >= 0; layer--) {
        const TilesetTile& tile = *g_map->tileAt(x, y, layer);
        if (tile.iID == TILESETNONE)
            continue;
        b.brush = BrushTile;
        b.tile = tile;
        b.layer = static_cast<short>(layer);
        return true;
    }
    return false;
}

// ---- The history: every edit packed in a record, the oldest first; an undo takes back a builder's
// newest stroke, as far back as there are records (the oldest go past MAP_MAKER_HISTORY_BYTES).
// A record: u16 its size, u8 its kind (1 a cell or the level-wide things, 2 a rectangle, fill or a
// paste's layer), u8 the builder, i16 the stroke, then what was there before (only the parts it
// changed), and its size again (to walk back from the newest). A tick's edits are staged in full
// (m.edits, m.bulks) and packed at its end (commitStaged).
namespace history {
constexpr size_t HEAD = 6, TAIL = 2;

void put(sim::vector<uint8_t>& out, const void* p, size_t n)
{
    const auto* b = static_cast<const uint8_t*>(p);
    out.insert(out.end(), b, b + n);
}

template <typename T>
void putValue(sim::vector<uint8_t>& out, const T& v)
{
    put(out, &v, sizeof(T));
}

// (the bytes of a struct from one field to before another: the fields between)
template <typename A, typename B>
size_t span(const MapMakerEdit& e, const A& from, const B& to)
{
    return static_cast<size_t>(reinterpret_cast<const uint8_t*>(&to) - reinterpret_cast<const uint8_t*>(&from));
    (void)e;
}

void begin(sim::vector<uint8_t>& out, uint8_t kind, short slot, short stroke)
{
    const uint16_t size = 0;
    putValue(out, size);
    putValue(out, kind);
    putValue(out, static_cast<uint8_t>(slot));
    putValue(out, stroke);
}

void end(sim::vector<uint8_t>& out, size_t start)
{
    const uint16_t size = static_cast<uint16_t>(out.size() + TAIL - start);
    std::memcpy(&out[start], &size, sizeof(size));
    putValue(out, size);
}

void packEdit(sim::vector<uint8_t>& out, const MapMakerEdit& e)
{
    const size_t start = out.size();
    begin(out, 1, e.slot, e.stroke);
    putValue(out, e.x);
    putValue(out, e.y);
    if (e.x < 0) {
        //(the level-wide things: from the background to the end)
        put(out, &e.background, sizeof(MapMakerEdit) - span(e, e.slot, e.background));
    } else {
        //(the cell: its tiles to its hazard's mark; the hazard, the platform only when there's one)
        put(out, &e.tiles, span(e, e.tiles, e.hazard));
        if (e.hadHazard)
            putValue(out, e.hazard);
        putValue(out, e.platformKind);
        if (e.platformKind != 0) {
            MapMakerPlatform platform = e.platform;
            platform.live = nullptr;
            putValue(out, platform);
        }
    }
    end(out, start);
}

void unpackEdit(const uint8_t* p, MapMakerEdit& e)
{
    std::memset(static_cast<void*>(&e), 0, sizeof(e));
    auto get = [&p](void* to, size_t n) {
        std::memcpy(to, p, n);
        p += n;
    };
    p += HEAD;
    get(&e.x, sizeof(e.x));
    get(&e.y, sizeof(e.y));
    if (e.x < 0) {
        get(&e.background, sizeof(MapMakerEdit) - span(e, e.slot, e.background));
    } else {
        get(&e.tiles, span(e, e.tiles, e.hazard));
        if (e.hadHazard)
            get(&e.hazard, sizeof(e.hazard));
        get(&e.platformKind, sizeof(e.platformKind));
        if (e.platformKind != 0)
            get(&e.platform, sizeof(e.platform));
    }
}

void packBulk(sim::vector<uint8_t>& out, const MapMakerBulk& b)
{
    const size_t start = out.size();
    begin(out, 2, b.slot, b.stroke);
    putValue(out, b.layer);
    putValue(out, static_cast<uint8_t>(b.blocks ? 1 : 0));
    uint16_t count = 0;
    for (int cell = 0; cell < MAP_CELLS; cell++)
        count = static_cast<uint16_t>(count + ((b.changed[cell / 8] >> (cell % 8)) & 1));
    putValue(out, count);
    for (int cell = 0; cell < MAP_CELLS; cell++) {
        if (!((b.changed[cell / 8] >> (cell % 8)) & 1))
            continue;
        putValue(out, static_cast<uint16_t>(cell));
        if (b.blocks) {
            putValue(out, b.blockType[cell]);
        } else {
            putValue(out, b.tiles[cell]);
            putValue(out, b.types[cell]);
        }
    }
    end(out, start);
}

void unpackBulk(const uint8_t* p, MapMakerBulk& b)
{
    std::memset(static_cast<void*>(&b), 0, sizeof(b));
    auto get = [&p](void* to, size_t n) {
        std::memcpy(to, p, n);
        p += n;
    };
    p += HEAD;
    get(&b.layer, sizeof(b.layer));
    uint8_t blocks = 0;
    get(&blocks, 1);
    b.blocks = blocks != 0;
    uint16_t count = 0;
    get(&count, sizeof(count));
    for (int k = 0; k < count; k++) {
        uint16_t cell = 0;
        get(&cell, sizeof(cell));
        if (cell >= MAP_CELLS)
            break;
        b.changed[cell / 8] = static_cast<uint8_t>(b.changed[cell / 8] | (1 << (cell % 8)));
        if (b.blocks) {
            get(&b.blockType[cell], sizeof(b.blockType[cell]));
        } else {
            get(&b.tiles[cell], sizeof(b.tiles[cell]));
            get(&b.types[cell], sizeof(b.types[cell]));
        }
    }
}

uint16_t sizeAt(const sim::vector<uint8_t>& h, size_t at)
{
    uint16_t size = 0;
    std::memcpy(&size, &h[at], sizeof(size));
    return size;
}
} // namespace history

// The tick's staged edits packed into the history (the oldest records dropped past its size)
void dropRedo(CGM_MapMaker& m, int slot);

void commitStaged(CGM_MapMaker& m)
{
    //(a builder that changed the map: its redo gone)
    if (!m.redo.empty()) {
        bool changed[SLOTS] {};
        for (int k = 0; k < m.editCount; k++)
            changed[m.edits[k].slot & 3] = true;
        for (int k = 0; k < m.bulkCount; k++)
            changed[m.bulks[k].slot & 3] = true;
        for (int s = 0; s < SLOTS; s++)
            if (changed[s])
                dropRedo(m, s);
    }
    for (int k = 0; k < m.editCount; k++)
        history::packEdit(m.history, m.edits[k]);
    for (int k = 0; k < m.bulkCount; k++)
        history::packBulk(m.history, m.bulks[k]);
    if (m.editCount > 0)
        std::memset(static_cast<void*>(m.edits), 0, sizeof(m.edits));
    if (m.bulkCount > 0)
        std::memset(static_cast<void*>(m.bulks), 0, sizeof(m.bulks));
    m.editCount = 0;
    m.bulkCount = 0;
    size_t drop = 0;
    while (m.history.size() - drop > MAP_MAKER_HISTORY_BYTES)
        drop += history::sizeAt(m.history, drop);
    if (drop > 0)
        m.history.erase(m.history.begin(), m.history.begin() + static_cast<std::ptrdiff_t>(drop));
}

// What the builders can change, as one number (the test's: undone all the way, the map is as it began)
uint64_t builtMapHash(const CGM_MapMaker& m)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t k = 0; k < n; k++)
            h = (h ^ b[k]) * 1099511628211ull;
    };
    for (short x = 0; x < MAPWIDTH; x++) {
        for (short y = 0; y < MAPHEIGHT; y++) {
            for (int l = 0; l < MAPLAYERS; l++) {
                const TilesetTile& t = *g_map->tileAt(x, y, l);
                mix(&t.iID, sizeof(t.iID));
                mix(&t.iCol, sizeof(t.iCol));
                mix(&t.iRow, sizeof(t.iRow));
            }
            const MapBlock& b = *g_map->blockat(x, y);
            mix(&b.iType, sizeof(b.iType));
            mix(&b.fHidden, sizeof(b.fHidden));
            const Warp& w = *g_map->warp(x, y);
            mix(&w.connection, sizeof(w.connection));
            mix(&w.direction, sizeof(w.direction));
            const TileType type = g_map->tileType(x, y);
            mix(&type, sizeof(type));
            for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
                const bool spawn = g_map->spawn(t, x, y);
                mix(&spawn, sizeof(spawn));
            }
        }
    }
    //(the items as a set: an undo puts one back at the list's end)
    mix(&m.itemCount, sizeof(m.itemCount));
    std::vector<MapItem> items(m.items, m.items + m.itemCount);
    std::sort(items.begin(), items.end(), [](const MapItem& p, const MapItem& q) {
        return std::tie(p.iy, p.ix, p.itype) < std::tie(q.iy, q.ix, q.itype);
    });
    for (const MapItem& item : items) {
        mix(&item.itype, sizeof(item.itype));
        mix(&item.ix, sizeof(item.ix));
        mix(&item.iy, sizeof(item.iy));
    }
    mix(&m.hazardCount, sizeof(m.hazardCount));
    mix(&m.platformCount, sizeof(m.platformCount));
    for (int k = 0; k < m.platformCount; k++) {
        mix(&m.platforms[k].x, sizeof(m.platforms[k].x));
        mix(&m.platforms[k].y, sizeof(m.platforms[k].y));
    }
    mix(&m.background, sizeof(m.background));
    mix(&m.music, sizeof(m.music));
    mix(m.eyecandy, sizeof(m.eyecandy));
    mix(m.switches, sizeof(m.switches));
    mix(&m.goalCount, sizeof(m.goalCount));
    mix(&m.baseCount, sizeof(m.baseCount));
    return h;
}

// A new edit staged (the staged ones packed first when they're all used), zeroed: its padding too
MapMakerEdit& pushEdit(CGM_MapMaker& m, int slot, short stroke)
{
    if (m.editCount == MAP_MAKER_UNDO)
        commitStaged(m);
    MapMakerEdit& edit = m.edits[m.editCount++];
    std::memset(static_cast<void*>(&edit), 0, sizeof(edit));
    edit.slot = static_cast<short>(slot);
    edit.stroke = stroke;
    return edit;
}

// A cell as it is now (field by field: the padding stays zero)
void captureCell(const CGM_MapMaker& m, int x, int y, MapMakerEdit& edit)
{
    edit.x = static_cast<short>(x);
    edit.y = static_cast<short>(y);
    for (int l = 0; l < MAPLAYERS; l++)
        edit.tiles[l] = *g_map->tileAt(x, y, l);
    const MapBlock& block = *g_map->blockat(static_cast<short>(x), static_cast<short>(y));
    edit.block.iType = block.iType;
    std::copy(std::begin(block.iSettings), std::end(block.iSettings), std::begin(edit.block.iSettings));
    edit.block.fHidden = block.fHidden;
    const int item = itemAt(m, x, y);
    edit.itemType = static_cast<short>(item >= 0 ? static_cast<short>(m.items[item].itype) : -1);
    const Warp& warp = *g_map->warp(static_cast<short>(x), static_cast<short>(y));
    edit.warp.direction = warp.direction;
    edit.warp.connection = warp.connection;
    const int hazard = hazardAt(m, x, y);
    edit.tileType = static_cast<short>(g_map->tileType(static_cast<short>(x), static_cast<short>(y)));
    for (short t = 0; t < NUMSPAWNAREATYPES; t++)
        edit.noSpawn[t] = !g_map->spawn(t, static_cast<short>(x), static_cast<short>(y));
    edit.hadHazard = hazard >= 0;
    if (hazard >= 0)
        std::memcpy(&edit.hazard, &m.hazards[hazard], sizeof(MapHazard));
}

void copyPlatform(MapMakerPlatform& to, const MapMakerPlatform& from)
{
    to.tile = from.tile;
    to.x = from.x;
    to.y = from.y;
    to.w = from.w;
    to.kind = from.kind;
    to.way = from.way;
    to.dist = from.dist;
    to.speed = from.speed;
    to.rx = from.rx;
    to.ry = from.ry;
    to.spin = from.spin;
    to.h = from.h;
    to.copied = from.copied;
    std::copy(std::begin(from.cells), std::end(from.cells), std::begin(to.cells));
}

void removePlatform(CGM_MapMaker& m, int k)
{
    if (m.platforms[k].live)
        m.platforms[k].live->kill();    //(the map's update deletes it)
    for (int i = k; i + 1 < m.platformCount; i++)
        m.platforms[i] = m.platforms[i + 1];
    m.platformCount--;
    std::memset(static_cast<void*>(&m.platforms[m.platformCount]), 0, sizeof(MapMakerPlatform));
}

// The cell put back as the edit has it
// The platform a record made (platformKind 1), to take away: where the record's copy of it is (a
// redo's), else where the cell is (an undo's: the platform was made there)
int recordPlatform(const CGM_MapMaker& m, const MapMakerEdit& edit)
{
    const short px = edit.platform.w > 0 ? edit.platform.x : edit.x, py = edit.platform.w > 0 ? edit.platform.y : edit.y;
    for (int k = 0; k < m.platformCount; k++)
        if (m.platforms[k].x == px && m.platforms[k].y == py)
            return k;
    return -1;
}

void restoreCell(CGM_MapMaker& m, const MapMakerEdit& edit)
{
    const short x = edit.x, y = edit.y;
    flashCell(x, y, 2);
    bool tiles = false;
    for (int l = 0; l < MAPLAYERS; l++) {
        TilesetTile* tile = g_map->tileAt(x, y, l);
        if (tile->iID != edit.tiles[l].iID || tile->iCol != edit.tiles[l].iCol || tile->iRow != edit.tiles[l].iRow) {
            *tile = edit.tiles[l];
            tiles = true;
        }
    }
    //(its type as it was: the tiles' or set apart; its no-spawn marks)
    bool spawns = false;
    for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
        if (g_map->spawn(t, x, y) == edit.noSpawn[t]) {
            g_map->setNoSpawn(t, x, y, edit.noSpawn[t]);
            spawns = true;
        }
    }
    if (tiles || spawns || g_map->tileType(x, y) != static_cast<TileType>(edit.tileType)) {
        g_map->setTileType(x, y, static_cast<TileType>(edit.tileType));
        tilesChanged(x, y, true);
    }
    MapBlock* block = g_map->blockat(x, y);
    if (block->iType != edit.block.iType || block->fHidden != edit.block.fHidden
        || !std::equal(std::begin(block->iSettings), std::end(block->iSettings), std::begin(edit.block.iSettings))) {
        block->iType = edit.block.iType;
        block->fHidden = edit.block.fHidden;
        std::copy(std::begin(edit.block.iSettings), std::end(edit.block.iSettings), std::begin(block->iSettings));
        SetMapBlockAt(x, y);
        g_map->refreshSpawnAreas();
    }
    const int item = itemAt(m, x, y);
    const short itemNow = static_cast<short>(item >= 0 ? static_cast<short>(m.items[item].itype) : -1);
    if (itemNow != edit.itemType) {
        if (item >= 0) {
            for (int k = item; k + 1 < m.itemCount; k++)
                m.items[k] = m.items[k + 1];
            m.itemCount--;
            std::memset(static_cast<void*>(&m.items[m.itemCount]), 0, sizeof(MapItem));
            RemoveMapItemObjectsAt(x, y);
        }
        if (edit.itemType >= 0 && m.itemCount < MAXMAPITEMS) {
            MapItem& added = m.items[m.itemCount++];
            added.itype = static_cast<MapItemType>(edit.itemType);
            added.ix = x;
            added.iy = y;
            SpawnMapItem(added);
        }
    }
    Warp* warp = g_map->warp(x, y);
    if (warp->direction != edit.warp.direction || warp->connection != edit.warp.connection) {
        warp->direction = edit.warp.direction;
        warp->connection = edit.warp.connection;
        g_map->recalcWarps();
    }
    int hazard = hazardAt(m, x, y);
    const bool hazardSame = edit.hadHazard ? hazard >= 0 && std::memcmp(&m.hazards[hazard], &edit.hazard, sizeof(MapHazard)) == 0 : hazard < 0;
    if (!hazardSame) {
        if (!edit.hadHazard) {
            for (int k = hazard; k + 1 < m.hazardCount; k++)
                m.hazards[k] = m.hazards[k + 1];
            m.hazardCount--;
            std::memset(static_cast<void*>(&m.hazards[m.hazardCount]), 0, sizeof(MapHazard));
        } else {
            if (hazard < 0 && m.hazardCount < MAXMAPHAZARDS)
                hazard = m.hazardCount++;
            if (hazard >= 0)
                std::memcpy(&m.hazards[hazard], &edit.hazard, sizeof(MapHazard));
        }
        RespawnMapHazards(m.hazards, m.hazardCount);
    }
    if (edit.platformKind == 1) {
        const int k = recordPlatform(m, edit);
        if (k >= 0)
            removePlatform(m, k);
    } else if (edit.platformKind == 2 && m.platformCount < MADE_PLATFORMS) {
        MapMakerPlatform& p = m.platforms[m.platformCount++];
        copyPlatform(p, edit.platform);
        p.live = makePlatform(p);
    }
}

// A builder's last stroke taken back (its edits, the newest first)
bool bulkBit(const MapMakerBulk& bulk, int cell)
{
    return (bulk.changed[cell / 8] >> (cell % 8)) & 1;
}

// A rectangle or fill taken back: its cells as they were
void restoreBulk(CGM_MapMaker& m, const MapMakerBulk& bulk)
{
    bool tiles = false;
    for (int cell = 0; cell < MAP_CELLS; cell++) {
        if (!bulkBit(bulk, cell))
            continue;
        const short x = static_cast<short>(cell % MAPWIDTH), y = static_cast<short>(cell / MAPWIDTH);
        if (bulk.blocks) {
            MapBlock* block = g_map->blockat(x, y);
            MapBlock wanted {};
            wanted.iType = bulk.blockType[cell];
            defaultBlockSettings(wanted, -1);
            block->iType = wanted.iType;
            block->fHidden = false;
            std::copy(std::begin(wanted.iSettings), std::end(wanted.iSettings), std::begin(block->iSettings));
            SetMapBlockAt(x, y);
        } else {
            *g_map->tileAt(x, y, bulk.layer) = bulk.tiles[cell];
            g_map->setTileType(x, y, static_cast<TileType>(bulk.types[cell]));
            tiles = true;
        }
    }
    if (tiles)
        for (short x = 0; x < MAPWIDTH; x++)
            for (short y = 0; y < MAPHEIGHT; y++)
                g_map->UpdateTileGap(x, y);
    g_map->refreshSpawnAreas();
    m.edited = true;
}

// A record put back (its cells as they were), and the other way packed into `back` (what's there
// now, as the record would put it back): an undo's record goes in the redo, a redo's in the history
void applyRecord(CGM_MapMaker& m, const uint8_t* record, sim::vector<uint8_t>& back)
{
    //(its builder and stroke: in its head, not unpacked)
    const short slot = record[3];
    short stroke = 0;
    std::memcpy(&stroke, record + 4, sizeof(stroke));
    if (record[2] == 2) {
        static MapMakerBulk bulk, now;    //(big: not on the stack)
        history::unpackBulk(record, bulk);
        std::memset(static_cast<void*>(&now), 0, sizeof(now));
        now.slot = slot;
        now.stroke = stroke;
        now.layer = bulk.layer;
        now.blocks = bulk.blocks;
        std::copy(std::begin(bulk.changed), std::end(bulk.changed), std::begin(now.changed));
        for (int cell = 0; cell < MAP_CELLS; cell++) {
            if (!bulkBit(bulk, cell))
                continue;
            const short x = static_cast<short>(cell % MAPWIDTH), y = static_cast<short>(cell / MAPWIDTH);
            if (bulk.blocks) {
                now.blockType[cell] = g_map->blockat(x, y)->iType;
            } else {
                now.tiles[cell] = *g_map->tileAt(x, y, bulk.layer);
                now.types[cell] = static_cast<uint8_t>(g_map->tileType(x, y));
            }
        }
        history::packBulk(back, now);
        restoreBulk(m, bulk);
        return;
    }
    MapMakerEdit edit, now;
    history::unpackEdit(record, edit);
    std::memset(static_cast<void*>(&now), 0, sizeof(now));
    now.slot = slot;
    now.stroke = stroke;
    if (edit.x < 0) {
        captureLevel(m, now);
        history::packEdit(back, now);
        restoreLevel(m, edit);
        return;
    }
    captureCell(m, edit.x, edit.y, now);
    //(a platform it takes away: made again the other way; one it makes: taken away the other way)
    if (edit.platformKind == 1) {
        const int k = recordPlatform(m, edit);
        if (k >= 0) {
            now.platformKind = 2;
            copyPlatform(now.platform, m.platforms[k]);
        }
    } else if (edit.platformKind == 2) {
        now.platformKind = 1;
        copyPlatform(now.platform, edit.platform);
    }
    history::packEdit(back, now);
    restoreCell(m, edit);
}

// The records past MAP_MAKER_HISTORY_BYTES dropped, the oldest first
void trimRecords(sim::vector<uint8_t>& records)
{
    size_t drop = 0;
    while (records.size() - drop > MAP_MAKER_HISTORY_BYTES)
        drop += history::sizeAt(records, drop);
    if (drop > 0)
        records.erase(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(drop));
}

// A builder's newest stroke in `records`: false if it has none
bool newestStroke(const sim::vector<uint8_t>& records, int slot, short& stroke)
{
    for (size_t at = records.size(); at > 0;) {
        const size_t start = at - history::sizeAt(records, at - history::TAIL);
        if (records[start + 3] == static_cast<uint8_t>(slot)) {
            std::memcpy(&stroke, &records[start + 4], sizeof(stroke));
            return true;
        }
        at = start;
    }
    return false;
}

// A builder's newest stroke in `from` put back, its records the newest first, each the other way into
// `back` (an undo: from the history into the redo; a redo: the other way)
void takeBack(CGM_MapMaker& m, int slot, sim::vector<uint8_t>& from, sim::vector<uint8_t>& back)
{
    short stroke = 0;
    if (!newestStroke(from, slot, stroke))
        return;
    for (size_t at = from.size(); at > 0;) {
        const size_t start = at - history::sizeAt(from, at - history::TAIL);
        short recordStroke = 0;
        std::memcpy(&recordStroke, &from[start + 4], sizeof(recordStroke));
        if (from[start + 3] == static_cast<uint8_t>(slot) && recordStroke == stroke) {
            applyRecord(m, &from[start], back);
            from.erase(from.begin() + static_cast<std::ptrdiff_t>(start), from.begin() + static_cast<std::ptrdiff_t>(at));
        }
        at = start;
    }
    trimRecords(back);
    m.edited = true;
}

// A builder's redo dropped (it changed the map: what it undid can't come back over it)
void dropRedo(CGM_MapMaker& m, int slot)
{
    sim::vector<uint8_t>& r = m.redo;
    for (size_t at = 0; at < r.size();) {
        const size_t n = history::sizeAt(r, at);
        if (r[at + 3] == static_cast<uint8_t>(slot))
            r.erase(r.begin() + static_cast<std::ptrdiff_t>(at), r.begin() + static_cast<std::ptrdiff_t>(at + n));
        else
            at += n;
    }
}

// A builder's newest stroke taken back (in the history, as far back as it goes); kept to redo
void undo(CGM_MapMaker& m, int slot)
{
    commitStaged(m);
    takeBack(m, slot, m.history, m.redo);
}

// What a builder's last undo took back, done again (back in the history, to undo again)
void redo(CGM_MapMaker& m, int slot)
{
    commitStaged(m);
    takeBack(m, slot, m.redo, m.history);
}

// The map cleared (the host's, from the pause menu: Mario Maker's reset rocket): everything placed goes,
// tiles, blocks, items, hazards, platforms, warps, types and no-spawn marks (not the background, the
// music, the weather); one stroke, so an undo brings it all back
void clearMap(CGM_MapMaker& m, int slot, short stroke)
{
    //(the platforms: each one's taken away, to be made again by the undo)
    while (m.platformCount > 0) {
        const int k = m.platformCount - 1;
        MapMakerEdit& edit = pushEdit(m, slot, stroke);
        captureCell(m, m.platforms[k].x, m.platforms[k].y, edit);
        edit.platformKind = 2;
        copyPlatform(edit.platform, m.platforms[k]);
        removePlatform(m, k);
    }
    MapMakerBuilder eraser {};
    eraser.brush = BrushTile;
    for (short y = 0; y < MAPHEIGHT; y++) {
        for (short x = 0; x < MAPWIDTH; x++) {
            bool any = g_map->blockat(x, y)->iType != -1 || itemAt(m, x, y) >= 0 || hazardAt(m, x, y) >= 0 ||
                       g_map->warp(x, y)->connection != -1 || typeSetApart(x, y);
            for (int l = 0; l < MAPLAYERS && !any; l++)
                any = g_map->tileAt(x, y, l)->iID != TILESETNONE;
            for (short t = 0; t < NUMSPAWNAREATYPES && !any; t++)
                any = !g_map->spawn(t, x, y);
            if (!any)
                continue;
            MapMakerEdit& edit = pushEdit(m, slot, stroke);
            captureCell(m, x, y, edit);
            flashCell(x, y, 1);
            //(what's there, a thing at a time, as B takes them; then its type and spawn marks)
            eraser.cx = x;
            eraser.cy = y;
            for (int k = 0; k < 12; k++)
                erase(m, eraser);
            if (typeSetApart(x, y)) {
                g_map->setTileType(x, y, tilesType(x, y));
                tilesChanged(x, y, true);
            }
            bool spawns = false;
            for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
                if (!g_map->spawn(t, x, y)) {
                    g_map->setNoSpawn(t, x, y, false);
                    spawns = true;
                }
            }
            if (spawns)
                g_map->refreshSpawnAreas();
        }
    }
    m.edited = true;
}

// A new rectangle or fill staged (the staged ones packed first when they're all used), zeroed
MapMakerBulk& pushBulk(CGM_MapMaker& m, int slot, short stroke, short layer, bool blocks)
{
    if (m.bulkCount == MAP_MAKER_BULK)
        commitStaged(m);
    MapMakerBulk& bulk = m.bulks[m.bulkCount++];
    std::memset(static_cast<void*>(&bulk), 0, sizeof(bulk));
    bulk.slot = static_cast<short>(slot);
    bulk.stroke = stroke;
    bulk.layer = layer;
    bulk.blocks = blocks;
    return bulk;
}

// Cells (`cells`: x + y * MAPWIDTH, `n` of them) painted with the brush's tile (or block) or erased, on
// the brush's layer; one undo takes them all back
void paintCells(CGM_MapMaker& m, const MapMakerBuilder& b, int slot, const short* cells, int n, bool erase)
{
    const bool blocks = b.brush == BrushBlock;
    MapMakerBulk* bulk = nullptr;
    bool tiles = false;
    for (int k = 0; k < n; k++) {
        const int cell = cells[k];
        const short x = static_cast<short>(cell % MAPWIDTH), y = static_cast<short>(cell / MAPWIDTH);
        if (blocks) {
            MapBlock* block = g_map->blockat(x, y);
            const short type = erase ? static_cast<short>(-1) : b.block;
            if (block->iType == type && !block->fHidden)
                continue;
            if (!bulk)
                bulk = &pushBulk(m, slot, b.stroke, b.layer, true);
            bulk->blockType[cell] = block->iType;
            bulk->changed[cell / 8] = static_cast<uint8_t>(bulk->changed[cell / 8] | (1 << (cell % 8)));
            MapBlock wanted {};
            wanted.iType = type;
            defaultBlockSettings(wanted, b.contents, b.mix);
            block->iType = type;
            block->fHidden = false;
            std::copy(std::begin(wanted.iSettings), std::end(wanted.iSettings), std::begin(block->iSettings));
            SetMapBlockAt(x, y);
        } else {
            TilesetTile* tile = g_map->tileAt(x, y, b.layer);
            const TilesetTile want = erase ? TilesetTile {TILESETNONE, 0, 0} : b.tile;
            if (tile->iID == want.iID && tile->iCol == want.iCol && tile->iRow == want.iRow)
                continue;
            if (!bulk)
                bulk = &pushBulk(m, slot, b.stroke, b.layer, false);
            bulk->tiles[cell] = *tile;
            bulk->types[cell] = static_cast<uint8_t>(g_map->tileType(x, y));
            bulk->changed[cell / 8] = static_cast<uint8_t>(bulk->changed[cell / 8] | (1 << (cell % 8)));
            const bool keep = typeSetApart(x, y);
            *tile = want;
            if (!keep)
                g_map->setTileType(x, y, tilesType(x, y));
            tiles = true;
        }
    }
    if (!bulk)
        return;
    if (tiles)
        for (short x = 0; x < MAPWIDTH; x++)
            for (short y = 0; y < MAPHEIGHT; y++)
                g_map->UpdateTileGap(x, y);
    g_map->refreshSpawnAreas();
    m.edited = true;
}

// Copy: the rectangle from the anchor to the cursor (from its top left, CLIP_W by CLIP_H at most), its
// layers' tiles and its blocks
void copyRectangle(MapMakerBuilder& b)
{
    const int x0 = std::min(b.ax, b.cx), y0 = std::min(b.ay, b.cy);
    b.clipW = static_cast<short>(std::min(std::abs(b.ax - b.cx) + 1, CLIP_W));
    b.clipH = static_cast<short>(std::min(std::abs(b.ay - b.cy) + 1, CLIP_H));
    for (int i = 0; i < b.clipW * b.clipH; i++) {
        const short x = static_cast<short>(x0 + i % b.clipW), y = static_cast<short>(y0 + i / b.clipW);
        for (short l = 0; l < MAPLAYERS; l++)
            b.clipTiles[l][i] = *g_map->tileAt(x, y, l);
        b.clipBlocks[i] = g_map->blockat(x, y)->iType;
    }
}

// Paste: what was copied, its top left at the cursor (its empty tiles leave the map's; its blocks set);
// a bulk a layer (and one for the blocks), one stroke: one undo
void pasteClip(CGM_MapMaker& m, const MapMakerBuilder& b, int slot)
{
    bool tiles = false, any = false;
    for (short l = 0; l < MAPLAYERS; l++) {
        MapMakerBulk* bulk = nullptr;
        for (int i = 0; i < b.clipW * b.clipH; i++) {
            const int x = b.cx + i % b.clipW, y = b.cy + i / b.clipW;
            const TilesetTile want = b.clipTiles[l][i];
            if (x >= MAPWIDTH || y >= MAPHEIGHT || want.iID == TILESETNONE)
                continue;
            TilesetTile* tile = g_map->tileAt(static_cast<short>(x), static_cast<short>(y), l);
            if (tile->iID == want.iID && tile->iCol == want.iCol && tile->iRow == want.iRow)
                continue;
            if (!bulk)
                bulk = &pushBulk(m, slot, b.stroke, l, false);
            const int cell = x + y * MAPWIDTH;
            bulk->tiles[cell] = *tile;
            bulk->types[cell] = static_cast<uint8_t>(g_map->tileType(static_cast<short>(x), static_cast<short>(y)));
            bulk->changed[cell / 8] = static_cast<uint8_t>(bulk->changed[cell / 8] | (1 << (cell % 8)));
            const bool keep = typeSetApart(x, y);
            *tile = want;
            if (!keep)
                g_map->setTileType(static_cast<short>(x), static_cast<short>(y), tilesType(x, y));
            tiles = any = true;
        }
    }
    MapMakerBulk* blocks = nullptr;
    for (int i = 0; i < b.clipW * b.clipH; i++) {
        const int x = b.cx + i % b.clipW, y = b.cy + i / b.clipW;
        const short type = b.clipBlocks[i];
        if (x >= MAPWIDTH || y >= MAPHEIGHT || type < 0)
            continue;
        MapBlock* block = g_map->blockat(static_cast<short>(x), static_cast<short>(y));
        if (block->iType == type)
            continue;
        if (!blocks)
            blocks = &pushBulk(m, slot, b.stroke, b.layer, true);
        const int cell = x + y * MAPWIDTH;
        blocks->blockType[cell] = block->iType;
        blocks->changed[cell / 8] = static_cast<uint8_t>(blocks->changed[cell / 8] | (1 << (cell % 8)));
        MapBlock wanted {};
        wanted.iType = type;
        defaultBlockSettings(wanted, -1);
        block->iType = type;
        block->fHidden = false;
        std::copy(std::begin(wanted.iSettings), std::end(wanted.iSettings), std::begin(block->iSettings));
        SetMapBlockAt(static_cast<short>(x), static_cast<short>(y));
        any = true;
    }
    if (!any)
        return;
    if (tiles)
        for (short x = 0; x < MAPWIDTH; x++)
            for (short y = 0; y < MAPHEIGHT; y++)
                g_map->UpdateTileGap(x, y);
    g_map->refreshSpawnAreas();
    m.edited = true;
}

// The rectangle from the anchor to the cursor
void paintRectangle(CGM_MapMaker& m, const MapMakerBuilder& b, int slot)
{
    short cells[MAP_CELLS];
    int n = 0;
    for (int y = std::min(b.ay, b.cy); y <= std::max(b.ay, b.cy); y++)
        for (int x = std::min(b.ax, b.cx); x <= std::max(b.ax, b.cx); x++)
            cells[n++] = static_cast<short>(x + y * MAPWIDTH);
    paintCells(m, b, slot, cells, n, b.anchorErase);
}

// The cells around the cursor's with its tile (on the brush's layer, side by side), filled with the
// brush's tile or erased
void fillAround(CGM_MapMaker& m, const MapMakerBuilder& b, int slot, bool erase)
{
    const TilesetTile start = *g_map->tileAt(b.cx, b.cy, b.layer);
    auto same = [&](int x, int y) {
        const TilesetTile* t = g_map->tileAt(x, y, b.layer);
        return t->iID == start.iID && t->iCol == start.iCol && t->iRow == start.iRow;
    };
    bool seen[MAP_CELLS] {};
    short cells[MAP_CELLS];
    short stack[MAP_CELLS];
    int n = 0, top = 0;
    stack[top++] = static_cast<short>(b.cx + b.cy * MAPWIDTH);
    seen[stack[0]] = true;
    while (top > 0) {
        const short cell = stack[--top];
        cells[n++] = cell;
        const int x = cell % MAPWIDTH, y = cell / MAPWIDTH;
        const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || nx[k] >= MAPWIDTH || ny[k] < 0 || ny[k] >= MAPHEIGHT)
                continue;
            const short next = static_cast<short>(nx[k] + ny[k] * MAPWIDTH);
            if (!seen[next] && same(nx[k], ny[k])) {
                seen[next] = true;
                stack[top++] = next;
            }
        }
    }
    paintCells(m, b, slot, cells, n, erase);
}

// A builder's paint or erase at its cursor, kept to undo when it changed something
void editCell(CGM_MapMaker& m, MapMakerBuilder& b, int slot, bool painting)
{
    MapMakerEdit before;
    std::memset(static_cast<void*>(&before), 0, sizeof(before));
    captureCell(m, b.cx, b.cy, before);
    const int platformBefore = platformAt(m, b.cx, b.cy);
    MapMakerPlatform taken;
    std::memset(static_cast<void*>(&taken), 0, sizeof(taken));
    if (platformBefore >= 0)
        copyPlatform(taken, m.platforms[platformBefore]);
    const short platforms = m.platformCount;

    MapMakerEdit level;
    std::memset(static_cast<void*>(&level), 0, sizeof(level));
    captureLevel(m, level);

    const bool wasEdited = m.edited;
    m.edited = false;
    if (painting)
        paint(m, b);
    else
        erase(m, b);
    const bool changed = m.edited;
    m.edited = wasEdited || changed;
    if (!changed)
        return;

    MapMakerEdit after;
    std::memset(static_cast<void*>(&after), 0, sizeof(after));
    captureLevel(m, after);
    if (std::memcmp(&level, &after, sizeof(level)) != 0) {
        MapMakerEdit& was = pushEdit(m, slot, b.stroke);
        level.slot = was.slot;
        level.stroke = was.stroke;
        std::memcpy(static_cast<void*>(&was), &level, sizeof(level));
    }

    MapMakerEdit& edit = pushEdit(m, slot, b.stroke);
    before.slot = edit.slot;
    before.stroke = edit.stroke;
    std::memcpy(static_cast<void*>(&edit), &before, sizeof(before));    //(the padding too: zero)
    if (m.platformCount > platforms) {
        edit.platformKind = 1;
    } else if (m.platformCount < platforms) {
        edit.platformKind = 2;
        copyPlatform(edit.platform, taken);
    }
}

void openPalette(MapMakerBuilder& b)
{
    b.palette = true;
    b.adjusting = false;
    if (b.brush == BrushType || b.brush == BrushSpawn) {
        b.page = pageOf(b.brush == BrushType ? PageTypes : PageSpawns);
        b.px = b.brush == BrushType ? b.typeBrush : b.spawnBrush;
        b.py = b.scrollX = b.scrollY = 0;
        return;
    }
    //(on the brush: its tileset and tile, the blocks and the block, the items and the item)
    if (b.brush == BrushHazard) {
        b.page = pageOf(PageHazards);
        b.px = b.hazard;
        b.py = 0;
    } else if (b.brush == BrushPlatform) {
        b.page = pageOf(PagePlatforms);
        b.px = b.platformKind;
        b.py = 0;
    } else if (b.brush == BrushWarp) {
        b.page = pageOf(PageWarps);
        b.px = b.warpConn;
        b.py = b.warpDir;
    } else if (b.brush == BrushItem) {
        b.page = pageOf(PageItems);
        b.px = b.item;
        b.py = 0;
    } else if (b.brush == BrushTile && b.tile.iID == TILESETANIMATED) {
        b.page = pageOf(PageAnimated);
        b.px = b.tile.iRow;
        b.py = b.tile.iCol;
    } else if (b.brush == BrushModeObject) {
        b.page = pageOf(PageModeObjects);
        b.px = static_cast<short>(b.modeObject % 8);
        b.py = static_cast<short>(b.modeObject >= 8 ? 1 : 0);
    } else if (b.brush == BrushBlock || b.tile.iID < 0) {
        b.page = pageOf(PageBlocks);
        b.px = b.py = 0;
        if (b.brush == BrushBlock)
            cellOfBlock(b.block, b.blockOn, b.px, b.py);
    } else {
        b.page = b.tile.iID;
        b.px = b.tile.iCol;
        b.py = b.tile.iRow;
    }
    b.scrollX = static_cast<short>(std::max(0, b.px - MAPWIDTH / 2));
    b.scrollY = static_cast<short>(std::max(0, b.py - 1));
}

// One tick of a builder, from its slot's byte (and the previous tick's: what was just pressed)
void builderTick(CGM_MapMaker& m, MapMakerBuilder& b, uint8_t byte, uint8_t prev)
{
    const int slot = static_cast<int>(&b - m.builders);
    //The undo button: the builder's last stroke taken back (in the palette too; a setting being changed
    //stays so)
    if (byte == B_UNDO) {
        if (prev != B_UNDO)
            undo(m, slot);
        return;
    }
    //The mouse's jump: the cursor to the mouse's column or row, at once (on the map)
    if (jumpByte(byte)) {
        const int jump = jumpOf(byte);
        if (!b.palette && byte != prev) {
            if (jump >= JUMP_LAYER_AT) {
                b.layer = static_cast<short>(jump - JUMP_LAYER_AT);
            } else if (jump >= JUMP_TOOL_AT) {
                b.tool = static_cast<short>(jump - JUMP_TOOL_AT);
                b.anchoring = false;
            }
        }
        if (!b.palette && jump < JUMP_TOOL_AT) {
            if (jump < JUMP_ROWS_AT)
                b.cx = static_cast<short>(jump);
            else
                b.cy = static_cast<short>(jump - JUMP_ROWS_AT);
        }
        return;
    }
    //The mouse's: a tab; a step, with A or B held (then read as the keys' A and B)
    const bool mouse = mouseCommand(byte);
    if (mouse && (byte >> 2) >= MOUSE_TAB && (byte >> 2) < CMD_PICK) {
        if (b.palette && !b.adjusting)
            openTab(b, (byte >> 2) - MOUSE_TAB);
        return;
    }
    //(the eyedropper, the recent brushes: on the map, as it's pressed)
    if (mouse && (byte >> 2) >= CMD_PICK) {
        if ((byte >> 2) == CMD_REDO) {
            if (byte != prev)
                redo(m, slot);
        } else if (!b.palette && byte != prev) {
            if ((byte >> 2) == CMD_TOOL) {
                //(pencil, rectangle, fill, round; from copy or paste: the pencil)
                b.tool = static_cast<short>(b.tool >= 2 ? 0 : b.tool + 1);
                b.anchoring = false;
            } else if ((byte >> 2) == CMD_PICK) {
                pickBrush(m, b);
            } else if ((byte >> 2) == CMD_GRAB) {
                //(pick up, as Mario Maker's: the brush of what's there, and it's taken off the map (one
                //stroke: undone, back); painted, it's put down there)
                if (pickBrush(m, b)) {
                    b.stroke++;
                    editCell(m, b, slot, false);
                }
            }
            else if ((byte >> 2) == CMD_RECENT_NEXT || (byte >> 2) == CMD_RECENT_PREV)
                stepRecent(b, (byte >> 2) == CMD_RECENT_NEXT ? 1 : -1);
        }
        return;
    }
    int mdx = 0, mdy = 0;
    if (mouse) {
        const int q = (byte >> 2) - MOUSE_STEP;
        mdx = q % 3 - 1;
        mdy = (q / 3) % 3 - 1;
        byte = mouseButtons(byte);
    }
    prev = mouseButtons(prev);
    auto pressed = [&](uint8_t bit) { return (byte & bit) && !(prev & bit); };
    const bool held = byte & (B_LEFT | B_RIGHT | B_UP | B_DOWN);
    b.holdFrames = held ? static_cast<short>(std::min(b.holdFrames + 1, 1000)) : 0;
    //(held: again after a moment, every 3 ticks; held longer, faster: every 2, then every tick)
    const int every = b.holdFrames > 90 ? 1 : b.holdFrames > 45 ? 2 : 3;
    auto stepped = [&](uint8_t bit) { return pressed(bit) || ((byte & bit) && b.holdFrames > 14 && b.holdFrames % every == 0); };

    if (b.palette) {
        int w = 1, h = 1;
        pageSize(b, w, h);
        //(the hazards' and platforms' settings, rows under the types: A, then left/right, then A)
        const bool settings = (onHazardsPage(b) || onPlatformsPage(b)) && b.py > 0;
        if (b.adjusting) {
            const bool hazards = onHazardsPage(b);
            const int type = b.px % (hazards ? HAZARD_TYPES : PLATFORM_KINDS);
            const Setting& setting = hazards ? HAZARD_SETTING[type][b.py - 1] : PLATFORM_SETTING[type][b.py - 1];
            short& value = hazards ? b.hazardSet[type][b.py - 1] : b.platformSet[type][b.py - 1];
            if (stepped(B_LEFT))
                value = stepSetting(setting, value, -1);
            if (stepped(B_RIGHT))
                value = stepSetting(setting, value, 1);
            if (pressed(B_PAINT) || pressed(B_ERASE))
                b.adjusting = false;
            return;
        }
        if (mouse)
            mouseMove(b, mdx, mdy);
        if (stepped(B_LEFT) && !settings)
            b.px = static_cast<short>((b.px + w - 1) % w);
        if (stepped(B_RIGHT) && !settings)
            b.px = static_cast<short>((b.px + 1) % w);
        if (stepped(B_UP))
            b.py = static_cast<short>((b.py + h - 1) % h);
        if (stepped(B_DOWN))
            b.py = static_cast<short>((b.py + 1) % h);
        if (onContentsPage(b) || onPage(b, PageBlocks) || onModeObjectsPage(b)) {
            pageSize(b, w, h);
            b.px = static_cast<short>(std::min<int>(b.px, w - 1));
        }
        //(Y on the Blocks page: the blocks hidden, or not)
        if (pressed(B_LAYER) && onPage(b, PageBlocks))
            b.blockHidden = !b.blockHidden;
        b.scrollX = static_cast<short>(std::clamp<int>(b.scrollX, std::max(0, b.px - MAPWIDTH + 1), b.px));
        b.scrollY = static_cast<short>(std::clamp<int>(b.scrollY, std::max(0, b.py - STRIP_ROWS + 1), b.py));
        //X: the next page (the tilesets, the blocks, the items); A takes; B goes back
        //X: the next tab (into the tiles: the brush's tileset); Y on the tiles: the next tileset
        const int tilesets = static_cast<int>(tilesetCount());
        if (pressed(B_PALETTE)) {
            openTab(b, (tabOfPage(b.page) + 1) % CYCLE_TABS);
        } else if (pressed(B_LAYER) && b.page < tilesets && tilesets > 0) {
            b.page = static_cast<short>((b.page + 1) % tilesets);
            b.px = b.py = b.scrollX = b.scrollY = 0;
        }
        if (pressed(B_PAINT) && settings) {
            //(changing a setting: the brush is that hazard or platform, the palette stays)
            b.adjusting = true;
            if (onHazardsPage(b)) {
                b.brush = BrushHazard;
                b.hazard = b.px;
            } else {
                b.brush = BrushPlatform;
                b.platformKind = b.px;
            }
        } else if (pressed(B_PAINT) && onContentsPage(b) && b.py == 1) {
            //(a powerup in the mix or out of it: one alone is that one; the brush a ? block of it)
            const short powerup = POWERUP_ORDER[b.px % NUM_POWERUPS];
            b.mix ^= 1u << powerup;
            b.brush = BrushBlock;
            b.block = 1;
            int count = 0, only = -1;
            for (int k = 0; k < NUM_POWERUPS; k++) {
                if ((b.mix >> k) & 1) {
                    count++;
                    only = k;
                }
            }
            b.contents = static_cast<short>(count == 0 ? -1 : count == 1 ? only : -2);
        } else if (pressed(B_PAINT) && onModeObjectsPage(b)) {
            b.brush = BrushModeObject;
            b.modeObject = static_cast<short>(b.py == 0 ? b.px : 8 + b.px);
            b.palette = false;
        } else if (pressed(B_PAINT) && onWeatherPage(b)) {
            //(the weather of a layer: on or off, at once; it can be undone)
            MapMakerEdit& edit = pushEdit(m, slot, ++b.stroke);
            captureLevel(m, edit);
            m.eyecandy[b.py % 3] = static_cast<short>(m.eyecandy[b.py % 3] ^ (1 << (b.px % WEATHERS)));
            for (int k = 0; k < 3; k++)
                g_map->eyecandy[k] = m.eyecandy[k];    //(the rain's steps read it in play)
            m.edited = true;
        } else if (pressed(B_PAINT) && onAnimatedPage(b)) {
            b.brush = BrushTile;
            b.tile = TilesetTile {TILESETANIMATED, b.py, b.px};    //(its row, its column: the animations' order)
            b.palette = false;
        } else if (pressed(B_PAINT) && (onTypesPage(b) || onSpawnsPage(b))) {
            b.brush = onTypesPage(b) ? BrushType : BrushSpawn;
            (onTypesPage(b) ? b.typeBrush : b.spawnBrush) = b.px;
            b.palette = false;
        } else if (pressed(B_PAINT) && onToolsPage(b)) {
            b.tool = b.px;
            b.anchoring = false;
            b.palette = false;
        } else if (pressed(B_PAINT)) {
            if (onBackgroundsPage(b) || onMusicPage(b)) {
                //(the level's background and music: not a brush; the choice can be undone)
                MapMakerEdit& edit = pushEdit(m, slot, ++b.stroke);
                captureLevel(m, edit);
                if (onBackgroundsPage(b))
                    m.background = b.px;
                else
                    m.music = b.px;
                m.edited = true;
            } else if (onBlocksPage(b)) {
                b.brush = BrushBlock;
                b.block = blockOfCell(b.px, b.py, b.blockOn);
                b.contents = -1;
            } else if (onHazardsPage(b)) {
                b.brush = BrushHazard;
                b.hazard = b.px;
            } else if (onPlatformsPage(b)) {
                b.brush = BrushPlatform;
                b.platformKind = b.px;
            } else if (onWarpsPage(b)) {
                b.brush = BrushWarp;
                b.warpConn = b.px;
                b.warpDir = b.py;
            } else if (onContentsPage(b)) {
                //(an item set: the mix forgotten)
                b.brush = BrushBlock;
                b.block = 1;    //(a ? block)
                b.contents = static_cast<short>(b.px == 0 ? -1 : -10 - ITEM_SETS[b.px - 1]);
                b.mix = 0;
            } else if (onItemsPage(b)) {
                b.brush = BrushItem;
                b.item = b.px;
            } else {
                b.brush = BrushTile;
                b.tile.iID = b.page;
                b.tile.iCol = b.px;
                b.tile.iRow = b.py;
            }
            b.palette = false;
        } else if (pressed(B_ERASE)) {
            b.palette = false;
        }
        return;
    }
    if (mouse)
        mouseMove(b, mdx, mdy);
    if (stepped(B_LEFT))
        b.cx = static_cast<short>((b.cx + MAPWIDTH - 1) % MAPWIDTH);
    if (stepped(B_RIGHT))
        b.cx = static_cast<short>((b.cx + 1) % MAPWIDTH);
    if (stepped(B_UP))
        b.cy = static_cast<short>((b.cy + MAPHEIGHT - 1) % MAPHEIGHT);
    if (stepped(B_DOWN))
        b.cy = static_cast<short>((b.cy + 1) % MAPHEIGHT);
    //A paints, B erases: held, along the way the cursor goes (a stroke: an undo takes it back whole). The
    //rectangle (tiles, blocks): from where A or B went down to where it comes up; the fill (tiles): at once
    if (pressed(B_PAINT) || pressed(B_ERASE))
        b.stroke++;
    const bool shapes = b.brush == BrushTile || b.brush == BrushBlock;
    if (b.anchoring) {
        if (!(byte & (b.anchorErase ? B_ERASE : B_PAINT))) {
            if (b.tool == 3)
                copyRectangle(b);
            else
                paintRectangle(m, b, slot);
            b.anchoring = false;
        }
    } else if (b.tool == 3 && pressed(B_PAINT)) {
        b.anchoring = true;
        b.anchorErase = false;
        b.ax = b.cx;
        b.ay = b.cy;
    } else if (b.tool == 4 && pressed(B_PAINT)) {
        if (b.clipW > 0)
            pasteClip(m, b, slot);
    } else if (b.tool >= 3) {
        //(copy and paste: the press, not the hold)
    } else if (b.tool == 1 && shapes && (pressed(B_PAINT) || pressed(B_ERASE))) {
        b.anchoring = true;
        b.anchorErase = !pressed(B_PAINT);
        b.ax = b.cx;
        b.ay = b.cy;
    } else if (b.tool == 2 && b.brush == BrushTile && (pressed(B_PAINT) || pressed(B_ERASE))) {
        fillAround(m, b, slot, !pressed(B_PAINT));
    } else if (b.tool != 0 && shapes) {
        //(the rectangle's and the fill's: the press, not the hold)
    } else if (byte & B_PAINT) {
        editCell(m, b, slot, true);
    } else if (byte & B_ERASE) {
        editCell(m, b, slot, false);
    }
    if (pressed(B_PALETTE))
        openPalette(b);
    //Y: the layer the tiles go on (1, 2 behind the players; 3, 4 in front of them)
    if (pressed(B_LAYER))
        b.layer = static_cast<short>((b.layer + 1) % MAPLAYERS);
}

// Building starts (the cursor where the character is, if it's in play) or ends (`atCursor`: the
// character spawns where the cursor is, else where the map lets it)
void setBuilding(CGM_MapMaker& m, int slot, bool on, bool atCursor = false)
{
    CPlayer* player = playerOf(slot);
    if (!player || player->isEditing() == on)
        return;
    MapMakerBuilder& b = m.builders[slot];
    if (on) {
        if (player->isready()) {
            b.cx = static_cast<short>(std::clamp((player->leftX() + HALFPW) / TILESIZE, 0, MAPWIDTH - 1));
            b.cy = static_cast<short>(std::clamp((player->topY() + HALFPH) / TILESIZE, 0, MAPHEIGHT - 1));
        }
        player->beginEditing();
        b.palette = false;
        b.adjusting = false;
    } else if (atCursor) {
        //(standing on the cell's floor, in the middle of it)
        player->endEditing(static_cast<short>(b.cx * TILESIZE + TILESIZE / 2 - HALFPW), static_cast<short>(b.cy * TILESIZE + TILESIZE - PH));
    } else {
        player->endEditing();
    }
}

// ---- Drawing

const gfxSprite& mapItemsPicture()
{
    static gfxSprite picture;
    if (!picture)
        picture = ImageLoader(convertPath("gfx/leveleditor/leveleditor_mapitems.png")).create();
    return picture;
}

const gfxSprite& warpsPicture()
{
    static gfxSprite picture;
    if (!picture)
        picture = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp.png")).create();
    return picture;
}

// The level editor's pictures (gfx/leveleditor), loaded once
const gfxSprite& editorPicture(const char* file)
{
    static std::map<std::string, gfxSprite> pictures;
    gfxSprite& picture = pictures[file];
    if (!picture)
        picture = ImageLoader(convertPath(std::string("gfx/leveleditor/") + file)).create();
    return picture;
}

// A tool's small badge (by a builder's cursor, 16 by 16; `scale` 2: 32 by 32): pixel art as SMB3's sprites
// are drawn (black outlines, flat colors, white shaded in its sky blue): a pencil, a rectangle, a paint
// bucket, copy (two pages), paste (a clipboard)
void drawToolBadge(int tool, int x, int y, int scale = 1)
{
    static const std::vector<const char*> ART[TOOLS] = {
        {"............KKK.", "...........KRRRK", "..........KWRRRK", ".........KSWRRK.", "........KYSSSK..", ".......KYYOSK...",
            "......KYYOOK....", ".....KYYOOK.....", "....KYYOOK......", "...KTYOOK.......", "...KTTOK........", "..KTTTTK........",
            "..KKTTK.........", ".KKKKK..........", ".KK.............", "................"},
        {"................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWWKKKKKKKKSSK.", ".KWWK......KSSK.", ".KWWK......KSSK.",
            ".KWWK......KSSK.", ".KWWK......KSSK.", ".KWWK......KSSK.", ".KWWK......KSSK.", ".KWWK......KSSK.", ".KWWKKKKKKKKSSK.",
            ".KWSSSSSSSSSSSK.", ".KKKKKKKKKKKKKK.", "................", "................"},
        {"................", "....KKKKKK......", "...KS....SK.....", "..KS......SK....", ".KKKKKKKKKKKK...", ".KUUUWUUUUUUK...",
            ".KUUUUUUUUUUKK..", ".KWWWWWWWWWSKUK.", "..KWWWWWWWSKKUK.", "..KWWWWWWWSK.KUK", "..KWWWWWWWSK.KUK", "...KWWWWWSK..KK.",
            "...KKKKKKKK.....", "................", "................", "................"},
        {"KKKKKKKKK.......", "KWWWWWWSK.......", "KWUUUUUSK.......", "KWWWWWWSK.......", "KWUUUKKKKKKKKKK.", "KWWWWKWWWWWWWSK.",
            "KWUUUKWUUUUUUSK.", "KWWWWKWWWWWWWSK.", "KWUUUKWUUUUUUSK.", "KWWWWKWWWWWWWSK.", "KSSSSKWUUUUUSSK.", "KKKKKKWWWWWWWSK.",
            ".....KWUUUUUUSK.", ".....KWWWWWWWSK.", ".....KSSSSSSSSK.", ".....KKKKKKKKKK."},
        {"......KKKK......", "....KKWWWWKK....", ".KKKSWWWWWWSKKK.", ".KYOKKKKKKKKOBK.", ".KYOWWWWWWWSOBK.", ".KYOWUUUUUWSOBK.",
            ".KYOWWWWWWWSOBK.", ".KYOWUUUUUWSOBK.", ".KYOWWWWWWWSOBK.", ".KYOWUUUWWWSOBK.", ".KYOWWWWWWWSOBK.", ".KYOSSSSSSSSOBK.",
            ".KYOOOOOOOOOOBK.", ".KBBBBBBBBBBBBK.", "..KKKKKKKKKKKK..", "................"},
    };
    //(SMB3's colors)
    static const std::vector<prompts::Ink> INKS = {{'K', {0, 0, 0, 255}}, {'W', {252, 252, 252, 255}}, {'S', {164, 228, 252, 255}},
        {'U', {0, 120, 248, 255}}, {'Y', {248, 184, 0, 255}}, {'O', {216, 96, 0, 255}}, {'T', {252, 224, 168, 255}},
        {'R', {216, 40, 0, 255}}, {'B', {136, 64, 0, 255}}};
    prompts::art(x, y, ART[tool % TOOLS], INKS, std::max(1, scale));
}

// A tool's picture (32 by 32: the toolbar's, the palette's tools page), made of the game's own sprites: the
// blocks (their small sizes), the builders' glove, a white arrow; the pencil drawn as the blocks are. The
// pencil; the rectangle: a box of bricks in a drag's corners; the fill: a patch of bricks the
// glove touched once; copy: two ? blocks; paste: a ? block put down
void drawToolIcon(int tool, int x, int y, SDL_Color cuff = {200, 200, 216, 255})
{
    const SDL_Color black {0, 0, 0, 255}, white {252, 252, 252, 255};
    auto question = [&](int bx, int by) { rm->spr_blocks[1].draw(bx, by, {16, 0, 16, 16}); };
    auto small = [&](int bx, int by) { rm->spr_blocks[2].draw(bx, by, {0, 0, 8, 8}); };
    auto glove = [&](int gx, int gy) { prompts::art(gx, gy, prompts::gloveArt(), prompts::gloveInks(cuff), 1); };
    //(a corner of a drag's frame: white, 2 wide, its black edge)
    auto corner = [&](int cx, int cy, int dx, int dy) {
        prompts::fill(cx - (dx < 0 ? 6 : 0), cy, 7, 2, white);
        prompts::fill(cx, cy - (dy < 0 ? 6 : 0), 2, 7, white);
    };
    switch (tool % TOOLS) {
    case 0: {
        //(the pencil: drawn as the blocks are, in their doubled pixels: black outline, their yellows for its
        //body, a white shine)
        static const std::vector<const char*> PENCIL = {"................", "............KKK.", "..........KPPPRK", ".........KPPRRK.", "........KWWSGK..", ".......KWSSGK...", "......KYYMDK....", ".....KYWMDK.....", "....KYYMDK......", "...KYYMDK.......", "..KYYMDK........", ".KTTTUK.........", "KTUK............", "KK..............", "................", "................"};
        static const std::vector<prompts::Ink> INKS = {{'K', {0, 0, 0, 255}}, {'Y', {247, 212, 0, 255}}, {'M', {203, 151, 0, 255}},
            {'D', {151, 109, 0, 255}}, {'W', {247, 247, 247, 255}}, {'S', {200, 200, 200, 255}}, {'G', {136, 136, 136, 255}},
            {'P', {248, 160, 176, 255}}, {'R', {208, 72, 104, 255}}, {'T', {248, 216, 160, 255}}, {'U', {200, 144, 88, 255}}};
        prompts::art(x, y, PENCIL, INKS, 2);
        break;
    }
    case 1:
        //(the rectangle: three by three small bricks, a drag's four corners round them)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                small(x + 4 + c * 8, y + 4 + r * 8);
        corner(x + 1, y + 1, 1, 1);
        corner(x + 29, y + 1, -1, 1);
        corner(x + 1, y + 29, 1, -1);
        corner(x + 29, y + 29, -1, -1);
        break;
    case 2: {
        //(the fill: a round patch of small bricks, the glove pressing in its middle)
        static const char* const PATCH[4] = {".XX.", "XXXX", "XXXX", ".XX."};
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (PATCH[r][c] == 'X')
                    small(x + c * 8, y + r * 8);
        glove(x + 12, y + 12);
        break;
    }
    case 3:
        //(copy: a ? block, and the same again)
        question(x + 2, y + 2);
        prompts::fill(x + 12, y + 12, 18, 18, black);
        question(x + 14, y + 14);
        break;
    case 4:
        //(paste: a white arrow down, a ? block where it points)
        prompts::fill(x + 12, y + 1, 8, 10, black);
        prompts::fill(x + 6, y + 9, 20, 4, black);
        prompts::fill(x + 8, y + 13, 16, 2, black);
        prompts::fill(x + 10, y + 15, 12, 2, black);
        prompts::fill(x + 12, y + 17, 8, 2, black);
        prompts::fill(x + 14, y + 2, 4, 9, white);
        prompts::fill(x + 8, y + 10, 16, 2, white);
        prompts::fill(x + 10, y + 12, 12, 2, white);
        prompts::fill(x + 12, y + 14, 8, 2, white);
        prompts::fill(x + 14, y + 16, 4, 2, white);
        question(x + 8, y + 16);
        break;
    }
}

const gfxSprite& hazardsPicture()
{
    static gfxSprite picture;
    if (!picture)
        picture = ImageLoader(convertPath("gfx/leveleditor/leveleditor_maphazard_buttons.png")).create();
    return picture;
}

const char* const HAZARD_NAMES[HAZARD_TYPES] = {"Fire bar", "Roto disc", "Bullet Bill cannon", "Flame cannon",
    "Piranha plant", "Fire piranha plant", "Tall piranha plant", "Short piranha plant"};

// A tileset's tile, a block, a map item, a warp, a hazard or a platform (its number: `index`; a warp's:
// its connection + 10 x its direction; a hazard's: its type; a platform's: its path) at x, y (the
// brush, the palette)
Uint64 animClock();

void drawBrushPiece(uint8_t kind, const TilesetTile& tile, short index, int x, int y)
{
    if (kind == BrushType) {
        //(the level editor's picture of the type: solid an X, on top triangles, ice a snowflake, death a
        //skull or arrows to its deadly sides; the tiles' own: a tile, an arrow down to it)
        const int type = index % TILE_TYPES;
        if (type == 0) {
            prompts::fill(x + 6, y + 12, 20, 16, SDL_Color {120, 120, 136, 255});
            prompts::fill(x + 8, y + 14, 16, 12, SDL_Color {168, 168, 184, 255});
            prompts::fill(x + 14, y + 2, 4, 6, SDL_Color {248, 248, 248, 255});
            prompts::fill(x + 11, y + 6, 10, 2, SDL_Color {248, 248, 248, 255});
            prompts::fill(x + 13, y + 8, 6, 2, SDL_Color {248, 248, 248, 255});
        } else {
            editorPicture("leveleditor_transparent_tiles.png").draw(x, y, {(type - 1) * TILESIZE, 0, TILESIZE, TILESIZE});
        }
        return;
    }
    if (kind == BrushSpawn) {
        //(the level editor's: a crossed cell, everyone's gray, a team's its color; the items' own)
        const int k = index % SPAWN_KINDS;
        if (k == 5)
            editorPicture("leveleditor_noitemspawntile.png").draw(x, y, {0, 0, TILESIZE, TILESIZE});
        else
            editorPicture("leveleditor_nospawntile.png").draw(x, y, {k * TILESIZE, 0, TILESIZE, TILESIZE});
        return;
    }
    if (kind == BrushModeObject) {
        //(the game's own, small: a race goal's flag pole and its number; a team's flag base)
        if (index < 8) {
            rm->spr_racegoal.drawStretch({0, 0, 68, 54}, blitdest, {x, y + 4, TILESIZE, 26});
            rm->spr_racetext.draw(x + TILESIZE - 14, y + TILESIZE - 16, {(index + 1) * 16, 0, 16, 16});
        } else {
            rm->spr_flagbases.drawStretch({((index - 8) & 3) * 48, 0, 48, 48}, blitdest, {x, y, TILESIZE, TILESIZE});
        }
        return;
    }
    if (kind == BrushHazard) {
        hazardsPicture().draw(x + 4, y + 4, {(index % HAZARD_TYPES) * 24, 32, 24, 24});
    } else if (kind == BrushPlatform) {
        //(the tile it's made of, its path's mark)
        if (tile.iID >= 0)
            if (CTileset* tileset = g_tilesetmanager->tileset(static_cast<size_t>(tile.iID)))
                tileset->draw(DrawSize::Ingame, CTilesetManager::rect(DrawSize::Ingame, tile.iCol, tile.iRow), blitdest, SDL_Rect {x, y, TILESIZE, TILESIZE});
        //(its path: a line, a line on round the screen, a circle; the level editor's pictures)
        prompts::fill(x + 5, y + 5, 22, 22, SDL_Color {16, 24, 48, 255});
        editorPicture("leveleditor_pathtype_buttons.png").draw(x + 6, y + 6, {2 + (index % PLATFORM_KINDS) * 24, 34, 20, 20});
    } else if (kind == BrushWarp) {
        warpsPicture().draw(x, y, {(index % WARP_CONNECTIONS) * TILESIZE, (index / WARP_CONNECTIONS) * TILESIZE, TILESIZE, TILESIZE});
    } else if (kind == BrushBlock) {
        rm->spr_blocks[0].draw(x, y, blockPicture(static_cast<short>(index & 63), !(index & 64)));
        if (index & 128)
            rm->menu_font_small.drawRightJustified(x + TILESIZE - 2, y + TILESIZE - 15, "H");
    } else if (kind == BrushItem) {
        mapItemsPicture().draw(x, y, {index * TILESIZE, 0, TILESIZE, TILESIZE});
    } else if (tile.iID == TILESETANIMATED) {
        //(its frames side by side: the first; its row iCol, its column iRow, as the map keeps them)
        const int frame = static_cast<int>((animClock() / 133) % 4);
        rm->spr_tileanimation[0].draw(x, y, {(tile.iCol * 4 + frame) * TILESIZE, tile.iRow * TILESIZE, TILESIZE, TILESIZE});
    } else if (tile.iID >= 0) {
        if (CTileset* tileset = g_tilesetmanager->tileset(static_cast<size_t>(tile.iID)))
            tileset->draw(DrawSize::Ingame, CTilesetManager::rect(DrawSize::Ingame, tile.iCol, tile.iRow), blitdest,
                SDL_Rect {x, y, TILESIZE, TILESIZE});
    }
}

// A frame round a cell, two pixels: dark outside, `light` inside (it shows on anything)
void cellFrame(int x, int y, int w, int h, SDL_Color light)
{
    const SDL_Color dark {16, 24, 48, 255};
    prompts::fill(x - 2, y - 2, w + 4, 2, dark);
    prompts::fill(x - 2, y + h, w + 4, 2, dark);
    prompts::fill(x - 2, y, 2, h, dark);
    prompts::fill(x + w, y, 2, h, dark);
    prompts::fill(x, y, w, 2, light);
    prompts::fill(x, y + h - 2, w, 2, light);
    prompts::fill(x, y + 2, 2, h - 4, light);
    prompts::fill(x + w - 2, y + 2, 2, h - 4, light);
}

// The clock of the hands' and the pictures' motion: the wall clock's; the autotest's run as fast as it
// goes (no --at-realtime), a drawn frame a 60th of a second (its screenshots as a person would see them)
Uint64 s_drawnFrames = 0;
Uint64 animClock()
{
    if (cmd::g_autotest.enabled && cmd::g_autotest.realtime == 0)
        return s_drawnFrames * 16;
    return SDL_GetTicks();
}

// A builder's hand: the level editor's glove (Mario Paint's), white, pointing up and left, its cuff in
// the player's color, its fingertip at the middle of the builder's cell (cx, cy: the cell's top left),
// the player's name on a tag beside it; the cell framed in the color. Drawn only: nothing of it is the
// match's; its motion is by the clock (the same at any frame rate):
//  - it glides to the cell (eased; a jump across the screen snaps), a short trail behind it fast
//  - the cell's frame pops: a little wider as the hand gets there, snapping to the cell
//  - a button held: the hand dips on a spring (overshooting a little, bouncing back as it's let go), the
//    finger bending at the bottom, a mark where it touches
//  - a press: sparks out from the fingertip, white and the color, shrinking as they go
//  - still a while: a slow bob, a tap of the finger now and then
//  - the tag follows the hand a little behind; the hand pops in, a sparkle round it, as building starts
void drawGlove(int slot, SDL_Rect cell, SDL_Point tip, SDL_Color color, const std::string& label, bool down, bool snap = false)
{
    const int cx = cell.x, cy = cell.y;
    struct Glove {
        float x = 0, y = 0;            // the fingertip as drawn
        float tagX = 0, tagY = 0;
        float dip = 0, dipVel = 0;     // the press's spring (1: down)
        int cellX = -1, cellY = -1, cellW = 0, cellH = 0;
        Uint64 lastMs = 0, movedMs = 0, pressMs = 0, tapMs = 0, appearMs = 0, arriveMs = 0;
        bool down = false;
        float trail[6][2] {};
        int trailAt = 0;
    };
    static Glove gloves[4];
    Glove& g = gloves[slot & 3];
    const Uint64 now = animClock();
    const float tx = static_cast<float>(tip.x), ty = static_cast<float>(tip.y);

    //(not drawn a while: it appears where it is, popping in)
    if (g.lastMs == 0 || now - g.lastMs > 400) {
        g.x = g.tagX = tx;
        g.y = g.tagY = ty;
        g.dip = g.dipVel = 0;
        g.appearMs = g.movedMs = g.tapMs = now;
        for (auto& point : g.trail) {
            point[0] = tx;
            point[1] = ty;
        }
    }
    const float dt = std::clamp(static_cast<float>(now - g.lastMs) / 1000.0f, 0.0f, 0.05f);
    g.lastMs = now;
    if (cx != g.cellX || cy != g.cellY || cell.w != g.cellW || cell.h != g.cellH) {
        //(a new cell: the frame pops; round the screen's edge: the hand is there at once)
        if (g.cellX >= 0 && std::abs(cx - g.cellX) > App::screenWidth / 2) {
            g.x = tx;
            g.y = ty;
        }
        g.cellX = cx;
        g.cellY = cy;
        g.cellW = cell.w;
        g.cellH = cell.h;
        g.movedMs = g.arriveMs = now;
    }

    //The glide: eased toward the cell (a time constant of 40 ms), the tag slower behind it
    const float follow = 1.0f - std::exp(-dt / 0.04f);
    const float tagFollow = 1.0f - std::exp(-dt / 0.09f);
    g.x += (tx - g.x) * follow;
    g.y += (ty - g.y) * follow;
    if (snap) {
        g.x = tx;    //(the mouse's: on the pointer, exactly)
        g.y = ty;
    }
    g.tagX += (g.x - g.tagX) * tagFollow;
    g.tagY += (g.y - g.tagY) * tagFollow;
    g.trail[g.trailAt][0] = g.x;
    g.trail[g.trailAt][1] = g.y;
    g.trailAt = (g.trailAt + 1) % 6;

    //The press: a spring to 1 (held) or 0, a little under critical damping (it overshoots a touch)
    if (down && !g.down)
        g.pressMs = now;
    g.down = down;
    const bool idle = !snap && !down && now - g.movedMs > 900 && now - g.pressMs > 900;
    if (idle && now - g.tapMs > 2600) {
        g.tapMs = now;
        g.dipVel += 9.0f;    //(a tap of the finger, now and then)
    }
    const float target = down ? 1.0f : 0.0f;
    g.dipVel += ((target - g.dip) * 520.0f - g.dipVel * 26.0f) * dt;
    g.dip = std::clamp(g.dip + g.dipVel * dt, -0.4f, 1.3f);

    //The bob, still a while: a slow wave, faded in
    float bob = 0;
    if (idle) {
        const float t = static_cast<float>(now - g.movedMs - 900) / 1000.0f;
        bob = std::sin(t * 2.0f * 3.14159265f * 0.8f) * 2.0f * std::min(1.0f, t / 0.4f);
    }

    const int fx = static_cast<int>(std::lround(g.x));
    const int fy = static_cast<int>(std::lround(g.y + bob + g.dip * 5.0f));

    //The cell's frame: popped wide as the hand gets there, snapping in (eased out)
    {
        const float t = std::min(1.0f, static_cast<float>(now - g.arriveMs) / 140.0f);
        const int grow = static_cast<int>(std::lround(6.0f * (1.0f - t) * (1.0f - t)));
        cellFrame(cx - grow, cy - grow, cell.w + grow * 2, cell.h + grow * 2, color);
    }

    //The trail: dots where the hand was, the oldest smallest (fast only)
    const float speed = std::hypot(tx - g.x, ty - g.y);
    if (speed > 10.0f) {
        for (int k = 1; k <= 3; k++) {
            const float* point = g.trail[(g.trailAt + 6 - 1 - k * 2) % 6];
            const int size = 5 - k;
            prompts::fill(static_cast<int>(point[0]) - size / 2, static_cast<int>(point[1]) - size / 2, size, size, k == 1 ? SDL_Color {255, 255, 255, 255} : color);
        }
    }

    //(where it touches, held: a dark mark under the fingertip)
    if (g.dip > 0.6f) {
        prompts::fill(fx - 4, static_cast<int>(ty) + 6, 8, 2, theme::shade(color, 45));
        prompts::fill(fx - 2, static_cast<int>(ty) + 5, 4, 1, theme::shade(color, 45));
    }

    const std::vector<prompts::Ink> inks = prompts::gloveInks(color);
    //(popping in: a size bigger for a moment)
    const bool popping = now - g.appearMs < 90;
    const int scale = popping ? 3 : 2;
    if (g.dip > 0.5f) {
        //(the finger bent: its tip drawn shorter)
        prompts::art(fx - 5 * scale / 2, fy - scale / 2 + 4, prompts::gloveArt(true), inks, scale);
    } else {
        prompts::art(fx - 5 * scale / 2, fy - scale / 2, prompts::gloveArt(), inks, scale);
    }

    //Sparks: a press's, eight out from the fingertip (white and the color, shrinking); popping in, a ring
    auto sparks = [&](Uint64 since, float reach, int count) {
        const float t = static_cast<float>(now - since) / 260.0f;
        if (since == 0 || t >= 1.0f)
            return;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const float r = 5.0f + reach * eased;
        const int size = std::max(1, static_cast<int>(std::lround(4.0f * (1.0f - t))));
        for (int k = 0; k < count; k++) {
            const float a = static_cast<float>(k) * 2.0f * 3.14159265f / static_cast<float>(count) - 3.14159265f / 2.0f;
            const int px = static_cast<int>(g.x + std::cos(a) * r), py = static_cast<int>(ty + std::sin(a) * r);
            prompts::fill(px - size / 2 - 1, py - size / 2 - 1, size + 2, size + 2, {0, 0, 0, 255});
            prompts::fill(px - size / 2, py - size / 2, size, size, k % 2 ? color : SDL_Color {255, 255, 255, 255});
        }
    };
    sparks(g.pressMs, 20.0f, 8);
    sparks(g.appearMs, 30.0f, 12);

    //The tag: the player's name in its color, a little behind the hand (none: one builder alone)
    if (label.empty())
        return;
    const int w = rm->menu_font_small.getWidth(label.c_str()) + 6;
    const int lx = std::min(static_cast<int>(g.tagX) + 22, App::screenWidth - w);
    const int ly = std::min(static_cast<int>(g.tagY + bob) + 26, App::screenHeight - 16);
    prompts::fill(lx + 2, ly, w - 4, 16, {0, 0, 0, 255});
    prompts::fill(lx, ly + 2, w, 12, {0, 0, 0, 255});
    prompts::fill(lx + 2, ly + 2, w - 4, 12, color);
    prompts::fill(lx + 2, ly + 2, w - 4, 2, theme::shade(color, 140));
    rm->menu_font_small.draw(lx + 3, ly + 1, label.c_str());
}

// The players' colors, for their cursors and names (red, green, yellow, blue)
SDL_Color slotColor(int slot)
{
    static const SDL_Color COLORS[SLOTS] {{232, 64, 48, 255}, {64, 200, 64, 255}, {248, 200, 40, 255}, {64, 136, 240, 255}};
    return COLORS[slot & 3];
}

// What a builder's palette has chosen: the hand's (drawn over the strip) rectangle and where it touches
// The mouse (this machine's): it steers one builder (mouseSlot()), a cell a tick, by commands in its
// byte. Where its builder will be is foreseen (`pred`: the match has the commands a few ticks late
// online), from the match's own once it's had them all
struct MouseState {
    bool active = false;          // used last (a key or a pad's button: not)
    int x = 0, y = 0;             // the screen's pixels
    bool moved = false;           // since the last tick
    bool left = false, right = false;
    bool leftClick = false, rightClick = false, middleClick = false;    // pressed since the last tick
    int wheel = 0;
    bool ctrlWheel = false;       // the wheel turned with Ctrl held (the map: the layer)
    int lineX = -1, lineY = -1;   // the map: where a stroke began; with Shift held then, it keeps to its row
    bool lineShift = false;       // or column (a straight line)
    bool back = false, forward = false;    // the side buttons: undo, redo
    bool stroke = false;          // the map: a button held, the cursor got to where it went down: painting
    bool take = false;            // the palette: A pressed where the cursor gets to
    bool pick = false;            // the map: Ctrl+click, the eyedropper where the cursor gets to
    bool grab = false;            // the map: Ctrl+right click, pick up where the cursor gets to
    int aimX = -1, aimY = -1;     // the palette: the cell it goes to
    MapMakerBuilder pred;
    bool predValid = false;
    int wait = 0;                 // ticks until the match's builder is read again (after what can't be foreseen)
    int idle = 0;                 // ticks since the last step
    std::vector<uint8_t> queue;   // bytes to send first (presses, and let go)
};
MouseState s_mouse;
SDL_Rect s_pauseRowRect[6] {};    // the pause menu's rows and players (the mouse's)
SDL_Rect s_recentRect[MAP_MAKER_RECENT] {};    // the mouse's builder's recent brushes, drawn by its card (k: k nexts on)
SDL_Rect s_toolRect[5] {};                     // ... its toolbar's tools, layers, and the whole bar (a click there
SDL_Rect s_layerRect[4] {};                    // doesn't paint the map under it)
SDL_Rect s_toolbarRect {};
int s_recentShown = 0;
SDL_Rect s_pauseChipRect[SLOTS] {};
bool s_pauseClick = false, s_pauseRightClick = false, s_pauseMoved = false;
int s_pauseClickX = 0, s_pauseClickY = 0;
bool s_cursorShown = false;

// The palette's places (drawn last, for the mouse's builder): a cell (px, py) or a tab
bool inRect(const SDL_Point& p, const SDL_Rect& r)    // (the Xbox's SDL hasn't SDL_PointInRect)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

struct PaletteHit {
    SDL_Rect r;
    short px, py, tab;
};
std::vector<PaletteHit> s_paletteHits;
constexpr short TAB_WHEEL = 99;    // (a hit's: the tabs' row, for the wheel)
SDL_Rect s_stripRect {};

// The builder the mouse steers: this machine's keyboard player's, else its first builder's
int mouseSlot()
{
    int first = -1;
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!localHuman(slot) || !building(slot))
            continue;
        const CInputPlayerControl* control = game_values.playerInput.inputControls[deviceOf(slot) & 3];
        if (control && control->iDevice == DEVICE_KEYBOARD_ID)
            return slot;
        if (first < 0)
            first = slot;
    }
    return first;
}

// One parts window on a machine's screen, however many of its players have theirs open: the first's (by
// slot), the window's; the others' go to its page and their hands pick in it too (paletteOwner,
// windowPageStep). Another machine's builders' windows are on their own screens
int s_windowSlot = -1;          // whose window this frame's is
bool s_mouseInWindow = false;   // the mouse's builder is in it (its clicks go there)
std::vector<PaletteHit> s_windowHits;    // its cells this frame (other hands are placed by them)
bool s_paletteCellShown[SLOTS] {};       // a builder's hand has its cell in the window this frame

// This machine's parts window's builder: its first player (by slot) with theirs open; -1 none
int paletteOwner(const CGM_MapMaker& m)
{
    for (int slot = 0; slot < SLOTS; slot++)
        if (building(slot) && localHuman(slot) && m.builders[slot].palette)
            return slot;
    return -1;
}

// Whether the window's mouse hits are recorded: its owner's, if the mouse's builder is in it
bool recordsMouse(int slot)
{
    return slot == s_windowSlot ? s_mouseInWindow : slot == mouseSlot();
}

void paletteHit(int slot, int x, int y, int w, int h, int px, int py, int tab = -1)
{
    if (recordsMouse(slot))
        s_paletteHits.push_back({{x, y, w, h}, static_cast<short>(px), static_cast<short>(py), static_cast<short>(tab)});
    if (slot == s_windowSlot)
        s_windowHits.push_back({{x, y, w, h}, static_cast<short>(px), static_cast<short>(py), static_cast<short>(tab)});
}

SDL_Rect s_paletteCell[SLOTS] {};
SDL_Point s_paletteTip[SLOTS] {};

void paletteSelect(int slot, int x, int y, int w, int h, int tipX = -1, int tipY = -1)
{
    s_paletteCell[slot & 3] = SDL_Rect {x, y, w, h};
    s_paletteTip[slot & 3] = SDL_Point {tipX >= 0 ? tipX : x + w / 2, tipY >= 0 ? tipY : y + h / 2};
    s_paletteCellShown[slot & 3] = true;
}

// A builder's palette: a strip of three rows of the page, always at the bottom of the screen (it doesn't
// move about with the cursor), its page's name in a bar over it
// The palette's tabs, along its top, in groups: a group's name a tab; the open group's pages its tabs
// instead, in a band of the player's color, the open page bright (a window round them, if they don't
// all fit)
void drawTabs(int slot, int tab, int y)
{
    constexpr int PAD = 8, H = 18;
    struct Piece {
        int tab;            // the tab it opens (a group's: its first)
        std::string name;
        int kind;           // 0 a group, 2 a page of the open group, 3 the open group (one page)
        int w;
    };
    std::vector<Piece> pieces;
    size_t openPiece = 0;
    for (const TabGroup& g : TAB_GROUPS) {
        const bool here = tab >= g.first && tab < g.first + g.count;
        if (here && g.count > 1) {
            for (int k = 0; k < g.count; k++) {
                if (g.first + k == tab)
                    openPiece = pieces.size();
                pieces.push_back({g.first + k, TAB_NAMES[g.first + k], 2, 0});
            }
        } else {
            if (here)
                openPiece = pieces.size();
            pieces.push_back({g.first, g.name, here ? 3 : 0, 0});
        }
    }
    int total = 0;
    for (Piece& piece : pieces) {
        piece.w = rm->menu_font_small.getWidth(piece.name.c_str()) + PAD * 2;
        total += piece.w + 2;
    }
    int x = 4;
    if (total + 8 > App::screenWidth) {
        //(the open one in the middle, as far as the ends allow)
        int before = 0;
        for (size_t k = 0; k < openPiece; k++)
            before += pieces[k].w + 2;
        x = std::clamp(App::screenWidth / 2 - before - pieces[openPiece].w / 2, App::screenWidth - 4 - total, 4);
    }
    prompts::fill(0, y, App::screenWidth, H, SDL_Color {8, 12, 28, 255});
    const SDL_Color color = slotColor(slot), groupColor = theme::shade(slotColor(slot), 55);
    for (const Piece& piece : pieces) {
        const int w = piece.w;
        if (x + w > 0 && x < App::screenWidth) {
            const bool open = piece.kind == 3 || (piece.kind == 2 && piece.tab == tab);
            const bool inGroup = piece.kind == 2;
            const SDL_Color ink = open ? color : inGroup ? groupColor : SDL_Color {40, 52, 88, 255};
            const int top = open || inGroup ? 0 : 3;
            prompts::fill(x, y + top, w, H - top, ink);
            if (open)
                prompts::fill(x, y, w, 2, theme::shade(color, 140));
            rm->menu_font_small.drawCentered(x + w / 2, y + (top == 0 ? 2 : 4), piece.name.c_str());
            paletteHit(slot, x, y, w, H, 0, 0, piece.tab);
        }
        //(the open group's pages joined: the band unbroken between them)
        if (piece.kind == 2 && &piece != &pieces.back() && (&piece + 1)->kind == 2)
            prompts::fill(x + w, y, 2, H, groupColor);
        x += w + 2;
    }
    //(more to the side: an arrow)
    if (total + 8 > App::screenWidth) {
        rm->menu_font_small.draw(2, y + 3, "<");
        rm->menu_font_small.drawRightJustified(App::screenWidth - 2, y + 3, ">");
        //(the mouse: the arrows, the tab before and after; the tabs' row wheeled, the same)
        paletteHit(slot, 0, y, 12, H, 0, 0, std::max(0, tab - 1));
        paletteHit(slot, App::screenWidth - 12, y, 12, H, 0, 0, std::min(CYCLE_TABS - 1, tab + 1));
    }
    paletteHit(slot, 0, y, App::screenWidth, H, 0, 0, TAB_WHEEL);
}

std::string brushName(const MapMakerBuilder& b);

void drawStrip(int slot, const MapMakerBuilder& b)
{
    constexpr int TABS_H = 18;
    const int h = STRIP_ROWS * TILESIZE + 24 + TABS_H;
    const int stripTop = App::screenHeight - h;
    prompts::fill(0, stripTop, App::screenWidth, h, SDL_Color {16, 24, 48, 255});
    if (recordsMouse(slot)) {
        s_paletteHits.clear();
        s_stripRect = {0, stripTop, App::screenWidth, h};
    }
    drawTabs(slot, tabOfPage(b.page), stripTop);
    const int top = stripTop + TABS_H;
    theme::statusBar(0, top, App::screenWidth, 24);
    static const char* const DIRECTIONS[4] = {"up", "right", "down", "left"};
    std::string title = "P" + std::to_string(slot + 1) + "  Items";
    if (onPage(b, PageItems)) {
        MapMakerBuilder item = b;
        item.brush = BrushItem;
        item.item = static_cast<short>(b.px);
        title = "P" + std::to_string(slot + 1) + "  " + brushName(item);
    }
    if (onBlocksPage(b)) {
        bool on = true;
        const short type = blockOfCell(b.px, b.py, on);
        title = "P" + std::to_string(slot + 1) + "  " + BLOCK_NAMES[type % 30] + (type >= 7 && type <= 14 ? (on ? ", on" : ", off") : "")
            + (hideable(type) && b.blockHidden ? " (hidden)" : "");
    }
    if (onModeObjectsPage(b))
        title = "P" + std::to_string(slot + 1) + (b.py == 0 ? "  Race goal " + std::to_string(b.px + 1) : "  Flag base, team " + std::to_string(b.px + 1))
            + " (B: back to random)";
    if (onContentsPage(b)) {
        int count = 0;
        for (int k = 0; k < NUM_POWERUPS; k++)
            count += (b.mix >> k) & 1;
        title = "P" + std::to_string(slot + 1) + "  ? Block: " + (b.py == 0 ? (b.px == 0 ? std::string("random (the usual mix)") : ITEM_SET_NAMES[b.px - 1])
            : "mix of " + std::to_string(count));
    }
    if (onWarpsPage(b))
        title = "P" + std::to_string(slot + 1) + "  Warp " + std::to_string(b.px + 1) + ", " + DIRECTIONS[b.py & 3];    //(a number's warps go to each other)
    if (onToolsPage(b))
        title = "P" + std::to_string(slot + 1) + "  " + TOOL_NAMES[b.px % TOOLS];
    if (onTypesPage(b))
        title = "P" + std::to_string(slot + 1) + "  Type: " + TILE_TYPE_NAMES[b.px % TILE_TYPES];
    if (onAnimatedPage(b))
        title = "P" + std::to_string(slot + 1) + "  Animated tiles";
    if (onWeatherPage(b)) {
        const CGM_MapMaker* m = mode();
        const bool on = m && (m->eyecandy[b.py % 3] >> (b.px % WEATHERS)) & 1;
        title = "P" + std::to_string(slot + 1) + "  " + WEATHER_NAMES[b.px % WEATHERS] + " " + WEATHER_LAYERS[b.py % 3] + (on ? ": on" : ": off");
    }
    if (onSpawnsPage(b))
        title = "P" + std::to_string(slot + 1) + "  " + SPAWN_NAMES[b.px % SPAWN_KINDS];
    if (onHazardsPage(b))
        title = "P" + std::to_string(slot + 1) + "  " + HAZARD_NAMES[b.px % HAZARD_TYPES];
    if (onPlatformsPage(b))
        title = "P" + std::to_string(slot + 1) + "  Platform: " + PLATFORM_KIND_NAMES[b.px % PLATFORM_KINDS];
    if (onBackgroundsPage(b) || onMusicPage(b)) {
        const int count = onBackgroundsPage(b) ? static_cast<int>(backgrounds().count()) : static_cast<int>(MusicCategory::COUNT);
        const std::string name = onBackgroundsPage(b)
            ? (backgrounds().count() ? backgrounds().at(static_cast<size_t>(b.px)).stem().string() : std::string("-"))
            : std::string(to_string(static_cast<MusicCategory>(b.px)));
        title = "P" + std::to_string(slot + 1) + (onBackgroundsPage(b) ? "  Background: " : "  Music: ") + name + "  (" + std::to_string(b.px + 1) + "/" + std::to_string(count) + ")";
    } else if (b.page < static_cast<int>(tilesetCount())) {
        const CTileset* tileset = g_tilesetmanager->tilesetInfo(static_cast<size_t>(b.page));
        title = "P" + std::to_string(slot + 1) + "  " + (tileset ? tileset->name() : std::string()) + "  (" + std::to_string(b.page + 1) + "/"
            + std::to_string(tilesetCount()) + ")";
    }
    //(the player's number in its color, then what's chosen)
    {
        const std::string tag = "P" + std::to_string(slot + 1);
        const int tw = rm->menu_font_small.getWidth(tag.c_str()) + 8;
        prompts::fill(6, top + 4, tw, 16, slotColor(slot));
        rm->menu_font_small.draw(10, top + 5, tag.c_str());
        const std::string rest = title.size() > tag.size() ? title.substr(tag.size()) : std::string();
        rm->menu_font_small.draw(10 + tw, top + 5, rest.c_str());
    }
    using prompts::Pad;
    using prompts::menuKey;
    //(right-aligned in the bar, the title on the left)
    //(on a setting: A changes it; changing: left/right, A done)
    const bool settings = (onHazardsPage(b) || onPlatformsPage(b)) && b.py > 0;
    const std::vector<prompts::Prompt> row = b.adjusting
        ? std::vector<prompts::Prompt> {{{Pad::Left, Pad::Right}, s_mouse.active ? std::vector<std::string> {"Wheel"}
                  : std::vector<std::string> {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"},
              {{Pad::A}, {s_mouse.active ? "LMB" : menuKey(prompts::MENU_SELECT)}, "done"}}
        : std::vector<prompts::Prompt> {{{Pad::LT, Pad::RT}, {"PgUp", "PgDn"}, "tabs"},
              {{Pad::A}, {s_mouse.active ? "LMB" : menuKey(prompts::MENU_SELECT)}, settings ? "change" : onContentsPage(b) && b.py == 1 ? "add/remove" : "take"},
              {{Pad::RB}, {"Bksp"}, "undo"}, {{Pad::B}, {s_mouse.active ? "RMB" : "Esc"}, "close"}};
    //(Y: on the tiles, the next tileset; on the blocks, hidden or not)
    std::vector<prompts::Prompt> rowY = row;
    if (!b.adjusting && b.page < static_cast<int>(tilesetCount()))
        rowY.insert(rowY.begin() + 1, prompts::Prompt {{Pad::Y}, {menuKey(prompts::MENU_SCROLL)}, "tileset"});
    else if (!b.adjusting && onBlocksPage(b))
        rowY.insert(rowY.begin() + 1, prompts::Prompt {{Pad::Y}, {menuKey(prompts::MENU_SCROLL)}, b.blockHidden ? "shown" : "hidden"});
    prompts::drawPrompts(App::screenWidth - 8 - prompts::promptsWidth(rowY, 12) / 2, top + 2, rowY, 12);

    const int rows = top + 24;
    if (onBackgroundsPage(b)) {
        //(a band of the background chosen there: left/right go through them)
        static int s_previewIndex = -1;
        static gfxSprite s_preview;
        if (s_previewIndex != b.px && backgrounds().count()) {
            s_previewIndex = b.px;
            const std::string name = backgrounds().at(static_cast<size_t>(b.px)).filename().string();
            s_preview = ImageLoader(convertPath("gfx/packs/backgrounds/" + name, gamegraphicspacklist->currentPath())).withoutColorKey().create();
        }
        if (s_preview)
            s_preview.draw(0, rows, {0, App::screenHeight / 2 - STRIP_ROWS * TILESIZE / 2, App::screenWidth, STRIP_ROWS * TILESIZE});
        paletteSelect(slot, 4, rows + 4, App::screenWidth - 8, STRIP_ROWS * TILESIZE - 8);
        paletteHit(slot, 0, rows, App::screenWidth, STRIP_ROWS * TILESIZE, b.px, 0);
        return;
    }
    if (onMusicPage(b)) {
        const std::string music = "< " + std::string(to_string(static_cast<MusicCategory>(b.px))) + " >";
        rm->menu_font_large.drawCentered(App::screenWidth / 2, rows + 36, music.c_str());
        const int w = rm->menu_font_large.getWidth(music.c_str()) + 24;
        paletteSelect(slot, App::screenWidth / 2 - w / 2, rows + 30, w, 30);
        paletteHit(slot, 0, rows, App::screenWidth, STRIP_ROWS * TILESIZE, b.px, 0);
        return;
    }
    int w = 1, ph = 1;
    pageSize(b, w, ph);
    if (onContentsPage(b)) {
        //(above: ? blocks, random and the item sets, their numbers on them; under them the powerups, the
        //item menus' order, the mix's ticked)
        const int scroll = b.py == 0 ? 0 : b.scrollX;
        for (int i = 0; i < ITEM_SET_CELLS; i++) {
            rm->spr_blocks[0].draw(i * TILESIZE, rows, {TILESIZE, 0, TILESIZE, TILESIZE});
            paletteHit(slot, i * TILESIZE, rows, TILESIZE, TILESIZE, i, 0);
            if (i > 0)
                rm->menu_font_small.drawRightJustified(i * TILESIZE + TILESIZE - 1, rows + TILESIZE - 14, std::to_string(i).c_str());
            const bool chosen = b.brush == BrushBlock && b.block == 1 && (i == 0 ? b.contents == -1 : b.contents == -10 - ITEM_SETS[i - 1]);
            if (chosen)
                prompts::fill(i * TILESIZE + 2, rows + 2, 6, 6, SDL_Color {248, 200, 40, 255});
        }
        for (int i = 0; i < MAPWIDTH && scroll + i < NUM_POWERUPS; i++) {
            const short powerup = POWERUP_ORDER[scroll + i];
            const int y = rows + TILESIZE + 4;
            rm->spr_storedpoweruplarge.draw(i * TILESIZE, y, {powerup * 32, 0, 32, 32});
            paletteHit(slot, i * TILESIZE, y, TILESIZE, TILESIZE, scroll + i, 1);
            if ((b.mix >> powerup) & 1) {
                prompts::fill(i * TILESIZE + 20, y + 20, 12, 12, SDL_Color {16, 24, 48, 255});
                prompts::fill(i * TILESIZE + 22, y + 22, 8, 8, SDL_Color {64, 200, 64, 255});
            }
        }
        paletteSelect(slot, (b.px - (b.py == 0 ? 0 : scroll)) * TILESIZE, rows + (b.py == 0 ? 0 : TILESIZE + 4), TILESIZE, TILESIZE);
        return;
    }
    if (onWeatherPage(b)) {
        //(a row a layer: back, middle, front; the weathers on green)
        const CGM_MapMaker* m = mode();
        for (int layer = 0; layer < 3; layer++) {
            for (int k = 0; k < WEATHERS; k++) {
                const bool on = m && (m->eyecandy[layer] >> k) & 1;
                const int x = k * TILESIZE + 4 * TILESIZE, y = rows + layer * TILESIZE;
                //(on: green, a tick in its corner; off: dark)
                prompts::fill(x + 1, y + 1, TILESIZE - 2, TILESIZE - 2, on ? SDL_Color {48, 152, 64, 255} : SDL_Color {28, 34, 56, 255});
                editorPicture("leveleditor_eyecandy.png").draw(x, y, {k * TILESIZE, 0, TILESIZE, TILESIZE});
                paletteHit(slot, x, y, TILESIZE, TILESIZE, k, layer);
                if (on)
                    prompts::art(x + TILESIZE - 14, y + TILESIZE - 12, {"......#.", ".....#w#", "#...#w#.", "##.#w#..", "#w#w#...", ".#w#....", "..#....."},
                        {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}}, 1);
            }
            static const char* const LAYER_MARKS[3] = {"Back", "Middle", "Front"};
            rm->menu_font_small.draw(8, rows + layer * TILESIZE + 9, LAYER_MARKS[layer]);
        }
        paletteSelect(slot, (b.px + 4) * TILESIZE, rows + b.py * TILESIZE, TILESIZE, TILESIZE);
        return;
    }
    if (onTypesPage(b) || onSpawnsPage(b)) {
        const bool types = onTypesPage(b);
        for (int i = 0; i < w; i++) {
            drawBrushPiece(types ? BrushType : BrushSpawn, b.tile, static_cast<short>(i), i * TILESIZE, rows);
            paletteHit(slot, i * TILESIZE, rows, TILESIZE, TILESIZE, i, 0);
        }
        paletteSelect(slot, b.px * TILESIZE, rows, TILESIZE, TILESIZE);
        return;
    }
    if (onToolsPage(b)) {
        //(the tools: a cell, a rectangle's outline, a filled one; the one in use dotted)
        for (int i = 0; i < TOOLS; i++) {
            const int x = i * TILESIZE, y = rows;
            const SDL_Color ink {248, 248, 248, 255};
            (void)ink;
            drawToolIcon(i, x, y, slotColor(slot));
            paletteHit(slot, x, y, TILESIZE, TILESIZE, i, 0);
            if (i == b.tool)
                prompts::fill(x + 14, y + 28, 4, 3, SDL_Color {248, 200, 40, 255});
        }
        paletteSelect(slot, b.px * TILESIZE, rows, TILESIZE, TILESIZE);
        return;
    }
    if (onHazardsPage(b) || onPlatformsPage(b)) {
        //(the types in a row; under it the settings of the one there, in two columns)
        const bool hazards = onHazardsPage(b);
        for (int i = 0; i < w; i++) {
            drawBrushPiece(hazards ? BrushHazard : BrushPlatform, b.tile, static_cast<short>(i), i * TILESIZE, rows);
            paletteHit(slot, i * TILESIZE, rows, TILESIZE, TILESIZE, i, 0);
        }
        const int type = b.px % w;
        const Setting* table = hazards ? HAZARD_SETTING[type] : PLATFORM_SETTING[type];
        const short* values = hazards ? b.hazardSet[type] : b.platformSet[type];
        if (b.py == 0)
            paletteSelect(slot, b.px * TILESIZE, rows, TILESIZE, TILESIZE);
        const int column = App::screenWidth / 3;
        for (int k = 0; k + 1 < ph; k++) {
            const int x = (k / 3) * column + 10;
            const int y = rows + TILESIZE + 6 + (k % 3) * 20;
            const bool here = b.py == k + 1;
            paletteHit(slot, x - 6, y - 2, column - 8, 18, b.px, k + 1);
            std::string text = std::string(table[k].name) + ": " + settingText(table[k], values[k]);
            if (here && b.adjusting) {
                //(being changed: sunk in, the arrows either side)
                prompts::fill(x - 6, y - 2, column - 8, 18, SDL_Color {8, 12, 28, 255});
                text = std::string(table[k].name) + ": <" + settingText(table[k], values[k]) + ">";
            }
            //(the hand at the end of its text: the text stays in sight)
            if (here)
                paletteSelect(slot, x - 6, y - 2, column - 8, 18, std::min(x + rm->menu_font_small.getWidth(text.c_str()) + 4, x + column - 20), y + 8);
            rm->menu_font_small.draw(x, y, text.c_str());
        }
        return;
    }
    if (onPage(b, PageBlocks)) {
        //(the level editor's rows; the one in use dotted)
        for (int row = 0; row < BLOCK_ROWS; row++) {
            for (int col = 0; col < BLOCK_ROW_CELLS[row]; col++) {
                bool on = true;
                const short type = blockOfCell(col, row, on);
                const int y = rows + row * (TILESIZE * 3 / 4);
                rm->spr_blocks[0].draw(col * TILESIZE, y, {blockPicture(type, on).x, blockPicture(type, on).y, TILESIZE, TILESIZE * 3 / 4});
                paletteHit(slot, col * TILESIZE, y, TILESIZE, TILESIZE * 3 / 4, col, row);
            }
        }
        paletteSelect(slot, b.px * TILESIZE, rows + b.py * (TILESIZE * 3 / 4), TILESIZE, TILESIZE * 3 / 4);
        return;
    }
    if (onModeObjectsPage(b)) {
        for (int i = 0; i < MODE_OBJECTS; i++) {
            drawBrushPiece(BrushModeObject, b.tile, static_cast<short>(i), (i % 8) * TILESIZE, rows + (i / 8) * TILESIZE);
            paletteHit(slot, (i % 8) * TILESIZE, rows + (i / 8) * TILESIZE, TILESIZE, TILESIZE, i % 8, i / 8);
        }
        paletteSelect(slot, b.px * TILESIZE, rows + b.py * TILESIZE, TILESIZE, TILESIZE);
        return;
    }
    const uint8_t kind = onWarpsPage(b) ? BrushWarp : onBlocksPage(b) ? BrushBlock : onItemsPage(b) ? BrushItem : BrushTile;
    const int across = WARP_CONNECTIONS;
    for (int j = 0; j < STRIP_ROWS && b.scrollY + j < ph; j++) {
        for (int i = 0; i < MAPWIDTH && b.scrollX + i < w; i++) {
            const int col = b.scrollX + i, row = b.scrollY + j;
            const TilesetTile tile = onAnimatedPage(b) ? TilesetTile {TILESETANIMATED, static_cast<short>(row), static_cast<short>(col)}
                                                       : TilesetTile {b.page, static_cast<short>(col), static_cast<short>(row)};
            const bool grid = kind == BrushWarp;
            drawBrushPiece(kind, tile, static_cast<short>(grid ? col + row * across : col), i * TILESIZE, rows + j * TILESIZE);
            paletteHit(slot, i * TILESIZE, rows + j * TILESIZE, TILESIZE, TILESIZE, col, row);
        }
    }
    paletteSelect(slot, (b.px - b.scrollX) * TILESIZE, rows + (b.py - b.scrollY) * TILESIZE, TILESIZE, TILESIZE);
}

// What a builder's brush is, in words (the brush card's)
std::string brushName(const MapMakerBuilder& b)
{
    static const char* const ITEMS[ITEM_TYPES] = {"Spring", "Spike", "Kuribo's shoe", "Golden spring", "Sticky shoe", "Throw box"};
    static const char* const DIRECTIONS[4] = {"up", "right", "down", "left"};
    switch (b.brush) {
    case BrushBlock: {
        std::string name = BLOCK_NAMES[b.block % 30];
        if (b.block >= 7 && b.block <= 14)
            name += b.blockOn ? ", on" : ", off";
        if (b.block == 1 && b.contents >= 0)
            name += ": one powerup";
        else if (b.block == 1 && b.contents == -2)
            name += ": a mix";
        else if (b.block == 1 && b.contents <= -10)
            name += std::string(": ") + ITEM_SET_NAMES[std::clamp(-b.contents - 10 - ITEM_SETS[0], 0, ITEM_SET_CELLS - 2)];
        if (hideable(b.block) && b.blockHidden)
            name += " (hidden)";
        return name;
    }
    case BrushItem: return ITEMS[b.item % ITEM_TYPES];
    case BrushWarp: return "Warp " + std::to_string(b.warpConn + 1) + ", " + DIRECTIONS[b.warpDir & 3];
    case BrushHazard: return HAZARD_NAMES[b.hazard % HAZARD_TYPES];
    case BrushPlatform: return std::string("Platform ") + PLATFORM_KIND_NAMES[b.platformKind % PLATFORM_KINDS];
    case BrushType: return std::string("Type: ") + TILE_TYPE_NAMES[b.typeBrush % TILE_TYPES];
    case BrushSpawn: return SPAWN_NAMES[b.spawnBrush % SPAWN_KINDS];
    case BrushModeObject: return b.modeObject < 8 ? "Race goal " + std::to_string(b.modeObject + 1) : "Flag base, team " + std::to_string(b.modeObject - 7);
    default:
        if (b.tile.iID == TILESETANIMATED)
            return "Animated tile";
        if (const CTileset* tileset = b.tile.iID >= 0 ? g_tilesetmanager->tilesetInfo(static_cast<size_t>(b.tile.iID)) : nullptr)
            return tileset->name() + " tile";
        return "Tile";
    }
}

// A brush's piece number for drawBrushPiece (its item, hazard, block and its state...)
short brushPieceIndex(const MapMakerBuilder& b)
{
    return static_cast<short>(b.brush == BrushItem ? b.item
            : b.brush == BrushWarp ? b.warpConn + b.warpDir * WARP_CONNECTIONS
            : b.brush == BrushHazard ? b.hazard
            : b.brush == BrushPlatform ? b.platformKind
            : b.brush == BrushType ? b.typeBrush
            : b.brush == BrushSpawn ? b.spawnBrush
            : b.brush == BrushModeObject ? b.modeObject
            : static_cast<short>(b.block | (b.blockOn ? 0 : 64) | (b.blockHidden && hideable(b.block) ? 128 : 0)));
}

// The layer button (LAYER_W by 32): the layer in use, "Layer 3" (a click: the next)
constexpr int LAYER_W = 72;
void drawLayerButton(int layer, int x, int y)
{
    const std::string name = "Layer " + std::to_string(layer + 1);
    rm->menu_font_small.drawCentered(x + LAYER_W / 2, y + 8, name.c_str());
}

// The layer view, while a builder has tiles in hand: the map a little darker, the layer in use drawn
// over it bright, the layers in front of it over that as faint ghosts (on layer 1 three of them, on 4
// none): which layer it is shows on the map itself. (Its pictures kept between frames: drawn again when
// the map or the layer changes)
bool s_layerViewDirty = true;
bool s_layerViewOn = false;    // drawn this frame (the map's front picture see-through, from the next)
void drawLayerView(int layer)
{
    static SDL_Surface* veil = nullptr;
    static SDL_Surface* front = nullptr;
    static int frontOf = -1;
    const int w = MAPWIDTH * TILESIZE, h = MAPHEIGHT * TILESIZE;
    if (!veil) {
        veil = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
        front = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
        if (!veil || !front)
            return;
        SDL_FillSurfaceRect(veil, nullptr, SDL_MapSurfaceRGBA(veil, 0, 0, 0, 255));
        SDL_SetSurfaceBlendMode(veil, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(veil, 110);
        SDL_SetSurfaceBlendMode(front, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(front, 90);
    }
    auto tiles = [](SDL_Surface* target, int l) {
        for (short x = 0; x < MAPWIDTH; x++) {
            for (short y = 0; y < MAPHEIGHT; y++) {
                const TilesetTile& tile = *g_map->tileAt(x, y, static_cast<short>(l));
                if (tile.iID >= 0)
                    g_tilesetmanager->Draw(target, tile.iID, DrawSize::Ingame, tile.iCol, tile.iRow, x, y);
            }
        }
    };
    if (s_layerViewDirty || frontOf != layer) {
        SDL_FillSurfaceRect(front, nullptr, SDL_MapSurfaceRGBA(front, 0, 0, 0, 0));
        for (int l = layer + 1; l < MAPLAYERS; l++)
            tiles(front, l);
        frontOf = layer;
        s_layerViewDirty = false;
    }
    SDL_BlitSurface(veil, nullptr, blitdest, nullptr);
    tiles(blitdest, layer);
    s_layerViewOn = true;
    if (layer < MAPLAYERS - 1)
        SDL_BlitSurface(front, nullptr, blitdest, nullptr);
}

// The toolbar: a builder's brush (its picture, its name; a tile's layer under it), its recent brushes
// (LB+Left/Right, Q/E, the wheel, the number keys), its tools and its layer. SMB3's status bar (as the
// help line under it), every square one of its item cards, the one in use framed in the player's color.
// The layer: a button, the layer in words; a click, the next (the map shows which: drawLayerView) The mouse: a click picks one, the pointer
// over a tool or a layer says what it is
bool s_toolbarTip = false;    // a toolbar tooltip drawn this frame (the short message waits: they'd overlap)

void drawBrushCard(int slot, const MapMakerBuilder& b, int barY, bool barAtTop)
{
    s_toolbarTip = false;
    static const char* const TOOL_NAMES[TOOLS] = {"Pencil", "Rectangle", "Fill", "Copy", "Paste"};
    const SDL_Color black {0, 0, 0, 255}, white {252, 252, 252, 255}, sunk {0, 48, 104, 255};
    const SDL_Color color = slotColor(slot);
    const int h = TOOLBAR_H, y = barAtTop ? barY + 24 : barY - h;
    const bool mouseOne = slot == mouseSlot();
    theme::statusBar(0, y, App::screenWidth, h);
    prompts::fill(4, y + 6, 4, h - 12, color);
    if (mouseOne)
        s_toolbarRect = {0, y, App::screenWidth, h};

    //A square of the bar, as SMB3's item cards: black in a white line (its corners cut); the one in use
    //in the player's color, a thicker line (`dim`: in use, but not for this brush); `empty`: sunk
    auto square = [&](int x, int w, bool on, bool dim, bool empty) {
        const int top = y + 4, sh = h - 8;
        const SDL_Color line = on ? (dim ? theme::shade(color, 60) : color) : empty ? sunk : white;
        prompts::fill(x + 1, top, w - 2, sh, black);
        prompts::fill(x, top + 1, w, sh - 2, black);
        prompts::fill(x + 2, top + 1, w - 4, sh - 2, line);
        prompts::fill(x + 1, top + 2, w - 2, sh - 4, line);
        const int in = on ? 3 : 2;
        prompts::fill(x + in, top + in, w - 2 * in, sh - 2 * in, black);
    };

    //The part in hand: its picture and name (cut short to fit); a tile's layer under it
    constexpr int NAME_W = 86;
    auto cut = [](std::string text) {
        if (rm->menu_font_small.getWidth(text.c_str()) <= NAME_W)
            return text;
        while (!text.empty() && rm->menu_font_small.getWidth((text + "...").c_str()) > NAME_W)
            text.pop_back();
        return text + "...";
    };
    drawBrushPiece(b.brush, b.tile, brushPieceIndex(b), 11, y + 6);
    const bool tiles = b.brush == BrushTile;
    rm->menu_font_small.draw(46, y + 14, cut(brushName(b)).c_str());

    //The recent parts: the others, in the order the next goes (numbered for the keys 2..8)
    const int shown = std::min<int>(b.recentCount, MAP_MAKER_RECENT);
    if (mouseOne)
        s_recentShown = std::min(shown, MAP_MAKER_RECENT - 1);
    const bool numbers = s_mouse.active || prompts::lastDevice() == prompts::Device::Keyboard;
    constexpr int RECENT_SHOWN = MAP_MAKER_RECENT - 1;    //(the bar's room: six; the keys 2..7, the wheel past them)
    const int cell = 38, rx = 136;
    for (int k = 1; k < RECENT_SHOWN; k++) {
        const int cx = rx + (k - 1) * cell;
        //(only the parts there: no row of empty boxes)
        if (k >= shown)
            continue;
        square(cx, cell - 2, false, false, false);
        MapMakerBuilder r = b;
        applyRecent(r, b.recent[k]);
        drawBrushPiece(r.brush, r.tile, brushPieceIndex(r), cx + 2, y + 6);
        if (mouseOne)
            s_recentRect[k] = {cx, y + 4, cell - 2, h - 8};
        if (numbers) {
            const std::string key = std::to_string(k + 1);
            prompts::fill(cx + 2, y + 6, 10, 13, black);
            rm->menu_font_small.draw(cx + 3, y + 5, key.c_str());
        }
    }

    //The tools: their pictures at a tile's size, the one in use lit
    const int tx = rx + (RECENT_SHOWN - 1) * cell + 3, tw = cell;
    for (int t = 0; t < TOOLS; t++) {
        const int ix = tx + t * tw;
        square(ix, tw - 2, t == b.tool % TOOLS, false, false);
        drawToolIcon(t, ix + 2, y + 6, color);
        if (mouseOne)
            s_toolRect[t] = {ix, y + 4, tw - 2, h - 8};
    }

    //The layer: a button with its name (dimmer: the brush isn't tiles); a click goes to the next (as the
    //layer key)
    const int lx = tx + TOOLS * tw + 3;
    square(lx, LAYER_W + 4, true, !tiles, false);
    drawLayerButton(b.layer, lx + 2, y + 6);
    for (int l = 0; l < MAPLAYERS; l++)
        s_layerRect[l] = {};
    if (mouseOne)
        s_layerRect[(b.layer + 1) % MAPLAYERS] = {lx, y + 4, LAYER_W + 4, h - 8};

    //(the mouse over a tool or a layer: what it is, over it; the bar at the top: under it)
    if (mouseOne && s_mouse.active) {
        const SDL_Point at {s_mouse.x, s_mouse.y};
        auto tip = [&](const SDL_Rect& r, const std::string& text) {
            if (!inRect(at, r))
                return;
            theme::tooltip(r.x + r.w / 2, barAtTop ? y + h + 32 : y - 2, text);
            s_toolbarTip = true;
        };
        for (int t = 0; t < TOOLS; t++)
            tip(s_toolRect[t], TOOL_NAMES[t]);
        for (int l = 0; l < MAPLAYERS; l++)
            tip(s_layerRect[l], "Next: layer " + std::to_string(l + 1));
    }
}

std::string s_savedPath;    // the last save's file

// The built map saved: "<the map's name> made.map" next to the maps (the data folder, or the home
// folder's copy of it), in the map list from then on; saved again, that file again
std::string saveMadeMap()
{
    CGM_MapMaker* m = mode();
    if (!m)
        return {};
    std::vector<MapItem>& items = g_map->items();
    items.assign(m->items, m->items + m->itemCount);
    g_map->hazards().assign(m->hazards, m->hazards + m->hazardCount);
    g_map->iNumRaceGoals = m->goalCount;
    g_map->iNumFlagBases = m->baseCount;
    for (int k = 0; k < 8; k++)
        g_map->raceGoals()[k] = Point {m->goals[k][0], m->goals[k][1]};
    for (int k = 0; k < 4; k++)
        g_map->flagBases()[k] = Point {m->bases[k][0], m->bases[k][1]};
    //(the switches as the map starts, not as play left them)
    const std::array<short, 4> live = g_map->switches();
    for (int k = 0; k < 4; k++)
        g_map->switches()[k] = m->switches[k];
    std::vector<MovingPlatform*> made;
    for (int k = 0; k < m->platformCount; k++)
        if (m->platforms[k].live)
            made.push_back(m->platforms[k].live);
    std::string name = game_values.mapmaker.blank ? std::string("new map") : maplist ? maplist->currentShortmapname() : std::string("map");
    const std::string suffix = " made";
    if (name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
        name += suffix;
    const std::string file = name + ".map";
    const std::string path = writablePath(convertPath("maps/" + file));
    g_map->saveMap(path, made);
    //(its filters as it is now; its thumbnail made again when it's next shown)
    if (maplist)
        maplist->refreshAutoFilters(path);
    std::remove(mapThumbnailPath(path).c_str());
    g_map->switches() = live;
    s_savedPath = path;
    if (maplist && !maplist->hasFile(path))
        maplist->add(file.c_str());
    return name;
}

// The host's toggles the match has now: no longer pending
void syncWantedRights()
{
    if (CGM_MapMaker* m = mode()) {
        for (int slot = 0; slot < SLOTS; slot++)
            if (s_wantPending[slot] && s_wantRights[slot] == m->rights[slot])
                s_wantPending[slot] = false;
    }
}

// A player's right as the host's panel shows it: its toggle on the way, else the match's
bool shownRight(const CGM_MapMaker& m, int slot)
{
    return s_wantPending[slot] ? s_wantRights[slot] : m.rights[slot];
}

} // namespace

namespace matchedit {

void prepareMap()
{
    if (!game_values.gamemode || game_values.gamemode->gamemode != game_mode_mapmaker || !game_values.mapmaker.blank || !g_tilesetmanager)
        return;
    //(the floor: the classic tileset's first solid tile)
    TilesetTile floor {static_cast<short>(g_tilesetmanager->classicTilesetIndex()), 0, 0};
    if (const CTileset* tileset = g_tilesetmanager->tilesetInfo(static_cast<size_t>(floor.iID))) {
        bool found = false;
        for (short row = 0; row < tileset->height() && !found; row++) {
            for (short col = 0; col < tileset->width() && !found; col++) {
                if (tileset->tileType(col, row) == TileType::Solid) {
                    floor.iCol = col;
                    floor.iRow = row;
                    found = true;
                }
            }
        }
    }
    g_map->makeBlank(floor);
}

void defaultSettings(MapMakerBuilder& b)
{
    for (int t = 0; t < HAZARD_TYPES; t++)
        for (int k = 0; k < HAZARD_SETTINGS; k++)
            b.hazardSet[t][k] = HAZARD_SETTING[t][k].def;
    for (int t = 0; t < PLATFORM_KINDS; t++)
        for (int k = 0; k < PLATFORM_SETTINGS; k++)
            b.platformSet[t][k] = PLATFORM_SETTING[t][k].def;
    b.adjusting = false;
}

bool allowed()
{
    //(the Map Maker, played here: not a replay's, not the determinism test's; the screenshots' script
    //of it: --at-role matchedit)
    const bool script = (cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "matchedit")
        || cmd::g_autotest.onlinePanel;    //(--at-online-panel: the panel's screenshot, its Map Maker rows)
    return mode() && !replay::playing() && !determinism::active() && (!cmd::g_autotest.enabled || script) && g_map && g_tilesetmanager;
}

bool building(int slot)
{
    return ::building(slot);
}

bool active()
{
    for (int slot = 0; slot < SLOTS; slot++) {
        if (building(slot))
            return true;
    }
    return false;
}

bool motionFrozen()
{
    const CGM_MapMaker* m = mode();
    if (!m)
        return false;
    for (int slot = 0; slot < SLOTS; slot++)
        if (building(slot) && m->builders[slot].palette)
            return true;
    return false;
}

bool edited()
{
    const CGM_MapMaker* m = mode();
    return m && m->edited;
}

int hostSlot()
{
    //(the first player of the host's machine; here, the first player at the controls)
    rollback::NetGame& net = rollback::NetGame::instance();
    for (int slot = 0; slot < SLOTS; slot++) {
        if (game_values.playercontrol[slot] == 1 && (!net.inMatch() || net.slotPeer(slot) == 0))
            return slot;
    }
    return 0;
}

void reset()
{
    for (int slot = 0; slot < SLOTS; slot++) {
        s_switchPressed[slot] = false;
        s_lbHeld[slot] = false;
        s_chordHeld[slot] = false;
        s_undoPressed[slot] = false;
        s_scriptByte[slot] = -1;
        s_layerHeld[slot] = false;
    }
    for (int slot = 0; slot < SLOTS; slot++)
        s_wantPending[slot] = false;
    //(the map's front picture opaque again: the layer view's see-through ends with the match)
    for (gfxSprite& front : rm->spr_frontmap) {
        if (front)
            SDL_SetSurfaceAlphaMod(front.getSurface(), 255);
    }
    s_layerViewDirty = true;
    s_rightsSlot = 1;
    s_pauseRow = 0;
    s_shownValid = false;
}

void beginFrame()
{
    for (bool& pressed : s_pausePressed)
        pressed = false;
    for (bool& pressed : s_escPressed)
        pressed = false;
    for (int slot = 0; slot < SLOTS; slot++)
        s_menuKeys.inputControls[slot] = game_values.playerInput.inputControls[slot];
    s_menuKeys.ClearPressedKeys(1);
}

// The mouse's events (in the screen's pixels): kept for the next tick; its pointer shows while it's used
void mouseEvent(const SDL_Event& event)
{
    const bool someone = mouseSlot() >= 0 || game_values.flags.pausegame;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        s_mouse.x = static_cast<int>(event.motion.x);
        s_mouse.y = static_cast<int>(event.motion.y);
        if (std::fabs(event.motion.xrel) + std::fabs(event.motion.yrel) > 0.5f) {
            s_mouse.moved = s_mouse.active = true;
            s_pauseMoved = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        s_mouse.x = static_cast<int>(event.button.x);
        s_mouse.y = static_cast<int>(event.button.y);
        s_mouse.active = true;
        if (event.button.button == SDL_BUTTON_LEFT && down && (SDL_GetModState() & SDL_KMOD_CTRL)) {
            s_mouse.pick = true;    //(Ctrl+click: the eyedropper, not paint)
        } else if (event.button.button == SDL_BUTTON_LEFT) {
            s_mouse.left = down;
            s_mouse.leftClick = s_mouse.leftClick || down;
            if (down) {
                s_pauseClick = true;
                s_pauseClickX = s_mouse.x;
                s_pauseClickY = s_mouse.y;
            }
        } else if (event.button.button == SDL_BUTTON_RIGHT && down && (SDL_GetModState() & SDL_KMOD_CTRL)) {
            s_mouse.grab = true;    //(Ctrl+right click: pick up, not erase)
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            s_mouse.right = down;
            s_mouse.rightClick = s_mouse.rightClick || down;
            s_pauseRightClick = s_pauseRightClick || down;
        } else if (event.button.button == SDL_BUTTON_X1 && down) {
            s_mouse.back = true;
        } else if (event.button.button == SDL_BUTTON_X2 && down) {
            s_mouse.forward = true;
        } else if (event.button.button == SDL_BUTTON_MIDDLE && down) {
            s_mouse.middleClick = true;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        s_mouse.active = true;
        s_mouse.wheel += event.wheel.y > 0 ? -1 : event.wheel.y < 0 ? 1 : 0;
        s_mouse.ctrlWheel = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        break;
    case SDL_EVENT_KEY_DOWN:
        //(the keys: the mouse's not used, but for its Shift and Ctrl)
        if (event.key.key != SDLK_LSHIFT && event.key.key != SDLK_RSHIFT && event.key.key != SDLK_LCTRL && event.key.key != SDLK_RCTRL)
            s_mouse.active = false;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        s_mouse.active = false;
        break;
    default:
        return;
    }
    const bool show = s_mouse.active && someone;
    if (show != s_cursorShown) {
        s_cursorShown = show;
        if (show)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }
}

// Per input device: `apply` to the devices whose players' controls are `from`
template <typename Apply>
void forDevice(SDL_JoystickID from, Apply apply)
{
    for (int device = 0; device < SLOTS; device++) {
        const CInputPlayerControl* control = game_values.playerInput.inputControls[device];
        if (control && control->iDevice == from)
            apply(device);
    }
}

void noteEvent(const SDL_Event& event)
{
    s_menuKeys.Update(event, 1);
    mouseEvent(event);
    //The triggers: the palette's tab before and after (pressed in past halfway; again once let go);
    //the keyboard's Page Up and Page Down
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        const int side = event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : 0;
        forDevice(event.gaxis.which, [&](int device) {
            if (event.gaxis.value > 16000 && !s_triggerHeld[device][side]) {
                s_triggerHeld[device][side] = true;
                s_tabStep[device] = side ? 1 : -1;
            } else if (event.gaxis.value < 8000) {
                s_triggerHeld[device][side] = false;
            }
        });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key == SDLK_PAGEUP || event.key.key == SDLK_PAGEDOWN)) {
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_tabStep[device] = event.key.key == SDLK_PAGEDOWN ? 1 : -1; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.key == SDLK_Q || event.key.key == SDLK_E)) {
        //(Q, E: the recent brushes; R: the eyedropper)
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_recentStep[device] = event.key.key == SDLK_E ? 1 : -1; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_R) {
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_pickPressed[device] = true; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_G) {
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_grabPressed[device] = true; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F1) {
        s_helpToggle = true;
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_T) {
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_toolPressed[device] = true; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key >= SDLK_1 && event.key.key <= SDLK_8
               && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
        //(1: the brush in hand; 2..8: the strip's recent ones, as numbered)
        const int on = static_cast<int>(event.key.key - SDLK_1);
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_recentJump[device] = on; });
    } else if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_CTRL)
               && (event.key.key == SDLK_Y || (event.key.key == SDLK_Z && (event.key.mod & SDL_KMOD_SHIFT)))) {
        //(Ctrl+Y, Ctrl+Shift+Z: redo; the key's own repeat, again)
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_redoPressed[device] = true; });
    }
    //The undo button held (RB, Backspace): it undoes again and again
    if ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
        event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
        //(LB held: redo instead)
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        forDevice(event.gbutton.which, [&](int device) {
            if (down && s_lbHeld[device]) {
                s_redoPressed[device] = s_redoHeld[device] = true;
            } else if (down) {
                s_undoPressed[device] = s_undoHeld[device] = true;
            } else {
                s_undoHeld[device] = s_redoHeld[device] = false;
            }
        });
    } else if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat &&
               event.key.key == SDLK_BACKSPACE) {
        forDevice(DEVICE_KEYBOARD_ID, [&](int device) { s_undoHeld[device] = event.type == SDL_EVENT_KEY_DOWN; });
    }
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (event.key.key == SDLK_ESCAPE)
            s_escHeld = down;
        if (event.key.key == SDLK_DELETE)
            s_deleteHeld = down;
    }
    //A pad's LB (held: L+Up switches building and playing) and RB (undo); the keyboard's Tab (the
    //switch), Backspace or Ctrl+Z (undo). Each for the players of its device.
    SDL_JoystickID from = 0;
    bool* flags = nullptr;
    bool on = true;
    if ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        && event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
        from = event.gbutton.which;
        flags = s_lbHeld;
        on = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
        from = event.gbutton.which;
        flags = s_pausePressed;
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE) {
        from = DEVICE_KEYBOARD_ID;
        flags = s_escPressed;
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_TAB) {
        from = DEVICE_KEYBOARD_ID;
        flags = s_switchPressed;
    } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat
        && (event.key.key == SDLK_BACKSPACE || (event.key.key == SDLK_Z && (event.key.mod & SDL_KMOD_CTRL) && !(event.key.mod & SDL_KMOD_SHIFT)))) {
        from = DEVICE_KEYBOARD_ID;
        flags = s_undoPressed;
    }
    if (!flags)
        return;
    for (int device = 0; device < SLOTS; device++) {
        const CInputPlayerControl* control = game_values.playerInput.inputControls[device];
        if (control && control->iDevice == from)
            flags[device] = on;
    }
}

// The mouse's builder's byte this tick (`used`: else the keys'): a press waiting; a step to where the
// mouse is, A or B held there
uint8_t mouseByte(int slot, const CGM_MapMaker& m, bool& used)
{
    used = false;
    MouseState& ms = s_mouse;
    const MapMakerBuilder& now = m.builders[slot];
    rollback::NetGame& net = rollback::NetGame::instance();
    const int late = net.inMatch() ? net.inputDelay() : 0;
    //(a press waiting, and its let go, first)
    if (!ms.queue.empty()) {
        const uint8_t byte = ms.queue.front();
        ms.queue.erase(ms.queue.begin());
        used = true;
        ms.idle = 0;
        return byte;
    }
    //(what the match will have: its own once it's had every step, or after what can't be foreseen)
    if (ms.wait > 0) {
        ms.wait--;
        return 0;
    }
    if (!ms.predValid || ms.idle > late + 1) {
        ms.pred = now;
        ms.predValid = true;
    }
    ms.idle++;
    auto unforeseen = [&]() {
        ms.predValid = false;
        ms.wait = late + 2;
        ms.take = false;
        ms.aimX = -1;
    };
    MapMakerBuilder& b = ms.pred;
    const SDL_Point at {ms.x, ms.y};
    const bool overStrip = b.palette && inRect(at, s_stripRect);
    if (ms.active) {
        //The palette: the middle button; a tab, a cell clicked (the cursor there, then A); wheeled, a row or
        //(a row alone) a column on; off the strip, back. The right button: back
        if ((ms.back || ms.forward) && ms.queue.empty()) {
            //(the side buttons: undo, redo; in the palette too)
            ms.queue = {ms.back ? B_UNDO : commandByte(CMD_REDO), 0};
            ms.back = ms.forward = false;
            unforeseen();
        } else if (ms.middleClick && ms.queue.empty()) {
            ms.queue = {B_PALETTE, 0};
            unforeseen();
        } else if (b.palette && b.adjusting) {
            if (ms.wheel != 0)
                ms.queue = {ms.wheel > 0 ? B_RIGHT : B_LEFT, 0};
            if (ms.leftClick || ms.rightClick) {
                ms.queue = {B_PAINT, 0};
                unforeseen();
            }
        } else if (b.palette) {
            const PaletteHit* hit = nullptr;
            bool tabsRow = false;
            for (const PaletteHit& h : s_paletteHits) {
                if (!inRect(at, h.r))
                    continue;
                if (h.tab == TAB_WHEEL)
                    tabsRow = true;
                else if (!hit || h.r.w < hit->r.w)
                    hit = &h;    //(the arrows over the tabs: the smaller)
            }
            if (tabsRow && ms.wheel != 0 && !ms.leftClick) {
                const int tab = std::clamp(tabOfPage(b.page) + ms.wheel, 0, CYCLE_TABS - 1);
                ms.queue = {mouseTabByte(tab)};
                openTab(b, tab);
                ms.aimX = -1;
                ms.wheel = 0;
            }
            if (ms.rightClick || (ms.leftClick && !overStrip)) {
                ms.queue = {B_ERASE, 0};
                unforeseen();
            } else if (hit && hit->tab >= 0 && ms.leftClick) {
                ms.queue = {mouseTabByte(hit->tab)};
                openTab(b, hit->tab);
                ms.aimX = -1;
            } else if (hit && hit->tab < 0 && (ms.moved || ms.leftClick)) {
                ms.aimX = hit->px;
                ms.aimY = hit->py;
                ms.take = ms.take || ms.leftClick;
            } else if (ms.wheel != 0 && !tabsRow) {
                int w = 1, h = 1;
                pageSize(b, w, h);
                ms.aimX = h > 1 ? b.px : std::clamp(b.px + ms.wheel, 0, w - 1);
                ms.aimY = h > 1 ? std::clamp(b.py + ms.wheel, 0, h - 1) : b.py;
            }
        } else if (ms.wheel != 0 && ms.ctrlWheel) {
            //The map: Ctrl+wheel, the layer (tiles)
            ms.queue = {B_LAYER, 0};
            unforeseen();
        } else if (ms.wheel != 0) {
            //The map: the wheel, the recent brushes
            ms.queue = {commandByte(ms.wheel > 0 ? CMD_RECENT_NEXT : CMD_RECENT_PREV)};
            unforeseen();
        } else if ((ms.leftClick || ms.rightClick) && inRect(at, s_toolbarRect)) {
            //(the toolbar clicked: a recent part, a tool, a layer; not painting or erasing the map under it)
            for (int k = 1; k < s_recentShown && ms.leftClick; k++) {
                if (!inRect(at, s_recentRect[k]))
                    continue;
                for (int n = 0; n < k; n++)
                    ms.queue.insert(ms.queue.end(), {commandByte(CMD_RECENT_NEXT), 0});
            }
            for (int t = 0; t < TOOLS && ms.leftClick; t++)
                if (inRect(at, s_toolRect[t]))
                    ms.queue = {jumpTo(JUMP_TOOL_AT + t), 0};
            for (int l = 0; l < MAPLAYERS && ms.leftClick; l++)
                if (inRect(at, s_layerRect[l]))
                    ms.queue = {jumpTo(JUMP_LAYER_AT + l), 0};
            ms.left = ms.right = false;
            unforeseen();
        }
    }
    if (b.palette)
        ms.pick = ms.grab = false;
    ms.moved = ms.leftClick = ms.rightClick = ms.middleClick = false;
    ms.wheel = 0;
    if (!ms.queue.empty()) {
        const uint8_t byte = ms.queue.front();
        ms.queue.erase(ms.queue.begin());
        used = true;
        ms.idle = 0;
        return byte;
    }
    if (!ms.active || !ms.predValid)
        return 0;
    if (b.palette) {
        if (ms.aimX < 0 || b.adjusting)
            return 0;
        const int dx = (ms.aimX > b.px) - (ms.aimX < b.px), dy = (ms.aimY > b.py) - (ms.aimY < b.py);
        used = true;
        ms.idle = 0;
        if (dx == 0 && dy == 0) {
            ms.aimX = -1;
            if (!ms.take) {
                used = false;
                return 0;
            }
            ms.queue = {0};
            unforeseen();
            return mouseStepByte(0, 0, true, false);    //(A there)
        }
        const int x = b.px, y = b.py;
        mouseMove(b, dx, dy);
        if (b.px == x && b.py == y) {
            ms.aimX = -1;    //(not to be got to: a row shorter)
            ms.take = false;
        }
        return mouseStepByte(dx, dy, false, false);
    }
    //The map: the cursor to the cell under the mouse; a button held there, painting (or erasing) on the
    //way after
    //(over the toolbar: the cursor stays where it is)
    if (inRect(at, s_toolbarRect) && !ms.left && !ms.right)
        return 0;
    int cx = std::clamp(ms.x / TILESIZE, 0, MAPWIDTH - 1), cy = std::clamp(ms.y / TILESIZE, 0, MAPHEIGHT - 1);
    //(Shift held as a stroke begins: it keeps to that row or column, whichever the mouse goes along more)
    if (!ms.left && !ms.right) {
        ms.lineX = -1;
    } else if (ms.lineX < 0) {
        ms.lineX = cx;
        ms.lineY = cy;
        ms.lineShift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    }
    if (ms.lineShift && ms.lineX >= 0) {
        if (std::abs(cx - ms.lineX) >= std::abs(cy - ms.lineY))
            cy = ms.lineY;
        else
            cx = ms.lineX;
    }
    const int dx = (cx > b.cx) - (cx < b.cx), dy = (cy > b.cy) - (cy < b.cy);
    if ((ms.pick || ms.grab) && dx == 0 && dy == 0) {
        const bool grab = ms.grab && !ms.pick;
        ms.pick = ms.grab = false;
        used = true;
        ms.idle = 0;
        unforeseen();
        return commandByte(grab ? CMD_GRAB : CMD_PICK);
    }
    if (!ms.left && !ms.right)
        ms.stroke = false;
    else if (dx == 0 && dy == 0)
        ms.stroke = true;
    if (dx == 0 && dy == 0 && !ms.stroke)
        return 0;
    //(not painting: straight to the mouse's cell, a column then a row; painting, a cell a tick, every
    //cell on the way)
    if (!ms.stroke) {
        used = true;
        ms.idle = 0;
        if (cx != b.cx) {
            b.cx = static_cast<short>(cx);
            return jumpTo(cx);
        }
        b.cy = static_cast<short>(cy);
        return jumpTo(JUMP_ROWS_AT + cy);
    }
    mouseMove(b, dx, dy);
    used = true;
    ms.idle = 0;
    return mouseStepByte(dx, dy, ms.stroke && ms.left, ms.stroke && ms.right && !ms.left);
}

uint8_t inputByte(int slot, int device, uint8_t gameByte)
{
    CGM_MapMaker* m = mode();
    if (!m || !allowed() || slot < 0 || slot >= SLOTS)
        return gameByte;
    //(the controls sheet up: this machine's builders still)
    if (s_helpOpen && building(slot))
        return 0;
    //The host's rights commands first (one a tick, until the match has them)
    if (slot == m->hostSlot) {
        syncWantedRights();
        for (int s = 0; s < SLOTS; s++) {
            if (s != m->hostSlot && s_wantPending[s] && s_wantRights[s] != m->rights[s])
                return static_cast<uint8_t>(RIGHTS_CMD | (s_wantRights[s] ? 4 : 0) | s);
        }
    }
    //The host's Clear map (its pause menu), building or not
    if (slot == m->hostSlot && s_clearPending) {
        s_clearPending = false;
        return commandByte(CMD_CLEAR);
    }
    //The switch: Tab, or L+Up (as it's pressed)
    const COutputControl& keys = s_menuKeys.outputControls[device & 3];
    const bool chord = s_lbHeld[device & 3] && keys.menu_up.fDown;
    //LB with Left / Right: the recent brush before / after; with Down: the eyedropper; with B: pick up
    //(each as pressed)
    {
        const bool lb = s_lbHeld[device & 3];
        const bool now[5] = {lb && keys.menu_left.fDown, lb && keys.menu_right.fDown, lb && keys.menu_down.fDown,
            lb && keys.menu_cancel.fDown, lb && keys.menu_scrollfast.fDown};
        bool* was = s_lbChord[device & 3];
        if (now[0] && !was[0])
            s_recentStep[device & 3] = -1;
        if (now[1] && !was[1])
            s_recentStep[device & 3] = 1;
        if (now[2] && !was[2])
            s_pickPressed[device & 3] = true;
        if (now[3] && !was[3])
            s_grabPressed[device & 3] = true;
        if (now[4] && !was[4])
            s_toolPressed[device & 3] = true;
        for (int k = 0; k < 5; k++)
            was[k] = now[k];
    }
    const bool flip = s_switchPressed[device & 3] || (chord && !s_chordHeld[device & 3]);
    s_chordHeld[device & 3] = chord;
    s_switchPressed[device & 3] = false;
    const bool undo = s_undoPressed[device & 3];
    s_undoPressed[device & 3] = false;
    if (undo && building(slot)) {
        s_undoHeldTicks[device & 3] = 0;
        return B_UNDO;
    }
    //(held: again after a moment, then about ten a second; a tick between, the undo's a press)
    if (s_undoHeld[device & 3] && building(slot)) {
        const int held = ++s_undoHeldTicks[device & 3];
        if (held > 24 && (held - 24) % 6 == 0)
            return B_UNDO;
    } else {
        s_undoHeldTicks[device & 3] = 0;
    }
    //Redo, as undo (held: again and again)
    const bool redoNow = s_redoPressed[device & 3];
    s_redoPressed[device & 3] = false;
    if (redoNow && building(slot)) {
        s_redoHeldTicks[device & 3] = 0;
        return commandByte(CMD_REDO);
    }
    if (s_redoHeld[device & 3] && building(slot)) {
        const int held = ++s_redoHeldTicks[device & 3];
        if (held > 24 && (held - 24) % 6 == 0)
            return commandByte(CMD_REDO);
    } else {
        s_redoHeldTicks[device & 3] = 0;
    }
    if (flip && building(slot))
        return B_DONE;
    //The eyedropper, the recent brushes (on the map, not in the palette)
    if (building(slot) && !m->builders[slot].palette) {
        if (s_pickPressed[device & 3]) {
            s_pickPressed[device & 3] = false;
            return commandByte(CMD_PICK);
        }
        if (s_grabPressed[device & 3]) {
            s_grabPressed[device & 3] = false;
            return commandByte(CMD_GRAB);
        }
        if (s_toolPressed[device & 3]) {
            s_toolPressed[device & 3] = false;
            return commandByte(CMD_TOOL);
        }
        //(a number: that many nexts, a tick between each (a press each))
        if (s_recentJump[device & 3] > 0) {
            s_recentJumpGap[device & 3] = !s_recentJumpGap[device & 3];
            if (!s_recentJumpGap[device & 3])
                return 0;
            s_recentJump[device & 3]--;
            return commandByte(CMD_RECENT_NEXT);
        }
        if (s_recentStep[device & 3] != 0) {
            const int step = s_recentStep[device & 3];
            s_recentStep[device & 3] = 0;
            return commandByte(step > 0 ? CMD_RECENT_NEXT : CMD_RECENT_PREV);
        }
    } else {
        s_pickPressed[device & 3] = false;
        s_grabPressed[device & 3] = false;
        s_toolPressed[device & 3] = false;
        s_recentJump[device & 3] = 0;
        s_recentStep[device & 3] = 0;
    }
    //The triggers: the palette's tab before or after (the palette shut: it opens)
    if (s_tabStep[device & 3] != 0 && building(slot)) {
        const int step = s_tabStep[device & 3];
        s_tabStep[device & 3] = 0;
        s_mouse.predValid = false;
        const MapMakerBuilder& b = m->builders[slot];
        if (!b.palette)
            return B_PALETTE;
        if (!b.adjusting)
            return mouseTabByte((tabOfPage(b.page) + step + CYCLE_TABS) % CYCLE_TABS);
    }
    if (s_scriptByte[slot] >= 0) {
        const uint8_t byte = static_cast<uint8_t>(s_scriptByte[slot]);
        s_scriptByte[slot] = -1;
        return byte;
    }
    if (!building(slot)) {
        if (slot == mouseSlot() || mouseSlot() < 0)
            s_mouse.predValid = false;
        return flip && m->rights[slot] ? static_cast<uint8_t>(gameByte | B_PALETTE) : gameByte;
    }
    {
        const int owner = paletteOwner(*m);
        const MapMakerBuilder& mine = m->builders[slot];
        if (owner >= 0 && owner != slot && mine.palette && !mine.adjusting && m->builders[owner].page != mine.page) {
            const int d = device & 3;
            if ((s_windowStepGap[d] ^= 1) == 0)
                return 0;
            const MapMakerBuilder& window = m->builders[owner];
            if (tabOfPage(window.page) != tabOfPage(mine.page))
                return mouseTabByte(tabOfPage(window.page));
            return B_LAYER;    //(the tiles: the next tileset, on to the window's)
        }
    }
    if (slot == mouseSlot()) {
        bool used = false;
        const uint8_t byte = mouseByte(slot, *m, used);
        if (used)
            return byte;
    }
    uint8_t byte = 0;
    //(LB held: the arrows are its chords, not the cursor's)
    if (!s_lbHeld[device & 3]) {
        byte |= keys.menu_left.fDown ? B_LEFT : 0;
        byte |= keys.menu_right.fDown ? B_RIGHT : 0;
        byte |= keys.menu_up.fDown ? B_UP : 0;
        byte |= keys.menu_down.fDown ? B_DOWN : 0;
    }
    //(the keyboard's: Delete erases, Escape is the pause (the palette's back); a keyboard player's cancel
    //key erases if it isn't Escape)
    const CInputPlayerControl* control = game_values.playerInput.inputControls[device & 3];
    if (control && control->iDevice == DEVICE_KEYBOARD_ID) {
        const bool escCancels = control->inputGameControls[1].keys[5] == SDLK_ESCAPE;
        byte |= (keys.menu_cancel.fDown && !escCancels) || s_deleteHeld || (s_escHeld && m->builders[slot].palette) ? B_ERASE : 0;
    } else {
        byte |= keys.menu_cancel.fDown && !s_lbHeld[device & 3] ? B_ERASE : 0;    //(LB+B: pick up, not erase)
    }
    byte |= keys.menu_select.fDown ? B_PAINT : 0;
    //(X: the parts window open; in it, shut (as B), the button that opened it; held on, not again)
    const int dx = device & 3;
    if (!keys.menu_random.fDown)
        s_xClosed[dx] = false;
    if (keys.menu_random.fDown && m->builders[slot].palette && !m->builders[slot].adjusting) {
        byte |= B_ERASE;
        s_xClosed[dx] = true;
    } else if (keys.menu_random.fDown && !s_xClosed[dx]) {
        byte |= B_PALETTE;
    }
    const bool layerKey = keys.menu_scrollfast.fDown && !(s_mouse.active && slot == mouseSlot()) && !s_lbHeld[device & 3];
    const MapMakerBuilder& here = m->builders[slot];
    const int d = device & 3;
    if (here.palette) {
        //(the palette's: Y as it was (its pages))
        s_layerHeld[d] = false;
        byte |= layerKey ? B_LAYER : 0;
    } else if (layerKey) {
        //(on the map, held: Up/Down the layer up or down (1 to 4, no wrapping round), the cursor still)
        if (!s_layerHeld[d]) {
            s_layerHeld[d] = true;
            s_layerStepped[d] = false;
            s_layerDir[d][0] = keys.menu_up.fDown;
            s_layerDir[d][1] = keys.menu_down.fDown;
        }
        const bool up = keys.menu_up.fDown, down = keys.menu_down.fDown;
        const int step = up && !s_layerDir[d][0] ? 1 : down && !s_layerDir[d][1] ? -1 : 0;
        s_layerDir[d][0] = up;
        s_layerDir[d][1] = down;
        byte &= ~(B_LEFT | B_RIGHT | B_UP | B_DOWN);
        if (step != 0) {
            s_layerStepped[d] = true;
            const int to = std::clamp(here.layer + step, 0, MAPLAYERS - 1);
            if (to != here.layer)
                return jumpTo(JUMP_LAYER_AT + to);
        }
    } else if (s_layerHeld[d]) {
        //(let go: a tap (no step on the way), the next layer, round from 4 to 1)
        s_layerHeld[d] = false;
        if (!s_layerStepped[d])
            byte |= B_LAYER;
    }
    if (byte == B_DONE)
        byte = B_PALETTE;    //(X and Y together alone: not "done")
    if ((byte & (B_LEFT | B_RIGHT)) == (B_LEFT | B_RIGHT))
        byte &= ~(B_LEFT | B_RIGHT);    //(left and right at once: neither; not the undo button, not the mouse's)
    if ((byte & (B_UP | B_DOWN)) == (B_UP | B_DOWN))
        byte &= ~(B_UP | B_DOWN);       //(up and down too: the mouse's jumps)
    return byte;
}

void simulate(const InputFrame& input)
{
    CGM_MapMaker* m = mode();
    if (!m)
        return;
    //(the first tick: the players who may build start building, if the rules say so)
    if (!m->started) {
        m->started = true;
        if (game_values.mapmaker.start == 0) {
            for (int slot = 0; slot < SLOTS; slot++)
                if (m->rights[slot] && game_values.playercontrol[slot] == 1)
                    setBuilding(*m, slot, true);
        }
    }
    for (int slot = 0; slot < SLOTS; slot++) {
        const uint8_t byte = input.players[slot];
        const uint8_t prev = g_sim.previousInput[slot];
        CPlayer* player = playerOf(slot);
        if (!player)
            continue;
        //The host gives or takes a right
        if (slot == m->hostSlot && (byte & RIGHTS_CMD) == RIGHTS_CMD) {
            const int target = byte & 3;
            if (target != m->hostSlot) {
                m->rights[target] = (byte & 4) != 0;
                if (!m->rights[target])
                    setBuilding(*m, target, false);
            }
            continue;
        }
        //The host clears the map (from its pause menu: building or not)
        if (slot == m->hostSlot && byte == commandByte(CMD_CLEAR)) {
            if (prev != byte) {
                clearMap(*m, slot, ++m->builders[slot].stroke);
                toast(slot, "Map cleared: undo brings it all back");
                if (!sim::isResimulating())
                    ifSoundOnPlay(rm->sfx_bobombsound);
            }
            continue;
        }
        if (!m->rights[slot])
            continue;
        if (player->isEditing()) {
            if (byte == B_DONE && prev != B_DONE) {
                setBuilding(*m, slot, false, true);
            } else {
                MapMakerBuilder& b = m->builders[slot];
                const size_t historyBefore = m->history.size(), redoBefore = m->redo.size();
                const int stagedBefore = m->editCount + m->bulkCount;
                const int editsBefore = m->editCount;
                const MapMakerRecent brushBefore = recentOf(b);
                const short toolBefore = b.tool, layerBefore = b.layer;
                const bool paletteBefore = b.palette;
                builderTick(*m, b, byte, prev);
                //(the first-time hints: each gone once it's done)
                if (!sim::isResimulating() && localHuman(slot)) {
                    if (m->editCount + m->bulkCount > stagedBefore && (mouseButtons(byte) & B_PAINT))
                        tipDone(0);
                    if (b.palette && !paletteBefore)
                        tipDone(1);
                    if (m->history.size() < historyBefore && m->redo.size() > redoBefore)
                        tipDone(2);
                }
                //(what it did, said a moment over this machine's toolbar)
                if (!sim::isResimulating() && localHuman(slot)) {
                    const bool changed = m->editCount + m->bulkCount > stagedBefore;
                    const int command = mouseCommand(byte) && byte != prev ? byte >> 2 : -1;
                    const bool newBrush = !sameRecent(brushBefore, recentOf(b));
                    if (m->history.size() < historyBefore && m->redo.size() > redoBefore) {
                        toast(slot, "Undo");
                    } else if (m->redo.size() < redoBefore && m->history.size() > historyBefore) {
                        toast(slot, "Redo");
                    } else if (command == CMD_GRAB && changed) {
                        toast(slot, "Picked up " + brushName(b) + ": paint to put it down");
                        s_carrying[slot] = true;
                    } else if (command == CMD_PICK && newBrush) {
                        toast(slot, "Copied " + brushName(b));
                    } else if (b.tool != toolBefore) {
                        toast(slot, std::string("Tool: ") + TOOL_SHORT[b.tool % TOOLS]);
                    } else if (b.layer != layerBefore) {
                        toast(slot, "Layer " + std::to_string(b.layer + 1));
                    }
                    if (s_carrying[slot] && command != CMD_GRAB && ((changed && (mouseButtons(byte) & B_PAINT)) || newBrush || b.palette))
                        s_carrying[slot] = false;
                }
                //(the cells it changed: a ring each; taken away red (B, a pick up))
                const bool away = (mouseButtons(byte) & B_ERASE) || (mouseCommand(byte) && (byte >> 2) == CMD_GRAB);
                for (int k = m->editCount >= editsBefore ? editsBefore : 0; k < m->editCount; k++)
                    flashCell(m->edits[k].x, m->edits[k].y, away ? 1 : 0);
                noteRecent(b);
                buildSound(*m, slot, byte, m->editCount + m->bulkCount > stagedBefore,
                    m->history.size() < historyBefore || m->redo.size() < redoBefore,
                    !sameRecent(brushBefore, recentOf(b)));
            }
        } else if (!mouseCommand(byte) && !jumpByte(byte) && (byte & B_PALETTE) && !(prev & B_PALETTE) && game_values.playercontrol[slot] == 1) {
            setBuilding(*m, slot, true);
        }
    }
    commitStaged(*m);    //(the tick's edits into the history)
}

void afterLoadState()
{
    if (CGM_MapMaker* m = mode()) {
        RebuildSwitchBlocks();
        for (int k = 0; k < 3; k++)
            g_map->eyecandy[k] = m->eyecandy[k];
    }
}

void syncVisuals()
{
    if (!mode() || !g_map || !rm)
        return;
    const CGM_MapMaker& m = *mode();
    if (!s_shownValid) {
        //(a new match: its pictures were drawn from its map)
        for (int x = 0; x < MAPWIDTH; x++)
            for (int y = 0; y < MAPHEIGHT; y++)
                for (int l = 0; l < MAPLAYERS; l++)
                    s_shown[x][y][l] = *g_map->tileAt(x, y, l);
        s_shownValid = true;
        s_shownBackground = s_shownMusic = -1;
        s_mapBackground = g_map->szBackgroundFile;
        s_mapMusic = g_map->musicCategoryID;
        return;
    }
    //The background and the music chosen (the map's are the level's data: a save writes them)
    bool newBackground = false;
    if (m.background != s_shownBackground) {
        s_shownBackground = m.background;
        g_map->szBackgroundFile = m.background >= 0 && static_cast<size_t>(m.background) < backgrounds().count()
            ? backgrounds().at(static_cast<size_t>(m.background)).filename().string() : s_mapBackground;
        LoadCurrentMapBackground();
        newBackground = true;
    }
    if (m.music != s_shownMusic) {
        s_shownMusic = m.music;
        g_map->musicCategoryID = m.music >= 0 ? m.music : s_mapMusic;
        //(online, a joiner plays the song the host's game picks: joinerFollowsHostMusic())
        rollback::NetGame& net = rollback::NetGame::instance();
        const bool joiner = net.inMatch() && !net.isHost();
        if (game_values.music && !sim::isResimulating() && !joiner) {
            musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), "", "");
            rm->backgroundmusic[0].stop();    //(stopped before it's let go: the mixer plays it)
            rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
            rm->backgroundmusic[0].play(game_values.playnextmusic, false);
        }
    }
    bool changed = false, whole = false;
    for (int x = 0; x < MAPWIDTH && !whole; x++) {
        for (int y = 0; y < MAPHEIGHT && !whole; y++) {
            bool cell = false;
            for (int l = 0; l < MAPLAYERS; l++) {
                const TilesetTile& now = *g_map->tileAt(x, y, l);
                TilesetTile& was = s_shown[x][y][l];
                if (now.iID != was.iID || now.iCol != was.iCol || now.iRow != was.iRow) {
                    //(an animated tile, there before or now: the animated tiles are set up for the whole map)
                    if (now.iID == TILESETANIMATED || was.iID == TILESETANIMATED)
                        whole = true;
                    was = now;
                    cell = true;
                }
            }
            if (!cell || whole)
                continue;
            changed = true;
            g_map->redrawCell(static_cast<short>(x), static_cast<short>(y), rm->spr_background, rm->spr_backmap[0], rm->spr_frontmap[0]);
            if (rm->useSecondMapLayers(g_map->hasAnimatedTiles())) {
                const SDL_Rect r {x * TILESIZE, y * TILESIZE, TILESIZE, TILESIZE};
                gfx_copyrect(rm->spr_backmap[0].getSurface(), rm->spr_backmap[1].getSurface(), r);
                gfx_copyrect(rm->spr_frontmap[0].getSurface(), rm->spr_frontmap[1].getSurface(), r);
            }
        }
    }
    if (whole || changed)
        s_layerViewDirty = true;
    if (whole || newBackground) {
        for (int x = 0; x < MAPWIDTH; x++)
            for (int y = 0; y < MAPHEIGHT; y++)
                for (int l = 0; l < MAPLAYERS; l++)
                    s_shown[x][y][l] = *g_map->tileAt(x, y, l);
        g_map->refreshVisuals();
        BuildMatchMapLayers();
    } else if (changed) {
        g_map->refreshDrawAreas();
        g_map->fitDrawAreas(rm->spr_frontmap[0]);
    }
}

uint32_t levelHash()
{
    const CGM_MapMaker* m = mode();
    if (!m)
        return 0;
    uint32_t hash = g_map->tilesHash(0);
    for (int k = 0; k < m->itemCount; k++)
        hash = (hash * 16777619u) ^ (static_cast<uint32_t>(m->items[k].itype) | (m->items[k].ix << 8) | (m->items[k].iy << 16));
    return hash;
}

bool rights(int slot)
{
    const CGM_MapMaker* m = mode();
    return m && slot >= 0 && slot < SLOTS && m->rights[slot];
}

bool isHostHere()
{
    const CGM_MapMaker* m = mode();
    return m && localHuman(m->hostSlot);
}

void setPanelRow(int row)
{
    if (row != s_pauseRow)
        s_clearArmed = false;    //(Clear map's "press again": gone once another row's chosen)
    s_pauseRow = row;
}

void hostPanelInput(bool left, bool right, bool down, bool select)
{
    CGM_MapMaker* m = mode();
    if (!m || !isHostHere())
        return;
    syncWantedRights();
    if (down)
        setPanelRow((s_pauseRow + 1) % 3);
    if (s_pauseRow < 0)
        return;
    if (s_pauseRow == 0) {
        //(the players but the host's)
        for (int step = 0; step < SLOTS && (left || right); step++) {
            s_rightsSlot = (s_rightsSlot + (right ? 1 : SLOTS - 1)) % SLOTS;
            if (s_rightsSlot != m->hostSlot)
                break;
        }
        if (s_rightsSlot == m->hostSlot)
            s_rightsSlot = (s_rightsSlot + 1) % SLOTS;
        if (select && game_values.playercontrol[s_rightsSlot] == 1 && playerOf(s_rightsSlot)) {
            s_wantRights[s_rightsSlot] = !shownRight(*m, s_rightsSlot);
            s_wantPending[s_rightsSlot] = true;
        }
    } else if (s_pauseRow == 2 && select) {
        //Clear map (online, where the match can't pause): twice, as the pause menu's
        if (!s_clearArmed) {
            s_clearArmed = true;
        } else {
            s_clearArmed = false;
            s_clearPending = true;
            s_savedNote = "Map cleared: undo brings it back";
            s_savedAt = SDL_GetTicks();
        }
        ifSoundOnPlay(rm->sfx_pause);
    } else if (s_pauseRow == 1 && select) {
        s_savedNote = "Saved as " + saveMadeMap();
        s_savedAt = SDL_GetTicks();
        s_savedThisPause = true;
        ifSoundOnPlay(rm->sfx_pause);
    }
}

namespace {
enum PauseRow { RowResume, RowRights, RowSave, RowClear, RowControls, RowQuit, PAUSE_ROWS };

// A row the local player can use: the rights, Save and Clear are the host's
bool pauseRowUsable(int row)
{
    return (row != RowRights && row != RowSave && row != RowClear) || isHostHere();
}

void pauseMenuDo(int row, bool left, bool right, bool select)
{
    CGM_MapMaker* m = mode();
    if (!m)
        return;
    if (row == RowRights && isHostHere()) {
        s_pauseRow = 0;
        hostPanelInput(left, right, false, select);
    } else if (row == RowSave && select && isHostHere()) {
        s_pauseRow = 1;
        hostPanelInput(false, false, false, true);
    } else if (row == RowClear && select && isHostHere()) {
        //(twice: the first asks; the second clears it, as the host's next tick: one undo brings it back)
        if (!s_clearArmed) {
            s_clearArmed = true;
            ifSoundOnPlay(rm->sfx_pause);
        } else {
            s_clearArmed = false;
            s_clearPending = true;
            game_values.flags.pausegame = false;
        }
    } else if (row == RowControls && select) {
        s_helpOpen = true;
    } else if (row == RowResume && select) {
        game_values.flags.pausegame = false;
    } else if (row == RowQuit && select) {
        game_values.flags.pausegame = false;
        game_values.flags.exitinggame = true;
        game_values.flags.exityes = false;
        game_values.playerInput.ResetKeys();
    }
}
} // namespace

// A builder's scoreboard portrait: its cursor's glove (the same one, its cuff in the player's color), in
// the 32 by 32 the character would have, in place of it
bool drawBuilderPortrait(int slot, int x, int y)
{
    //(a shadow under it, as the characters' sprites stand out from the map: the glove's shape, black, 2 down)
    const std::vector<prompts::Ink> shadow = {{'#', {0, 0, 0, 255}}, {'w', {0, 0, 0, 255}}, {'g', {0, 0, 0, 255}}, {'c', {0, 0, 0, 255}},
        {'C', {0, 0, 0, 255}}};
    prompts::art(x + 5, y + 3, prompts::gloveArt(), shadow, 2);
    prompts::art(x + 3, y + 1, prompts::gloveArt(), prompts::gloveInks(slotColor(slot)), 2);
    return true;
}

bool sheetCoversScreen()
{
    static bool wasOpen = false;
    const bool covers = s_helpOpen || wasOpen;
    wasOpen = s_helpOpen;
    return covers;
}

// The controls sheet's keys (true: it has them this frame)
bool helpInput()
{
    COutputControl* game = game_values.playerInput.outputControls;
    bool close = s_helpToggle && s_helpOpen;
    if (s_helpToggle && !s_helpOpen) {
        s_helpToggle = false;
        s_helpOpen = true;
        return true;
    }
    s_helpToggle = false;
    if (!s_helpOpen)
        return false;
    for (int d = 0; d < SLOTS; d++) {
        const COutputControl& keys = s_menuKeys.outputControls[d];
        close = close || keys.menu_cancel.fPressed || keys.menu_select.fPressed || s_escPressed[d] || s_pausePressed[d];
        s_escPressed[d] = s_pausePressed[d] = false;
        game[d].game_cancel.fPressed = false;
        game[d].game_start.fPressed = false;
    }
    close = close || s_pauseClick || s_pauseRightClick;
    s_pauseClick = s_pauseRightClick = s_pauseMoved = false;
    if (close)
        s_helpOpen = false;
    return true;
}

void pauseMenuInput()
{
    if (!s_menuWasOpen) {
        s_menuWasOpen = true;
        s_menuRow = RowResume;
        s_savedThisPause = false;
        s_clearArmed = false;
    }
    //(any local player's buttons, read as the menus')
    bool up = false, down = false, left = false, right = false, select = false, cancel = false;
    for (int d = 0; d < SLOTS; d++) {
        const COutputControl& keys = s_menuKeys.outputControls[d];
        up = up || keys.menu_up.fPressed;
        down = down || keys.menu_down.fPressed;
        left = left || keys.menu_left.fPressed;
        right = right || keys.menu_right.fPressed;
        select = select || keys.menu_select.fPressed;
        cancel = cancel || keys.menu_cancel.fPressed;
    }
    const int rowBefore = s_menuRow;
    for (int step = 0; step < PAUSE_ROWS && (up || down); step++) {
        s_menuRow = (s_menuRow + (down ? 1 : PAUSE_ROWS - 1)) % PAUSE_ROWS;
        if (pauseRowUsable(s_menuRow))
            break;
    }
    //The mouse: the row under it chosen (a player's square on the rights: that player); clicked, as A;
    //the right button, back to the match
    if (s_pauseMoved || s_pauseClick) {
        const SDL_Point at = s_pauseClick ? SDL_Point {s_pauseClickX, s_pauseClickY} : SDL_Point {s_mouse.x, s_mouse.y};
        for (int row = 0; row < PAUSE_ROWS; row++) {
            if (!inRect(at, s_pauseRowRect[row]) || !pauseRowUsable(row))
                continue;
            s_menuRow = row;
            bool onPlayer = row != RowRights;
            const CGM_MapMaker* m = mode();
            for (int slot = 0; slot < SLOTS && row == RowRights; slot++) {
                if (inRect(at, s_pauseChipRect[slot]) && m && slot != m->hostSlot) {
                    s_rightsSlot = slot;
                    onPlayer = true;
                }
            }
            select = select || (s_pauseClick && onPlayer);
        }
    }
    cancel = cancel || s_pauseRightClick;
    s_pauseMoved = s_pauseClick = s_pauseRightClick = false;
    //(Clear map's "press again": gone once another row's chosen)
    if (s_menuRow != rowBefore || s_menuRow != RowClear)
        s_clearArmed = false;
    if (cancel) {
        game_values.flags.pausegame = false;    //(B: back to the match)
        return;
    }
    pauseMenuDo(s_menuRow, left, right, select);
}

void pauseMenuPress(int row)
{
    s_menuWasOpen = true;
    s_menuRow = row;
    pauseMenuDo(row, false, false, true);
}

// The controls sheet (F1; the pause menu's Controls): every control of building, by what it's for, the
// controller's, the keyboard's and the mouse's side by side
void drawControlsSheet()
{
    using prompts::Pad;
    struct Row {
        const char* what;
        std::vector<Pad> pads;    // one after another with a "+" (from `orFrom` on: a "/"); none: "-"
        const char* keys;         // key caps, ", " between
        const char* mouse;
        int orFrom = 99;
    };
    struct Section {
        const char* title;
        std::vector<Row> rows;
    };
    static const std::vector<Section> SECTIONS = {
        {"Moving and playing",
            {{"Move the cursor", {Pad::DPad}, "Arrows", "move it"}, {"Build / play here", {Pad::LB, Pad::Up}, "Tab", "-"},
                {"Pause, save, clear", {Pad::Start}, "Esc", "-"}}},
        {"Painting",
            {{"Paint", {Pad::A}, "Enter", "left button"}, {"Erase", {Pad::B}, "Delete", "right button"},
                {"Straight line", {}, "-", "Shift + drag"}, {"Next tool", {Pad::LB, Pad::Y}, "T", "click a tool"},
                {"Next layer (round)", {Pad::Y}, "Shift", "click Layer"},
                {"Layer up / down", {Pad::Y, Pad::Up, Pad::Down}, "Shift+Up/Dn", "Ctrl+wheel", 2}}},
        {"Parts",
            {{"Parts palette", {Pad::X}, "Space", "middle button"}, {"Palette tab", {Pad::LT, Pad::RT}, "PgUp, PgDn", "click a tab", 1},
                {"Recent part", {Pad::LB, Pad::Left, Pad::Right}, "Q, E, 2-8", "wheel, click", 2},
                {"Copy what's here", {Pad::LB, Pad::Down}, "R", "Ctrl + left"}, {"Pick up to move", {Pad::LB, Pad::B}, "G", "Ctrl + right"}}},
        {"Undo and more",
            {{"Undo (hold: more)", {Pad::RB}, "Bksp, Ctrl+Z", "back button"}, {"Redo (hold: more)", {Pad::LB, Pad::RB}, "Ctrl+Y", "fwd button"},
                {"Screenshot", {}, "F12", "-"}}},
    };
    const SDL_Color head {0, 40, 104, 255}, dark {8, 16, 48, 255};
    const int x = 20, y = 12, w = 600, h = 456;
    menupanel::box(x, y, w, h);
    rm->menu_font_large.drawCentered(App::screenWidth / 2, y + 10, "Map Maker controls");
    const int colWhat = x + 18, colPad = x + 206, colKeys = x + 330, colMouse = x + 460;
    int ry = y + 40;
    prompts::fill(x + 14, ry, w - 28, 16, dark);
    rm->menu_font_small.draw(colPad, ry + 1, "Controller");
    rm->menu_font_small.draw(colKeys, ry + 1, "Keyboard");
    rm->menu_font_small.draw(colMouse, ry + 1, "Mouse");
    ry += 20;
    for (const Section& section : SECTIONS) {
        prompts::fill(x + 14, ry, w - 28, 16, head);
        rm->menu_font_small.draw(colWhat, ry + 1, section.title);
        ry += 18;
        for (const Row& row : section.rows) {
            rm->menu_font_small.draw(colWhat + 6, ry + 1, row.what);
            int px = colPad;
            if (row.pads.empty())
                rm->menu_font_small.draw(px, ry + 1, "-");
            for (size_t k = 0; k < row.pads.size(); k++) {
                if (k > 0) {
                    const char* join = static_cast<int>(k) >= row.orFrom ? "/" : "+";
                    rm->menu_font_small.draw(px + 1, ry + 1, join);
                    px += rm->menu_font_small.getWidth(join) + 4;
                }
                px += prompts::drawPad(px, ry, row.pads[k]) + 1;
            }
            int kx = colKeys;
            std::string keys = row.keys;
            if (keys == "-") {
                rm->menu_font_small.draw(kx, ry + 1, "-");
            } else {
                for (size_t from = 0; from <= keys.size();) {
                    size_t to = keys.find(", ", from);
                    if (to == std::string::npos)
                        to = keys.size();
                    kx += prompts::drawKey(kx, ry, keys.substr(from, to - from)) + 4;
                    from = to + 2;
                }
            }
            rm->menu_font_small.draw(colMouse, ry + 1, row.mouse);
            ry += 17;    //(17: room for every row over the close prompt)
        }
        ry += 2;
    }
    prompts::drawPrompts(App::screenWidth / 2, y + h - 26, {{{prompts::Pad::B}, {"F1"}, "close"}}, 14);
}

void drawPauseMenu()
{
    syncWantedRights();
    const CGM_MapMaker* m = mode();
    const bool host = isHostHere();
    const int x = 90, y = 40, w = 460, h = 400;
    menupanel::box(x, y, w, h);
    const SDL_Color dark {8, 16, 48, 255};
    const SDL_Color light {248, 248, 248, 255};

    //The title: the mode's hammer, its name, the map's; paused
    std::string mapName = game_values.mapmaker.blank ? std::string("A new map") : maplist ? maplist->currentShortmapname() : std::string();
    rm->menu_mode_small.draw(x + 22, y + 15, {23 * 16, 0, 16, 16});
    rm->menu_font_large.draw(x + 46, y + 12, "Map Maker");
    rm->menu_font_large.drawRightJustified(x + w - 22, y + 12, "Paused");
    rm->menu_font_small.draw(x + 46 + rm->menu_font_large.getWidth("Map Maker") + 12, y + 16, mapName.c_str());
    prompts::fill(x + 18, y + 40, w - 36, 2, light);
    prompts::fill(x + 18, y + 42, w - 36, 1, {0, 40, 104, 255});

    //The rows
    const int rx = x + 20, rw = w - 40, rh = 34;
    auto rowY = [&](int row) { return y + 54 + row * (rh + 6); };
    std::string saveName = game_values.mapmaker.blank ? std::string("new map") : maplist ? maplist->currentShortmapname() : std::string("map");
    if (saveName.size() < 5 || saveName.compare(saveName.size() - 5, 5, " made") != 0)
        saveName += " made";
    const bool justSaved = !s_savedNote.empty() && SDL_GetTicks() - s_savedAt < 4000;
    for (int row = 0; row < PAUSE_ROWS; row++) {
        const int ry = rowY(row);
        const bool usable = pauseRowUsable(row);
        menupanel::row(rx, ry, rw, rh, row == s_menuRow, !usable);
        s_pauseRowRect[row] = {rx, ry, rw, rh};
        const char* names[PAUSE_ROWS] = {"Resume", "Who builds", "Save map", "Clear map", "Controls", "Quit"};
        rm->menu_font_large.draw(rx + 14, ry + 7, names[row]);
        if (row == RowRights) {
            //Each player: its color, a hammer if it may build; faded if it may not; a CPU's gray "CPU"
            //(CPUs don't build); the one the host is on framed
            const int cell = 54;
            const int cx0 = rx + rw - 10 - cell * SLOTS;
            for (int slot = 0; slot < SLOTS; slot++) {
                const int cx = cx0 + slot * cell;
                s_pauseChipRect[slot] = {cx + 3, ry + 4, cell - 6, rh - 8};
                const bool person = game_values.playercontrol[slot] == 1 && playerOf(slot);
                const bool cpu = game_values.playercontrol[slot] == 2;
                const bool may = person && m && (host ? shownRight(*m, slot) : m->rights[slot]);
                SDL_Color ink = person ? slotColor(slot) : SDL_Color {70, 76, 92, 255};
                if (person && !may)
                    ink = theme::shade(ink, 45);
                prompts::fill(cx + 3, ry + 4, cell - 6, rh - 8, ink);
                const std::string label = person ? "P" + std::to_string(slot + 1) : cpu ? "CPU" : "-";
                rm->menu_font_small.draw(cx + 7, ry + 10, label.c_str());
                if (may)
                    rm->menu_mode_small.draw(cx + cell - 23, ry + 9, {23 * 16, 0, 16, 16});
                if (host && s_menuRow == RowRights && slot == s_rightsSlot)
                    cellFrame(cx + 1, ry + 2, cell - 2, rh - 4, light);
            }
        } else if (row == RowClear && s_clearArmed) {
            const int sw = rm->menu_font_small.getWidth("Press again") + 16;
            prompts::fill(rx + rw - 12 - sw, ry + 8, sw, 18, {184, 48, 40, 255});
            rm->menu_font_small.drawRightJustified(rx + rw - 20, ry + 10, "Press again");
        } else if (row == RowSave) {
            //(saved a moment ago: a green "Saved"; else the file's name)
            if (justSaved) {
                const int sw = rm->menu_font_small.getWidth("Saved!") + 16;
                prompts::fill(rx + rw - 12 - sw, ry + 8, sw, 18, {48, 152, 64, 255});
                rm->menu_font_small.drawRightJustified(rx + rw - 20, ry + 10, "Saved!");
            } else {
                rm->menu_font_small.drawRightJustified(rx + rw - 14, ry + 11, (saveName + ".map").c_str());
            }
        }
    }

    //What the chosen row does, in a box under them
    std::string line1, line2;
    switch (s_menuRow) {
    case RowResume:
        line1 = "Back to the match.";
        line2 = prompts::lastDevice() == prompts::Device::Keyboard ? "(Esc or a right click does too, from any row.)" : "(B or Start does too, from any row.)";
        break;
    case RowRights:
        line1 = host ? "Pick a player to let them build, or stop them." : "Who may build. The host chooses.";
        line2 = "Hammer = may build. The host always may.";
        break;
    case RowSave:
        line1 = justSaved ? "Saved as \"" + saveName + "\"." : "Saves the map as it is now, as \"" + saveName + "\".";
        line2 = "It's in the map list from then on.";
        break;
    case RowClear:
        line1 = !host ? "Empties the whole map. The host chooses." : s_clearArmed ? "Sure? Press again to empty the whole map."
                                                                             : "Empties the whole map, to start over.";
        line2 = "Changed your mind? Undo brings it all back.";
        break;
    case RowControls:
        line1 = "Every button, key and mouse control, on one page.";
        line2 = "While building, F1 opens it too.";
        break;
    default:
        line1 = "Ends the match: back to the map and the rules.";
        line2 = s_savedThisPause ? "Your map is saved." : m && m->edited ? "Save first to keep what you built." : "Nothing's been built yet.";
        break;
    }
    const int boxY = rowY(PAUSE_ROWS) + 2;
    prompts::fill(rx, boxY, rw, 46, dark);
    rm->menu_font_small.drawCentered(App::screenWidth / 2, boxY + 7, line1.c_str());
    rm->menu_font_small.drawCentered(App::screenWidth / 2, boxY + 25, line2.c_str());

    using prompts::Pad;
    using prompts::menuKey;

    //What the buttons do here
    prompts::fill(x + 18, y + h - 44, w - 36, 1, {0, 40, 104, 255});
    const bool rights = s_menuRow == RowRights && host;
    std::vector<prompts::Prompt> row {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"}};
    if (rights)
        row.push_back({{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "player"});
    row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, rights ? "allow / stop" : s_menuRow == RowSave ? "save" : s_menuRow == RowClear ? (s_clearArmed ? "clear it" : "clear") : s_menuRow == RowControls ? "open" : s_menuRow == RowQuit ? "quit" : "resume"});
    if (s_menuRow != RowResume && !rights)
        row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "resume"});
    //(too wide (the keys' names): "choose" left out)
    prompts::setLargeLabels(false);
    if (prompts::promptsWidth(row, 14) > w - 24)
        row.erase(row.begin());
    prompts::drawPromptsLarge(App::screenWidth / 2, y + h - 34, row, 14, w - 24);
    if (s_helpOpen)
        drawControlsSheet();
}

void drawPauseRights(int y, bool withSave)
{
    syncWantedRights();
    const CGM_MapMaker* m = mode();
    const bool host = isHostHere();
    //The players' right to build: theirs marked +, the one the host is on framed
    const int cell = 56;
    const int x0 = App::screenWidth / 2 - cell * 2;
    for (int slot = 0; slot < SLOTS; slot++) {
        const int x = x0 + slot * cell;
        const bool present = game_values.playercontrol[slot] == 1 && playerOf(slot);
        const SDL_Color ink = present ? slotColor(slot) : SDL_Color {90, 96, 110, 255};
        prompts::fill(x + 6, y, cell - 12, 24, ink);
        const bool may = m && (host ? shownRight(*m, slot) : m->rights[slot]);
        const std::string label = "P" + std::to_string(slot + 1) + (may && present ? "+" : "");
        rm->menu_font_small.drawCentered(x + cell / 2, y + 6, label.c_str());
        if (host && s_pauseRow == 0 && slot == s_rightsSlot)
            cellFrame(x + 4, y - 2, cell - 8, 28, SDL_Color {255, 255, 255, 255});
    }
    if (withSave && host) {
        //The Save and Clear map buttons under them (or what the last one did)
        const int by = y + 30, bw = 130;
        for (int k = 0; k < 2; k++) {
            const int bx = App::screenWidth / 2 + (k == 0 ? -bw - 6 : 6);
            prompts::fill(bx, by, bw, 22, k == 0 ? SDL_Color {40, 120, 40, 255} : SDL_Color {160, 48, 40, 255});
            const char* label = k == 0 ? "Save map" : s_clearArmed ? "Press again" : "Clear map";
            rm->menu_font_small.drawCentered(bx + bw / 2, by + 5, label);
            if (s_pauseRow == 1 + k)
                cellFrame(bx - 2, by - 2, bw + 4, 26, SDL_Color {255, 255, 255, 255});
        }
    }
    const bool note = !s_savedNote.empty() && SDL_GetTicks() - s_savedAt < 4000;
    const int noteY = withSave && host ? y + 56 : y + 30;
    rm->menu_font_small.drawCentered(App::screenWidth / 2, noteY,
        note ? s_savedNote.c_str() : host ? "+ : may build" : "+ : may build (the host chooses)");
}

} // namespace matchedit

// Each frame of the Map Maker on this machine (from update(), after the input): the pause screen's
// rights and Save (the host's) and quit (Back); in play, Back starts and ends building (taken from the
// quit dialog: it goes in the input byte, matchedit::inputByte)
void GameplayState::updateEditMode()
{
    //(the online panel has the keys while it's up: its own rows for the rights)
    if (!matchedit::allowed() || onlinePanelOpen())
        return;
    //(the controls sheet, while it's up: its keys only)
    if (matchedit::helpInput())
        return;
    COutputControl* game = game_values.playerInput.outputControls;    // read as the game's keys

    if (game_values.flags.pausegame && rollback::NetGame::instance().inMatch() && !rollback::NetGame::instance().isHost())
        return;    //(online, paused by the host: only the host's menu)
    if (game_values.flags.pausegame) {
        //(its menu has the keys: B or Escape resumes, its Quit quits; not the game's quit key too)
        matchedit::pauseMenuInput();
        for (int slot = 0; slot < SLOTS; slot++) {
            game[slot].game_cancel.fPressed = false;
            game[slot].game_start.fPressed = false;
        }
        //(a pad's Start, the keyboard's Escape: back to the match)
        for (int d = 0; d < SLOTS; d++)
            if (s_pausePressed[d] || s_escPressed[d])
                game_values.flags.pausegame = false;
        return;
    }
    s_menuWasOpen = false;    //(not paused: the menu opens on Resume next time)
    s_pauseMoved = s_pauseClick = s_pauseRightClick = false;    //(the mouse's then: not the menu's)
    if (s_cursorShown && mouseSlot() < 0) {
        s_cursorShown = false;    //(the mouse's pointer: only while someone here builds)
        SDL_HideCursor();
    }
    if (game_values.flags.exitinggame)
        return;
    //Building, the game's pause and quit keys are building's (a keyboard's Enter paints): not the game's
    //then. The pause: a pad's Start, the keyboard's Escape (the palette open: its back) (Quit: the pause
    //menu's)
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!localHuman(slot) || !building(slot))
            continue;
        const int device = deviceOf(slot) & 3;
        game[device].game_start.fPressed = false;
        game[device].game_cancel.fPressed = false;
        const bool escape = s_escPressed[device] && !mode()->builders[slot].palette;
        //(online: the host's pause stops everyone, RollbackSession's PAUSE)
        if ((s_pausePressed[device] || escape) && (!rollback::NetGame::instance().inMatch() || rollback::NetGame::instance().isHost()))
            game_values.flags.pausegame = true;
    }

}

// Where a builder's cursor is drawn: its cell; the mouse's builder's, the cell under the mouse (at
// once: the match has it a tick or two later)
SDL_Point shownCell(int slot, const MapMakerBuilder& b)
{
    if (b.palette || slot != mouseSlot() || !s_mouse.active)
        return {b.cx, b.cy};
    return {std::clamp(s_mouse.x / TILESIZE, 0, MAPWIDTH - 1), std::clamp(s_mouse.y / TILESIZE, 0, MAPHEIGHT - 1)};
}

void GameplayState::drawEditMode()
{
    if (!mode())
        return;
    s_toolbarRect = {};    //(drawn again below, if it's up)
    s_drawnFrames++;
    const CGM_MapMaker& m = *mode();
    {
        static short s_weather[3] = {-1, -1, -1};
        static const CGM_MapMaker* s_weatherMode = nullptr;
        if (s_weatherMode != &m || g_sim.tick < 2) {
            s_weatherMode = &m;
            for (int k = 0; k < 3; k++)
                s_weather[k] = g_map->eyecandy[k];
        }
        if (s_weather[0] != g_map->eyecandy[0] || s_weather[1] != g_map->eyecandy[1] || s_weather[2] != g_map->eyecandy[2]) {
            for (int k = 0; k < 3; k++) {
                s_weather[k] = g_map->eyecandy[k];
                eyecandy[k].clean();
            }
            initEyeCandy();
        }
    }
    //(the layer view: this machine's first builder, tiles in hand: its layer shown on the map; the front
    //layers, drawn before this, see-through from the next frame while it's on)
    s_layerViewOn = false;
    for (int slot = 0; slot < SLOTS; slot++) {
        if (building(slot) && localHuman(slot)) {
            const MapMakerBuilder& b = m.builders[slot];
            if (b.brush == BrushTile && !b.palette)
                drawLayerView(b.layer);
            break;
        }
    }
    for (gfxSprite& front : rm->spr_frontmap) {
        if (front)
            SDL_SetSurfaceAlphaMod(front.getSurface(), s_layerViewOn ? 90 : 255);
    }
    //(the types set apart, and the no-spawn zones, shown while a builder has their brush)
    bool showTypes = false, showSpawns = false;
    for (int slot = 0; slot < SLOTS; slot++) {
        if (building(slot)) {
            showTypes = showTypes || m.builders[slot].brush == BrushType;
            showSpawns = showSpawns || m.builders[slot].brush == BrushSpawn;
        }
    }
    for (short x = 0; x < MAPWIDTH && (showTypes || showSpawns); x++) {
        for (short y = 0; y < MAPHEIGHT; y++) {
            if (showTypes) {
                const TileType type = g_map->tileType(x, y);
                if (type != TileType::NonSolid && type != TileType::Gap) {
                    const int t = static_cast<int>(type);
                    drawBrushPiece(BrushType, TilesetTile {}, static_cast<short>(t), x * TILESIZE, y * TILESIZE);
                    if (typeSetApart(x, y))
                        cellFrame(x * TILESIZE + 2, y * TILESIZE + 2, TILESIZE - 4, TILESIZE - 4, SDL_Color {255, 255, 255, 255});
                }
            }
            if (showSpawns) {
                for (short t = 0; t < NUMSPAWNAREATYPES; t++) {
                    if (!g_map->spawn(t, x, y) && (t == 0 || t == 5 || g_map->spawn(0, x, y))) {
                        drawBrushPiece(BrushSpawn, TilesetTile {}, static_cast<short>(t), x * TILESIZE, y * TILESIZE);
                        break;
                    }
                }
            }
        }
    }
    //(the warps and the hazards, shown while someone builds: the warps, the cannons and the plants in
    //their pipes are not seen in play; the hidden blocks, the race goals and flag bases placed too)
    //(only where they matter, not all over the map: all of a kind while a builder has its brush (the
    //hidden blocks: the blocks'), else only near a builder's cursor)
    if (matchedit::active()) {
        bool allHidden = false, allGoals = false, allHazards = false, allWarps = false;
        std::vector<SDL_Point> builders;
        for (int slot = 0; slot < SLOTS; slot++) {
            if (!building(slot))
                continue;
            const MapMakerBuilder& b = m.builders[slot];
            allHidden = allHidden || b.brush == BrushBlock;
            allGoals = allGoals || b.brush == BrushModeObject;
            allHazards = allHazards || b.brush == BrushHazard;
            allWarps = allWarps || b.brush == BrushWarp;
            builders.push_back({b.cx, b.cy});
        }
        auto nearBuilder = [&](int x, int y) {
            for (const SDL_Point& c : builders)
                if (std::abs(x - c.x) <= 4 && std::abs(y - c.y) <= 4)
                    return true;
            return false;
        };
        for (short x = 0; x < MAPWIDTH; x++)
            for (short y = 0; y < MAPHEIGHT; y++)
                if (g_map->blockat(x, y)->fHidden && g_map->blockat(x, y)->iType >= 0 && (allHidden || nearBuilder(x, y)))
                    rm->menu_font_small.draw(x * TILESIZE + 2, y * TILESIZE + 2, "H");
        for (int i = 0; i < m.goalCount; i++)
            if (allGoals || nearBuilder(m.goals[i][0] / TILESIZE, m.goals[i][1] / TILESIZE))
                drawBrushPiece(BrushModeObject, TilesetTile {}, static_cast<short>(i), m.goals[i][0], m.goals[i][1]);
        for (int i = 0; i < m.baseCount; i++)
            if (allGoals || nearBuilder(m.bases[i][0] / TILESIZE, m.bases[i][1] / TILESIZE))
                drawBrushPiece(BrushModeObject, TilesetTile {}, static_cast<short>(8 + i), m.bases[i][0], m.bases[i][1]);
        for (int k = 0; k < m.hazardCount; k++)
            if (allHazards || nearBuilder(m.hazards[k].ix / 2, m.hazards[k].iy / 2))
                hazardsPicture().draw(m.hazards[k].ix * 16 + 4, m.hazards[k].iy * 16 + 4, {static_cast<int>(m.hazards[k].itype) * 24, 32, 24, 24});
        for (short x = 0; x < MAPWIDTH; x++) {
            for (short y = 0; y < MAPHEIGHT; y++) {
                const Warp* warp = g_map->warp(x, y);
                if (warp->connection >= 0 && warp->direction >= 0 && (allWarps || nearBuilder(x, y)))
                    drawBrushPiece(BrushWarp, TilesetTile {}, static_cast<short>(warp->connection + warp->direction * WARP_CONNECTIONS), x * TILESIZE, y * TILESIZE);
            }
        }
    }
    //(a builder of this machine's on the map: the grid round its cursor, marks at the cells' corners, as
    //Mario Maker's; not over the whole map)
    constexpr int GRID_REACH = 3;
    std::vector<SDL_Point> cursors;
    for (int slot = 0; slot < SLOTS; slot++)
        if (building(slot) && localHuman(slot) && !m.builders[slot].palette)
            cursors.push_back({m.builders[slot].cx, m.builders[slot].cy});
    auto nearCursor = [&](int x, int y) {
        for (const SDL_Point& c : cursors)
            if (x >= c.x - GRID_REACH + 1 && x <= c.x + GRID_REACH && y >= c.y - GRID_REACH + 1 && y <= c.y + GRID_REACH)
                return true;
        return false;
    };
    for (short x = 1; x < MAPWIDTH && !cursors.empty(); x++) {
        for (short y = 1; y < MAPHEIGHT; y++) {
            if (!nearCursor(x, y))
                continue;
            //(a small cross, light on a dark shadow: seen on light and dark tiles)
            const int px = x * TILESIZE, py = y * TILESIZE;
            prompts::fill(px - 2, py, 6, 1, SDL_Color {16, 16, 24, 255});
            prompts::fill(px, py - 2, 1, 6, SDL_Color {16, 16, 24, 255});
            prompts::fill(px - 3, py - 1, 6, 1, SDL_Color {236, 236, 244, 255});
            prompts::fill(px - 1, py - 3, 1, 6, SDL_Color {236, 236, 244, 255});
        }
    }
    bool any = false;
    int lowest = 0;
    int lowestTool = 0;
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!building(slot))
            continue;
        any = true;
        const MapMakerBuilder& b = m.builders[slot];
        if (b.cy >= lowest) {
            lowest = b.cy;
            lowestTool = b.tool;
        }
        //The brush at the cursor, framed in the player's color (pulsing), the player's number on it (the
        //mouse's builder: under the mouse, at once)
        const SDL_Point at = shownCell(slot, b);
        const int x = at.x * TILESIZE, y = at.y * TILESIZE;
        drawBrushPiece(b.brush, b.tile,
            static_cast<short>(b.brush == BrushItem ? b.item
                : b.brush == BrushWarp ? b.warpConn + b.warpDir * WARP_CONNECTIONS
                : b.brush == BrushHazard ? b.hazard
                : b.brush == BrushPlatform ? b.platformKind
                : b.brush == BrushType ? b.typeBrush
                : b.brush == BrushSpawn ? b.spawnBrush
                : b.brush == BrushModeObject ? b.modeObject
                : static_cast<short>(b.block | (b.blockOn ? 0 : 64) | (b.blockHidden && hideable(b.block) ? 128 : 0))), x, y);
        //(a ? block with a powerup chosen: the powerup on it, small)
        if (b.brush == BrushBlock && b.contents >= 0)
            rm->spr_storedpowerupsmall.draw(x + 16, y + 16, {b.contents * 16, 0, 16, 16});
        else if (b.brush == BrushBlock && b.contents != -1)
            rm->menu_font_small.drawRightJustified(x + TILESIZE - 1, y + TILESIZE - 14, b.contents == -2 ? "mix" : std::to_string(-b.contents - 10).c_str());
        const bool bright = (SDL_GetTicks() / 300) % 2 == 0;
        if (b.anchoring) {
            //(the rectangle so far: from its corner to the cursor; red erasing)
            const int x0 = std::min(b.ax, b.cx) * TILESIZE, y0 = std::min(b.ay, b.cy) * TILESIZE;
            const int w = (std::abs(b.ax - b.cx) + 1) * TILESIZE, h = (std::abs(b.ay - b.cy) + 1) * TILESIZE;
            cellFrame(x0, y0, w, h, b.anchorErase ? SDL_Color {232, 64, 48, 255} : slotColor(slot));
        }
        if (b.tool == 4 && b.clipW > 0)
            cellFrame(x, y, std::min(b.clipW, static_cast<short>(MAPWIDTH - b.cx)) * TILESIZE, std::min(b.clipH, static_cast<short>(MAPHEIGHT - b.cy)) * TILESIZE,
                SDL_Color {248, 248, 248, 255});
        (void)bright;
    }
    //(the cells just changed: a ring each, growing out from it, a quarter of a second)
    {
        const Uint64 now = SDL_GetTicks();
        constexpr Uint64 FLASH_MS = 240;
        static const SDL_Color FLASH_COLOR[3] = {{248, 248, 248, 255}, {232, 72, 56, 255}, {248, 216, 64, 255}};
        s_flashes.erase(std::remove_if(s_flashes.begin(), s_flashes.end(), [&](const CellFlash& f) { return now - f.at >= FLASH_MS; }),
            s_flashes.end());
        for (const CellFlash& f : s_flashes) {
            const int grow = 1 + static_cast<int>((now - f.at) * 6 / FLASH_MS);
            cellFrame(f.x * TILESIZE - grow, f.y * TILESIZE - grow, TILESIZE + grow * 2, TILESIZE + grow * 2, FLASH_COLOR[f.kind % 3]);
        }
    }
    //(what's under a builder's cursor, named over it, once the cursor's rested there a moment: what B
    //takes, the eyedropper copies; not while painting or erasing)
    static int s_restFrames[SLOTS] {};
    static SDL_Point s_restAt[SLOTS] {};
    for (int slot = 0; slot < SLOTS; slot++) {
        const MapMakerBuilder& b = m.builders[slot];
        const bool moved = s_restAt[slot].x != b.cx || s_restAt[slot].y != b.cy;
        s_restAt[slot] = {b.cx, b.cy};
        s_restFrames[slot] = moved || (g_sim.previousInput[slot] & (B_PAINT | B_ERASE)) ? 0 : std::min(s_restFrames[slot] + 1, 1000);
        if (!building(slot) || !localHuman(slot) || b.palette || b.anchoring || s_restFrames[slot] < 20)
            continue;
        MapMakerBuilder here = b;
        if (!pickBrush(m, here))
            continue;
        //(what B would take, outlined: its cell, a platform all of it)
        const SDL_Color amber {248, 184, 48, 255};
        const int platform = here.brush == BrushPlatform ? platformAt(m, b.cx, b.cy) : -1;
        if (platform >= 0 && m.platforms[platform].live) {
            const MovingPlatform* live = m.platforms[platform].live;
            cellFrame(static_cast<int>(live->centerX()) - live->width() / 2, static_cast<int>(live->centerY()) - live->height() / 2,
                live->width(), live->height(), amber);
        } else {
            cellFrame(b.cx * TILESIZE, b.cy * TILESIZE, TILESIZE, TILESIZE, amber);
        }
        const std::string name = brushName(here);
        const int w = rm->menu_font_small.getWidth(name.c_str()) + 10, h = 18;
        const int x = std::clamp(b.cx * TILESIZE + TILESIZE / 2 - w / 2, 2, App::screenWidth - w - 2);
        const int y = b.cy > 0 ? b.cy * TILESIZE - h - 2 : (b.cy + 1) * TILESIZE + 2;
        prompts::fill(x, y, w, h, SDL_Color {0, 0, 0, 255});
        prompts::fill(x + 1, y + 1, w - 2, h - 2, SDL_Color {24, 32, 64, 255});
        rm->menu_font_small.draw(x + 5, y + 2, name.c_str());
    }
    //(this machine's parts window: its first open one's; the others' hands in it, on their cells, where
    //they're on its page and in sight)
    for (bool& shown : s_paletteCellShown)
        shown = false;
    s_windowHits.clear();
    s_windowSlot = paletteOwner(m);
    if (s_windowSlot >= 0) {
        const MapMakerBuilder& w = m.builders[s_windowSlot];
        const int ms = mouseSlot();
        s_mouseInWindow = ms == s_windowSlot || (ms >= 0 && m.builders[ms].palette && m.builders[ms].page == w.page);
        drawStrip(s_windowSlot, w);
        for (int slot = 0; slot < SLOTS; slot++) {
            const MapMakerBuilder& b = m.builders[slot];
            if (slot == s_windowSlot || !building(slot) || !localHuman(slot) || !b.palette || b.page != w.page)
                continue;
            for (const PaletteHit& hit : s_windowHits) {
                if (hit.tab == -1 && hit.px == b.px && hit.py == b.py) {
                    paletteSelect(slot, hit.r.x, hit.r.y, hit.r.w, hit.r.h);
                    break;
                }
            }
        }
    }
    //(the builders' hands, after it all: over their cells, or their palette's choice; a hand isn't hidden
    //under another's cell or strip. Their "P1" tags only when there's more than one)
    int builders = 0;
    for (int slot = 0; slot < SLOTS; slot++)
        builders += building(slot) ? 1 : 0;
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!building(slot))
            continue;
        const MapMakerBuilder& b = m.builders[slot];
        const bool down = (g_sim.previousInput[slot] & (B_PAINT | B_ERASE)) && g_sim.previousInput[slot] != B_UNDO;
        const SDL_Point at = shownCell(slot, b);
        //(in a parts window: not shown in this one (another machine's, or out of sight): no hand)
        if (b.palette && !s_paletteCellShown[slot])
            continue;
        const SDL_Rect cell = b.palette ? s_paletteCell[slot] : SDL_Rect {at.x * TILESIZE, at.y * TILESIZE, TILESIZE, TILESIZE};
        //(the mouse's: its fingertip on the pointer, exactly)
        const bool mouseHere = !b.palette && slot == mouseSlot() && s_mouse.active;
        const SDL_Point tip = b.palette ? s_paletteTip[slot]
            : mouseHere ? SDL_Point {std::clamp(s_mouse.x, 0, App::screenWidth - 1), std::clamp(s_mouse.y, 0, App::screenHeight - 1)}
                        : SDL_Point {cell.x + TILESIZE / 2, cell.y + TILESIZE / 2};
        drawGlove(slot, cell, tip, slotColor(slot), builders > 1 ? "P" + std::to_string(slot + 1) : std::string(), down, mouseHere);
        if (!b.palette) {
            const uint8_t was = g_sim.previousInput[slot];
            if (was != B_UNDO && (mouseButtons(was) & B_ERASE))
                cellFrame(cell.x + 3, cell.y + 3, TILESIZE - 6, TILESIZE - 6, SDL_Color {232, 64, 48, 255});
            if (b.tool % TOOLS != 0) {
                prompts::fill(cell.x + TILESIZE - 2, cell.y - 14, 18, 18, SDL_Color {0, 0, 0, 255});
                drawToolBadge(b.tool, cell.x + TILESIZE - 1, cell.y - 13, 1);
            }
            if (s_carrying[slot]) {
                //(dashes round the cell, marching)
                const int phase = static_cast<int>(SDL_GetTicks() / 80) % 8;
                for (int k = -8; k < TILESIZE + 8; k += 8) {
                    const int a = std::clamp(k + phase, 0, TILESIZE), e = std::clamp(k + phase + 4, 0, TILESIZE);
                    if (e <= a)
                        continue;
                    prompts::fill(cell.x + a, cell.y - 3, e - a, 2, SDL_Color {248, 248, 248, 255});
                    prompts::fill(cell.x + a, cell.y + TILESIZE + 1, e - a, 2, SDL_Color {248, 248, 248, 255});
                    prompts::fill(cell.x - 3, cell.y + a, 2, e - a, SDL_Color {248, 248, 248, 255});
                    prompts::fill(cell.x + TILESIZE + 1, cell.y + a, 2, e - a, SDL_Color {248, 248, 248, 255});
                }
            }
        }
    }
    //What the builders' buttons do: a bar at the bottom, if one of this machine's players builds
    //(not while one's palette is open: it has its own, and the bar would cover its last row)
    bool local = false, palette = false;
    for (int slot = 0; slot < SLOTS; slot++) {
        local = local || (building(slot) && localHuman(slot));
        palette = palette || (building(slot) && localHuman(slot) && m.builders[slot].palette);
    }
    using prompts::Pad;
    using prompts::menuKey;
    //(always at the bottom (the screen's things don't move about); a cursor under it: the bar see-through,
    //the cells behind it showing: drawn on a layer of its own, put over the map faded)
    const int barY = App::screenHeight - 24;
    const bool barShown = any && local && !palette;
    const SDL_Rect barRect {0, barY - TOOLBAR_H, App::screenWidth, App::screenHeight - (barY - TOOLBAR_H)};
    static SDL_Surface* barLayer = nullptr;
    SDL_Surface* const screen = blitdest;
    const bool underBar = lowest * TILESIZE + TILESIZE > barRect.y;
    if (barShown && underBar && screen && !barLayer)
        barLayer = SDL_CreateSurface(App::screenWidth, App::screenHeight, SDL_PIXELFORMAT_ARGB8888);
    const bool seeThrough = barShown && underBar && screen && barLayer;
    if (seeThrough) {
        SDL_FillSurfaceRect(barLayer, &barRect, SDL_MapSurfaceRGBA(barLayer, 0, 0, 0, 0));
        SDL_SetSurfaceBlendMode(barLayer, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(barLayer, 120);
        blitdest = barLayer;
    }
    if (barShown) {
        theme::statusBar(0, barY, App::screenWidth, 24);
        //(this machine's builder's toolbar: the first of them)
        for (int slot = 0; slot < SLOTS; slot++) {
            if (building(slot) && localHuman(slot)) {
                drawBrushCard(slot, m.builders[slot], barY, barY == 0);
                break;
            }
        }
        //(only the main ones; the rest on the controls sheet. The mouse's buttons if it was used last, else
        //the keys)
        const bool mouse = s_mouse.active;
        std::vector<prompts::Prompt> row {{{Pad::A}, {mouse ? "LMB" : menuKey(prompts::MENU_SELECT)}, lowestTool == 1 ? "rectangle" : lowestTool == 2 ? "fill"
                : lowestTool == 3 ? "copy" : lowestTool == 4 ? "paste" : "paint"},
            {{Pad::B}, {mouse ? "RMB" : "Del"}, "erase"},
            {{Pad::X}, {mouse ? "MMB" : menuKey(prompts::MENU_RANDOM)}, "parts"},
            {{Pad::RB}, {"Bksp"}, "undo"}, {{Pad::LB, Pad::Up}, {"Tab"}, "play here"},
            {{Pad::Start}, {"F1"}, "all controls"}};
        //(the tips done (paint, the parts, undo: known): only these two, the rest on the controls sheet)
        if (tipsDone() == (1 << TIPS) - 1)
            row = {row[4], row[5]};
        int gap = 14;
        const int room = App::screenWidth - 8;
        if (prompts::promptsWidth(row, gap) > room)
            gap = 8;
        if (prompts::promptsWidth(row, gap) > room && row.size() > 5)
            row[5].label = "help";
        prompts::drawPrompts(App::screenWidth / 2, barY + 2, row, gap);
    }
    if (seeThrough) {
        blitdest = screen;
        SDL_Rect to = barRect;
        SDL_BlitSurface(barLayer, &barRect, screen, &to);
    }
    //A first-time hint (the first not done yet), at the top: what to press, for this machine's builder
    if (barShown && tipsDone() != (1 << TIPS) - 1) {
        const bool mouse = s_mouse.active;
        int tip = 0;
        while ((tipsDone() >> tip) & 1)
            tip++;
        const prompts::Prompt hint = tip == 0 ? prompts::Prompt {{Pad::A}, {mouse ? "LMB" : menuKey(prompts::MENU_SELECT)}, "paints (hold it and move: a line)"}
            : tip == 1 ? prompts::Prompt {{Pad::X}, {mouse ? "MMB" : menuKey(prompts::MENU_RANDOM)}, "opens the parts: blocks, items, hazards..."}
                       : prompts::Prompt {{Pad::RB}, {"Bksp"}, "undoes (as far back as you like)"};
        const std::string head = "Tip " + std::to_string(tip + 1) + " of " + std::to_string(TIPS) + ":";
        const int hw = rm->menu_font_small.getWidth(head.c_str());
        const int w = hw + 10 + prompts::promptsWidth({hint}, 14) + 24, h = 24;
        const int x = App::screenWidth / 2 - w / 2, y = barY == 0 ? 24 + TOOLBAR_H + 34 : 30;
        prompts::fill(x, y, w, h, SDL_Color {0, 0, 0, 255});
        prompts::fill(x + 1, y + 1, w - 2, h - 2, SDL_Color {200, 152, 24, 255});
        prompts::fill(x + 2, y + 2, w - 4, h - 4, SDL_Color {24, 40, 96, 255});
        rm->menu_font_small.draw(x + 12, y + 5, head.c_str());
        prompts::drawPrompts(x + 12 + hw + 10 + prompts::promptsWidth({hint}, 14) / 2, y + 4, {hint}, 14);
    }
    //A short message (what was just done), over the toolbar a moment
    if (!s_toast.empty() && SDL_GetTicks() - s_toastAt < 1600 && !palette && !(barShown && s_toolbarTip)) {
        const int tw = rm->menu_font_small.getWidth(s_toast.c_str()) + 20, th = 20;
        const int tx = App::screenWidth / 2 - tw / 2;
        const int ty = !barShown ? App::screenHeight - th - 6 : barY == 0 ? 24 + TOOLBAR_H + 6 : barY - TOOLBAR_H - th - 6;
        prompts::fill(tx, ty, tw, th, SDL_Color {0, 0, 0, 255});
        prompts::fill(tx + 1, ty + 1, tw - 2, th - 2, SDL_Color {24, 40, 96, 255});
        rm->menu_font_small.drawCentered(App::screenWidth / 2, ty + 3, s_toast.c_str());
    }
    //The controls sheet over it all (paused: the pause menu draws it)
    if (s_helpOpen && !game_values.flags.pausegame)
        matchedit::drawControlsSheet();
}

// --at-role matchedit: a Map Maker match (started from the menus: MenuAutoTest); player 1 builds (Back) while the others play: a row
// of tiles, a tile from the palette, ? blocks, a spring; done (Back), it plays; the pause screen, Save.
// It plays the builder's input bytes (s_scriptByte), as a person's would be. Screenshots e0..e7.
// The script's Tab: a key event, as the keyboard sends it (building starts or ends)
void pressTab()
{
    for (const bool down : {true, false}) {
        SDL_Event key {};
        key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        key.key.key = SDLK_TAB;
        key.key.down = down;
        SDL_PushEvent(&key);
    }
}

bool GameplayState::autoTestEditScript(int f)
{
    static uint64_t s_startHash = 0;    // the map as the building begins (the undo test's)
    static uint64_t s_markHash = 0;     // the map at a mark (the eyedropper's and Clear map's tests)
    static MapMakerRecent s_markRecent {};
    static uint64_t s_builtHash = 0;    // the map built, before it's all undone (the redo test's)
    auto shot = [](const char* name) {
        if (!cmd::g_autotest.norender)
            gfx_save_screenshot(cmd::g_autotest.shotsDir + "/" + name + ".png");
    };
    CGM_MapMaker* m = mode();
    if (!m || !matchedit::allowed())
        return false;
    //The builder's input, a byte a frame, as a person's would be: a row of tiles; the palette, a tile two
    //rows down taken and painted; the palette paged to the blocks, a ? block taken, a row of them; the
    //items' page, a spring. (256 + k: screenshot k there.)
    static std::vector<int> steps;
    static size_t step = 0;
    //(--at-pace N: each byte held N frames, a person's pace to watch; the pause and Save then follow
    //the building's end, slower too)
    const int pace = std::max(1, cmd::g_autotest.pace);
    static int hold = 0, held = 0, doneAt = -1;
    if (f == 70) {
        hold = 0;
        held = 0;
        doneAt = -1;
        s_startHash = builtMapHash(*m);
        if (!building(0))
            pressTab();    //(the rules: building from the start; else the switch)
        steps.clear();
        step = 0;
        auto add = [&](std::initializer_list<int> bytes, int times = 1) {
            for (int t = 0; t < times; t++)
                steps.insert(steps.end(), bytes);
        };
        add({0}, 18);
        if (cmd::g_autotest.mmDemo == 3) {
            //(--at-mm-demo 3, the layer button: the mouse onto it (908), its tooltip read, clicked (907 prints
            //the layer), a tile painted on it a row down; four times round)
            add({0}, 170);    //(the "Map Maker" title gone first)
            for (int k = 0; k < MAPLAYERS; k++) {
                add({908 + k});
                add({906}, 60);
                if (k == 0)
                    add({560});    //(a shot: the layer view, on the layer it started on)
                add({900, 906, 906, 901, 906, 907});
                add({906}, 60);
                add({600 + 6 + k * 2 + 12 * MAPWIDTH});
                add({906}, 20);
                add({900, 906, 906, 901});
                add({906}, 40);
            }
            add({0}, 60);
            return false;
        }
        if (cmd::g_autotest.mmDemo == 2) {
            //(--at-mm-demo 2, the README's: each part put on its cell (the cursor's jumps; the palette's tabs
            //straight): a spring on the wooden block, a fire bar between the big blocks (its length changed
            //in the palette: 561, the shot), a row of ? blocks in the sky; the cursor resting on one, its name
            //over it (560); the controls sheet (563))
            auto at = [&](int x, int y) { add({jumpTo(x), 0, jumpTo(JUMP_ROWS_AT + y), 0}); };
            add({0}, 170);    //(the "Map Maker" title gone first)
            add({B_PALETTE, 0, mouseTabByte(PageItems + 1), 0, B_PAINT, 0});
            at(1, 6);
            add({B_PAINT, 0});
            add({B_PALETTE, 0, mouseTabByte(PageHazards + 1), 0, B_DOWN, 0, B_PAINT, 0, B_LEFT, 0});
            add({0}, 50);
            add({561, B_PAINT, 0, B_UP, 0, B_ERASE, 0});
            at(11, 6);
            add({B_PAINT, 0});
            add({B_PALETTE, 0, mouseTabByte(PageBlocks + 1), 0, B_RIGHT, 0, B_PAINT, 0});
            at(9, 2);
            add({B_PAINT | B_RIGHT, B_PAINT}, 3);
            add({B_PAINT, 0});
            at(11, 2);
            add({0}, 70);
            add({560});
            add({0}, 4);
            add({562});
            add({0}, 6);
            add({563, 564});
            add({0}, 30);
            return false;
        }
        if (cmd::g_autotest.mmDemo) {
            //(--at-mm-demo: a few things painted, so there are recent parts; on through them and back; the
            //eyedropper; the pause menu's Clear map, pressed twice; one undo: the map back. 530 pauses, 532
            //the row below, 531 presses it)
            add({B_DOWN, 0}, 3);
            add({B_PAINT | B_RIGHT, B_PAINT}, 6);
            add({B_PALETTE, 0, B_DOWN, 0, B_DOWN, 0, B_PAINT, 0});
            add({B_UP, 0}, 3);
            add({B_PAINT | B_RIGHT, B_PAINT}, 3);
            add({B_PALETTE, 0, B_PALETTE, 0, B_RIGHT, 0, B_PAINT, 0});
            add({B_UP, 0}, 3);
            add({B_PAINT | B_LEFT, B_PAINT, B_PAINT}, 4);
            add({B_PALETTE, 0, B_PALETTE, 0, B_PALETTE, 0, B_PAINT, 0});
            add({B_RIGHT, 0}, 6);
            add({B_PAINT, 0});
            add({0}, 8);
            for (const int dir : {CMD_RECENT_NEXT, CMD_RECENT_NEXT, CMD_RECENT_NEXT, CMD_RECENT_PREV, CMD_RECENT_PREV, CMD_RECENT_PREV}) {
                add({commandByte(dir), 0});
                add({0}, 5);
            }
            add({B_LEFT, 0}, 5);
            add({0}, 4);
            add({commandByte(CMD_PICK), 0});
            add({0}, 6);
            add({B_DOWN, 0}, 2);
            add({B_PAINT | B_RIGHT, B_PAINT}, 4);
            add({0}, 8);
            //(the last one picked up, carried up and over, put down)
            add({commandByte(CMD_GRAB), 0});
            add({0}, 4);
            add({B_UP, 0}, 3);
            add({B_RIGHT, 0}, 3);
            add({B_PAINT, 0});
            add({0}, 8);
            add({530});
            add({0}, 6);
            add({532, 0, 0, 532, 0, 0, 532});
            add({0}, 6);
            add({531});
            add({0}, 12);
            add({531});
            add({0}, 16);
            add({B_UNDO, 0});
            add({0}, 20);
            return false;
        }
        //(the keyboard's keys while building: Enter doesn't pause, Escape does, and goes back)
        add({514, 0, 0, 517, 516, 0, 0, 517, 516, 0, 0, 517});
        add({256 + 1});
        add({B_DOWN, 0}, 3);
        add({B_PAINT | B_RIGHT, B_PAINT}, 10);
        add({256 + 2});
        add({B_PALETTE, 0, B_DOWN, 0, B_DOWN, 0});
        add({256 + 3});
        add({B_PAINT, 0});
        add({B_UP, 0}, 3);
        add({B_PAINT, B_PAINT, 0});
        //(the palette opens on the brush's tileset: the blocks are the page after the last tileset)
        const int tilesets = static_cast<int>(g_tilesetmanager->count());
        const int page = m->builders[0].tile.iID >= 0 ? m->builders[0].tile.iID : 0;
        //(the tools, the last page: from the brush's tileset, the pages after it. Four cells up, a
        //rectangle of the tile 3 by 3 (A held across it); then the fill, B in it: it's erased; the undo
        //button: it's back; the pencil again, the cursor where it was)
        //(the tools: the toolbar's, clicked)
        add({B_UP, 0}, 4);
        add({jumpTo(JUMP_TOOL_AT + 1), 0, 256 + 27});
        add({B_PAINT, B_PAINT | B_RIGHT, B_PAINT, B_PAINT | B_RIGHT, B_PAINT, B_PAINT | B_DOWN, B_PAINT, B_PAINT | B_DOWN, B_PAINT, 0, 256 + 17});
        add({jumpTo(JUMP_TOOL_AT + 2), 0, B_ERASE, 0, 256 + 18});
        add({512, 513, 0, 0, 256 + 19});
        add({jumpTo(JUMP_TOOL_AT), 0, B_LEFT, 0, B_LEFT, 0, B_DOWN, 0, B_DOWN, 0});
        add({B_DOWN, 0}, 2);
        //(the old level editor's: each page as a person would use it. The palette opens on the brush's
        //page; X goes on through the pages, round)
        int open = page;
        auto go = [&](int target) {
            add({B_PALETTE, 0});
            add({B_PALETTE, 0}, (tabOfPage(target) - tabOfPage(open) + CYCLE_TABS) % CYCLE_TABS);
        };
        auto extra = [&](int pageOfExtra) { return tilesets + pageOfExtra; };
        //(a type set apart: death, two cells; a no-spawn zone, team 1's, two cells)
        go(extra(PageTypes));
        add({B_RIGHT, 0}, 4);
        add({B_PAINT, 0});
        open = extra(PageTypes);
        add({B_PAINT, B_PAINT | B_RIGHT, B_PAINT, 0, 256 + 20});
        go(extra(PageSpawns));
        add({B_RIGHT, 0, B_PAINT, 0});
        open = extra(PageSpawns);
        add({B_DOWN, 0, B_PAINT, B_PAINT | B_LEFT, B_PAINT, 0, 256 + 21});
        //(an animated tile; snow in the middle layer)
        go(extra(PageAnimated));
        add({B_RIGHT, 0, B_RIGHT, 0, B_DOWN, 0, B_PAINT, 0});
        open = extra(PageAnimated);
        add({B_DOWN, 0, B_PAINT, 0, 256 + 22});
        go(extra(PageWeather));
        add({B_RIGHT, 0}, 3);
        add({B_DOWN, 0, B_PAINT, 0, 256 + 23, B_ERASE, 0});
        //(race goal 1 placed)
        go(extra(PageModeObjects));
        add({B_PAINT, 0});
        open = extra(PageModeObjects);
        add({B_UP, 0, B_PAINT, 0});
        //(a red on/off block that starts off; a hidden ? block (Y on the page))
        go(extra(PageBlocks));
        add({B_DOWN, 0, B_DOWN, 0, B_PAINT, 0});
        open = extra(PageBlocks);
        add({B_RIGHT, 0, B_PAINT, 0});
        go(extra(PageBlocks));
        add({B_UP, 0, B_UP, 0, B_RIGHT, 0, B_LAYER, 0, B_PAINT, 0});
        add({B_RIGHT, 0, B_PAINT, 0, 256 + 24});
        //(a platform of the map's tiles there, two tall: Height, then Made of; taken; made two cells up)
        go(extra(PagePlatforms));
        open = extra(PagePlatforms);
        add({B_DOWN, 0, B_DOWN, 0, B_PAINT, 0, B_RIGHT, 0, B_PAINT, 0});
        add({B_DOWN, 0, B_PAINT, 0, B_RIGHT, 0, B_PAINT, 0});
        add({B_UP, 0, B_UP, 0, B_UP, 0, B_PAINT, 0});
        add({B_UP, 0}, 2);
        add({B_PAINT, 0});
        //(copy, 3 by 2; paste, four cells on; the undo button: the paste gone)
        add({jumpTo(JUMP_TOOL_AT + 3), 0});
        add({B_PAINT, B_PAINT | B_RIGHT, B_PAINT, B_PAINT | B_RIGHT, B_PAINT, B_PAINT | B_DOWN, B_PAINT, 0});
        add({jumpTo(JUMP_TOOL_AT + 4), 0});
        add({B_RIGHT, 0}, 4);
        add({B_PAINT, 0, 256 + 25});
        add({512, 513, 0, 0, 256 + 26});
        //(back: the pencil; a tile of the brush's tileset, its first: the next steps open there)
        add({jumpTo(JUMP_TOOL_AT), 0});
        go(page);
        add({B_PAINT, 0});
        open = page;
        add({B_PALETTE, 0});
        add({B_PALETTE, 0}, 1);    //(the tiles' tab, then the blocks')
        add({B_RIGHT, 0, B_PAINT, 0});    //(the second block: the ? block)
        add({B_UP, 0}, 3);
        add({B_PAINT | B_LEFT, B_PAINT, B_PAINT}, 4);
        //(the ? block's page: the third powerup there, a ? block of it a row down)
        add({B_PALETTE, 0, B_PALETTE, 0, B_DOWN, 0});
        add({B_RIGHT, 0}, 2);
        add({B_PAINT, 0, 256 + 9, B_ERASE, 0});
        add({B_DOWN, 0, B_PAINT, 0, B_UP, 0});
        add({B_PALETTE, 0, B_PALETTE, 0, B_PALETTE, 0, B_PAINT, 0});    //(the palette on the blocks; past the ? block's page: the items; the spring)
        add({B_DOWN, 0}, 2);
        add({B_PAINT, 0});
        //(the warps' page after the items: a warp two wide, its other end further along)
        add({B_PALETTE, 0, B_PALETTE, 0, 256 + 11, B_PAINT, 0});
        add({B_UP, 0}, 2);
        add({B_PAINT | B_RIGHT, B_PAINT}, 2);
        add({B_RIGHT, 0}, 6);
        add({B_PAINT | B_RIGHT, B_PAINT}, 2);
        add({256 + 10});
        //(the hazards' page after the warps: the fire bar, its length (A, right twice: 7, A), painted; two
        //cells down, a flame cannon facing up (its facing: A, right twice, A))
        add({B_PALETTE, 0, B_PALETTE, 0, B_DOWN, 0, B_PAINT, 0, B_RIGHT, 0, B_RIGHT, 0, 256 + 12, B_PAINT, 0, B_ERASE, 0});
        add({B_PAINT, 0});
        //(the brush's settings set back to the fire bar's first; the eyedropper on it: its length (7) again,
        //painted, the same fire bar (522))
        add({521, B_PALETTE, 0, B_DOWN, 0, B_PAINT, 0, B_LEFT, 0, B_LEFT, 0, B_PAINT, 0, B_ERASE, 0});
        add({commandByte(CMD_PICK), 0, B_PAINT, 0, 0, 522});
        add({B_DOWN, 0}, 2);
        add({B_LEFT, 0}, 4);
        add({B_PALETTE, 0, B_RIGHT, 0, B_RIGHT, 0, B_RIGHT, 0, B_DOWN, 0, B_DOWN, 0, B_PAINT, 0, B_RIGHT, 0, B_RIGHT, 0, B_PAINT, 0, B_ERASE, 0});
        add({B_PAINT, 0});
        //(the platforms' page after them: back and forth, as it starts (3 wide, right 4), of the tile brush's
        //tile, mid-map up high; then six cells on, one in a circle 2 tiles across)
        add({B_LEFT, 0}, 11);
        add({B_UP, 0}, 2);
        add({B_PALETTE, 0, B_PALETTE, 0, 256 + 13, B_PAINT, 0, B_PAINT, 0});
        add({B_RIGHT, 0}, 6);
        add({B_PALETTE, 0, B_RIGHT, 0, B_RIGHT, 0, B_DOWN, 0, B_DOWN, 0, B_PAINT, 0, B_LEFT, 0, B_PAINT, 0, B_UP, 0, B_UP, 0, B_PAINT, 0});
        add({B_PAINT, 0});
        add({0}, 30);
        add({256 + 14});
        //(the undo button, Backspace (a real key event), twice: the two platforms taken back)
        add({512, 513, 0, 0, 256 + 15, 512, 513, 0, 0});
        add({0}, 4);
        add({256 + 16});
        //(the palette again, on the platforms: the next page, the backgrounds, the fourth taken; then the
        //music's page, the third category)
        add({B_PALETTE, 0, B_PALETTE, 0});
        add({B_RIGHT, 0}, 3);
        add({B_PAINT, 0});
        add({B_PALETTE, 0, B_PALETTE, 0, B_PALETTE, 0});    //(it opens on the platforms: the backgrounds, the music)
        add({B_RIGHT, 0}, 2);
        add({256 + 8});
        add({B_PAINT, 0});
        add({256 + 4});
        //(the mouse (600 + a cell: it moves there; 900/901 its left button down/up, 902/903 the right's,
        //904 the middle's click; 906 a tick of it alone): dragged, a row of the brush; the right button
        //dragged, a gap in it; the middle button, the palette; a tab clicked, a cell; the right button)
        add({0, 0, 904});
        add({906}, 8);
        add({920, 906, 906, 906, 920, 906, 906, 906, 920, 906, 906, 906, 950 + 2});    //(the tiles' tab (its arrow, till it's seen), a tile)
        add({906}, 10);
        add({600 + 3 + 11 * MAPWIDTH});
        add({906}, 24);
        add({900, 906, 906, 600 + 9 + 11 * MAPWIDTH});
        add({906}, 10);
        add({901, 906, 906, 600 + 5 + 11 * MAPWIDTH});
        add({906}, 6);
        add({902, 906, 906, 600 + 6 + 11 * MAPWIDTH});
        add({906}, 4);
        add({903, 906, 906, 256 + 28, 904});
        add({906}, 8);
        add({920 + PageItems + 1, 906, 906, 906, 906, 256 + 29, 950 + 2});
        add({906}, 10);
        add({600 + 12 + 9 * MAPWIDTH});
        add({906}, 12);
        add({900, 906, 906, 901, 906, 906, 906, 256 + 30});
        //(the recent brushes: on one and back, the brush as it was (519, 520); the eyedropper where the
        //cursor is, painted there again: the map as it was (521, 522); the host's Clear map (the online
        //panel's, pressed twice: 525), the map empty (523); undone once, all back (524))
        add({0, 0, 519, 256 + 32, commandByte(CMD_RECENT_NEXT), 0, 256 + 33, commandByte(CMD_RECENT_PREV), 0, 520});
        add({B_LEFT, 0}, 3);
        add({521, commandByte(CMD_PICK), 0, 256 + 34, B_PAINT, 0, 0, 522});
        //(Clear map first: the picking up after it changes the map, so the clear's redo goes)
        add({525, 525, 0, 0, 0, 523, 256 + 35, B_UNDO, 0, 0, 524, 256 + 36, 526});
        //(picked up (to move it): gone from there, two cells on (528); back, put down: as it was; undone
        //twice (the putting down, the picking up), as it was too (529))
        add({521, commandByte(CMD_GRAB), 0, B_RIGHT, 0, B_RIGHT, 0, 528, B_LEFT, 0, B_LEFT, 0, B_PAINT, 0, 0, 529});
        add({B_UNDO, 0, 0, B_UNDO, 0, 0, 529});
        //(the computer's keys and buttons, as real events: Backspace undoes, Ctrl+Y redoes; the mouse's
        //back button undoes, its forward redoes (536: the map as it was, each); T the next tool (538); 4,
        //the strip's fourth (539, 545))
        add({535, 512, 513, 0, 0, 0, 546, 540, 0, 0, 0, 536});
        add({543, 906, 906, 906, 906, 546, 544, 906, 906, 906, 906, 536});
        //(the mouse across the map, no button: the cursor there at once, a tick a column then a row (548))
        add({600 + 2 + 13 * MAPWIDTH, 906, 906, 906, 906, 906, 906, 906, 906, 906, 906});
        add({600 + 18 + 1 * MAPWIDTH, 906, 906, 906, 548});
        add({537, 541, 0, 0, 0, 538, 541, 541, 0, 0});
        add({539, 542, 0, 0, 0, 0, 0, 0, 0, 0, 545});
        add({jumpTo(JUMP_TOOL_AT + 2), 0, jumpTo(JUMP_LAYER_AT + 3), 0, 0, 549, jumpTo(JUMP_TOOL_AT), 0, jumpTo(JUMP_LAYER_AT + 1), 0});
        //(the layer key, as real key events (570..575: Shift, Up, Down, down and up): held, Up twice, Up at
        //the top (no wrapping), Down: 2 to 3; let go, no "next" (it stepped); tapped twice: 4, then 1 round
        //(576 prints it; 577: a tick with no scripted byte, the keys read as a person's))
        add({0, 570, 577, 577, 572, 577, 573, 577, 572, 577, 573, 577, 572, 577, 573, 577, 574, 577, 575, 577, 571, 577, 577, 576});
        add({570, 577, 577, 571, 577, 577, 576, 570, 577, 577, 571, 577, 577, 576});
        add({550, 906, 906, 906, 906, 906, 906, 551});
        //(undo, again and again, past everything built: the map as it began, 518 checks)
        add({0, 0, 533});
        add({B_UNDO, 0}, 250);
        add({518, 256 + 31});
        //(redo, again and again: the map as it was built (534); undone all the way again (518))
        add({commandByte(CMD_REDO), 0}, 250);
        add({534});
        add({B_UNDO, 0}, 250);
        add({518});
    }
    if (f > 70 && (step < steps.size() || hold > 0)) {
        if (hold > 0) {
            hold--;
            if (building(0))
                s_scriptByte[0] = held;
            if (hold == 0 && step == steps.size()) {
                pressTab();    //(the switch: playing)
                doneAt = f;
            }
            return false;
        }
        int byte = steps[step++];
        if (byte >= 600 && byte < 1000) {
            //(the mouse: as its events would be, in the screen's pixels; the builder's byte not the script's)
            static int mx = 0, my = 0;
            auto send = [&](SDL_EventType type, Uint8 button) {
                SDL_Event e {};
                e.type = type;
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    e.motion.x = static_cast<float>(mx);
                    e.motion.y = static_cast<float>(my);
                    e.motion.xrel = 4;
                } else {
                    e.button.x = static_cast<float>(mx);
                    e.button.y = static_cast<float>(my);
                    e.button.button = button;
                    e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                }
                matchedit::mouseEvent(e);
                prompts::setLastDevice(prompts::Device::Keyboard);    //(the keys' and the mouse's prompts)
            };
            auto clickHit = [&](bool tab, int k) {
                int n = 0;
                const bool seen = std::any_of(s_paletteHits.begin(), s_paletteHits.end(), [&](const PaletteHit& h) { return h.tab == k && h.r.w > 12; });
                for (const PaletteHit& h : s_paletteHits) {
                    if (h.tab == TAB_WHEEL || (h.tab >= 0) != tab || (tab ? h.tab != k && (seen || h.r.x != 0) : n++ != k))
                        continue;
                    mx = h.r.x + h.r.w / 2;
                    my = h.r.y + h.r.h / 2;
                    send(SDL_EVENT_MOUSE_MOTION, 0);
                    send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
                    send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
                    break;
                }
            };
            if (byte >= 908 && byte < 908 + MAPLAYERS) {
                //(the mouse onto the toolbar's layer button: its rectangle, the next layer's)
                for (const SDL_Rect& r : s_layerRect) {
                    if (r.w > 0) {
                        mx = r.x + r.w / 2;
                        my = r.y + r.h / 2;
                    }
                }
                send(SDL_EVENT_MOUSE_MOTION, 0);
            } else if (byte == 907) {
                printf("layertest: layer %d\n", m->builders[0].layer + 1);
            } else if (byte < 900) {
                mx = ((byte - 600) % MAPWIDTH) * TILESIZE + TILESIZE / 2;
                my = ((byte - 600) / MAPWIDTH) * TILESIZE + TILESIZE / 2;
                send(SDL_EVENT_MOUSE_MOTION, 0);
            } else if (byte <= 903) {
                send(byte % 2 == 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP, byte < 902 ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT);
            } else if (byte == 904) {
                send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_MIDDLE);
                send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_MIDDLE);
            } else if (byte >= 950) {
                clickHit(false, byte - 950);
            } else if (byte >= 920) {
                clickHit(true, byte - 920);
            }
            byte = -1;
        } else if (byte >= 560 && byte <= 564) {
            //(the README's screenshots: building, the palette; the controls sheet opened, shot, shut)
            if (byte == 560)
                shot("s1_building");
            else if (byte == 561)
                shot("s2_palette");
            else if (byte == 562)
                s_helpOpen = true;
            else if (byte == 563)
                shot("s3_controls");
            else
                s_helpOpen = false;
            byte = -1;
        } else if (byte >= 530 && byte <= 532) {
            //(the demo's pause menu: paused; a row down; that row pressed)
            if (byte == 530)
                game_values.flags.pausegame = true;
            else if (byte == 532)
                s_menuRow = std::min(s_menuRow + 1, static_cast<int>(matchedit::RowQuit));
            else
                matchedit::pauseMenuPress(s_menuRow);
            byte = -1;
        } else if (byte == 526) {
            matchedit::setPanelRow(-1);
            byte = 0;
        } else if (byte == 525) {
            matchedit::setPanelRow(2);
            matchedit::hostPanelInput(false, false, false, true);
            byte = 0;
        } else if (byte == 528 || byte == 529) {
            const MapMakerBuilder& b = m->builders[0];
            if (byte == 528) {
                MapMakerBuilder here = b;
                here.cx = static_cast<short>(b.cx - 2);
                const bool still = pickBrush(*m, here) && sameRecent(recentOf(here), recentOf(b));
                printf("grabtest: picked up brush %d: %s, the map %s\n", b.brush, still ? "STILL there" : "gone from there",
                    builtMapHash(*m) == s_markHash ? "UNCHANGED" : "changed");
            } else {
                printf("grabtest: the map %s as before\n", builtMapHash(*m) == s_markHash ? "is" : "is NOT");
            }
            byte = 0;
        } else if (byte >= 519 && byte <= 524) {
            const MapMakerBuilder& b = m->builders[0];
            if (byte == 519 || byte == 521) {
                s_markRecent = b.recent[0];
                s_markHash = builtMapHash(*m);
            } else if (byte == 520) {
                printf("recenttest: %d recent, the brush %s as it was\n", b.recentCount, sameRecent(b.recent[0], s_markRecent) ? "is" : "is NOT");
            } else if (byte == 522) {
                printf("picktest: picked brush %d, painted again: the map %s\n", b.brush, builtMapHash(*m) == s_markHash ? "unchanged" : "CHANGED");
                s_markHash = builtMapHash(*m);
            } else if (byte == 523) {
                int cells = 0;
                for (int x = 0; x < MAPWIDTH; x++)
                    for (int y = 0; y < MAPHEIGHT; y++)
                        for (int l = 0; l < MAPLAYERS; l++)
                            cells += (g_map->tileAt(x, y, l)->iID != TILESETNONE ? 1 : 0) + (g_map->blockat(x, y)->iType >= 0 && l == 0 ? 1 : 0);
                int live = 0;
                for (int k = 0; k < m->platformCount; k++)
                    live += m->platforms[k].live ? 1 : 0;
                printf("cleartest: tiles %d, items %d, hazards %d, platforms %d\n", cells, m->itemCount, m->hazardCount, live);
            } else {
                printf("cleartest: undone, the map %s as before\n", builtMapHash(*m) == s_markHash ? "is" : "is NOT");
            }
            byte = 0;
        } else if (byte >= 535 && byte <= 546) {
            //(the computer's keys and mouse buttons: as their events would be)
            const MapMakerBuilder& b = m->builders[0];
            static short toolBefore = 0;
            static MapMakerRecent fourth {};
            auto key = [](SDL_Keycode k, SDL_Keymod mod) {
                for (const bool down : {true, false}) {
                    SDL_Event e {};
                    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                    e.key.key = k;
                    e.key.mod = mod;
                    e.key.down = down;
                    SDL_PushEvent(&e);
                }
            };
            auto button = [](Uint8 which) {
                for (const bool down : {true, false}) {
                    SDL_Event e {};
                    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button = which;
                    e.button.down = down;
                    e.button.x = static_cast<float>(s_mouse.x);
                    e.button.y = static_cast<float>(s_mouse.y);
                    SDL_PushEvent(&e);
                }
            };
            if (byte == 535) {
                s_markHash = builtMapHash(*m);
            } else if (byte == 536) {
                printf("pctest: undone and redone, the map %s as it was\n", builtMapHash(*m) == s_markHash ? "is" : "is NOT");
            } else if (byte == 546) {
                printf("pctest: undone, the map %s\n", builtMapHash(*m) != s_markHash ? "changed" : "NOT changed");
            } else if (byte == 537) {
                toolBefore = b.tool;
            } else if (byte == 538) {
                printf("pctest: T, the tool %d to %d\n", toolBefore, b.tool);
            } else if (byte == 539) {
                fourth = b.recent[3];
            } else if (byte == 545) {
                printf("pctest: 4, the brush %s the strip's fourth\n", sameRecent(b.recent[0], fourth) ? "is" : "is NOT");
            } else if (byte == 540) {
                key(SDLK_Y, SDL_KMOD_LCTRL);
            } else if (byte == 541) {
                key(SDLK_T, SDL_KMOD_NONE);
            } else if (byte == 542) {
                key(SDLK_4, SDL_KMOD_NONE);
            } else if (byte == 543 || byte == 544) {
                button(byte == 543 ? SDL_BUTTON_X1 : SDL_BUTTON_X2);
            }
            byte = byte == 541 || byte == 542 || byte == 540 || byte == 543 || byte == 544 ? -1 : 0;
        } else if (byte == 549) {
            const MapMakerBuilder& b = m->builders[0];
            printf("tooltest: the tool %d (fill: 2), the layer %d (4: 3)\n", b.tool, b.layer);
            byte = 0;
        } else if (byte == 550 || byte == 551) {
            //(the toolbar's rectangle tool clicked, as the mouse's events: if the toolbar's drawn)
            static bool clicked = false;
            const SDL_Rect r = s_toolRect[1];
            if (byte == 550) {
                clicked = r.w > 0;
                for (const SDL_EventType type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
                    if (!clicked)
                        break;
                    SDL_Event e {};
                    e.type = type;
                    if (type == SDL_EVENT_MOUSE_MOTION) {
                        e.motion.x = static_cast<float>(r.x + r.w / 2);
                        e.motion.y = static_cast<float>(r.y + r.h / 2);
                    } else {
                        e.button.x = static_cast<float>(r.x + r.w / 2);
                        e.button.y = static_cast<float>(r.y + r.h / 2);
                        e.button.button = SDL_BUTTON_LEFT;
                        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    }
                    SDL_PushEvent(&e);
                }
                byte = -1;
            } else {
                const MapMakerBuilder& b = m->builders[0];
                if (clicked)
                    printf("toolbartest: the rectangle clicked, the tool %d (1), the map %s\n", b.tool, builtMapHash(*m) == s_markHash ? "unchanged" : "CHANGED");
                else
                    printf("toolbartest: skipped (the toolbar wasn't drawn: --at-norender)\n");
                byte = 0;
            }
        } else if (byte == 548) {
            const MapMakerBuilder& b = m->builders[0];
            printf("jumptest: the mouse to (18, 1), 3 ticks on the cursor's at (%d, %d): %s\n", b.cx, b.cy,
                b.cx == 18 && b.cy == 1 ? "there" : "NOT there");
            byte = 0;
        } else if (byte == 533) {
            s_builtHash = builtMapHash(*m);
            byte = 0;
        } else if (byte == 534) {
            printf("redotest: redone all the way, the map %s as it was built (redo %zu bytes)\n",
                builtMapHash(*m) == s_builtHash ? "is" : "is NOT", m->redo.size());
            byte = 0;
        } else if (byte == 518) {
            printf("undotest: undone all the way, the map %s as it began (history %zu bytes)\n",
                builtMapHash(*m) == s_startHash ? "is" : "is NOT", m->history.size());
            byte = 0;
        } else if (byte == 517) {
            printf("keytest: paused %d, quitting %d, building %d\n", game_values.flags.pausegame ? 1 : 0, game_values.flags.exitinggame ? 1 : 0, building(0) ? 1 : 0);
            byte = 0;
        } else if (byte >= 570 && byte <= 577) {
            //(the layer key's test: 570/571 Left Shift down/up, 572/573 Up, 574/575 Down; 576 the layer;
            //577 nothing. No scripted byte: the builder's keys are read as a person's)
            if (byte == 577) {
            } else if (byte == 576) {
                printf("layerkeys: the layer %d\n", m->builders[0].layer + 1);
            } else {
                static const SDL_Keycode KEYS[3] = {SDLK_LSHIFT, SDLK_UP, SDLK_DOWN};
                SDL_Event key {};
                const bool down = (byte - 570) % 2 == 0;
                key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                key.key.key = KEYS[(byte - 570) / 2];
                key.key.down = down;
                SDL_PushEvent(&key);
            }
            byte = -1;
        } else if (byte >= 514) {
            //(514 Enter, 515 P, 516 Escape: pressed and let go)
            const SDL_Keycode keys[3] = {SDLK_RETURN, SDLK_P, SDLK_ESCAPE};
            for (const bool down : {true, false}) {
                SDL_Event key {};
                key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                key.key.key = keys[byte - 514];
                key.key.down = down;
                SDL_PushEvent(&key);
            }
            byte = 0;
        } else if (byte >= 512) {
            //(512: Backspace down, 513: up)
            SDL_Event key {};
            key.type = byte == 512 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            key.key.key = SDLK_BACKSPACE;
            key.key.down = byte == 512;
            SDL_PushEvent(&key);
            byte = 0;
        } else if (byte >= 256) {
            shot(("e" + std::to_string(byte - 256) + (byte == 257 ? "_building" : byte == 258 ? "_painted" : byte == 259 ? "_palette" : byte == 264 ? "_music_page" : byte == 265 ? "_contents_page" : byte == 266 ? "_warps" : byte == 267 ? "_warps_page" : byte == 268 ? "_hazards_page" : byte == 269 ? "_platforms_page" : byte == 270 ? "_hazards_platforms" : byte == 271 ? "_undo_once" : byte == 272 ? "_undone" : byte == 273 ? "_rect" : byte == 274 ? "_fill_erased" : byte == 275 ? "_rect_undone" : byte == 276 ? "_types" : byte == 277 ? "_spawns" : byte == 278 ? "_animated" : byte == 279 ? "_weather_page" : byte == 280 ? "_blocks_hidden_off" : byte == 281 ? "_paste" : byte == 282 ? "_paste_undone" : byte == 283 ? "_rectangle" : byte == 284 ? "_mouse_drawn" : byte == 285 ? "_mouse_palette" : byte == 286 ? "_mouse_item" : byte == 287 ? "_all_undone" : byte == 288 ? "_recent" : byte == 289 ? "_recent_next" : byte == 290 ? "_picked" : byte == 291 ? "_cleared" : byte == 292 ? "_clear_undone" : "_blocks")).c_str());
            byte = 0;
        }
        if (building(0) && byte >= 0)
            s_scriptByte[0] = byte;
        held = byte;
        hold = pace - 1;
        if (hold == 0 && step == steps.size()) {
            pressTab();    //(the switch: playing)
            doneAt = f;
        }
    }
    if (f == 60)
        shot("e0_playing");
    //(at a person's pace: from the building's end, a second's play, then the pause screen a while)
    const int slow = pace > 1 ? 6 : 1;
    const int end = doneAt < 0 ? -100000 : doneAt + (pace > 1 ? 60 : 20);
    if (f == end)
        shot("e5_played");
    //The pause screen: who may build; Save
    if (f == end + 10 * slow)
        game_values.flags.pausegame = true;
    if (f == end + 16 * slow)
        s_menuRow = matchedit::RowRights;
    if (f == end + 22 * slow)
        shot("e6_pause_rights");
    if (f == end + 26 * slow) {
        matchedit::pauseMenuPress(2);    //(Save)
    }
    if (pace > 1 && f == end + 60 * slow)
        game_values.flags.pausegame = false;    //(and on playing)
    if (f == end + 36 * slow) {
        shot("e7_saved");
        //(the test's map not left in the maps)
        if (!s_savedPath.empty())
            std::remove(s_savedPath.c_str());
    }
    if (f == end + 37 * slow)
        matchedit::pauseMenuPress(matchedit::RowClear);    //(Clear map, pressed once: "Press again")
    if (f == end + 39 * slow)
        shot("e7a_pause_clear_armed");
    if (f == end + 40 * slow)
        matchedit::pauseMenuPress(matchedit::RowControls);    //(the controls sheet, over the pause menu)
    if (f == end + 41 * slow)
        shot("e7c_controls");
    if (f == end + 42 * slow)
        s_helpOpen = false;
    if (f == end + 43 * slow)
        s_menuRow = matchedit::RowQuit;
    if (f == end + 44 * slow)
        shot("e7b_pause_quit");
    if (f == end + 48 * slow) {
        //(the mouse on the pause menu: Resume clicked)
        const SDL_Rect& row = s_pauseRowRect[matchedit::RowResume];
        for (const SDL_EventType type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event e {};
            e.type = type;
            e.button.x = e.motion.x = static_cast<float>(row.x + row.w / 2);
            e.button.y = e.motion.y = static_cast<float>(row.y + row.h / 2);
            if (type == SDL_EVENT_MOUSE_MOTION)
                e.motion.xrel = 4;
            else
                e.button.button = SDL_BUTTON_LEFT;
            matchedit::mouseEvent(e);
        }
    }
    if (f == end + 50 * slow)
        printf("mousetest: %s\n", s_pauseRowRect[matchedit::RowResume].w == 0 ? "skipped (the pause menu wasn't drawn: --at-norender)"
            : game_values.flags.pausegame ? "paused 1 after Resume clicked (FAILED)" : "paused 0 after Resume clicked");
    return false;    //(the match goes on: the others play while it builds)
}

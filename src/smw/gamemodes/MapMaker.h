#pragma once

#include "GameMode.h"
#include "map.h"
#include "SimArena.h"

#include <cstdint>

class MovingPlatform;

constexpr int MADE_PLATFORMS = 8;           // the moving platforms the Map Maker makes, at most
constexpr int PLATFORM_KINDS = 3;           // its platforms' paths: to and fro, on round the screen, a circle
constexpr int PLATFORM_SETTINGS = 7;        // a kind's settings at most (width, height, made of, way, distance, speed...)
constexpr int PLATFORM_MAX_W = 8, PLATFORM_MAX_H = 4;    // a platform's size, at most
constexpr int HAZARD_TYPES = 8;             // MapHazardType
constexpr int HAZARD_SETTINGS = 4;          // a hazard type's settings at most (length, speed, way...)
constexpr int MAP_MAKER_UNDO = 8;           // a tick's edits, staged (then packed into the history: GameplayEdit.cpp)
constexpr int MAP_MAKER_BULK = 6;           // a tick's rectangles, fills and pastes (a paste: a layer each, the blocks)
constexpr size_t MAP_MAKER_HISTORY_BYTES = 256u << 10;    // the history to undo: all of it, unless it gets this big
constexpr int CLIP_W = 10, CLIP_H = 8;      // the copy tool's: as much as it copies
constexpr int MAP_CELLS = MAPWIDTH * MAPHEIGHT;

// A builder's editor (its cursor, brush and palette): simulation state, worked by the input bytes of
// its slot while it builds (smw/GameplayEdit.cpp), so it rolls back with the rest of the match
struct MapMakerBuilder {
    short cx = MAPWIDTH / 2;
    short cy = MAPHEIGHT / 2;
    short holdFrames = 0;      // a direction held: the cursor moves on its own after a while
    uint8_t brush = 0;         // a tile, a block, a map item, a warp, a hazard, a platform (GameplayEdit.cpp's Brush)
    TilesetTile tile {0, 0, 0};
    short block = 0;
    bool blockOn = true;       // an on/off or switch block's: how it starts
    bool blockHidden = false;  // a ?, flip, bounce or note block: hidden (Y on the Blocks page)
    short modeObject = 0;      // the Mode objects page's: 0..7 a race goal, 8..11 a flag base
    short contents = -1;       // a ? block's: one powerup (its index); -1 random by the default weights;
                               // -2 the powerups of `mix`, evenly; -10 - k the item set k's weights
    uint32_t mix = 0;          // (a bit a powerup)
    short item = 0;
    short warpDir = 0;         // a warp's: its direction (0 up, 1 right, 2 down, 3 left), its connection (0..9)
    short warpConn = 0;
    short hazard = 0;          // a hazard's type, and each type's settings (GameplayEdit.cpp's tables)
    short hazardSet[HAZARD_TYPES][HAZARD_SETTINGS] {};
    short platformKind = 0;    // a platform's path, and each kind's settings
    short platformSet[PLATFORM_KINDS][PLATFORM_SETTINGS] {};
    bool adjusting = false;    // the palette: a setting being changed (A, then left/right, then A)
    short typeBrush = 1;       // the Types page's: 0 the tiles' own, 1..18 a TileType
    short spawnBrush = 0;      // the Spawns page's: 0 no player spawns, 1..4 a team's, 5 the items'
    short tool = 0;            // 0 pencil, 1 rectangle, 2 fill, 3 copy, 4 paste (the tiles; a rectangle, the blocks too)
    short clipW = 0, clipH = 0;    // what copy copied: its size, its 4 layers' tiles, its blocks
    TilesetTile clipTiles[MAPLAYERS][CLIP_W * CLIP_H] {};
    short clipBlocks[CLIP_W * CLIP_H] {};
    bool anchoring = false;    // a rectangle: its first corner set (A or B held), the cursor its other
    bool anchorErase = false;  // (B: the rectangle erases)
    short ax = 0, ay = 0;
    short stroke = 0;          // A or B pressed: a new stroke (an undo takes back a stroke's edits)
    short layer = 1;
    bool palette = false;
    short page = 0;
    short px = 0, py = 0;
    short scrollX = 0, scrollY = 0;
};

// A moving platform the Map Maker made: of the brush's tile, `w` wide, its left end at x, y (a circle's:
// its middle there). Its path: `kind` (0 to and fro, `dist` tiles the `way`; 1 on and on the `way`,
// round the screen; 2 a circle, rx by ry tiles, `spin` 1 the other way round); `way` one of eight
// directions (0 right, then clockwise), `speed` the level editor's. `live` is it in the match (a
// temporary platform, in the simulation's arena: it rolls back with the rest)
struct MapMakerPlatform {
    TilesetTile tile;
    short x, y, w;
    short kind, way, dist, speed, rx, ry, spin;
    short h;                   // its height in tiles; `copied`: of the map's tiles that were there (`cells`,
    short copied;              // a column after another), not `tile`
    TilesetTile cells[PLATFORM_MAX_W * PLATFORM_MAX_H];
    MovingPlatform* live;
};

// An edit to undo: a cell as it was before a builder's stroke changed it (all of it: its tiles, block,
// item, warp, hazard; the platform the stroke made or took away), or the background and music before
// a choice (x -1). Taken back by the builder (`slot`), a stroke at a time.
struct MapMakerEdit {
    short slot, stroke;
    short x, y;
    TilesetTile tiles[MAPLAYERS];
    MapBlock block;
    short tileType;            // its type (the tiles' or set apart)
    bool noSpawn[6];           // its no-spawn marks (NUMSPAWNAREATYPES)
    short itemType;            // -1: none
    Warp warp;
    bool hadHazard;
    MapHazard hazard;
    short platformKind;        // 0 none; 1 made there (undo: it goes); 2 taken away (undo: made again)
    MapMakerPlatform platform; // (its `live` not kept)
    short background, music;
    short eyecandy[3];
    short switches[4];
    short goalCount, goals[8][2], baseCount, bases[4][2];
};

// A rectangle or a fill to undo: the cells it changed (`changed`, a bit each) as they were, of one layer's
// tiles or (`blocks`) the blocks' types
struct MapMakerBulk {
    short slot, stroke;
    short layer;
    bool blocks;
    uint8_t changed[(MAP_CELLS + 7) / 8];
    TilesetTile tiles[MAP_CELLS];
    uint8_t types[MAP_CELLS];
    short blockType[MAP_CELLS];
};

// Map Maker: the match is a workshop. Everyone plays the map while the players the host lets build
// change it, live (smw/GameplayEdit.cpp): the host gives and takes the right to build. Kills count as
// in Frag Limit; it never ends by itself (no goal). Its state below is the match's (in every rollback
// snapshot, with the mode): the rights, the builders, the map's items as built.
class CGM_MapMaker : public CGameMode {
public:
    CGM_MapMaker();
    void init() override;

    short hostSlot = 0;                       // the host's player: it builds always, it gives the rights
    bool rights[4] {};
    MapMakerBuilder builders[4];
    MapItem items[MAXMAPITEMS] {};            // the map's items (g_map's list: when the map is saved)
    short itemCount = 0;
    MapHazard hazards[MAXMAPHAZARDS] {};      // the map's hazards (likewise)
    short hazardCount = 0;
    MapMakerPlatform platforms[MADE_PLATFORMS] {};    // the platforms made (the map's own stay as they are)
    short platformCount = 0;
    MapMakerEdit edits[MAP_MAKER_UNDO] {};    // this tick's edits, staged (the history has the ones before)
    short editCount = 0;
    MapMakerBulk bulks[MAP_MAKER_BULK] {};    // this tick's rectangles and fills, staged
    short bulkCount = 0;
    sim::vector<uint8_t> history;             // every edit since the match started, packed (only what it changed),
                                              // the oldest first: undone as far back as it goes
    bool edited = false;                      // the map differs from its file
    bool started = false;                     // the first tick's done (the builders started building)
    short background = -1;                    // the background chosen (its place in the backgrounds' list; -1: the map's)
    short music = -1;                         // the music category chosen (-1: the map's)
    short eyecandy[3] {};                     // the weather, a layer each (CMap::eyecandy's bits: the map's)
    short switches[4] {};                     // the on/off switches' states as the map starts (1 on)
    short goalCount = 0;                      // the race goals placed: 0 (random) or 8; their top lefts
    short goals[8][2] {};
    short baseCount = 0;                      // the flag bases likewise: 0 or 4
    short bases[4][2] {};
};

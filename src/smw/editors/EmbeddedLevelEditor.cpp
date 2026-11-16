// The level (map) editor, built into the game (EditorLauncher.h). Its sources are
// the standalone editor's (src/leveleditor), compiled here as one unit inside
// the namespace leveleditor:: so that its names don't clash with the game's or the
// other editor's. SMW_EMBEDDED_EDITOR leaves out what the game has itself (the
// window, the screen surfaces, the stand-ins for gameplay code).
#define SMW_EMBEDDED_EDITOR
#include "CoEdit.h"
#include "LevelShell.h"
#include "../../leveleditor/LevelEditorState.h"

namespace leveleditor {
#include "../../leveleditor/EditorAnimation.cpp"
#include "../../leveleditor/EditorBackgrounds.cpp"
#include "../../leveleditor/EditorBlocks.cpp"
#include "../../leveleditor/EditorDialogs.cpp"
#include "../../leveleditor/EditorDraw.cpp"
#include "../../leveleditor/EditorFile.cpp"
#include "../../leveleditor/EditorGlobals.cpp"
#include "../../leveleditor/EditorMapEdit.cpp"
#include "../../leveleditor/EditorMapHazards.cpp"
#include "../../leveleditor/EditorModeItems.cpp"
#include "../../leveleditor/EditorPlatforms.cpp"
#include "../../leveleditor/EditorTiles.cpp"
#include "../../leveleditor/EditorWarpEyecandy.cpp"
#include "../../leveleditor/main.cpp"

// ---- editing together (CoEdit.h): the map as keyed values

using editors::coedit::Bytes;
using editors::coedit::Reader;
using editors::coedit::Writer;

namespace {
enum Section : uint32_t { SECTION_MISC = 1, SECTION_ITEMS, SECTION_HAZARDS, SECTION_PLATFORMS };

// A tile by its tileset's name: the tilesets' order differs between games
void writeTile(Writer& w, const TilesetTile& tile)
{
    if (tile.iID >= 0 && static_cast<size_t>(tile.iID) < g_tilesetmanager->count())
        w.str(g_tilesetmanager->tileset(tile.iID)->name());
    else
        w.str("#" + std::to_string(tile.iID));    //(none, animated, unknown)
    w.i16(tile.iCol);
    w.i16(tile.iRow);
}

void readTile(Reader& r, TilesetTile& tile)
{
    const std::string name = r.str();
    if (!name.empty() && name[0] == '#')
        tile.iID = static_cast<short>(std::atoi(name.c_str() + 1));
    else
        tile.iID = static_cast<short>(g_tilesetmanager->indexFromName(name));
    tile.iCol = r.i16();
    tile.iRow = r.i16();
}
} // namespace

class CoeditDocument : public editors::coedit::Document {
public:
    void capture(std::unordered_map<uint32_t, Bytes>& values) override
    {
        for (short x = 0; x < MAPWIDTH; x++) {
            for (short y = 0; y < MAPHEIGHT; y++) {
                Writer w;
                for (short layer = 0; layer < MAPLAYERS; layer++)
                    writeTile(w, g_map->mapdata[x][y][layer]);
                w.i32(static_cast<int32_t>(g_map->mapdatatop[x][y]));
                const MapBlock& block = g_map->objectdata[x][y];
                w.i16(block.iType);
                for (short setting : block.iSettings)
                    w.i16(setting);
                w.u8(block.fHidden ? 1 : 0);
                const Warp& warp = g_map->warpdata[x][y];
                w.i32(static_cast<int32_t>(warp.direction));
                w.i16(warp.connection);
                w.i16(warp.id);
                for (short type = 0; type < NUMSPAWNAREATYPES; type++)
                    w.u8(g_map->nospawn[type][x][y] ? 1 : 0);
                values[editors::coedit::cellKey(x, y)] = std::move(w.out);
            }
        }

        Writer misc;
        for (short e : g_map->eyecandy)
            misc.i16(e);
        misc.str(g_map->szBackgroundFile);
        misc.i16(g_map->musicCategoryID);
        misc.i16(g_map->iNumRaceGoals);
        for (const Point& p : g_map->racegoallocations) {
            misc.i16(static_cast<int16_t>(p.x));
            misc.i16(static_cast<int16_t>(p.y));
        }
        misc.i16(g_map->iNumFlagBases);
        for (const Point& p : g_map->flagbaselocations) {
            misc.i16(static_cast<int16_t>(p.x));
            misc.i16(static_cast<int16_t>(p.y));
        }
        for (short sw : g_map->iSwitches)
            misc.i16(sw);
        values[SECTION_MISC] = std::move(misc.out);

        Writer items;
        items.u32(static_cast<uint32_t>(g_map->mapitems.size()));
        for (const MapItem& item : g_map->mapitems) {
            items.i32(static_cast<int32_t>(item.itype));
            items.i16(item.ix);
            items.i16(item.iy);
        }
        values[SECTION_ITEMS] = std::move(items.out);

        Writer hazards;
        hazards.u32(static_cast<uint32_t>(g_map->maphazards.size()));
        for (const MapHazard& hazard : g_map->maphazards) {
            hazards.i32(static_cast<int32_t>(hazard.itype));
            hazards.i16(hazard.ix);
            hazards.i16(hazard.iy);
            for (short p : hazard.iparam)
                hazards.i16(p);
            for (float p : hazard.dparam)
                hazards.f32(p);
        }
        values[SECTION_HAZARDS] = std::move(hazards.out);

        Writer platforms;
        platforms.i16(g_iNumPlatforms);
        for (short k = 0; k < g_iNumPlatforms && k < MAX_PLATFORMS; k++) {
            const MapPlatform& p = g_Platforms[k];
            //(the tiles that are set, and every tile type)
            uint32_t used = 0;
            for (const TilesetTile& tile : p.tiles)
                used += tile.iID != TILESETNONE;
            platforms.u32(used);
            for (size_t t = 0; t < p.tiles.size(); t++) {
                if (p.tiles[t].iID == TILESETNONE)
                    continue;
                platforms.u16(static_cast<uint16_t>(t));
                writeTile(platforms, p.tiles[t]);
            }
            for (TileType type : p.types)
                platforms.u8(static_cast<uint8_t>(type));
            platforms.i16(p.iVelocity);
            platforms.i16(p.iStartX);
            platforms.i16(p.iStartY);
            platforms.i16(p.iEndX);
            platforms.i16(p.iEndY);
            platforms.i32(static_cast<int32_t>(p.iPathType));
            platforms.f32(p.fAngle);
            platforms.f32(p.fRadiusX);
            platforms.f32(p.fRadiusY);
            platforms.i16(p.iDrawLayer);
        }
        values[SECTION_PLATFORMS] = std::move(platforms.out);
    }

    void apply(uint32_t key, const Bytes& value) override
    {
        if (value.empty())
            return;
        Reader r(value);
        if (key & editors::coedit::CELL) {
            const int x = editors::coedit::cellX(key);
            const int y = editors::coedit::cellY(key);
            if (x >= MAPWIDTH || y >= MAPHEIGHT)
                return;
            TilesetTile tiles[MAPLAYERS];
            for (TilesetTile& tile : tiles)
                readTile(r, tile);
            const TileType top = static_cast<TileType>(r.i32());
            MapBlock block {};
            block.iType = r.i16();
            for (short& setting : block.iSettings)
                setting = r.i16();
            block.fHidden = r.u8() != 0;
            Warp warp {};
            warp.direction = static_cast<WarpEnterDirection>(r.i32());
            warp.connection = r.i16();
            warp.id = r.i16();
            bool nospawn[NUMSPAWNAREATYPES];
            for (bool& n : nospawn)
                n = r.u8() != 0;
            if (!r.ok)
                return;
            for (short layer = 0; layer < MAPLAYERS; layer++)
                g_map->mapdata[x][y][layer] = tiles[layer];
            g_map->mapdatatop[x][y] = top;
            g_map->objectdata[x][y] = block;
            g_map->warpdata[x][y] = warp;
            for (short type = 0; type < NUMSPAWNAREATYPES; type++)
                g_map->nospawn[type][x][y] = nospawn[type];
            return;
        }

        switch (key) {
            case SECTION_MISC: {
                short eyecandy[3];
                for (short& e : eyecandy)
                    e = r.i16();
                const std::string background = r.str();
                const short music = r.i16();
                const short goals = r.i16();
                Point goal[MAXRACEGOALS];
                for (Point& p : goal) {
                    p.x = r.i16();
                    p.y = r.i16();
                }
                const short bases = r.i16();
                Point base[4];
                for (Point& p : base) {
                    p.x = r.i16();
                    p.y = r.i16();
                }
                std::array<short, 4> switches {};
                for (short& sw : switches)
                    sw = r.i16();
                if (!r.ok)
                    return;
                for (int k = 0; k < 3; k++)
                    g_map->eyecandy[k] = eyecandy[k];
                g_map->musicCategoryID = music;
                g_map->iNumRaceGoals = goals;
                std::copy(std::begin(goal), std::end(goal), std::begin(g_map->racegoallocations));
                g_map->iNumFlagBases = bases;
                std::copy(std::begin(base), std::end(base), std::begin(g_map->flagbaselocations));
                g_map->iSwitches = switches;
                if (background != g_map->szBackgroundFile) {
                    g_map->szBackgroundFile = background;
                    //(as loadcurrentmap() does)
                    std::string filename = "gfx/packs/Classic/backgrounds/" + background;
                    std::string path = convertPath(filename);
                    backgroundlist->setCurrentPath(filename);
                    if (!FileExists(path)) {
                        path = convertPath("gfx/packs/Classic/backgrounds/Land_Classic.png");
                        backgroundlist->setCurrentPath("gfx/packs/Classic/backgrounds/Land_Classic.png");
                    }
                    rm->spr_background = ImageLoader(path).withoutColorKey().create();
                }
                break;
            }
            case SECTION_ITEMS: {
                std::vector<MapItem> items(std::min<uint32_t>(r.u32(), 4096));
                for (MapItem& item : items) {
                    item.itype = static_cast<MapItemType>(r.i32());
                    item.ix = r.i16();
                    item.iy = r.i16();
                }
                if (r.ok)
                    g_map->mapitems = std::move(items);
                break;
            }
            case SECTION_HAZARDS: {
                std::vector<MapHazard> hazards(std::min<uint32_t>(r.u32(), 4096));
                for (MapHazard& hazard : hazards) {
                    hazard.itype = static_cast<MapHazardType>(r.i32());
                    hazard.ix = r.i16();
                    hazard.iy = r.i16();
                    for (short& p : hazard.iparam)
                        p = r.i16();
                    for (float& p : hazard.dparam)
                        p = r.f32();
                }
                if (r.ok)
                    g_map->maphazards = std::move(hazards);
                break;
            }
            case SECTION_PLATFORMS: {
                const short count = std::clamp<short>(r.i16(), 0, MAX_PLATFORMS);
                for (short k = 0; k < count && r.ok; k++) {
                    MapPlatform& p = g_Platforms[k];
                    for (TilesetTile& tile : p.tiles)
                        ClearTilesetTile(&tile);
                    const uint32_t used = r.u32();
                    for (uint32_t t = 0; t < used && r.ok; t++) {
                        const uint16_t index = r.u16();
                        TilesetTile tile {};
                        readTile(r, tile);
                        if (index < p.tiles.size())
                            p.tiles[index] = tile;
                    }
                    for (TileType& type : p.types)
                        type = static_cast<TileType>(r.u8());
                    p.iVelocity = r.i16();
                    p.iStartX = r.i16();
                    p.iStartY = r.i16();
                    p.iEndX = r.i16();
                    p.iEndY = r.i16();
                    p.iPathType = static_cast<PlatformPathType>(r.i32());
                    p.fAngle = r.f32();
                    p.fRadiusX = r.f32();
                    p.fRadiusY = r.f32();
                    p.iDrawLayer = r.i16();
                    p.UpdatePreview();
                }
                if (r.ok)
                    g_iNumPlatforms = count;
                break;
            }
            default:
                break;
        }
    }

    std::string title() override
    {
        return "map " + maplist->currentShortmapname();
    }
};

std::unique_ptr<editors::coedit::Document> makeCoeditDocument()
{
    return std::make_unique<CoeditDocument>();
}

// ---- the editor window's view of the editor (LevelShell.h)

namespace shell {

bool inMapView()
{
    return state == EDITOR_EDIT;
}

int mode()
{
    return edit_mode;
}

void setMode(int mode)
{
    edit_mode = mode;
}

int block()
{
    return set_block;
}

void setBlock(int block, bool on)
{
    set_block = block;
    if (block >= 7 && block <= 10)
        g_map->iSwitches[block - 7] = on ? 1 : 0;
    else if (block >= 11 && block <= 14)
        set_block_switch_on = on;
    edit_mode = 0;
}

bool switchOn(int color)
{
    return color >= 0 && color < 4 && g_map->iSwitches[color] != 0;
}

bool switchBlockOn()
{
    return set_block_switch_on;
}

int item()
{
    return set_mapitem;
}

void setItem(int item)
{
    set_mapitem = item;
    edit_mode = 7;
}

int tileType()
{
    return static_cast<int>(set_tiletype);
}

void setTileType(int type)
{
    set_tiletype = static_cast<TileType>(type);
    edit_mode = 6;
}

int layer()
{
    return selected_layer;
}

void setLayer(int layer)
{
    selected_layer = std::clamp(layer, 0, MAPLAYERS - 1);
}

bool othersHidden()
{
    return view_only_layer;
}

void setOthersHidden(bool hidden)
{
    view_only_layer = hidden;
}

bool blocksShown()
{
    return viewblocks;
}

void setBlocksShown(bool shown)
{
    viewblocks = shown;
}

std::string mapName()
{
    return maplist ? maplist->currentShortmapname() : std::string();
}

bool drawBrushTiles(const Brush& b, int x, int y, int size)
{
    if (b.mode != 1 || b.id < 0 || static_cast<size_t>(b.id) >= g_tilesetmanager->count() || b.cols < 1 || b.rows < 1)
        return false;
    //(the top left of the selection, at most 3x3 tiles, 16 pixels each: shrunk to fit)
    const int cols = std::min(b.cols, 3);
    const int rows = std::min(b.rows, 3);
    const int cell = std::max(4, size / std::max(cols, rows) / 8 * 8);
    const DrawSize drawSize = cell >= 32 ? DrawSize::Ingame : cell >= 16 ? DrawSize::Preview : DrawSize::Thumbnail;
    const int px = drawSize == DrawSize::Ingame ? 32 : drawSize == DrawSize::Preview ? 16 : 8;
    const int ox = x + (size - cols * px) / 2;
    const int oy = y + (size - rows * px) / 2;
    for (int c = 0; c < cols; c++) {
        for (int r = 0; r < rows; r++) {
            const SDL_Rect dst {ox + c * px, oy + r * px, px, px};
            g_tilesetmanager->tileset(b.id)->draw(drawSize,
                CTilesetManager::rect(drawSize, static_cast<size_t>(b.x + c), static_cast<size_t>(b.y + r)), screen, dst);
        }
    }
    return true;
}

bool drawBrushTile(const Brush& b, int dc, int dr, int x, int y)
{
    if (b.mode != 1 || b.id < 0 || static_cast<size_t>(b.id) >= g_tilesetmanager->count() || b.cols < 1 || b.rows < 1)
        return false;
    //(the selection's pattern, repeated: as a rectangle paints it)
    const int c = ((dc % b.cols) + b.cols) % b.cols;
    const int r = ((dr % b.rows) + b.rows) % b.rows;
    const SDL_Rect dst {x, y, 32, 32};
    g_tilesetmanager->tileset(b.id)->draw(DrawSize::Ingame,
        CTilesetManager::rect(DrawSize::Ingame, static_cast<size_t>(b.x + c), static_cast<size_t>(b.y + r)), screen, dst);
    return true;
}

bool drawTiles(int x, int y, int size)
{
    Brush b;
    b.mode = 1;
    b.id = set_tile_tileset;
    b.x = set_tile_start_x;
    b.y = set_tile_start_y;
    b.cols = set_tile_cols;
    b.rows = set_tile_rows;
    return drawBrushTiles(b, x, y, size);
}

// ---- many cells at once

bool paintsCells()
{
    return edit_mode == 0 || edit_mode == 1 || edit_mode == 2 || edit_mode == 4 || edit_mode == 5 || edit_mode == 6 || edit_mode == 8;
}

namespace {
// The clipboard: a rectangle of whole cells
struct Copied {
    TilesetTile tiles[MAPLAYERS];
    TileType top;
    MapBlock block;
    Warp warp;
    bool nospawn[NUMSPAWNAREATYPES];
};
std::vector<Copied> g_copied;
int g_copiedCols = 0;
int g_copiedRows = 0;
} // namespace

// (the map's cells: a friend of CMap)
class Cells {
public:
// One cell painted (or cleared), as a click there would; (ox, oy): where it is in the
// selected tiles' pattern
static void paint(int x, int y, bool erase, int ox, int oy)
{
    if (x < 0 || x >= MAPWIDTH || y < 0 || y >= MAPHEIGHT)
        return;
    const short cx = static_cast<short>(x);
    const short cy = static_cast<short>(y);
    switch (edit_mode) {
        case 0:
            if (erase) {
                g_map->objectdata[x][y].iType = -1;
                break;
            }
            g_map->objectdata[x][y].iType = set_block;
            g_map->objectdata[x][y].fHidden = false;
            if (set_block == 1 || set_block == 15) {
                for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
                    g_map->objectdata[x][y].iSettings[iSetting] = defaultPowerupSetting(0, iSetting);
            } else if (set_block >= 11 && set_block <= 14) {
                g_map->objectdata[x][y].iSettings[0] = set_block_switch_on;
            }
            AdjustMapItems(cx, cy);
            break;
        case 1:
        case 8: {
            const bool fNeedUpdate = !TileTypeIsModified(cx, cy);
            if (erase)
                g_map->mapdata[x][y][selected_layer].iID = TILESETNONE;
            else if (edit_mode == 1)
                SetTilesetTile(&g_map->mapdata[x][y][selected_layer], set_tile_tileset, set_tile_start_x + ox % std::max(1, set_tile_cols),
                    set_tile_start_y + oy % std::max(1, set_tile_rows));
            else
                SetTilesetTile(&g_map->mapdata[x][y][selected_layer], TILESETANIMATED, set_tile_start_y + oy % std::max(1, set_tile_rows),
                    set_tile_start_x + ox % std::max(1, set_tile_cols));
            if (fNeedUpdate)
                UpdateTileType(cx, cy);
            AdjustMapItems(cx, cy);
            break;
        }
        case 2:
            g_map->warpdata[x][y].direction = erase ? WARP_UNDEFINED : static_cast<WarpEnterDirection>(set_direction);
            g_map->warpdata[x][y].connection = erase ? -1 : set_connection;
            break;
        case 4:
            SetNoSpawn(static_cast<short>(nospawn_mode), cx, cy, !erase);
            break;
        case 5:
            g_map->nospawn[5][x][y] = !erase;
            break;
        case 6:
            g_map->mapdatatop[x][y] = erase ? TileType::NonSolid : set_tiletype;
            AdjustMapItems(cx, cy);
            break;
        default:
            break;
    }
}

// What the mode paints at a cell, to compare cells for the fill
static std::string value(int x, int y)
{
    std::string v;
    auto add = [&v](int n) { v += std::to_string(n) + ","; };
    switch (edit_mode) {
        case 0: add(g_map->objectdata[x][y].iType); break;
        case 1:
        case 8: {
            const TilesetTile& t = g_map->mapdata[x][y][selected_layer];
            add(t.iID);
            if (t.iID != TILESETNONE) {
                add(t.iCol);
                add(t.iRow);
            }
            break;
        }
        case 2: add(static_cast<int>(g_map->warpdata[x][y].direction)); add(g_map->warpdata[x][y].connection); break;
        case 4: add(g_map->nospawn[nospawn_mode][x][y]); break;
        case 5: add(g_map->nospawn[5][x][y]); break;
        case 6: add(static_cast<int>(g_map->mapdatatop[x][y])); break;
        default: break;
    }
    return v;
}

static void copy(int x0, int y0, int x1, int y1)
{
    g_copied.clear();
    g_copiedCols = x1 - x0 + 1;
    g_copiedRows = y1 - y0 + 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            Copied c;
            for (short layer = 0; layer < MAPLAYERS; layer++)
                c.tiles[layer] = g_map->mapdata[x][y][layer];
            c.top = g_map->mapdatatop[x][y];
            c.block = g_map->objectdata[x][y];
            c.warp = g_map->warpdata[x][y];
            for (short type = 0; type < NUMSPAWNAREATYPES; type++)
                c.nospawn[type] = g_map->nospawn[type][x][y];
            g_copied.push_back(c);
        }
    }
}

static void paste(int col, int row)
{
    for (int dy = 0; dy < g_copiedRows; dy++) {
        for (int dx = 0; dx < g_copiedCols; dx++) {
            const int x = col + dx;
            const int y = row + dy;
            if (x < 0 || x >= MAPWIDTH || y < 0 || y >= MAPHEIGHT)
                continue;
            const Copied& c = g_copied[static_cast<size_t>(dy * g_copiedCols + dx)];
            for (short layer = 0; layer < MAPLAYERS; layer++)
                g_map->mapdata[x][y][layer] = c.tiles[layer];
            g_map->mapdatatop[x][y] = c.top;
            g_map->objectdata[x][y] = c.block;
            g_map->warpdata[x][y] = c.warp;
            for (short type = 0; type < NUMSPAWNAREATYPES; type++)
                g_map->nospawn[type][x][y] = c.nospawn[type];
            AdjustMapItems(static_cast<short>(x), static_cast<short>(y));
        }
    }
}
};

void fillRect(int c0, int r0, int c1, int r1, bool erase)
{
    if (!paintsCells())
        return;
    const int x0 = std::clamp(std::min(c0, c1), 0, MAPWIDTH - 1);
    const int x1 = std::clamp(std::max(c0, c1), 0, MAPWIDTH - 1);
    const int y0 = std::clamp(std::min(r0, r1), 0, MAPHEIGHT - 1);
    const int y1 = std::clamp(std::max(r0, r1), 0, MAPHEIGHT - 1);
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++)
            Cells::paint(x, y, erase, x - x0, y - y0);
    }
}

void floodFill(int col, int row)
{
    if (!paintsCells() || col < 0 || col >= MAPWIDTH || row < 0 || row >= MAPHEIGHT)
        return;
    const std::string from = Cells::value(col, row);
    std::vector<std::pair<int, int>> cells;
    std::vector<bool> seen(static_cast<size_t>(MAPWIDTH * MAPHEIGHT), false);
    std::vector<std::pair<int, int>> todo {{col, row}};
    seen[static_cast<size_t>(row * MAPWIDTH + col)] = true;
    while (!todo.empty()) {
        const std::pair<int, int> at = todo.back();
        todo.pop_back();
        cells.push_back(at);
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
            const int x = at.first + d[0];
            const int y = at.second + d[1];
            if (x < 0 || x >= MAPWIDTH || y < 0 || y >= MAPHEIGHT || seen[static_cast<size_t>(y * MAPWIDTH + x)])
                continue;
            seen[static_cast<size_t>(y * MAPWIDTH + x)] = true;
            if (Cells::value(x, y) == from)
                todo.push_back({x, y});
        }
    }
    //(painted after: painting changes what the cells are; the selected tiles' pattern from the cell filled from)
    const int cols = std::max(1, set_tile_cols);
    const int rows = std::max(1, set_tile_rows);
    for (const std::pair<int, int>& c : cells)
        Cells::paint(c.first, c.second, false, ((c.first - col) % cols + cols) % cols, ((c.second - row) % rows + rows) % rows);
}

void copyArea(int c0, int r0, int c1, int r1)
{
    Cells::copy(std::clamp(std::min(c0, c1), 0, MAPWIDTH - 1), std::clamp(std::min(r0, r1), 0, MAPHEIGHT - 1),
        std::clamp(std::max(c0, c1), 0, MAPWIDTH - 1), std::clamp(std::max(r0, r1), 0, MAPHEIGHT - 1));
}

bool clipboard(int* cols, int* rows)
{
    if (cols)
        *cols = g_copiedCols;
    if (rows)
        *rows = g_copiedRows;
    return !g_copied.empty();
}

void paste(int col, int row)
{
    Cells::paste(col, row);
}

// ---- the recently used

Brush brush()
{
    Brush b;
    switch (edit_mode) {
        case 0:
            b.mode = 0;
            b.id = set_block;
            b.on = set_block >= 11 && set_block <= 14 ? set_block_switch_on : set_block >= 7 && set_block <= 10 ? switchOn(set_block - 7) : true;
            break;
        case 1:
            if (set_tile_tileset < 0 || set_tile_cols < 1 || set_tile_rows < 1)
                break;
            b.mode = 1;
            b.id = set_tile_tileset;
            b.x = set_tile_start_x;
            b.y = set_tile_start_y;
            b.cols = set_tile_cols;
            b.rows = set_tile_rows;
            break;
        case 6:
            b.mode = 6;
            b.id = static_cast<int>(set_tiletype);
            break;
        case 7:
            b.mode = 7;
            b.id = set_mapitem;
            break;
        default:
            break;
    }
    return b;
}

void setBrush(const Brush& b)
{
    switch (b.mode) {
        case 0: setBlock(b.id, b.on); break;
        case 1:
            if (b.id < 0 || static_cast<size_t>(b.id) >= g_tilesetmanager->count())
                return;
            set_tile_tileset = b.id;
            set_tile_start_x = b.x;
            set_tile_start_y = b.y;
            set_tile_end_x = b.x + b.cols - 1;
            set_tile_end_y = b.y + b.rows - 1;
            set_tile_cols = b.cols;
            set_tile_rows = b.rows;
            edit_mode = 1;
            break;
        case 6: setTileType(b.id); break;
        case 7: setItem(b.id); break;
        default: break;
    }
}

// ---- the tileset screen

bool tilePicker(TilePicker* picker)
{
    if (state != EDITOR_TILES || set_tile_tileset < 0 || static_cast<size_t>(set_tile_tileset) >= g_tilesetmanager->count())
        return false;
    CTileset* ts = g_tilesetmanager->tileset(set_tile_tileset);
    picker->viewCol = view_tileset_x;
    picker->viewRow = view_tileset_y;
    picker->cols = ts->width();
    picker->rows = ts->height();
    picker->index = set_tile_tileset;
    picker->count = static_cast<int>(g_tilesetmanager->count());
    picker->name = ts->name();
    return true;
}

int hazardType(int index)
{
    return index >= 0 && static_cast<size_t>(index) < g_map->maphazards.size() ? static_cast<int>(g_map->maphazards[static_cast<size_t>(index)].itype) : 0;
}

Screen currentScreen()
{
    Screen sc;
    const Command done {"Done", SDLK_ESCAPE};
    switch (state) {
        case EDITOR_TILES:
            sc.title = "Tileset " + std::to_string(set_tile_tileset + 1) + " of " + std::to_string(g_tilesetmanager->count()) + ": " +
                       g_tilesetmanager->tileset(set_tile_tileset)->name();
            sc.a = "take (held, with the d-pad: several)";
            sc.shoulders = "tileset";
            sc.lb = SDLK_PAGEUP;
            sc.rb = SDLK_PAGEDOWN;
            sc.commands = {{"Previous tileset", SDLK_PAGEUP}, {"Next tileset", SDLK_PAGEDOWN}, done};
            break;
        case EDITOR_TILETYPE:
            sc.title = "Tile types";
            sc.a = "take";
            sc.commands = {done};
            break;
        case EDITOR_ANIMATION:
            sc.title = "Animated tiles";
            sc.a = "take (drag: several)";
            sc.commands = {done};
            break;
        case EDITOR_WARP:
            sc.title = "Warps: pick a color and a direction";
            sc.a = "take";
            sc.commands = {done};
            break;
        case EDITOR_EYECANDY:
            sc.title = "Eyecandy, on each layer";
            sc.a = "switch on / off";
            sc.commands = {{"Done", SDLK_E}};
            break;
        case EDITOR_BACKGROUNDS:
            sc.title = "Backgrounds";
            sc.a = "take, with its music";
            sc.b = "the picture only";
            sc.shoulders = "page";
            sc.lb = SDLK_PAGEUP;
            sc.rb = SDLK_PAGEDOWN;
            sc.commands = {{"Previous page", SDLK_PAGEUP}, {"Next page", SDLK_PAGEDOWN}, done};
            break;
        case EDITOR_BLOCKS:
            sc.title = "Blocks";
            sc.a = "take";
            sc.commands = {done};
            break;
        case EDITOR_MAPITEMS:
            sc.title = "Map items";
            sc.a = "take";
            sc.commands = {done};
            break;
        case EDITOR_PROPERTIES:
            sc.title = "What the ? blocks give";
            sc.a = "more";
            sc.b = "less";
            sc.holds = "LT: every block";
            sc.commands = {{"Default", SDLK_D}, {"Every block: default", SDLK_D, true}, {"Random", SDLK_GRAVE}, {"Nothing", SDLK_0}, done};
            break;
        case EDITOR_MODEITEMS:
            sc.title = modeitemmode == 0 ? "Race goals" : "Flag bases";
            sc.a = "move (drag)";
            sc.shoulders = "goals / bases";
            sc.lb = SDLK_1;
            sc.rb = SDLK_2;
            sc.commands = {{"Race goals", SDLK_1}, {"Flag bases", SDLK_2}, {"Let the game place them", SDLK_R}, {"Save the map", SDLK_S}, done};
            break;
        case EDITOR_PLATFORM:
            switch (iPlatformEditState) {
                case PLATFORM_EDIT_STATE_SELECT:
                    sc.title = "Platforms";
                    sc.a = "edit one";
                    sc.commands = {{"New platform", SDLK_N}, {"Try the paths", SDLK_C}, {"Save the map", SDLK_S}, done};
                    break;
                case PLATFORM_EDIT_STATE_PATH_TYPE:
                case PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE:
                    sc.title = "Platform: its path's shape";
                    sc.a = "choose";
                    sc.commands = {done};
                    break;
                case PLATFORM_EDIT_STATE_PATH:
                    sc.title = "Platform " + std::to_string(iEditPlatform + 1) + ": its path (" +
                               szPathNames[static_cast<int>(g_Platforms[iEditPlatform].iPathType)] + ")";
                    if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                        sc.a = "center";
                        sc.b = "start angle";
                        sc.xHold = SDLK_X;
                        sc.yHold = SDLK_Z;
                        sc.rbHold = SDLK_C;
                        sc.holds = "hold X / Y / RB with A: width, height, circle";
                    } else {
                        sc.a = "start point";
                        sc.b = g_Platforms[iEditPlatform].iPathType == PlatformPathType::Straight ? "end point" : "angle";
                        sc.holds = "LT: snap";
                    }
                    sc.commands = {{"Path shape", SDLK_T}, done};
                    break;
                case PLATFORM_EDIT_STATE_TEST:
                    sc.title = "Platforms: trying the paths";
                    sc.commands = {done};
                    break;
                default:
                    sc.title = "Platform " + std::to_string(iEditPlatform + 1);
                    sc.a = "paint";
                    sc.b = "erase";
                    sc.shoulders = "slower / faster";
                    sc.lb = SDLK_MINUS;
                    sc.rb = SDLK_EQUALS;
                    sc.commands = {{"Its tiles", SDLK_T}, {"Animated tiles", SDLK_A}, {"Tile types", SDLK_L}, {"Its path", SDLK_P},
                        {"Draw layer", SDLK_Y}, {"Slower", SDLK_MINUS}, {"Faster", SDLK_EQUALS}, {"Delete it", SDLK_DELETE}, done};
                    break;
            }
            break;
        case EDITOR_MAPHAZARDS:
            switch (iEditState) {
                case MAPHAZARD_EDIT_STATE_SELECT:
                    sc.title = "Hazards";
                    sc.a = "edit one";
                    sc.commands = {{"New hazard", SDLK_N}, done};
                    break;
                case MAPHAZARD_EDIT_STATE_TYPE:
                    sc.title = "Hazard: what kind";
                    sc.a = "choose";
                    sc.commands = {done};
                    break;
                case MAPHAZARD_EDIT_STATE_LOCATION:
                    sc.title = "Hazard: where it is";
                    sc.a = "place";
                    sc.commands = {{"Its settings", SDLK_P}, {"Delete it", SDLK_DELETE}, done};
                    break;
                default: {
                    struct {
                        MapHazardType itype;
                    } hazard {static_cast<MapHazardType>(hazardType(iEditMapHazard))};
                    sc.title = std::string("Hazard: ") + szHazardNames[static_cast<int>(hazard.itype)];
                    sc.commands = {{"Where it is", SDLK_L}};
                    if (hazard.itype == MapHazardType::FireBar || hazard.itype == MapHazardType::RotoDisc) {
                        sc.a = "angle and size";
                        sc.shoulders = "slower / faster";
                        sc.lb = SDLK_MINUS;
                        sc.rb = SDLK_EQUALS;
                        sc.holds = "LT: snap the angle";
                        sc.commands.push_back({"Slower", SDLK_MINUS});
                        sc.commands.push_back({"Faster", SDLK_EQUALS});
                        if (hazard.itype == MapHazardType::RotoDisc) {
                            for (int k = 1; k <= 9; k++)
                                sc.commands.push_back({std::to_string(k) + (k == 1 ? " disc" : " discs"), static_cast<SDL_Keycode>(SDLK_0 + k)});
                        }
                    } else {
                        //([ fires more often, ] less)
                        sc.shoulders = "more / less often";
                        sc.lb = SDLK_LEFTBRACKET;
                        sc.rb = SDLK_RIGHTBRACKET;
                        sc.commands.push_back({"More often", SDLK_LEFTBRACKET});
                        sc.commands.push_back({"Less often", SDLK_RIGHTBRACKET});
                        sc.commands.push_back({"Direction", SDLK_D});
                        if (hazard.itype == MapHazardType::BulletBill) {
                            sc.commands.push_back({"Slower", SDLK_MINUS});
                            sc.commands.push_back({"Faster", SDLK_EQUALS});
                        }
                    }
                    sc.commands.push_back({"Delete it", SDLK_DELETE});
                    sc.commands.push_back(done);
                    break;
                }
            }
            break;
        default:
            break;
    }
    return sc;
}

void pick(int col, int row)
{
    if (col < 0 || col >= MAPWIDTH || row < 0 || row >= MAPHEIGHT)
        return;
    const short blockType = g_map->objectdata[col][row].iType;
    const TilesetTile& tile = g_map->mapdata[col][row][selected_layer];
    if ((edit_mode == 0 || tile.iID < 0) && blockType >= 0) {
        set_block = blockType;
        edit_mode = 0;
    } else if (tile.iID >= 0) {
        set_tile_tileset = tile.iID;
        set_tile_start_x = tile.iCol;
        set_tile_start_y = tile.iRow;
        set_tile_end_x = tile.iCol;
        set_tile_end_y = tile.iRow;
        set_tile_cols = 1;
        set_tile_rows = 1;
        edit_mode = 1;
    }
}

}  // namespace shell
} // namespace leveleditor

// The level (map) editor, built into the game (EditorLauncher.h). Its sources are
// the standalone editor's (src/leveleditor), compiled here as one unit inside
// the namespace leveleditor:: so that its names don't clash with the game's or the
// other editor's. SMW_EMBEDDED_EDITOR leaves out what the game has itself (the
// window, the screen surfaces, the stand-ins for gameplay code).
#define SMW_EMBEDDED_EDITOR
#include "CoEdit.h"
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
} // namespace leveleditor

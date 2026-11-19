#ifndef SMW_MAP_H
#define SMW_MAP_H

#include "gfx.h"
#include "GlobalConstants.h"
#include "SimArena.h"
#include "TileTypes.h"

#include <SDL3/SDL.h>

#include <array>
#include <list>
#include <span>
#include <string>
#include <vector>

constexpr int NUM_FRAMES_IN_TILE_ANIMATION = 4;
constexpr int NUM_FRAMES_BETWEEN_TILE_ANIMATION = 8;

enum class PlatformPathType: unsigned char;

enum ReadType: uint8_t {
    read_type_full = 0,
    read_type_preview = 1,
    read_type_summary = 2
};

// NOTE: Types have a fixed order! See the 1.8 map reader.
enum MapItemType : unsigned char {
    GreenSpring,
    Spike,
    GreenKuriboShoe,
    GoldenSpring,
    StickyKuriboShoe,
    ThrowBox,
};

// NOTE: Types have a fixed order! See the 1.8 map reader.
enum class MapHazardType : unsigned char {
    FireBar,
    RotoDisc,
    BulletBill,
    FlameCannon,
    PirhanaGreen,
    PirhanaRed,
    PirhanaTall,
    PirhanaShort,
};

/*
                                    PD  SDR SDL SDB SDT G	DR	DL	DB	DT	I	SOT	S
    tile_nonsolid = 0				0	0	0	0	0	0	0	0	0	0	0	0	0
    tile_solid = 1					0	0	0	0	0	0	0	0	0	0	0	0	1
    tile_solid_on_top = 2			0	0	0	0	0	0	0	0	0	0	0	1	0
    tile_ice_on_top = 6				0	0	0	0	0	0	0	0	0	0	1	1	0
    tile_ice = 5					0	0	0	0	0	0	0	0	0	0	1	0	1
    tile_death = 121				0	0	0	0	0	0	1	1	1	1	0	0	1
    tile_death_on_top = 9			0	0	0	0	0	0	0	0	0	1	0	0	1
    tile_death_on_bottom = 17		0	0	0	0	0	0	0	0	1	0	0	0	1
    tile_death_on_left = 33			0	0	0	0	0	0	0	1	0	0	0	0	1
    tile_death_on_right = 65		0	0	0	0	0	0	1	0	0	0	0	0	1
    tile_ice_death_on_bottom = 21	0	0	0	0	0	0	0	0	1	0	1	0	1
    tile_ice_death_on_left = 37		0	0	0	0	0	0	0	1	0	0	1	0	1
    tile_ice_death_on_right = 69	0	0	0	0	0	0	1	0	0	0	1	0	1
    tile_gap = 128					0	0	0	0	0	1	0	0	0	0	0	0	0
    tile_super_death = 3961			0	1	1	1	1	0	1	1	1	1	0	0	1
    tile_super_death_top = 265		0	0	0	0	1	0	0	0	0	1	0	0	1
    tile_super_death_bottom	= 529	0	0	0	1	0	0	0	0	1	0	0	0	1
    tile_super_death_left = 1057	0	0	1	0	0	0	0	1	0	0	0	0	1
    tile_super_death_right = 2113	0	1	0	0	0	0	1	0	0	0	0	0	1
    tile_player_death =	4096		1	0	0	0	0	0	0	0	0	0	0	0	0
*/

class CPlayer;
class MovingPlatform;
class IO_MovingObject;

struct Point {
    short x, y;
};

enum WarpEnterDirection {
    WARP_DOWN = 0,
    WARP_LEFT = 1,
    WARP_UP = 2,
    WARP_RIGHT = 3,
    WARP_UNDEFINED = -1
};

enum WarpExitDirection {
    WARP_EXIT_UP = 0,
    WARP_EXIT_RIGHT = 1,
    WARP_EXIT_DOWN = 2,
    WARP_EXIT_LEFT = 3,
    WARP_EXIT_UNDEFINED = -1
};

struct Warp {
    WarpEnterDirection direction;
    short connection;
    short id;
};

struct WarpExit {
    WarpExitDirection direction;
    short connection;
    short id;

    short x; //Player location where player warps out of
    short y;

    short lockx;  //Location to display lock icon
    short locky;

    short warpx;  //map grid location for first block in warp
    short warpy;
    short numblocks;  //number of warp blocks for this warp

    short locktimer;  //If > 0, then warp is locked and has this many frames left until unlock
};

struct SpawnArea {
    short left;
    short top;
    short width;
    short height;
    short size;
};

struct MapItem {
    MapItemType itype;
    short ix;
    short iy;
};

struct MapHazard {
    MapHazardType itype;
    short ix;
    short iy;

    short iparam[NUMMAPHAZARDPARAMS];
    float dparam[NUMMAPHAZARDPARAMS];
};

struct TilesetTile {
    short iID;
    short iCol;
    short iRow;
};

struct AnimatedTile {
    short id;
    TilesetTile layers[4];
    SDL_Rect rSrc[4][4];
    SDL_Rect rAnimationSrc[2][4];
    SDL_Rect rDest;
    bool fBackgroundAnimated;
    bool fForegroundAnimated;
    MovingPlatform * pPlatform;
};

struct MapBlock {
    short iType;
    short iSettings[NUM_BLOCK_SETTINGS];
    bool fHidden;
};

class IO_Block;

void DrawMapHazard(const MapHazard& hazard, short iSize, bool fDrawCenter, SDL_Surface* dst);
void DrawPlatform(PlatformPathType pathtype, std::span<const TilesetTile> tiles,
    short startX, short startY, short endX, short endY,
    float angle, float radiusX, float radiusY,
    short iSize, short iPlatformWidth, short iPlatformHeight,
    bool fDrawPlatform, bool fDrawShadow, SDL_Surface* dst);

// The level editor built into the game (src/smw/editors) has the same
// functions in its own namespace: they get the same access (see the friends
// of CMap and MovingPlatform)
namespace leveleditor {
void drawmap(bool fScreenshot, short iBlockSize, bool fWithPlatforms);
void drawlayer(int layer, bool fUseCopied, short iBlockSize);
void takescreenshot();
bool copyselectedtiles();
void clearselectedmaptiles();
void pasteselectedtiles(int movex, int movey);
TileType CalculateTileType(short x, short y);
void UpdateTileType(short x, short y);
bool TileTypeIsModified(short x, short y);
void AdjustMapItems(short x, short y);
void RemoveMapItemAt(short x, short y);
int editor_edit();
int editor_tiles();
int editor_blocks();
int editor_warp();
int editor_modeitems();
void init_editor_properties(short iBlockCol, short iBlockRow);
int editor_properties(short iBlockCol, short iBlockRow);
int editor_maphazards();
short NewMapHazard();
short* GetBlockProperty(short x, short y, short iBlockCol, short iBlockRow, short* iSettingIndex);
int save_as();
void draw_platform(short iPlatform, bool fDrawTileTypes);
void insert_platforms_into_map();
void loadcurrentmap();
void SetNoSpawn(short nospawnmode, short col, short row, bool value);
int editor_platforms();
class CoeditDocument;    // editing together (src/smw/editors/CoEdit.h)
namespace shell {           // the editor window (src/smw/editors/LevelShell.h)
void setBlock(int block, bool on);
bool switchOn(int color);
void pick(int col, int row);
int hazardType(int index);
bool hasProperties(int col, int row);
class Cells;    // (painting, filling, copying cells)
}
} // namespace leveleditor

// The map files saved (CMap::saveMap), in order: the editors' launcher reads them (EditorLauncher.cpp)
extern std::vector<std::string> g_savedMapFiles;

class CMap
{
    public:
        CMap();
        ~CMap();

        void clearMap();
        void clearPlatforms();

        void loadMap(const std::string& file, ReadType iReadType);
        // The file of the latest loadMap()
        const std::string& loadedFile() const { return szLoadedFile; }
        void saveMap(const std::string& file);

        gfxSprite createThumbnailSurface(bool fUseClassicPack);
        void saveThumbnail(const std::string &file, bool fUseClassicPack);

        void UpdateAllTileGaps();
        void UpdateTileGap(short i, short j);

        //void convertMap();

        void shift(int xshift, int yshift);

        void predrawbackground(gfxSprite &background, gfxSprite &mapspr);
        void predrawforeground(gfxSprite &foregroundspr);
        /// The front layer's draw areas fitted to the front image drawn for the match (only the
        /// cells it shows something in)
        void fitDrawAreas(const gfxSprite& front);

        void SetupAnimatedTiles();

        void preDrawPreviewBackground(gfxSprite& targetSurface, bool fThumbnail);
        void preDrawPreviewBackground(const gfxSprite& spr_background, gfxSprite& targetSurface, bool fThumbnail);
        void preDrawPreviewBlocks(gfxSprite& targetSurface, bool fThumbnail);
        void preDrawPreviewForeground(gfxSprite& targetSurface, bool fThumbnail);
        void preDrawPreviewWarps(gfxSprite& targetSurface, bool fThumbnail);
        void preDrawPreviewMapItems(gfxSprite& targetSurface, bool fThumbnail);

        void drawfrontlayer();

        bool checkforwarp(short iData1, short iData2, short iData3, short iDirection);

        void optimize();

        //returns the tiletype at the specific position (map coordinates) of the
        //front most visible tile
    int map(int x, int y) {
        return tileToFlags(mapdatatop[x][y]);
    }

    IO_Block * block(short x, short y) {
            return blockdata[x][y];
        }

    Warp * warp(short x, short y) {
            return &warpdata[x][y];
        }

    MapBlock * blockat(short x, short y) {
            return &objectdata[x][y];
        }

    bool spawn(short iType, short x, short y) {
            return !nospawn[iType][x][y];
        }

        void updatePlatforms();
        void drawPlatforms(short iLayer);
        void drawPlatforms(short iOffsetX, short iOffsetY, short iLayer);
        void resetPlatforms();

        void movingPlatformCollision(CPlayer * player);
        void movingPlatformCollision(IO_MovingObject * object);
        bool movingPlatformCheckSides(IO_MovingObject * object);

    bool isconnectionlocked(int connection) {
        return warplocked[connection];
    }
        void lockconnection(int connection);

        WarpExit * getRandomWarpExit(int connection, int currentID);
        // The warp exits (the AI plans paths through warps)
        int warpExitCount() const { return numwarpexits; }
        const WarpExit& warpExit(int k) const { return warpexits[k]; }

        void clearWarpLocks();
        void drawWarpLocks();

        void update();

        void AddPermanentPlatform(MovingPlatform * platform);
        void AddTemporaryPlatform(MovingPlatform * platform);

        const std::vector<MovingPlatform*>& permanentPlatforms() const { return platforms; }
        /// Any platform drawPlatforms(x, y, layer) draws, on any layer
        bool hasPlatformsToDraw() const
        {
            for (const auto& layer : platformdrawlayer) {
                if (!layer.empty())
                    return true;
            }
            return false;
        }
        const sim::list<MovingPlatform*>& temporaryPlatforms() const { return tempPlatforms; }

        // Calls fn(void* data, size_t size, const char* name) for each part of
        // the map that changes during a match, for rollback snapshots (see
        // smw/rollback/Snapshot.h). Everything else in CMap is level data that
        // stays the same during a match. The permanent moving platforms and
        // the temporary ones (and their list nodes) live in the simulation
        // arena, so only the list object itself is listed here.
        template <typename Fn>
        void forEachRuntimeRegion(Fn&& fn) {
            fn(mapdatatop, sizeof(mapdatatop), "map.mapdatatop");
            fn(blockdata, sizeof(blockdata), "map.blockdata");
            fn(&tempPlatforms, sizeof(tempPlatforms), "map.tempPlatforms");
            fn(warpexits, sizeof(warpexits), "map.warpexits");
            fn(warplocktimer, sizeof(warplocktimer), "map.warplocktimer");
            fn(warplocked, sizeof(warplocked), "map.warplocked");
            fn(&iSwitches, sizeof(iSwitches), "map.iSwitches");
        }

        bool findspawnpoint(short iType, short * x, short * y, short width, short height, bool tilealigned);
        bool IsInPlatformNoSpawnZone(short x, short y, short width, short height);

        std::string szBackgroundFile;
        std::string szLoadedFile;
        short backgroundID;
        short eyecandy[3];
        short musicCategoryID;

        short iNumRaceGoals = 0;
        short iNumFlagBases = 0;

    private:

        void SetTileGap(short i, short j);
        void calculatespawnareas(short iType, bool fUseTempBlocks, bool fIgnoreDeath);

        TilesetTile	mapdata[MAPWIDTH][MAPHEIGHT][MAPLAYERS];
        TileType		mapdatatop[MAPWIDTH][MAPHEIGHT];
        MapBlock	objectdata[MAPWIDTH][MAPHEIGHT];
        IO_Block*   blockdata[MAPWIDTH][MAPHEIGHT];
        bool		nospawn[NUMSPAWNAREATYPES][MAPWIDTH][MAPHEIGHT];

        std::vector<AnimatedTile*> animatedtiles;

                std::vector<MovingPlatform*> platforms;
        sim::list<MovingPlatform*> tempPlatforms;     // allocated in the simulation arena

                std::vector<MapItem> mapitems;
                std::vector<MapHazard>	maphazards;

        SpawnArea	spawnareas[NUMSPAWNAREATYPES][MAXSPAWNAREAS];
        short		numspawnareas[NUMSPAWNAREATYPES];
        short		totalspawnsize[NUMSPAWNAREATYPES];

        Warp        warpdata[MAPWIDTH][MAPHEIGHT];
        short		numwarpexits = 0; //number of warp exits
        WarpExit	warpexits[MAXWARPS];
        short		warplocktimer[10];
        bool		warplocked[10];
        short		maxConnection = 0;

        SDL_Rect	tilebltrect;
        SDL_Rect    bltrect;

        SDL_Rect	drawareas[MAXDRAWAREAS];
        short		numdrawareas = 0;
        void computeDrawAreas();
        void buildDrawAreas(const bool skip[MAPWIDTH][MAPHEIGHT]);

        std::array<short, 4> iSwitches;
        std::list<IO_Block*> switchBlocks[8];

        std::array<bool, NUM_AUTO_FILTERS> fAutoFilter;

        Point       racegoallocations[MAXRACEGOALS];

        Point       flagbaselocations[4];

        short		iTileAnimationTimer = 0;
        short		iTileAnimationFrame = 0;

        short iAnimatedBackgroundLayers = 0;
        SDL_Surface * animatedFrontmapSurface = nullptr;
        gfxSprite animatedTilesSurface;

        short iAnimatedTileCount = 0;
        short iAnimatedVectorIndices[NUM_FRAMES_BETWEEN_TILE_ANIMATION + 1];
        SDL_Surface * animatedBackmapSurface = nullptr;

        std::list<MovingPlatform*> platformdrawlayer[PLATFORMDRAWLAYERS];

        void AnimateTiles(short iFrame);
        void ClearAnimatedTiles();

        void draw(gfxSprite& targetsurf, int layer);
        void drawThumbnailHazards(gfxSprite& targetSurface);
                void drawThumbnailPlatforms(gfxSprite& targetSurface);
        void drawPreview(gfxSprite& targetsurf, int layer, bool fThumbnail);
        void drawPreviewBlocks(gfxSprite& targetSurface, bool fThumbnail);

        void addPlatformAnimatedTiles();

        friend class CPlayerAI;

        friend void drawmap(bool fScreenshot, short iBlockSize, bool fWithPlatforms);
        friend void drawlayer(int layer, bool fUseCopied, short iBlocksize);
        friend void takescreenshot();

        friend bool copyselectedtiles();
        friend void clearselectedmaptiles();
        friend void pasteselectedtiles(int movex, int movey);

        friend TileType CalculateTileType(short x, short y);
        friend void UpdateTileType(short x, short y);
        friend bool TileTypeIsModified(short x, short y);

        friend void AdjustMapItems(short iClickX, short iClickY);
        friend void RemoveMapItemAt(short x, short y);

        friend int editor_edit();
        friend int editor_tiles();
        friend int editor_blocks();
        friend int editor_warp();
        friend int editor_modeitems();
        friend void init_editor_properties(short iBlockCol, short iBlockRow);
        friend int editor_properties(short iBlockCol, short iBlockRow);
        friend int editor_maphazards();
        friend short NewMapHazard();

        friend short * GetBlockProperty(short x, short y, short iBlockCol, short iBlockRow, short * iSettingIndex);
        friend int save_as();
        friend int load();
        friend void LoadMapObjects(bool fPreview);
        friend void LoadMapHazards(bool fPreview);
        friend void draw_platform(short iPlatform, bool fDrawTileTypes);
        friend void insert_platforms_into_map();
        friend void loadcurrentmap();
        friend void loadmap(char * szMapFile);
        friend void SetNoSpawn(short nospawnmode, short col, short row, bool value);
        friend int editor_platforms();

        friend void leveleditor::drawmap(bool, short, bool);
        friend void leveleditor::drawlayer(int, bool, short);
        friend void leveleditor::takescreenshot();
        friend bool leveleditor::copyselectedtiles();
        friend void leveleditor::clearselectedmaptiles();
        friend void leveleditor::pasteselectedtiles(int, int);
        friend TileType leveleditor::CalculateTileType(short, short);
        friend void leveleditor::UpdateTileType(short, short);
        friend bool leveleditor::TileTypeIsModified(short, short);
        friend void leveleditor::AdjustMapItems(short, short);
        friend void leveleditor::RemoveMapItemAt(short, short);
        friend int leveleditor::editor_edit();
        friend int leveleditor::editor_tiles();
        friend int leveleditor::editor_blocks();
        friend int leveleditor::editor_warp();
        friend int leveleditor::editor_modeitems();
        friend void leveleditor::init_editor_properties(short, short);
        friend int leveleditor::editor_properties(short, short);
        friend int leveleditor::editor_maphazards();
        friend short leveleditor::NewMapHazard();
        friend short* leveleditor::GetBlockProperty(short, short, short, short, short*);
        friend int leveleditor::save_as();
        friend void leveleditor::draw_platform(short, bool);
        friend void leveleditor::insert_platforms_into_map();
        friend void leveleditor::loadcurrentmap();
        friend void leveleditor::SetNoSpawn(short, short, short, bool);
        friend int leveleditor::editor_platforms();
        friend void leveleditor::shell::setBlock(int, bool);
        friend bool leveleditor::shell::hasProperties(int, int);
        friend bool leveleditor::shell::switchOn(int);
        friend void leveleditor::shell::pick(int, int);
        friend int leveleditor::shell::hazardType(int);
        friend class leveleditor::shell::Cells;
        friend class leveleditor::CoeditDocument;

        friend class B_BreakableBlock;
        friend class B_DonutBlock;
        friend class B_ThrowBlock;
        friend class B_OnOffSwitchBlock;
        friend class B_FlipBlock;
        friend class B_WeaponBreakableBlock;

        friend class MovingPlatform;
        friend class MapList;
        friend class MI_MapField;
        friend class CPlayer;
        friend class PlayerWarpStatus;

        friend class MO_FlagBase;
        friend class OMO_RaceGoal;

        friend class MapReader;
            friend class MapReader1500;
            friend class MapReader1600;
            friend class MapReader1700;
            friend class MapReader1701;
            friend class MapReader1702;
            friend class MapReader1800;
            friend class MapReader1802;
};

// Defined in global.cpp
extern CMap* g_map;
void LoadCurrentMapBackground();
// The background image file of a map (the classic one if it isn't installed)
std::string mapBackgroundPath(const CMap& map);
// rm->spr_background if it's that file's image (loaded already), else null
const gfxSprite* loadedMapBackground(const std::string& path);
// That file's image becomes rm->spr_background (loaded elsewhere: a map preview's worker)
void setLoadedMapBackground(const std::string& path, gfxSprite&& image);


// Defined in map.cpp
extern short g_iCurrentDrawIndex;
extern SDL_Rect g_rFlameRects[4][4];
extern SDL_Rect g_rPirhanaRects[4][4][4];

#endif // SMW_MAP_H

#pragma once

#define SMW_EDITOR

#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "CmdArgs.h"
#include "eyecandy.h"
#include "FPSLimiter.h"
#include "GameMode.h"
#include "gfx.h"
#include "GlobalConstants.h"
#include "map.h"
#include "MapList.h"
#include "movingplatform.h"
#include "path.h"
#include "FileIO.h"
#include "FileList.h"
#include "ResourceManager.h"
#include "sfx.h"
#include "SimulationState.h"
#include "TilesetManager.h"
#include "GameValues.h"

// Included only for movingplatform
// TODO: Remove and fix linker errors
#include "objects/moving/MovingObject.h"
#include "player.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctype.h>
#include <cstring>
#include <cstdlib>
#include <string>

#ifdef _WIN32
	#include <windows.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef SMW_EMBEDDED_EDITOR
// Built into the game: the events, the input state and the screen flips go
// through the controller layer (editors/EditorInput.h)
#include "editors/EditorInput.h"
#define SDL_PollEvent(e) ::editors::input::pollEvent(e)
#define SDL_GetKeyboardState(n) ::editors::input::keyboardState(n)
#define SDL_GetModState() ::editors::input::modState()
#define SDL_GetMouseState(x, y) ::editors::input::mouseState(x, y)
#define gfx_flipscreen() ::editors::input::flip()
#else
// The standalone editor: the mouse in the screen's 640x480, at any window size
#include "gfx.h"
static inline bool editorPollEvent(SDL_Event* event)
{
    if (!SDL_PollEvent(event))
        return false;
    gfx_eventtoscreen(event);
    return true;
}
#define SDL_PollEvent(e) editorPollEvent(e)
#define SDL_GetMouseState(x, y) gfx_mousestate(x, y)
#endif

#ifdef SMW_EMBEDDED_EDITOR
// Built into the game (EditorLauncher.h): the editor's own names are in a
// namespace; the screen, the eyecandy and the simulation state are the game's
namespace leveleditor {
#endif


enum {EDITOR_EDIT, EDITOR_TILES, EDITOR_QUIT, SAVE_AS, FIND, CLEAR_MAP, EDITOR_BLOCKS, NEW_MAP, SAVE, EDITOR_WARP, EDITOR_EYECANDY, DISPLAY_HELP, EDITOR_PLATFORM, EDITOR_TILETYPE, EDITOR_BACKGROUNDS, EDITOR_MAPITEMS, EDITOR_ANIMATION, EDITOR_PROPERTIES, EDITOR_MODEITEMS, EDITOR_MAPHAZARDS};

constexpr int MAX_PLATFORMS = 32;
constexpr int MAX_PLATFORM_VELOCITY = 16;
constexpr int NUMTILETYPES = 19;
constexpr int UI_PLATFORM_ROWS = 4;
constexpr int UI_PLATFORM_COLS = 8;

class EditorMapTile
{
	public:
		TilesetTile	tile[MAPLAYERS];
		MapBlock	block;
		Warp		warp;
		bool		nospawn[NUMSPAWNAREATYPES];
		TileType	tiletype;
		int			item;
};

class MapPlatform
{
	public:
    MapPlatform()
        : tiles(MAPWIDTH * MAPHEIGHT)
        , types(MAPWIDTH * MAPHEIGHT)
    {}

    void UpdatePreview() {
        if (!preview) {
            preview = gfxSprite::blank(160, 120);
            SDL_SetSurfaceColorKey(preview.getSurface(), true, SDL_MapSurfaceRGB(preview.getSurface(), 255, 0, 255));
        }

        SDL_FillSurfaceRect(preview.getSurface(), nullptr, SDL_MapSurfaceRGB(preview.getSurface(), 255, 0, 255));

        for (short iPlatformX = 0; iPlatformX < MAPWIDTH; iPlatformX++) {
            for (short iPlatformY = 0; iPlatformY < MAPHEIGHT; iPlatformY++) {
                TilesetTile * tile = &tiles[iPlatformX * MAPHEIGHT + iPlatformY];

                SDL_Rect bltrect = {iPlatformX << 3, iPlatformY << 3, THUMBTILESIZE, THUMBTILESIZE};
                if (tile->iID >= 0) {
                    g_tilesetmanager->tileset(tile->iID)->draw(DrawSize::Thumbnail, CTilesetManager::rect(DrawSize::Thumbnail, tile->iCol, tile->iRow), preview.getSurface(), bltrect);
                } else if (tile->iID == TILESETANIMATED) {
                    rm->spr_tileanimation[2].draw(CTilesetManager::rect(DrawSize::Thumbnail, tile->iCol * 4, tile->iRow), preview.getSurface(), bltrect);
                } else if (tile->iID == TILESETUNKNOWN) {
                    //Draw unknown tile
                    rm->spr_unknowntile[2].draw(CTilesetManager::rect(DrawSize::Thumbnail, 0, 0), preview.getSurface(), bltrect);
                }
            }
        }
    }

    std::vector<TilesetTile> tiles;
    std::vector<TileType> types;
		short iVelocity;
		short iStartX;
		short iStartY;
		short iEndX;
		short iEndY;

		PlatformPathType iPathType;

		float fAngle;
		float fRadiusX;
		float fRadiusY;

		short iDrawLayer;

		SDL_Rect rIcon[2];
		gfxSprite preview;
};

bool ReadAnimatedTileTypeFile(const char * szFile);
bool WriteAnimatedTileTypeFile(const char * szFile);

int save_as();
int find();
int clearMap();
bool dialog(const char * title, const char * instructions, char * input, int inputsize);
int display_help();

#ifdef _DEBUG
	void convertAll();
#endif

void drawmap(bool fScreenshot, short iBlockSize, bool fWithPlatforms = false);
void draw_platform(short iPlatform, bool fDrawTileTypes);
void SetPlatformToDefaults(short iPlatform);

void loadcurrentmap();
int savecurrentmap();
int findcurrentstring();
int newmap();
void save_map(const std::string &file);
void insert_platforms_into_map();
void CalculatePlatformDims(short iPlatform, short * ix, short * iy, short * iw, short * ih);
void LoadBackgroundPage(std::array<gfxSprite, 16>& sBackgrounds, short iPage);

int editor_edit();
int editor_warp();
int editor_eyecandy();
int	editor_tiles();
int editor_blocks();
int editor_mapitems();
int editor_platforms();
int editor_maphazards();
int editor_tiletype();
int editor_backgrounds();
int editor_animation();
int editor_properties(short iBlockCol, short iBlockRow);
int editor_modeitems();

void DisplayPlatformPreview(short iPlatformId, short iMouseX, short iMouseY);
void SwitchPlatforms(short iPlatformId1, short iPlatformId2);
void CopyPlatform(MapPlatform * toPlatform, MapPlatform * fromPlatform);

void UpdatePlatformPathStart(short iEditPlatform, short iClickX, short iClickY, bool fSnapToTile);
void UpdatePlatformPathEnd(short iEditPlatform, short iClickX, short iClickY, bool fSnapToTile);
void UpdatePlatformPathAngle(short iEditPlatform, short iClickX, short iClickY, bool fSnapToAngle);
void UpdatePlatformPathRadius(short iEditPlatform, short iClickX, short iClickY, bool fSnapToRadius, bool fIsRadiusX, bool fSetCircle);

void resetselectedtiles();
void copymoveselection();
void pastemoveselection(int movex, int movey);
bool copyselectedtiles();
void clearselectedmaptiles();
void pasteselectedtiles(int movex, int movey);
void getcenterselection(int * x, int * y);
void takescreenshot();

void DrawMessage();

short NewMapHazard();
void DrawMapHazardControls(const MapHazard& hazard);
void AdjustMapHazardRadius(MapHazard * hazard, short iClickX, short iClickY);

void SetNoSpawn(short nospawnmode, short col, short row, bool value);

// Helpers defined in EditorGlobals.cpp and EditorMapEdit.cpp
int bound_to_window_w(int x);
int bound_to_window_h(int y);
void bound_mouse_motion_coords();
bool CheckKey(const bool* keystate, SDL_Keycode key);
void SetTilesetTile(TilesetTile * tile, short iTileset, short iCol, short iRow);
void ClearTilesetTile(TilesetTile * tile);
void UpdateTileType(short x, short y);
void RemoveMapItemAt(short x, short y);

#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern SDL_Surface *screen;
#endif
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern SDL_Surface *blitdest;
#endif
extern SDL_Event event;
extern TileType set_type;
extern int set_tile_rows;
extern int set_tile_cols;
extern int set_tile_tileset;
extern int set_tile_start_x;
extern int set_tile_start_y;
extern int set_tile_end_x;
extern int set_tile_end_y;
extern bool set_tile_drag;
extern int view_tileset_x;
extern int view_tileset_y;
extern int view_animated_tileset_x;
extern int set_block;
extern int set_block_switch_on;
extern TileType set_tiletype;
extern int set_mapitem;
extern int set_direction;
extern int set_connection;
extern int edit_mode;
extern int selected_layer;
extern int nospawn_mode;
extern int move_mode;
extern int move_start_x;
extern int move_start_y;
extern int move_offset_x;
extern int move_offset_y;
extern bool move_nodrag;
extern bool move_replace;
extern int move_drag_start_x;
extern int move_drag_start_y;
extern int move_drag_offset_x;
extern int move_drag_offset_y;
extern int state;
extern bool selectedtiles[MAPWIDTH][MAPHEIGHT];
extern bool moveselectedtiles[MAPWIDTH][MAPHEIGHT];
extern EditorMapTile copiedtiles[MAPWIDTH][MAPHEIGHT];
extern int copiedlayer;
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern short x_shake;
#endif
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern short y_shake;
#endif
extern int mouse_x, mouse_y;
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern CEyecandyContainer eyecandy[3];
#endif
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern CGameMode *gamemodes[GAMEMODE_LAST];
#endif
#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
extern SimulationState g_sim;
#endif
extern TileType * animatedtiletypes;
extern gfxSprite s_platform;
extern gfxSprite s_platformpathbuttons;
extern gfxSprite s_maphazardbuttons;
extern bool viewblocks;
extern bool view_only_layer;
extern bool viewwarps;
extern bool ignoreclick;
extern std::string findstring;
extern short g_iNumPlatforms;
extern MapPlatform g_Platforms[MAX_PLATFORMS];
extern BackgroundList *backgroundlist;
extern short g_musiccategorydisplaytimer;
extern short g_messagedisplaytimer;
extern std::string g_szMessageTitle;
extern std::string g_szMessageLine[3];
extern bool g_fFullScreen;
extern SDL_Rect r;
extern SDL_Rect rNewButton[2];
extern SDL_Rect rTypeButton[8][4];
extern short view_tileset_repeat_direction;
extern short view_tileset_repeat_timer;

// The editor's own help text, mode names and file name on its screens; built
// into the game, the overlay shows them instead (editors/EditorInput.cpp)
#ifdef SMW_EMBEDDED_EDITOR
constexpr bool OLD_HUD = false;
#else
constexpr bool OLD_HUD = true;
#endif

#ifdef SMW_EMBEDDED_EDITOR
} // namespace leveleditor
#endif

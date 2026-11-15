#include "LevelEditorState.h"

#ifndef SMW_EMBEDDED_EDITOR  // stand-ins for the game's code (the game has the real ones)
void CPlayer::flipsidesifneeded() {}
bool CPlayer::isInvincible() const { return false; }
bool CPlayer::isShielded() const { return false; }
PlayerKillType CPlayer::KillPlayerMapHazard(bool, KillStyle, bool, short) { return PlayerKillType::None; }
bool PlayerKuriboShoe::is_on() const { return false; }
void IO_MovingObject::flipsidesifneeded() {}
void IO_MovingObject::KillObjectMapHazard(short playerID) {}
void removeifprojectile(IO_MovingObject * object, bool playsound, bool forcedead) {}

SDL_Surface		*screen;
SDL_Surface		*blitdest;
#endif
SDL_Event		event;

TileType		set_type = TileType::Solid;
int				set_tile_rows = 0;
int				set_tile_cols = 0;
int				set_tile_tileset = 0;
int				set_tile_start_x = 0;
int				set_tile_start_y = 0;
int				set_tile_end_x = 0;
int				set_tile_end_y = 0;
bool			set_tile_drag = false;

int				view_tileset_x = 0;
int				view_tileset_y = 0;

int				view_animated_tileset_x = 0;

int				set_block = 0;
int				set_block_switch_on = 0;
TileType		set_tiletype = TileType::NonSolid;
int				set_mapitem = 0;

int				set_direction = 0;
int				set_connection = 0;

int				edit_mode = 1;
int				selected_layer = 0;
int				nospawn_mode = 0;

int				move_mode = 0;
int				move_start_x = 0;
int				move_start_y = 0;
int				move_offset_x = 0;
int				move_offset_y = 0;
bool			move_nodrag = false;
bool			move_replace = true;

int				move_drag_start_x = 0;
int				move_drag_start_y = 0;
int				move_drag_offset_x = 0;
int				move_drag_offset_y = 0;

int				state;
bool			selectedtiles[MAPWIDTH][MAPHEIGHT];
bool			moveselectedtiles[MAPWIDTH][MAPHEIGHT];
EditorMapTile	copiedtiles[MAPWIDTH][MAPHEIGHT];
int				copiedlayer;

//// Global stuff that the map editor doesn't need, but has references to
#ifndef SMW_EMBEDDED_EDITOR
short			x_shake = 0;
short			y_shake = 0;
#endif

int				mouse_x, mouse_y;

int bound_to_window_w(int x) {
	return std::max(0, std::min(x, 640 - 1));
}

int bound_to_window_h(int y) {
	return std::max(0, std::min(y, 480 - 1));
}

void bound_mouse_motion_coords() {
    mouse_x = bound_to_window_w(event.motion.x);
    mouse_y = bound_to_window_h(event.motion.y);
}

#ifndef SMW_EMBEDDED_EDITOR  // stand-ins for the game's code (the game has the real ones)
CEyecandyContainer eyecandy[3];
CGameMode		*gamemodes[GAMEMODE_LAST];
SimulationState g_sim;
CPlayer::~CPlayer() {}

void SDLCALL musicfinished(void*, MIX_Track* rawptr){}
#endif

IO_MovingObject* createpowerup(short iType, short ix, short iy, bool side, bool spawn)
{
    return nullptr;
}
///////

TileType * animatedtiletypes;

bool CheckKey(const bool* keystate, SDL_Keycode key) {
    return keystate[SDL_GetScancodeFromKey(key, nullptr)];
}

gfxSprite s_platform;
gfxSprite s_platformpathbuttons;
gfxSprite s_maphazardbuttons;

bool viewblocks = true;
bool view_only_layer = false;
bool viewwarps = true;
bool ignoreclick = false;

std::string findstring;

short g_iNumPlatforms = 0;
MapPlatform g_Platforms[MAX_PLATFORMS];

BackgroundList *backgroundlist;
short g_musiccategorydisplaytimer = 0;

short g_messagedisplaytimer = 0;
std::string g_szMessageTitle = "";
std::string g_szMessageLine[3];

void SetTilesetTile(TilesetTile * tile, short iTileset, short iCol, short iRow)
{
	tile->iID = iTileset;
	tile->iCol = iCol;
	tile->iRow = iRow;
}

void ClearTilesetTile(TilesetTile * tile)
{
	SetTilesetTile(tile, TILESETNONE, 0, 0);
}

bool g_fFullScreen = false;

// Globals shared with other editor translation units

SDL_Rect r;
SDL_Rect rNewButton[2];
SDL_Rect rTypeButton[8][4];
short view_tileset_repeat_direction = -1;
short view_tileset_repeat_timer = 0;

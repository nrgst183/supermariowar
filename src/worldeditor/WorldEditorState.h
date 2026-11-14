#pragma once

#define SMW_EDITOR

#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "CmdArgs.h"
#include "eyecandy.h"
#include "FileIO.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameModeSettingsSerialization.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "map.h"
#include "MapList.h"
#include "SimulationState.h"
#include "menu/ModeOptionsMenu.h"
#include "ObjectContainer.h"
#include "path.h"
#include "player.h"
#include "ResourceManager.h"
#include "sfx.h"
#include "TilesetManager.h"
#include "world.h"
#include "world/WorldMapIO.h"
#include "WorldTourStop.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_MapField.h"
#include "ui/MI_ImageSelectField.h"
#include "ui/MI_Text.h"
#include "ui/MI_TextField.h"
#include "objects/blocks/WeaponBreakableBlock.h"
#include "objects/carriable/CO_Flag.h"
#include "objects/carriable/CO_Spring.h"
#include "objects/carriable/CO_Bomb.h"
#include "objects/carriable/CO_Egg.h"
#include "objects/carriable/CO_PhantoKey.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Star.h"
#include "objects/carriable/CO_ThrowBox.h"
#include "objects/moving/MO_Coin.h"
#include "objects/overmap/WO_Area.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
	#include <windows.h>
#endif

// TODO: Fix JS related problems.
#ifdef __EMSCRIPTEN__
#define SDL_Delay(n) ;
#endif

enum {EDITOR_EDIT, EDITOR_WATER, EDITOR_BACKGROUND, EDITOR_STAGEFOREGROUND, EDITOR_STRUCTUREFOREGROUND, EDITOR_BRIDGES, EDITOR_PATHSPRITE, EDITOR_VEHICLES, EDITOR_QUIT, SAVE_AS, FIND, CLEAR_WORLD, NEW_WORLD, RESIZE_WORLD, SAVE, EDITOR_WARP, DISPLAY_HELP, EDITOR_PATH, EDITOR_TYPE, EDITOR_BOUNDARY, EDITOR_START_ITEMS, EDITOR_STAGE};

int save_as();
int find();
int clear_world();
bool dialog(const char * title, const char * instructions, char * input, int inputsize);
int display_help();

void drawmap(bool fScreenshot, short iBlockSize);

bool UpdateForeground(short iCol, short iRow);
void UpdatePath(short iCol, short iRow);
void AutoSetPath(short iCol, short iRow);
bool UpdateCoastline(short iCol, short iRow);
bool AutoSetTile(short iCol, short iRow);
void UpdatePathSprite(short iCol, short iRow);
void AutoSetPathSprite(short iCol, short iRow);
short AdjustForeground(short iSprite, short iCol, short iRow);

void GetForegroundTileValues(short iCol, short iRow, short iOldTiles[9]);
bool ForegroundTileValuesChanged(short iCol, short iRow, short iOldTiles[9]);

void ReadVehiclesIntoEditor();
void WriteVehiclesIntoWorld();
void AddVehicleToTile(short iCol, short iRow, short iType);
void RemoveVehicleFromTile(short iCol, short iRow);

void ReadWarpsIntoEditor();
void WriteWarpsIntoWorld();
void AddWarpToTile(short iCol, short iRow, short iType);
void RemoveWarpFromTile(short iCol, short iRow);

void loadcurrentworld();
int savecurrentworld();
int findcurrentstring();
int new_world();
int resize_world();

int editor_edit();
int editor_warp();
int editor_boundary();
int editor_water();
int	editor_background();
int editor_stageforeground();
int editor_structureforeground();
int editor_bridges();
int editor_vehicles();
int	editor_path();
int editor_pathsprite();
int editor_type();
int editor_stage();
int editor_start_items();

void DisplayStageDetails(bool fForce, short iStageId, short iMouseX, short iMouseY);

void updateworldsurface();
void takescreenshot();

void SetDisplayMessage(short iTime,
                       const char *szTitle,
                       const char *szLine1,
                       const char *szLine2,
                       const char *szLine3);
void DrawMessage();

// Helpers defined in WorldEditorGlobals.cpp and WorldEditorStage.cpp
int bound_to_window_w(int x);
int bound_to_window_h(int y);
void bound_mouse_motion_coords();
bool CheckKey(const bool* keystate, SDL_Keycode key);
void SetBonusString(std::string& szString, short iPlace, short iItem, short iStageType);
void TestAndSetBonusItem(TourStop * ts, short iPlace, short iButtonX, short iButtonY);
void AdjustBonuses(TourStop * ts);
void SaveStage(short iEditStage);
void EditStage(short iEditStage);
void EnableStageMenu(bool fEnable);
void NewStage(short * iEditStage);

extern SDL_Surface *screen;
extern SDL_Surface *blitdest;
extern gfxSprite sMapSurface;
extern SDL_Rect rectSrcSurface;
extern SDL_Rect rectDstSurface;
extern bool fNeedBlackBackground;
extern short iWorldWidth, iWorldHeight;
extern SDL_Event event;
extern gfxSprite spr_dialog;
extern gfxSprite menu_shade;
extern gfxSprite spr_largedialog;
extern gfxSprite spr_warps[3];
extern gfxSprite spr_path;
extern gfxSprite spr_vehicleicons;
extern int set_tile;
extern bool fAutoPaint;
extern int edit_mode;
extern int draw_offset_col;
extern int draw_offset_row;
extern int draw_offset_x;
extern int draw_offset_y;
extern int state;
extern bool selectedtiles[MAPWIDTH][MAPHEIGHT];
extern bool moveselectedtiles[MAPWIDTH][MAPHEIGHT];
extern WorldMapTile copiedtiles[MAPWIDTH][MAPHEIGHT];
extern int mouse_x, mouse_y;
extern WorldVehicle g_wvVehicleStamp;
extern gfxSprite spr_warplock;
extern short x_shake;
extern short y_shake;
extern gfxSprite spr_awardsouls, spr_fireballexplosion;
extern gfxSprite spr_backmap[2];
extern gfxSprite spr_background;
extern gfxSprite spr_explosion;
extern sfxSound sfx_boomerang;
extern sfxSound sfx_bobombsound;
extern sfxSound sfx_bulletbillsound;
extern sfxSound sfx_flamecannon;
extern sfxSound sfx_hit;
extern sfxSound sfx_kicksound;
extern sfxSound sfx_mip;
extern sfxSound sfx_transform;
extern gfxSprite **spr_player[4];
extern CGameMode *gamemodes[GAMEMODE_LAST];
extern gfxSprite spr_overlay, spr_overlayhole;
extern CEyecandyContainer eyecandy[3];
extern gfxSprite spr_frontmap[2];
extern SimulationState g_sim;
extern gfxSprite spr_scoretext;
extern gfxSprite spr_poof;
extern bool ignoreclick;
extern std::string findstring;
extern short g_musiccategorydisplaytimer;
extern short g_messagedisplaytimer;
extern std::string g_szMessageTitle;
extern std::string g_szMessageLine[3];
extern std::vector<std::unique_ptr<WorldVehicle>> vehiclelist;
extern std::vector<std::unique_ptr<WorldWarp>> warplist;
extern bool g_fFullScreen;
extern bool g_fShowStagePreviews;
extern UI_Menu * mCurrentMenu;
extern UI_Menu mStageSettingsMenu;
extern UI_Menu mBonusItemPicker;
extern MI_ImageSelectField * miModeField;
extern MI_SelectField<short> * miGoalField[GAMEMODE_LAST];
extern MI_Button * miModeSettingsButton;
extern MI_Button * miBonusItemsButton;
extern MI_SelectField<short> * miSpecialGoalField[3];
extern MI_SelectField<bool> * miFinalStageField;
extern MI_SelectField<short> * miPointsField;
extern MI_TextField * miNameField;
extern MI_SelectField<short> * miBonusType;
extern MI_TextField * miBonusTextField[5];
extern MI_MapField * miMapField;
extern MI_Button * miDeleteStageButton;
extern MI_Image * miDeleteStageDialogImage;
extern MI_Text * miDeleteStageDialogAreYouText;
extern MI_Text * miDeleteStageDialogSureText;
extern MI_Button * miDeleteStageDialogYesButton;
extern MI_Button * miDeleteStageDialogNoButton;
extern UI_Menu mVehicleMenu;
extern MI_ImageSelectField * miVehicleSpriteField;
extern MI_ImageSelectField * miVehicleStageField;
extern MI_SelectField<short> * miVehicleMinMovesField;
extern MI_SelectField<short> * miVehicleMaxMovesField;
extern MI_SelectField<bool> * miVehiclePacesField;
extern MI_SelectField<short> * miVehicleDirectionField;
extern MI_SelectField<short> * miVehicleBoundaryField;
extern MI_Button * miVehicleCreateButton;
extern MI_Text * miTitleText;
extern UI_ModeOptionsMenu* mModeOptionsMenu;
extern gfxSprite sMapThumbnail;
extern short iOldStageId;
extern int g_iNumGameModeSettings[GAMEMODE_LAST];
extern SDL_Rect rItemDst[NUM_WORLD_ITEMS];

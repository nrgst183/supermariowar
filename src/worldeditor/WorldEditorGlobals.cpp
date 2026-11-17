#include "WorldEditorState.h"

#ifndef SMW_EMBEDDED_EDITOR  // the game's own (EditorLauncher.h)
SDL_Surface		*screen;
SDL_Surface		*blitdest;
#endif
gfxSprite		sMapSurface;

SDL_Rect		rectSrcSurface = {0, 0, 768, 608};
SDL_Rect		rectDstSurface = {0, 0, 640, 480};
bool			fNeedBlackBackground = false;
short			iWorldWidth, iWorldHeight;

SDL_Event		event;

gfxSprite		spr_dialog;
gfxSprite		menu_shade;
gfxSprite		spr_largedialog;

gfxSprite		spr_warps[3];
gfxSprite		spr_path;

gfxSprite		spr_vehicleicons;

int				set_tile = 0;
bool			fAutoPaint = true;

int				edit_mode = 0;

int				draw_offset_col = 0;  //col and row offset for drawing map to surface
int				draw_offset_row = 0;
int				draw_offset_x = 0;  //x and y offset for drawing maps smaller than screensize
int				draw_offset_y = 0;

int				state;
bool			selectedtiles[MAPWIDTH][MAPHEIGHT];
bool			moveselectedtiles[MAPWIDTH][MAPHEIGHT];
WorldMapTile	copiedtiles[MAPWIDTH][MAPHEIGHT];

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

//Vehicle structure that holds the current vehicle "stamp"
WorldVehicle g_wvVehicleStamp;

//// Global stuff that the map editor doesn't need, but has references to
gfxSprite		spr_warplock;
#ifndef SMW_EMBEDDED_EDITOR  // stand-ins for the game's code and the game's own globals (EditorLauncher.h)
short			x_shake = 0;
short			y_shake = 0;

void CPlayer::flipsidesifneeded() {}
bool CPlayer::isInvincible() const { return false; }
bool CPlayer::isShielded() const { return false; }
void CPlayer::AddKillerAward(CPlayer*, KillStyle) {}
PlayerKillType CPlayer::KillPlayerMapHazard(bool fForce, KillStyle style, bool fKillCarriedItem, short iPlayerId)
{
    return PlayerKillType::None;
}
bool PlayerCollisions::checktop(CPlayer&)
{
    return false;
}
bool CPlayer::bouncejump()
{
    return false;
}
bool PlayerKuriboShoe::is_on() const { return false; }
bool PlayerTanookiSuit::isStatue() const { return false; }

void B_WeaponBreakableBlock::triggerBehavior(short iPlayerID, short iTeamID) {}

//void IO_MovingObject::flipsidesifneeded() {}
//void IO_MovingObject::KillObjectMapHazard(short playerID) {}

void CO_Egg::placeEgg() {}
void CO_Flag::placeFlag() {}
void CO_PhantoKey::placeKey() {}
void CO_Star::placeStar() {}
void MO_Coin::placeCoin() {}
void OMO_Area::placeArea() {}
void OMO_Area::reset() {}
void OMO_Area::setOwner(CPlayer * player) {}

bool CO_ThrowBox::HasKillVelocity()
{
    return false;
}

PlayerKillType PlayerKilledPlayer(short id, CPlayer * killed, PlayerDeathStyle deathstyle, KillStyle style, bool fForce, bool fKillCarriedItem)
{
    return PlayerKillType::None;
}
void AddAwardKill(CPlayer * killer, CPlayer * killed, KillStyle style) {}

CPlayer * GetPlayerFromGlobalID(short id)
{
    return nullptr;
}

void removeifprojectile(IO_MovingObject * object, bool playsound, bool forcedead) {}
bool LoadMenuSkin(short playerID, short skinID, short colorID, bool fLoadBothDirections)
{
    return false;
}

gfxSprite		spr_awardsouls, spr_fireballexplosion;
gfxSprite		spr_backmap[2];
gfxSprite		spr_background;
gfxSprite		spr_explosion;

sfxSound		sfx_boomerang;
sfxSound		sfx_bobombsound;
sfxSound		sfx_bulletbillsound;
sfxSound		sfx_flamecannon;
sfxSound		sfx_hit;
sfxSound		sfx_kicksound;
sfxSound		sfx_mip;
sfxSound		sfx_transform;

gfxSprite		**spr_player[4];
CGameMode		*gamemodes[GAMEMODE_LAST];

gfxSprite		spr_overlay, spr_overlayhole;

void SDLCALL musicfinished(void*, MIX_Track* rawptr){}

CEyecandyContainer eyecandy[3];
gfxSprite		spr_frontmap[2];

SimulationState g_sim;
CPlayer::~CPlayer() {}

short LookupTeamID(short id)
{
    return 0;
}
gfxSprite		spr_scoretext;
gfxSprite		spr_poof;

void CScore::AdjustScore(short iValue) {}
#endif
///////

bool CheckKey(const bool* keystate, SDL_Keycode key) {
    return keystate[SDL_GetScancodeFromKey(key, nullptr)];
}

bool ignoreclick = false;

std::string findstring;

short g_musiccategorydisplaytimer = 0;

short g_messagedisplaytimer = 0;
std::string g_szMessageTitle = "";
std::string g_szMessageLine[3];

//Vehicle stuff
std::vector<std::unique_ptr<WorldVehicle>> vehiclelist;

//Warp stuff
std::vector<std::unique_ptr<WorldWarp>> warplist;

bool g_fFullScreen = false;
bool g_fShowStagePreviews = true;

//Stage Mode Menu
UI_Menu * mCurrentMenu;
UI_Menu mStageSettingsMenu;
UI_Menu mBonusItemPicker;

MI_ImageSelectField * miModeField;
MI_SelectField<short> * miGoalField[GAMEMODE_LAST];
MI_Button * miModeSettingsButton;
MI_Button * miBonusItemsButton;
MI_SelectField<short> * miSpecialGoalField[3];

MI_SelectField<bool> * miFinalStageField;
MI_SelectField<short> * miPointsField;

MI_TextField * miNameField;
MI_SelectField<short> * miBonusType;
MI_TextField * miBonusTextField[5];

MI_MapField * miMapField;

MI_Button * miDeleteStageButton;

MI_Image * miDeleteStageDialogImage;
MI_Text * miDeleteStageDialogAreYouText;
MI_Text * miDeleteStageDialogSureText;
MI_Button * miDeleteStageDialogYesButton;
MI_Button * miDeleteStageDialogNoButton;

//Vehicle Creation Menu
UI_Menu mVehicleMenu;
MI_ImageSelectField * miVehicleSpriteField;
MI_ImageSelectField * miVehicleStageField;
MI_SelectField<short> * miVehicleMinMovesField;
MI_SelectField<short> * miVehicleMaxMovesField;
MI_SelectField<bool> * miVehiclePacesField;
MI_SelectField<short> * miVehicleDirectionField;
MI_SelectField<short> * miVehicleBoundaryField;
MI_Button * miVehicleCreateButton;
MI_Text * miTitleText;

UI_ModeOptionsMenu* mModeOptionsMenu;

gfxSprite sMapThumbnail;
short iOldStageId = -1;

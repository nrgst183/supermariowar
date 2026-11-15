/*----------------------------------------------------------+
| Super Mario War World Editor								|
|															|
|															|
| this sourcecode is released under the GPLv2.				|
|															|
| start:		1.1.2008									|
| last changes:	1.10.2009									|
|															|
|  (C) 2003-2009 Florian Hufsky <florian.hufsky@gmail.com>	|
+----------------------------------------------------------*/

#include "WorldEditorState.h"

#ifndef SMW_EMBEDDED_EDITOR  // standalone program (in the game: EditorLauncher.h)
// Defines the platform entry point (WinMain on Windows), so include it in this file only
#include <SDL3/SDL_main.h>

#ifdef _WIN32
    #ifdef _MSC_VER
    #if _MSC_VER >= 1400
        #include <stdio.h>
        FILE _iob[] = {*stdin, *stdout, *stderr};
        extern "C" FILE * __cdecl __iob_func(void) { return _iob; }
    #endif
    #endif
#endif
#endif


constexpr const char* MAPTITLESTRING = "World Editor";

namespace {

//Sets up default mode options
struct StageModeOption {
	char szName[64];
	short iValue;
};

struct StageMode {
	char szName[128];
	char szGoal[64];

	short iDefaultGoal;

	StageModeOption options[GAMEMODE_NUM_OPTIONS - 1];
};

StageMode stagemodes[GAMEMODE_LAST];

void SetStageMode(short iIndex, const char * szModeName, const char * szGoalName, short iIncrement, short iDefault)
{
	if (iIndex < 0 || iIndex >= GAMEMODE_LAST)
		return;

	StageMode * sm = &stagemodes[iIndex];

	strncpy(sm->szName, szModeName, 128);
	sm->szName[127] = 0;

	strncpy(sm->szGoal, szGoalName, 64);
	sm->szGoal[63] = 0;

	sm->iDefaultGoal = iDefault;

    for (short iModeOption = 0; iModeOption < GAMEMODE_NUM_OPTIONS - 1; iModeOption++) {
		sm->options[iModeOption].iValue = (iModeOption + 1) * iIncrement;
		snprintf(sm->options[iModeOption].szName, sizeof(sm->options[iModeOption].szName), "%d", sm->options[iModeOption].iValue);
	}
}
} // namespace

void inner_main();

#ifndef SMW_EMBEDDED_EDITOR  // the standalone program's entry point
//main main main
int main(int argc, char *argv[])
{
    const cmd::Args cmd = cmd::parse_args(argc, argv);
    if (!cmd.success) {
        return 1;
    }
    if (cmd.show_help) {
        char title[128];
        snprintf(title, sizeof(title), "%s %s", TITLESTRING, MAPTITLESTRING);
        cmd::print_help(title, "");
        return 0;
    }
    if (cmd.debug) {
        cmd::show_windows_console();
    }
    if (!cmd.data_root.empty())
        RootDataDirectory = cmd.data_root;

    try {
        inner_main();
    }
    catch (const char* what) {
        gfx_show_catched_error(what);
        return 1;
    }
    catch (const std::string& ex) {
        gfx_show_catched_error(ex);
        return 1;
    }
    catch (const std::exception& ex) {
        gfx_show_catched_error(ex.what());
        return 1;
    }
    catch (...) {
        gfx_show_catched_error({});
        return 1;
    }

    return 0;
}
#endif

void inner_main()
{
    printf("-------------------------------------------------------------------------------\n");
    printf(" %s %s\n", TITLESTRING, MAPTITLESTRING);
    printf("-------------------------------------------------------------------------------\n");
    printf("\n---------------- startup ----------------\n");

#ifndef SMW_EMBEDDED_EDITOR
    App::registerSdlMetadata(TITLESTRING);
#endif

    ensureSettingsDir();

    /* This must occur before any data files are loaded */
#ifndef SMW_EMBEDDED_EDITOR
    Initialize_Paths();
#endif

	bool done;

    int saved_col = 0, saved_row = 0;
    {
        const std::string options_path(GetHomeDirectory() + "worldeditor.bin");
        BinaryFile editor_settings(options_path, "rb");
        if (editor_settings.is_open()) {
            saved_col = editor_settings.read_i32();
            saved_row = editor_settings.read_i32();
            g_fFullScreen = editor_settings.read_bool();
            findstring = editor_settings.read_string_long(FILEBUFSIZE);
        }
    }

#ifndef SMW_EMBEDDED_EDITOR
	gfx_init(640,480, g_fFullScreen);
#else
    //(a new session: every editor state starts over; the menus get their controls again below)
    state = EDITOR_EDIT;
    for (UI_Menu* menu : {&mStageSettingsMenu, &mBonusItemPicker, &mVehicleMenu}) {
        menu->~UI_Menu();
        new (menu) UI_Menu();
    }
    delete mModeOptionsMenu;
    mModeOptionsMenu = nullptr;
#endif
	blitdest = screen;

        rm = new CResourceManager();

        g_map = new CMap();
#ifndef SMW_EMBEDDED_EDITOR  //(in the game: the game's tilesets, edited live)
        g_tilesetmanager = new CTilesetManager(convertPath("gfx/Classic/tilesets"));
#endif
        filterslist = new FiltersList();
        maplist = new MapList(true);
        menugraphicspacklist = new GraphicsList;
        gamegraphicspacklist = new GraphicsList;
        worldlist = new WorldList;

        game_values.sound = false;
        game_values.music = false;

	char title[128];
	snprintf(title, sizeof(title), "%s %s", TITLESTRING, MAPTITLESTRING);
	gfx_settitle(title);

	game_values.toplayer = true;

	printf("\n---------------- loading graphics ----------------\n");

	spr_warps[0] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp.png")).create();
	spr_warps[1] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp_preview.png")).create();
	spr_warps[2] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp_thumbnail.png")).create();

	spr_path = ImageLoader(convertPath("gfx/leveleditor/leveleditor_world_path.png")).create();

        rm->spr_selectedtile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_selectedtile.png")).withColorKey(colors::BLACK).withAlpha(128).create();

	spr_dialog = ImageLoader(convertPath("gfx/leveleditor/leveleditor_dialog.png")).withAlpha(255).create();
	menu_shade = ImageLoader(convertPath("gfx/leveleditor/leveleditor_shade.png")).withAlpha(128).create();
	spr_largedialog = ImageLoader(convertPath("gfx/leveleditor/leveleditor_platform.png")).withAlpha(255).create();

	rm->menu_font_small = gfxFont(convertPath("gfx/packs/Classic/fonts/font_small.png"));
	rm->menu_font_large = gfxFont(convertPath("gfx/packs/Classic/fonts/font_large.png"));


	printf("\n---------------- load world ----------------\n");

	rm->spr_worldbackground[0] = ImageLoader(convertPath("gfx/packs/Classic/world/world_background.png")).create();
	rm->spr_worldbackground[1] = ImageLoader(convertPath("gfx/packs/Classic/world/preview/world_background.png")).create();
	rm->spr_worldbackground[2] = ImageLoader(convertPath("gfx/packs/Classic/world/thumbnail/world_background.png")).create();

	rm->spr_worldforeground[0] = ImageLoader(convertPath("gfx/packs/Classic/world/world_foreground.png")).create();
	rm->spr_worldforeground[1] = ImageLoader(convertPath("gfx/packs/Classic/world/preview/world_foreground.png")).create();
	rm->spr_worldforeground[2] = ImageLoader(convertPath("gfx/packs/Classic/world/thumbnail/world_foreground.png")).create();

	rm->spr_worldforegroundspecial[0] = ImageLoader(convertPath("gfx/packs/Classic/world/world_foreground_special.png")).create();
	rm->spr_worldforegroundspecial[1] = ImageLoader(convertPath("gfx/packs/Classic/world/preview/world_foreground_special.png")).create();
	rm->spr_worldforegroundspecial[2] = ImageLoader(convertPath("gfx/packs/Classic/world/thumbnail/world_foreground_special.png")).create();

	rm->spr_worldpaths[0] = ImageLoader(convertPath("gfx/packs/Classic/world/world_paths.png")).create();
	rm->spr_worldpaths[1] = ImageLoader(convertPath("gfx/packs/Classic/world/preview/world_paths.png")).create();
	rm->spr_worldpaths[2] = ImageLoader(convertPath("gfx/packs/Classic/world/thumbnail/world_paths.png")).create();

	rm->spr_worldvehicle[0] = ImageLoader(convertPath("gfx/packs/Classic/world/world_vehicles.png")).create();
	rm->spr_worldvehicle[1] = ImageLoader(convertPath("gfx/packs/Classic/world/preview/world_vehicles.png")).create();
	rm->spr_worldvehicle[2] = ImageLoader(convertPath("gfx/packs/Classic/world/thumbnail/world_vehicles.png")).create();

	rm->spr_worlditems = ImageLoader(convertPath("gfx/packs/Classic/world/world_powerups.png")).create();
	rm->spr_worlditempopup = ImageLoader(convertPath("gfx/packs/Classic/world/world_item_popup.png")).create();

	rm->spr_storedpowerupsmall = ImageLoader(convertPath("gfx/packs/Classic/powerups/small.png")).create();
	rm->spr_worlditemssmall = ImageLoader(convertPath("gfx/packs/Classic/world/world_powerupssmall.png")).create();
	rm->spr_worlditemsplace = ImageLoader(convertPath("gfx/packs/Classic/world/world_bonusplace.png")).create();

	rm->menu_dialog = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_dialog.png")).create();

	//Mode Options Menu Gfx
	rm->menu_egg = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/menu_egg.png")).create();
	rm->menu_stomp = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/menu_stomp.png")).create();
	rm->menu_survival = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/menu_survival.png")).create();
	rm->spr_phanto = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/phanto.png")).create();
	rm->menu_plain_field = ImageLoader(convertPath("gfx/leveleditor/menu_plain_field.png")).create();
	rm->menu_slider_bar = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_slider_bar.png")).create();
	rm->spr_selectfield = ImageLoader(convertPath("gfx/leveleditor/menu_selectfield.png")).create();
	rm->menu_verticalarrows = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_vertical_arrows.png")).create();
	rm->spr_storedpoweruplarge = ImageLoader(convertPath("gfx/packs/Classic/powerups/large.png")).create();

	rm->menu_mode_small = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_mode_small.png")).create();
	rm->menu_mode_large = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_mode_large.png")).create();

	spr_vehicleicons = ImageLoader(convertPath("gfx/leveleditor/vehicle_icons.png")).create();

	rm->spr_thumbnail_warps[0] = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_warp_preview.png")).create();
	rm->spr_thumbnail_warps[1] = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_warp_thumbnail.png")).create();

	rm->spr_thumbnail_mapitems[0] = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_mapitems_preview.png")).create();
	rm->spr_thumbnail_mapitems[1] = ImageLoader(convertPath("gfx/packs/Classic/menu/menu_mapitems_thumbnail.png")).create();

	rm->spr_tileanimation[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/tile_animation_preview.png")).create();
	rm->spr_tileanimation[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/tile_animation_thumbnail.png")).create();

	rm->spr_blocks[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/blocks_preview.png")).create();
	rm->spr_blocks[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/blocks_thumbnail.png")).create();

	rm->spr_unknowntile[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/unknown_tile_preview.png")).create();
	rm->spr_unknowntile[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/unknown_tile_thumbnail.png")).create();

	rm->spr_hazard_fireball[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/fireball_preview.png")).create();
	rm->spr_hazard_fireball[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/fireball_thumbnail.png")).create();

	rm->spr_hazard_rotodisc[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/rotodisc_preview.png")).create();
	rm->spr_hazard_rotodisc[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/rotodisc_thumbnail.png")).create();

	rm->spr_hazard_bulletbill[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/bulletbill_preview.png")).create();
	rm->spr_hazard_bulletbill[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/bulletbill_thumbnail.png")).create();

	rm->spr_hazard_flame[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/flame_preview.png")).create();
	rm->spr_hazard_flame[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/flame_thumbnail.png")).create();

	rm->spr_hazard_pirhanaplant[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/pirhanaplant_preview.png")).create();
	rm->spr_hazard_pirhanaplant[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/pirhanaplant_thumbnail.png")).create();

	rm->loadMenuGraphics();

	sMapSurface = gfxSprite::blank(768, 608);

    worldlist->find(findstring);
    game_values.worldindex = worldlist->currentIndex();
	loadcurrentworld();
	findstring.clear();  //clear out the find string so that pressing "f" will give you the find dialog

    if (saved_row >= 0 && saved_row <= iWorldHeight - 15 && saved_col >= 0 && saved_col <= iWorldWidth - 20) {
		draw_offset_row = saved_row;
		draw_offset_col = saved_col;
		updateworldsurface();
	}

	//Setup input for menus
	game_values.inputConfiguration[0][0].iDevice = DEVICE_KEYBOARD_ID;
    for (short iInputState = 0; iInputState < 2; iInputState++) { //for game/menu
        for (short iKey = 0; iKey < NUM_KEYS; iKey++) {
			game_values.inputConfiguration[0][0].inputGameControls[iInputState].keys[iKey] = controlkeys[0][iInputState][0][iKey];
		}
	}

	game_values.playerInput.inputControls[0] = &game_values.inputConfiguration[0][0];

	SetStageMode(0, "Classic", "Lives", 5, 10);
	SetStageMode(1, "Frag", "Kills", 5, 20);
	SetStageMode(2, "Time Limit", "Time", 30, 60);
	SetStageMode(3, "Jail", "Kills", 5, 20);
	SetStageMode(4, "Coin Collection", "Coins", 5, 20);
	SetStageMode(5, "Stomp", "Kills", 10, 10);
	SetStageMode(6, "Yoshi's Eggs", "Eggs", 5, 20);
	SetStageMode(7, "Capture The Flag", "Points", 5, 20);
	SetStageMode(8, "Chicken", "Points", 50, 200);
	SetStageMode(9, "Tag", "Points", 50, 200);
	SetStageMode(10, "Star", "Lives", 1, 5);
	SetStageMode(11, "Domination", "Points", 50, 200);
	SetStageMode(12, "King of the Hill", "Points", 50, 200);
	SetStageMode(13, "Race", "Laps", 2, 10);
	SetStageMode(14, "Owned", "Points", 50, 200);
	SetStageMode(15, "Frenzy", "Kills", 5, 20);
	SetStageMode(16, "Survival", "Lives", 5, 10);
	SetStageMode(17, "Greed", "Coins", 10, 40);
	SetStageMode(18, "Health", "Lives", 1, 5);
	SetStageMode(19, "Card Collection", "Points", 10, 30);
	SetStageMode(20, "Phanto Chase", "Points", 50, 200);
	SetStageMode(21, "Shy Guy Tag", "Points", 50, 200);

	//Setup The Mode Menu
	mCurrentMenu = &mStageSettingsMenu;

	//Name
	miNameField = new MI_TextField(&rm->spr_selectfield, 70, 20, "Name", 500, 120);
	miNameField->SetDisallowedChars(",");

	miModeField = new MI_ImageSelectField(&rm->spr_selectfield, &rm->menu_mode_small, 70, 60, "Mode", 500, 120, 16, 16);
	//miModeField->SetData(game_values.tourstops[0]->iMode, nullptr, nullptr);
	//miModeField->SetKey(0);
	miModeField->setItemChangedCode(MENU_CODE_MODE_CHANGED);

    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
		miModeField->add(stagemodes[iGameMode].szName, iGameMode);

		miGoalField[iGameMode] = new MI_SelectField<short>(&rm->spr_selectfield, 70, 100, stagemodes[iGameMode].szGoal, 352, 120);
		miGoalField[iGameMode]->setVisible(iGameMode == 0);

        for (short iGameModeOption = 0; iGameModeOption < GAMEMODE_NUM_OPTIONS - 1; iGameModeOption++) {
			StageModeOption * option = &stagemodes[iGameMode].options[iGameModeOption];
			miGoalField[iGameMode]->add(option->szName, option->iValue);
		}

		//miGoalField[iGameMode]->SetData(&gamemodes[iGameMode]->goal, nullptr, nullptr);
		//miGoalField[iGameMode]->SetKey(gamemodes[iGameMode]->goal);
	}

	miModeField->add("Bonus House", 24);
	miModeField->add("Pipe Minigame", 25);
	miModeField->add("Boss Minigame", 26);
	miModeField->add("Boxes Minigame", 27);

	//Create goal field for pipe game
	miSpecialGoalField[0] = new MI_SelectField<short>(&rm->spr_selectfield, 70, 100, "Points", 352, 120);
	miSpecialGoalField[0]->setVisible(false);

    for (short iGameModeOption = 0; iGameModeOption < GAMEMODE_NUM_OPTIONS - 1; iGameModeOption++) {
		short iValue = 10 + iGameModeOption * 10;
		char szName[16];
		snprintf(szName, sizeof(szName), "%d", iValue);
		miSpecialGoalField[0]->add(szName, iValue);
	}

	//Create goal field for boss game
	miSpecialGoalField[1] = new MI_SelectField<short>(&rm->spr_selectfield, 70, 100, "Lives", 352, 120);
	miSpecialGoalField[1]->setVisible(false);

    for (short iGameLives = 1; iGameLives <= 30; iGameLives++) {
		char szName[16];
		snprintf(szName, sizeof(szName), "%d", iGameLives);
		miSpecialGoalField[1]->add(szName, iGameLives);
	}

	//Create goal field for boxes game
	miSpecialGoalField[2] = new MI_SelectField<short>(&rm->spr_selectfield, 70, 100, "Lives", 352, 120);
	miSpecialGoalField[2]->setVisible(false);

    for (short iGameLives = 1; iGameLives <= 30; iGameLives++) {
		char szName[16];
		snprintf(szName, sizeof(szName), "%d", iGameLives);
		miSpecialGoalField[2]->add(szName, iGameLives);
	}

	//Mode Settings Button
    miModeSettingsButton = new MI_Button(&rm->spr_selectfield, 430, 100, "Settings", 140);
	miModeSettingsButton->SetCode(MENU_CODE_TO_MODE_SETTINGS_MENU);

	//Points Field
	miPointsField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 140, "Points", 245, 120);
    for (short iPoints = 0; iPoints <= 20; iPoints++) {
		char szPoints[8];
		snprintf(szPoints, sizeof(szPoints), "%d", iPoints);
		miPointsField->add(szPoints, iPoints);
	}

	//Final Stage Field
	miFinalStageField = new MI_SelectField<bool>(&rm->spr_selectfield, 325, 140, "End Stage", 245, 120);
	miFinalStageField->add("No", false);
	miFinalStageField->add("Yes", true);
	miFinalStageField->setAutoAdvance(true);

	//Map Select Field
	miMapField = new MI_MapField(&rm->spr_selectfield, 70, 180, "Map", 500, 120, true);

	//Bonus Item Picker Menu Button
    miBonusItemsButton= new MI_Button(&rm->spr_selectfield, 430, 220, "Bonuses", 140);
	miBonusItemsButton->SetCode(MENU_CODE_TO_BONUS_PICKER_MENU);

	//Bonus Type
	miBonusType = new MI_SelectField<short>(&rm->spr_selectfield, 70, 100, "Type", 500, 120);
	miBonusType->add("Fixed", false);
	miBonusType->add("Random", true);
	miBonusType->setAutoAdvance(true);

	//Bonus House Text * 5
	miBonusTextField[0] = new MI_TextField(&rm->spr_selectfield, 70, 140, "Text", 500, 120);
	miBonusTextField[1] = new MI_TextField(&rm->spr_selectfield, 70, 180, "Text", 500, 120);
	miBonusTextField[2] = new MI_TextField(&rm->spr_selectfield, 70, 220, "Text", 500, 120);
	miBonusTextField[3] = new MI_TextField(&rm->spr_selectfield, 70, 260, "Text", 500, 120);
	miBonusTextField[4] = new MI_TextField(&rm->spr_selectfield, 70, 300, "Text", 500, 120);

	miBonusTextField[0]->SetDisallowedChars(",|");
	miBonusTextField[1]->SetDisallowedChars(",|");
	miBonusTextField[2]->SetDisallowedChars(",|");
	miBonusTextField[3]->SetDisallowedChars(",|");
	miBonusTextField[4]->SetDisallowedChars(",|");

	//Delete Stage Button
    miDeleteStageButton = new MI_Button(&rm->spr_selectfield, 430, 440, "Delete", 140);
	miDeleteStageButton->SetCode(MENU_CODE_DELETE_STAGE_BUTTON);

	//Are You Sure Dialog for Delete Stage
	miDeleteStageDialogImage = new MI_Image(&spr_dialog, 224, 176, 0, 0, 192, 128, 1, 1, 0);
    miDeleteStageDialogAreYouText = new MI_HeaderText("Are You", 320, 195);
    miDeleteStageDialogSureText = new MI_HeaderText("Sure?", 320, 220);
    miDeleteStageDialogYesButton = new MI_Button(&rm->spr_selectfield, 235, 250, "Yes", 80, TextAlign::CENTER);
    miDeleteStageDialogNoButton = new MI_Button(&rm->spr_selectfield, 325, 250, "No", 80, TextAlign::CENTER);

	miDeleteStageDialogYesButton->SetCode(MENU_CODE_DELETE_STAGE_YES);
	miDeleteStageDialogNoButton->SetCode(MENU_CODE_DELETE_STAGE_NO);

	miDeleteStageDialogImage->setVisible(false);
	miDeleteStageDialogAreYouText->setVisible(false);
	miDeleteStageDialogSureText->setVisible(false);
	miDeleteStageDialogYesButton->setVisible(false);
	miDeleteStageDialogNoButton->setVisible(false);

	//Add Name Field
	mStageSettingsMenu.AddControl(miNameField, miDeleteStageButton, miModeField, nullptr, nullptr);

	//Add Mode Field
	mStageSettingsMenu.AddControl(miModeField, miNameField, miGoalField[0], nullptr, nullptr);

	//Add Mode Goal Fields
	mStageSettingsMenu.AddControl(miGoalField[0], miModeField, miGoalField[1], nullptr, miModeSettingsButton);

	for (short iGoalField = 1; iGoalField < GAMEMODE_LAST - 1; iGoalField++)
		mStageSettingsMenu.AddControl(miGoalField[iGoalField], miGoalField[iGoalField - 1], miGoalField[iGoalField + 1], miGoalField[iGoalField - 1], miModeSettingsButton);

	mStageSettingsMenu.AddControl(miGoalField[GAMEMODE_LAST - 1], miGoalField[GAMEMODE_LAST - 2], miSpecialGoalField[0], miGoalField[GAMEMODE_LAST - 2], miModeSettingsButton);

	mStageSettingsMenu.AddControl(miSpecialGoalField[0], miGoalField[GAMEMODE_LAST - 1], miSpecialGoalField[1], miGoalField[GAMEMODE_LAST - 1], miSpecialGoalField[1]);
	mStageSettingsMenu.AddControl(miSpecialGoalField[1], miSpecialGoalField[0], miSpecialGoalField[2], miSpecialGoalField[0], miSpecialGoalField[2]);
	mStageSettingsMenu.AddControl(miSpecialGoalField[2], miSpecialGoalField[1], miPointsField, miSpecialGoalField[1], miModeSettingsButton);

	//Add Mode Settings Button
	mStageSettingsMenu.AddControl(miModeSettingsButton, miModeField, miFinalStageField, miSpecialGoalField[2], nullptr);

	//Add Points Field
	mStageSettingsMenu.AddControl(miPointsField, miSpecialGoalField[2], miMapField, nullptr, miFinalStageField);

	//Add Final Stage Field
	mStageSettingsMenu.AddControl(miFinalStageField, miSpecialGoalField[2], miMapField, miPointsField, nullptr);

	//Add Map Field
	mStageSettingsMenu.AddControl(miMapField, miFinalStageField, miBonusType, nullptr, miBonusItemsButton);

	//Add Bonus House Fields
	mStageSettingsMenu.AddControl(miBonusType, miMapField, miBonusTextField[0], nullptr, nullptr);
	mStageSettingsMenu.AddControl(miBonusTextField[0], miBonusType, miBonusTextField[1], nullptr, nullptr);
	mStageSettingsMenu.AddControl(miBonusTextField[1], miBonusTextField[0], miBonusTextField[2], nullptr, nullptr);
	mStageSettingsMenu.AddControl(miBonusTextField[2], miBonusTextField[1], miBonusTextField[3], nullptr, nullptr);
	mStageSettingsMenu.AddControl(miBonusTextField[3], miBonusTextField[2], miBonusTextField[4], nullptr, nullptr);
	mStageSettingsMenu.AddControl(miBonusTextField[4], miBonusTextField[3], miBonusItemsButton, nullptr, nullptr);

	//Add Bonus Button
	mStageSettingsMenu.AddControl(miBonusItemsButton, miBonusTextField[4], miDeleteStageButton, miMapField, nullptr);

	//Add Delete Stage Button
	mStageSettingsMenu.AddControl(miDeleteStageButton, miBonusItemsButton, miNameField, miMapField, nullptr);

	//Add Are You Sure Dialog
	mStageSettingsMenu.AddNonControl(miDeleteStageDialogImage);
	mStageSettingsMenu.AddNonControl(miDeleteStageDialogAreYouText);
	mStageSettingsMenu.AddNonControl(miDeleteStageDialogSureText);

	mStageSettingsMenu.AddControl(miDeleteStageDialogYesButton, nullptr, nullptr, nullptr, miDeleteStageDialogNoButton);
	mStageSettingsMenu.AddControl(miDeleteStageDialogNoButton, nullptr, nullptr, miDeleteStageDialogYesButton, nullptr);

	mStageSettingsMenu.setInitialFocus(miNameField);
	mStageSettingsMenu.SetCancelCode(MENU_CODE_EXIT_APPLICATION);

	mBonusItemPicker.SetCancelCode(MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS);

	mModeOptionsMenu = new UI_ModeOptionsMenu();

	g_wvVehicleStamp.setDrawSprite(0);
	g_wvVehicleStamp.setActionId(0);
	g_wvVehicleStamp.setCurrentTile(0, 0);
	g_wvVehicleStamp.setMinMoves(5);
	g_wvVehicleStamp.setMaxMoves(8);
	g_wvVehicleStamp.setSpritePacing(true);
	g_wvVehicleStamp.setDrawDirection(0);
	g_wvVehicleStamp.setBoundary(0);

	//Create Vehicle Menu
	miVehicleSpriteField = new MI_ImageSelectField(&rm->spr_selectfield, &spr_vehicleicons, 70, 80, "Sprite", 500, 150, 16, 16);
	miVehicleSpriteField->add("Hammer Brother", 0);
	miVehicleSpriteField->add("Boomerang Brother", 1);
	miVehicleSpriteField->add("Fire Brother", 2);
	miVehicleSpriteField->add("Tank 1", 3);
	miVehicleSpriteField->add("Boat 1", 4);
	miVehicleSpriteField->add("Boat 2", 5);
	miVehicleSpriteField->add("Airship 1", 6);
	miVehicleSpriteField->add("Airship 2", 7);
	miVehicleSpriteField->add("Tank 2", 8);
	miVehicleSpriteField->setOnChanged([](short sprite) { g_wvVehicleStamp.setDrawSprite(sprite); });
	miVehicleSpriteField->setCurrentValue(g_wvVehicleStamp.getDrawSprite());

	miVehicleStageField = new MI_ImageSelectField(&rm->spr_selectfield, &rm->menu_mode_small, 70, 120, "Stage", 500, 150, 16, 16);
	miVehicleStageField->setOnChanged([](short stage) { g_wvVehicleStamp.setActionId(stage); });

	miVehicleMinMovesField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 160, "Min Moves", 500, 150);

    for (short iMinMoves = 0; iMinMoves <= 100; iMinMoves++) {
		char szMinMoves[8];
		snprintf(szMinMoves, sizeof(szMinMoves), "%d", iMinMoves);
		miVehicleMinMovesField->add(szMinMoves, iMinMoves);
	}

	miVehicleMinMovesField->setOnChanged([](short minmoves) { g_wvVehicleStamp.setMinMoves(minmoves); });
	miVehicleMinMovesField->setCurrentValue(g_wvVehicleStamp.getMinMoves());
	miVehicleMinMovesField->setItemChangedCode(MENU_CODE_VEHICLE_MIN_MOVES_CHANGED);
	miVehicleMinMovesField->allowWrap(false);
	miVehicleMinMovesField->allowFastScroll(true);

	miVehicleMaxMovesField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 200, "Max Moves", 500, 150);

    for (short iMaxMoves = 0; iMaxMoves <= 100; iMaxMoves++) {
		char szMaxMoves[8];
		snprintf(szMaxMoves, sizeof(szMaxMoves), "%d", iMaxMoves);
		miVehicleMaxMovesField->add(szMaxMoves, iMaxMoves);
	}

	miVehicleMaxMovesField->setOnChanged([](short maxmoves) { g_wvVehicleStamp.setMaxMoves(maxmoves); });
	miVehicleMaxMovesField->setCurrentValue(g_wvVehicleStamp.getMaxMoves());
	miVehicleMaxMovesField->setItemChangedCode(MENU_CODE_VEHICLE_MAX_MOVES_CHANGED);
	miVehicleMaxMovesField->allowWrap(false);
	miVehicleMaxMovesField->allowFastScroll(true);

	miVehiclePacesField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 240, "Paces", 500, 150);
	miVehiclePacesField->add("No", false);
	miVehiclePacesField->add("Yes", true);
	miVehiclePacesField->setOnChanged([](bool paces) { g_wvVehicleStamp.setSpritePacing(paces); });
	miVehiclePacesField->setCurrentValue(g_wvVehicleStamp.isSpritePacing() ? 1 : 0);
	miVehiclePacesField->setAutoAdvance(true);

	miVehicleDirectionField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 280, "Direction", 500, 150);
	miVehicleDirectionField->add("Left", false);
	miVehicleDirectionField->add("Right", true);
	miVehicleDirectionField->setOnChanged([](short direction) { g_wvVehicleStamp.setDrawDirection(direction); });
	miVehicleDirectionField->setCurrentValue(g_wvVehicleStamp.getDrawDirection());
	miVehicleDirectionField->setAutoAdvance(true);

	miVehicleBoundaryField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 320, "Boundary", 500, 150);
	miVehicleBoundaryField->add("No Boundary", 0);

    for (short iBoundary = 1; iBoundary <= 100; iBoundary++) {
		char szBoundary[8];
		snprintf(szBoundary, sizeof(szBoundary), "%d", iBoundary);
		miVehicleBoundaryField->add(szBoundary, iBoundary);
	}

	miVehicleBoundaryField->setOnChanged([](short boundary) { g_wvVehicleStamp.setBoundary(boundary); });
	miVehicleBoundaryField->setCurrentValue(g_wvVehicleStamp.getBoundary());
	miVehicleBoundaryField->allowFastScroll(true);

    miVehicleCreateButton = new MI_Button(&rm->spr_selectfield, 430, 360, "OK", 140, TextAlign::CENTER);
	miVehicleCreateButton->SetCode(MENU_CODE_CREATE_VEHICLE);

    miTitleText = new MI_Text("Clicking on the map will add the vehicle configured below", 320, 50, 640, true, TextAlign::CENTER);

	mVehicleMenu.AddNonControl(miTitleText);

	mVehicleMenu.AddControl(miVehicleSpriteField, miVehicleCreateButton, miVehicleStageField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleStageField, miVehicleSpriteField, miVehicleMinMovesField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleMinMovesField, miVehicleStageField, miVehicleMaxMovesField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleMaxMovesField, miVehicleMinMovesField, miVehiclePacesField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehiclePacesField, miVehicleMaxMovesField, miVehicleDirectionField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleDirectionField, miVehiclePacesField, miVehicleBoundaryField, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleBoundaryField, miVehicleDirectionField, miVehicleCreateButton, nullptr, nullptr);
	mVehicleMenu.AddControl(miVehicleCreateButton, miVehicleBoundaryField, miVehicleSpriteField, nullptr, nullptr);

	mVehicleMenu.setInitialFocus(miVehicleSpriteField);
	mVehicleMenu.SetCancelCode(MENU_CODE_EXIT_APPLICATION);

	printf("\n---------------- ready, steady, go! ----------------\n");

	printf("entering world editor loop...\n");
	done = false;
    while (!done) {
        switch (state) {
			case EDITOR_EDIT:
				state = editor_edit();
			break;

			case EDITOR_WATER:
				state = editor_water();
			break;

			case EDITOR_BACKGROUND:
				state = editor_background();
			break;

			case EDITOR_STAGEFOREGROUND:
				state = editor_stageforeground();
			break;

			case EDITOR_STRUCTUREFOREGROUND:
				state = editor_structureforeground();
			break;

			case EDITOR_BRIDGES:
				state = editor_bridges();
			break;

			case EDITOR_VEHICLES:
				state = editor_vehicles();
			break;

			case EDITOR_PATH:
				state = editor_path();
			break;

			case EDITOR_PATHSPRITE:
				state = editor_pathsprite();
			break;

			case EDITOR_WARP:
				state = editor_warp();
			break;

			case EDITOR_START_ITEMS:
				state = editor_start_items();
			break;

			case EDITOR_BOUNDARY:
				state = editor_boundary();
			break;

			case EDITOR_TYPE:
				state = editor_type();
			break;

			case EDITOR_STAGE:
				state = editor_stage();
			break;

			case EDITOR_QUIT:
				done = true;
			break;

			case DISPLAY_HELP:
				state = display_help();
			break;

			case SAVE_AS:
				state = save_as();
			break;

			case FIND:
				state = find();
			break;

			case CLEAR_WORLD:
				state = clear_world();
			break;

			case NEW_WORLD:
				state = new_world();
			break;

			case RESIZE_WORLD:
				state = resize_world();
			break;

			case SAVE:
				state = savecurrentworld();
			break;

			default:
				printf(" PANIC: WEIRD GAMESTATE: %d\n", state);
			break;
		}
	}

	printf("\n---------------- save world ----------------\n");

	WriteVehiclesIntoWorld();
	WriteWarpsIntoWorld();
	saveWorldMap(g_worldmap, convertPath("worlds/ZZworldeditor.txt"));

    {
        const std::string options_path(GetHomeDirectory() + "worldeditor.bin");
        BinaryFile editor_settings(options_path, "wb");
        if (editor_settings.is_open()) {
            editor_settings.write_i32(draw_offset_col);
            editor_settings.write_i32(draw_offset_row);
            editor_settings.write_bool(g_fFullScreen);
            editor_settings.write_string_long(worldlist->currentPath().string().c_str());
        }
    }

	printf("\n---------------- shutdown ----------------\n");
}

/*----------------------------------------------------------+
| Super Mario War Leveleditor								|
|															|
| based on the jnrdev #1/#2 level editor					|
|															|
| ...a big bunch of crappy code ;)							|
| ...with some improvements									|
|															|
|															|
| this sourcecode is released under the GPLv2.				|
|															|
| start:		18.12.2003									|
| last changes:	 3.12.2007									|
|															|
|  (C) 2003-2009 Florian Hufsky <florian.hufsky@gmail.com>	|
+----------------------------------------------------------*/

#include "LevelEditorState.h"

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

constexpr const char* MAPTITLESTRING = "Level Editor";

#ifdef __EMSCRIPTEN__
void gameloop_frame();
#endif

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
    if (!cmd.data_root.empty()) {
        RootDataDirectory = cmd.data_root;
    }

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

void editorReleaseBackgrounds();
void editorReleaseMapPicture();

// The session's own graphics freed (the Xbox, at a session's end: the game needs the memory; the
// next session loads them again, its graphics not kept there). (leveleditor:: in the game: the
// editor's files are compiled in that namespace, EmbeddedLevelEditor.cpp)
void releaseSessionGraphics()
{
    editorReleaseBackgrounds();
    editorReleaseMapPicture();
    s_platform = gfxSprite();
    s_platformpathbuttons = gfxSprite();
    s_maphazardbuttons = gfxSprite();
}

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

    {
        const std::string options_path(GetHomeDirectory() + "leveleditor.bin");
        BinaryFile editor_settings(options_path, "rb");
        if (editor_settings.is_open()) {
            g_fFullScreen = editor_settings.read_bool();
            findstring = editor_settings.read_string_long(FILEBUFSIZE);
        }
    }

#ifndef SMW_EMBEDDED_EDITOR
    gfx_init(640,480, g_fFullScreen);
#else
    //(a new session: every editor state starts over, the modes' too)
    state = EDITOR_EDIT;
    editor_edit_initialized = false;
    editor_tiles_initialized = false;
    editor_properties_initialized = false;
    editor_animation_initialized = false;
    editor_backgrounds_initialized = false;
    editor_maphazards_initialized = false;
    editor_modeitems_initialized = false;
    editor_platforms_initialized = false;
#endif
    blitdest = screen;

#ifdef SMW_EMBEDDED_EDITOR
        //(the graphics of the last session, kept while the game was in its menus: none to load)
        CResourceManager* const keptGraphics = editors::takeKeptGraphics(editors::Editor::Level);
        const bool graphicsLoaded = keptGraphics != nullptr;
        rm = graphicsLoaded ? keptGraphics : new CResourceManager();
#else
        const bool graphicsLoaded = false;
        rm = new CResourceManager();
#endif
#ifndef SMW_EMBEDDED_EDITOR  //(in the game: the game's tilesets, edited live)
        g_tilesetmanager = new CTilesetManager(convertPath("gfx/Classic/tilesets"));
#endif
        g_map = new CMap();
        filterslist = new FiltersList();
        maplist = new MapList(false);
        backgroundlist = new BackgroundList();

    //Add all of the maps that are world only so we can edit them
    maplist->addWorldMaps();
    char title[128];
    snprintf(title, sizeof(title), "%s %s", TITLESTRING, MAPTITLESTRING);
    gfx_settitle(title);

    if (!graphicsLoaded) {
    printf("\n---------------- loading graphics ----------------\n");

    rm->spr_tiletypes = ImageLoader(convertPath("gfx/leveleditor/leveleditor_tile_types.png")).withoutColorKey().create();
    rm->spr_transparenttiles = ImageLoader(convertPath("gfx/leveleditor/leveleditor_transparent_tiles.png")).withAlpha(160).create();

    rm->spr_backgroundlevel = ImageLoader(convertPath("gfx/leveleditor/leveleditor_background_levels.png")).create();
    rm->spr_tilesetlevel = ImageLoader(convertPath("gfx/leveleditor/leveleditor_tileset_levels.png")).create();

        rm->spr_eyecandy = ImageLoader(convertPath("gfx/leveleditor/leveleditor_eyecandy.png")).create();

        s_platform = ImageLoader(convertPath("gfx/leveleditor/leveleditor_platform.png")).create();
        s_platformpathbuttons = ImageLoader(convertPath("gfx/leveleditor/leveleditor_pathtype_buttons.png")).create();
        s_maphazardbuttons = ImageLoader(convertPath("gfx/leveleditor/leveleditor_maphazard_buttons.png")).create();

    rm->spr_warps[0] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp.png")).create();
    rm->spr_warps[1] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp_preview.png")).create();
    rm->spr_warps[2] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_warp_thumbnail.png")).create();

    rm->spr_platformpath = ImageLoader(convertPath("gfx/leveleditor/leveleditor_platform_path.png")).withAlpha(128).create();

    rm->spr_selectedtile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_selectedtile.png")).withColorKey(colors::BLACK).withAlpha(128).create();
    rm->spr_nospawntile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_nospawntile.png")).withColorKey(colors::BLACK).withAlpha(128).create();
    rm->spr_noitemspawntile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_noitemspawntile.png")).withColorKey(colors::BLACK).withAlpha(128).create();
    rm->spr_platformstarttile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_platformstarttile.png")).withColorKey(colors::BLACK).withAlpha(64).withWrapping().create();
    rm->spr_platformendtile = ImageLoader(convertPath("gfx/leveleditor/leveleditor_selectedtile.png")).withColorKey(colors::BLACK).withAlpha(64).withWrapping().create();

    rm->spr_mapitems[0] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_mapitems.png")).create();
    rm->spr_mapitems[1] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_mapitems_preview.png")).create();
    rm->spr_mapitems[2] = ImageLoader(convertPath("gfx/leveleditor/leveleditor_mapitems_thumbnail.png")).create();

    rm->spr_dialog = ImageLoader(convertPath("gfx/leveleditor/leveleditor_dialog.png")).withAlpha(255).create();
    rm->menu_shade = ImageLoader(convertPath("gfx/leveleditor/leveleditor_shade.png")).withAlpha(128).withChangingAlpha().create();

    rm->spr_tileanimation[0] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/tile_animation.png")).create();
    rm->spr_tileanimation[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/tile_animation_preview.png")).create();
    rm->spr_tileanimation[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/tile_animation_thumbnail.png")).create();

    rm->spr_blocks[0] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/blocks.png")).create();
    rm->spr_blocks[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/blocks_preview.png")).create();
    rm->spr_blocks[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/blocks_thumbnail.png")).create();

    rm->spr_unknowntile[0] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/unknown_tile.png")).create();
    rm->spr_unknowntile[1] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/unknown_tile_preview.png")).create();
    rm->spr_unknowntile[2] = ImageLoader(convertPath("gfx/packs/Classic/tilesets/unknown_tile_thumbnail.png")).create();

    rm->spr_powerups = ImageLoader(convertPath("gfx/packs/Classic/powerups/large.png")).create();
    rm->spr_powerupselector = ImageLoader(convertPath("gfx/leveleditor/leveleditor_powerup_selector.png")).withAlpha(128).create();
    rm->spr_hidden_marker = ImageLoader(convertPath("gfx/leveleditor/leveleditor_hidden_marker.png")).create();

    rm->spr_flagbases = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/flagbases.png")).create();
    rm->spr_racegoals = ImageLoader(convertPath("gfx/packs/Classic/modeobjects/racegoal.png")).create();

    rm->spr_hazard_fireball[0] = ImageLoader(convertPath("gfx/packs/Classic/hazards/fireball.png")).create();
    rm->spr_hazard_fireball[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/fireball_preview.png")).create();
    rm->spr_hazard_fireball[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/fireball_thumbnail.png")).create();

    rm->spr_hazard_rotodisc[0] = ImageLoader(convertPath("gfx/packs/Classic/hazards/rotodisc.png")).create();
    rm->spr_hazard_rotodisc[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/rotodisc_preview.png")).create();
    rm->spr_hazard_rotodisc[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/rotodisc_thumbnail.png")).create();

    rm->spr_hazard_bulletbill[0] = ImageLoader(convertPath("gfx/packs/Classic/hazards/bulletbill.png")).create();
    rm->spr_hazard_bulletbill[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/bulletbill_preview.png")).create();
    rm->spr_hazard_bulletbill[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/bulletbill_thumbnail.png")).create();

    rm->spr_hazard_flame[0] = ImageLoader(convertPath("gfx/packs/Classic/hazards/flame.png")).create();
    rm->spr_hazard_flame[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/flame_preview.png")).create();
    rm->spr_hazard_flame[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/flame_thumbnail.png")).create();

    rm->spr_hazard_pirhanaplant[0] = ImageLoader(convertPath("gfx/packs/Classic/hazards/pirhanaplant.png")).create();
    rm->spr_hazard_pirhanaplant[1] = ImageLoader(convertPath("gfx/packs/Classic/hazards/pirhanaplant_preview.png")).create();
    rm->spr_hazard_pirhanaplant[2] = ImageLoader(convertPath("gfx/packs/Classic/hazards/pirhanaplant_thumbnail.png")).create();

    rm->spr_number_icons = ImageLoader(convertPath("gfx/packs/Classic/awards/killsinrownumbers.png")).create();

    for (short i = 0; i < 3; i++) {
        rm->spr_hazard_fireball[i].setWrap(640 >> i);
        rm->spr_hazard_rotodisc[i].setWrap(640 >> i);
        rm->spr_hazard_flame[i].setWrap(640 >> i);
        rm->spr_hazard_pirhanaplant[i].setWrap(640 >> i);
    }

    rm->menu_font_small = gfxFont(convertPath("gfx/packs/Classic/fonts/font_small.png"));
    rm->menu_font_large = gfxFont(convertPath("gfx/packs/Classic/fonts/font_large.png"));
    }    //(graphicsLoaded)

    printf("\n---------------- load map ----------------\n");

    //Setup Platforms
    constexpr int UI_PLATFORM_START_X = (640 - UI_PLATFORM_COLS * 32 - (UI_PLATFORM_COLS - 1) * 10 /* spacing */) / 2;
    constexpr int UI_PLATFORM_START_Y = (480 - UI_PLATFORM_ROWS * 32 - (UI_PLATFORM_ROWS - 1) * 10 /* spacing */) / 2;
    for (short iPlatform = 0; iPlatform < MAX_PLATFORMS; iPlatform++) {
        g_Platforms[iPlatform].rIcon[0].x = (iPlatform % 8) * 32;
        g_Platforms[iPlatform].rIcon[0].y = (iPlatform / 8) * 32 + 224;
        g_Platforms[iPlatform].rIcon[0].w = 32;
        g_Platforms[iPlatform].rIcon[0].h = 32;

        g_Platforms[iPlatform].rIcon[1].x = (iPlatform % 8) * 42 + UI_PLATFORM_START_X;
        g_Platforms[iPlatform].rIcon[1].y = (iPlatform / 8) * 42 + UI_PLATFORM_START_Y;
        g_Platforms[iPlatform].rIcon[1].w = 32;
        g_Platforms[iPlatform].rIcon[1].h = 32;

        for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
            for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
                ClearTilesetTile(&g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow]);
                g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow] = TileType::NonSolid;
            }
        }

        g_Platforms[iPlatform].iVelocity = 4;
        g_Platforms[iPlatform].iDrawLayer = 2;  //Default to drawing in the middle layer
    }

    ReadAnimatedTileTypeFile(convertPath("gfx/packs/Classic/tilesets/tile_animation_tileset.tls").c_str());

    maplist->find(findstring);
    loadcurrentmap();
        findstring.clear();  //clear out the find string so that pressing "f" will give you the find dialog

    printf("\n---------------- ready, steady, go! ----------------\n");

    resetselectedtiles();

#ifndef SMW_EMBEDDED_EDITOR  //(the game's settings are set up)
    game_values.init(); // Needed for FPSLimiter
#endif

    printf("entering level editor loop...\n");
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(gameloop_frame, 0, 1);
}

void gameloop_frame()
#else
    done = false;
    while (!done)
#endif
    {
        FPSLimiter::instance().frameStart();

        switch (state) {
            case EDITOR_EDIT:
                move_nodrag = false;
                state = editor_edit();
            break;

            case EDITOR_TILES:
                state = editor_tiles();
            break;

            case EDITOR_BLOCKS:
                state = editor_blocks();
            break;

            case EDITOR_MAPITEMS:
                state = editor_mapitems();
            break;

            case EDITOR_MODEITEMS:
                state = editor_modeitems();
            break;

            case EDITOR_WARP:
                state = editor_warp();
            break;

            case EDITOR_EYECANDY:
                state = editor_eyecandy();
            break;

            case EDITOR_PLATFORM:
                state = editor_platforms();
            break;

            case EDITOR_MAPHAZARDS:
                state = editor_maphazards();
            break;

            case EDITOR_TILETYPE:
                state = editor_tiletype();
            break;

            case EDITOR_BACKGROUNDS:
                state = editor_backgrounds();
            break;

            case EDITOR_ANIMATION:
                state = editor_animation();
            break;

            case EDITOR_QUIT:
            #ifndef __EMSCRIPTEN__
                done = true;
            #endif
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

            case CLEAR_MAP:
                state = clearMap();
            break;

            case NEW_MAP:
                state = newmap();
            break;

            case SAVE:
                state = savecurrentmap();
            break;

            default:
                printf(" PANIC: WEIRD STATE: %d\n", state);
            break;
        }

        FPSLimiter::instance().beforeFlip();
        gfx_flipscreen();
        FPSLimiter::instance().afterFlip();

#ifndef __EMSCRIPTEN__
    }

    printf("\n---------------- save map ----------------\n");

    save_map(writablePath(convertPath("maps/ZZleveleditor.map")));

    const std::string options_path(GetHomeDirectory() + "leveleditor.bin");
    BinaryFile editor_settings(options_path, "wb");
    if (editor_settings.is_open()) {
        editor_settings.write_bool(g_fFullScreen);
        editor_settings.write_string_long(maplist->currentFilename());
    }

    WriteAnimatedTileTypeFile(convertPath("gfx/packs/Classic/tilesets/tile_animation_tileset.tls").c_str());
    g_tilesetmanager->saveTilesets();

    printf("\n---------------- shutdown ----------------\n");
#endif
}

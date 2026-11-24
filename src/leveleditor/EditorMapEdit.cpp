#include "LevelEditorState.h"

#include <algorithm>

TileType CalculateTileType(short x, short y)
{
    TileType type = TileType::NonSolid;
    for (short k = MAPLAYERS - 1; k >= 0; k--) {
        TilesetTile * tile = &g_map->mapdata[x][y][k];

        TileType iTileType = TileType::NonSolid;
        if (tile->iID >= 0)
                    iTileType = g_tilesetmanager->tileset(tile->iID)->tileType(tile->iCol, tile->iRow);
        else if (tile->iID == TILESETANIMATED)
            iTileType = animatedtiletypes[tile->iRow + (tile->iCol << 5)];

        if (iTileType != TileType::NonSolid) {
            type = iTileType;
            break;
        }
    }

    return type;
}

void UpdateTileType(short x, short y)
{
    g_map->mapdatatop[x][y] = CalculateTileType(x, y);
}

bool TileTypeIsModified(short x, short y)
{
    return g_map->mapdatatop[x][y] != CalculateTileType(x, y);
}

void AdjustMapItems(short x, short y)
{
    constexpr std::array<TileType, 3> IGNORED_TILES {
        TileType::NonSolid,
        TileType::SolidOnTop,
        TileType::IceOnTop,
    };

    const auto pred = [x, y](const MapItem& item){ return item.ix == x && item.iy == y; };
    const auto it = std::find_if(g_map->mapitems.begin(), g_map->mapitems.end(), pred);
    if (it != g_map->mapitems.end()) {
        const TileType tiletype = g_map->mapdatatop[x][y];
        const short objtype = g_map->objectdata[x][y].iType;

        const bool tiletypeValid = std::find(IGNORED_TILES.cbegin(), IGNORED_TILES.cend(), tiletype) == IGNORED_TILES.cend();
        const bool objtypeValid = objtype != -1;
        if (tiletypeValid || objtypeValid) {
            g_map->mapitems.erase(it);
        }
    }
}

/// Removes map items located at the XY position.
void RemoveMapItemAt(short x, short y)
{
    const auto pred = [x, y](const MapItem& item){ return item.ix == x && item.iy == y; };
    const auto it = std::remove_if(g_map->mapitems.begin(), g_map->mapitems.end(), pred);
    g_map->mapitems.erase(it, g_map->mapitems.end());
}

namespace {
bool fExiting = false;
bool fSelectedYes = false;

bool editor_edit_initialized = false;
void init_editor_edit()
{
    if (editor_edit_initialized)
        return;

    fExiting = false;
    fSelectedYes = false;
    g_musiccategorydisplaytimer = 0;

    editor_edit_initialized = true;
}
} // namespace

int editor_edit()
{
    init_editor_edit();

        if (fExiting) {
            //handle messages
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        //A click on Yes or No (the boxes drawn below)
                        if (event.button.button == SDL_BUTTON_LEFT && event.button.y >= 250 && event.button.y < 282) {
                            if (event.button.x >= 250 && event.button.x < 314)
                                return EDITOR_QUIT;
                            if (event.button.x >= 326 && event.button.x < 390)
                                fExiting = false;
                        }
                        break;

                    case SDL_EVENT_KEY_DOWN: {
                        const SDL_Keycode key = event.key.key;

                        if (key == SDLK_LEFT) {
                            fSelectedYes = true;
                        } else if (key == SDLK_RIGHT) {
                            fSelectedYes = false;
                        } else if (event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_RETURN) {
                            if (fSelectedYes)
                                return EDITOR_QUIT;

                            fExiting = false;
                        }
                #ifdef _DEBUG
                        else if (event.key.key == SDLK_ESCAPE) {
                            return EDITOR_QUIT;
                        }
                #elif defined(SMW_EMBEDDED_EDITOR)
                        //(in the game: Escape and the controller's B are No)
                        else if (event.key.key == SDLK_ESCAPE) {
                            fExiting = false;
                        }
                #endif
                    }
                }
            }
        } else {
            //handle messages
            while (SDL_PollEvent(&event)) {
                const bool* keystate = SDL_GetKeyboardState(nullptr);

                switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

                case SDL_EVENT_KEY_DOWN: {
                    const SDL_Keycode key = event.key.key;

                    if (key == SDLK_ESCAPE) {
                            if (g_musiccategorydisplaytimer > 0)
                                g_musiccategorydisplaytimer = 0;
#ifndef SMW_EMBEDDED_EDITOR
                            //(in the game, Escape is the exit prompt from any mode: no detour through tiles)
                            else if (edit_mode != 1)
                                edit_mode = 1;
#endif
                        else {
                                fSelectedYes = false;
                                fExiting = true;
                            }
                        }

                    if (event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                        if (event.key.key == SDLK_RETURN) {
                                g_fFullScreen = !g_fFullScreen;
                                gfx_changefullscreen(g_fFullScreen);
                                blitdest = screen;
                            }
                        }

                    if (key >= SDLK_1 && key <= SDLK_4) {
                        if (edit_mode == 4) { //no spawn zones
                                nospawn_mode = key - SDLK_1 + 1;
                            }
                        }

                    if (key == SDLK_INSERT)
                        takescreenshot();

                    if (key == SDLK_T) {
                        editor_edit_initialized = false;
                        return EDITOR_TILES;
                    }

                    if (key == SDLK_I) {
                        if (edit_mode == 4) {
                                for (short iRow = 0; iRow < MAPHEIGHT; iRow++)
                                    for (short iCol = 0; iCol < MAPWIDTH; iCol++)
                                        SetNoSpawn(nospawn_mode, iCol, iRow, !g_map->nospawn[nospawn_mode][iCol][iRow]);
                        } else {
                            editor_edit_initialized = false;
                            return EDITOR_BLOCKS;
                        }
                    }

                    if (key == SDLK_A) {
                        if (edit_mode == 4) {
                                for (short iRow = 0; iRow < MAPHEIGHT; iRow++)
                                    for (short iCol = 0; iCol < MAPWIDTH; iCol++)
                                        SetNoSpawn(nospawn_mode, iCol, iRow, true);
                        } else {
                                editor_edit_initialized = false;
                                return EDITOR_ANIMATION;
                            }
                        }

                        if (key == SDLK_O) {
                            editor_edit_initialized = false;
                            return EDITOR_MAPITEMS;
                        }

                        if (key == SDLK_J) {
                            editor_edit_initialized = false;
                            return EDITOR_MODEITEMS;
                        }

                        if (key == SDLK_H) {
                            editor_edit_initialized = false;
                            return EDITOR_MAPHAZARDS;
                        }

                    if (key == SDLK_K) {
                            int iMouseX, iMouseY;
                            iMouseX = mouse_x / TILESIZE;
                            iMouseY = mouse_y / TILESIZE;

                            short iType = g_map->objectdata[iMouseX][iMouseY].iType;
                        if (iType == 1 || iType == 15 || iType == 4 || iType == 5 || iType == 17 || iType == 18 || iType == 3) {
                                editor_properties(iMouseX, iMouseY);
                            }
                        }

                        //if 'B' is pressed, rotate backgrounds
                        if (key == SDLK_B) {
                            editor_edit_initialized = false;
                            return EDITOR_BACKGROUNDS;
                        }

                    if (key == SDLK_G) {
                        backgroundlist->next();

                        rm->spr_background = ImageLoader(backgroundlist->currentPath()).withoutColorKey().create();
                        g_map->szBackgroundFile = backgroundlist->currentPath().filename().string();

                        if (!CheckKey(keystate, SDLK_LSHIFT) && !CheckKey(keystate, SDLK_RSHIFT)) {
                                //Set music to background default
                            for (short iCategory = 0; iCategory < MAXMUSICCATEGORY; iCategory++) {
                                if (!strncmp(g_szMusicCategoryNames[iCategory], g_map->szBackgroundFile.c_str(), strlen(g_szMusicCategoryNames[iCategory]))) {
                                        g_map->musicCategoryID = iCategory;
                                        break;
                                    }
                                }
                            }
                        }

                    if (key == SDLK_R) {
                            if (g_musiccategorydisplaytimer > 0 && ++g_map->musicCategoryID >= MAXMUSICCATEGORY)
                                g_map->musicCategoryID = 0;

                            g_musiccategorydisplaytimer = 90;
                        }

                    if (key == SDLK_S ) {
                            if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT))
                                return SAVE_AS;

                            return SAVE;
                        }

#ifdef SMW_EMBEDDED_EDITOR
                    //Play this map: the editor closes (saving it to its scratch file, as always) and the
                    //game plays it, then opens it here again (EditorLauncher.h). Not while editing together
                    if (key == SDLK_F5 && editors::coedit::state() == editors::coedit::State::Off) {
                        editors::requestTestPlay(writablePath(convertPath("maps/ZZleveleditor.map")));
                        printf("testplay: edited %08x (tiles %08x types %08x blocks %08x)\n", g_map->tilesHash(), g_map->tilesHash(1), g_map->tilesHash(2), g_map->tilesHash(3));    //(the match's and the editor's after it: the same)
                        return EDITOR_QUIT;
                    }
#endif

                    if (key == SDLK_F ) {
                            if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT) || findstring.empty())
                                return FIND;

                            findcurrentstring();
                        }

                    if (key == SDLK_DELETE && (CheckKey(keystate, SDLK_LCTRL) || CheckKey(keystate, SDLK_RCTRL))) {
                            return CLEAR_MAP;
                        }

                    if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
                        if (edit_mode == 3) {
                                clearselectedmaptiles();
                                resetselectedtiles();
                        } else if (edit_mode == 6) {
                                for (short iRow = 0; iRow < MAPHEIGHT; iRow++)
                                    for (short iCol = 0; iCol < MAPWIDTH; iCol++)
                                        g_map->mapdatatop[iCol][iRow] = TileType::NonSolid;
                            }
                        }

                    if (key == SDLK_N) {
                        if (edit_mode == 4) {
                                for (short iRow = 0; iRow < MAPHEIGHT; iRow++)
                                    for (short iCol = 0; iCol < MAPWIDTH; iCol++)
                                        SetNoSpawn(nospawn_mode, iCol, iRow, false);
                        } else {
                                return NEW_MAP;
                            }
                        }

                        if (key == SDLK_V)
                            viewblocks = !viewblocks;

                        if (key == SDLK_E) {
                            editor_edit_initialized = false;
                            return EDITOR_EYECANDY;
                        }

                        if (key == SDLK_W) {
                            editor_edit_initialized = false;
                            return EDITOR_WARP;
                        }

                        if (key == SDLK_L) {
                            editor_edit_initialized = false;
                            return EDITOR_TILETYPE;
                        }

                        if (key == SDLK_F1) {
                            return DISPLAY_HELP;
                        }

                    if (key == SDLK_PAGEUP) {
                        do {
                                maplist->prev(false);
                        } while (!maplist->isValid());

                            loadcurrentmap();
                        }

                    if (key == SDLK_PAGEDOWN) {
                        do {
                                maplist->next(false);
                        } while (!maplist->isValid());

                            loadcurrentmap();
                        }

                    if (key == SDLK_Y) {
                            if (++selected_layer >= MAPLAYERS)
                                selected_layer = 0;
                        }

                    if (key == SDLK_U) {
                            view_only_layer = !view_only_layer;
                        }

                        //if (key == SDLK_END)
                            //g_map->optimize();

                    if (key == SDLK_M) {
                            if (edit_mode == 3)
                                move_replace = !move_replace;

                            edit_mode = 3;
                        }

                    if (key == SDLK_X) {
                            edit_mode = 4;
                            nospawn_mode = 0;
                        }

                        if (key == SDLK_Z)
                            edit_mode = 5;

                        if (key == SDLK_LCTRL)
                            move_nodrag = true;

                        if (key == SDLK_P) {
                            editor_edit_initialized = false;
                            return EDITOR_PLATFORM;
                        }

                    if (key == SDLK_C) {
                        if (edit_mode == 3) {
                            if (copyselectedtiles()) {
                                    move_mode = 3;
                                    getcenterselection(&move_start_x, &move_start_y);
                                }
                            }
                        }

                        break;
                    }

                case SDL_EVENT_KEY_UP: {
                        if (event.key.key == SDLK_LCTRL)
                            move_nodrag = false;
                        break;
                    }

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        short iClickX = event.button.x / TILESIZE;
                        short iClickY = event.button.y / TILESIZE;

                    if (event.button.button == SDL_BUTTON_LEFT && !ignoreclick) {
                        if (edit_mode == 0) { //paint selected blocks
                                g_map->objectdata[iClickX][iClickY].iType = set_block;
                                g_map->objectdata[iClickX][iClickY].fHidden = false;

                            if (set_block == 1 || set_block == 15) {
                                    for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
                                        g_map->objectdata[iClickX][iClickY].iSettings[iSetting] = defaultPowerupSetting(0, iSetting);
                            } else if (set_block >= 11 && set_block <= 14) {
                                    g_map->objectdata[iClickX][iClickY].iSettings[0] = set_block_switch_on;
                                }

                                AdjustMapItems(iClickX, iClickY);
                        } else if (edit_mode == 1) { //paint selected tile(s)
                            for (short i = 0; i < set_tile_cols; i++) {
                                    short iLocalX = iClickX + i;

                                for (short j = 0; j < set_tile_rows; j++) {
                                        short iLocalY = iClickY + j;

                                    if (iLocalX >= 0 && iLocalX < MAPWIDTH && iLocalY >= 0 && iLocalY < MAPHEIGHT) {
                                            bool fNeedUpdate = !TileTypeIsModified(iLocalX, iLocalY);
                                            SetTilesetTile(&g_map->mapdata[iLocalX][iLocalY][selected_layer], set_tile_tileset, set_tile_start_x + i, set_tile_start_y + j);

                                            if (fNeedUpdate)
                                                UpdateTileType(iLocalX, iLocalY);

                                            AdjustMapItems(iLocalX, iLocalY);
                                        }
                                    }
                                }
                        } else if (edit_mode == 2) { //warps
                                g_map->warpdata[iClickX][iClickY].direction = (WarpEnterDirection)set_direction;
                                g_map->warpdata[iClickX][iClickY].connection = set_connection;
                        } else if (edit_mode == 3) { //move tiles
                            if (move_mode == 3) {
                                    move_mode = 0;

                                    pasteselectedtiles(move_offset_x, move_offset_y);

                                    copymoveselection();
                                    resetselectedtiles();
                                    pastemoveselection(move_offset_x, move_offset_y);

                                    move_offset_x = 0;
                                    move_offset_y = 0;
                            } else {
                                if (selectedtiles[iClickX][iClickY]) {
                                    if (copyselectedtiles()) {
                                            move_mode = 1;
                                            clearselectedmaptiles();
                                        }
                                } else {
                                        if (!CheckKey(keystate, SDLK_LSHIFT) && !CheckKey(keystate, SDLK_RSHIFT) && !CheckKey(keystate, SDLK_LCTRL))
                                            resetselectedtiles();

                                    if (move_nodrag) {
                                            selectedtiles[iClickX][iClickY] = true;
                                    } else {
                                            move_mode = 2;
                                            move_drag_start_x = iClickX;
                                            move_drag_start_y = iClickY;
                                        }
                                    }

                                    move_start_x = iClickX;
                                    move_start_y = iClickY;
                                }
                        } else if (edit_mode == 4) { //no player spawn areas
                                SetNoSpawn(nospawn_mode, iClickX, iClickY, true);
                        } else if (edit_mode == 5) { // no item spawn areas
                                g_map->nospawn[5][iClickX][iClickY] = true;
                        } else if (edit_mode == 6) { //tile types
                                g_map->mapdatatop[iClickX][iClickY] = set_tiletype;
                                AdjustMapItems(iClickX, iClickY);
                        } else if (edit_mode == 7) { //map items
                            if (g_map->mapitems.size() < MAXMAPITEMS) {
                                bool fTileNotAvailable = false;
                                for (const MapItem& item : g_map->mapitems) {
                                    if (item.ix == iClickX && item.iy == iClickY) {
                                        fTileNotAvailable = true;
                                        break;
                                    }
                                }
                                if (g_map->mapdatatop[iClickX][iClickY] != TileType::NonSolid &&
                                    g_map->mapdatatop[iClickX][iClickY] != TileType::SolidOnTop &&
                                    g_map->mapdatatop[iClickX][iClickY] != TileType::IceOnTop) {
                                    fTileNotAvailable = true;
                                }
                                if (!fTileNotAvailable) {
                                    MapItem mapitem = {};
                                    mapitem.itype = static_cast<MapItemType>(set_mapitem);
                                    mapitem.ix = iClickX;
                                    mapitem.iy = iClickY;
                                    g_map->mapitems.emplace_back(std::move(mapitem));
                                }
                            }
                        } else if (edit_mode == 8) { //animated tiles
                            for (short i = 0; i < set_tile_cols; i++) {
                                    short iLocalX = iClickX + i;

                                for (short j = 0; j < set_tile_rows; j++) {
                                        short iLocalY = iClickY + j;

                                    if (iLocalX >= 0 && iLocalX < MAPWIDTH && iLocalY >= 0 && iLocalY < MAPHEIGHT) {
                                            bool fNeedUpdate = !TileTypeIsModified(iLocalX, iLocalY);
                                            SetTilesetTile(&g_map->mapdata[iLocalX][iLocalY][selected_layer], TILESETANIMATED, set_tile_start_y + j, set_tile_start_x + i);

                                            if (fNeedUpdate)
                                                UpdateTileType(iLocalX, iLocalY);
                                        }
                                    }
                                }
                            }
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            if (edit_mode == 0)
                                g_map->objectdata[iClickX][iClickY].iType = -1;
                        else if (edit_mode == 1 || edit_mode == 8) {
                                bool fNeedUpdate = !TileTypeIsModified(iClickX, iClickY);
                                g_map->mapdata[iClickX][iClickY][selected_layer].iID = TILESETNONE;

                                if (fNeedUpdate)
                                    UpdateTileType(iClickX, iClickY);
                        } else if (edit_mode == 2) {
                                g_map->warpdata[iClickX][iClickY].direction = WARP_UNDEFINED;
                                g_map->warpdata[iClickX][iClickY].connection = -1;
                        } else if (edit_mode == 3) {
                            if (move_mode == 3) {
                                    move_mode = 0;
                                    resetselectedtiles();
                                    move_offset_x = 0;
                                    move_offset_y = 0;
                            } else {
                                if (selectedtiles[iClickX][iClickY]) {
                                        selectedtiles[iClickX][iClickY] = false;
                                } else {
                                        resetselectedtiles();
                                    }
                                }
                        } else if (edit_mode == 4) {
                                SetNoSpawn(nospawn_mode, iClickX, iClickY, false);
                        } else if (edit_mode == 5) {
                                g_map->nospawn[5][iClickX][iClickY] = false;
                        } else if (edit_mode == 6) {
                                g_map->mapdatatop[iClickX][iClickY] = TileType::NonSolid;
                        } else if (edit_mode == 7) {
                                RemoveMapItemAt(iClickX, iClickY);
                            }
                        }

                        break;
                    }

                case SDL_EVENT_MOUSE_MOTION: {
                        bound_mouse_motion_coords();
                        short iClickX = bound_to_window_w(event.motion.x) / TILESIZE;
                        short iClickY = bound_to_window_h(event.motion.y) / TILESIZE;

                    if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT) && !ignoreclick) {
                        if (edit_mode == 0) {
                                g_map->objectdata[iClickX][iClickY].iType = set_block;
                                g_map->objectdata[iClickX][iClickY].fHidden = false;

                            if (set_block == 1 || set_block == 15) {
                                    for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
                                        g_map->objectdata[iClickX][iClickY].iSettings[iSetting] = defaultPowerupSetting(0, iSetting);
                            } else if (set_block >= 11 && set_block <= 14) {
                                    g_map->objectdata[iClickX][iClickY].iSettings[0] = set_block_switch_on;
                                }

                                AdjustMapItems(iClickX, iClickY);
                        } else if (edit_mode == 1) {
                            for (short i = 0; i < set_tile_cols; i++) {
                                    short iLocalX = iClickX + i;

                                for (short j = 0; j < set_tile_rows; j++) {
                                        short iLocalY = iClickY + j;

                                    if (iLocalX >= 0 && iLocalX < MAPWIDTH && iLocalY >= 0 && iLocalY < MAPHEIGHT) {
                                            bool fNeedUpdate = !TileTypeIsModified(iLocalX, iLocalY);

                                            SetTilesetTile(&g_map->mapdata[iLocalX][iLocalY][selected_layer], set_tile_tileset, set_tile_start_x + i, set_tile_start_y + j);

                                            if (fNeedUpdate)
                                                UpdateTileType(iLocalX, iLocalY);

                                            AdjustMapItems(iLocalY, iLocalY);
                                        }
                                    }
                                }
                        } else if (edit_mode == 2) {
                                g_map->warpdata[iClickX][iClickY].direction = (WarpEnterDirection)set_direction;
                                g_map->warpdata[iClickX][iClickY].connection = set_connection;
                        } else if (edit_mode == 3) {
                            if (move_mode == 0) {
                                    selectedtiles[iClickX][iClickY] = true;
                                }
                        } else if (edit_mode == 4) {
                                SetNoSpawn(nospawn_mode, iClickX, iClickY, true);
                        } else if (edit_mode == 5) {
                                g_map->nospawn[5][iClickX][iClickY] = true;
                        } else if (edit_mode == 6) {
                                g_map->mapdatatop[iClickX][iClickY] = set_tiletype;
                                AdjustMapItems(iClickX, iClickY);
                        } else if (edit_mode == 8) {
                            for (short i = 0; i < set_tile_cols; i++) {
                                    short iLocalX = iClickX + i;

                                for (short j = 0; j < set_tile_rows; j++) {
                                        short iLocalY = iClickY + j;

                                    if (iLocalX >= 0 && iLocalX < MAPWIDTH && iLocalY >= 0 && iLocalY < MAPHEIGHT) {
                                            bool fNeedUpdate = !TileTypeIsModified(iLocalX, iLocalY);
                                            SetTilesetTile(&g_map->mapdata[iLocalX][iLocalY][selected_layer], TILESETANIMATED, set_tile_start_y + j, set_tile_start_x + i);

                                            if (fNeedUpdate)
                                                UpdateTileType(iLocalX, iLocalY);
                                        }
                                    }
                                }
                            }
                    } else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
                            if (edit_mode == 0)
                                g_map->objectdata[iClickX][iClickY].iType = -1;
                        else if (edit_mode == 1 || edit_mode == 8) {
                                bool fNeedUpdate = !TileTypeIsModified(iClickX, iClickY);
                                g_map->mapdata[iClickX][iClickY][selected_layer].iID = TILESETNONE;

                                if (fNeedUpdate)
                                    UpdateTileType(iClickX, iClickY);
                        } else if (edit_mode == 2) {
                                g_map->warpdata[iClickX][iClickY].direction = WARP_UNDEFINED;
                                g_map->warpdata[iClickX][iClickY].connection = -1;
                        } else if (edit_mode == 3) {
                                if (move_mode == 0)
                                    selectedtiles[iClickX][iClickY] = false;
                        } else if (edit_mode == 4) {
                                SetNoSpawn(nospawn_mode, iClickX, iClickY, false);
                        } else if (edit_mode == 5) {
                                g_map->nospawn[5][iClickX][iClickY] = false;
                        } else if (edit_mode == 6) {
                                g_map->mapdatatop[iClickX][iClickY] = TileType::NonSolid;
                            }
                        }

                        break;
                    }

                case SDL_EVENT_MOUSE_BUTTON_UP: {
                        short iClickX = event.button.x / TILESIZE;
                        short iClickY = event.button.y / TILESIZE;

                    if (event.button.button == SDL_BUTTON_LEFT) {
                            ignoreclick = false;

                        if (move_mode == 1) {
                                pasteselectedtiles(move_offset_x, move_offset_y);

                                copymoveselection();
                                resetselectedtiles();
                                pastemoveselection(move_offset_x, move_offset_y);

                                move_offset_x = 0;
                                move_offset_y = 0;
                        } else if (move_mode == 2) {
                                int left = move_drag_start_x < iClickX ? move_drag_start_x : iClickX;
                                int top = move_drag_start_y < iClickY ? move_drag_start_y : iClickY;
                                int right = move_drag_start_x < iClickX ? iClickX : move_drag_start_x;
                                int bottom = move_drag_start_y < iClickY ? iClickY : move_drag_start_y;

                            for (int k = top; k <= bottom; k++) {
                                for (int j = left; j <= right; j++) {
                                        selectedtiles[j][k] = true;
                                    }
                                }
                            }

                            move_mode = 0;
                        }

                        break;
                    }

                    default:
                        break;
                }
            }

            if (move_mode == 1 || move_mode == 3) {
                move_offset_x = (mouse_x / TILESIZE) - move_start_x;
                move_offset_y = (mouse_y / TILESIZE) - move_start_y;
            } else if (move_mode == 2) {
                move_drag_offset_x = (mouse_x / TILESIZE);
                move_drag_offset_y = (mouse_y / TILESIZE);
            }
        }

        if (maplist->isValid()) {
            drawmap(false, TILESIZE);
        } else {
            SDL_FillSurfaceRect(screen, nullptr, 0x0);
            rm->menu_font_large.drawCentered(320, 200, "Map has been deleted.");
        }

        //Ask if you are sure you want to exit
        if (fExiting) {
#ifdef SMW_EMBEDDED_EDITOR
            //(in the game: its dialogs' look)
            shell::drawConfirm("Leave the editor?", "Unsaved changes are lost", fSelectedYes);
#else
            rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
            rm->menu_font_large.drawCentered(320, 195, "Exit");
            rm->menu_font_large.drawCentered(320, 220, "Are You Sure?");
            rm->menu_font_large.drawCentered(282, 254, "Yes");
            rm->menu_font_large.drawCentered(356, 254, "No");

            rm->spr_dialog.draw(fSelectedYes ? 250 : 326, 250, {192, 0, 64, 32});
#endif
        } else {
            if (edit_mode == 0) {
                if (OLD_HUD) rm->menu_font_small.draw(0,0, "Block Mode");
            } else if (edit_mode == 1 || edit_mode == 8) {
                std::string modestring;

                if (edit_mode == 1)
                    modestring = "Tile Mode - ";
                else
                    modestring = "Animated Tile Mode - ";

                if (selected_layer == 0)
                    modestring += "Bottom Background";
                else if (selected_layer == 1)
                    modestring += "Top Background";
                else if (selected_layer == 2)
                    modestring += "Bottom Foreground";
                else if (selected_layer == 3)
                    modestring += "Top Foreground";

                if (view_only_layer)
                    modestring += " Only";

                if (OLD_HUD) rm->menu_font_small.draw(0,0, modestring.c_str());

                if (OLD_HUD && view_only_layer)
                    rm->spr_backgroundlevel.draw(2, 18 + (3 - selected_layer) * 18, {selected_layer * 16, (3 - selected_layer) * 18, 16, 16});
                else if (OLD_HUD)
                    rm->spr_backgroundlevel.draw(2, 18, {selected_layer * 16, 0, 16, 70});
            } else if (edit_mode == 2) {
                if (OLD_HUD) rm->menu_font_small.draw(0,0, "Warp Mode");
            } else if (edit_mode == 3) {
                for (int k = 0; k < MAPHEIGHT; k++) {
                    for (int j = 0; j < MAPWIDTH; j++) {
                        if (selectedtiles[j][k])
                            rm->spr_selectedtile.draw((j + move_offset_x) * TILESIZE, (k + move_offset_y) * TILESIZE);
                    }
                }

                //Draw dragging selection
                if (move_mode == 2) {
                    int left = move_drag_start_x < move_drag_offset_x ? move_drag_start_x : move_drag_offset_x;
                    int top = move_drag_start_y < move_drag_offset_y ? move_drag_start_y : move_drag_offset_y;
                    int right = move_drag_start_x < move_drag_offset_x ? move_drag_offset_x : move_drag_start_x;
                    int bottom = move_drag_start_y < move_drag_offset_y ? move_drag_offset_y : move_drag_start_y;

                    for (int k = top; k <= bottom; k++) {
                        for (int j = left; j <= right; j++) {
                            if (!selectedtiles[j][k])
                                rm->spr_selectedtile.draw(j * TILESIZE, k * TILESIZE);
                        }
                    }
                }

                if (OLD_HUD && move_replace)
                    rm->menu_font_small.draw(0,0, "Move Mode - Replace");
                else if (OLD_HUD)
                    rm->menu_font_small.draw(0,0, "Move Mode - Merge");

                if (OLD_HUD && view_only_layer)
                    rm->spr_backgroundlevel.draw(2, 18 + (3 - selected_layer) * 18, {selected_layer * 16, (3 - selected_layer) * 18, 16, 16});
                else if (OLD_HUD)
                    rm->spr_backgroundlevel.draw(2, 18, {selected_layer * 16, 0, 16, 70});
            } else if (edit_mode == 4) {
                for (int k = 0; k < MAPHEIGHT; k++) {
                    for (int j = 0; j < MAPWIDTH; j++) {
                        if (g_map->nospawn[nospawn_mode][j][k])
                            rm->spr_nospawntile.draw(j * TILESIZE, k * TILESIZE, {nospawn_mode * 32, 0, 32, 32});

                        if (nospawn_mode > 0) {
                            if (g_map->nospawn[0][j][k])
                                rm->spr_nospawntile.draw(j * TILESIZE, k * TILESIZE, {0, 0, 32, 32});
                        }
                    }
                }

                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - (rm->menu_font_small.getHeight() << 1), "No Player Spawn: [x] Global, [1-4] Team Spawn Zone");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[a] All, [n] None, [i] Invert");
            } else if (edit_mode == 5) {
                for (int k = 0; k < MAPHEIGHT; k++) {
                    for (int j = 0; j < MAPWIDTH; j++) {
                        if (g_map->nospawn[5][j][k])
                            rm->spr_noitemspawntile.draw(j * TILESIZE, k  * TILESIZE);
                    }
                }

                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "No Item Spawn");
            } else if (edit_mode == 6) {
                for (int k = 0; k < MAPHEIGHT; k++) {
                    for (int j = 0; j < MAPWIDTH; j++) {
                        if (g_map->mapdatatop[j][k] != TileType::NonSolid)
                            rm->spr_transparenttiles.draw(j * TILESIZE, k * TILESIZE, {static_cast<int>(PrevTileType(g_map->mapdatatop[j][k])) * TILESIZE, 0, TILESIZE, TILESIZE});
                    }
                }

                if (OLD_HUD) rm->menu_font_small.draw(0, 0, "Tile Type Mode");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Press [Delete] To Clear All Tile Types");
            } else if (edit_mode == 7) {
                if (OLD_HUD) rm->menu_font_small.draw(0, 0, "Map Item Mode");
            }

            if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

            if (g_musiccategorydisplaytimer > 0) {
                --g_musiccategorydisplaytimer;

                rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
                rm->menu_font_small.drawCentered(320, 195, "Music Category");
                rm->menu_font_large.drawCentered(320, 220, g_szMusicCategoryNames[g_map->musicCategoryID]);

                rm->menu_font_small.drawCentered(320, 255, "Press 'R' Again");
                rm->menu_font_small.drawCentered(320, 270, "To Change");
            }

            DrawMessage();
        }

        return EDITOR_EDIT;
}

void DrawMessage()
{
    if (g_messagedisplaytimer > 0) {
        --g_messagedisplaytimer;

        rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
        rm->menu_font_large.drawCentered(320, 195, g_szMessageTitle.c_str());
        rm->menu_font_large.drawCentered(320, 220, g_szMessageLine[0].c_str());
        rm->menu_font_large.drawCentered(320, 240, g_szMessageLine[1].c_str());
        rm->menu_font_large.drawCentered(320, 260, g_szMessageLine[2].c_str());
    }
}

void SetNoSpawn(short nospawnmode, short col, short row, bool value)
{
    if (!value && nospawnmode > 0 && g_map->nospawn[0][col][row])
        return;

    g_map->nospawn[nospawnmode][col][row] = value;

    if (nospawnmode == 0) {
        for (short imode = 1; imode < 5; imode++) {
            g_map->nospawn[imode][col][row] = value;
        }
    }
}

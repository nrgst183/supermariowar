#include "WorldEditorState.h"

namespace {
const char * szEditModes[10] = {"Background Mode", "Foreground Mode", "Path Sprite Mode", "Stage Mode", "Path Mode", "Vehicle Mode", "Warp Mode", "Start/Door Mode", "Boundary Mode", "Stage Mode"};
} // namespace

int editor_edit()
{
    bool done = false;
    g_musiccategorydisplaytimer = 0;

    short view_repeat_direction = -1;
    short view_repeat_timer = 0;

    bool fExiting = false;
    bool fSelectedYes = false;

    short iStageDisplay = -1;

    while (!done) {
        int framestart = SDL_GetTicks();

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
#endif
                    }
                }
            }
        } else {
            //handle messages
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_EVENT_QUIT: {
                        done = true;
                        break;
                    }

                case SDL_EVENT_KEY_DOWN: {
                    const bool* keystate = SDL_GetKeyboardState(nullptr);
                    if (event.key.key == SDLK_ESCAPE) {
                            if (g_musiccategorydisplaytimer > 0)
                                g_musiccategorydisplaytimer = 0;
                        else if (edit_mode != 0) {
                                edit_mode = 0;
                                set_tile = 0;
                        } else {
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

                        if (event.key.key == SDLK_INSERT)
                            takescreenshot();

                        if (event.key.key == SDLK_1)
                            return EDITOR_WATER;

                        if (event.key.key == SDLK_2)
                            return EDITOR_BACKGROUND;

                        if (event.key.key == SDLK_3)
                            return EDITOR_STAGEFOREGROUND;

                        if (event.key.key == SDLK_4)
                            return EDITOR_PATHSPRITE;

                        if (event.key.key == SDLK_5)
                            return EDITOR_STRUCTUREFOREGROUND;

                        if (event.key.key == SDLK_6)
                            return EDITOR_BRIDGES;

                        if (event.key.key == SDLK_T)
                            return EDITOR_TYPE;

                        if (event.key.key == SDLK_E)
                            return EDITOR_STAGE;

                        if (event.key.key == SDLK_P)
                            return EDITOR_PATH;

                        if (event.key.key == SDLK_V)
                            return EDITOR_VEHICLES;

                    if (event.key.key == SDLK_SPACE) {
                            g_fShowStagePreviews = !g_fShowStagePreviews;
                            if (g_fShowStagePreviews)
                                SetDisplayMessage(60, "Stage Previews", "Preview popups", "have been", "enabled.");
                            else
                                SetDisplayMessage(60, "Stage Previews", "Preview popups", "have been", "disabled.");
                        }

                    if (edit_mode == 5 && event.key.key == SDLK_C) {
                            short iButtonX = mouse_x - draw_offset_x;
                            short iButtonY = mouse_y - draw_offset_y;
                            short iCol = iButtonX / TILESIZE + draw_offset_col;
                            short iRow = iButtonY / TILESIZE + draw_offset_row;

                            auto itr = vehiclelist.begin(), lim = vehiclelist.end();
                        while (itr != lim) {
                                WorldVehicle * vehicle = itr->get();
                            if (vehicle->getCurrentTile().x == iCol && vehicle->getCurrentTile().y == iRow) {
                                    g_wvVehicleStamp.setDrawSprite(vehicle->getDrawSprite());
                                    g_wvVehicleStamp.setActionId(vehicle->getActionId());
                                    g_wvVehicleStamp.setMinMoves(vehicle->getMinMoves());
                                    g_wvVehicleStamp.setMaxMoves(vehicle->getMaxMoves());
                                    g_wvVehicleStamp.setSpritePacing(vehicle->isSpritePacing());
                                    g_wvVehicleStamp.setDrawDirection(vehicle->getDrawDirection());
                                    g_wvVehicleStamp.setBoundary(vehicle->getBoundary());

                                    miVehicleSpriteField->setCurrentValue(g_wvVehicleStamp.getDrawSprite());
                                    miVehicleStageField->setCurrentValue(g_wvVehicleStamp.getActionId());
                                    miVehicleMinMovesField->setCurrentValue(g_wvVehicleStamp.getMinMoves());
                                    miVehicleMaxMovesField->setCurrentValue(g_wvVehicleStamp.getMaxMoves());
                                    miVehiclePacesField->setCurrentValue(g_wvVehicleStamp.isSpritePacing() ? 1 : 0);
                                    miVehicleDirectionField->setCurrentValue(g_wvVehicleStamp.getDrawDirection());
                                    miVehicleBoundaryField->setCurrentValue(g_wvVehicleStamp.getBoundary());

                                    return EDITOR_VEHICLES;
                                }

                                itr++;
                            }
                        }

                        if (event.key.key == SDLK_W)
                            return EDITOR_WARP;

                        if (event.key.key == SDLK_I)
                            return EDITOR_START_ITEMS;

                        if (event.key.key == SDLK_B)
                            return EDITOR_BOUNDARY;

                        if (event.key.key == SDLK_A)
                            fAutoPaint = !fAutoPaint;

                    if (event.key.key == SDLK_R) {
                            if (g_musiccategorydisplaytimer > 0)
                                g_worldmap.nextMusicCategory();

                            g_musiccategorydisplaytimer = 90;
                        }

                    if (event.key.key == SDLK_S) {
                            if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT))
                                return SAVE_AS;

                            return SAVE;
                        }

                    if (event.key.key == SDLK_F) {
                            if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT) || findstring.empty())
                                return FIND;

                            findcurrentstring();
                        }

                    if (event.key.key == SDLK_DELETE && (CheckKey(keystate, SDLK_LCTRL) || CheckKey(keystate, SDLK_RCTRL))) {
                            return CLEAR_WORLD;
                        }

                        if (event.key.key == SDLK_N)
                            return NEW_WORLD;

                        if (event.key.key == SDLK_K)
                            return RESIZE_WORLD;

                        if (event.key.key == SDLK_H || event.key.key == SDLK_F1)
                            return DISPLAY_HELP;

                    if (event.key.key == SDLK_UP) {
                        if (draw_offset_row > 0) {
                                draw_offset_row--;
                                updateworldsurface();

                                view_repeat_direction = 0;
                                view_repeat_timer = 30;

                                iStageDisplay = -1;
                            }
                    } else if (event.key.key == SDLK_DOWN) {
                        if (draw_offset_row < iWorldHeight - 15) {
                                draw_offset_row++;
                                updateworldsurface();

                                view_repeat_direction = 1;
                                view_repeat_timer = 30;

                                iStageDisplay = -1;
                            }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (draw_offset_col > 0) {
                                draw_offset_col--;
                                updateworldsurface();

                                view_repeat_direction = 2;
                                view_repeat_timer = 30;

                                iStageDisplay = -1;
                            }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (draw_offset_col < iWorldWidth - 20) {
                                draw_offset_col++;
                                updateworldsurface();

                                view_repeat_direction = 3;
                                view_repeat_timer = 30;

                                iStageDisplay = -1;
                            }
                        }

                    if (event.key.key == SDLK_PAGEUP) {
                            if (--game_values.worldindex < 0)
                            game_values.worldindex = worldlist->count() - 1;

                        worldlist->prev();

                            loadcurrentworld();

                            iOldStageId = -1;
                        }

                    if (event.key.key == SDLK_PAGEDOWN) {
                        if (++game_values.worldindex >= static_cast<short>(worldlist->count()))
                                game_values.worldindex = 0;

                        worldlist->next();

                            loadcurrentworld();

                            iOldStageId = -1;
                        }

                        break;
                    }

                case SDL_EVENT_KEY_UP: {
                        if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN ||
                            event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                            view_repeat_direction = -1;
                            view_repeat_timer = 0;
                        }

                        break;
                    }

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        short iButtonX = bound_to_window_w(event.button.x) - draw_offset_x;
                        short iButtonY = bound_to_window_h(event.button.y) - draw_offset_y;
                        short iCol = iButtonX / TILESIZE + draw_offset_col;
                        short iRow = iButtonY / TILESIZE + draw_offset_row;

                    if (iButtonX >= 0 && iButtonY >= 0 && iButtonX < iWorldWidth * TILESIZE && iButtonY < iWorldHeight * TILESIZE) {
                        if (event.button.button == SDL_BUTTON_LEFT && !ignoreclick) {
                            if (edit_mode == 0) { //selected background
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != set_tile || fAutoPaint) {
                                        bool fNeedUpdate = false;
                                    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != set_tile) {
                                            g_worldmap.getTile(iCol, iRow).iBackgroundSprite = set_tile;
                                            fNeedUpdate = true;
                                        }

                                        fNeedUpdate |= UpdateForeground(iCol, iRow);

                                        if ((set_tile % WORLD_BACKGROUND_SPRITE_SET_SIZE) < 2 && fAutoPaint)
                                            fNeedUpdate |= UpdateCoastline(iCol, iRow);

                                        if (fNeedUpdate)
                                            updateworldsurface();
                                    }
                            } else if (edit_mode == 1) { //selected foreground
                                if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != set_tile) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = set_tile;
                                        updateworldsurface();

                                        if (set_tile >= WORLD_BRIDGE_SPRITE_OFFSET && set_tile <= WORLD_BRIDGE_SPRITE_OFFSET + 3)
                                            g_worldmap.getTile(iCol, iRow).iConnectionType = set_tile - WORLD_BRIDGE_SPRITE_OFFSET + 12;
                                    }
                            } else if (edit_mode == 2) { //selected connection
                                    g_worldmap.getTile(iCol, iRow).iConnectionType = set_tile;

                                    if (fAutoPaint)
                                        UpdatePath(iCol, iRow);
                            } else if (edit_mode == 3) { //selected type
                                    //start tiles
                                if (set_tile <= 1) {
                                    if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != set_tile + WORLD_START_SPRITE_OFFSET) {
                                            g_worldmap.getTile(iCol, iRow).iType = 1;
                                            g_worldmap.getTile(iCol, iRow).iForegroundSprite = set_tile + WORLD_START_SPRITE_OFFSET;
                                            updateworldsurface();
                                        }
                                } else if (set_tile <= 5) { //doors
                                    if (g_worldmap.getTile(iCol, iRow).iType != set_tile) {
                                            //if the door was placed on a start tile
                                            if (g_worldmap.getTile(iCol, iRow).iType == 1)
                                                g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;

                                            g_worldmap.getTile(iCol, iRow).iType = set_tile;
                                            updateworldsurface();
                                        }
                                    }
                            } else if (edit_mode == 4) { //selected path sprite
                                    short iAdjustedTile = AdjustForeground(set_tile, iCol, iRow);
                                    bool fNeedUpdate = false;

                                if (!fAutoPaint && g_worldmap.getTile(iCol, iRow).iForegroundSprite != iAdjustedTile) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = iAdjustedTile;
                                        fNeedUpdate = true;
                                    }

                                    //Detect if there was a change so we can repaint the screen
                                if (fAutoPaint) {
                                        short iOldTiles[9];
                                        GetForegroundTileValues(iCol, iRow, iOldTiles);
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = iAdjustedTile;
                                        UpdatePathSprite(iCol, iRow);

                                        if (ForegroundTileValuesChanged(iCol, iRow, iOldTiles))
                                            fNeedUpdate = true;
                                    }

                                    if (fNeedUpdate)
                                        updateworldsurface();
                            } else if (edit_mode == 5) { //selected vehicle
                                    AddVehicleToTile(iCol, iRow, set_tile);
                            } else if (edit_mode == 6) { //selected warp
                                    AddWarpToTile(iCol, iRow, set_tile);
                            } else if (edit_mode == 7) { //water
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundWater != set_tile) {
                                        g_worldmap.getTile(iCol, iRow).iBackgroundWater = set_tile;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 8) { //boundary
                                    g_worldmap.getTile(iCol, iRow).iVehicleBoundary = set_tile;
                            } else if (edit_mode == 9) {
                                    //if the stage was placed on a start tile
                                if (g_worldmap.getTile(iCol, iRow).iType == 1) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                    }

                                    g_worldmap.getTile(iCol, iRow).iType = set_tile;
                                }
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            if (edit_mode == 0) {
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != 0 || fAutoPaint) {
                                        bool fNeedUpdate = false;

                                    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != 0) {
                                            g_worldmap.getTile(iCol, iRow).iBackgroundSprite = 0;
                                            fNeedUpdate = true;
                                        }

                                        fNeedUpdate |= UpdateForeground(iCol, iRow);

                                        if (fAutoPaint)
                                            fNeedUpdate |= UpdateCoastline(iCol, iRow);

                                        if (fNeedUpdate)
                                            updateworldsurface();
                                    }
                            } else if (edit_mode == 1) {
                                if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != 0) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 2) {
                                    g_worldmap.getTile(iCol, iRow).iConnectionType = 0;

                                    if (fAutoPaint)
                                        UpdatePath(iCol, iRow);
                            } else if (edit_mode == 3) { //selected start/door
                                if (g_worldmap.getTile(iCol, iRow).iType == 1) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                } else if (g_worldmap.getTile(iCol, iRow).iType <= 5) {
                                        g_worldmap.getTile(iCol, iRow).iType = 0;
                                        updateworldsurface();
                                    }

                                    g_worldmap.getTile(iCol, iRow).iType = 0;
                            } else if (edit_mode == 4) {
                                    bool fNeedUpdate = false;

                                if (!fAutoPaint && g_worldmap.getTile(iCol, iRow).iForegroundSprite != 0) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        fNeedUpdate = true;
                                    }

                                    //Detect if there was a change so we can repaint the screen
                                if (fAutoPaint) {
                                        short iOldTiles[9];
                                        GetForegroundTileValues(iCol, iRow, iOldTiles);
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        UpdatePathSprite(iCol, iRow);

                                        if (ForegroundTileValuesChanged(iCol, iRow, iOldTiles))
                                            fNeedUpdate = true;
                                    }

                                    if (fNeedUpdate)
                                        updateworldsurface();
                            } else if (edit_mode == 5) {
                                    RemoveVehicleFromTile(iCol, iRow);
                                    iStageDisplay = -1;
                            } else if (edit_mode == 6) {
                                    RemoveWarpFromTile(iCol, iRow);
                            } else if (edit_mode == 7) { //water
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundWater != 0) {
                                        g_worldmap.getTile(iCol, iRow).iBackgroundWater = 0;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 8) { //boundary
                                    g_worldmap.getTile(iCol, iRow).iVehicleBoundary = 0;
                            } else if (edit_mode == 9) { //stage
                                    g_worldmap.getTile(iCol, iRow).iType = 0;
                                    iStageDisplay = -1;
                                }
                            }
                        }

                        break;
                    }
                    //Painting tiles with mouse movement
                case SDL_EVENT_MOUSE_MOTION: {
                        bound_mouse_motion_coords();
                        short iButtonX = bound_to_window_w(event.motion.x) - draw_offset_x;
                        short iButtonY = bound_to_window_h(event.motion.y) - draw_offset_y;
                        short iCol = (iButtonX >> 5) + draw_offset_col;
                        short iRow = (iButtonY >> 5) + draw_offset_row;

                    if (iButtonX >= 0 && iButtonY >= 0 && iButtonX < iWorldWidth * TILESIZE && iButtonY < iWorldHeight * TILESIZE) {
                        if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT) && !ignoreclick) {
                            if (edit_mode == 0) { //selected background
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != set_tile || fAutoPaint) {
                                        bool fNeedUpdate = false;
                                    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != set_tile) {
                                            g_worldmap.getTile(iCol, iRow).iBackgroundSprite = set_tile;
                                            fNeedUpdate = true;
                                        }

                                        fNeedUpdate |= UpdateForeground(iCol, iRow);

                                        if ((set_tile % WORLD_BACKGROUND_SPRITE_SET_SIZE) < 2 && fAutoPaint)
                                            fNeedUpdate |= UpdateCoastline(iCol, iRow);

                                        if (fNeedUpdate)
                                            updateworldsurface();
                                    }
                            } else if (edit_mode == 1) {
                                if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != set_tile) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = set_tile;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 2) {
                                    g_worldmap.getTile(iCol, iRow).iConnectionType = set_tile;

                                    if (fAutoPaint)
                                        UpdatePath(iCol, iRow);
                            } else if (edit_mode == 3) { //selected stage/door
                                if (set_tile <= 1) {
                                    if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != set_tile + WORLD_START_SPRITE_OFFSET) {
                                            g_worldmap.getTile(iCol, iRow).iType = 1;
                                            g_worldmap.getTile(iCol, iRow).iForegroundSprite = set_tile + WORLD_START_SPRITE_OFFSET;
                                            updateworldsurface();
                                        }
                                } else if (set_tile <= 5) {
                                    if (g_worldmap.getTile(iCol, iRow).iType != set_tile) {
                                            //if the door was placed on a start tile
                                            if (g_worldmap.getTile(iCol, iRow).iType == 1)
                                                g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;

                                            g_worldmap.getTile(iCol, iRow).iType = set_tile;
                                            updateworldsurface();
                                        }
                                    }
                            } else if (edit_mode == 4) {
                                    short iAdjustedTile = AdjustForeground(set_tile, iCol, iRow);
                                    bool fNeedUpdate = false;

                                if (!fAutoPaint && g_worldmap.getTile(iCol, iRow).iForegroundSprite != iAdjustedTile) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = iAdjustedTile;
                                        fNeedUpdate = true;
                                    }

                                    //Detect if there was a change so we can repaint the screen
                                if (fAutoPaint) {
                                        short iOldTiles[9];
                                        GetForegroundTileValues(iCol, iRow, iOldTiles);
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = iAdjustedTile;
                                        UpdatePathSprite(iCol, iRow);

                                        if (ForegroundTileValuesChanged(iCol, iRow, iOldTiles))
                                            fNeedUpdate = true;
                                    }

                                    if (fNeedUpdate)
                                        updateworldsurface();
                            } else if (edit_mode == 5) {
                                    AddVehicleToTile(iCol, iRow, set_tile);
                            } else if (edit_mode == 6) {
                                    AddWarpToTile(iCol, iRow, set_tile);
                            } else if (edit_mode == 7) { //water
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundWater != set_tile) {
                                        g_worldmap.getTile(iCol, iRow).iBackgroundWater = set_tile;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 9) { //stage
                                    //if the stage was placed on a start tile
                                if (g_worldmap.getTile(iCol, iRow).iType == 1) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                    }

                                    g_worldmap.getTile(iCol, iRow).iType = set_tile;
                                }
                        } else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
                            if (edit_mode == 0) { //selected background
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != 0 || fAutoPaint) {
                                        bool fNeedUpdate = false;
                                    if (g_worldmap.getTile(iCol, iRow).iBackgroundSprite != 0) {
                                            g_worldmap.getTile(iCol, iRow).iBackgroundSprite = 0;
                                            fNeedUpdate = true;
                                        }

                                        fNeedUpdate |= UpdateForeground(iCol, iRow);

                                        if (fAutoPaint)
                                            fNeedUpdate |= UpdateCoastline(iCol, iRow);

                                        if (fNeedUpdate)
                                            updateworldsurface();
                                    }
                            } else if (edit_mode == 1) {
                                if (g_worldmap.getTile(iCol, iRow).iForegroundSprite != 0) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 2) {
                                    g_worldmap.getTile(iCol, iRow).iConnectionType = 0;

                                    if (fAutoPaint)
                                        UpdatePath(iCol, iRow);
                            } else if (edit_mode == 3) {
                                if (g_worldmap.getTile(iCol, iRow).iType == 1) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        updateworldsurface();
                                } else if (g_worldmap.getTile(iCol, iRow).iType <= 5) {
                                        g_worldmap.getTile(iCol, iRow).iType = 0;
                                        updateworldsurface();
                                    }

                                    g_worldmap.getTile(iCol, iRow).iType = 0;
                            } else if (edit_mode == 4) {
                                    bool fNeedUpdate = false;

                                if (!fAutoPaint && g_worldmap.getTile(iCol, iRow).iForegroundSprite != 0) {
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        fNeedUpdate = true;
                                    }

                                    //Detect if there was a change so we can repaint the screen
                                if (fAutoPaint) {
                                        short iOldTiles[9];
                                        GetForegroundTileValues(iCol, iRow, iOldTiles);
                                        g_worldmap.getTile(iCol, iRow).iForegroundSprite = 0;
                                        UpdatePathSprite(iCol, iRow);

                                        if (ForegroundTileValuesChanged(iCol, iRow, iOldTiles))
                                            fNeedUpdate = true;
                                    }

                                    if (fNeedUpdate)
                                        updateworldsurface();
                            } else if (edit_mode == 5) { //vehicles
                                    RemoveVehicleFromTile(iCol, iRow);
                                    iStageDisplay = -1;
                            } else if (edit_mode == 6) { //Warps
                                    RemoveWarpFromTile(iCol, iRow);
                            } else if (edit_mode == 7) { //water
                                if (g_worldmap.getTile(iCol, iRow).iBackgroundWater != 0) {
                                        g_worldmap.getTile(iCol, iRow).iBackgroundWater = 0;
                                        updateworldsurface();
                                    }
                            } else if (edit_mode == 9) { //stage
                                    g_worldmap.getTile(iCol, iRow).iType = 0;
                                    iStageDisplay = -1;
                                }
                            }
                        }

                        //Scan to see if we are mousing over a stage
                    if (edit_mode == 5) {
                            iStageDisplay = -1;

                            auto itr = vehiclelist.begin(), lim = vehiclelist.end();
                        while (itr != lim) {
                                WorldVehicle * vehicle = itr->get();
                            if (vehicle->getCurrentTile().x == iCol && vehicle->getCurrentTile().y == iRow) {
                                    iStageDisplay = vehicle->getActionId();
                                    break;
                                }

                                itr++;
                            }
                    } else if (edit_mode == 9) {
                            iStageDisplay = -1;
                        if (iCol >= 0 && iRow >= 0 && iCol < iWorldWidth && iRow < iWorldHeight) {
                                short iType = g_worldmap.getTile(iCol, iRow).iType - 6;
                            if (iType >= 0) {
                                    iStageDisplay = iType;
                                }
                            }
                        }

                        break;
                    }

                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                            ignoreclick = false;
                        }

                        break;
                    }

                    default:
                        break;
                }
            }

            //Allow auto-scrolling of world when the arrow keys are held down
            if (view_repeat_direction >= 0 && view_repeat_timer > 0) {
                if (--view_repeat_timer <= 0) {
                    view_repeat_timer = 5;

                    if (view_repeat_direction == 0 && draw_offset_row > 0) {
                        draw_offset_row--;
                        updateworldsurface();
                    } else if (view_repeat_direction == 1 && draw_offset_row < iWorldHeight - 15) {
                        draw_offset_row++;
                        updateworldsurface();
                    } else if (view_repeat_direction == 2 && draw_offset_col > 0) {
                        draw_offset_col--;
                        updateworldsurface();
                    } else if (view_repeat_direction == 3 && draw_offset_col < iWorldWidth - 20) {
                        draw_offset_col++;
                        updateworldsurface();
                    }
                }
            }
        }

        drawmap(false, TILESIZE);

        //Ask if you are sure you want to exit
        if (fExiting) {
            spr_dialog.draw(224, 176, {0, 0, 192, 128});
            rm->menu_font_large.drawCentered(320, 195, "Exit");
            rm->menu_font_large.drawCentered(320, 220, "Are You Sure?");
            rm->menu_font_large.drawCentered(282, 254, "Yes");
            rm->menu_font_large.drawCentered(356, 254, "No");

            spr_dialog.draw(fSelectedYes ? 250 : 326, 250, {192, 0, 64, 32});
        } else {
            //Draw Paths
            if (edit_mode == 2) {
                for (short iRow = draw_offset_row; iRow < draw_offset_row + 15 && iRow < iWorldHeight; iRow++) {
                    for (short iCol = draw_offset_col; iCol <= draw_offset_col + 20 && iCol < iWorldWidth; iCol++) {
                        short iConnection = g_worldmap.getTile(iCol, iRow).iConnectionType;

                        if (iConnection > 0)
                            spr_path.draw((iCol - draw_offset_col) * TILESIZE + draw_offset_x, (iRow - draw_offset_row) * TILESIZE + draw_offset_y, {(iConnection - 1) << 5, 0, TILESIZE, TILESIZE});
                    }
                }
            } else if (edit_mode == 6) { //draw warps
                auto itr = warplist.begin(), lim = warplist.end();
                while (itr != lim) {
                    WorldWarp * warp = itr->get();

                    short ix, iy;

                    if (warp->posA.x >= 0) {
                        ix = (warp->posA.x - draw_offset_col) * TILESIZE + draw_offset_x;
                        iy = (warp->posA.y - draw_offset_row) * TILESIZE + draw_offset_y;

                        spr_warps[0].draw(ix, iy, {warp->id << 5, 0, 32, 32});
                    }

                    if (warp->posB.x >= 0) {
                        ix = (warp->posB.x - draw_offset_col) * TILESIZE + draw_offset_x;
                        iy = (warp->posB.y - draw_offset_row) * TILESIZE + draw_offset_y;

                        spr_warps[0].draw(ix, iy, {warp->id << 5, 0, 32, 32});
                    }

                    itr++;
                }
            } else if (edit_mode == 8) { //draw boundaries
                int color = SDL_MapSurfaceRGB(blitdest, 255, 0, 255);
                for (short iRow = draw_offset_row; iRow < draw_offset_row + 15 && iRow < iWorldHeight; iRow++) {
                    for (short iCol = draw_offset_col; iCol <= draw_offset_col + 20 && iCol < iWorldWidth; iCol++) {
                        short iBoundary = g_worldmap.getTile(iCol, iRow).iVehicleBoundary - 1;

                        if (iBoundary >= 0) {
                            short ix = (iCol - draw_offset_col) * TILESIZE + draw_offset_x;
                            short iy = (iRow - draw_offset_row) * TILESIZE + draw_offset_y;
                            SDL_Rect r = {ix, iy, 32, 32};
                            SDL_FillSurfaceRect(blitdest, &r, color);

                            rm->spr_worldforegroundspecial[0].draw(ix, iy, {(iBoundary % 10) << 5, (iBoundary / 10) << 5, 32, 32});
                        }
                    }
                }
            } else if (edit_mode == 9) { //draw stages
                int color = SDL_MapSurfaceRGB(blitdest, 0, 0, 255);
                for (short iRow = draw_offset_row; iRow < draw_offset_row + 15 && iRow < iWorldHeight; iRow++) {
                    for (short iCol = draw_offset_col; iCol <= draw_offset_col + 20 && iCol < iWorldWidth; iCol++) {
                        short iType = g_worldmap.getTile(iCol, iRow).iType - 6;

                        if (iType >= 0) {
                            short ix = (iCol - draw_offset_col) * TILESIZE + draw_offset_x;
                            short iy = (iRow - draw_offset_row) * TILESIZE + draw_offset_y;
                            SDL_Rect r = {ix, iy, 32, 32};
                            SDL_FillSurfaceRect(blitdest, &r, color);

                            rm->spr_worldforegroundspecial[0].draw(ix, iy, {(iType % 10) << 5, (iType / 10) << 5, 32, 32});
                        }
                    }
                }

                if (iStageDisplay >= 0 && g_fShowStagePreviews) {
                    DisplayStageDetails(false, iStageDisplay, mouse_x, mouse_y);
                }
            }

            if (edit_mode == 5 || edit_mode == 8) { //draw vehicles
                auto itr = vehiclelist.begin(), lim = vehiclelist.end();
                int color = SDL_MapSurfaceRGB(blitdest, 0, 0, 128);
                while (itr != lim) {
                    WorldVehicle * vehicle = itr->get();

                    short ix = (vehicle->getCurrentTile().x - draw_offset_col) * TILESIZE + draw_offset_x;
                    short iy = (vehicle->getCurrentTile().y - draw_offset_row) * TILESIZE + draw_offset_y;

                    SDL_Rect r = {ix, iy, 32, 32};
                    SDL_FillSurfaceRect(blitdest, &r, color);

                    rm->spr_worldvehicle[0].draw(ix, iy, {vehicle->getDrawDirection() << 5, vehicle->getDrawSprite() << 5, 32, 32});

                    if (edit_mode == 5) {
                        rm->spr_worldforegroundspecial[0].draw(ix, iy, {(vehicle->getActionId() % 10) << 5, (vehicle->getActionId() / 10) << 5, 32, 32});
                    }

                    if (edit_mode == 8) {
                        short iBoundary = vehicle->getBoundary() - 1;
                        if (iBoundary == -1)
                            rm->spr_worldforegroundspecial[0].draw(ix, iy, {288, 288, 32, 32});
                        else
                            rm->spr_worldforegroundspecial[0].draw(ix, iy, {(iBoundary % 10) << 5, (iBoundary / 10) << 5, 32, 32});
                    }

                    itr++;
                }

                if (edit_mode == 5) {
                    if (iStageDisplay >= 0) {
                        DisplayStageDetails(false, iStageDisplay, mouse_x, mouse_y);
                    }
                }
            }

            //(in the game, the overlay shows these: EditorInput.cpp)
#ifndef SMW_EMBEDDED_EDITOR
            rm->menu_font_small.draw(0, 0, szEditModes[edit_mode]);

            if (fAutoPaint)
                rm->menu_font_small.draw(0, 16, "Auto Paint");

                        rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());
#endif

            if (--g_musiccategorydisplaytimer > 0) {
                spr_dialog.draw(224, 176, {0, 0, 192, 128});
                rm->menu_font_small.drawCentered(320, 195, "Music Category");
                                rm->menu_font_large.drawCentered(320, 220, to_string(g_worldmap.GetMusicCategory()).data());

                rm->menu_font_small.drawCentered(320, 255, "Press 'R' Again");
                rm->menu_font_small.drawCentered(320, 270, "To Change");
            }

            DrawMessage();
        }

        gfx_flipscreen();

        int delay = WAITTIME - (SDL_GetTicks() - framestart);
        if (delay < 0)
            delay = 0;
        else if (delay > WAITTIME)
            delay = WAITTIME;

        SDL_Delay(delay);
    }

    return EDITOR_QUIT;
}

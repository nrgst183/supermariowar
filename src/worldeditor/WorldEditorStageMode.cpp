#include "WorldEditorState.h"

int editor_stage()
{
    mCurrentMenu = &mStageSettingsMenu;
    mCurrentMenu->ResetMenu();

    bool done = false;
    short iStageDisplay = -1;
    bool fForceStageDisplay = false;
    short iEditStage = -1;

    SDL_Rect rStageBonusDst[10];
    SDL_Rect rHouseBonusDst[MAX_BONUS_CHESTS];

    short iColCount = 0;
    short iRowCount = 0;
    for (short iItem = 0; iItem < NUM_WORLD_ITEMS; iItem++) {
        rItemDst[iItem].x = 16 + iColCount * 48;
        rItemDst[iItem].y = 16 + iRowCount * 48;
        rItemDst[iItem].w = 32;
        rItemDst[iItem].h = 32;

        if (++iColCount > 12) {
            iColCount = 0;
            iRowCount++;
        }
    }

    for (short iStageBonus = 0; iStageBonus < 10; iStageBonus++) {
        rStageBonusDst[iStageBonus].x = 35 + iStageBonus * 58;
        rStageBonusDst[iStageBonus].y = 360;
        rStageBonusDst[iStageBonus].w = 32;
        rStageBonusDst[iStageBonus].h = 32;
    }

    short iStartItemX = (640 - (MAX_BONUS_CHESTS * 58 - 10)) >> 1;

    for (short iHouseBonus = 0; iHouseBonus < MAX_BONUS_CHESTS; iHouseBonus++) {
        rHouseBonusDst[iHouseBonus].x = iStartItemX + iHouseBonus * 58;
        rHouseBonusDst[iHouseBonus].y = 360;
        rHouseBonusDst[iHouseBonus].w = 32;
        rHouseBonusDst[iHouseBonus].h = 32;
    }

    while (!done) {
        int framestart = SDL_GetTicks();

        //Reset the keys that were down the last frame
        game_values.playerInput.ClearPressedKeys(1);

        MenuCodeEnum code = MENU_CODE_NONE;

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT: {
                    done = true;
                    break;
                }

            case SDL_EVENT_KEY_DOWN: {
                    //Do not allow saving world by pressing 's' key
                    //World data structures are not in the correct state to be saved
                    //until exiting the stage editor menu in (MENU_CODE_EXIT_APPLICATION == code) below

                if (iEditStage == -1 && event.key.key == SDLK_N) {
                        NewStage(&iEditStage);
                } else if ((event.key.key == SDLK_ESCAPE || event.key.key == SDLK_E) && iEditStage == -1) {
                    if (g_worldmap.getNumStages() == 0) {
                            edit_mode = 1;
                    } else if (set_tile < 6 || set_tile >= g_worldmap.getNumStages() + 6) {
                            set_tile = 6;
                            edit_mode = 9;
                    } else {
                            edit_mode = 9;  //change to edit mode using stages
                        }

                        return EDITOR_EDIT;
                } else if (mCurrentMenu == &mBonusItemPicker && event.key.key >= SDLK_1 && event.key.key <= SDLK_4) {
                        TourStop * ts = game_values.tourstops[iEditStage];
                    if (ts->iStageType == 0) {
                            short iPlace = event.key.key - SDLK_1 + 1;

                            TestAndSetBonusItem(ts, iPlace, mouse_x, mouse_y);
                        }
                } else if ((event.key.key == SDLK_PAGEUP && iEditStage > 0) ||
                          (event.key.key == SDLK_PAGEDOWN && iEditStage < g_worldmap.getNumStages() - 1)) {
                    if (iEditStage != -1 && mCurrentMenu == &mStageSettingsMenu) {
                            SaveStage(iEditStage);

                            if (event.key.key == SDLK_PAGEUP)
                                iEditStage--;
                            else if (event.key.key == SDLK_PAGEDOWN)
                                iEditStage++;

                            EditStage(iEditStage);
                            mCurrentMenu->ResetMenu();

                            code = MENU_CODE_MODE_CHANGED;

                            mModeOptionsMenu->Refresh();
                        }
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    short iTileX = bound_to_window_w(event.button.x) / TILESIZE;
                    short iTileY = bound_to_window_h(event.button.y) / TILESIZE;
                    short iButtonX = bound_to_window_w(event.button.x);
                    short iButtonY = bound_to_window_h(event.button.y);

                if (event.button.button == SDL_BUTTON_LEFT) {
                        //Stages
                    if (iEditStage == -1) {
                        if (iTileX >= 0 && iTileX < g_worldmap.getNumStages() - (iTileY * 20) && iTileY >= 0 && iTileY <= (g_worldmap.getNumStages() - 1) / 20) {
                                set_tile = iTileX + (iTileY * 20) + 6;

                                edit_mode = 9;  //change to edit mode using warps

                                //The user must release the mouse button before trying to add a tile
                                ignoreclick = true;

                                return EDITOR_EDIT;
                            }
                            //New stage button
                        else if (iButtonX >= 256 && iButtonX < 384 && iButtonY >= 420 && iButtonY < 452) {
                                NewStage(&iEditStage);
                            }
                    } else if (mCurrentMenu == &mBonusItemPicker) {
                            TourStop * ts = game_values.tourstops[iEditStage];

                            //See if we clicked an item and add it if we did
                            TestAndSetBonusItem(ts, 0, iButtonX, iButtonY);

                            //See if we clicked an already added item and remove it
                        for (short iRemoveItem = 0; iRemoveItem < ts->iNumBonuses; iRemoveItem++) {
                                SDL_Rect * rects = rStageBonusDst;

                                if (ts->iStageType == 1)
                                    rects = rHouseBonusDst;

                                if (iButtonX >= rects[iRemoveItem].x && iButtonX < rects[iRemoveItem].w + rects[iRemoveItem].x &&
                                    iButtonY >= rects[iRemoveItem].y && iButtonY < rects[iRemoveItem].h + rects[iRemoveItem].y) {
                                for (short iAdjust = iRemoveItem; iAdjust < ts->iNumBonuses - 1; iAdjust++) {
                                        ts->wsbBonuses[iAdjust].iBonus = ts->wsbBonuses[iAdjust + 1].iBonus;
                                        ts->wsbBonuses[iAdjust].iWinnerPlace = ts->wsbBonuses[iAdjust + 1].iWinnerPlace;
                                        ts->wsbBonuses[iAdjust].szBonusString = ts->wsbBonuses[iAdjust + 1].szBonusString;
                                    }

                                    ts->iNumBonuses--;

                                    break;
                                }
                            }
                    } else {
                            code = mCurrentMenu->MouseClick(iButtonX, iButtonY);
                        }
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    if (iEditStage == -1) {
                        if (iTileX >= 0 && iTileX < g_worldmap.getNumStages() - (iTileY * 20) && iTileY >= 0 && iTileY <= (g_worldmap.getNumStages() - 1) / 20) {
                                iEditStage = iTileX + (iTileY * 20);
                                EditStage(iEditStage);

                                code = MENU_CODE_MODE_CHANGED;

                                mModeOptionsMenu->Refresh();
                            }
                        }
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_MOTION: {
                    bound_mouse_motion_coords();
                    iStageDisplay = -1;

                if (iEditStage == -1) {
//						if (event.button.x >= 0 && event.button.y >= 0)
                        {
                            short iMouseX = bound_to_window_w(event.button.x) / TILESIZE;
                            short iMouseY = bound_to_window_h(event.button.y) / TILESIZE;

                        if (iMouseX >= 0 && iMouseX < g_worldmap.getNumStages() - (iMouseY * 20) && iMouseY >= 0 && iMouseY <= (g_worldmap.getNumStages() - 1) / 20) {
                                iStageDisplay = iMouseX + (iMouseY * 20);
                            }
                        }
                    }
                }

                default:
                    break;
            }

            game_values.playerInput.Update(event, 1);
        }

        if (iEditStage >= 0) {
            if (MENU_CODE_NONE == code) {
                code = mCurrentMenu->SendInput(&game_values.playerInput);
            }

            if (MENU_CODE_EXIT_APPLICATION == code) {
                //Save the current stage
                SaveStage(iEditStage);

                //We are no longer working on a specific stage
                iEditStage = -1;
            } else if (MENU_CODE_MODE_CHANGED == code) {
                short iMode = miModeField->currentValue();

                miPointsField->setVisible(iMode != 24);
                miFinalStageField->setVisible(iMode != 24);

                miMapField->setVisible(iMode != 24);

                miBonusType->setVisible(iMode == 24);
                miBonusTextField[0]->setVisible(iMode == 24);
                miBonusTextField[1]->setVisible(iMode == 24);
                miBonusTextField[2]->setVisible(iMode == 24);
                miBonusTextField[3]->setVisible(iMode == 24);
                miBonusTextField[4]->setVisible(iMode == 24);

                miSpecialGoalField[0]->setVisible(iMode == 25);
                miSpecialGoalField[1]->setVisible(iMode == 26);
                miSpecialGoalField[2]->setVisible(iMode == 27);

                miBonusItemsButton->SetPosition(430, iMode != 24 ? 220 : 340);

                if (iMode >= 0 && iMode < GAMEMODE_LAST) {
                    miModeSettingsButton->setVisible(iMode != game_mode_owned && iMode != game_mode_mapmaker);

                    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
                        miGoalField[iGameMode]->setVisible(iMode == iGameMode);
                    }

                    miGoalField[iMode]->setOutputPtr(&game_values.tourstops[iEditStage]->iGoal);
                    miGoalField[iMode]->updateOutput();

                    game_values.tourstops[iEditStage]->iStageType = 0;

                    game_values.tourstops[iEditStage]->fUseSettings = true;
                    game_values.tourstops[iEditStage]->iNumUsedSettings = g_iNumGameModeSettings[iMode];
                } else {
                    //Show the settings button for boss mode
                    miModeSettingsButton->setVisible(iMode == 26);

                    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++)
                        miGoalField[iGameMode]->setVisible(false);

                    if (iMode == 24) {
                        game_values.tourstops[iEditStage]->iStageType = 1;
                        game_values.tourstops[iEditStage]->iBonusTextLines = 5;
                    } else if (iMode >= 25 && iMode <= 27) {
                        game_values.tourstops[iEditStage]->iStageType = 0;
                    }
                }

                //Removes bonuses if we went from a stage to a bonus house
                //(and there were more than the max bonuses for a house)
                AdjustBonuses(game_values.tourstops[iEditStage]);
            } else if (MENU_CODE_TO_MODE_SETTINGS_MENU == code) {
                bool fModeFound = false;
                for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
                    if (miGoalField[iGameMode]->IsVisible()) {
                        mCurrentMenu = mModeOptionsMenu->GetOptionsMenu(iGameMode);
                        mCurrentMenu->ResetMenu();
                        fModeFound = true;
                        break;
                    }
                }

                //Look to see if this is the boss mode and go to boss settings
                if (!fModeFound) {
                    if (miSpecialGoalField[1]->IsVisible()) {
                        mCurrentMenu = mModeOptionsMenu->GetBossOptionsMenu();
                        mCurrentMenu->ResetMenu();
                    }
                }
            } else if (MENU_CODE_HEALTH_MODE_START_LIFE_CHANGED == code) {
                mModeOptionsMenu->HealthModeStartLifeChanged();
            } else if (MENU_CODE_HEALTH_MODE_MAX_LIFE_CHANGED == code) {
                mModeOptionsMenu->HealthModeMaxLifeChanged();
            } else if (MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS == code) {
                mCurrentMenu = &mStageSettingsMenu;
                mCurrentMenu->ResetMenu();
            } else if (MENU_CODE_MAP_CHANGED == code) {
                game_values.tourstops[iEditStage]->pszMapFile = maplist->currentShortmapname();
                fForceStageDisplay = true;
            } else if (MENU_CODE_TO_BONUS_PICKER_MENU == code) {
                mCurrentMenu = &mBonusItemPicker;
                mCurrentMenu->ResetMenu();
            } else if (MENU_CODE_DELETE_STAGE_BUTTON == code) {
                miDeleteStageDialogImage->setVisible(true);
                miDeleteStageDialogAreYouText->setVisible(true);
                miDeleteStageDialogSureText->setVisible(true);
                miDeleteStageDialogYesButton->setVisible(true);
                miDeleteStageDialogNoButton->setVisible(true);

                EnableStageMenu(false);

                mStageSettingsMenu.RememberCurrent();

                mStageSettingsMenu.setInitialFocus(miDeleteStageDialogNoButton);
                mStageSettingsMenu.SetCancelCode(MENU_CODE_DELETE_STAGE_NO);
                mStageSettingsMenu.ResetMenu();
            } else if (MENU_CODE_DELETE_STAGE_YES == code || MENU_CODE_DELETE_STAGE_NO == code) {
                miDeleteStageDialogImage->setVisible(false);
                miDeleteStageDialogAreYouText->setVisible(false);
                miDeleteStageDialogSureText->setVisible(false);
                miDeleteStageDialogYesButton->setVisible(false);
                miDeleteStageDialogNoButton->setVisible(false);

                mStageSettingsMenu.setInitialFocus(miNameField);
                mStageSettingsMenu.SetCancelCode(MENU_CODE_EXIT_APPLICATION);
                mStageSettingsMenu.RestoreCurrent();

                //Yes was selected to delete this stage
                if (MENU_CODE_DELETE_STAGE_YES == code) {
                    //Scan the grid of stages and remove any references to this stage
                    //and decrement stage numbers greater than this stage
                    for (short iRow = 0; iRow < iWorldHeight; iRow++) {
                        for (short iCol = 0; iCol < iWorldWidth; iCol++) {
                            if (g_worldmap.getTile(iCol, iRow).iType == iEditStage + 6) {
                                g_worldmap.getTile(iCol, iRow).iType = 0;
                            } else if (g_worldmap.getTile(iCol, iRow).iType > iEditStage + 6) {
                                g_worldmap.getTile(iCol, iRow).iType--;
                            }
                        }
                    }

                    //Scan vehicles and remove references to deleted stage
                    auto itrVehicle = vehiclelist.begin();
                    while (itrVehicle != vehiclelist.end()) {
                        WorldVehicle * vehicle = itrVehicle->get();
                        if (vehicle->getActionId() == iEditStage) {
                            itrVehicle = vehiclelist.erase(itrVehicle);
                            continue;
                        } else if (vehicle->getActionId() > iEditStage) {
                            vehicle->setActionId(vehicle->getActionId() - 1);
                        }

                        itrVehicle++;
                    }

                    //Remove stage from tourstops vector
                    std::vector<TourStop*>::iterator itr = game_values.tourstops.begin(), lim = game_values.tourstops.end();

                    short iIndex = 0;
                    while (itr != lim) {
                        if (iIndex == iEditStage) {
                            delete (*itr);

                            game_values.tourstops.erase(itr);
                            g_worldmap.setNumStages(g_worldmap.getNumStages() - 1);

                            break;
                        }

                        ++itr;
                        ++iIndex;
                    }

                    iEditStage = -1;
                    mCurrentMenu = &mStageSettingsMenu;
                    mCurrentMenu->ResetMenu();
                }

                EnableStageMenu(true);
            }
        }

        drawmap(false, TILESIZE);
        menu_shade.draw(0, 0);

        int color = SDL_MapSurfaceRGB(blitdest, 0, 0, 255);

        if (iEditStage == -1) {
            for (short iStage = 0; iStage < g_worldmap.getNumStages(); iStage++) {
                short ix = (iStage % 20) << 5;
                short iy = ((iStage / 20) << 5);

                SDL_Rect r = {ix, iy, 32, 32};
                SDL_FillSurfaceRect(blitdest, &r, color);

                rm->spr_worldforegroundspecial[0].draw(ix, iy, {(iStage % 10) << 5, (iStage / 10) << 5, 32, 32});
            }

            if (iStageDisplay >= 0) {
                DisplayStageDetails(fForceStageDisplay, iStageDisplay, mouse_x, mouse_y);
                fForceStageDisplay = false;
            }

            //Display New button
            rm->spr_selectfield.draw(256, 420, {0, 0, 64, 32});
            rm->spr_selectfield.draw(320, 420, {448, 0, 64, 32});

            rm->menu_font_large.drawCentered(320, 425, "New Stage");

            rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[LMB] Select Stage, [RMB] Edit Stage, [n] New Stage");
        } else {
            mCurrentMenu->Update();
            mCurrentMenu->Draw();

            if (mCurrentMenu != &mBonusItemPicker) {
                short ix = 20;
                short iy = 20;

                SDL_Rect r = {ix, iy, 32, 32};
                SDL_FillSurfaceRect(blitdest, &r, color);

                rm->spr_worldforegroundspecial[0].draw(ix, iy, {(iEditStage % 10) << 5, (iEditStage / 10) << 5, 32, 32});
            }
        }

        if (mCurrentMenu == &mBonusItemPicker) {
            TourStop * ts = game_values.tourstops[iEditStage];

            //Game powerups
            for (short iItem = 0; iItem < NUM_POWERUPS; iItem++) {
                rm->spr_storedpoweruplarge.draw(rItemDst[iItem].x, rItemDst[iItem].y, {iItem << 5, 0, 32, 32});
            }

            //World Powerups
            for (short iWorldItem = 0; iWorldItem < NUM_WORLD_POWERUPS; iWorldItem++) {
                rm->spr_worlditems.draw(rItemDst[iWorldItem + NUM_POWERUPS].x, rItemDst[iWorldItem + NUM_POWERUPS].y, {iWorldItem << 5, 0, 32, 32});
            }

            //Score Bonuses
            if (ts->iStageType == 1) {
                for (short iScoreBonus = 0; iScoreBonus < NUM_WORLD_SCORE_BONUSES; iScoreBonus++) {
                    rm->spr_worlditems.draw(rItemDst[iScoreBonus + NUM_POWERUPS + NUM_WORLD_POWERUPS].x, rItemDst[iScoreBonus + NUM_POWERUPS + NUM_WORLD_POWERUPS].y, {iScoreBonus < 10 ? iScoreBonus << 5 : (iScoreBonus - 10) << 5, iScoreBonus < 10 ? 32 : 64, 32, 32});
                }
            }

            //Draw background container
            rm->spr_worlditempopup.draw(0, 344, {0, 0, 320, 64});
            rm->spr_worlditempopup.draw(320, 344, {192, 0, 320, 64});

            SDL_Rect * rects = rStageBonusDst;

            if (ts->iStageType == 1)
                rects = rHouseBonusDst;

            for (short iPickedItem = 0; iPickedItem < ts->iNumBonuses; iPickedItem++) {
                short iBonus = ts->wsbBonuses[iPickedItem].iBonus;
                short iPlace = ts->wsbBonuses[iPickedItem].iWinnerPlace;

                //Draw place behind bonus
                if (ts->iStageType == 0)
                    rm->spr_worlditempopup.draw(rects[iPickedItem].x - 8, rects[iPickedItem].y - 8, {iPlace * 48, 256, 48, 48});

                if (iBonus >= NUM_POWERUPS + NUM_WORLD_POWERUPS) {
                    short iBonusIndex = iBonus - NUM_POWERUPS - NUM_WORLD_POWERUPS;
                    rm->spr_worlditems.draw(rects[iPickedItem].x, rects[iPickedItem].y, {iBonusIndex < 10 ? iBonusIndex << 5 : (iBonusIndex - 10) << 5, iBonusIndex < 10 ? 32 : 64, 32, 32});
                } else if (iBonus >= NUM_POWERUPS) {
                    rm->spr_worlditems.draw(rects[iPickedItem].x, rects[iPickedItem].y, {(iBonus - NUM_POWERUPS) << 5, 0, 32, 32});
                } else {
                    rm->spr_storedpoweruplarge.draw(rects[iPickedItem].x, rects[iPickedItem].y, {iBonus << 5, 0, 32, 32});
                }
            }

            if (ts->iStageType == 0)
                rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[1-4] Select Items, [LMB] Remove Items");
            else
                rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[LMB] Select Items, [LMB] Remove Items");
        }

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());

        DrawMessage();
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

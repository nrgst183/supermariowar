#include "WorldEditorState.h"

void ReadVehiclesIntoEditor()
{
    vehiclelist.clear();

    for (const WorldVehicle& vehicle : g_worldmap.getVehicles()) {
        auto vehiclecopy = std::make_unique<WorldVehicle>();

        vehiclecopy->setDrawSprite(vehicle.getDrawSprite());
        vehiclecopy->setActionId(vehicle.getActionId());
        vehiclecopy->setCurrentTile(vehicle.getCurrentTile().x, vehicle.getCurrentTile().y);
        vehiclecopy->setMinMoves(vehicle.getMinMoves());
        vehiclecopy->setMaxMoves(vehicle.getMaxMoves());
        vehiclecopy->setSpritePacing(vehicle.isSpritePacing());
        vehiclecopy->setDrawDirection(vehicle.getDrawDirection());
        vehiclecopy->setBoundary(vehicle.getBoundary());

        vehiclelist.push_back(std::move(vehiclecopy));
    }
}

void WriteVehiclesIntoWorld()
{
    //Insert new vehicles
    std::vector<WorldVehicle> worldvehicles;
    worldvehicles.reserve(vehiclelist.size());

    for (const std::unique_ptr<WorldVehicle>& vehicle : vehiclelist) {
        WorldVehicle vehiclecopy;
        vehiclecopy.setDrawSprite(vehicle->getDrawSprite());
        vehiclecopy.setActionId(vehicle->getActionId());
        vehiclecopy.setCurrentTile(vehicle->getCurrentTile().x, vehicle->getCurrentTile().y);
        vehiclecopy.setMinMoves(vehicle->getMinMoves());
        vehiclecopy.setMaxMoves(vehicle->getMaxMoves());
        vehiclecopy.setSpritePacing(vehicle->isSpritePacing());
        vehiclecopy.setDrawDirection(vehicle->getDrawDirection());
        vehiclecopy.setBoundary(vehicle->getBoundary());
        worldvehicles.emplace_back(std::move(vehiclecopy));
    }

    g_worldmap.setVehicles(std::move(worldvehicles));
}

void AddVehicleToTile(short iCol, short iRow, short iType)
{
    auto itr = vehiclelist.begin(), lim = vehiclelist.end();
    WorldVehicle * newvehicle = nullptr;
    while (itr != lim) {
        WorldVehicle * vehicle = itr->get();
        if (vehicle->getCurrentTile().x == iCol && vehicle->getCurrentTile().y == iRow) {
            newvehicle = vehicle;
            break;
        }

        itr++;
    }

    if (!newvehicle) {
        newvehicle = vehiclelist.emplace_back(std::make_unique<WorldVehicle>()).get();
        newvehicle->setCurrentTile(iCol, iRow);
    }

    newvehicle->setDrawSprite(g_wvVehicleStamp.getDrawSprite());
    newvehicle->setActionId(g_wvVehicleStamp.getActionId());
    newvehicle->setMinMoves(g_wvVehicleStamp.getMinMoves());
    newvehicle->setMaxMoves(g_wvVehicleStamp.getMaxMoves());
    newvehicle->setSpritePacing(g_wvVehicleStamp.isSpritePacing());
    newvehicle->setDrawDirection(g_wvVehicleStamp.getDrawDirection());
    newvehicle->setBoundary(g_wvVehicleStamp.getBoundary());
}

void RemoveVehicleFromTile(short iCol, short iRow)
{
    auto itr = vehiclelist.begin(), lim = vehiclelist.end();
    while (itr != lim) {
        WorldVehicle * vehicle = itr->get();
        if (vehicle->getCurrentTile().x == iCol && vehicle->getCurrentTile().y == iRow) {
            itr = vehiclelist.erase(itr);
            lim = vehiclelist.end();

            return;
        }

        itr++;
    }
}

void ReadWarpsIntoEditor()
{
    warplist.clear();

    for (const WorldWarp& warp : g_worldmap.getWarps()) {
        warplist.push_back(std::make_unique<WorldWarp>(warp));
    }
}

void WriteWarpsIntoWorld()
{
    std::vector<WorldWarp> worldwarps;
    worldwarps.reserve(warplist.size());

    for (const std::unique_ptr<WorldWarp>& warp : warplist) {
        worldwarps.emplace_back(*warp);
    }

    g_worldmap.setWarps(std::move(worldwarps));
}

void AddWarpToTile(short iCol, short iRow, short iType)
{
    auto itr = warplist.begin(), lim = warplist.end();
    WorldWarp * newwarp = nullptr;
    while (itr != lim) {
        WorldWarp * warp = itr->get();
        if (warp->id == iType) {
            newwarp = warp;
            break;
        }

        itr++;
    }

    if (!newwarp) {
        newwarp = warplist.emplace_back(std::make_unique<WorldWarp>(iType, Vec2s(iCol, iRow), Vec2s(-1, -1))).get();
    } else {
        if (newwarp->posA.x == -1) {
            newwarp->posA = {iCol, iRow};
        } else if (newwarp->posA.x != iCol || newwarp->posA.y != iRow) {
            newwarp->posB = {iCol, iRow};
        }
    }
}

void RemoveWarpFromTile(short iCol, short iRow)
{
    auto itr = warplist.begin(), lim = warplist.end();
    while (itr != lim) {
        WorldWarp * warp = itr->get();
        if (warp->posA.x == iCol && warp->posA.y == iRow) {
            if (warp->posB.x == -1 && warp->posB.y == -1) {
                itr = warplist.erase(itr);
                lim = warplist.end();

                return;
            } else {
                warp->posA = {-1, -1};
            }
        } else if (warp->posB.x == iCol && warp->posB.y == iRow) {
            if (warp->posA.x == -1 && warp->posA.y == -1) {
                itr = warplist.erase(itr);
                lim = warplist.end();

                return;
            } else {
                warp->posB = {-1, -1};
            }
        }

        itr++;
    }
}

int editor_warp()
{
    bool done = false;

    while (!done) {
        int framestart = SDL_GetTicks();

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT: {
                    done = true;
                    break;
                }

            case SDL_EVENT_KEY_DOWN: {
                    edit_mode = 6;  //change to edit mode using warps
                    return EDITOR_EDIT;
                }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
                    short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (iButtonX >= 0 && iButtonX <= 9 && iButtonY == 0) {
                            set_tile = iButtonX;

                            edit_mode = 6;  //change to edit mode using warps

                            //The user must release the mouse button before trying to add a tile
                            ignoreclick = true;

                            return EDITOR_EDIT;
                        }
                    }

                    break;
                }

                default:
                    break;
            }
        }

        drawmap(false, TILESIZE);
        menu_shade.draw(0, 0);

        spr_warps[0].draw(0, 0, {0, 0, 320, 32});

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

int editor_vehicles()
{
    if (g_worldmap.getNumStages() <= 0) {
        SetDisplayMessage(120, "No Stages", "You need to create", "stages before you", "can create vehicles");
        return EDITOR_EDIT;
    }

    mCurrentMenu = &mVehicleMenu;
    mCurrentMenu->ResetMenu();

    bool done = false;

    miVehicleStageField->clear();

    for (short iStage = 0; iStage < g_worldmap.getNumStages(); iStage++) {
        TourStop * ts = game_values.tourstops[iStage];
        char szStageName[256];
        snprintf(szStageName, sizeof(szStageName), "(%d) %s", iStage + 1, ts->szName.c_str());

        SF_ListItem<short>& item = miVehicleStageField->add(szStageName, iStage);
        item.iconOverride = ts->iStageType == 1 ? 24 : (ts->iMode >= 1000 ? ts->iMode - 975 : ts->iMode);
    }

    miVehicleStageField->setCurrentValue(g_wvVehicleStamp.getActionId());

    while (!done) {
        int framestart = SDL_GetTicks();

        game_values.playerInput.ClearPressedKeys(1);

        MenuCodeEnum code = MENU_CODE_NONE;

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT: {
                    edit_mode = 5;
                    game_values.playerInput.ResetKeys();
                    return EDITOR_EDIT;
                }

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_V) {
                    if (!mCurrentMenu->IsModifying()) {
                            edit_mode = 5;
                            game_values.playerInput.ResetKeys();
                            return EDITOR_EDIT;
                        }
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    short iButtonX = bound_to_window_w(event.button.x);
                    short iButtonY = bound_to_window_h(event.button.y);

                if (event.button.button == SDL_BUTTON_LEFT) {
                        code = mCurrentMenu->MouseClick(iButtonX, iButtonY);
                    }

                    break;
                }

                default:
                    break;
            }

            game_values.playerInput.Update(event, 1);
        }

        if (MENU_CODE_NONE == code) {
            code = mCurrentMenu->SendInput(&game_values.playerInput);
        }

        if (MENU_CODE_EXIT_APPLICATION == code) {
            edit_mode = 5;
            game_values.playerInput.ResetKeys();
            return EDITOR_EDIT;
        } else if (MENU_CODE_VEHICLE_MIN_MOVES_CHANGED == code) {
            short iMaxMoves = miVehicleMaxMovesField->currentValue();
            if (miVehicleMinMovesField->currentValue() > iMaxMoves) {
                miVehicleMinMovesField->setCurrentValue(iMaxMoves);
            }
        } else if (MENU_CODE_VEHICLE_MAX_MOVES_CHANGED == code) {
            short iMinMoves = miVehicleMinMovesField->currentValue();
            if (miVehicleMaxMovesField->currentValue() < iMinMoves) {
                miVehicleMaxMovesField->setCurrentValue(iMinMoves);
            }
        } else if (MENU_CODE_CREATE_VEHICLE == code) {
            edit_mode = 5;
            ignoreclick = true;
            game_values.playerInput.ResetKeys();
            return EDITOR_EDIT;
        }

        drawmap(false, TILESIZE);
        menu_shade.draw(0, 0);

        mCurrentMenu->Update();
        mCurrentMenu->Draw();

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

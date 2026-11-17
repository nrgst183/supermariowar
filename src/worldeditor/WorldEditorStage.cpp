#include "WorldEditorState.h"

void DisplayStageDetails(bool fForce, short iStageId, short iMouseX, short iMouseY)
{
    TourStop * ts = game_values.tourstops[iStageId];

    //If we're pointing to a new stage or no stage at all
    if (iStageId != iOldStageId || fForce) {
        if (ts->iStageType == 1) {
            sMapThumbnail = gfxSprite();
        } else {
            if (!ts->pszMapFile.empty()) {
                sMapThumbnail = gfxSprite();

                if (maplist->findexact(ts->pszMapFile.c_str(), false)) {
                    g_map->loadMap(maplist->currentFilename(), read_type_preview);
                    sMapThumbnail = g_map->createThumbnailSurface(true);
                } else { //otherwise show a unknown map icon
                    sMapThumbnail = ImageLoader(convertPath("gfx/leveleditor/leveleditor_mapnotfound.png")).withoutColorKey().create();
                }
            }
        }
    }

    iOldStageId = iStageId;

    short iMode = ts->iMode;
    if (ts->iStageType == 1)
        iMode = 24;
    else if (ts->iMode >= 1000)
        iMode = ts->iMode - 975;  //Convert mode id from map file to internal id for special stage modes

    //Make sure we're displaying it on the screen
    if (iMouseX > 408)
        iMouseX = 408;

    if (iMode < GAMEMODE_LAST || (iMode >= 25 && iMode <= 27)) {
        if (iMouseY > 248)
            iMouseY = 248;

        spr_largedialog.draw(iMouseX, iMouseY, {0, 0, 116, 116});
        spr_largedialog.draw(iMouseX + 116, iMouseY, {140, 0, 116, 116});
        spr_largedialog.draw(iMouseX, iMouseY + 116, {0, 108, 116, 116});
        spr_largedialog.draw(iMouseX + 116, iMouseY + 116, {140, 108, 116, 116});
    } else if (iMode == 24) {
        //Make sure we're displaying it on the screen
        if (iMouseY > 392)
            iMouseY = 392;

        spr_largedialog.draw(iMouseX, iMouseY, {0, 0, 116, 44});
        spr_largedialog.draw(iMouseX + 116, iMouseY, {140, 0, 116, 44});
        spr_largedialog.draw(iMouseX, iMouseY + 44, {0, 180, 116, 44});
        spr_largedialog.draw(iMouseX + 116, iMouseY + 44, {140, 180, 116, 44});
    }

    rm->menu_mode_large.draw(iMouseX + 16, iMouseY + 16, {iMode << 5, 0, 32, 32});

    rm->menu_font_small.drawChopRight(iMouseX + 52, iMouseY + 16, 164, ts->szName.c_str());

    char szPrint[128];
    if (iMode != 24) {
        snprintf(szPrint, sizeof(szPrint), "Goal: %d", ts->iGoal);
        rm->menu_font_small.drawChopRight(iMouseX + 52, iMouseY + 34, 164, szPrint);

        snprintf(szPrint, sizeof(szPrint), "Points: %d", ts->iPoints);
        rm->menu_font_small.drawChopRight(iMouseX + 16, iMouseY + 176, 100, szPrint);

        snprintf(szPrint, sizeof(szPrint), "End: %s", ts->fEndStage ? "Yes" : "No");
        rm->menu_font_small.drawChopRight(iMouseX + 126, iMouseY + 176, 80, szPrint);

        for (short iBonus = 0; iBonus < ts->iNumBonuses; iBonus++) {
            WorldStageBonus * wsb = &ts->wsbBonuses[iBonus];
            rm->spr_worlditemsplace.draw(iMouseX + iBonus * 20 + 16, iMouseY + 194, {wsb->iWinnerPlace * 20, 0, 20, 20});

            short iBonusIcon = wsb->iBonus;
            gfxSprite * spr_icon = iBonusIcon < NUM_POWERUPS ? &rm->spr_storedpowerupsmall : &rm->spr_worlditemssmall;
            spr_icon->draw(iMouseX + iBonus * 20 + 18, iMouseY + 196, {(iBonusIcon < NUM_POWERUPS ? iBonusIcon : iBonusIcon - NUM_POWERUPS) << 4, 0, 16, 16});
        }

        if (sMapThumbnail) {
            const SDL_Rect rSrc {0, 0, 160, 120};
            const SDL_Rect rDst {iMouseX + 16, iMouseY + 52, 160, 120};
            sMapThumbnail.draw(rSrc, blitdest, rDst);
        }
    } else {
        snprintf(szPrint, sizeof(szPrint), "Sort: %s", ts->iBonusType == 0 ? "Fixed" : "Random");
        rm->menu_font_small.drawChopRight(iMouseX + 52, iMouseY + 34, 164, szPrint);

        for (short iBonus = 0; iBonus < ts->iNumBonuses; iBonus++) {
            short iBonusIcon = ts->wsbBonuses[iBonus].iBonus;
            gfxSprite * spr_icon = nullptr;
            short iSrcX = 0, iSrcY = 0;

            if (iBonusIcon < NUM_POWERUPS) {
                spr_icon = &rm->spr_storedpowerupsmall;
                iSrcX = iBonusIcon << 4;
                iSrcY = 0;
            } else if (iBonusIcon < NUM_POWERUPS + NUM_WORLD_POWERUPS) {
                spr_icon = &rm->spr_worlditemssmall;
                iSrcX = (iBonusIcon - NUM_POWERUPS) << 4;
                iSrcY = 0;
            } else if (iBonusIcon < NUM_POWERUPS + NUM_WORLD_POWERUPS + 10) {
                spr_icon = &rm->spr_worlditemssmall;
                iSrcX = (iBonusIcon - NUM_POWERUPS - NUM_WORLD_POWERUPS) << 4;
                iSrcY = 16;
            } else {
                spr_icon = &rm->spr_worlditemssmall;
                iSrcX = (iBonusIcon - NUM_POWERUPS - NUM_WORLD_POWERUPS - 10) << 4;
                iSrcY = 32;
            }

            spr_icon->draw(iMouseX + iBonus * 20 + 18, iMouseY + 52, {iSrcX, iSrcY, 16, 16});
        }
    }
}

int g_iNumGameModeSettings[GAMEMODE_LAST] = {2,2,3,4,3,10,9,6,2,1,3,5,3,3,0,22,6,4,3,4,4,3};

SDL_Rect rItemDst[NUM_WORLD_ITEMS];

void SetBonusString(std::string& szString, short iPlace, short iItem, short iStageType)
{
    char cType = 'p';

    if (iItem >= NUM_POWERUPS + NUM_WORLD_POWERUPS) {
        cType = 's';
        iItem -= NUM_POWERUPS + NUM_WORLD_POWERUPS;
    } else if (iItem >= NUM_POWERUPS) {
        cType = 'w';
        iItem -= NUM_POWERUPS;
    }

    char buf[32];
    if (iStageType == 0) {
        snprintf(buf, sizeof(buf), "%d%c%d", iPlace, cType, iItem);
    } else {
        snprintf(buf, sizeof(buf), "%c%d", cType, iItem);
    }
    szString = buf;
}

void TestAndSetBonusItem(TourStop * ts, short iPlace, short iButtonX, short iButtonY)
{
    short iMaxBonusesAllowed = 10;

    if (ts->iStageType == 1)
        iMaxBonusesAllowed = MAX_BONUS_CHESTS;

    if (ts->iNumBonuses < iMaxBonusesAllowed) {
        short iNumSelectableItems = NUM_WORLD_ITEMS;
        if (ts->iStageType == 0)
            iNumSelectableItems = NUM_POWERUPS + NUM_WORLD_POWERUPS;

        for (short iItem = 0; iItem < iNumSelectableItems; iItem++) {
            if (iButtonX >= rItemDst[iItem].x && iButtonX < rItemDst[iItem].w + rItemDst[iItem].x &&
                    iButtonY >= rItemDst[iItem].y && iButtonY < rItemDst[iItem].h + rItemDst[iItem].y) {
                //If this is a normal stage, then alert the player that they need to select the place for this item
                if (ts->iStageType == 0 && iPlace == 0) {
                    SetDisplayMessage(120, "Use Number Keys", "Hover over item", "use keys 1 to 4", "to select bonus");
                    return;
                }

                //Set the bonus item for the place selected
                ts->wsbBonuses[ts->iNumBonuses].iBonus = iItem;
                ts->wsbBonuses[ts->iNumBonuses].iWinnerPlace = iPlace - 1;

                SetBonusString(ts->wsbBonuses[ts->iNumBonuses].szBonusString, iPlace, iItem, ts->iStageType);

                ts->iNumBonuses++;

                break;
            }
        }
    }
}

static short iLastStageType = 0;

void AdjustBonuses(TourStop * ts)
{
    //No need to do anything if we were a stage and we're still a stage
    //(or a house and still a house)
    if (ts->iStageType == iLastStageType)
        return;

    if (ts->iStageType == 0) {
        //Remove any score bonuses
        for (short iBonus = 0; iBonus < ts->iNumBonuses; iBonus++) {
            if (ts->wsbBonuses[iBonus].iBonus >= NUM_POWERUPS + NUM_WORLD_POWERUPS) {
                ts->iNumBonuses--;
                for (short iRemoveBonus = iBonus; iRemoveBonus < ts->iNumBonuses; iRemoveBonus++) {
                    ts->wsbBonuses[iRemoveBonus].iBonus = ts->wsbBonuses[iRemoveBonus + 1].iBonus;
                    ts->wsbBonuses[iRemoveBonus].iWinnerPlace = ts->wsbBonuses[iRemoveBonus + 1].iWinnerPlace;
                    ts->wsbBonuses[iRemoveBonus].szBonusString = ts->wsbBonuses[iRemoveBonus + 1].szBonusString;
                }
            }
        }

        //Add places to bonuses
        for (short iBonus = 0; iBonus < ts->iNumBonuses; iBonus++) {
            if (ts->wsbBonuses[iBonus].iWinnerPlace < 0 || ts->wsbBonuses[iBonus].iWinnerPlace > 3)
                ts->wsbBonuses[iBonus].iWinnerPlace = 0;

            SetBonusString(ts->wsbBonuses[iBonus].szBonusString, ts->wsbBonuses[iBonus].iWinnerPlace + 1, ts->wsbBonuses[iBonus].iBonus, ts->iStageType);
        }
    } else if (ts->iStageType == 1) {
        //Cap the number of
        if (ts->iNumBonuses > MAX_BONUS_CHESTS)
            ts->iNumBonuses = MAX_BONUS_CHESTS;

        for (short iBonus = 0; iBonus < ts->iNumBonuses; iBonus++) {
            SetBonusString(ts->wsbBonuses[iBonus].szBonusString, 0, ts->wsbBonuses[iBonus].iBonus, ts->iStageType);
        }
    }

    iLastStageType = ts->iStageType;
}

void SaveStage(short iEditStage)
{
    if (game_values.tourstops[iEditStage]->iMode >= 25 && game_values.tourstops[iEditStage]->iMode <= 27)
        game_values.tourstops[iEditStage]->iMode += 975;

    //Set the number of game mode settings to the maximum so we write them all out
    game_values.tourstops[iEditStage]->fUseSettings = true;
    game_values.tourstops[iEditStage]->iNumUsedSettings = serializeGMS(game_values.tourstops[iEditStage]->iMode, game_values.gamemodemenusettings).size();

    //Copy the working values back into the structure that will be saved
    memcpy(&game_values.tourstops[iEditStage]->gmsSettings, &game_values.gamemodemenusettings, sizeof(GameModeSettings));
}

void EditStage(short iEditStage)
{
    memcpy(&game_values.gamemodemenusettings, &game_values.tourstops[iEditStage]->gmsSettings, sizeof(GameModeSettings));

    //Set fields to write data to the selected stage
    TourStop * ts = game_values.tourstops[iEditStage];

    miModeField->setOutputPtr(&ts->iMode);
    miNameField->SetData(ts->szName, 128);

    miPointsField->setOutputPtr(&ts->iPoints);
    miFinalStageField->setOutputPtr(&ts->fEndStage);

    if (ts->pszMapFile.empty())
        ts->pszMapFile = maplist->currentShortmapname();

    miMapField->SetMap(ts->pszMapFile.c_str(), false);

    miBonusType->setOutputPtr(&ts->iBonusType);
    miBonusTextField[0]->SetData(ts->szBonusText[0], 128);
    miBonusTextField[1]->SetData(ts->szBonusText[1], 128);
    miBonusTextField[2]->SetData(ts->szBonusText[2], 128);
    miBonusTextField[3]->SetData(ts->szBonusText[3], 128);
    miBonusTextField[4]->SetData(ts->szBonusText[4], 128);

    ts->iBonusTextLines = 5;

    short iMode = game_values.tourstops[iEditStage]->iMode;
    short iStageType = game_values.tourstops[iEditStage]->iStageType;

    if (iMode >= 25 && iMode <= 27)
        iMode += 975;

    //Show fields applicable for this mode
    miPointsField->setVisible(iStageType == 0);
    miFinalStageField->setVisible(iStageType == 0);
    miMapField->setVisible(iStageType == 0);

    miBonusType->setVisible(iStageType == 1);
    miBonusTextField[0]->setVisible(iStageType == 1);
    miBonusTextField[1]->setVisible(iStageType == 1);
    miBonusTextField[2]->setVisible(iStageType == 1);
    miBonusTextField[3]->setVisible(iStageType == 1);
    miBonusTextField[4]->setVisible(iStageType == 1);

    miSpecialGoalField[0]->setVisible(iMode == 1000);
    miSpecialGoalField[1]->setVisible(iMode == 1001);
    miSpecialGoalField[2]->setVisible(iMode == 1002);

    miBonusItemsButton->SetPosition(430, iStageType == 0 ? 220 : 340);

    if (iStageType == 0 && iMode >= 0 && iMode < GAMEMODE_LAST) {
        miModeField->setCurrentValue(iMode);

        miModeSettingsButton->setVisible(iMode != game_mode_owned);

        for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
            miGoalField[iGameMode]->setVisible(iMode == iGameMode);
        }

        miGoalField[iMode]->setOutputPtr(&game_values.tourstops[iEditStage]->iGoal);
        miGoalField[iMode]->setCurrentValue(game_values.tourstops[iEditStage]->iGoal);
        miPointsField->setCurrentValue(game_values.tourstops[iEditStage]->iPoints);
        miFinalStageField->setCurrentValue(game_values.tourstops[iEditStage]->fEndStage ? 1 : 0);

        game_values.tourstops[iEditStage]->fUseSettings = true;
        game_values.tourstops[iEditStage]->iNumUsedSettings = g_iNumGameModeSettings[iMode];
    } else {
        //Show the settings button for boss mode
        miModeSettingsButton->setVisible(iMode == 1001);

        for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
            miGoalField[iGameMode]->setVisible(false);
        }

        if (iStageType == 1) { //Bonus House
            miModeField->setCurrentValue(24);
        } else if (iMode >= 1000 && iMode <= 1002) { //Pipe, Boss and Boxes Game
            miModeField->setCurrentValue(iMode - 975);
            miSpecialGoalField[iMode-1000]->setOutputPtr(&game_values.tourstops[iEditStage]->iGoal);
            miSpecialGoalField[iMode-1000]->setCurrentValue(game_values.tourstops[iEditStage]->iGoal);
            miPointsField->setCurrentValue(game_values.tourstops[iEditStage]->iPoints);
            miFinalStageField->setCurrentValue(game_values.tourstops[iEditStage]->fEndStage ? 1 : 0);

            //Have to set this back again since the setCurrentValue() above will set it to 25
            game_values.tourstops[iEditStage]->iMode = iMode;
        }
    }
}

void EnableStageMenu(bool fEnable)
{
    miNameField->Disable(!fEnable);
    miModeField->Disable(!fEnable);

    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++)
        miGoalField[iGameMode]->Disable(!fEnable);

    miSpecialGoalField[0]->Disable(!fEnable);
    miSpecialGoalField[1]->Disable(!fEnable);
    miSpecialGoalField[2]->Disable(!fEnable);

    miModeSettingsButton->Disable(!fEnable);

    miPointsField->Disable(!fEnable);
    miFinalStageField->Disable(!fEnable);
    miMapField->Disable(!fEnable);
    miBonusItemsButton->Disable(!fEnable);
    miBonusType->Disable(!fEnable);
    miBonusTextField[0]->Disable(!fEnable);
    miBonusTextField[1]->Disable(!fEnable);
    miBonusTextField[2]->Disable(!fEnable);
    miBonusTextField[3]->Disable(!fEnable);
    miBonusTextField[4]->Disable(!fEnable);
    miDeleteStageButton->Disable(!fEnable);
}

void NewStage(short * iEditStage)
{
    TourStop * ts = new TourStop();

    ts->iStageType = 0;

    ts->szBonusText[0].clear();
    ts->szBonusText[1].clear();
    ts->szBonusText[2].clear();
    ts->szBonusText[3].clear();
    ts->szBonusText[4].clear();

    ts->pszMapFile = maplist->currentShortmapname();
    ts->iMode = 0;

    ts->fUseSettings = true;
    ts->iNumUsedSettings = g_iNumGameModeSettings[0];

    ts->iGoal = 10;
    ts->iPoints = 1;

    ts->iBonusType = 0;
    ts->iNumBonuses = 0;

    ts->szName = "Tour Stop " + std::to_string(game_values.tourstops.size() + 1);

    ts->fEndStage = false;

    //Copy in default values first
    memcpy(&ts->gmsSettings, &game_values.gamemodemenusettings, sizeof(GameModeSettings));

    game_values.tourstops.push_back(ts);
    g_worldmap.setNumStages(g_worldmap.getNumStages() + 1);

    *iEditStage = game_values.tourstops.size() - 1;
    EditStage(*iEditStage);
}

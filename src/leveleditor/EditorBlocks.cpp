#include "LevelEditorState.h"

short * GetBlockProperty(short x, short y, short iBlockCol, short iBlockRow, short * iSettingIndex)
{
    for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++) {
        short ix = (iSetting % 6) * 100 + 35;
        short iy = (iSetting / 6) * 62 + 65;

        if (x >= ix - 10 && x < ix + 80 && y >= iy - 10 && y < iy + 42) {
            if (iSettingIndex)
                *iSettingIndex = iSetting;

            return &g_map->objectdata[iBlockCol][iBlockRow].iSettings[iSetting];
        }
    }

    return nullptr;
}

namespace {
short iBlockType;
bool editor_properties_initialized = false;
} // namespace

// The ? block screen is showing (its loop runs inside the map's: the game's overlay asks, see LevelShell.h)
bool g_inBlockProperties = false;
void init_editor_properties(short iBlockCol, short iBlockRow)
{
    if (editor_properties_initialized)
        return;

    iBlockType = g_map->objectdata[iBlockCol][iBlockRow].iType;

    editor_properties_initialized = true;
}

int editor_properties(short iBlockCol, short iBlockRow)
{
    init_editor_properties(iBlockCol, iBlockRow);
    struct Showing {
        Showing() { g_inBlockProperties = true; }
        ~Showing() { g_inBlockProperties = false; }
    } showing;
    while (true)
    {
        FPSLimiter::instance().frameStart();
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_K) {
                    editor_properties_initialized = false;
                    return EDITOR_EDIT;
                } else if ((iBlockType == 1 || iBlockType == 15) && ((event.key.key >= SDLK_0 && event.key.key <= SDLK_9) || event.key.key == SDLK_GRAVE || event.key.key == SDLK_D)) {
                        short iSettingIndex = 0;
                        short * piSetting = GetBlockProperty(mouse_x, mouse_y, iBlockCol, iBlockRow, &iSettingIndex);

                        //If shift is held, set all powerups to this setting
                        short iValue = event.key.key - SDLK_0;

                        if (event.key.key == SDLK_0)
                            iValue = 10;
                        else if (event.key.key == SDLK_GRAVE)
                            iValue = 0;
                        else if (event.key.key == SDLK_D)
                            iValue = defaultPowerupSetting(0, iSettingIndex);

                    const bool* keystate = SDL_GetKeyboardState(nullptr);
                    if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT)) {
                        for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++) {
                                if (event.key.key == SDLK_D)
                                iValue = defaultPowerupSetting(0, iSetting);

                                g_map->objectdata[iBlockCol][iBlockRow].iSettings[iSetting] = iValue;
                            }
                    } else if (piSetting) {
                            *piSetting = iValue;
                        }
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    short iHiddenCheckboxY = 0;
                if (iBlockType == 1 || iBlockType == 15) {
                        short iSettingIndex;
                        short * piSetting = GetBlockProperty(event.button.x, event.button.y, iBlockCol, iBlockRow, &iSettingIndex);

                    if (piSetting) {
                            Uint8 iMouseState = SDL_GetMouseState(nullptr, nullptr);

                            if ((event.button.button == SDL_BUTTON_RIGHT && (iMouseState & SDL_BUTTON_LMASK)) ||
                                (event.button.button == SDL_BUTTON_LEFT && (iMouseState & SDL_BUTTON_RMASK))) {
                                                            *piSetting = defaultPowerupSetting(0, iSettingIndex);
                        } else if (event.button.button == SDL_BUTTON_LEFT) {
                                if (*piSetting < 10)
                                    (*piSetting)++;
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                                if (*piSetting > 0)
                                    (*piSetting)--;
                            }
                        }

                        iHiddenCheckboxY = 365;
                } else if (iBlockType == 4 || iBlockType == 5 || iBlockType == 17 || iBlockType == 18 || iBlockType == 3) {
                        iHiddenCheckboxY = 214;
                    }

                if (event.button.x >= 270 && event.button.x < 370 && event.button.y >= iHiddenCheckboxY && event.button.y < iHiddenCheckboxY + 52) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                            g_map->objectdata[iBlockCol][iBlockRow].fHidden = !g_map->objectdata[iBlockCol][iBlockRow].fHidden;
                        }
                    }

                if (iBlockType == 1 || iBlockType == 15) {
                    if (event.button.x >= 390 && event.button.x < 490 && event.button.y >= iHiddenCheckboxY && event.button.y < iHiddenCheckboxY + 52) {
                        if (event.button.button == SDL_BUTTON_LEFT) {
                                if (g_map->objectdata[iBlockCol][iBlockRow].iSettings[0] >= 0)
                                    g_map->objectdata[iBlockCol][iBlockRow].iSettings[0] = -1;
                                else
                                                                    g_map->objectdata[iBlockCol][iBlockRow].iSettings[0] = defaultPowerupSetting(0, 0);
                            }
                        }
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_MOTION: {
                bound_mouse_motion_coords();
                break;
                }

                default:
                    break;
            }
        }

        drawmap(false, TILESIZE);
        rm->menu_shade.draw(0, 0);

        short iHiddenCheckboxY = 0;

        if (iBlockType == 1 || iBlockType == 15) {
            bool fUseGame = g_map->objectdata[iBlockCol][iBlockRow].iSettings[0] == -1;

            for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++) {
                short ix = (iSetting % 6) * 100 + 35;
                short iy = (iSetting / 6) * 62 + 65;

                if (mouse_x >= ix - 10 && mouse_x < ix + 80 && mouse_y >= iy - 10 && mouse_y < iy + 42 && !fUseGame)
                    rm->spr_powerupselector.draw(ix - 10, iy - 10, {0, 0, 90, 52});
                else
                    rm->spr_powerupselector.draw(ix - 10, iy - 10, {0, 52, 90, 52});

                rm->spr_powerups.draw(ix, iy, {iSetting << 5, 0, 32, 32});

                char szNum[8];
                if (fUseGame)
                    snprintf(szNum, sizeof(szNum), "X");
                else
                    snprintf(szNum, sizeof(szNum), "%d", g_map->objectdata[iBlockCol][iBlockRow].iSettings[iSetting]);

                rm->menu_font_large.drawCentered(ix + 55, iy + 5, szNum);
            }

            iHiddenCheckboxY = 365;

            if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight() * 3, "Block Property Mode");
            if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight() * 2, "[~, 0-9] Set Value [LMB] Increase [RMB] Decrease [D] Default");
            if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight(), "[Shift] + [0-9 or D] Set All To Value");
        } else if (iBlockType == 4 || iBlockType == 5 || iBlockType == 17 || iBlockType == 18 || iBlockType == 3) {
            if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight(), "Block Property Mode");
            iHiddenCheckboxY = 214;
        }

        if (mouse_x >= 270 && mouse_x < 370 && mouse_y >= iHiddenCheckboxY && mouse_y < iHiddenCheckboxY + 52)
            rm->spr_powerupselector.draw(270, iHiddenCheckboxY, {90, 0, 100, 52});
        else
            rm->spr_powerupselector.draw(270, iHiddenCheckboxY, {90, 52, 100, 52});

        rm->menu_font_large.drawCentered(320, iHiddenCheckboxY + 3, "Hidden");

        if (g_map->objectdata[iBlockCol][iBlockRow].fHidden)
            rm->spr_hidden_marker.draw(310, iHiddenCheckboxY + 27);

        //Display "Use Game" option
        if (iBlockType == 1 || iBlockType == 15) {
            if (mouse_x >= 390 && mouse_x < 490 && mouse_y >= iHiddenCheckboxY && mouse_y < iHiddenCheckboxY + 52)
                rm->spr_powerupselector.draw(390, iHiddenCheckboxY, {90, 0, 100, 52});
            else
                rm->spr_powerupselector.draw(390, iHiddenCheckboxY, {90, 52, 100, 52});

            rm->menu_font_large.drawCentered(440, iHiddenCheckboxY + 3, "Use Game");

            if (g_map->objectdata[iBlockCol][iBlockRow].iSettings[0] == -1)
                rm->spr_hidden_marker.draw(430, iHiddenCheckboxY + 27);
        }

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        FPSLimiter::instance().beforeFlip();
        gfx_flipscreen();
        FPSLimiter::instance().afterFlip();
    }
}

int editor_blocks()
{
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

                case SDL_EVENT_KEY_DOWN:
                    edit_mode = 0;
                    return EDITOR_EDIT;
                break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                        short set_block_x = event.button.x / TILESIZE;
                        short set_block_y = event.button.y / TILESIZE;

                        //Set the selected block to one of the interaction blocks
                        if (set_block_y == 0 && set_block_x >= 0 && set_block_x <= 6)
                            set_block = set_block_x;
                        else if (set_block_y == 0 && set_block_x >= 7 && set_block_x <= 11)
                            set_block = set_block_x + 8;
                    else if (set_block_y >= 1 && set_block_y <= 2 && set_block_x >= 0 && set_block_x <= 3) {
                        //set the selected block to an on/off switch block

                            set_block = set_block_x + 7;
                            g_map->iSwitches[set_block_x] = 2 - set_block_y;
                    } else if (set_block_y >= 1 && set_block_y <= 2 && set_block_x >= 4 && set_block_x <= 7) {
                        //set the selected block to a switch block

                            set_block = set_block_x + 7;
                            set_block_switch_on = set_block_y == 1;
                    } else if (set_block_y == 3 && set_block_x >= 0 && set_block_x <= 9) {
                        //set the selected block to a weapon breakable block

                            set_block = set_block_x + 20;
                        }

                        edit_mode = 0;

                        //The user must release the mouse button before trying to add a tile
                        ignoreclick = true;
                        return EDITOR_EDIT;
                    }
                break;

                default:
                    break;
            }
        }

        drawmap(false, TILESIZE);
        rm->menu_shade.draw(0, 0);

        SDL_Rect rSrc = {0, 0, 224, 32};
        SDL_Rect rDst = {0, 0, 224, 32};

        rm->spr_blocks[0].draw(rSrc, screen, rDst);

        SDL_Rect rOnOffSrc = {224, 0, 128, 64};
        SDL_Rect rOnOffDst = {0, 32, 128, 64};

        rm->spr_blocks[0].draw(rOnOffSrc, screen, rOnOffDst);

        SDL_Rect rOnOffBlockSrc = {352, 0, 128, 64};
        SDL_Rect rOnOffBlockDst = {128, 32, 128, 64};

        rm->spr_blocks[0].draw(rOnOffBlockSrc, screen, rOnOffBlockDst);

        SDL_Rect rBlocksRow2Src = {0, 32, 160, 32};
        SDL_Rect rBlocksRow2Dst = {224, 0, 160, 32};

        rm->spr_blocks[0].draw(rBlocksRow2Src, screen, rBlocksRow2Dst);

        SDL_Rect rBlocksRow3Src = {0, 64, 320, 32};
        SDL_Rect rBlocksRow3Dst = {0, 96, 320, 32};

        rm->spr_blocks[0].draw(rBlocksRow3Src, screen, rBlocksRow3Dst);

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_BLOCKS;
}

int editor_mapitems()
{
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

                case SDL_EVENT_KEY_DOWN:
                    edit_mode = 7;
                    return EDITOR_EDIT;
                break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                        short set_item_x = event.button.x / TILESIZE;
                        short set_item_y = event.button.y / TILESIZE;

                        //Set the selected block to one of the interaction blocks
                    if (set_item_y == 0 && set_item_x >= 0 && set_item_x <= 5) {
                            set_mapitem = set_item_x;

                            edit_mode = 7;

                            //The user must release the mouse button before trying to add a tile
                            ignoreclick = true;
                            return EDITOR_EDIT;
                        }
                    }
                break;

                default:
                    break;
            }
        }

        drawmap(false, TILESIZE);
        rm->menu_shade.draw(0, 0);

        rm->spr_mapitems[0].draw(0, 0, {0, 0, 192, 32});

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());
        if (OLD_HUD) rm->menu_font_small.drawRightJustified(0, 480 - rm->menu_font_small.getHeight(), "Map Items");

        DrawMessage();
        return EDITOR_MAPITEMS;
}

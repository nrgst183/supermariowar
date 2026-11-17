#include "LevelEditorState.h"

namespace {
short modeitemmode = 0;
short dragmodeitem = -1;
short dragoffsetx = 0;
short dragoffsety = 0;

bool editor_modeitems_initialized = false;
void init_editor_modeitems()
{
    if (editor_modeitems_initialized)
        return;

    modeitemmode = 0;
    dragmodeitem = -1;
    dragoffsetx = 0;
    dragoffsety = 0;

    editor_modeitems_initialized = true;
}
} // namespace

int editor_modeitems()
{
    init_editor_modeitems();

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                    dragmodeitem = -1;

                if (event.key.key == SDLK_S) {
                        savecurrentmap();
                } else if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_J) {
                    editor_modeitems_initialized = false;
                    return EDITOR_EDIT;
                } else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_2) {
                        modeitemmode = event.key.key - SDLK_1;
                } else if (event.key.key == SDLK_R) {
                        //Set this mode item set to random
                    if (modeitemmode == 0) {
                            if (g_map->iNumRaceGoals == 0)
                                g_map->iNumRaceGoals = MAXRACEGOALS;
                            else
                                g_map->iNumRaceGoals = 0;
                    } else if (modeitemmode == 1) {
                            if (g_map->iNumFlagBases == 0)
                                g_map->iNumFlagBases = 4;
                            else
                                g_map->iNumFlagBases = 0;
                        }
                    }
                }

            [[fallthrough]];
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                        short iMouseX = event.button.x;
                        short iMouseY = event.button.y;

                        dragmodeitem = -1;
                    if (modeitemmode == 0) {
                        for (short iGoal = 0; iGoal < g_map->iNumRaceGoals; iGoal++) {
                                if (iMouseX >= g_map->racegoallocations[iGoal].x && iMouseX < g_map->racegoallocations[iGoal].x + 36 &&
                                    iMouseY >= g_map->racegoallocations[iGoal].y && iMouseY < g_map->racegoallocations[iGoal].y + 36) {
                                    dragmodeitem = iGoal;
                                    dragoffsetx = iMouseX - g_map->racegoallocations[iGoal].x;
                                    dragoffsety = iMouseY - g_map->racegoallocations[iGoal].y;
                                }
                            }
                    } else if (modeitemmode == 1) {
                        for (short iBase = 0; iBase < g_map->iNumFlagBases; iBase++) {
                                if (iMouseX >= g_map->flagbaselocations[iBase].x && iMouseX < g_map->flagbaselocations[iBase].x + 32 &&
                                    iMouseY >= g_map->flagbaselocations[iBase].y && iMouseY < g_map->flagbaselocations[iBase].y + 32) {
                                    dragmodeitem = iBase;
                                    dragoffsetx = iMouseX - g_map->flagbaselocations[iBase].x;
                                    dragoffsety = iMouseY - g_map->flagbaselocations[iBase].y;
                                }
                            }
                        }

                        //Move mode items around with mouse dragging
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                        dragmodeitem = -1;
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_MOTION: {
                bound_mouse_motion_coords();
                if (dragmodeitem >= 0 && event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
                    const bool* keystate = SDL_GetKeyboardState(nullptr);
                        bool fShiftDown = CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT);

                    if (modeitemmode == 0) {
                            g_map->racegoallocations[dragmodeitem].x = event.motion.x - dragoffsetx;
                            g_map->racegoallocations[dragmodeitem].y = event.motion.y - dragoffsety;

                        if (fShiftDown) {
                                g_map->racegoallocations[dragmodeitem].x = int(event.motion.x / 32.f) * 32.f;
                                g_map->racegoallocations[dragmodeitem].y = int(event.motion.y / 32.f) * 32.f;
                            }
                    } else if (modeitemmode == 1) {
                            g_map->flagbaselocations[dragmodeitem].x = event.motion.x - dragoffsetx;
                            g_map->flagbaselocations[dragmodeitem].y = event.motion.y - dragoffsety;

                        if (fShiftDown) {
                                g_map->flagbaselocations[dragmodeitem].x = int(event.motion.x / 32.f) * 32.f;
                                g_map->flagbaselocations[dragmodeitem].y = int(event.motion.y / 32.f) * 32.f;
                            }
                        }
                    }
                }

                default:
                    break;
            }
        }

        drawmap(false, TILESIZE);
        rm->menu_shade.draw(0, 0);

        //rm->spr_mapitems[0].draw(0, 0, 0, 0, 96, 32);

        //draw race goals
        if (modeitemmode == 0) {
            if (g_map->iNumRaceGoals == 0) {
                rm->menu_font_large.drawCentered(320, 200, "Race goals are set to random.");
                rm->menu_font_large.drawCentered(320, 220, "Press 'R' to manually set them.");
            } else {
                for (short iGoal = 0; iGoal < g_map->iNumRaceGoals; iGoal++) {
                    rm->spr_racegoals.draw(g_map->racegoallocations[iGoal].x - 16, g_map->racegoallocations[iGoal].y - 18, {0, 0, 68, 54});
                    char szNum[16];
                    snprintf(szNum, sizeof(szNum), "%d", iGoal + 1);
                    rm->menu_font_large.drawCentered(g_map->racegoallocations[iGoal].x + 18, g_map->racegoallocations[iGoal].y + 6, szNum);
                }
            }

            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Set Race Goal Locations - Press [2] for Flag Bases");
        } else if (modeitemmode == 1) {
            if (g_map->iNumFlagBases == 0) {
                rm->menu_font_large.drawCentered(320, 200, "Flag bases are set to random.");
                rm->menu_font_large.drawCentered(320, 220, "Press 'R' to manually set them.");
            } else {
                for (short iBase = 0; iBase < g_map->iNumFlagBases; iBase++) {
                    rm->spr_flagbases.draw(g_map->flagbaselocations[iBase].x - 8, g_map->flagbaselocations[iBase].y - 8, {iBase * 48, 0, 48, 48});
                }
            }

            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Set Flag Base Locations - Press [1] for Race Goals");
        }

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_MODEITEMS;
}

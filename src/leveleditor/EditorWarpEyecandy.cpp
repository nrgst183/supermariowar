#include "LevelEditorState.h"

int editor_warp()
{
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

                case SDL_EVENT_KEY_DOWN:
                    edit_mode = 2;  //change to edit mode using warps
                    return EDITOR_EDIT;
                break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (event.button.x / TILESIZE < 10 && event.button.y / TILESIZE < 4) {
                            set_direction = event.button.y / TILESIZE;
                            set_connection = event.button.x / TILESIZE;
                        }

                        edit_mode = 2;  //change to edit mode using warps

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

        r.x = 0;
        r.y = 0;
        r.w = 640;
        r.h = 480;

        rm->spr_warps[0].draw(screen, r);
        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_WARP;
}

namespace {
constexpr int NUM_EYECANDY = 7;
const char * szEyecandyNames[NUM_EYECANDY] = {"Clouds", "Ghosts", "Leaves", "Snow", "Fish", "Rain", "Bubbles"};
const char * szLayerNames[3] = {"Back Layer", "Mid Layer", "Top Layer"};
} // namespace
int editor_eyecandy()
{
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_E) {
                        return EDITOR_EDIT;
                    }
                }

            [[fallthrough]];
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                        short ix = 165;
                    for (short iLayer = 0; iLayer < 3; iLayer++) {
                        for (short k = 0; k < NUM_EYECANDY; k++) {
                                if (event.button.x >= ix && event.button.x < ix + 90 &&
                                    event.button.y >= k * 65 + 20 && event.button.y < k * 65 + 72) {
                                    short mask = 1 << k;
                                    if (g_map->eyecandy[iLayer] & mask)
                                        g_map->eyecandy[iLayer] &= ~mask;
                                    else
                                        g_map->eyecandy[iLayer] |= mask;
                                }
                            }

                            ix += 110;
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

        short ix = 165;
        for (short iLayer = 0; iLayer < 3; iLayer++) {
            for (short k = 0; k < NUM_EYECANDY; k++) {
                short iy = k * 65 + 20;

                if (mouse_x >= ix && mouse_x < ix + 90 && mouse_y >= iy && mouse_y < iy + 52)
                    rm->spr_powerupselector.draw(ix, iy, {0, 0, 90, 52});
                else
                    rm->spr_powerupselector.draw(ix, iy, {0, 52, 90, 52});

                rm->spr_eyecandy.draw(ix + 10, iy + 10, {k << 5, 0, 32, 32});

                short mask = 1 << k;
                if (g_map->eyecandy[iLayer] & mask)
                    rm->spr_hidden_marker.draw(ix + 57, iy + 16);

                //rm->menu_font_small.drawCentered(320, iy - rm->menu_font_small.getHeight() + 7, szEyecandyNames[k]);
            }

            rm->menu_font_small.drawCentered(ix + 45, 10, szLayerNames[iLayer]);
            ix += 110;
        }

        for (short k = 0; k < NUM_EYECANDY; k++) {
            //rm->menu_font_small.drawRightJustified(160, k * 65 + 40, szEyecandyNames[k]);
            rm->menu_font_small.drawCentered(320, k * 65 + 62, szEyecandyNames[k]);
        }

        if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight(), "Eyecandy: [e] Exit, [LMB] Choose Eyecandy");
        //rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_EYECANDY;
}

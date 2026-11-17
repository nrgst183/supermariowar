#include "LevelEditorState.h"

namespace {
CTileset * tileset = nullptr;

bool editor_tiles_initialized = false;
void init_editor_tiles()
{
    if (editor_tiles_initialized)
        return;

    set_tile_drag = false;
    view_tileset_repeat_direction = -1;
    view_tileset_repeat_timer = 0;
    tileset = g_tilesetmanager->tileset(set_tile_tileset);

    editor_tiles_initialized = true;
}
} // namespace

int editor_tiles()
{
    init_editor_tiles();

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (!set_tile_drag) {
                    if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9 && event.key.key < SDLK_1 + g_tilesetmanager->count()) {
                            set_tile_tileset = event.key.key - SDLK_1;
                        tileset = g_tilesetmanager->tileset(set_tile_tileset);
                            view_tileset_x = 0;
                            view_tileset_y = 0;
                    } else if (event.key.key == SDLK_PAGEUP) {
                        if (set_tile_tileset > 0) {
                                set_tile_tileset--;
                            tileset = g_tilesetmanager->tileset(set_tile_tileset);
                                view_tileset_x = 0;
                                view_tileset_y = 0;
                            }
                    } else if (event.key.key == SDLK_PAGEDOWN) {
                        if (set_tile_tileset < static_cast<int>(g_tilesetmanager->count()) - 1) {
                                set_tile_tileset++;
                            tileset = g_tilesetmanager->tileset(set_tile_tileset);
                                view_tileset_x = 0;
                                view_tileset_y = 0;
                            }
                    } else if (event.key.key == SDLK_UP) {
                        if (view_tileset_y > 0) {
                                view_tileset_y--;
                                view_tileset_repeat_direction = 0;
                                view_tileset_repeat_timer = 30;
                            }
                    } else if (event.key.key == SDLK_DOWN) {
                        if (view_tileset_y < g_tilesetmanager->tileset(set_tile_tileset)->height() - 15) {
                                view_tileset_y++;
                                view_tileset_repeat_direction = 1;
                                view_tileset_repeat_timer = 30;
                            }
                    } else if (event.key.key == SDLK_LEFT) {
                        if (view_tileset_x > 0) {
                                view_tileset_x--;
                                view_tileset_repeat_direction = 2;
                                view_tileset_repeat_timer = 30;
                            }
                    } else if (event.key.key == SDLK_RIGHT) {
                        if (view_tileset_x < g_tilesetmanager->tileset(set_tile_tileset)->width() - 20) {
                                view_tileset_x++;
                                view_tileset_repeat_direction = 3;
                                view_tileset_repeat_timer = 30;
                            }
                    } else {
                            edit_mode = 1;  //change to edit mode using tiles
                            set_tile_drag = false;
                            editor_tiles_initialized = false;
                            return EDITOR_EDIT;
                        }
                    }

                    break;
                }

            case SDL_EVENT_KEY_UP: {
                    if (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN ||
                        event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                        view_tileset_repeat_direction = -1;
                        view_tileset_repeat_timer = 0;
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    short iCol = event.button.x / TILESIZE + view_tileset_x;
                    short iRow = event.button.y / TILESIZE + view_tileset_y;

                if (event.button.button == SDL_BUTTON_LEFT) {
                                            CTileset * tileset = g_tilesetmanager->tileset(set_tile_tileset);

                    if (iCol < tileset->width() && iRow < tileset->height()) {
                            set_tile_start_x = iCol;
                            set_tile_start_y = iRow;
                            set_tile_end_x = set_tile_start_x;
                            set_tile_end_y = set_tile_start_y;

                            set_tile_drag = true;
                        }
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    set_type = tileset->incrementTileType(iCol, iRow);
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    set_type = tileset->decrementTileType(iCol, iRow);
                }

                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                    short iCol = event.button.x / TILESIZE + view_tileset_x;
                    short iRow = event.button.y / TILESIZE + view_tileset_y;

                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (iCol < tileset->width() && iRow < tileset->height()) {
                            set_tile_cols = set_tile_end_x - set_tile_start_x + 1;
                            set_tile_rows = set_tile_end_y - set_tile_start_y + 1;

                            set_tile_drag = false;
                            edit_mode = 1;  //change to edit mode using tiles
                            editor_tiles_initialized = false;
                            return EDITOR_EDIT;
                        }
                    }

                    break;
                }

            case SDL_EVENT_MOUSE_MOTION: {
                    bound_mouse_motion_coords();
                    short iCol = bound_to_window_w(event.motion.x) / TILESIZE + view_tileset_x;
                    short iRow = bound_to_window_h(event.motion.y) / TILESIZE + view_tileset_y;

                if (iCol < tileset->width() && iRow < tileset->height()) {
                    if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
                            if (iCol < set_tile_start_x)
                                set_tile_start_x = iCol;

                            if (iCol > set_tile_end_x)
                                set_tile_end_x = iCol;

                            if (iRow < set_tile_start_y)
                                set_tile_start_y = iRow;

                            if (iRow > set_tile_end_y)
                                set_tile_end_y = iRow;
                    } else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT) || event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) {
                        tileset->setTileType(iCol, iRow, set_type);
                    }
                }

                break;
            }

                default:
                    break;
            }
        }

        //Allow auto-scrolling of tilesets when the arrow keys are held down
        if (view_tileset_repeat_direction >= 0 && view_tileset_repeat_timer > 0) {
            if (--view_tileset_repeat_timer <= 0) {
                view_tileset_repeat_timer = 5;

                if (view_tileset_repeat_direction == 0 && view_tileset_y > 0) {
                    view_tileset_y--;
                } else if (view_tileset_repeat_direction == 1 && view_tileset_y < g_tilesetmanager->tileset(set_tile_tileset)->height() - 15) {
                    view_tileset_y++;
                } else if (view_tileset_repeat_direction == 2 && view_tileset_x > 0) {
                    view_tileset_x--;
                } else if (view_tileset_repeat_direction == 3 && view_tileset_x < g_tilesetmanager->tileset(set_tile_tileset)->width() - 20) {
                    view_tileset_x++;
                }
            }
        }

        //drawmap(false, TILESIZE);
        //rm->menu_shade.draw(0, 0);
        SDL_FillSurfaceRect(screen, nullptr, 0xFF888888);

        SDL_Rect rectSrc;
        rectSrc.x = view_tileset_x << 5;
        rectSrc.y = view_tileset_y << 5;
                rectSrc.w = tileset->width() > 20 ? 640 : tileset->width() << 5;
                rectSrc.h = tileset->height() > 15 ? 480 : tileset->height() << 5;

        r.x = 0;
        r.y = 0;
        r.w = 640;
        r.h = 480;

        g_tilesetmanager->tileset(set_tile_tileset)->draw(DrawSize::Ingame, rectSrc, screen, r);
        //rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), tileset->name());

        int i, j;
        for (i = view_tileset_x; i < view_tileset_x + 20 && i < tileset->width(); i++) {
            for (j = view_tileset_y; j < view_tileset_y + 15 && j < tileset->height(); j++) {
                                TileType t = tileset->tileType(i, j);
                if (t != TileType::NonSolid)
                                    rm->spr_tiletypes.draw((i - view_tileset_x) << 5, (j - view_tileset_y) << 5, {static_cast<int>(PrevTileType(t)) << 3, 0, 8, 8});
            }
        }

        if (set_tile_drag) {
            for (i = set_tile_start_x; i <= set_tile_end_x; i++) {
                for (j = set_tile_start_y; j <= set_tile_end_y; j++) {
                    rm->spr_selectedtile.draw((i - view_tileset_x) << 5, (j - view_tileset_y) << 5);
                }
            }
        }

        DrawMessage();
        return EDITOR_TILES;
}

int editor_tiletype()
{
        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                    edit_mode = 6;
                    return EDITOR_EDIT;

                    break;
                }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                        short iCol = event.button.x / TILESIZE;
                        short iRow = event.button.y / TILESIZE;

                    if (iCol < NUMTILETYPES - 1 && iRow == 0) {
                            set_tiletype = (TileType)(iCol + 1);

                            edit_mode = 6;
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
        rm->menu_shade.draw(0, 0);

        rm->spr_transparenttiles.draw(0, 0);

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_TILETYPE;
}

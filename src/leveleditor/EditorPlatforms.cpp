#include "LevelEditorState.h"

#include <algorithm>

//SDL_Rect r;

namespace {
const char * szPathNames[3] = {"Line Segment", "Continuous", "Ellipse"};

enum {PLATFORM_EDIT_STATE_SELECT, PLATFORM_EDIT_STATE_PATH_TYPE, PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE, PLATFORM_EDIT_STATE_EDIT, PLATFORM_EDIT_STATE_PATH, PLATFORM_EDIT_STATE_TEST, PLATFORM_EDIT_STATE_TILETYPE, PLATFORM_EDIT_STATE_ANIMATED, PLATFORM_EDIT_STATE_MOVE};

short iPlatformEditState = PLATFORM_EDIT_STATE_SELECT;
short iPlatformSwitchState = 0, iPlatformSwitchIndex = 0;
short iEditPlatform = 0;
short iPlatformTop, iPlatformLeft, iPlatformWidth, iPlatformHeight;

short iPlatformPreview = -1;
} // namespace

namespace {
void editor_platforms_update_layout()
{
    constexpr int padding = 32;
    constexpr int btn_size = 32;
    constexpr int btn_spacing = 10;

    constexpr int grid_area_w = UI_PLATFORM_COLS * btn_size + (UI_PLATFORM_COLS - 1) * btn_spacing;
    constexpr int grid_area_max_h = UI_PLATFORM_ROWS * btn_size + (UI_PLATFORM_ROWS - 1) * btn_spacing;

    const int rows = (std::min(g_iNumPlatforms + 1, MAX_PLATFORMS) + UI_PLATFORM_COLS - 1) / UI_PLATFORM_COLS;
    const int grid_area_h = rows * btn_size + (rows - 1) * btn_spacing;
    const int title_area_h = padding + rm->menu_font_small.getHeight();
    const int newbtn_area_h = padding + (g_iNumPlatforms < MAX_PLATFORMS ? 24 + rNewButton[0].h : 0);

    r.w = 2 * padding + grid_area_w;
    r.h = title_area_h + grid_area_h + newbtn_area_h;
    r.x = (640 - r.w) / 2;
    r.y = (480 - grid_area_max_h) / 2 - title_area_h;

    rNewButton[1].x = r.x + (r.w >> 1) - (rNewButton[0].w >> 1);
    rNewButton[1].y = r.y + r.h - padding - rNewButton[0].h;
}

bool editor_platforms_initialized = false;
void init_editor_platforms()
{
    if (editor_platforms_initialized)
        return;

    rNewButton[0].x = 0;
    rNewButton[0].y = 352;
    rNewButton[0].w = 76;
    rNewButton[0].h = 32;

    rNewButton[1].w = rNewButton[0].w;
    rNewButton[1].h = rNewButton[0].h;

    editor_platforms_update_layout();

    for (short iType = 0; iType < 3; iType++) {
        rTypeButton[iType][0].x = 0;
        rTypeButton[iType][0].y = 0;
        rTypeButton[iType][0].w = 192;
        rTypeButton[iType][0].h = 32;

        rTypeButton[iType][1].x = 320 - (rTypeButton[iType][0].w >> 1);
        rTypeButton[iType][1].y = 180 + iType * 40;
        rTypeButton[iType][1].w = 192;
        rTypeButton[iType][1].h = 32;

        rTypeButton[iType][2].x = 24 * iType;
        rTypeButton[iType][2].y = 32;
        rTypeButton[iType][2].w = 24;
        rTypeButton[iType][2].h = 24;

        rTypeButton[iType][3].x = rTypeButton[iType][1].x + 8;
        rTypeButton[iType][3].y = rTypeButton[iType][1].y + 4;
        rTypeButton[iType][3].w = 24;
        rTypeButton[iType][3].h = 24;
    }

    editor_platforms_initialized = true;
}

void editor_platforms_draw_background_section(const SDL_Rect& src_area, const SDL_Rect& dst_area)
{
    int offset_y = 0;
    while (offset_y < dst_area.h) {
        const int h = std::min<int>(dst_area.h - offset_y, src_area.h);

        int offset_x = 0;
        while (offset_x < dst_area.w) {
            const int w = std::min<int>(dst_area.w - offset_x, src_area.w);

            SDL_Rect src { src_area.x, src_area.y, w, h };
            SDL_Rect dst { dst_area.x + offset_x, dst_area.y + offset_y, w, h };
            s_platform.draw(src, screen, dst);

            offset_x += w;
        }

        offset_y += h;
    }
}

void editor_platforms_draw_background()
{
    constexpr int corner = 24;

    SDL_Rect src {0, 0, corner, corner};
    SDL_Rect dst {r.x, r.y, corner, corner};

    constexpr std::array<int, 3> src_w { corner, 256 - 2 * corner, corner };
    constexpr std::array<int, 3> src_h { corner, 224 - 2 * corner, corner };

    const std::array<int, 3> dst_w { corner, r.w - 2 * corner, corner };
    const std::array<int, 3> dst_h { corner, r.h - 2 * corner, corner };

    for (int row = 0; row < 3; row++) {
        src.x = 0;
        src.h = src_h[row];

        dst.x = r.x;
        dst.h = dst_h[row];

        for (int col = 0; col < 3; col++) {
            src.w = src_w[col];
            dst.w = dst_w[col];
            editor_platforms_draw_background_section(src, dst);
            src.x += src_w[col];
            dst.x += dst_w[col];
        }

        src.y += src.h;
        dst.y += dst.h;
    }
}
} // namespace

// TODO: Check this for problems.
int editor_platforms()
{
    init_editor_platforms();

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_S) {
                        savecurrentmap();
                } else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                    const int platformIndex = static_cast<int>(event.key.key) - SDLK_1;
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState && platformIndex < g_iNumPlatforms) {
                            iEditPlatform = platformIndex;
                            iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                    } else if ((PLATFORM_EDIT_STATE_PATH_TYPE == iPlatformEditState || PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE == iPlatformEditState) && event.key.key >= SDLK_1 && event.key.key <= SDLK_3) {
                            g_Platforms[iEditPlatform].iPathType = static_cast<PlatformPathType>(event.key.key - SDLK_1);

                        if (PLATFORM_EDIT_STATE_PATH_TYPE == iPlatformEditState) {
                                iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                                SetPlatformToDefaults(iEditPlatform);
                        } else {
                                iPlatformEditState = PLATFORM_EDIT_STATE_PATH;
                            }
                    } else if (PLATFORM_EDIT_STATE_MOVE == iPlatformEditState && platformIndex < g_iNumPlatforms) {
                        if (iPlatformSwitchState == 1) {
                                SwitchPlatforms(iPlatformSwitchIndex, platformIndex);

                                iPlatformPreview = -1;

                                iPlatformSwitchState = 0;
                                iPlatformEditState = PLATFORM_EDIT_STATE_SELECT;
                        } else {
                                iPlatformSwitchState = 1;
                                iPlatformSwitchIndex = event.key.key - SDLK_1;
                            }
                        }
                } else if (event.key.key == SDLK_N) {
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState && g_iNumPlatforms < MAX_PLATFORMS) {
                            iEditPlatform = g_iNumPlatforms;
                            g_iNumPlatforms++;
                            editor_platforms_update_layout();
                            iPlatformEditState = PLATFORM_EDIT_STATE_PATH_TYPE;
                        }
                } else if (event.key.key == SDLK_M) {
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState) {
                            iPlatformPreview = -1;
                            iPlatformEditState = PLATFORM_EDIT_STATE_MOVE;
                            iPlatformSwitchState = 0;
                        }
                } else if (event.key.key == SDLK_T) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            FPSLimiter::instance().frameStart();
                            while (editor_tiles() == EDITOR_TILES) {
                                FPSLimiter::instance().beforeFlip();
                                gfx_flipscreen();
                                FPSLimiter::instance().afterFlip();
                                FPSLimiter::instance().frameStart();
                            }
                            iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                    } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState) {
                            iPlatformEditState = PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE;
                        }
                } else if (event.key.key == SDLK_A) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            FPSLimiter::instance().frameStart();
                            while (editor_animation() == EDITOR_ANIMATION) {
                                FPSLimiter::instance().beforeFlip();
                                gfx_flipscreen();
                                FPSLimiter::instance().afterFlip();
                                FPSLimiter::instance().frameStart();
                            }
                            iPlatformEditState = PLATFORM_EDIT_STATE_ANIMATED;
                        }
                } else if (event.key.key == SDLK_L) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            FPSLimiter::instance().frameStart();
                            while (editor_tiletype() == EDITOR_TILETYPE) {
                                FPSLimiter::instance().beforeFlip();
                                gfx_flipscreen();
                                FPSLimiter::instance().afterFlip();
                                FPSLimiter::instance().frameStart();
                            }
                            iPlatformEditState = PLATFORM_EDIT_STATE_TILETYPE;
                        }
                } else if (event.key.key == SDLK_Y) { //Change the draw layer
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            if (++g_Platforms[iEditPlatform].iDrawLayer > 4)
                                g_Platforms[iEditPlatform].iDrawLayer = 0;
                        }
                } else if (event.key.key == SDLK_C) {
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState) {
                            iPlatformEditState = PLATFORM_EDIT_STATE_TEST;
                            insert_platforms_into_map();
                            g_map->resetPlatforms();
                        }
                } else if (event.key.key == SDLK_DELETE) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            //Copy platforms into empty spot
                        for (short iPlatform = iEditPlatform; iPlatform < g_iNumPlatforms - 1; iPlatform++) {
                            for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
                                for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
                                        g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow] = g_Platforms[iPlatform + 1].tiles[iCol * MAPHEIGHT + iRow];
                                        g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow] = g_Platforms[iPlatform + 1].types[iCol * MAPHEIGHT + iRow];
                                    }
                                }

                                g_Platforms[iPlatform].iPathType = g_Platforms[iPlatform + 1].iPathType;

                                g_Platforms[iPlatform].iVelocity = g_Platforms[iPlatform + 1].iVelocity;
                                g_Platforms[iPlatform].iStartX = g_Platforms[iPlatform + 1].iStartX;
                                g_Platforms[iPlatform].iStartY = g_Platforms[iPlatform + 1].iStartY;
                                g_Platforms[iPlatform].iEndX = g_Platforms[iPlatform + 1].iEndX;
                                g_Platforms[iPlatform].iEndY = g_Platforms[iPlatform + 1].iEndY;

                                g_Platforms[iPlatform].fAngle = g_Platforms[iPlatform + 1].fAngle;
                                g_Platforms[iPlatform].fRadiusX = g_Platforms[iPlatform + 1].fRadiusX;
                                g_Platforms[iPlatform].fRadiusY = g_Platforms[iPlatform + 1].fRadiusY;

                                g_Platforms[iPlatform].iDrawLayer = g_Platforms[iPlatform + 1].iDrawLayer;
                            }

                            g_iNumPlatforms--;
                            iPlatformEditState = PLATFORM_EDIT_STATE_SELECT;
                        }
                } else if (event.key.key == SDLK_ESCAPE) {
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState) {
                            editor_platforms_initialized = false;
                            return EDITOR_EDIT;
                    } else if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_MOVE == iPlatformEditState || PLATFORM_EDIT_STATE_TEST == iPlatformEditState) {
                            iPlatformPreview = -1;
                            g_Platforms[iEditPlatform].UpdatePreview();
                            iPlatformEditState = PLATFORM_EDIT_STATE_SELECT;
                            //Fix menu offset
                            editor_platforms_update_layout();
                    } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                        }
                } else if (event.key.key == SDLK_KP_MINUS || event.key.key == SDLK_MINUS) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                                if (g_Platforms[iEditPlatform].iVelocity > -10)
                                    g_Platforms[iEditPlatform].iVelocity--;

                                if (g_Platforms[iEditPlatform].iVelocity == 0)
                                    g_Platforms[iEditPlatform].iVelocity--;
                        } else {
                                if (g_Platforms[iEditPlatform].iVelocity > 2)
                                    g_Platforms[iEditPlatform].iVelocity--;
                            }
                        }
                } else if (event.key.key == SDLK_KP_PLUS || event.key.key == SDLK_EQUALS) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                                if (g_Platforms[iEditPlatform].iVelocity < 10)
                                    g_Platforms[iEditPlatform].iVelocity++;

                                if (g_Platforms[iEditPlatform].iVelocity == 0)
                                    g_Platforms[iEditPlatform].iVelocity++;
                        } else {
                                if (g_Platforms[iEditPlatform].iVelocity < MAX_PLATFORM_VELOCITY)
                                    g_Platforms[iEditPlatform].iVelocity++;
                            }
                        }
                } else if (event.key.key == SDLK_P) {
                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            iPlatformEditState = PLATFORM_EDIT_STATE_PATH;
                            CalculatePlatformDims(iEditPlatform, &iPlatformLeft, &iPlatformTop, &iPlatformWidth, &iPlatformHeight);
                            insert_platforms_into_map();
                        }
                    }

                    break;
                }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT && !ignoreclick) {
                    if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState || PLATFORM_EDIT_STATE_MOVE == iPlatformEditState) {
                            //check clicks on existing platforms
                        for (int iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++) {
                                if (event.button.x >= g_Platforms[iPlatform].rIcon[1].x && event.button.x < g_Platforms[iPlatform].rIcon[1].x + g_Platforms[iPlatform].rIcon[1].w &&
                                    event.button.y >= g_Platforms[iPlatform].rIcon[1].y && event.button.y < g_Platforms[iPlatform].rIcon[1].y + g_Platforms[iPlatform].rIcon[1].h) {
                                if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState) {
                                        iEditPlatform = iPlatform;
                                        iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                                        ignoreclick = true;
                                } else if (PLATFORM_EDIT_STATE_MOVE == iPlatformEditState) {
                                    if (iPlatformSwitchState == 1) {
                                            SwitchPlatforms(iPlatformSwitchIndex, iPlatform);

                                            iPlatformPreview = -1;

                                            iPlatformSwitchState = 0;
                                            iPlatformEditState = PLATFORM_EDIT_STATE_SELECT;
                                    } else {
                                            iPlatformSwitchState = 1;
                                            iPlatformSwitchIndex = iPlatform;
                                        }
                                    }
                                }
                            }

                            if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState && g_iNumPlatforms < MAX_PLATFORMS && event.button.x >= rNewButton[1].x &&
                                event.button.x < rNewButton[1].x + rNewButton[1].w && event.button.y >= rNewButton[1].y && event.button.y < rNewButton[1].y + rNewButton[1].h) {
                                //Create a new platform then edit it

                                iEditPlatform = g_iNumPlatforms;
                                g_iNumPlatforms++;
                                editor_platforms_update_layout();
                                iPlatformEditState = PLATFORM_EDIT_STATE_PATH_TYPE;
                                ignoreclick = true;
                            }
                    } else if (PLATFORM_EDIT_STATE_PATH_TYPE == iPlatformEditState || PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE == iPlatformEditState) {
                        if (!ignoreclick) {
                                short iClickX = event.button.x;
                                short iClickY = event.button.y;

                            for (int iType = 0; iType < 3; iType++) {
                                    if (iClickX >= rTypeButton[iType][1].x && iClickX < rTypeButton[iType][1].x + rTypeButton[iType][1].w &&
                                        iClickY >= rTypeButton[iType][1].y && iClickY < rTypeButton[iType][1].y + rTypeButton[iType][1].h) {
                                        ignoreclick = true;

                                        g_Platforms[iEditPlatform].iPathType = static_cast<PlatformPathType>(iType);

                                    if (PLATFORM_EDIT_STATE_PATH_TYPE == iPlatformEditState) {
                                            iPlatformEditState = PLATFORM_EDIT_STATE_EDIT;
                                            SetPlatformToDefaults(iEditPlatform);
                                    } else {
                                            iPlatformEditState = PLATFORM_EDIT_STATE_PATH;
                                        }

                                        break;
                                    }
                                }
                            }
                    } else if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState) {
                        if (!ignoreclick) {
                                short ix = event.button.x / TILESIZE;
                                short iy = event.button.y / TILESIZE;

                            for (short i = 0; i < set_tile_cols; i++) {
                                for (short j = 0; j < set_tile_rows; j++) {
                                    if (ix + i >= 0 && ix + i < MAPWIDTH && iy + j >= 0 && iy + j < MAPHEIGHT) {
                                            TilesetTile * tile = &g_Platforms[iEditPlatform].tiles[(ix + i) * MAPHEIGHT + iy + j];
                                            SetTilesetTile(tile, set_tile_tileset, set_tile_start_x + i, set_tile_start_y + j);
                                            g_Platforms[iEditPlatform].types[(ix + i) * MAPHEIGHT + iy + j] = g_tilesetmanager->tileset(tile->iID)->tileType(tile->iCol, tile->iRow);
                                        }
                                    }
                                }
                            }
                    } else if (PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState) { //animated tiles
                        if (!ignoreclick) {
                                short ix = event.button.x / TILESIZE;
                                short iy = event.button.y / TILESIZE;

                            for (short i = 0; i < set_tile_cols; i++) {
                                for (short j = 0; j < set_tile_rows; j++) {
                                    if (ix + i >= 0 && ix + i < MAPWIDTH && iy + j >= 0 && iy + j < MAPHEIGHT) {
                                            TilesetTile * tile = &g_Platforms[iEditPlatform].tiles[(ix + i) * MAPHEIGHT + iy + j];
                                            SetTilesetTile(tile, TILESETANIMATED, set_tile_start_y + j, set_tile_start_x + i);
                                            g_Platforms[iEditPlatform].types[(ix + i) * MAPHEIGHT + iy + j] = animatedtiletypes[tile->iRow + (tile->iCol << 5)];
                                        }
                                    }
                                }
                            }
                    } else if (PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                        if (!ignoreclick) {
                                short ix = event.button.x / TILESIZE;
                                short iy = event.button.y / TILESIZE;

                                g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = set_tiletype;
                            }
                    } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState) {
                        const bool* keystate = SDL_GetKeyboardState(nullptr);
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse && (CheckKey(keystate, SDLK_Z) || CheckKey(keystate, SDLK_X) || CheckKey(keystate, SDLK_C))) {
                                UpdatePlatformPathRadius(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT), CheckKey(keystate, SDLK_Z) != 0, CheckKey(keystate, SDLK_C) != 0);
                        } else {
                                UpdatePlatformPathStart(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                            }
                        }

                        break;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        short ix = event.button.x / TILESIZE;
                        short iy = event.button.y / TILESIZE;

                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState) {
                            ClearTilesetTile(&g_Platforms[iEditPlatform].tiles[ix * MAPHEIGHT + iy]);
                            g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = TileType::NonSolid;
                    } else if (PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                            g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = TileType::NonSolid;
                    } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState) {
                        const bool* keystate = SDL_GetKeyboardState(nullptr);
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Straight) {
                                UpdatePlatformPathEnd(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                        } else if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::StraightContinuous || g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                                UpdatePlatformPathAngle(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                            }
                        }
                    }
                }
            [[fallthrough]];
            case SDL_EVENT_MOUSE_MOTION: {
                    bound_mouse_motion_coords();
                    short ix = bound_to_window_w(event.motion.x) / TILESIZE;
                    short iy = bound_to_window_h(event.motion.y) / TILESIZE;

                if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState) {
                    if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT) && !ignoreclick) {
                        for (short i = 0; i < set_tile_cols; i++) {
                            for (short j = 0; j < set_tile_rows; j++) {
                                if (ix + i >= 0 && ix + i < MAPWIDTH && iy + j >= 0 && iy + j < MAPHEIGHT) {
                                        TilesetTile * tile = &g_Platforms[iEditPlatform].tiles[(ix + i) * MAPHEIGHT + iy + j];

                                    if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState) {
                                            SetTilesetTile(tile, set_tile_tileset, set_tile_start_x + i, set_tile_start_y + j);
                                        g_Platforms[iEditPlatform].types[(ix + i) * MAPHEIGHT + iy + j] = g_tilesetmanager->tileset(tile->iID)->tileType(tile->iCol, tile->iRow);
                                    } else {
                                            SetTilesetTile(tile, TILESETANIMATED, set_tile_start_y + j, set_tile_start_x + i);
                                            g_Platforms[iEditPlatform].types[(ix + i) * MAPHEIGHT + iy + j] = animatedtiletypes[tile->iRow + (tile->iCol << 5)];
                                        }
                                    }
                                }
                            }
                    } else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
                            ClearTilesetTile(&g_Platforms[iEditPlatform].tiles[ix * MAPHEIGHT + iy]);
                            g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = TileType::NonSolid;
                        }
                } else if (PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
                        if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT) && !ignoreclick)
                            g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = set_tiletype;
                        else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))
                            g_Platforms[iEditPlatform].types[ix * MAPHEIGHT + iy] = TileType::NonSolid;
                } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState) {
                    if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
                        const bool* keystate = SDL_GetKeyboardState(nullptr);
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse && (CheckKey(keystate, SDLK_Z) || CheckKey(keystate, SDLK_X) || CheckKey(keystate, SDLK_C))) {
                            UpdatePlatformPathRadius(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT), CheckKey(keystate, SDLK_Z) != 0, CheckKey(keystate, SDLK_C) != 0);
                        } else {
                            UpdatePlatformPathStart(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                        }
                    } else if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
                        const bool* keystate = SDL_GetKeyboardState(nullptr);
                        if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Straight) {
                                UpdatePlatformPathEnd(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                        } else if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::StraightContinuous || g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                                UpdatePlatformPathAngle(iEditPlatform, event.button.x, event.button.y, CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT));
                            }
                        }
                    }
                    //Display platform preview
                else if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState || PLATFORM_EDIT_STATE_MOVE == iPlatformEditState) {
                        iPlatformPreview = -1;
                    for (int iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++) {
                            if (event.button.x >= g_Platforms[iPlatform].rIcon[1].x && event.button.x < g_Platforms[iPlatform].rIcon[1].x + g_Platforms[iPlatform].rIcon[1].w &&
                                event.button.y >= g_Platforms[iPlatform].rIcon[1].y && event.button.y < g_Platforms[iPlatform].rIcon[1].y + g_Platforms[iPlatform].rIcon[1].h) {
                                iPlatformPreview = iPlatform;
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

        if (PLATFORM_EDIT_STATE_TEST != iPlatformEditState) {
            drawmap(false, TILESIZE);
            rm->menu_shade.draw(0, 0);
        }

        if (PLATFORM_EDIT_STATE_SELECT == iPlatformEditState || PLATFORM_EDIT_STATE_MOVE == iPlatformEditState) {
            editor_platforms_draw_background();

            rm->menu_font_small.drawCentered(320, r.y + 18, "Platforms");

            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Platform Mode: [esc] Exit  [c] Check Paths, [1-8] Select, [n] New");
            if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

            for (int iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++)
                s_platform.draw(g_Platforms[iPlatform].rIcon[0], screen, g_Platforms[iPlatform].rIcon[1]);

            if (g_iNumPlatforms < MAX_PLATFORMS && PLATFORM_EDIT_STATE_SELECT == iPlatformEditState)
                s_platform.draw(rNewButton[0], screen, rNewButton[1]);

            if (PLATFORM_EDIT_STATE_MOVE == iPlatformEditState) {
                if (iPlatformSwitchState == 0) {
                    rm->menu_font_small.drawCentered(320, 280, "Select First");
                    rm->menu_font_small.drawCentered(320, 300, "Platform To Switch");
                } else {
                    rm->menu_font_small.drawCentered(320, 280, "Select Second");
                    rm->menu_font_small.drawCentered(320, 300, "Platform To Switch");
                }
            }

            if (iPlatformPreview >= 0) {
                DisplayPlatformPreview(iPlatformPreview, event.button.x, event.button.y);
            }
        } else if (PLATFORM_EDIT_STATE_PATH_TYPE == iPlatformEditState || PLATFORM_EDIT_STATE_CHANGE_PATH_TYPE == iPlatformEditState) {
            //Draw path options
            for (short iType = 0; iType < 3; iType++) {
                s_platformpathbuttons.draw(rTypeButton[iType][0], screen, rTypeButton[iType][1]);
                s_platformpathbuttons.draw(rTypeButton[iType][2], screen, rTypeButton[iType][3]);

                rm->menu_font_large.draw(rTypeButton[iType][1].x + 36, rTypeButton[iType][1].y + 6, szPathNames[iType]);
            }

            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Path Type");
        } else if (PLATFORM_EDIT_STATE_EDIT == iPlatformEditState || PLATFORM_EDIT_STATE_ANIMATED == iPlatformEditState || PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState) {
            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 3, "Edit Platform");
            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 2, "[esc] Exit  [t] Tiles  [a] Animation [l] Types [del] Delete  [p] Path");
            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[+/-] Velocity  [y] Draw Layer");
            draw_platform(iEditPlatform, PLATFORM_EDIT_STATE_TILETYPE == iPlatformEditState);

            if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                short iVelMarkerX = 198 + (g_Platforms[iEditPlatform].iVelocity + 10) * 12;

                SDL_Rect rVel[2] = {{0, 400, 244, 17},{198, 10, 244, 17}};
                s_platform.draw(rVel[0], screen, rVel[1]);

                SDL_Rect rMarker[2] = {{244,400,8,18},{iVelMarkerX,10,8,18}};
                s_platform.draw(rMarker[0], screen, rMarker[1]);

                rm->menu_font_small.drawRightJustified(198, 10, "Counter");
                rm->menu_font_small.draw(442, 10, "Clockwise");
            } else {
                short iVelMarkerX = 220 + (g_Platforms[iEditPlatform].iVelocity - 1) * 12;

                SDL_Rect rVel[2] = {{12, 384, 172, 13},{234, 10, 172, 13}};
                s_platform.draw(rVel[0], screen, rVel[1]);

                SDL_Rect rMarker[2] = {{184, 384, 8, 16},{iVelMarkerX, 8, 8, 16}};
                s_platform.draw(rMarker[0], screen, rMarker[1]);

                rm->menu_font_small.drawRightJustified(234, 10, "Slow");
                rm->menu_font_small.draw(406, 10, "Fast");
            }

            rm->spr_number_icons.draw(619, 5, {g_Platforms[iEditPlatform].iDrawLayer << 4, 48, 16, 16});
        } else if (PLATFORM_EDIT_STATE_PATH == iPlatformEditState) {
            if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Straight) {
                MapPlatform * platform = &g_Platforms[iEditPlatform];
                DrawPlatform(platform->iPathType, g_map->platforms[iEditPlatform]->iTileData, platform->iStartX, platform->iStartY, platform->iEndX, platform->iEndY, platform->fAngle, platform->fRadiusX, platform->fRadiusY, 0, iPlatformWidth, iPlatformHeight, false, true, blitdest);

                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - (rm->menu_font_small.getHeight() << 1), "Edit Path");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[esc] Exit  [LMB] Set Start Point  [RMB] Set End Point [t] Path Type");
            } else if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::StraightContinuous) {
                MapPlatform * platform = &g_Platforms[iEditPlatform];
                DrawPlatform(platform->iPathType, g_map->platforms[iEditPlatform]->iTileData, platform->iStartX, platform->iStartY, platform->iEndX, platform->iEndY, platform->fAngle, platform->fRadiusX, platform->fRadiusY, 0, iPlatformWidth, iPlatformHeight, false, true, blitdest);

                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - (rm->menu_font_small.getHeight() << 1), "Edit Path: [esc] Exit  [LMB] Set Start Point  [RMB] Set Angle");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[SHIFT + LMB] Location Snap [SHIFT + RMB] Angle Snap [t] Path Type");
            } else if (g_Platforms[iEditPlatform].iPathType == PlatformPathType::Ellipse) {
                MapPlatform * platform = &g_Platforms[iEditPlatform];
                DrawPlatform(platform->iPathType, g_map->platforms[iEditPlatform]->iTileData, platform->iStartX, platform->iStartY, platform->iEndX, platform->iEndY, platform->fAngle, platform->fRadiusX, platform->fRadiusY, 0, iPlatformWidth, iPlatformHeight, false, true, blitdest);

                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 4, "Edit Path: [esc] Exit  [LMB] Set Center [SHIFT + LMB] Center Snap");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 3, "[X + LMB] Set X Radius [SHIFT + X + LMB] X Radius Snap");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 2, "[Z + LMB] Set Y Radius [SHIFT + Z + LMB] Y Radius Snap");
                if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[C + LMB] Set Circular Radius [SHIFT + C + LMB] Circular Radius Snap [t] Path Type");
            }
        } else if (PLATFORM_EDIT_STATE_TEST == iPlatformEditState) {
            g_map->updatePlatforms();

            //Platforms are drawn inside drawmap(...)
            drawmap(false, TILESIZE, true);

            if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Check Paths: [esc] Exit");
        }

        DrawMessage();
        return EDITOR_PLATFORM;
}

void DisplayPlatformPreview(short iPlatformId, short iMouseX, short iMouseY)
{
    SDL_Rect srcRect = {0, 0, 160, 120};
    SDL_Rect dstRect = {iMouseX, iMouseY, 160, 120};
    g_Platforms[iPlatformId].preview.draw(srcRect, screen, dstRect);
}

void SwitchPlatforms(short iPlatformId1, short iPlatformId2)
{
    MapPlatform tempPlatform;
    CopyPlatform(&tempPlatform, &g_Platforms[iPlatformId1]);
    CopyPlatform(&g_Platforms[iPlatformId1], &g_Platforms[iPlatformId2]);
    CopyPlatform(&g_Platforms[iPlatformId2], &tempPlatform);

    g_Platforms[iPlatformId1].UpdatePreview();
    g_Platforms[iPlatformId2].UpdatePreview();
}

void CopyPlatform(MapPlatform * toPlatform, MapPlatform * fromPlatform)
{
    toPlatform->fAngle = fromPlatform->fAngle;
    toPlatform->fRadiusX = fromPlatform->fRadiusX;
    toPlatform->fRadiusY = fromPlatform->fRadiusY;
    toPlatform->iDrawLayer = fromPlatform->iDrawLayer;
    toPlatform->iEndX = fromPlatform->iEndX;
    toPlatform->iEndY = fromPlatform->iEndY;
    toPlatform->iPathType = fromPlatform->iPathType;
    toPlatform->iStartX = fromPlatform->iStartX;
    toPlatform->iStartY = fromPlatform->iStartY;
    toPlatform->iVelocity = fromPlatform->iVelocity;

    for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
        for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
            toPlatform->tiles[iCol * MAPHEIGHT + iRow] = fromPlatform->tiles[iCol * MAPHEIGHT + iRow];
            toPlatform->types[iCol * MAPHEIGHT + iRow] = fromPlatform->types[iCol * MAPHEIGHT + iRow];
        }
    }
}

void UpdatePlatformPathStart(short iEditPlatform, short iClickX, short iClickY, bool fSnapToTile)
{
    if (!ignoreclick) {
        if (fSnapToTile) {
            //Round off coord to nearest tile
            iClickX &= ~15;
            iClickY &= ~15;
        }

        g_Platforms[iEditPlatform].iStartX = iClickX;
        g_Platforms[iEditPlatform].iStartY = iClickY;
    }
}

void UpdatePlatformPathEnd(short iEditPlatform, short iClickX, short iClickY, bool fSnapToTile)
{
    if (!ignoreclick) {
        if (fSnapToTile) {
            //Round off coord to nearest tile
            iClickX &= ~15;
            iClickY &= ~15;
        }

        g_Platforms[iEditPlatform].iEndX = iClickX;
        g_Platforms[iEditPlatform].iEndY = iClickY;
    }
}

void UpdatePlatformPathAngle(short iEditPlatform, short iClickX, short iClickY, bool fSnapToAngle)
{
    if (!ignoreclick) {
        int iDiffX = iClickX - g_Platforms[iEditPlatform].iStartX;
        int iDiffY = iClickY - g_Platforms[iEditPlatform].iStartY;

        float fAngle = atan2(static_cast<float>(iDiffY), static_cast<float>(iDiffX));

        if (fAngle < 0.0f)
            fAngle += TWO_PI;

        if (fSnapToAngle) {
            float dSector = TWO_PI / 16;
            fAngle += TWO_PI / 32;

            if (fAngle > TWO_PI)
                fAngle -= TWO_PI;

            for (short iSector = 0; iSector < 16; iSector++) {
                if (fAngle >= dSector * iSector && fAngle < dSector * (iSector + 1)) {
                    fAngle = dSector * iSector;
                    break;
                }
            }
        }

        g_Platforms[iEditPlatform].fAngle = fAngle;
    }
}

void UpdatePlatformPathRadius(short iEditPlatform, short iClickX, short iClickY, bool fSnapToRadius, bool fIsRadiusX, bool fSetCircle)
{
    if (!ignoreclick) {
        if (fSetCircle) {
            short iDiffX = iClickX - g_Platforms[iEditPlatform].iStartX;
            short iDiffY = iClickY - g_Platforms[iEditPlatform].iStartY;

            short iRadius = static_cast<short>(sqrt(static_cast<double>(iDiffX * iDiffX + iDiffY * iDiffY)));

            if (fSnapToRadius)
                iRadius &= ~15;

            g_Platforms[iEditPlatform].fRadiusX = static_cast<float>(iRadius);
            g_Platforms[iEditPlatform].fRadiusY = g_Platforms[iEditPlatform].fRadiusX;
        } else {
            if (fIsRadiusX) {
                short iRadiusX = abs(iClickX - g_Platforms[iEditPlatform].iStartX);

                if (fSnapToRadius)
                    iRadiusX &= ~15;

                g_Platforms[iEditPlatform].fRadiusX = static_cast<float>(iRadiusX);
            } else {
                short iRadiusY = abs(iClickY - g_Platforms[iEditPlatform].iStartY);

                if (fSnapToRadius)
                    iRadiusY &= ~15;

                g_Platforms[iEditPlatform].fRadiusY = static_cast<float>(iRadiusY);
            }
        }
    }
}

//Draws the unfinished platform (complete map width x height grid)
void draw_platform(short iPlatform, bool fDrawTileTypes)
{
    for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
        for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
            TilesetTile * tile = &g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow];

            if (tile->iID >= 0) {
                g_tilesetmanager->Draw(screen, tile->iID, DrawSize::Ingame, tile->iCol, tile->iRow, iCol, iRow);
                //g_tilesetmanager->tileset(tile->iID)->draw(CTilesetManager::rect(0, tile->iCol, tile->iRow), screen, bltrect);
            } else if (tile->iID == TILESETANIMATED) {
                short iSrcCol = tile->iCol << 2;
                short iSrcRow = tile->iRow;

                if (iSrcCol > 31 || iSrcCol < 0 || iSrcRow > 31 || iSrcRow < 0) {
                    iSrcCol = 0;
                    iSrcRow = 0;
                }

                rm->spr_tileanimation[0].draw(CTilesetManager::rect(DrawSize::Ingame, iSrcCol, iSrcRow), screen, CTilesetManager::rect(DrawSize::Ingame, iCol, iRow));
            } else if (tile->iID == TILESETUNKNOWN) {
                rm->spr_unknowntile[0].draw(CTilesetManager::rect(DrawSize::Ingame, 0, 0), screen, CTilesetManager::rect(DrawSize::Ingame, iCol, iRow));
            }

            if (fDrawTileTypes) {
                TileType type = g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow];
                rm->spr_transparenttiles.draw(iCol * TILESIZE, iRow * TILESIZE, {static_cast<int>(PrevTileType(type)) * TILESIZE, 0, TILESIZE, TILESIZE});
            }
        }
    }
}

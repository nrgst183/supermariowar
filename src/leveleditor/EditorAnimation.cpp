#include "LevelEditorState.h"

namespace {
bool editor_animation_initialized = false;
void init_editor_animation()
{
    if (editor_animation_initialized)
        return;

    set_tile_drag = false;
    view_tileset_repeat_direction = -1;
    view_tileset_repeat_timer = 0;

    editor_animation_initialized = true;
}
} // namespace

int editor_animation()
{
    init_editor_animation();

		//handle messages
        while (SDL_PollEvent(&event)) {
			short iCol = event.button.x / TILESIZE + view_animated_tileset_x;
			short iRow = event.button.y / TILESIZE;

			bool fInValidTile = iRow >= 0 && iRow <= 7;

            switch (event.type) {
            case SDL_EVENT_QUIT:
                return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_A) {
                    if (!set_tile_drag) {
							edit_mode = 8;
                            editor_animation_initialized = false;
							return EDITOR_EDIT;
						}
                } else if (event.key.key == SDLK_LEFT) {
                    if (view_animated_tileset_x > 0) {
							view_animated_tileset_x--;
							view_tileset_repeat_direction = 2;
							view_tileset_repeat_timer = 30;
						}
                } else if (event.key.key == SDLK_RIGHT) {
                    if (view_animated_tileset_x < 12) {
							view_animated_tileset_x++;
							view_tileset_repeat_direction = 3;
							view_tileset_repeat_timer = 30;
						}
					}

					break;
				}

            case SDL_EVENT_KEY_UP: {
                if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
						view_tileset_repeat_direction = -1;
						view_tileset_repeat_timer = 0;
					}

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (fInValidTile) {
							set_tile_start_x = iCol;
							set_tile_start_y = iRow;
							set_tile_end_x = set_tile_start_x;
							set_tile_end_y = set_tile_start_y;

							set_tile_drag = true;
						}
                }
                else if (event.button.button == SDL_BUTTON_RIGHT) {
					TileType * type = &animatedtiletypes[iCol + (iRow << 5)];
					set_type = *type = NextTileType(*type);
				}
				else if (event.button.button == SDL_BUTTON_MIDDLE) {
					TileType * type = &animatedtiletypes[iCol + (iRow << 5)];
					set_type = *type = PrevTileType(*type);
				}
				break;
			}

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (fInValidTile) {
							set_tile_cols = set_tile_end_x - set_tile_start_x + 1;
							set_tile_rows = set_tile_end_y - set_tile_start_y + 1;

							set_tile_drag = false;
							edit_mode = 8;  //change to edit mode using tiles
                            editor_animation_initialized = false;
							return EDITOR_EDIT;

						}
					}

					break;
				}

            case SDL_EVENT_MOUSE_MOTION: {
                bound_mouse_motion_coords();
                if (fInValidTile) {
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
						animatedtiletypes[iCol + (iRow << 5)] = set_type;
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

                if (view_tileset_repeat_direction == 2 && view_animated_tileset_x > 0) {
					view_animated_tileset_x--;
                } else if (view_tileset_repeat_direction == 3 && view_animated_tileset_x < 12) {
					view_animated_tileset_x++;
				}
			}
		}

		//drawmap(false, TILESIZE);
		//rm->menu_shade.draw(0, 0);
		SDL_FillSurfaceRect(screen, nullptr, 0xFF888888);

        for (short iCol = view_animated_tileset_x; iCol < view_animated_tileset_x + 20; iCol++) {
            for (short iRow = 0; iRow < 8; iRow++) {
				short iDestX = (iCol - view_animated_tileset_x) << 5;
				short iDestY = iRow << 5;
				short iSrcX = iRow << 7;
				short iSrcY = iCol << 5;

				rm->spr_tileanimation[0].draw(iDestX, iDestY, {iSrcX, iSrcY, TILESIZE, TILESIZE});

				TileType t = animatedtiletypes[iCol + (iRow << 5)];
				if (t != TileType::NonSolid)
					rm->spr_tiletypes.draw(iDestX, iDestY, {static_cast<int>(PrevTileType(t)) << 3, 0, 8, 8});
			}
		}

        if (set_tile_drag) {
            for (short i = set_tile_start_x - view_animated_tileset_x; i <= set_tile_end_x - view_animated_tileset_x; i++) {
                for (short j = set_tile_start_y; j <= set_tile_end_y; j++) {
					rm->spr_selectedtile.draw(i << 5, j << 5);
				}
			}
		}

		if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

		if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Use Arrow Keys To Scroll");

		DrawMessage();
		return EDITOR_ANIMATION;
}

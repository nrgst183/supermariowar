#include "WorldEditorState.h"

int editor_start_items()
{
	bool done = false;

	SDL_Rect rItemDst[NUM_POWERUPS + NUM_WORLD_POWERUPS];

	SDL_Rect rPickedItemDst[32];

	short iColCount = 0;
	short iRowCount = 0;
    for (short iItem = 0; iItem < NUM_POWERUPS + NUM_WORLD_POWERUPS; iItem++) {
		rItemDst[iItem].x = 16 + iColCount * 48;
		rItemDst[iItem].y = 16 + iRowCount * 48;
		rItemDst[iItem].w = 32;
		rItemDst[iItem].h = 32;

        if (++iColCount > 12) {
			iColCount = 0;
			iRowCount++;
		}
	}

	short iPickedItem = 0;
    for (short iPickedItemY = 0; iPickedItemY < 4; iPickedItemY++) {
        for (short iPickedItemX = 0; iPickedItemX < 8; iPickedItemX++) {
			rPickedItemDst[iPickedItem].x = 122 + iPickedItemX * 52;
			rPickedItemDst[iPickedItem].y = 240 + iPickedItemY * 64;
			rPickedItemDst[iPickedItem].w = 32;
			rPickedItemDst[iPickedItem].h = 32;

			iPickedItem++;
		}
	}

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
					return EDITOR_EDIT;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
					short iButtonX = bound_to_window_w(event.button.x);
					short iButtonY = bound_to_window_h(event.button.y);

                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    for (short iItem = 0; iItem < NUM_POWERUPS + NUM_WORLD_POWERUPS; iItem++) {
							if (iButtonX >= rItemDst[iItem].x && iButtonX < rItemDst[iItem].w + rItemDst[iItem].x &&
                                iButtonY >= rItemDst[iItem].y && iButtonY < rItemDst[iItem].h + rItemDst[iItem].y) {
								g_worldmap.addInitialBonus(iItem);
								break;
							}
						}
					}

                    for (short iRemoveItem = 0; iRemoveItem < g_worldmap.getNumInitialBonuses(); iRemoveItem++) {
							if (iButtonX >= rPickedItemDst[iRemoveItem].x && iButtonX < rPickedItemDst[iRemoveItem].w + rPickedItemDst[iRemoveItem].x &&
                                iButtonY >= rPickedItemDst[iRemoveItem].y && iButtonY < rPickedItemDst[iRemoveItem].h + rPickedItemDst[iRemoveItem].y) {
								g_worldmap.removeInitialBonus(iRemoveItem);

								break;
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

        for (short iItem = 0; iItem < NUM_POWERUPS; iItem++) {
			rm->spr_storedpoweruplarge.draw(rItemDst[iItem].x, rItemDst[iItem].y, {iItem << 5, 0, 32, 32});
		}

        for (short iWorldItem = 0; iWorldItem < NUM_WORLD_POWERUPS; iWorldItem++) {
			rm->spr_worlditems.draw(rItemDst[iWorldItem + NUM_POWERUPS].x, rItemDst[iWorldItem + NUM_POWERUPS].y, {iWorldItem << 5, 0, 32, 32});
		}

        for (short iPopup = 0; iPopup < 4; iPopup++) {
			rm->spr_worlditempopup.draw(0, 416 - (iPopup << 6), {0, 0, 320, 64});
			rm->spr_worlditempopup.draw(320, 416 - (iPopup << 6), {192, 0, 320, 64});
		}

        for (short iPickedItem = 0; iPickedItem < g_worldmap.getNumInitialBonuses(); iPickedItem++) {
			short iPowerup = g_worldmap.getInitialBonus(iPickedItem);
			if (iPowerup >= NUM_POWERUPS)
				rm->spr_worlditems.draw(rPickedItemDst[iPickedItem].x, rPickedItemDst[iPickedItem].y, {(iPowerup - NUM_POWERUPS) << 5, 0, 32, 32});
			else
				rm->spr_storedpoweruplarge.draw(rPickedItemDst[iPickedItem].x, rPickedItemDst[iPickedItem].y, {iPowerup << 5, 0, 32, 32});
		}

        rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());

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

int editor_boundary()
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
					edit_mode = 8;  //change to edit mode using warps
					return EDITOR_EDIT;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
					short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
					short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (iButtonX >= 0 && iButtonX <= 9 && iButtonY >= 0 && iButtonY <= 9) {
							set_tile = iButtonX + 10 * iButtonY + 1;

							edit_mode = 8;  //change to edit mode using warps

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

		int color = SDL_MapSurfaceRGB(blitdest, 255, 0, 255);
		SDL_Rect r = {0, 0, 320, 320};
		SDL_FillSurfaceRect(blitdest, &r, color);

		rm->spr_worldforegroundspecial[0].draw(0, 0, {0, 0, 320, 320});

                rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());

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

int editor_type()
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
					edit_mode = 3;  //change to edit mode using doors/start
					return EDITOR_EDIT;

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

						//Start and doors
                    if (iButtonX >= 0 && iButtonX <= 5 && iButtonY == 0) {
							set_tile = iButtonX;
						}

						edit_mode = 3;  //change to edit mode using warps

						//The user must release the mouse button before trying to add a tile
						ignoreclick = true;

						return EDITOR_EDIT;
					}

					break;
				}

				default:
					break;
			}
		}


		drawmap(false, TILESIZE);
		menu_shade.draw(0, 0);

		rm->spr_worldforegroundspecial[0].draw(0, 0, {320, 128, 64, 32});
		rm->spr_worldforegroundspecial[0].draw(64, 0, {320, 192, 128, 32});

		rm->spr_worldforegroundspecial[0].draw(64, 0, {448, 64, 128, 32});

        rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());

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

int editor_water()
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
					edit_mode = 7;
					return EDITOR_EDIT;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonY == 0) {
                        if (iButtonX >= 0 && iButtonX <= 2) {
								set_tile = iButtonX + 4;
							}
						}

						ignoreclick = true;
						edit_mode = 7;
						return EDITOR_EDIT;
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

		for (short iWater = 0; iWater < 3; iWater++)
			rm->spr_worldbackground[0].draw(iWater << 5, 0, {512 + (iWater << 7), 0, 32, 32});

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

int editor_background()
{
	bool done = false;
	short iPage = 0;

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
                const SDL_Keycode key = event.key.key;
                if (key >= SDLK_1 && key <= SDLK_2) {
						iPage = key - SDLK_1;
                } else {
						edit_mode = 0;
						return EDITOR_EDIT;
					}

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

						short iTileStyleOffset = ((iButtonX / 4) + (iPage * 5)) * WORLD_BACKGROUND_SPRITE_SET_SIZE;

						iButtonX %= 4;

                    if (iButtonX == 0) {
							if (iButtonY == 0)
								set_tile = 0;
							else if (iButtonY >= 1 && iButtonY < 15)
								set_tile = iButtonY + 1;
                    } else if (iButtonX == 1) {
							if (iButtonY == 0)
								set_tile = 1;
							else if (iButtonY >= 1 && iButtonY < 15)
								set_tile = iButtonY + 15;
                    } else if (iButtonX == 2) {
							if (iButtonY >= 0 && iButtonY < 15)
								set_tile = iButtonY + 30;
                    } else if (iButtonX == 3) {
							if (iButtonY >= 0 && iButtonY < 15)
								set_tile = iButtonY + 45;
						}

						set_tile += iTileStyleOffset;

						ignoreclick = true;
						edit_mode = 0;
						return EDITOR_EDIT;
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

		rm->spr_worldbackground[0].draw(0, 0, {iPage * 640, 32, 640, 480});

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

int editor_stageforeground()
{
	bool done = false;
	short iForegroundScreen = 0;

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
                const SDL_Keycode key = event.key.key;

                if (key >= SDLK_1 && key <= SDLK_4) {
						iForegroundScreen = key - SDLK_1;
                } else {
						edit_mode = 1;
						return EDITOR_EDIT;
					}

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonX >= 0 && iButtonX < 10) {
                        if (iButtonY >= 0 && iButtonY < 10) {
								set_tile = WORLD_FOREGROUND_STAGE_OFFSET + iButtonY * 10 + iButtonX + iForegroundScreen * 100;

								ignoreclick = true;
								edit_mode = 1;
								return EDITOR_EDIT;
							}
						}
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

        for (short iRow = 0; iRow < 10; iRow++) {
            for (short iCol = 0; iCol < 10; iCol++) {
				rm->spr_worldforegroundspecial[0].draw(iCol << 5, iRow << 5, {384, iForegroundScreen << 5, 32, 32});
			}
		}

		rm->spr_worldforegroundspecial[0].draw(0, 0, {0, 0, 320, 320});

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

int editor_bridges()
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
					edit_mode = 1;
					return EDITOR_EDIT;

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonX >= 0 && iButtonX < 4) {
                        if (iButtonY >= 0 && iButtonY < 1) {
								set_tile = WORLD_BRIDGE_SPRITE_OFFSET + iButtonX;

								ignoreclick = true;
								edit_mode = 1;
								return EDITOR_EDIT;
							}
						}
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

		rm->spr_worldforegroundspecial[0].draw(0, 0, {320, 224, 128, 32});

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

int editor_structureforeground()
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
					edit_mode = 1;
					return EDITOR_EDIT;

					break;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonY >= 0 && iButtonY < 15) {
                        if (iButtonX >= 0 && iButtonX < 12) {
								set_tile = WORLD_FOREGROUND_SPRITE_OFFSET + iButtonX + iButtonY * 12;

								ignoreclick = true;
								edit_mode = 1;
								return EDITOR_EDIT;
                        } else if (iButtonX >= 12 && iButtonX < 14) {
								set_tile = WORLD_FOREGROUND_SPRITE_ANIMATED_OFFSET + iButtonY + (iButtonX - 12) * 15;

								ignoreclick = true;
								edit_mode = 1;
								return EDITOR_EDIT;
							}
						}
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

		rm->spr_worldforeground[0].draw(0, 0, {0, 0, 416, 480});
		rm->spr_worldforeground[0].draw(416, 0, {512, 0, 32, 480});

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

int editor_pathsprite()
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
					edit_mode = 4;
					return EDITOR_EDIT;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonX >= 0 && iButtonX < 8) {
							if (iButtonY >= 0 && iButtonY < 6)
								set_tile = iButtonY + 1 + iButtonX * WORLD_PATH_SPRITE_SET_SIZE;
						}

						ignoreclick = true;
						edit_mode = 4;
						return EDITOR_EDIT;
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);

        for (short iPath = 0; iPath < 8; iPath++) {
			rm->spr_worldpaths[0].draw(iPath << 5, 0, {(iPath % 4) * 160, (iPath / 4) * 320, 32, 192});
		}

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

//Display stages over vehicles
//allow setting of stages on vehicles

int editor_path()
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
					edit_mode = 2;
					return EDITOR_EDIT;
				}

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
						short iButtonX = bound_to_window_w(event.button.x) / TILESIZE;
						short iButtonY = bound_to_window_h(event.button.y) / TILESIZE;

                    if (iButtonX >= 0 && iButtonX <= 15 && iButtonY == 0) {
							set_tile = iButtonX + 1;

							ignoreclick = true;
							edit_mode = 2;
							return EDITOR_EDIT;
						}
					}

					break;
				}

				default:
					break;
			}
		}

		SDL_FillSurfaceRect(screen, nullptr, 0x0);
		spr_path.draw(0, 0, {0, 0, 480, 32});

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

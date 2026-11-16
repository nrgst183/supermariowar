#include "LevelEditorState.h"

namespace {
enum {MAPHAZARD_EDIT_STATE_SELECT, MAPHAZARD_EDIT_STATE_TYPE, MAPHAZARD_EDIT_STATE_LOCATION, MAPHAZARD_EDIT_STATE_PROPERTIES};

short iEditState = MAPHAZARD_EDIT_STATE_SELECT;
short iEditMapHazard = 0;

SDL_Rect rBackground[2];
//SDL_Rect rNewButton[2];
//SDL_Rect rTypeButton[8][4];
SDL_Rect rIconRects[MAXMAPHAZARDS][2];

const char * szHazardNames[8] = {"Fireballs", "Rotodisc", "Bullet Bill", "Flame Thrower", "Green Pirhana", "Red Pirhana", "Tall Pirhana", "Short Pirhana"};

bool editor_maphazards_initialized = false;
void init_editor_maphazards()
{
    if (editor_maphazards_initialized)
        return;

    rBackground[0].x = 0;
    rBackground[0].y = 0;
    rBackground[0].w = 256;
    rBackground[0].h = 224;

    rBackground[1].x = 320 - (rBackground[0].w >> 1);
    rBackground[1].y = 240 - (rBackground[0].h >> 1);
    rBackground[1].w = 256;
    rBackground[1].h = 224;

    rNewButton[0].x = 0;
    rNewButton[0].y = 352;
    rNewButton[0].w = 76;
    rNewButton[0].h = 32;

    rNewButton[1].x = rBackground[1].x + (rBackground[1].w >> 1) - (rNewButton[0].w >> 1);
    rNewButton[1].y = rBackground[1].y + rBackground[1].h + 8;
    rNewButton[1].w = 76;
    rNewButton[1].h = 32;

    for (short iType = 0; iType < 8; iType++) {
        rTypeButton[iType][0].x = 0;
        rTypeButton[iType][0].y = 0;
        rTypeButton[iType][0].w = 192;
        rTypeButton[iType][0].h = 32;

        rTypeButton[iType][1].x = 320 - (rTypeButton[iType][0].w >> 1);
        rTypeButton[iType][1].y = 84 + iType * 40;
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

    //Setup Map Hazard Icons
    for (short iMapHazard = 0; iMapHazard < MAXMAPHAZARDS; iMapHazard++) {
        rIconRects[iMapHazard][0].x = (iMapHazard % 8) * 32;
        rIconRects[iMapHazard][0].y = (iMapHazard / 8) * 32 + 224;
        rIconRects[iMapHazard][0].w = 32;
        rIconRects[iMapHazard][0].h = 32;

        rIconRects[iMapHazard][1].x = (iMapHazard % 6) * 40 + 204;
        rIconRects[iMapHazard][1].y = (iMapHazard / 6) * 40 + rBackground[1].y + 16;
        rIconRects[iMapHazard][1].w = 32;
        rIconRects[iMapHazard][1].h = 32;
    }

    editor_maphazards_initialized = true;
}
} // namespace

int editor_maphazards()
{
	init_editor_maphazards();

        //handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
				return EDITOR_QUIT;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_S) {
						savecurrentmap();
                } else if (event.key.key == SDLK_L) {
                    if (MAPHAZARD_EDIT_STATE_SELECT != iEditState) {
							iEditState = MAPHAZARD_EDIT_STATE_LOCATION;
						}
                } else if (event.key.key == SDLK_P) {
                    if (MAPHAZARD_EDIT_STATE_SELECT != iEditState) {
							iEditState = MAPHAZARD_EDIT_STATE_PROPERTIES;
						}
                } else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_9) {
                    if (MAPHAZARD_EDIT_STATE_SELECT == iEditState) {
                        short iHazard = event.key.key - SDLK_1;
                        if (iHazard < static_cast<short>(g_map->maphazards.size())) {
                            iEditMapHazard = iHazard;
                            iEditState = MAPHAZARD_EDIT_STATE_PROPERTIES;
                        }
                    } else if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

							if (hazard->itype == MapHazardType::FireBar || hazard->itype == MapHazardType::RotoDisc)
								hazard->iparam[0] = event.key.key - SDLK_1 + 1;
						}
                } else if (event.key.key == SDLK_DELETE) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState || MAPHAZARD_EDIT_STATE_LOCATION == iEditState) {
                        //Copy platforms into empty spot
                        for (short iMapHazard = iEditMapHazard; iMapHazard < static_cast<short>(g_map->maphazards.size()) - 1; iMapHazard++) {
                            g_map->maphazards[iMapHazard].itype = g_map->maphazards[iMapHazard + 1].itype;
                            g_map->maphazards[iMapHazard].ix = g_map->maphazards[iMapHazard + 1].ix;
                            g_map->maphazards[iMapHazard].iy = g_map->maphazards[iMapHazard + 1].iy;

                            for (short iParam = 0; iParam < NUMMAPHAZARDPARAMS; iParam++) {
                                g_map->maphazards[iMapHazard].iparam[iParam] = g_map->maphazards[iMapHazard + 1].iparam[iParam];
                                g_map->maphazards[iMapHazard].dparam[iParam] = g_map->maphazards[iMapHazard + 1].dparam[iParam];
                            }
                        }

                        g_map->maphazards.pop_back();
                        iEditState = MAPHAZARD_EDIT_STATE_SELECT;
                    }
                } else if (event.key.key == SDLK_ESCAPE) {
                    if (MAPHAZARD_EDIT_STATE_SELECT == iEditState) {
                            editor_maphazards_initialized = false;
							return EDITOR_EDIT;
                    } else if (MAPHAZARD_EDIT_STATE_LOCATION == iEditState || MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							iEditState = MAPHAZARD_EDIT_STATE_SELECT;
						}
                } else if (event.key.key == SDLK_KP_MINUS || event.key.key == SDLK_MINUS) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

							switch (hazard->itype) {
								case MapHazardType::FireBar:
								case MapHazardType::RotoDisc:
									if (g_map->maphazards[iEditMapHazard].dparam[0] > -0.05f)
										g_map->maphazards[iEditMapHazard].dparam[0] -= 0.005f;
									break;

								case MapHazardType::BulletBill:
									if (g_map->maphazards[iEditMapHazard].dparam[0] > -10.0f)
										g_map->maphazards[iEditMapHazard].dparam[0] -= 1.0f;

									if (g_map->maphazards[iEditMapHazard].dparam[0] == 0.0f)
										g_map->maphazards[iEditMapHazard].dparam[0] -= 1.0f;
									break;

								default:
									break;
							}
						}
                } else if (event.key.key == SDLK_KP_PLUS || event.key.key == SDLK_EQUALS) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

							switch (hazard->itype) {
								case MapHazardType::FireBar:
								case MapHazardType::RotoDisc:
									if (g_map->maphazards[iEditMapHazard].dparam[0] < 0.05f)
										g_map->maphazards[iEditMapHazard].dparam[0] += 0.005f;
									break;

								case MapHazardType::BulletBill:
									if (g_map->maphazards[iEditMapHazard].dparam[0] < 10.0f)
										g_map->maphazards[iEditMapHazard].dparam[0] += 1.0f;

									if (g_map->maphazards[iEditMapHazard].dparam[0] == 0.0f)
										g_map->maphazards[iEditMapHazard].dparam[0] += 1.0f;
									break;

								default:
									break;
							}
						}
                } else if (event.key.key == SDLK_COMMA || event.key.key == SDLK_LEFTBRACKET) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

                        if (hazard->itype >= MapHazardType::BulletBill && hazard->itype <= MapHazardType::PirhanaShort) {
								if (g_map->maphazards[iEditMapHazard].iparam[0] > 30)
									g_map->maphazards[iEditMapHazard].iparam[0] -= 30;
							}
						}
                } else if (event.key.key == SDLK_PERIOD || event.key.key == SDLK_RIGHTBRACKET) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

                        if (hazard->itype >= MapHazardType::BulletBill && hazard->itype <= MapHazardType::PirhanaShort) {
								if (g_map->maphazards[iEditMapHazard].iparam[0] < 480)
									g_map->maphazards[iEditMapHazard].iparam[0] += 30;
							}
						}
                } else if (event.key.key == SDLK_D) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];

							switch (hazard->itype) {
								case MapHazardType::FlameCannon:
								case MapHazardType::PirhanaGreen:
								case MapHazardType::PirhanaRed:
								case MapHazardType::PirhanaTall:
								case MapHazardType::PirhanaShort:
									if (++hazard->iparam[1] > 3)
										hazard->iparam[1] = 0;
									break;

								case MapHazardType::BulletBill:
									g_map->maphazards[iEditMapHazard].dparam[0] = -g_map->maphazards[iEditMapHazard].dparam[0];
									break;

								default:
									break;
							}
						}
                } else if (event.key.key == SDLK_N) {
                    if (MAPHAZARD_EDIT_STATE_SELECT == iEditState) {
							iEditMapHazard = NewMapHazard();
							iEditState = MAPHAZARD_EDIT_STATE_TYPE;
						}
					}

					break;
				}
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT && !ignoreclick) {
						short iClickX = event.button.x;
						short iClickY = event.button.y;

                    if (MAPHAZARD_EDIT_STATE_SELECT == iEditState) {
                        //check clicks on existing platforms
                        for (int iMapHazard = 0; iMapHazard < static_cast<int>(g_map->maphazards.size()); iMapHazard++) {
                            if (iClickX >= rIconRects[iMapHazard][1].x && iClickX < rIconRects[iMapHazard][1].x + rIconRects[iMapHazard][1].w &&
                                iClickY >= rIconRects[iMapHazard][1].y && iClickY < rIconRects[iMapHazard][1].y + rIconRects[iMapHazard][1].h) {
                                iEditMapHazard = iMapHazard;
                                iEditState = MAPHAZARD_EDIT_STATE_PROPERTIES;
                                ignoreclick = true;

                                break;
                            }
                        }

                        //check click on the new button
                        if (g_map->maphazards.size() < MAXMAPHAZARDS && iClickX >= rNewButton[1].x && iClickX < rNewButton[1].x + rNewButton[1].w &&
                            iClickY >= rNewButton[1].y && iClickY < rNewButton[1].y + rNewButton[1].h) {
                            iEditMapHazard = NewMapHazard();
                            iEditState = MAPHAZARD_EDIT_STATE_TYPE;
                            ignoreclick = true;
                        }
                    } else if (MAPHAZARD_EDIT_STATE_TYPE == iEditState && !ignoreclick) {
                        for (int iType = 0; iType < 8; iType++) {
								if (iClickX >= rTypeButton[iType][1].x && iClickX < rTypeButton[iType][1].x + rTypeButton[iType][1].w &&
                                    iClickY >= rTypeButton[iType][1].y && iClickY < rTypeButton[iType][1].y + rTypeButton[iType][1].h) {
									MapHazard * hazard = &g_map->maphazards[iEditMapHazard];
									const MapHazardType type = static_cast<MapHazardType>(iType);
									hazard->itype = type;
									iEditState = MAPHAZARD_EDIT_STATE_LOCATION;
									ignoreclick = true;

									//Set some default values for the selected type
									switch (type) {
										case MapHazardType::FireBar:
											hazard->iparam[0] = 5; //Number of fireballs in string
											hazard->dparam[0] = 0.02f; //Angular velocity
											hazard->dparam[1] = 0.0f; //Start Angle
											break;

										case MapHazardType::RotoDisc:
											hazard->iparam[0] = 2; //Number of fireballs in string
											hazard->dparam[0] = 0.02f; //Angular velocity
											hazard->dparam[1] = 0.0f; //Start Angle
											hazard->dparam[2] = 112.0f; //Radius
											break;

										case MapHazardType::BulletBill:
											hazard->iparam[0] = 120; //Frequency
											hazard->dparam[0] = 2.0f; //Velocity
											break;

										case MapHazardType::FlameCannon:
											hazard->iparam[0] = 120; //Frequency
											hazard->iparam[1] = 0; //Direction
											break;

										case MapHazardType::PirhanaGreen:
										case MapHazardType::PirhanaRed:
										case MapHazardType::PirhanaTall:
										case MapHazardType::PirhanaShort:
											hazard->iparam[0] = 300; //Frequency
											hazard->iparam[1] = 0; //Direction
											break;
									}

									break;
								}
							}
                    } else if (MAPHAZARD_EDIT_STATE_LOCATION == iEditState && !ignoreclick) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];
							hazard->ix = iClickX / 16;
							hazard->iy = iClickY / 16;
                    } else if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState && !ignoreclick) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];
							switch (hazard->itype) {
								case MapHazardType::FireBar: //Edit fireball string
								case MapHazardType::RotoDisc: //rotodisc
									AdjustMapHazardRadius(hazard, iClickX, iClickY);
									break;

								default:
									break;
							}
						}

						break;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazardType iType = g_map->maphazards[iEditMapHazard].itype;
                        if (iType == MapHazardType::FireBar) { //Edit fireball string

                        } else if (iType == MapHazardType::RotoDisc) { //rotodisc

							}
						}
					}
				}
            [[fallthrough]];
            case SDL_EVENT_MOUSE_MOTION: {
					bound_mouse_motion_coords();
					short iClickX = bound_to_window_w(event.motion.x);
					short iClickY = bound_to_window_h(event.motion.y);

                if (event.motion.state == SDL_BUTTON_MASK(SDL_BUTTON_LEFT) && !ignoreclick) {
                    if (MAPHAZARD_EDIT_STATE_LOCATION == iEditState && !ignoreclick) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];
							hazard->ix = iClickX / 16;
							hazard->iy = iClickY / 16;
                    } else if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
							MapHazard * hazard = &g_map->maphazards[iEditMapHazard];
							switch (hazard->itype) {
								case MapHazardType::FireBar: //Edit fireball string
								case MapHazardType::RotoDisc: //rotodisc
									AdjustMapHazardRadius(hazard, iClickX, iClickY);
									break;

								default:
									break;
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

		//Draw platform editing
		drawmap(false, TILESIZE);

		rm->menu_shade.draw(0, 0);

        if (MAPHAZARD_EDIT_STATE_SELECT == iEditState) {
			s_platform.draw(rBackground[0], screen, rBackground[1]);

			if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Map Hazard Mode: [esc] Exit");

			for (size_t iMapHazard = 0; iMapHazard < g_map->maphazards.size(); iMapHazard++)
				s_platform.draw(rIconRects[iMapHazard][0], screen, rIconRects[iMapHazard][1]);

			if (g_map->maphazards.size() < MAXMAPHAZARDS)
				s_platform.draw(rNewButton[0], screen, rNewButton[1]);

			rm->menu_font_small.drawCentered(320, rBackground[1].y - 18, "Hazards");
        } else if (MAPHAZARD_EDIT_STATE_TYPE == iEditState) {
			//s_platform.draw(rBackground[0], screen, &rBackground[1]);

			//Draw map hazard options
            for (short iType = 0; iType < 8; iType++) {
				s_maphazardbuttons.draw(rTypeButton[iType][0], screen, rTypeButton[iType][1]);
				s_maphazardbuttons.draw(rTypeButton[iType][2], screen, rTypeButton[iType][3]);

				rm->menu_font_large.draw(rTypeButton[iType][1].x + 36, rTypeButton[iType][1].y + 6, szHazardNames[iType]);
			}

			if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Choose Hazard Type");
        } else if (MAPHAZARD_EDIT_STATE_LOCATION == iEditState) {
			const MapHazard& hazard = g_map->maphazards[iEditMapHazard];
			DrawMapHazard(hazard, 0, true, blitdest);
			DrawMapHazardControls(hazard);

			if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Location: [esc] Exit, [p] Properties, [LMB] Set Location");
        } else if (MAPHAZARD_EDIT_STATE_PROPERTIES == iEditState) {
            const MapHazard& hazard = g_map->maphazards[iEditMapHazard];
            DrawMapHazard(hazard, 0, true, blitdest);
            DrawMapHazardControls(hazard);

            switch (hazard.itype) {
                case MapHazardType::FireBar:
                case MapHazardType::RotoDisc:
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 3, "Properties");
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight() * 2, "[esc] Exit, [l] Location, [+/-] Velocity, [LMB] Angle and Radius");

                    if (OLD_HUD && hazard.itype == MapHazardType::RotoDisc)
                        rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[Shift + LMB] Snap To Angle, [1-9] Number of Rotodiscs");
                    else if (OLD_HUD)
                        rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[Shift + LMB] Snap To Angle");
                    break;

                case MapHazardType::BulletBill:
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - (rm->menu_font_small.getHeight() << 1), "Properties: [esc] Exit, [l] Location, [d] Direction");
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[-/+] Velocity, [[/]] or [</>] Frequency");
                    break;

                case MapHazardType::FlameCannon:
                case MapHazardType::PirhanaGreen:
                case MapHazardType::PirhanaRed:
                case MapHazardType::PirhanaTall:
                case MapHazardType::PirhanaShort:
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - (rm->menu_font_small.getHeight() << 1), "Properties: [esc] Exit, [l] Location");
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "[[/]] or [</>] Frequency, [d] direction");
                    break;

                default:
                    if (OLD_HUD) rm->menu_font_small.draw(0, 480 - rm->menu_font_small.getHeight(), "Properties: [esc] Exit, [l] Location");
                    break;
            }
        }

        if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());

        DrawMessage();
        return EDITOR_MAPHAZARDS;
}

short NewMapHazard()
{
    MapHazard hazard;
    hazard.itype = MapHazardType::FireBar;
    hazard.ix = 10;
    hazard.iy = 7;

    for (short iMapHazard = 0; iMapHazard < NUMMAPHAZARDPARAMS; iMapHazard++) {
        hazard.iparam[iMapHazard] = 0;
        hazard.dparam[iMapHazard] = 0.f;
    }

    g_map->maphazards.emplace_back(std::move(hazard));
    return g_map->maphazards.size() - 1;
}

void DrawMapHazardControls(const MapHazard& hazard)
{
    if (hazard.itype == MapHazardType::FireBar || hazard.itype == MapHazardType::RotoDisc || hazard.itype == MapHazardType::BulletBill) {
		short iVelMarkerX = 0;

		if (hazard.itype == MapHazardType::BulletBill)
			iVelMarkerX = static_cast<short>(hazard.dparam[0] + 10.0f) * 12 + 196;
		else
			iVelMarkerX = static_cast<short>((hazard.dparam[0] + 0.05f) / 0.005f) * 12 + 196;

		SDL_Rect rVel[2] = {{0, 400, 244, 17},{198, 420, 244, 17}};
		s_platform.draw(rVel[0], screen, rVel[1]);

		SDL_Rect rMarker[2] = {{244,400,8,18},{iVelMarkerX,418,8,18}};
		s_platform.draw(rMarker[0], screen, rMarker[1]);

        if (hazard.itype == MapHazardType::BulletBill) {
			rm->menu_font_small.drawRightJustified(190, 420, "Left");
			rm->menu_font_small.draw(450, 420, "Right");
        } else {
			rm->menu_font_small.drawRightJustified(190, 420, "Counter Clockwise");
			rm->menu_font_small.draw(450, 420, "Clockwise");
		}
	}

    if (hazard.itype >= MapHazardType::BulletBill && hazard.itype <= MapHazardType::PirhanaShort) { // Draw frequency for bullet bill and flame cannon
		short iFreqMarkerX = ((hazard.iparam[0] / 30) - 1) * 12 + 196;

		SDL_Rect rVel[2] = {{0, 384, 184, 13},{198, 390, 184, 13}};
		s_platform.draw(rVel[0], screen, rVel[1]);

		SDL_Rect rMarker[2] = {{244,400,8,18},{iFreqMarkerX,388,8,18}};
		s_platform.draw(rMarker[0], screen, rMarker[1]);

		rm->menu_font_small.drawRightJustified(190, 390, "More Frequent");
		rm->menu_font_small.draw(388, 390, "Less Frequent");
	}
}

void AdjustMapHazardRadius(MapHazard * hazard, short iClickX, short iClickY)
{
	short iDiffX = iClickX - ((hazard->ix << 4) + 16);
	short iDiffY = iClickY - ((hazard->iy << 4) + 16);

	float radius = static_cast<float>(sqrt(static_cast<double>(iDiffX * iDiffX + iDiffY * iDiffY)));
	float angle = static_cast<float>(atan2(static_cast<double>(iDiffY), static_cast<double>(iDiffX)));

	if (angle < 0.0f)
		angle += TWO_PI;

    const bool* keystate = SDL_GetKeyboardState(nullptr);
    if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT)) {
		float dSector = TWO_PI / 16;
		angle += TWO_PI / 32;

		if (angle > TWO_PI)
			angle -= TWO_PI;

        for (short iSector = 0; iSector < 16; iSector++) {
            if (angle >= dSector * iSector && angle < dSector * (iSector + 1)) {
				angle = dSector * iSector;
				break;
			}
		}
	}

    switch (hazard->itype) {
        case MapHazardType::FireBar:
            if (radius >= 24.0f) {
				hazard->iparam[0] = static_cast<short>(radius) / 24 + 1;
				hazard->dparam[1] = angle;
            } else {
				hazard->iparam[0] = 1;
				hazard->dparam[1] = 0.0f;
			}
            break;

        case MapHazardType::RotoDisc:
			hazard->dparam[1] = angle;

            if (radius > 32.0f) {
                if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT)) {
					//Snap radius to every 16 pixels
					hazard->dparam[2] = static_cast<float>((static_cast<int>(radius - 16.0f) >> 4) << 4);
                } else {
					hazard->dparam[2] = radius - 16.0f;
				}
            } else {
				hazard->dparam[2] = 16.0f;
			}
            break;

        default:
            break;
    }
}

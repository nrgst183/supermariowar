#include "LevelEditorState.h"

int display_help()
{
	//unsigned int currentChar = 0;

	drawmap(false, TILESIZE);
	rm->menu_shade.draw(0, 0);
	//rm->menu_font_large.drawCentered(320, 15, "Help");

	int offsety = 10;
	int offsetx = 20;
	rm->menu_font_small.draw(offsetx, offsety, "Modes:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[t] - Tile Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[i] - Block Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[o] - Map Item Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[w] - Warp Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[m] - Move Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[l] - Tile Type Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[p] - Platform Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[x] - No Player Spawn Area");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[z] - No Item Spawn Area");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[h] - Hazard Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[a] - Animated Tile Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[j] - Race Goals Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[k] - Block Properties");
	offsety += rm->menu_font_small.getHeight() + 12;

	rm->menu_font_small.draw(offsetx, offsety, "Layers:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[v] - Hide Blocks");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[y] - Select Active Tile Layer");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[u] - Hide Inactive Tile Layers");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[end] - Optimize Layers");
	offsety += rm->menu_font_small.getHeight() + 12;

	rm->menu_font_small.draw(offsetx, offsety, "Miscellaneous:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[b] - Background Thumbnails");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[g] - Change Backgrounds");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[r] - Change Music Category");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[e] - Change Floating Eyecandy");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[ctrl] + [delete] - Clear All");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[insert] - Take Screenshot");
	offsety += rm->menu_font_small.getHeight() + 2;


	offsetx = 305;
	offsety = 10;

	rm->menu_font_small.draw(offsetx, offsety, "File:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[n] - New Map");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[s] - Save Map");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[shift] + [s] - Save As");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[f] - Find Map");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[shift] + [f] - New Search");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[pageup] - Go To Previous Map");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[pagedown] - Go To Next Map");
	offsety += rm->menu_font_small.getHeight() + 20;

	rm->menu_font_small.draw(offsetx, offsety, "Tile, Warp and Block Modes:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Left Mouse Button] - Place Item");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Right Mouse Button] - Remove Item");
	offsety += rm->menu_font_small.getHeight() + 20;

	rm->menu_font_small.draw(offsetx, offsety, "Move Mode:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Left Mouse Button] - Select Area");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Right Mouse Button] - Unselect Area");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "Select And Drag - Move Selections");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "Hold [shift] - Multiple Selections");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "Hold [ctrl] - Freehand Selections");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[delete] - Delete Selection");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[c] - Copy Selection");
	offsety += rm->menu_font_small.getHeight() + 20;

	rm->menu_font_small.draw(offsetx, offsety, "Platforms:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[p] - Path");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[+/-] - Change Speed");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[delete] - Delete");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[alt] + [enter] - Full Screen/Window");


		//handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
				case SDL_EVENT_QUIT:
					return 0;
				break;

				case SDL_EVENT_KEY_DOWN:
					return 0;
				break;

				default:
					break;
			}
		}

	return DISPLAY_HELP;
}

int save_as()
{
	char fileName[FILEBUFSIZE] = "";

    if (dialog("Save As", "Enter name:", fileName, 64)) {
		std::string mapName = fileName;
		save_map(convertPath("maps/" + mapName + ".map"));
		mapName += ".map";
		maplist->add(mapName.c_str());
		maplist->find(mapName);
		loadcurrentmap();
	}

	return 0;
}

bool dialog(const char * title, const char * instructions, char * input, int inputsize)
{
#ifdef SMW_EMBEDDED_EDITOR
    //(a controller: the on-screen keyboard)
    ::editors::input::TextEntry textEntry(title, instructions, input);
#endif
// FIXME
#ifdef __EMSCRIPTEN__
    return false;
#endif
	unsigned int currentChar = 0;

	drawmap(false, TILESIZE);
	rm->menu_shade.draw(0, 0);
	rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
	rm->menu_font_large.drawCentered(320, 200, title);
	rm->menu_font_small.draw(240, 235, instructions);
	if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());
	gfx_flipscreen();

    while (true) {
		int framestart = SDL_GetTicks();

		//handle messages
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
				case SDL_EVENT_QUIT:
					return false;
				break;

				case SDL_EVENT_KEY_DOWN:

                if (event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_RETURN) {
						return true;
                } else if (event.key.key == SDLK_ESCAPE) {
						return false;
                } else if (event.key.key == SDLK_BACKSPACE) {
                    if (currentChar > 0) {
							input[currentChar-1] = '\0';

							drawmap(false, TILESIZE);
							rm->menu_shade.draw(0, 0);
							rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
							rm->menu_font_large.drawCentered(320, 200, title);
							rm->menu_font_small.draw(240, 235, instructions);
							rm->menu_font_small.draw(240, 255, input);
							if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());
							gfx_flipscreen();

							currentChar--;
						}
                } else {

						/* I realize the if statement below is long and can be substituted with
						the function isalnum(event.key.key) but I did it this way because
						isalnum acts funny (ie wrong) when the number pad is pressed. */
                    if ((isdigit(event.key.key) || event.key.key == 45 || event.key.key == 32 || event.key.key == 61 || (event.key.key >= 95 && event.key.key <= 122)) && currentChar < static_cast<unsigned>(inputsize) - 1) {
							//insert character into fileName and onScreenText and increment current char
							Uint8 key = event.key.key;

                        const bool* keystate = SDL_GetKeyboardState(nullptr);
                        if (CheckKey(keystate, SDLK_LSHIFT) || CheckKey(keystate, SDLK_RSHIFT)) {
								if (event.key.key == 45)
									key = 95;
								else if (event.key.key >= 95 && event.key.key <= 122)
									key-= 32;  //Capitalize
								else if (event.key.key == 48)
									key = 41;
								else if (event.key.key == 49)
									key = 33;
								else if (event.key.key == 50)
									key = 64;
								else if (event.key.key == 51)
									key = 35;
								else if (event.key.key == 52)
									key = 36;
								else if (event.key.key == 53)
									key = 37;
								else if (event.key.key == 54)
									key = 94;
								else if (event.key.key == 55)
									key = 38;
								else if (event.key.key == 57)
									key = 40;
								else if (event.key.key == 61)
									key = 43;
							}

							input[currentChar] = key;
							currentChar++;
							input[currentChar] = '\0';

							drawmap(false, TILESIZE);
							rm->menu_shade.draw(0, 0);
							rm->spr_dialog.draw(224, 176, {0, 0, 192, 128});
							rm->menu_font_large.drawCentered(320, 200, title);
							rm->menu_font_small.draw(240, 235, instructions);
							rm->menu_font_small.draw(240, 255, input);
							if (OLD_HUD) rm->menu_font_small.drawRightJustified(640, 0, maplist->currentFilename().c_str());
							gfx_flipscreen();
						}
					}
				break;

				default:
					break;
			}
		}

		int delay = WAITTIME - (SDL_GetTicks() - framestart);
		if (delay < 0)
			delay = 0;
		else if (delay > WAITTIME)
			delay = WAITTIME;

		SDL_Delay(delay);
	}

	return false;
}

int find()
{
	char fileName[FILEBUFSIZE] = "";
	//char mapLocation[FILEBUFSIZE] = "maps/";

    if (dialog("Find Map", "Enter name:", fileName, 64)) {
		findstring = fileName;

        if (maplist->find(findstring)) {
			loadcurrentmap();
		}
	}

	return 0;
}

int clearMap()
{
	g_map->clearMap();
	g_iNumPlatforms = 0;


	printf("Map Cleared\n");
	return 0;
}

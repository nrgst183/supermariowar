#include "WorldEditorState.h"

int display_help()
{
	//unsigned int currentChar = 0;

	drawmap(false, TILESIZE);
	menu_shade.draw(0, 0);
	rm->menu_font_large.drawCentered(320, 15, "Help");

	int offsety = 55;
	int offsetx = 30;
	rm->menu_font_small.draw(offsetx, offsety, "Modes:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[1] - Water Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[2] - Land Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[3] - Stage Objects Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[4] - Path Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[5] - Objects Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[6] - Bridges Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[p] - Connection Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[w] - Warp Mode");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[v] - Vehicle");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "      [c] - Copy Vehicle");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[t] - Start and Doors");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[b] - Vehicle Boundaries");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[i] - Initial Powerups");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[e] - Edit Stages");
	offsety += rm->menu_font_small.getHeight() + 20;

	rm->menu_font_small.draw(offsetx, offsety, "File:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[n] - New World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[s] - Save World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[shift] + [s] - Save As");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[f] - Find World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[shift] + [f] - New Search");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[pageup] - Go To Previous World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[pagedown] - Go To Next World");

	offsetx = 300;
	offsety = 55;

	rm->menu_font_small.draw(offsetx, offsety, "Place Tiles:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Left Mouse Button] - Place Item");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Right Mouse Button] - Remove Item");
	offsety += rm->menu_font_small.getHeight() + 20;


	rm->menu_font_small.draw(offsetx, offsety, "Miscellaneous:");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[r] - Change Music Category");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[Arrow Keys] - Navigate World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[a] - Automatic Path/Land");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[k] - Resize World");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[ctrl] + [delete] - Clear All");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[insert] - Screenshot");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[alt] + [enter] - Full Screen/Window");
	offsety += rm->menu_font_small.getHeight() + 2;
	rm->menu_font_small.draw(offsetx, offsety, "[space] - Toggle Stage Previews");

	gfx_flipscreen();

    while (true) {
		int framestart = SDL_GetTicks();

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

		int delay = WAITTIME - (SDL_GetTicks() - framestart);
		if (delay < 0)
			delay = 0;
		else if (delay > WAITTIME)
			delay = WAITTIME;

		SDL_Delay(delay);
	}

	return 0;
}

int save_as()
{
	char fileName[FILEBUFSIZE] = "";

    if (dialog("Save As", "Enter name:", fileName, 64)) {
        std::string worldName = std::string(fileName) + ".txt";
        worldlist->add("worlds/" + worldName);
        worldlist->find(worldName);
        game_values.worldindex = worldlist->currentIndex();
		savecurrentworld();
		loadcurrentworld();
	}

	return 0;
}

bool dialog(const char * title, const char * instructions, char * input, int inputsize)
{
#ifdef SMW_EMBEDDED_EDITOR
    //(a controller: the on-screen keyboard)
    ::editors::input::TextEntry textEntry;
#endif
	unsigned int currentChar = 0;

	drawmap(false, TILESIZE);
	menu_shade.draw(0, 0);
	spr_dialog.draw(224, 176, {0, 0, 192, 128});
	rm->menu_font_large.drawCentered(320, 200, title);
	rm->menu_font_small.draw(240, 235, instructions);
    rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());
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
							menu_shade.draw(0, 0);
							spr_dialog.draw(224, 176, {0, 0, 192, 128});
							rm->menu_font_large.drawCentered(320, 200, title);
							rm->menu_font_small.draw(240, 235, instructions);
							rm->menu_font_small.draw(240, 255, input);
                        rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());
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
							menu_shade.draw(0, 0);
							spr_dialog.draw(224, 176, {0, 0, 192, 128});
							rm->menu_font_large.drawCentered(320, 200, title);
							rm->menu_font_small.draw(240, 235, instructions);
							rm->menu_font_small.draw(240, 255, input);
                        rm->menu_font_small.drawRightJustified(640, 0, worldlist->currentPath().string().c_str());
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

        if (worldlist->find(findstring)) {
            game_values.worldindex = worldlist->currentIndex();
			loadcurrentworld();
		}
	}

	return 0;
}

int clear_world()
{
	g_worldmap.Clear();
	updateworldsurface();

	printf("World Cleared\n");
	return 0;
}

void loadcurrentworld()
{
	game_values.worldindex = worldlist->currentIndex();
	g_worldmap = loadWorldMap(worldlist->at(game_values.worldindex).string(), TILESIZE);
	ReadVehiclesIntoEditor();
	ReadWarpsIntoEditor();

	setupworldview();
}

// The view of the world (after it was loaded or its size changed)
void setupworldview()
{
	draw_offset_col = 0;
	draw_offset_row = 0;

	rectSrcSurface.x = 0;
	rectSrcSurface.y = 0;
	fNeedBlackBackground = false;

	g_worldmap.GetWorldSize(&iWorldWidth, &iWorldHeight);

    if (iWorldWidth >= 20) {
		rectSrcSurface.w = 640;
		draw_offset_x = 0;
    } else {
		rectSrcSurface.w = iWorldWidth * TILESIZE;
		draw_offset_x = (640 - iWorldWidth * TILESIZE) >> 1;
		fNeedBlackBackground = true;
	}

    if (iWorldHeight >= 15) {
		rectSrcSurface.h = 480;
		draw_offset_y = 0;
    } else {
		rectSrcSurface.h = iWorldHeight * TILESIZE;
		draw_offset_y = (480 - iWorldHeight * TILESIZE) >> 1;
		fNeedBlackBackground = true;
	}

	rectDstSurface.x = draw_offset_x;
	rectDstSurface.y = draw_offset_y;

	updateworldsurface();
}

int savecurrentworld()
{
	SetDisplayMessage(60, "Saved", "Your world has", "been saved.", "");

	WriteVehiclesIntoWorld();
	WriteWarpsIntoWorld();
	saveWorldMap(g_worldmap, worldlist->at(game_values.worldindex).string());
	return 0;
}

void SetDisplayMessage(short iTime,
                       const char * szTitle,
                       const char * szLine1,
                       const char * szLine2,
                       const char * szLine3)
{
	g_messagedisplaytimer = iTime;
	g_szMessageTitle = szTitle;
	g_szMessageLine[0] = szLine1;
	g_szMessageLine[1] = szLine2;
	g_szMessageLine[2] = szLine3;
}

int findcurrentstring()
{
    if (!findstring.empty()) {
        if (worldlist->find(findstring)) {
            game_values.worldindex = worldlist->currentIndex();
			loadcurrentworld();
		}
	}

	return 0;
}

int new_world()
{
	char fileName[FILEBUFSIZE] = "";
	char szWidth[5], szHeight[5];

	if (dialog("New World", "Enter name:", fileName, 64) &&
		dialog("New World", "Width:", szWidth, 4) &&
            dialog("New World", "Height:", szHeight, 4)) {
		short iWidth = atoi(szWidth);
		short iHeight = atoi(szHeight);

		if (iWidth < 1)
			iWidth = 1;

		if (iHeight < 1)
			iHeight = 1;

		vehiclelist.clear();

		warplist.clear();

		g_worldmap = WorldMap(iWidth, iHeight);
        std::string worldName = std::string(fileName) + ".txt";
        worldlist->add(convertPath("worlds/" + worldName).c_str());
        worldlist->find(worldName);
        game_values.worldindex = worldlist->currentIndex();
		savecurrentworld();
		loadcurrentworld();
	}

	return 0;
}


int resize_world()
{
	char szWidth[5], szHeight[5];

	if (dialog("Resize World", "Width:", szWidth, 4) &&
            dialog("Resize World", "Height:", szHeight, 4)) {
		short iWidth = atoi(szWidth);
		short iHeight = atoi(szHeight);

		if (iWidth < 1)
			iWidth = 1;

		if (iHeight < 1)
			iHeight = 1;

		auto itrVehicle = vehiclelist.begin(), limVehicle = vehiclelist.end();
        while (itrVehicle != limVehicle) {
			if ((*itrVehicle)->getCurrentTile().x >= iWidth || (*itrVehicle)->getCurrentTile().y >= iHeight) {
				RemoveVehicleFromTile((*itrVehicle)->getCurrentTile().x, (*itrVehicle)->getCurrentTile().y);
				//List was modified, restart.
				itrVehicle = vehiclelist.begin();
				limVehicle = vehiclelist.end();
			}
			else
				itrVehicle++;
		}

		warplist.clear();

		g_worldmap.Resize(iWidth, iHeight);

		savecurrentworld();
		loadcurrentworld();
	}

	return 0;
}

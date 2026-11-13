#include "LevelEditorState.h"

void drawlayer(int layer, bool fUseCopied, short iBlockSize)
{
	const DrawSize drawsize = iBlockSize == TILESIZE ? DrawSize::Ingame : iBlockSize == PREVIEWTILESIZE ? DrawSize::Preview : DrawSize::Thumbnail;

	//draw left to right full vertical
    for (short i = 0; i < MAPWIDTH; i++) {
        for (short j = 0; j < MAPHEIGHT; j++) {
			TilesetTile * tile = nullptr;
			if ((move_mode == 1 || move_mode == 3) && i - move_offset_x >= 0 && i - move_offset_x < MAPWIDTH &&
				j - move_offset_y >= 0 && j - move_offset_y < MAPHEIGHT &&
                    selectedtiles[i - move_offset_x][j - move_offset_y]) {
				if (fUseCopied)
					tile = &copiedtiles[i - move_offset_x][j - move_offset_y].tile[layer];
            } else {
				if (!fUseCopied)
					tile = &g_map->mapdata[i][j][layer];
			}

			if (!tile || tile->iID == TILESETNONE)
				continue;

            if (tile->iID >= 0) {
				g_tilesetmanager->Draw(screen, tile->iID, drawsize, tile->iCol, tile->iRow, i, j);
            } else if (tile->iID == TILESETANIMATED) {
				short iSrcCol = tile->iCol << 2;
				short iSrcRow = tile->iRow;

                if (iSrcCol > 31 || iSrcCol < 0 || iSrcRow > 31 || iSrcRow < 0) {
					iSrcCol = 0;
					iSrcRow = 0;
				}

                rm->spr_tileanimation[static_cast<size_t>(drawsize)].draw(CTilesetManager::rect(drawsize, iSrcCol, iSrcRow), screen, CTilesetManager::rect(drawsize, i, j));
            } else if (tile->iID == TILESETUNKNOWN) {
                rm->spr_unknowntile[static_cast<size_t>(drawsize)].draw(CTilesetManager::rect(drawsize, 0, 0), screen, CTilesetManager::rect(drawsize, i, j));
			}
		}
	}
}

void drawmap(bool fScreenshot, short iBlockSize, bool fWithPlatforms)
{
    if (iBlockSize != TILESIZE) {
		SDL_Rect srcrect;
		srcrect.x = 0;
		srcrect.y = 0;
		srcrect.w = 640;
		srcrect.h = 480;

		SDL_Rect dstrect;
		dstrect.x = 0;
		dstrect.y = 0;
		dstrect.w = iBlockSize * 20;
		dstrect.h = iBlockSize * 15;

                rm->spr_background.drawStretch(srcrect, blitdest, dstrect);
    } else {
		rm->spr_background.draw(0,0);
	}

	if ((view_only_layer && selected_layer == 0) || !view_only_layer)
		drawlayer(0, false, iBlockSize);

    if (!fScreenshot) {
		if ((view_only_layer && copiedlayer == 0) || !view_only_layer)
			drawlayer(0, true, iBlockSize);
	}

	if ((view_only_layer && selected_layer == 1) || !view_only_layer)
		drawlayer(1, false, iBlockSize);

    if (!fScreenshot) {
		if ((view_only_layer && copiedlayer == 1) || !view_only_layer)
			drawlayer(1, true, iBlockSize);
	}

	if (fWithPlatforms)
		g_map->drawPlatforms(0);

    if ((viewblocks && !view_only_layer) || fScreenshot) {
		const DrawSize drawsize = iBlockSize == 32 ? DrawSize::Ingame : iBlockSize == 16 ? DrawSize::Preview : DrawSize::Thumbnail;

		SDL_Rect rSrc = {0, 0, iBlockSize, iBlockSize};

		MapBlock * block = nullptr;
        for (int j = 0; j < MAPHEIGHT; j++) {
            for (int i = 0; i < MAPWIDTH; i++) {
				if ((move_mode == 1 || move_mode == 3) && i - move_offset_x >= 0 && i - move_offset_x < MAPWIDTH &&
					j - move_offset_y >= 0 && j - move_offset_y < MAPHEIGHT &&
                        selectedtiles[i - move_offset_x][j - move_offset_y]) {
					block = &copiedtiles[i - move_offset_x][j - move_offset_y].block;
                } else {
					block = &g_map->objectdata[i][j];
				}

				int blocktype = block->iType;

                if (blocktype > -1) {
					//Don't screenshot hidden blocks
					if (fScreenshot && g_map->objectdata[i][j].fHidden)
						continue;

                    if (blocktype < 7) {
						rSrc.x = blocktype * iBlockSize;
						rSrc.y = 0;
                    } else if (blocktype >= 7 && blocktype <= 10) { //On/Off Blocks
						rSrc.x = blocktype * iBlockSize;
						rSrc.y = iBlockSize * (1 - g_map->iSwitches[(blocktype - 7) % 4]);
                    } else if (blocktype >= 11 && blocktype <= 14) { //Switched Blocks
						rSrc.x = blocktype * iBlockSize;
						rSrc.y = iBlockSize * (1 - block->iSettings[0]);
                    } else if (blocktype >= 15 && blocktype <= 19) {
						rSrc.x = (blocktype - 15) * iBlockSize;
						rSrc.y = iBlockSize;
                    } else if (blocktype >= 20 && blocktype <= 29) {
						rSrc.x = (blocktype - 20) * iBlockSize;
						rSrc.y = iBlockSize << 1;
					}

                    rm->spr_blocks[static_cast<size_t>(drawsize)].draw(rSrc, screen, CTilesetManager::rect(drawsize, i, j));
				}
			}
		}
	}

	if (fWithPlatforms)
		g_map->drawPlatforms(1);

    if (!view_only_layer || fScreenshot) {
        for (const MapItem& item : g_map->mapitems) {
            rm->spr_mapitems[iBlockSize == TILESIZE ? 0 : iBlockSize == PREVIEWTILESIZE ? 1 : 2].draw(item.ix * iBlockSize, item.iy * iBlockSize, {item.itype * iBlockSize, 0, iBlockSize, iBlockSize});
        }

        if (!fScreenshot) {
            for (int j = 0; j < MAPHEIGHT; j++) {
                for (int i = 0; i < MAPWIDTH; i++) {
					short iNewX = i - move_offset_x;
					short iNewY = j - move_offset_y;

					if ((move_mode == 1 || move_mode == 3) && iNewX >= 0 && iNewX < MAPWIDTH &&
						iNewY >= 0 && iNewY < MAPHEIGHT &&
                            selectedtiles[iNewX][iNewY]) {
						if (copiedtiles[iNewX][iNewY].item >= 0)
							rm->spr_mapitems[0].draw(i << 5, j << 5, {copiedtiles[iNewX][iNewY].item << 5, 0, TILESIZE, TILESIZE});
					}
				}
			}
		}
	}

	if (fWithPlatforms)
		g_map->drawPlatforms(2);

	if ((view_only_layer && selected_layer == 2) || !view_only_layer)
		drawlayer(2, false, iBlockSize);

    if (!fScreenshot) {
		if ((view_only_layer && copiedlayer == 2) || !view_only_layer)
			drawlayer(2, true, iBlockSize);
	}

	if ((view_only_layer && selected_layer == 3) || !view_only_layer)
		drawlayer(3, false, iBlockSize);

    if (!fScreenshot) {
		if ((view_only_layer && copiedlayer == 3) || !view_only_layer)
			drawlayer(3, true, iBlockSize);
	}

	if (fWithPlatforms)
		g_map->drawPlatforms(3);

    if ((viewwarps && !view_only_layer) || fScreenshot) {
        for (int j = 0; j < MAPHEIGHT; j++) {
            for (int i = 0; i < MAPWIDTH; i++) {
				Warp * warp = nullptr;
				if ((move_mode == 1 || move_mode == 3) && i - move_offset_x >= 0 && i - move_offset_x < MAPWIDTH &&
					j - move_offset_y >= 0 && j - move_offset_y < MAPHEIGHT &&
                        selectedtiles[i - move_offset_x][j - move_offset_y]) {
					warp = &copiedtiles[i - move_offset_x][j - move_offset_y].warp;
                } else {
					warp = &g_map->warpdata[i][j];
				}

                if (warp->connection != -1) {
					SDL_Rect rSrc = {warp->connection * iBlockSize, warp->direction * iBlockSize, iBlockSize, iBlockSize};
					SDL_Rect rDst = {i * iBlockSize, j * iBlockSize, iBlockSize, iBlockSize};

					rm->spr_warps[iBlockSize == TILESIZE ? 0 : iBlockSize == PREVIEWTILESIZE ? 1 : 2].draw(rSrc, screen, rDst);
				}
			}
		}
	}

	if (fWithPlatforms)
		g_map->drawPlatforms(4);

}

void resetselectedtiles()
{
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
			selectedtiles[j][k] = false;
		}
	}
}

void copymoveselection()
{
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
			moveselectedtiles[j][k] = selectedtiles[j][k];
		}
	}

	copiedlayer = selected_layer;
}

void pastemoveselection(int movex, int movey)
{
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
            if (moveselectedtiles[j][k]) {
				if (j + movex >= 0 && j + movex < MAPWIDTH &&
                        k + movey >= 0 && k + movey < MAPHEIGHT) {
					selectedtiles[j + movex][k + movey] = moveselectedtiles[j][k];
				}
			}
		}
	}
}

bool copyselectedtiles()
{
    //Copy the selected tiles and remove tiles from map
    bool ret = false;
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
            if (selectedtiles[j][k]) {
                ret = true;
                for (short iLayer = 0; iLayer < MAPLAYERS; iLayer++) {
                    copiedtiles[j][k].tile[iLayer] = g_map->mapdata[j][k][iLayer];
                }

				copiedtiles[j][k].block.iType = g_map->objectdata[j][k].iType;
				for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
					copiedtiles[j][k].block.iSettings[iSetting] = g_map->objectdata[j][k].iSettings[iSetting];
				copiedtiles[j][k].block.fHidden = g_map->objectdata[j][k].fHidden;

				copiedtiles[j][k].warp.connection = g_map->warpdata[j][k].connection;
				copiedtiles[j][k].warp.direction = g_map->warpdata[j][k].direction;
				copiedtiles[j][k].warp.id = g_map->warpdata[j][k].id;

				copiedtiles[j][k].tiletype = g_map->mapdatatop[j][k];

				for (short iType = 0; iType < NUMSPAWNAREATYPES; iType++)
					copiedtiles[j][k].nospawn[iType] = g_map->nospawn[iType][j][k];

				copiedtiles[j][k].item = -1;
                for (const MapItem& item : g_map->mapitems) {
                    if (item.ix == j && item.iy == k) {
                        copiedtiles[j][k].item = item.itype;
                        break;
                    }
                }
            }
        }
    }

    copiedlayer = selected_layer;

    return ret;
}

void clearselectedmaptiles()
{
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
            if (selectedtiles[j][k]) {
                if (view_only_layer) {
					g_map->mapdata[j][k][selected_layer].iID = TILESETNONE;
                } else {
					for (short iLayer = 0; iLayer < MAPLAYERS; iLayer++)
						g_map->mapdata[j][k][iLayer].iID = TILESETNONE;

					g_map->objectdata[j][k].iType = -1;

					g_map->warpdata[j][k].connection = -1;
					g_map->warpdata[j][k].direction = WARP_UNDEFINED;
					g_map->warpdata[j][k].id = -1;

					for (short iType = 0; iType < NUMSPAWNAREATYPES; iType++)
						g_map->nospawn[iType][j][k] = false;

					RemoveMapItemAt(j, k);
				}

				UpdateTileType(j, k);
			}
		}
	}
}

namespace {
void replacetile(short * iDstTile, short iSrcTile, bool fAllowReplace)
{
    if (move_replace) {
		*iDstTile = iSrcTile;
    } else {
		if (fAllowReplace)
			*iDstTile = iSrcTile;
	}
}

void copytilesettile(TilesetTile * dst, TilesetTile * src)
{
	dst->iID = src->iID;
	dst->iCol = src->iCol;
	dst->iRow = src->iRow;
}

void replacetile(TilesetTile * dstTile, TilesetTile * srcTile)
{
    if (move_replace) {
		copytilesettile(dstTile, srcTile);
    } else {
		if (srcTile->iID != TILESETNONE)
			copytilesettile(dstTile, srcTile);
	}
}

void copymapblock(MapBlock * dst, MapBlock * src)
{
	dst->iType = src->iType;
	dst->fHidden = src->fHidden;

	for (short iSetting = 0; iSetting < NUM_BLOCK_SETTINGS; iSetting++)
		dst->iSettings[iSetting] = src->iSettings[iSetting];
}

void replacetile(MapBlock * dstTile, MapBlock * srcTile)
{
    if (move_replace) {
		copymapblock(dstTile, srcTile);
    } else {
		if (srcTile->iType != -1)
			copymapblock(dstTile, srcTile);
	}
}
} // namespace

void pasteselectedtiles(int movex, int movey)
{
	//Paste the tiles into their new location
	//Removing tiles that hang over the edges of the map
    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
            if (selectedtiles[j][k]) {
				if (j + movex >= 0 && j + movex < MAPWIDTH &&
                        k + movey >= 0 && k + movey < MAPHEIGHT) {
					short iNewX = j + movex;
					short iNewY = k + movey;

                    if (view_only_layer) {
						replacetile(&g_map->mapdata[iNewX][iNewY][selected_layer], &copiedtiles[j][k].tile[copiedlayer]);
                    } else {
						for (short iLayer = 0; iLayer < MAPLAYERS; iLayer++)
							replacetile(&g_map->mapdata[iNewX][iNewY][iLayer], &copiedtiles[j][k].tile[iLayer]);

						replacetile(&g_map->objectdata[iNewX][iNewY], &copiedtiles[j][k].block);

						replacetile(&g_map->warpdata[iNewX][iNewY].connection, copiedtiles[j][k].warp.connection, copiedtiles[j][k].warp.connection != -1);
						replacetile((short*)&g_map->warpdata[iNewX][iNewY].direction, static_cast<short>(copiedtiles[j][k].warp.direction), copiedtiles[j][k].warp.connection != -1);
						replacetile(&g_map->warpdata[iNewX][iNewY].id, copiedtiles[j][k].warp.id, copiedtiles[j][k].warp.connection != -1);

						if (move_replace)
							g_map->mapdatatop[iNewX][iNewY] = copiedtiles[j][k].tiletype;
						else
							UpdateTileType(j, k);

						for (short iType = 0; iType < NUMSPAWNAREATYPES; iType++)
							g_map->nospawn[iType][iNewX][iNewY] = copiedtiles[j][k].nospawn[iType];

						//if (move_replace)
						//	RemoveMapItemAt(j, k);

                                                if (g_map->mapitems.size() < MAXMAPITEMS && copiedtiles[j][k].item >= 0) {
						//	if (!move_replace)
						//		RemoveMapItemAt(j, k);

                                                    MapItem item = {};
                                                    item.itype = static_cast<MapItemType>(copiedtiles[j][k].item);
                                                    item.ix = iNewX;
                                                    item.iy = iNewY;
                                                    g_map->mapitems.emplace_back(std::move(item));
						}
					}
				}
			}
		}
	}
}

void getcenterselection(int * x, int * y)
{
	int left = -1;
	int top = -1;
	int right = -1;
	int bottom = -1;

    for (int k = 0; k < MAPHEIGHT; k++) {
        for (int j = 0; j < MAPWIDTH; j++) {
            if (selectedtiles[j][k]) {
				if (left == -1)
					left = j;

				if (top == -1)
					top = k;

				right = j;
				bottom = k;
			}
		}
	}

    if (right == -1) {
		*x = 0;
		*y = 0;
		return;
	}

	*x = ((right - left) >> 1) + left;
	*y = ((bottom - top) >> 1) + top;
}

//take screenshots in full and thumbnail sizes
void takescreenshot()
{
	short iTileSizes[3] = {TILESIZE, PREVIEWTILESIZE, THUMBTILESIZE};
	SDL_Surface * old_screen = screen;

	for (short iScreenshotSize = 0; iScreenshotSize < 3; iScreenshotSize++) {
		short iTileSize = iTileSizes[iScreenshotSize];

		//Allow wrapping of path dots
		rm->spr_platformpath.setWrap(640 >> iScreenshotSize);

		//Create new screenshot surface
		auto screenshot = gfxSprite::blank(iTileSize * 20, iTileSize * 15);
		blitdest = screenshot.getSurface();
		screen = screenshot.getSurface();

		//Draw map to screenshot
		drawmap(true, iTileSize);

		//Draw platforms to screenshot
		for (short iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++) {
			MapPlatform * platform = &g_Platforms[iPlatform];
			DrawPlatform(platform->iPathType, platform->tiles, platform->iStartX, platform->iStartY, platform->iEndX, platform->iEndY, platform->fAngle, platform->fRadiusX, platform->fRadiusY, iScreenshotSize, g_map->platforms[iPlatform]->iTileWidth, g_map->platforms[iPlatform]->iTileHeight, true, true, blitdest);
		}

		//Draw map hazards
		for (const MapHazard& hazard : g_map->maphazards)
			DrawMapHazard(hazard, iScreenshotSize, false, blitdest);

		//Save the screenshot with the same name as the map file
		std::string szSaveFile("maps/screenshots/");
		szSaveFile += GetNameFromFileName(maplist->currentFilename());

		if (iTileSize == PREVIEWTILESIZE)
			szSaveFile += "_preview";
		else if (iTileSize == THUMBTILESIZE)
			szSaveFile += "_thumb";

		szSaveFile += ".png";
		IMG_SavePNG(screenshot.getSurface(), convertPath(szSaveFile).c_str());

		printf("Screenshot taken: %s\n", szSaveFile.c_str());

		screen = old_screen;
		blitdest = screen;
	}
}

#include "LevelEditorState.h"

#ifdef _DEBUG
void convertAll()
{
    for (size_t k = 0; k < maplist->count(); k++) {
        //g_map->convertMap();
        g_map->saveMap(maplist->currentFilename());
        maplist->next(false);
        g_map->loadMap(maplist->currentFilename(), read_type_full);
    }
}
#endif

// (loadmapfile's file, read by loadcurrentmap instead of the list's current map)
std::string g_loadMapFrom;

// A map file's contents into the editor (the list's current map stays: saving writes that one)
void loadmapfile(const std::string& file)
{
    g_loadMapFrom = file;
    loadcurrentmap();
    g_loadMapFrom.clear();
}

void loadcurrentmap()
{
    g_map->loadMap(g_loadMapFrom.empty() ? maplist->currentFilename() : g_loadMapFrom, read_type_full);

    if (g_map->iNumRaceGoals == 0) {
        for (short iGoal = 0; iGoal < MAXRACEGOALS; iGoal++) {
            g_map->racegoallocations[iGoal].x = iGoal * 80 + 20;
            g_map->racegoallocations[iGoal].y = 18;
        }
    }

    if (g_map->iNumFlagBases == 0) {
        for (short iBase = 0; iBase < 4; iBase++) {
            g_map->flagbaselocations[iBase].x = iBase * 80 + 20;
            g_map->flagbaselocations[iBase].y = 18;
        }
    }

    std::string filename = "gfx/packs/Classic/backgrounds/" + g_map->szBackgroundFile;
    std::string path = convertPath(filename);
    backgroundlist->setCurrentPath(filename);

    if (!FileExists(path)) {
        path = convertPath("gfx/packs/Classic/backgrounds/Land_Classic.png");
        backgroundlist->setCurrentPath("gfx/packs/Classic/backgrounds/Land_Classic.png");
    }

    rm->spr_background = ImageLoader(path).withoutColorKey().create();

        g_iNumPlatforms = g_map->platforms.size();

    for (short iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++) {
        for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
            for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
                if (iCol < g_map->platforms[iPlatform]->iTileWidth && iRow < g_map->platforms[iPlatform]->iTileHeight) {
                    g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow] = g_map->platforms[iPlatform]->tileAt(iCol, iRow);
                    g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow] = g_map->platforms[iPlatform]->tileTypeAt(iCol, iRow);
                } else {
                    ClearTilesetTile(&g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow]);
                    g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow] = TileType::NonSolid;
                }
            }
        }

        g_Platforms[iPlatform].iDrawLayer = g_map->platforms[iPlatform]->iDrawLayer;
        g_Platforms[iPlatform].iPathType = g_map->platforms[iPlatform]->pPath->typeId();

        if (auto* path = dynamic_cast<StraightPath*>(g_map->platforms[iPlatform]->pPath)) {
            g_Platforms[iPlatform].iVelocity = static_cast<int>(path->speed() * 4.0f);
            g_Platforms[iPlatform].iStartX = static_cast<int>(path->startPos().x);
            g_Platforms[iPlatform].iStartY = static_cast<int>(path->startPos().y);
            g_Platforms[iPlatform].iEndX = static_cast<int>(path->endPos().x);
            g_Platforms[iPlatform].iEndY = static_cast<int>(path->endPos().y);
        } else if (auto* path = dynamic_cast<StraightPathContinuous*>(g_map->platforms[iPlatform]->pPath)) {
            g_Platforms[iPlatform].iVelocity = static_cast<int>(path->speed() * 4.0f);
            g_Platforms[iPlatform].iStartX = static_cast<int>(path->startPos().x);
            g_Platforms[iPlatform].iStartY = static_cast<int>(path->startPos().y);
            g_Platforms[iPlatform].fAngle = path->angle();
        } else if (auto* path = dynamic_cast<EllipsePath*>(g_map->platforms[iPlatform]->pPath)) {
            g_Platforms[iPlatform].iVelocity = static_cast<short>(path->speed() / 0.0030f);
            g_Platforms[iPlatform].fRadiusX = path->radius().x;
            g_Platforms[iPlatform].fRadiusY = path->radius().y;
            g_Platforms[iPlatform].iStartX = static_cast<int>(path->centerPos().x);
            g_Platforms[iPlatform].iStartY = static_cast<int>(path->centerPos().y);
            g_Platforms[iPlatform].fAngle = path->startAngle();
        }

        g_Platforms[iPlatform].UpdatePreview();
    }
}

void SetPlatformToDefaults(short iPlatform)
{
    for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
        for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
            ClearTilesetTile(&g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow]);
            g_Platforms[iPlatform].types[iCol * MAPHEIGHT + iRow] = TileType::NonSolid;
        }
    }

    g_Platforms[iPlatform].iVelocity = 4;
    g_Platforms[iPlatform].iStartX = 320;
    g_Platforms[iPlatform].iStartY = 240;
    g_Platforms[iPlatform].iEndX = 352;
    g_Platforms[iPlatform].iEndY = 240;

    g_Platforms[iPlatform].fAngle = 0.0f;
    g_Platforms[iPlatform].fRadiusX = 128.0f;
    g_Platforms[iPlatform].fRadiusY = 128.0f;

    g_Platforms[iPlatform].iDrawLayer = 2;
}

int savecurrentmap()
{
    g_messagedisplaytimer = 60;
    g_szMessageTitle = "Saved";
    g_szMessageLine[0] = "Your map has";
    g_szMessageLine[1] = "been saved.";
    g_szMessageLine[2] = "";

    //(a map on a disc that can't be written: saved in the home folder, the list then reads it there)
    const std::string file = writablePath(maplist->currentFilename());
    save_map(file);
    if (file != maplist->currentFilename())
        maplist->setCurrentFilename(file);
    return 0;
}

void insert_platforms_into_map()
{
    //First take the created platforms and move them into the actual map
    g_map->clearPlatforms();

    //g_map->iNumPlatforms = g_iNumPlatforms;
        g_map->platforms.reserve(g_iNumPlatforms);

    for (short iPlatform = 0; iPlatform < g_iNumPlatforms; iPlatform++) {
        short iTop, iLeft, iWidth, iHeight;
        CalculatePlatformDims(iPlatform, &iLeft, &iTop, &iWidth, &iHeight);

        std::vector<TilesetTile> tiles(iWidth * iHeight);
        std::vector<TileType> types(iWidth * iHeight);

        for (short iCol = 0; iCol < iWidth; iCol++) {
            for (short iRow = 0; iRow < iHeight; iRow++) {
                const size_t cellIdx = (iCol + iLeft) * MAPHEIGHT + iRow + iTop;
                tiles[iCol * iHeight + iRow] = g_Platforms[iPlatform].tiles[cellIdx];
                types[iCol * iHeight + iRow] = g_Platforms[iPlatform].types[cellIdx];
            }
        }

        short iDrawLayer = g_Platforms[iPlatform].iDrawLayer;

        float fStartX = static_cast<float>(g_Platforms[iPlatform].iStartX);
        float fStartY = static_cast<float>(g_Platforms[iPlatform].iStartY);

        MovingPlatformPath * path = nullptr;

        if (g_Platforms[iPlatform].iPathType == PlatformPathType::Straight) {
            float fVelocity = static_cast<float>(g_Platforms[iPlatform].iVelocity) * 0.26f;
            float fEndX = static_cast<float>(g_Platforms[iPlatform].iEndX);
            float fEndY = static_cast<float>(g_Platforms[iPlatform].iEndY);
            path = new StraightPath(fVelocity, Vec2f(fStartX, fStartY), Vec2f(fEndX, fEndY), false);
        } else if (g_Platforms[iPlatform].iPathType == PlatformPathType::StraightContinuous) {
            float fVelocity = static_cast<float>(g_Platforms[iPlatform].iVelocity) * 0.26f;
            path = new StraightPathContinuous(fVelocity, Vec2f(fStartX, fStartY), g_Platforms[iPlatform].fAngle, false);
        } else if (g_Platforms[iPlatform].iPathType == PlatformPathType::Ellipse) {
            float fVelocity = static_cast<float>(g_Platforms[iPlatform].iVelocity) * 0.0030f;
            float radiusX = g_Platforms[iPlatform].fRadiusX;
            float radiusY = g_Platforms[iPlatform].fRadiusY;
            path = new EllipsePath(fVelocity, g_Platforms[iPlatform].fAngle, Vec2f(radiusX, radiusY), Vec2f(fStartX, fStartY), false);
        }

        g_map->AddPermanentPlatform(new MovingPlatform(std::move(tiles), std::move(types), iWidth, iHeight, iDrawLayer, path, false));
    }
}

void save_map(const std::string &file)
{
    insert_platforms_into_map();

    //Then save the rest of the map
    g_map->saveMap(file);
}

void CalculatePlatformDims(short iPlatform, short * ix, short * iy, short * iw, short * ih)
{
    short iTop = MAPHEIGHT, iRight = -1, iBottom = -1, iLeft = MAPWIDTH;
    //Calculate the height and width of the platform
    for (short iCol = 0; iCol < MAPWIDTH; iCol++) {
        for (short iRow = 0; iRow < MAPHEIGHT; iRow++) {
            if (g_Platforms[iPlatform].tiles[iCol * MAPHEIGHT + iRow].iID != TILESETNONE) {
                if (iTop > iRow)
                    iTop = iRow;

                if (iLeft > iCol)
                    iLeft = iCol;

                if (iBottom < iRow)
                    iBottom = iRow;

                if (iRight < iCol)
                    iRight = iCol;
            }
        }
    }

    short iWidth = iRight - iLeft + 1;
    short iHeight = iBottom - iTop + 1;

    if (iRight == -1 || iBottom == -1) {
        iWidth = 1;
        iHeight = 1;
        iLeft = 0;
        iTop = 0;
    }

    *ix = iLeft;
    *iy = iTop;

    *iw = iWidth;
    *ih = iHeight;
}

int findcurrentstring()
{
    if (!findstring.empty()) {
        if (maplist->find(findstring)) {
            loadcurrentmap();
        }
    }

    return 0;
}

int newmap()
{
    char fileName[FILEBUFSIZE] = "";

    if (dialog("New Map", "Enter name:", fileName, 64)) {
        std::string mapName = fileName;
        g_map->clearMap();
        g_map->clearPlatforms();
        g_map->saveMap(writablePath(convertPath("maps/" + mapName + ".map")));
        mapName += ".map";
        maplist->add(mapName.c_str());
        maplist->find(mapName);
        loadcurrentmap();
    }

    return 0;
}

bool ReadAnimatedTileTypeFile(const char * szFile)
{
    //Detect if the tiletype file already exists, if not create it
    if (FileExists(szFile)) {
        BinaryFile tsf(szFile, "rb");
        if (!tsf.is_open()) {
            printf("ERROR: couldn't open tileset file: %s\n", szFile);
            return false;
        }

        animatedtiletypes = new TileType[256];

        for (short i = 0; i < 256; i++) {
            animatedtiletypes[i] = (TileType)tsf.read_i32();
        }
    } else {
        animatedtiletypes = new TileType[256];

        for (short i = 0; i < 256; i++) {
            animatedtiletypes[i] = TileType::NonSolid;
        }
    }

    return true;
}

bool WriteAnimatedTileTypeFile(const char * szFile)
{
    BinaryFile tsf(szFile, "wb");
    if (!tsf.is_open()) {
        printf("ERROR: couldn't open tileset file to save tile types: %s\n", szFile);
        return false;
    }

    for (short i = 0; i < 256; i++) {
            tsf.write_i32(static_cast<int>(animatedtiletypes[i]));
    }

    return true;
}

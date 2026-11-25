#ifndef MOVINGPLATFORM_H
#define MOVINGPLATFORM_H

#include "map.h"
#include "MovingPlatformPaths.h"
#include "RenderInterpolation.h"
#include "SimArena.h"

#include <array>
#include <vector>

class CPlayer;
class IO_MovingObject;

// Moving platforms are simulation state: they live in the simulation arena
// (see SimArena.h), like their paths and the temporary platforms' list.
class MovingPlatform : public sim::ArenaAllocated
{
    public:
        MovingPlatform(const std::vector<TilesetTile>& tiledata, const std::vector<TileType>& tiletypes, short w, short h, short drawlayer, MovingPlatformPath * path, bool preview);
        // A solid platform of w x h tiles drawn with `sprite` (a loaded
        // resource, not owned), without prerendered surfaces. Used for the
        // temporary platforms created during a match (falling donut blocks).
        MovingPlatform(const gfxSprite& sprite, short w, short h, short drawlayer, MovingPlatformPath * path);
        // A platform of w x h of one tileset tile (its type `type`), its tiles drawn each frame without
        // prerendered surfaces: the Map Maker's, made during a match (smw/GameplayEdit.cpp)
        MovingPlatform(const TilesetTile& tile, TileType type, short w, short h, short drawlayer, MovingPlatformPath * path);
        // Likewise, of w x h tiles of their own (`tiles`, `types`: a column after another)
        MovingPlatform(const TilesetTile* tiles, const TileType* types, short w, short h, short drawlayer, MovingPlatformPath * path);
        ~MovingPlatform();

        void draw();
        void draw(short iOffsetX, short iOffsetY);
        void update();


        void ResetPath();

        void collide(CPlayer * player);

        void GetTileTypesFromPlayer(CPlayer * player, int * lefttile, int * righttile);
        int GetTileTypeFromCoord(short x, short y);

        void collide(IO_MovingObject * object);

    void setXf(float xf) {
        fx = xf;
        ix = static_cast<short>(fx);
    }
    void setXi(short xi) {
        ix = xi;
        fx = static_cast<float>(ix);
    }
    void setYf(float yf) {
        fy = yf;
        iy = static_cast<short>(fy);
    }
    void setYi(short yi) {
        iy = yi;
        fy = static_cast<float>(iy);
    }

        bool IsInNoSpawnZone(short x, short y, short w, short h);

    // Gone at the map's next update (a temporary platform: deleted then)
    void kill() { fDead = true; }

    void SetPlayerId(short playerId) {
        iPlayerId = playerId;
    }

    const TilesetTile& tileAt(size_t col, size_t row) const;
    TileType tileTypeAt(size_t col, size_t row) const;

    // Render-only (smooth motion, see RenderInterpolation.h); the simulation never reads these
    // For the CPU players' paths (AINav): the middle, the size and the way it moves
    float centerX() const { return fx; }
    float centerY() const { return fy; }
    float velX() const { return fVelX; }
    float velY() const { return fVelY; }
    short width() const { return iWidth; }
    short height() const { return iHeight; }
    short tileWidth() const { return iTileWidth; }
    short tileHeight() const { return iTileHeight; }
    const MovingPlatformPath* path() const { return pPath; }

    RenderPosition renderPosition() const { return {fx, fy, ix, iy}; }
    void setRenderPosition(const RenderPosition& pos) { fx = pos.fx; fy = pos.fy; ix = pos.ix; iy = pos.iy; }
    RenderPrevious renderPrevious;

    protected:

        void check_map_collision_right(CPlayer * player);
        void check_map_collision_left(CPlayer * player);

        short coldec_player(CPlayer * player);
        short coldec_object(IO_MovingObject * object);

        bool collision_detection_check_sides(IO_MovingObject * object);

        sim::vector<TilesetTile> iTileData;
        sim::vector<TileType> iTileType;
        short ix, iy;
        short iWidth, iHeight;
        short iTileWidth, iTileHeight;
        short iHalfWidth, iHalfHeight;

        bool fDead;

        float fx, fy;
        float fOldX, fOldY;
        float fOldVelX, fOldVelY;

        //The prerendered tiles (front and back buffer of the tile animation),
        //created when the map is loaded. Empty for a platform drawn with
        //customSprite.
        std::array<gfxSprite, 2> sprites;
        const gfxSprite* customSprite = nullptr;
        bool drawTiles = false;    // its tiles drawn from the tilesets each frame (no sprites)

        SDL_Rect	rSrcRect;

        //The parts of the prerendered tiles that aren't empty (in the sprites): drawing only these
        //skips a platform's empty tiles, which every frame were read and compared pixel by pixel
        //(an airship's outline: a good share of its rectangle). Rows of tiles with the same
        //columns merged into one rectangle. (In the arena like the rest: a snapshot copies the
        //platform, and a debug build's std::vector has a part on the heap a restore would leave stale)
        sim::vector<SDL_Rect> drawParts;
        //On the Xbox, of those the parts opaque all over (static tiles without a pixel of the color
        //key: most of a platform's): copied, without the key (the same pixels, far faster there: a
        //memcpy, not a test of each pixel). drawParts has the rest (on a PC, all).
        sim::vector<SDL_Rect> drawOpaque;

        short iDrawLayer;

        MovingPlatformPath * pPath;

        float fVelX, fVelY;

        short iPlayerId;

    friend class FallingPath;
    friend class StraightPathContinuous;

    friend class CPlayer;
    friend class IO_MovingObject;
    friend class CMap;
    friend void loadcurrentmap();
    friend void takescreenshot();
    friend void loadmap(char * szMapFile);
    friend int editor_platforms();
    friend void leveleditor::loadcurrentmap();
    friend void leveleditor::takescreenshot();
    friend int leveleditor::editor_platforms();
};

#endif // MOVINGPLATFORM_H

#ifndef MOVINGPLATFORM_H
#define MOVINGPLATFORM_H

#include "map.h"
#include "MovingPlatformPaths.h"
#include "RenderInterpolation.h"
#include "SimArena.h"

#include <array>

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

    void SetPlayerId(short playerId) {
        iPlayerId = playerId;
    }

    const TilesetTile& tileAt(size_t col, size_t row) const;
    TileType tileTypeAt(size_t col, size_t row) const;


    // Render-only (smooth motion, see RenderInterpolation.h); the simulation never reads these
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

		short iSteps;
		short iOnStep;

		//The prerendered tiles (front and back buffer of the tile animation),
		//created when the map is loaded. Empty for a platform drawn with
		//customSprite.
		std::array<gfxSprite, 2> sprites;
		const gfxSprite* customSprite = nullptr;

		SDL_Rect	rSrcRect;

		bool fForwardDirection;
		bool fStartDirection;

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

};

#endif // MOVINGPLATFORM_H

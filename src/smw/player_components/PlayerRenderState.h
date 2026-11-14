#ifndef PLAYER_RENDERSTATE
#define PLAYER_RENDERSTATE

#include "gfx.h"

class CPlayer;

// Cosmetic animation state of a player that the drawing code advances (see
// render::advancesAnimations()) or picks while drawing: the cape and wings
// frames, the angle of the award ring, the bob-omb smoke timer and the
// sprites the scoreboard shows.
//
// It is kept out of CPlayer on purpose. CPlayer is simulation state: it lives
// in the simulation arena and is saved and restored by rollback snapshots, so
// drawing must never write into it. The simulation may reset these fields
// (e.g. when a player respawns), but must never read them for a gameplay
// decision.
struct PlayerRenderState {
    // PlayerCape
    unsigned char capeTimer = 0;
    unsigned short capeFrameX = 0;
    unsigned short capeFrameY = 0;
    bool capeUp = false;
    short capeYOffset = 0;

    // PlayerWings
    unsigned char wingsTimer = 0;
    unsigned short wingsFrame = 0;

    // PlayerAwardEffects
    float awardAngle = 0.0f;

    // CPlayer (smoke puffs of a bob-omb player)
    short bobombSmokeTimer = 0;

    // The sprites the player was last drawn with (normal, bob-omb, chicken,
    // ...), shown on the scoreboard
    SpriteStrip* scoreboardSprite = nullptr;
};

// The render state of the player with this global id (0..3)
PlayerRenderState& playerRenderState(short globalID);
PlayerRenderState& playerRenderState(const CPlayer& player);

#endif // PLAYER_RENDERSTATE

#pragma once

#include "GlobalConstants.h"
#include "objects/moving/MovingObject.h"

class CPlayer;
class gfxSprite;


class PU_FeatherPowerup : public IO_MovingObject {
public:
    PU_FeatherPowerup(gfxSprite* nspr, Vec2s pos, short iNumSpr, short aniSpeed, short iCollisionWidth, short iCollisionHeight, short iCollisionOffsetX, short iCollisionOffsetY);

    void update() override;
    void draw() override;
    bool collide(CPlayer* player) override;

    void nospawn(short y);

    /// The middle it sways about (where it is, until it starts floating)
    float swayCenterX() const {
        return state >= 2 ? dFloatCenterX : fx;
    }

private:
    bool fFloatDirectionRight = true;
    float dFloatAngle = HALF_PI;
    float dFloatCenterX = 0.f;
    float dFloatCenterY = 0.f;
    float desty;
};

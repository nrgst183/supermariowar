#ifndef PLAYER_AWARDEFFECTS
#define PLAYER_AWARDEFFECTS

#include "GlobalConstants.h"
#include "PlayerKillStyles.h"

class CPlayer;

class PlayerAwardEffects
{
public:
    void addDeathAward(CPlayer&);
    void addKillerAward(CPlayer& killer, CPlayer* killed, KillStyle style);
    void addKillsInRowInAirAward(CPlayer&);

    void drawRingAward(CPlayer& player);

private:
    void addExploding(CPlayer&);
    void addSwirling(CPlayer&);
    void addRocket(CPlayer&);

    short awards[MAXAWARDS];    // the angle of the ring is cosmetic, see PlayerRenderState
};


#endif // PLAYER_AWARDEFFECTS

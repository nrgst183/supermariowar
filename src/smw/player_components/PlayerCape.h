#ifndef PLAYER_CAPE
#define PLAYER_CAPE

class CPlayer;

// The cape's animation state is cosmetic and lives in PlayerRenderState
class PlayerCape
{
public:
    void reset(CPlayer& player);
    void restart_animation(CPlayer& player);
    void spin(CPlayer& player);
    void draw(CPlayer& player);
};


#endif // PLAYER_CAPE

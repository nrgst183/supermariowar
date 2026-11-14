#ifndef PLAYER_WINGS
#define PLAYER_WINGS

class CPlayer;

// The wings' animation state is cosmetic and lives in PlayerRenderState
class PlayerWings
{
public:
    void reset(CPlayer& player);
    void draw(CPlayer& player);
};

#endif // PLAYER_WINGS

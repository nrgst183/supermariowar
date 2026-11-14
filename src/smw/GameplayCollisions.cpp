#include "GSGameplay.h"

#include "CmdArgs.h"
#include "eyecandy.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "joystick.h"
#include "GSMenu.h"
#include "map.h"
#include "MapList.h"
#include "ObjectContainer.h"
#include "objecthazard.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "Score.h"
#include "sfx.h"
#include "SimulationState.h"
#include "WorldTourStop.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/Tag.h"
#include "objects/MysteryMushroomTempPlayer.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Spike.h"
#include "objects/carriable/CO_ThrowBlock.h"
#include "objects/carriable/CO_ThrowBox.h"
#include "objects/blocks/BounceBlock.h"
#include "objects/blocks/BreakableBlock.h"
#include "objects/blocks/DonutBlock.h"
#include "objects/blocks/FlipBlock.h"
#include "objects/blocks/NoteBlock.h"
#include "objects/blocks/OnOffSwitchBlock.h"
#include "objects/blocks/PowerupBlock.h"
#include "objects/blocks/SwitchBlock.h"
#include "objects/blocks/ThrowBlock.h"
#include "objects/blocks/ViewBlock.h"
#include "objects/blocks/WeaponBreakableBlock.h"
#include "objects/moving/MO_BulletBill.h"
#include "objects/moving/MO_Coin.h"
#include "objects/moving/MO_PirhanaPlant.h"
#include "objects/powerup/PU_BobombPowerup.h"
#include "objects/powerup/PU_BombPowerup.h"
#include "objects/powerup/PU_BoomerangPowerup.h"
#include "objects/powerup/PU_BulletBillPowerup.h"
#include "objects/powerup/PU_ClockPowerup.h"
#include "objects/powerup/PU_ExtraGuyPowerup.h"
#include "objects/powerup/PU_FeatherPowerup.h"
#include "objects/powerup/PU_FirePowerup.h"
#include "objects/powerup/PU_HammerPowerup.h"
#include "objects/powerup/PU_IceWandPowerup.h"
#include "objects/powerup/PU_LeafPowerup.h"
#include "objects/powerup/PU_ModPowerup.h"
#include "objects/powerup/PU_MysteryMushroomPowerup.h"
#include "objects/powerup/PU_PodoboPowerup.h"
#include "objects/powerup/PU_PoisonPowerup.h"
#include "objects/powerup/PU_PowPowerup.h"
#include "objects/powerup/PU_PWingsPowerup.h"
#include "objects/powerup/PU_StarPowerup.h"
#include "objects/powerup/PU_Tanooki.h"
#include "objects/walkingenemy/WalkingEnemy.h"

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

//TODO  - Review what is colliding with what and remove duplicates (i.e. shell vs. throwblock and throwblock vs. shell should only detect one way)
constexpr short g_iCollisionMap[MOVINGOBJECT_LAST][MOVINGOBJECT_LAST] = {
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_none = 0
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_powerup = 1
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,1,0,0,1}, //movingobject_fireball = 2
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_goomba = 3
    {0,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,0,1,0,0,0}, //movingobject_bulletbill = 4
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,1,0,0,1}, //movingobject_hammer = 5
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_poisonpowerup = 6
    {0,0,0,1,1,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,1,1,0,1,1,0,0,0,0,0,1,0,0,0}, //movingobject_shell = 7
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,1,0,1,1,0,1,1,0,0,0,0,0,1,0,0,0}, //movingobject_throwblock = 8
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0}, //movingobject_egg = 9
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_star = 10
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, //movingobject_flag = 11
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_cheepcheep = 12
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_koopa = 13
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,1,0,0,1}, //movingobject_boomerang = 14
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_carried = 15
    {0,0,0,1,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1}, //movingobject_iceblast = 16
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_bomb = 17
    {0,0,0,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0}, //movingobject_podobo = 18
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_treasurechest = 19
    {0,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,1,0,0,0}, //movingobject_attackzone = 20
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_pirhanaplant = 21
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,1,0,0,1}, //movingobject_explosion = 22
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_buzzybeetle = 23
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_spiny = 24
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_phantokey = 25
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_flagbase = 26
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_yoshi = 27
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_coin = 28
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_collectioncard = 29
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, //movingobject_sledgebrother = 30
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0,0,0,1}, //movingobject_sledgehammer = 31
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0,0,0,1}, //movingobject_superfireball = 32
    {0,0,0,1,1,0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,1,1,0,1,1,0,0,0,0,0,1,0,0,1}, //movingobject_throwbox = 33
};


bool coldec_player2player(CPlayer * o1, CPlayer * o2);
bool coldec_player2obj(CPlayer * o1, CObject * o2);
bool coldec_obj2obj(CObject * o1, CObject * o2);

//handles a collision between a powerup and an object
void collisionhandler_o2o(IO_MovingObject * o1, IO_MovingObject * o2)
{
    o2->collide(o1);
}


void handleP2PCollisions()
{
    //Player to player collisions
    size_t i, j;
    for (i = 0; i < g_sim.players.size(); i++) {
        CPlayer* player1 = g_sim.players[i];
        assert(player1);
        if (player1->state > PlayerState::Dead) {
            for (j = i + 1; j < g_sim.players.size(); j++) {
                CPlayer* player2 = g_sim.players[j];
                assert(player2);
                if (player2->state > PlayerState::Dead) {
                    if (coldec_player2player(player1, player2)) {
                        player1->collidesWith(player2);

                        //if player was killed by another player, continue with next player for collision detection
                        if (player1->state <= PlayerState::Dead)
                            break;
                    }
                }
            }
        }
    }
}

void handleP2ObjCollisions()
{
    //Collide player with objects
    for (CPlayer* player : g_sim.players) {
        if (player->state != PlayerState::Ready)
            continue;

        //Collide with objects
        for (short iLayer = 0; iLayer < 3; iLayer++) {
            for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[iLayer].list()) {
                if (!obj->isDead()) {
                    if (coldec_player2obj(player, obj.get())) {
                        if (player->collidesWith(obj.get()))
                            break;
                    }
                }
            }

            //if the object killed the player, then continue with the other players
            if (player->state != PlayerState::Ready)
                break;

            //If player collided with a swap mushroom, the break from colliding with everything else
            if (game_values.flags.swapplayers)
                return;
        }
    }
}

void handleObj2ObjCollisions()
{
    for (size_t iLayer1 = 0; iLayer1 < 3; iLayer1++) {
        size_t iContainerEnd1 = g_sim.objectcontainer[iLayer1].list().size();
        for (size_t iObject1 = 0; iObject1 < iContainerEnd1; iObject1++) {
            CObject * object1 = g_sim.objectcontainer[iLayer1].list()[iObject1].get();

            auto* movingobject1 = dynamic_cast<IO_MovingObject*>(object1);
            if (!movingobject1)
                continue;

            for (size_t iLayer2 = iLayer1; iLayer2 < 3; iLayer2++) {
                size_t iContainerEnd2 = g_sim.objectcontainer[iLayer2].list().size();
                for (size_t iObject2 = (iLayer1 == iLayer2 ? iObject1 + 1 : 0); iObject2 < iContainerEnd2; iObject2++) {
                    CObject * object2 = g_sim.objectcontainer[iLayer2].list()[iObject2].get();

                    if (object2->isDead())
                        continue;

                    auto* movingobject2 = dynamic_cast<IO_MovingObject*>(object2);
                    if (!movingobject2)
                        continue;

                    //if (g_iCollisionMap[movingobject1->getMovingObjectType()][movingobject2->getMovingObjectType()])
                    //  continue;

                    //if (iLayer1 == iLayer2 && iObject1 == iObject2)
                    //  continue;

                    MovingObjectType iType1 = movingobject1->getMovingObjectType();
                    MovingObjectType iType2 = movingobject2->getMovingObjectType();
                    if (g_iCollisionMap[iType1][iType2]) {
                        if (coldec_obj2obj(movingobject1, movingobject2)) {
                            collisionhandler_o2o(movingobject1, movingobject2);
                        }
                    } else if (g_iCollisionMap[iType2][iType1]) {
                        if (coldec_obj2obj(movingobject2, movingobject1)) {
                            collisionhandler_o2o(movingobject2, movingobject1);
                        }
                    }

                    if (object1->isDead())
                        return; // CONTINUEOBJECT1
                }
            }
        }
    }
}

bool coldec_player2player(CPlayer * o1, CPlayer * o2)
{
    //Special cases to deal with players overlapping the right and left sides of the screen
    if (o1->rightX() < o2->ix) {
        return o1->ix + App::screenWidth < o2->rightX() && o1->rightX() + App::screenWidth >= o2->ix && o1->iy <= o2->bottomY() && o1->bottomY() >= o2->iy;
    } else if (o2->rightX() < o1->ix) {
        return o1->ix < o2->rightX() + App::screenWidth && o1->rightX() >= o2->ix + App::screenWidth && o1->iy <= o2->bottomY() && o1->bottomY() >= o2->iy;
    } else { //Normal case where no overlap
        return o1->ix < o2->rightX() && o1->rightX() >= o2->ix && o1->iy <= o2->bottomY() && o1->bottomY() >= o2->iy;
    }
}

bool coldec_player2obj(CPlayer * o1, CObject * o2)
{
    //Special cases to deal with players overlapping the right and left sides of the screen
    if (o1->rightX() < o2->x()) {
        return o1->ix + App::screenWidth < o2->x() + o2->collisionRectW() && o1->rightX() + App::screenWidth >= o2->x() && o1->iy < o2->y() + o2->collisionRectH() && o1->bottomY() >= o2->y();
    } else if (o2->x() + o2->collisionRectW() < o1->ix) {
        return o1->ix < o2->x() + o2->collisionRectW() + App::screenWidth && o1->rightX() >= o2->x() + App::screenWidth && o1->iy < o2->y() + o2->collisionRectH() && o1->bottomY() >= o2->y();
    } else { //Normal case where no overlap
        return o1->ix < o2->x() + o2->collisionRectW() && o1->rightX() >= o2->x() && o1->iy < o2->y() + o2->collisionRectH() && o2->y() <= o1->bottomY();
    }
}

bool coldec_obj2obj(CObject * o1, CObject * o2)
{
    //Special cases to deal with players overlapping the right and left sides of the screen
    short o1r = o1->x() + o1->collisionRectW();
    short o1b = o1->y() + o1->collisionRectH();
    short o2r = o2->x() + o2->collisionRectW();
    short o2b = o2->y() + o2->collisionRectH();

    if (o1r < o2->x()) {
        return o1->x() + App::screenWidth < o2r && o1r + App::screenWidth >= o2->x() && o1->y() < o2b && o1b >= o2->y();
    } else if (o2r < o1->x()) {
        return o1->x() < o2r + App::screenWidth && o1r >= o2->x() + App::screenWidth && o1->y() < o2b && o1b >= o2->y();
    } else {
        return o1->x() < o2r && o1r >= o2->x() && o1->y() < o2b && o1b >= o2->y();
    }
}

#include "WO_RaceGoal.h"

#include "GameValues.h"
#include "map.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Race.h"
#include "Score.h"

#include <cmath>


//------------------------------------------------------------------------------
// class race goal (for Race mode)
//------------------------------------------------------------------------------
short flagpositions[3][4][2] = { { { 18, 20 }, { 34, 20 }, { 0, 0 }, { 0, 0 } },
    { { 18, 20 }, { 34, 20 }, { 26, 36 }, { 0, 0 } },
    { { 18, 20 }, { 34, 20 }, { 18, 36 }, { 34, 36 } } };

OMO_RaceGoal::OMO_RaceGoal(gfxSprite* nspr, short id)
    : IO_OverMapObject(nspr, Vec2s::zero(), 2, 8)
    , goalID(id)
{
    iw = static_cast<short>(spr->getWidth()) >> 1;
    ih = static_cast<short>(spr->getHeight()) >> 1;
    collisionWidth = 36;
    collisionHeight = 36;
    collisionOffsetX = 16;
    collisionOffsetY = 18;

    objectType = object_race_goal;
    state = 1;

    for (short k = 0; k < 4; k++)
        tagged[k] = -1;

    angle = static_cast<float>(RANDOM_INT(1000) * 0.00628f);
    anglechange = static_cast<float>(RANDOM_INT(100) * 0.0002f);
    anglechangetimer = static_cast<short>(RANDOM_INT(50) + 100);

    velx = sin(angle);
    vely = cos(angle);

    // quantity must be set before placeRaceGoal(), which spaces the goals by it
    speed = static_cast<float>(game_values.gamemodesettings.race.speed) / 4.0f;
    quantity = game_values.gamemodesettings.race.quantity;
    isfinishline = goalID == quantity - 1;

    placeRaceGoal();
}

bool OMO_RaceGoal::collide(CPlayer* player)
{
    if (game_values.gamemode->getgamemode() == game_mode_race && player->tanookisuit.notStatue()) {
        CGM_Race* gamemode = static_cast<CGM_Race*>(game_values.gamemode);

        if (tagged[player->teamID] != player->colorID && gamemode->getNextGoal(player->teamID) >= goalID) {
            tagged[player->teamID] = player->colorID;

            if (isfinishline)
                ifSoundOnPlay(rm->sfx_racesound);
            else
                ifSoundOnPlay(rm->sfx_areatag);
        }

        if (gamemode->getNextGoal(player->teamID) == goalID)
            gamemode->setNextGoal(player->teamID);
    }
    return false;
}

void OMO_RaceGoal::draw()
{
    if (isfinishline) {
        spr->draw(ix - collisionOffsetX, iy - collisionOffsetY, {drawframe, 54, iw, ih});
    } else {
        spr->draw(ix - collisionOffsetX, iy - collisionOffsetY, {drawframe, 0, iw, ih});

        for (short k = 0; k < g_sim.score_cnt; k++) {
            if (tagged[k] > -1)
                rm->spr_bonus.draw(ix - collisionOffsetX + flagpositions[g_sim.score_cnt - 2][k][0], iy - collisionOffsetY + flagpositions[g_sim.score_cnt - 2][k][1], {0, tagged[k] * 16, 16, 16});
        }
    }

    rm->spr_racetext.draw(ix - collisionOffsetX + 26, iy - collisionOffsetY, {(goalID + 1) * 16, 0, 16, 16});
}

void OMO_RaceGoal::update()
{
    if (--anglechangetimer <= 0) {
        anglechange = static_cast<float>((RANDOM_INT(101) - 50) * 0.0002f);
        anglechangetimer = static_cast<short>(RANDOM_INT(50) + 100);
    }

    angle += anglechange;

    velx = speed * sin(angle);
    vely = speed * cos(angle);

    IO_OverMapObject::update();

    ix = static_cast<short>(fx);
    iy = static_cast<short>(fy);

    if (ix < 0) {
        velx = -velx;
        ix = 0;
        fx = static_cast<float>(ix);

        angle = atan2(velx, vely);
    } else if (ix + collisionWidth >= App::screenWidth) {
        velx = -velx;
        ix = App::screenWidth - 1 - collisionWidth;
        fx = static_cast<float>(ix);

        angle = atan2(velx, vely);
    }

    if (iy < 0) {
        vely = -vely;
        iy = 0;
        fy = static_cast<float>(iy);

        angle = atan2(velx, vely);
    } else if (iy + collisionHeight >= App::screenHeight) {
        vely = -vely;
        iy = App::screenHeight - 1 - collisionHeight;
        fy = static_cast<float>(iy);

        angle = atan2(velx, vely);
    }
}

void OMO_RaceGoal::placeRaceGoal()
{
    short x = 0, y = 0;

    if (goalID < g_map->iNumRaceGoals) {
        x = g_map->racegoallocations[goalID].x;
        y = g_map->racegoallocations[goalID].y;
    } else {
        short tries = 0;
        do {
            if (++tries > 32)
                break;

            x = static_cast<short>(RANDOM_INT(App::screenWidth - collisionWidth));
            y = static_cast<short>(RANDOM_INT(App::screenHeight - collisionHeight));
        } while (g_sim.objectcontainer[2].getClosestObject(x, y, object_race_goal) <= 250.0f - (quantity * 25.0f));
    }

    setXi(x);
    setYi(y);
}

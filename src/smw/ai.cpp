#include "ai.h"

#include "AINav.h"
#include "CmdArgs.h"

#include "GameValues.h"
#include "input.h"
#include "map.h"
#include "movingplatform.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/Race.h"
#include "gamemodes/Star.h"
#include "gamemodes/Tag.h"
#include "objects/IO_FlameCannon.h"

#include "objects/moving/MO_BonusHouseChest.h"
#include "objects/moving/MO_Boomerang.h"
#include "objects/moving/MO_CarriedObject.h"
#include "objects/moving/MO_Coin.h"
#include "objects/moving/MO_CollectionCard.h"
#include "objects/moving/MO_Explosion.h"
#include "objects/moving/MO_Fireball.h"
#include "objects/moving/MO_FlagBase.h"
#include "objects/moving/MO_Hammer.h"
#include "objects/moving/MO_IceBlast.h"
#include "objects/moving/MO_PirhanaPlant.h"
#include "objects/moving/MO_Podobo.h"
#include "objects/moving/MO_Yoshi.h"

#include "objects/carriable/CO_Flag.h"
#include "objects/carriable/CO_Spring.h"
#include "objects/carriable/CO_Bomb.h"
#include "objects/carriable/CO_Egg.h"
#include "objects/carriable/CO_PhantoKey.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Star.h"
#include "objects/carriable/CO_ThrowBlock.h"
#include "objects/carriable/CO_ThrowBox.h"

#include "objects/overmap/WO_Area.h"
#include "objects/overmap/WO_Phanto.h"
#include "objects/overmap/WO_PipeBonus.h"
#include "objects/overmap/WO_PipeCoin.h"
#include "objects/overmap/WO_RaceGoal.h"

#include "objects/powerup/PU_BobombPowerup.h"
#include "objects/powerup/PU_BombPowerup.h"
#include "objects/powerup/PU_BoomerangPowerup.h"
#include "objects/powerup/PU_BulletBillPowerup.h"
#include "objects/powerup/PU_ClockPowerup.h"
#include "objects/powerup/PU_CoinPowerup.h"
#include "objects/powerup/PU_ExtraGuyPowerup.h"
#include "objects/powerup/PU_ExtraHeartPowerup.h"
#include "objects/powerup/PU_ExtraTimePowerup.h"
#include "objects/powerup/PU_FeatherPowerup.h"
#include "objects/powerup/PU_FirePowerup.h"
#include "objects/powerup/PU_HammerPowerup.h"
#include "objects/powerup/PU_IceWandPowerup.h"
#include "objects/powerup/PU_JailKeyPowerup.h"
#include "objects/powerup/PU_LeafPowerup.h"
#include "objects/powerup/PU_ModPowerup.h"
#include "objects/powerup/PU_MysteryMushroomPowerup.h"
#include "objects/powerup/PU_PodoboPowerup.h"
#include "objects/powerup/PU_PoisonPowerup.h"
#include "objects/powerup/PU_PowPowerup.h"
#include "objects/powerup/PU_PWingsPowerup.h"
#include "objects/powerup/PU_SecretPowerup.h"
#include "objects/powerup/PU_StarPowerup.h"
#include "objects/powerup/PU_Tanooki.h"
#include "objects/powerup/PU_TreasureChestBonus.h"

#include <memory>
#include <cstdio>
#include <cstdlib> // abs()

namespace {
RandomNumberGenerator* g_aiRandom = nullptr;

RandomNumberGenerator& aiRandom()
{
    return g_aiRandom ? *g_aiRandom : RandomNumberGenerator::gameplay();
}
}  // namespace

void CPlayerAI::useRandom(RandomNumberGenerator* rng)
{
    g_aiRandom = rng;
}

bool CPlayerAI::canTakeStar() const
{
    return pPlayer->throw_star == 0 && g_sim.tick >= starThrowUntil;
}

// see docs/development/HowAIWorks.txt for more information

namespace aibench {
int kills[4] = {};
int deaths[4] = {};
int selfDeaths[4] = {};
int styleKills[32] = {};
int stuck[4] = {};
int itemUses[ItemKinds][4] = {};

void count(ItemUse kind, int player)
{
    if (cmd::g_autotest.aiBench && player >= 0 && player < 4)
        itemUses[kind][player]++;
}
int reversals[4] = {};
int navTried[3] = {};
int navReached[3] = {};

void reset()
{
    for (int k = 0; k < 4; k++)
        kills[k] = deaths[k] = selfDeaths[k] = 0;
}

void print()
{
    for (int g = 0; g < 4; g++) {
        const CPlayer* p = GetPlayerFromGlobalID(g);
        const int team = p ? p->getTeamID() : -1;
        printf("aibench: player %d %s team %d kills %d deaths %d self %d score %d winner %d stuck %d rev %d\n", g,
            (cmd::g_autotest.aiLegacyMask & (1 << g)) ? "old" : "new", team, kills[g], deaths[g], selfDeaths[g],
            team >= 0 ? g_sim.score[team].score : -1, team >= 0 && game_values.gamemode->winningteam == team ? 1 : 0, stuck[g], reversals[g]);
        stuck[g] = 0;
        reversals[g] = 0;
        printf("aibench: items player %d %s feather %d leaf %d pwings %d statue %d\n", g, (cmd::g_autotest.aiLegacyMask & (1 << g)) ? "old" : "new",
            itemUses[ItemFeather][g], itemUses[ItemLeaf][g], itemUses[ItemPWings][g], itemUses[ItemStatue][g]);
        for (int k = 0; k < ItemKinds; k++)
            itemUses[k][g] = 0;
    }
    printf("aibench: nav jump %d/%d drop %d/%d walk %d/%d\n", navReached[0], navTried[0], navReached[1], navTried[1], navReached[2], navTried[2]);
    for (int k = 0; k < 3; k++)
        navTried[k] = navReached[k] = 0;
    printf("aibench: styles");
    for (int k = 0; k < 32; k++) {
        if (styleKills[k])
            printf(" %d:%d", k, styleKills[k]);
        styleKills[k] = 0;
    }
    printf("\n");
    reset();
}
}

// The improved behaviour is on unless an AI comparison asked for the original logic for this player
bool CPlayerAI::improved() const
{
    return !(cmd::g_autotest.aiLegacyMask & (1 << pPlayer->globalID));
}

// The stomp fighting and path following help in the modes that are about knocking out the
// others or getting somewhere; in the item modes (coins, eggs, flags, stars...) they made the
// CPU worse in AI comparisons, so those keep the original behaviour.
bool CPlayerAI::improvedFighting() const
{
    if (!improved())
        return false;
    switch (game_values.gamemode->gamemode) {
        case game_mode_classic:
        case game_mode_frag:
        case game_mode_timelimit:
        case game_mode_jail:
        case game_mode_chicken:
        case game_mode_tag:
        case game_mode_koth:
        case game_mode_race:
        case game_mode_owned:
        case game_mode_frenzy:
        case game_mode_survival:
        case game_mode_chase:
        case game_mode_shyguytag:
            return true;
        default:
            return false;
    }
}

CPlayerAI::CPlayerAI()
{
    currentAttentionObject.iID = -1;
    currentAttentionObject.iType = 0;
    currentAttentionObject.iTimer = 0;
}

CPlayerAI::~CPlayerAI()
{
    std::map<int, AttentionObject*>::iterator itr = attentionObjects.begin(), lim = attentionObjects.end();
    while (itr != lim) {
        delete (itr->second);
        itr++;
    }

    attentionObjects.clear();
}

//Setup AI so that it can ignore or pay attention to some objects
void CPlayerAI::Init()
{
    //Scan yoshi's egg mode objects to make sure that we ignore eggs without matching yoshis
    if (game_values.gamemode->gamemode == game_mode_eggs) {
        bool fYoshi[4] = {false, false, false, false};

        //Scan Yoshis to see which ones are present
        for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
            if (auto* yoshi = dynamic_cast<MO_Yoshi*>(obj.get())) {
                fYoshi[yoshi->getColor()] = true;
            }
        }

        //Now scan eggs and ignore any egg that doesn't have a yoshi
        for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
            if (auto* egg = dynamic_cast<CO_Egg*>(obj.get())) {
                if (!fYoshi[egg->getColor()]) {
                    AttentionObject * ao = new AttentionObject();
                    ao->iID = egg->networkId();
                    ao->iType = 1;     //Ignore this object
                    ao->iTimer = 0;		//Ignore it forever

                    attentionObjects[ao->iID] = ao;
                }
            }
        }
    }
}

// Tuned with AI comparisons (--at-ai-old/--at-ai-bench)
namespace {
constexpr int SIDE_OFFSET = 96;   // climb to this far beside an opponent above us
constexpr int NEAR_ABOVE = 120;   // an opponent above us closer than this: dodge, don't climb
constexpr float LEAD_FRAMES = 6.0f;   // aim where the opponent will be this many frames from now
constexpr int SAME_LEVEL_JUMP = 120;  // jump at an opponent on our level this close
constexpr int UNDER_FEET = 56;        // keep this far out from under an opponent above us
constexpr int HOP_RANGE = 120;        // hop toward an opponent below us this close
constexpr int WAIT_RANGE = 140;       // an opponent jumping this close: wait for them to come down
constexpr float AIR_BRAKE_FRAMES = 6.0f;  // steering in the air allows for this much drift
}

void CPlayerAI::Think(COutputControl * playerKeys)
{
    short iDecisionPercentage[5] = {25, 35, 50, 75, 100};

    //Clear out the old input settings
    playerKeys->game_left.fDown = false;
    playerKeys->game_right.fDown = false;

    //A percentage of the time the cpu will do nothing based on the difficulty level (not in the
    //middle of a running jump: half done, it would only bump its head on the ledge)
    const bool committed = navMove == static_cast<short>(ainav::Move::RunJump) && navActive;
    if (aiRandom().getBoolean(100, iDecisionPercentage[game_values.cpudifficulty]) && pPlayer->isready() && !committed)
        return;

    playerKeys->game_jump.fDown = false;
    playerKeys->game_down.fDown = false;
    playerKeys->game_turbo.fDown = false;
    playerKeys->game_powerup.fDown = false;

    if (pPlayer->isdead() || pPlayer->isspawning())
        return;

    navActive = false;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);
    auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode);

    /***************************************************
    * 1. Figure out what objects are nearest to us
    ***************************************************/
    GetNearestObjects();

    short iTenSeconds = 15 * iDecisionPercentage[game_values.cpudifficulty] / 10;

    //If there is a goal, then make sure we aren't paying attention to it for too long
    if (nearestObjects.goal) {
        if (currentAttentionObject.iID == nearestObjects.goal->networkId()) {
            //If we have been paying attention to this goal for too long, then start ignoring it
            if (++currentAttentionObject.iTimer > iTenSeconds) {
                AttentionObject * ao = new AttentionObject();
                ao->iID = currentAttentionObject.iID;
                ao->iType = 1;     //Ignore this object
                ao->iTimer = iTenSeconds;

                attentionObjects[ao->iID] = ao;
            }
        } else {
            currentAttentionObject.iID = nearestObjects.goal->networkId();
            currentAttentionObject.iTimer = 0;
        }
    } else {
        currentAttentionObject.iID = -1;
    }

    //Expire attention objects
	std::vector<int> toDelete;
    std::map<int, AttentionObject*>::iterator itr = attentionObjects.begin(), lim = attentionObjects.end();
    while (itr != lim) {
        if (itr->second->iTimer > 0) {
            if (--(itr->second->iTimer) == 0) {

				toDelete.push_back(itr->first);
            }
        }

        ++itr;
	}

	for (std::vector<int>::iterator tdIt = toDelete.begin(); tdIt != toDelete.end(); ++tdIt) {
		// perform the actual disposal and removal
		std::map<int, AttentionObject*>::iterator deadObjIt = attentionObjects.find(*tdIt);

		delete deadObjIt->second;

		attentionObjects.erase(deadObjIt);
	}

    short iStoredPowerup = game_values.gamepowerups[pPlayer->globalID];
    MO_CarriedObject * carriedItem = pPlayer->carriedItem;
    short ix = pPlayer->ix;
    short iy = pPlayer->iy;
    short iTeamID = pPlayer->teamID;

    /***************************************************
    * 2. Figure out priority of actions
    ***************************************************/

    short actionType = 0;
    if (nearestObjects.threat && nearestObjects.threatdistance < 14400 && !pPlayer->isInvincible()) {
        actionType = 2;
    } else if (nearestObjects.stomp && nearestObjects.stompdistance < 14400) {
        actionType = 4;
    } else if (nearestObjects.goal && nearestObjects.teammate) {
        if (nearestObjects.goaldistance < nearestObjects.teammatedistance)
            actionType = 1;
        else
            actionType = 3;
    } else if (nearestObjects.teammate) {
        actionType = 3;
    } else if (nearestObjects.goal) {
        if (nearestObjects.playerdistance < 8100)
            actionType = 0;
        else
            actionType = 1;
    } else {
        actionType = 0;
    }

    /***************************************************
    * 3. Deal with closest item
    ***************************************************/
    //return if no players, goals, or threats available
    if (((actionType != 0) || nearestObjects.player) && (actionType != 1 || nearestObjects.goal) && ((actionType != 2) || nearestObjects.threat) &&
            ((actionType != 3) || nearestObjects.teammate) && ((actionType != 4) || nearestObjects.stomp) && (iFallDanger == 0)) {
        //Use turbo
        if (pPlayer->bobomb) {
            if (nearestObjects.playerdistance <= 4096 || nearestObjects.stompdistance <= 4096 || nearestObjects.threatdistance <= 4096)
                playerKeys->game_turbo.fDown = true;
        } else {
            if (pPlayer->powerup == -1 || !aiRandom().getBoolean(20))
                playerKeys->game_turbo.fDown = true;

            if (carriedItem) {
                //Hold important mode goal objects (we'll drop the star later if it is a bad star)
                MovingObjectType carriedobjecttype = carriedItem->getMovingObjectType();
                if ((carriedobjecttype == movingobject_egg) || (carriedobjecttype == movingobject_flag) ||
                        (carriedobjecttype == movingobject_star) || (carriedobjecttype == movingobject_phantokey)) {
                    playerKeys->game_turbo.fDown = true;
                }
            }
        }

        if (actionType == 0) { //Kill nearest player
            CPlayer * player = nearestObjects.player;
            bool * moveToward;
            bool * moveAway;

            if ((player->ix > ix && nearestObjects.playerwrap) || (player->ix < ix && !nearestObjects.playerwrap)) {
                moveToward = &playerKeys->game_left.fDown;
                moveAway = &playerKeys->game_right.fDown;
            } else {
                moveToward = &playerKeys->game_right.fDown;
                moveAway = &playerKeys->game_left.fDown;
            }

            const bool plainFight = !carriedItem && !(gmTag && (gmTag->tagged() == player || gmTag->tagged() == pPlayer)) &&
                !(gmChicken && gmChicken->chicken() == pPlayer) && !player->isInvincible() && !player->shyguy && !player->bobomb &&
                !pPlayer->isInvincible() && !pPlayer->shyguy && !pPlayer->bobomb;

            //Move player relative to opponent when we are playing star mode and we are holding a star
            if (improvedFighting() && plainFight) {
                // Climb up beside an opponent above us, never right under their feet
                int ox = player->ix + PW / 2;
                const int odx = ainav::HorizontalDistance(ox - (ix + PW / 2), iy, player->iy);
                const bool above = player->iy < iy - 40;
                if (above)
                    ox += odx > 0 ? -SIDE_OFFSET : SIDE_OFFSET;
                const bool usePath = (pPlayer->inair ? navMove != 0 : true) && NeedsPath(ox, player->iy + PH / 2) && !(above && odx > -NEAR_ABOVE && odx < NEAR_ABOVE);
                const bool midMove = pPlayer->inair && navMove != 0;   // finish a path move we've started
                if (!(usePath || midMove) || !NavigateTo(playerKeys, ox, player->iy + PH - 1)) {
                    if (!pPlayer->inair)
                        navMove = 0;
                    FightPlayer(playerKeys, player, nearestObjects.playerwrap);
                }
            } else if (game_values.gamemode->gamemode == game_mode_star) {
                if (carriedItem && carriedItem->getMovingObjectType() == movingobject_star) {
                    if ((static_cast<CO_Star*>(carriedItem))->getType() == 1)
                        *moveAway = true;
                    else {
                        *moveToward = true;

                        //If player is close
                        if (player->iy <= iy && player->iy > iy - 60 &&
                                player->ix - ix < 90 && player->ix - ix > -90) {
                            //And we are facing toward that player, throw the star
                            if ((player->ix > ix && pPlayer->isFacingRight()) ||
                                ((player->ix < ix) && !pPlayer->isFacingRight())) {
                                pPlayer->throw_star = 30;
                                playerKeys->game_turbo.fDown = false;
                            }
                        }
                    }
                }
            }
            //Move player relative to opponent when we are carrying a weapon like a shell or throwblock
            else if (carriedItem) {
                if (carriedItem->getMovingObjectType() == movingobject_shell || carriedItem->getMovingObjectType() == movingobject_throwblock) {
                    *moveToward = true;

                    //If player is close
                    if (player->iy > iy - 10 && player->iy < iy + 30 &&
                            abs(player->ix - ix) < 150) {
                        //And we are facing toward that player, throw the projectile
                        if ((player->ix > ix && pPlayer->isFacingRight()) ||
                            ((player->ix < ix) && !pPlayer->isFacingRight())) {
                            playerKeys->game_turbo.fDown = false;
                        }
                    }
                }
            } else if ((gmTag && gmTag->tagged() == player) || player->isInvincible() || player->shyguy || (gmChicken && gmChicken->chicken() == pPlayer)) {
                *moveAway = true;
            } else if (pPlayer->isInvincible() || pPlayer->shyguy  || pPlayer->bobomb || (gmTag && gmTag->tagged() == pPlayer)) {
                *moveToward = true;
            } else if (player->iy >= iy && !player->isInvincible() && !player->bobomb) {
                *moveToward = true;
            } else {
                if (nearestObjects.playerdistance < 8100)	//else if player is near but higher, run away (left)
                    *moveAway = true;
                else
                    *moveToward = true;	//Don't just stand and do nothing
            }


            if (improvedFighting() && plainFight) {
                //FightPlayer() did the jumping
            } else if (player->iy <= iy &&					//jump if player is higher or at the same level and
                    player->ix - ix < 45 &&				//player is very near
                    player->ix - ix > -45) {
                //or if player is high
                playerKeys->game_jump.fDown = true;
            } else if (player->iy > iy &&			//try to down jump if player is below us
                       player->ix - ix < 45 &&
                       player->ix - ix > -45 && aiRandom().getBoolean()) {
                //or if player is high
                if (!pPlayer->inair) playerKeys->game_down.fDown = true;

                if (!pPlayer->superstomp.isInSuperStompState()) {
                    //If the player has the tanooki or shoe, then try to super stomp on them
                    if (pPlayer->tanookisuit.isOn() || pPlayer->kuriboshoe.is_on()) {
                        playerKeys->game_down.fDown = true;
                        playerKeys->game_jump.fPressed = true;
                        if (pPlayer->tanookisuit.isOn())
                        {
                            playerKeys->game_turbo.fPressed = true;
                            pPlayer->lockfire = false;
                        }
                    }
                }
            }
        } else if (actionType == 1 && nearestObjects.goalIsBlock) { //Hit a ? block from below
            CObject * block = nearestObjects.goal;
            const int blockX = block->x() + TILESIZE / 2;
            const int spotY = nearestObjects.goalSpotRow * TILESIZE + TILESIZE / 2;
            int spotCol = -1, spotRow = -1;
            const bool under = !pPlayer->inair && ainav::PlayerSpot(ix, iy, spotCol, spotRow) && spotRow == nearestObjects.goalSpotRow &&
                               spotCol == block->x() / TILESIZE;
            const int dx = ainav::HorizontalDistance(blockX - (ix + PW / 2), iy, spotY);
            if (under) {
                //Under it: line up with it, then jump into it
                if (dx < -4)
                    playerKeys->game_left.fDown = true;
                else if (dx > 4)
                    playerKeys->game_right.fDown = true;
                if (dx >= -8 && dx <= 8)
                    playerKeys->game_jump.fDown = !pPlayer->lockjump;
            } else if (pPlayer->inair && navMove == 0 && dx >= -24 && dx <= 24 && block->y() < iy) {
                //Jumping into it: keep rising, stay under it
                if (pPlayer->vely < 0.0f)
                    playerKeys->game_jump.fDown = true;
                if (dx < -4)
                    playerKeys->game_left.fDown = true;
                else if (dx > 4)
                    playerKeys->game_right.fDown = true;
            } else if (!NavigateTo(playerKeys, blockX, spotY)) {
                //No path known: head that way
                if (dx < 0)
                    playerKeys->game_left.fDown = true;
                else
                    playerKeys->game_right.fDown = true;
            }
        } else if (actionType == 1) { //Go for goal
            CObject * goal = nearestObjects.goal;
            const int goalX = goal->x() + goal->w() / 2;
            const int goalY = goal->y() + goal->h() / 2;

            if (improvedFighting() && (NeedsPath(goalX, goalY) || (pPlayer->inair && navMove != 0)) && NavigateTo(playerKeys, goalX, goalY)) {
                //Following the path to it
            } else if ((goal->x() > ix && nearestObjects.goalwrap) || (goal->x() < ix && !nearestObjects.goalwrap))
                playerKeys->game_left.fDown = true;
            else
                playerKeys->game_right.fDown = true;

            if (goal->y() <= iy && goal->x() - ix < 45 && goal->x() - ix > -45) {
                playerKeys->game_jump.fDown = true;
            } else if (goal->y() > iy && goal->x() - ix < 45 && goal->x() - ix > -45) {
                if (!pPlayer->inair) playerKeys->game_down.fDown = true;
            }

            if (dynamic_cast<CO_Egg*>(goal))
                playerKeys->game_turbo.fDown = true;
            else if (dynamic_cast<CO_Star*>(goal) && canTakeStar())
                playerKeys->game_turbo.fDown = true;
            else if (dynamic_cast<CO_Flag*>(goal))
                playerKeys->game_turbo.fDown = true;

            //Drop current item if we're going after another carried item
            if (carriedItem) {
                if (auto* movingobject = dynamic_cast<IO_MovingObject*>(goal)) {
                    MovingObjectType goalobjecttype = movingobject->getMovingObjectType();
                    if (goalobjecttype == movingobject_egg || goalobjecttype == movingobject_flag ||
                            goalobjecttype == movingobject_star || goalobjecttype == movingobject_phantokey) {
                        MovingObjectType carriedobjecttype = carriedItem->getMovingObjectType();

                        //If we are holding something that isn't a mode goal object, then drop it
                        if (carriedobjecttype != movingobject_egg && carriedobjecttype != movingobject_flag &&
                                carriedobjecttype != movingobject_star && carriedobjecttype != movingobject_phantokey) {
                            playerKeys->game_turbo.fDown = false;
                        }
                    }
                }
            }

            //Open treasure chests in bonus houses in world mode
            if (dynamic_cast<MO_BonusHouseChest*>(goal))
                playerKeys->game_turbo.fPressed = true;
        } else if (actionType == 2) { //Evade Threat
            CObject * threat = nearestObjects.threat;

            //If threat, always use turbo
            playerKeys->game_turbo.fDown = true;

            if ((threat->x() > ix && nearestObjects.threatwrap) || (threat->x() < ix && !nearestObjects.threatwrap))
                playerKeys->game_right.fDown = true;
            else
                playerKeys->game_left.fDown = true;

            if (threat->y() <= iy && threat->x() - ix < 60 && threat->x() - ix > -60) {
                if (!pPlayer->inair) playerKeys->game_down.fDown = true;
            } else if (threat->y() > iy && threat->x() - ix < 60 && threat->x() - ix > -60) {
                playerKeys->game_jump.fDown = true;
            }
        } else if (actionType == 3) { //Tag teammate
            CPlayer * teammate = nearestObjects.teammate;

            if ((teammate->ix > ix && nearestObjects.teammatewrap) || (teammate->ix < ix && !nearestObjects.teammatewrap))
                playerKeys->game_left.fDown = true;
            else
                playerKeys->game_right.fDown = true;

            if (teammate->iy <= iy && teammate->ix - ix < 45 && teammate->ix - ix > -45) {
                playerKeys->game_jump.fDown = true;
            } else if (teammate->iy > iy && teammate->ix - ix < 45 && teammate->ix - ix > -45) {
                if (!pPlayer->inair) playerKeys->game_down.fDown = true;
            }
        } else if (actionType == 4) { //Stomp something (goomba, koopa, cheepcheep)
            CObject * stomp = nearestObjects.stomp;
            bool * moveToward;
            bool * moveAway;

            if ((stomp->x() > ix && nearestObjects.stompwrap) || (stomp->x() < ix && !nearestObjects.stompwrap)) {
                moveToward = &playerKeys->game_left.fDown;
                moveAway = &playerKeys->game_right.fDown;
            } else {
                moveToward = &playerKeys->game_right.fDown;
                moveAway = &playerKeys->game_left.fDown;
            }

            if (stomp->y() > iy + PH || pPlayer->shyguy || pPlayer->isInvincible() ||
                    (carriedItem && (carriedItem->getMovingObjectType() == movingobject_shell || carriedItem->getMovingObjectType() == movingobject_throwblock))) {
                //if true stomp target is lower or at the same level, run toward
                *moveToward = true;
            } else {
                if (nearestObjects.stompdistance < 8100)
                    *moveAway = true;
                else
                    *moveToward = true;
            }


            if (stomp->y() <= iy + PH && nearestObjects.stompdistance < 2025) {
                playerKeys->game_jump.fDown = true;
            } else if (stomp->y() > iy + PH && nearestObjects.stompdistance < 2025) {
                if (!pPlayer->inair) playerKeys->game_down.fDown = true;

                if (!pPlayer->superstomp.isInSuperStompState()) {
                    //If the player has the tanooki or shoe, then try to super stomp on them
                    if (pPlayer->tanookisuit.isOn() || pPlayer->kuriboshoe.is_on()) {
                        playerKeys->game_down.fDown = true;
                        playerKeys->game_jump.fPressed = true;
                        if (pPlayer->tanookisuit.isOn())
                        {
                            playerKeys->game_turbo.fPressed = true;
                            pPlayer->lockfire = false;
                        }
                    }
                }
            }
        }
    }

    if (improved())
        AimWeapon(playerKeys, actionType);

    //Jump if trying to move left/right and x velocity is zero
    if (!navActive && (playerKeys->game_left.fDown || playerKeys->game_right.fDown) && pPlayer->velx == 0.0f)
        playerKeys->game_jump.fDown = true;

    //Pick up throwable blocks from below
    if (playerKeys->game_turbo.fDown && !carriedItem && !pPlayer->inair)
        playerKeys->game_turbo.fPressed = true;

    //Stay inside tanooki statue (the improved CPU decides that in UseMovementItems())
    if (!improved() && pPlayer->tanookisuit.isOn() && pPlayer->tanookisuit.isStatue())
        playerKeys->game_down.fDown = true;

    //"Star Mode" specific stuff
    //Drop the star if we're not it
    if (game_values.gamemode->gamemode == game_mode_star) {
        CGM_Star * starmode = static_cast<CGM_Star*>(game_values.gamemode);

        if (carriedItem && carriedItem->getMovingObjectType() == movingobject_star && (static_cast<CO_Star*>(carriedItem))->getType() == 0 && !starmode->isplayerstar(pPlayer))
            playerKeys->game_turbo.fDown = false;
        else if (starmode->isplayerstar(pPlayer) && canTakeStar())
            playerKeys->game_turbo.fDown = true;
    }

    if (carriedItem) {
        MovingObjectType carriedobjecttype = carriedItem->getMovingObjectType();

        //"Phanto Mode" specific stuff
        if (game_values.gamemode->gamemode == game_mode_chase) {
            //Ignore the key if a phanto is really close
            if (nearestObjects.threat && nearestObjects.threat->getObjectType() == object_phanto && nearestObjects.threatdistance < 4096 && !pPlayer->isInvincible()) {
                playerKeys->game_turbo.fDown = false;

                //Ignore the key for a little while
                if (attentionObjects.find(carriedItem->networkId()) != attentionObjects.end()) {
                    AttentionObject * ao = attentionObjects[carriedItem->networkId()];
                    ao->iType = 1;
                    ao->iTimer = iDecisionPercentage[game_values.cpudifficulty] / 3;
                } else {
                    AttentionObject * ao = new AttentionObject();
                    ao->iID = carriedItem->networkId();
                    ao->iType = 1;
                    ao->iTimer = iDecisionPercentage[game_values.cpudifficulty] / 3;
                    attentionObjects[ao->iID] = ao;
                }
            }
        }
        //If we are holding something that we are ignoring, drop it
        else if (attentionObjects.find(carriedItem->networkId()) != attentionObjects.end()) {
            playerKeys->game_turbo.fDown = false;
        }

        //Drop live bombs if they are not yours
        if (auto* bomb = dynamic_cast<CO_Bomb*>(carriedItem)) {
            if (bomb->iTeamID != pPlayer->teamID)
                playerKeys->game_turbo.fDown = false;
        } else if (carriedobjecttype == movingobject_carried || carriedobjecttype == movingobject_throwbox) { //Drop springs,spikes,shoes
            playerKeys->game_turbo.fDown = false;
        }
    }

    if (improved())
        Stabilize(playerKeys);

    /***************************************************
    * 4. Deal with falling onto spikes (this needs to be improved)
    ***************************************************/

    //Make sure we don't jump over something that could kill us
    short iDeathY = iy / TILESIZE;
    short iDeathX1;
    short iDeathX2;

    //The player's position is not wrapped around the screen edges yet when it was pushed after its last move
    if (ix < 0 && ix / TILESIZE < 0)
        iDeathX1 = (ix + App::screenWidth) / TILESIZE;
    else if (ix >= App::screenWidth)
        iDeathX1 = (ix - App::screenWidth) / TILESIZE;
    else
        iDeathX1 = ix / TILESIZE;

    if (ix + PW >= App::screenWidth)
        iDeathX2 = (ix + PW - App::screenWidth) / TILESIZE;
    else if (ix + PW < 0 && (ix + PW) / TILESIZE < 0)
        iDeathX2 = (ix + PW + App::screenWidth) / TILESIZE;
    else
        iDeathX2 = (ix + PW) / TILESIZE;

    if (iDeathY < 0)
        iDeathY = 0;

    //short depth = -1;
    while (iDeathY < MAPHEIGHT) {
        int ttLeftTile = g_map->map(iDeathX1, iDeathY);
        int ttRightTile = g_map->map(iDeathX2, iDeathY);

        bool fDeathTileUnderPlayer1 = ((ttLeftTile & tile_flag_death_on_top) && (ttRightTile & tile_flag_death_on_top)) ||
                                     ((ttLeftTile & tile_flag_death_on_top) && !(ttRightTile & tile_flag_solid)) ||
                                     (!(ttLeftTile & tile_flag_solid) && (ttRightTile & tile_flag_death_on_top));

        if (fDeathTileUnderPlayer1) {
            if (iFallDanger == 0)
                iFallDanger = (playerKeys->game_right.fDown ? -1 : 1);

            goto ExitDeathCheck;
        } else if ((ttLeftTile & tile_flag_solid) || (ttLeftTile & tile_flag_solid_on_top) || g_map->block(iDeathX1, iDeathY) ||
                  (ttRightTile & tile_flag_solid) || (ttRightTile & tile_flag_solid_on_top) || g_map->block(iDeathX2, iDeathY)) {
            iFallDanger = 0;
            goto ExitDeathCheck;
        }


        //Look through all platforms and see if we are hitting solid or death tiles in them
        for (size_t iPlatform = 0; iPlatform < g_map->platforms.size(); iPlatform++) {
            int lefttile = g_map->platforms[iPlatform]->GetTileTypeFromCoord(ix, iDeathY << 5);
            int righttile = g_map->platforms[iPlatform]->GetTileTypeFromCoord(ix + PW, iDeathY << 5);

            bool fDeathTileUnderPlayer2 = ((lefttile & tile_flag_death_on_top) && (righttile & tile_flag_death_on_top)) ||
                                         ((lefttile & tile_flag_death_on_top) && !(righttile & tile_flag_solid)) ||
                                         (!(lefttile & tile_flag_solid) && (righttile & tile_flag_death_on_top));

            if (fDeathTileUnderPlayer2) {
                if (iFallDanger == 0)
                    iFallDanger = (playerKeys->game_right.fDown ? -1 : 1);

                goto ExitDeathCheck;
            } else if ((lefttile & tile_flag_solid) || (lefttile & tile_flag_solid_on_top) ||
                      (righttile & tile_flag_solid) || (righttile & tile_flag_solid_on_top)) {
                iFallDanger = 0;
                goto ExitDeathCheck;
            }
        }

        iDeathY++;
    }

//If we are done checking for death under the player, come here
ExitDeathCheck:

    //There is a death tile below us so move to the side that is safest
    if (iFallDanger < 0) {
        playerKeys->game_right.fDown = true;
        playerKeys->game_left.fDown = false;
        playerKeys->game_jump.fDown = true;
        playerKeys->game_turbo.fDown = true;
        playerKeys->game_down.fDown = false;
    } else if (iFallDanger > 0) {
        playerKeys->game_right.fDown = false;
        playerKeys->game_left.fDown = true;
        playerKeys->game_jump.fDown = true;
        playerKeys->game_turbo.fDown = true;
        playerKeys->game_down.fDown = false;
    }

    //Make sure we don't jump up into something that can kill us
    iDeathY = iy / TILESIZE;

    if (iDeathY < 0)
        iDeathY = 0;

    short heightlimit = 3;
    while (iDeathY >= 0 && heightlimit > 0) {
        int ttLeftTile = g_map->map(iDeathX1, iDeathY);
        int ttRightTile = g_map->map(iDeathX2, iDeathY);

        if (heightlimit == 2 && (ttLeftTile & tile_flag_solid || ttRightTile & tile_flag_solid) &&
            !g_map->checkforwarp(iDeathX1, iDeathX2, iDeathY, 2)) //Avoid jumping wildly in 1 tile high gaps
            playerKeys->game_jump.fDown = false;

        if ((ttLeftTile & tile_flag_solid && (ttLeftTile & tile_flag_death_on_bottom) == 0) ||
                (ttRightTile & tile_flag_solid && (ttRightTile & tile_flag_death_on_bottom) == 0) ||
                g_map->block(iDeathX1, iDeathY) || g_map->block(iDeathX2, iDeathY)) {
            break;
        } else if (ttLeftTile & tile_flag_death_on_bottom || ttRightTile & tile_flag_death_on_bottom) {
            playerKeys->game_jump.fDown = false;
            break;
        }

        iDeathY--;
        heightlimit--;
    }

    if (improved())
        UseMovementItems(playerKeys, actionType, nearestObjects);

    /***************************************************
    * 5. Use stored powerups
    ***************************************************/

    if (iStoredPowerup > 0) {
        const auto canUse = [this, carriedItem, iTeamID](PowerupType powerup) -> bool {
            switch (powerup) {
                case PowerupType::PoisonMushroom:
                    return false;
                case PowerupType::ExtraLife1:
                case PowerupType::ExtraLife2:
                case PowerupType::ExtraLife3:
                case PowerupType::ExtraLife5:
                case PowerupType::Clock:
                case PowerupType::Pow:
                case PowerupType::BulletBill:
                case PowerupType::Mod:
                case PowerupType::Podobo:
                    return true;  // use 1-5up, clock, pow, bulletbill, mod, podobo, right away
                case PowerupType::Fire:
                case PowerupType::Hammer:
                case PowerupType::Feather:
                case PowerupType::Boomerang:
                case PowerupType::IceWand:
                case PowerupType::Bomb:
                case PowerupType::Leaf:
                case PowerupType::PWings:
                    return pPlayer->powerup == -1;
                case PowerupType::Star:
                    return !pPlayer->isInvincible();
                case PowerupType::Bobomb:
                    return !pPlayer->bobomb;
                case PowerupType::ShellGreen:
                case PowerupType::ShellRed:
                case PowerupType::ShellSpiny:
                case PowerupType::ShellBuzzy:
                    return !carriedItem;
                case PowerupType::Tanooki:
                    return !pPlayer->tanookisuit.isOn();
                case PowerupType::JailKey:
                    return pPlayer->jail.isActive();
                case PowerupType::MysteryMushroom:
                    //See if another player has a powerup
                    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
                        if (iPlayer == static_cast<size_t>(pPlayer->localID) || g_sim.players[iPlayer]->teamID == iTeamID) {
                            continue;
                        }
                        if (game_values.gamepowerups[g_sim.players[iPlayer]->globalID] > 0) {
                            return true;
                        }
                    }
                    break;
            }
            return false;
        };
        if (canUse(static_cast<PowerupType>(iStoredPowerup))) {
            playerKeys->game_powerup.fDown = true;
        }
    }

    // AI comparisons: count direction reversals (dithering) and turns spent pushing against
    // something without moving
    if (cmd::g_autotest.aiBench && pPlayer->globalID < 4) {
        static int benchLastDir[4] = {0, 0, 0, 0};
        const int d = playerKeys->game_left.fDown == playerKeys->game_right.fDown ? 0 : playerKeys->game_left.fDown ? -1 : 1;
        if (d != 0 && benchLastDir[pPlayer->globalID] == -d)
            aibench::reversals[pPlayer->globalID]++;
        if (d != 0)
            benchLastDir[pPlayer->globalID] = d;
    }
    if (cmd::g_autotest.aiBench && pPlayer->globalID < 4) {
        // Stuck: pushing the same way as last turn and not having moved at all since
        static int benchLastIx[4] = {-1000, -1000, -1000, -1000};
        static int benchLastPush[4] = {0, 0, 0, 0};
        const int g = pPlayer->globalID;
        const int push = playerKeys->game_left.fDown == playerKeys->game_right.fDown ? 0 : playerKeys->game_left.fDown ? -1 : 1;
        if (push != 0 && push == benchLastPush[g] && pPlayer->ix == benchLastIx[g] && !pPlayer->inair && !pPlayer->isFrozen())
            aibench::stuck[g]++;
        benchLastPush[g] = push;
        benchLastIx[g] = pPlayer->ix;
    }
}

// The simple "move toward it and jump when close" approach works when the target is about
// level with us; for targets well above us, or below and to the side, we follow a path.
bool CPlayerAI::NeedsPath(int x, int y) const
{
    const int dx = ainav::HorizontalDistance(x - (pPlayer->ix + PW / 2), pPlayer->iy, y);
    const int dy = y - (pPlayer->iy + PH / 2);
    const int adx = dx < 0 ? -dx : dx;
    if (dy < -40 || (dy > 48 && adx > 48))
        return true;
    // A wall between us: walking straight at it gets nowhere
    if (adx > 16 && ainav::FreeRun(pPlayer->ix, pPlayer->iy, dx < 0 ? -1 : 1, 1) == 0)
        return true;
    // On another level and not close: the way there may go around
    return (dy < -24 || dy > 24) && adx > 96;
}

bool CPlayerAI::NavigateTo(COutputControl * playerKeys, int x, int y)
{
    bool& left = playerKeys->game_left.fDown;
    bool& right = playerKeys->game_right.fDown;
    const int px = pPlayer->ix;

    auto steerTo = [&](int col) {
        // Toward the middle of the column, the short way around the screen; in the air we
        // keep moving the way we're going for a while, so aim with where that takes us
        const int ahead = pPlayer->inair ? static_cast<int>(pPlayer->velx * AIR_BRAKE_FRAMES) : 0;
        int d = col * TILESIZE + (TILESIZE - PW) / 2 - px - ahead;
        if (d > App::screenWidth / 2)
            d -= App::screenWidth;
        else if (d < -App::screenWidth / 2)
            d += App::screenWidth;
        if (d < -3)
            left = true;
        else if (d > 3)
            right = true;
    };

    if (pPlayer->inair) {
        if (navMove == 0)
            return false;
        navActive = true;
        if (navMove == static_cast<short>(ainav::Move::Jump)) {
            // Rise straight up until our feet are above the floor we're jumping onto, then steer
            const int floorTop = (navRow + 1) * TILESIZE;
            if (pPlayer->vely < 0.0f)
                playerKeys->game_jump.fDown = true;
            if (pPlayer->iy + PH <= floorTop + 4 || navRow * TILESIZE >= pPlayer->iy)
                steerTo(navCol);
        } else if (navMove == static_cast<short>(ainav::Move::RunJump)) {
            // The whole jump; brake (back toward where we took off) until our head is above the
            // ledge's underside, then onto it (its side may stop us on the way up: harmless)
            const int floorBottom = (navRow + 2) * TILESIZE;
            if (pPlayer->vely < 0.0f)
                playerKeys->game_jump.fDown = true;
            if (pPlayer->iy >= floorBottom)
                steerTo(navLastCol);
            else
                steerTo(navCol);
        } else {
            steerTo(navCol);
        }
        return true;
    }

    int col, row;
    if (!ainav::PlayerSpot(pPlayer->ix, pPlayer->iy, col, row))
        return false;
    if (cmd::g_autotest.aiBench && navMove != 0 && !(col == navLastCol && row == navLastRow)) {
        // We were following a move and have landed: did we get where it leads?
        const int k = navMove == static_cast<short>(ainav::Move::Jump) || navMove == static_cast<short>(ainav::Move::RunJump) ? 0
                      : navMove == static_cast<short>(ainav::Move::Drop) ? 1 : 2;
        aibench::navTried[k]++;
        if (col == navCol && row == navRow)
            aibench::navReached[k]++;
    }
    const ainav::Step step = ainav::NextStep(col, row, x, y);
    if (step.move == ainav::Move::None) {
        navMove = 0;
        return false;
    }

    navActive = true;
    navLastCol = static_cast<short>(col);
    navLastRow = static_cast<short>(row);
    navCol = static_cast<short>(step.toCol);
    navRow = static_cast<short>(step.toRow);
    navMove = static_cast<short>(step.move);

    switch (step.move) {
        case ainav::Move::Walk:
            if (step.dir < 0)
                left = true;
            else if (step.dir > 0)
                right = true;
            //(onto the start of a running jump: get there running, fast enough to take off)
            if (step.dir != 0 && ainav::RunJumpFrom(step.toCol, step.toRow, step.dir))
                playerKeys->game_turbo.fDown = true;
            break;
        case ainav::Move::Drop:
            // Falling through needs a fresh press of down
            playerKeys->game_down.fDown = !pPlayer->lockfall;
            break;
        case ainav::Move::Jump: {
            // Jumping needs a fresh press of jump; line up under the gap for straight-up jumps
            int d = col * TILESIZE + (TILESIZE - PW) / 2 - px;
            if (step.dir == 0 && (d < -6 || d > 6)) {
                steerTo(col);
            } else {
                playerKeys->game_jump.fDown = !pPlayer->lockjump;
                if (step.toRow >= row && step.dir != 0) {
                    // Across a gap: run for it
                    if (step.dir < 0)
                        left = true;
                    else
                        right = true;
                }
            }
            break;
        }
        case ainav::Move::RunJump: {
            // Run (with run held, past walking speed) toward the ledge and jump while we're still
            // in this column's first part, braking at once (the jump keeps its height: the speed
            // counts when it starts). Too far in, or too slow to get there in time: back up first.
            const int d = step.dir < 0 ? -1 : 1;
            const float v = pPlayer->velx * static_cast<float>(d);
            const int along = (px + PW / 2 - (col * TILESIZE + TILESIZE / 2)) * d;
            constexpr float TAKEOFF = VELMOVING + 0.6f;    // still over walking speed after a frame of braking
            constexpr int LAST = 8;                          // the last place to take off (pixels past the middle)
            bool& ahead = d < 0 ? left : right;
            bool& back = d < 0 ? right : left;
            if (v > TAKEOFF && along <= LAST) {
                playerKeys->game_turbo.fDown = true;
                playerKeys->game_jump.fDown = !pPlayer->lockjump;
                back = true;
            } else {
                const float speed = v > 0.0f ? v : 0.0f;
                const float need = (TAKEOFF * TAKEOFF - speed * speed) / (2.0f * VELMOVINGADD) + (v < 0.0f ? v * v / (2.0f * VELMOVINGADD) : 0.0f);
                if (v >= 0.0f && static_cast<float>(along) + need <= static_cast<float>(LAST)) {
                    playerKeys->game_turbo.fDown = true;
                    ahead = true;
                } else {
                    back = true;
                }
            }
            break;
        }
        default:
            break;
    }
    return true;
}

// Stomp an opponent and avoid being stomped (both just need the higher feet when the players meet).
// We aim where the opponent will be in a few frames, get above opponents next to us by jumping,
// steer onto opponents below us while we fall, and stay out from under opponents above us.
void CPlayerAI::FightPlayer(COutputControl * playerKeys, CPlayer * player, bool wrap)
{
    // Horizontal distance, the short way around the screen
    int dx = player->ix - pPlayer->ix;
    if (wrap)
        dx += dx > 0 ? -App::screenWidth : App::screenWidth;
    const int lead = static_cast<int>(player->velx * LEAD_FRAMES);    // where the opponent will be in ~6 frames
    const int aimDx = dx + lead;
    const int dy = player->iy - pPlayer->iy;                   // > 0: the opponent is lower

    bool& left = playerKeys->game_left.fDown;
    bool& right = playerKeys->game_right.fDown;
    auto toward = [&](int d) { if (d < 0) left = true; else if (d > 0) right = true; };
    auto away = [&](int d) {
        // Away from them; straight above or below us, toward the side with more room
        int dir = d < 0 ? 1 : d > 0 ? -1 : 0;
        const int runRight = ainav::FreeRun(pPlayer->ix, pPlayer->iy, 1);
        const int runLeft = ainav::FreeRun(pPlayer->ix, pPlayer->iy, -1);
        if (dir == 0 || (dir > 0 ? runRight : runLeft) == 0)
            dir = runRight >= runLeft ? 1 : -1;
        if (dir > 0)
            right = true;
        else
            left = true;
    };

    // A tanooki statue or a shielded opponent can't be hurt, and a statue lands as a super stomp:
    // keep away until it's over
    if (player->IsTanookiStatue() || player->isShielded()) {
        away(dx);
        return;
    }

    if (dy < -12) {
        // The opponent is above us
        if (abs(dx) < UNDER_FEET && player->vely >= 0.0f) {
            away(dx);                                          // don't stand under falling feet
        } else if (abs(dx) < WAIT_RANGE && dy > -100 && !pPlayer->inair && player->vely < 0.0f) {
            away(dx);                                          // they're jumping right next to us: wait it out
        } else {
            toward(aimDx);
            // Climb toward them when they're not over us
            if (abs(dx) >= UNDER_FEET && !pPlayer->inair && dy > -150)
                playerKeys->game_jump.fDown = true;
        }
        // Keep jumping up if we're already above their feet level on the way up
        if (pPlayer->inair && pPlayer->vely < 0.0f && abs(dx) >= 56)
            playerKeys->game_jump.fDown = true;
    } else if (dy > 12) {
        // The opponent is below us: get over them and come down
        if (pPlayer->inair) {
            if (abs(aimDx) > 4)
                toward(aimDx);
            // Right above them with the tanooki suit or the shoe: super stomp
            if (abs(aimDx) < 24 && !pPlayer->superstomp.isInSuperStompState() &&
                    (pPlayer->tanookisuit.isOn() || pPlayer->kuriboshoe.is_on())) {
                playerKeys->game_down.fDown = true;
                playerKeys->game_jump.fPressed = true;
                if (pPlayer->tanookisuit.isOn()) {
                    playerKeys->game_turbo.fPressed = true;
                    pPlayer->lockfire = false;
                }
            }
        } else {
            toward(aimDx);
            if (abs(aimDx) < 24)
                playerKeys->game_down.fDown = true;           // drop through a platform onto them
            else if (abs(dx) < HOP_RANGE)
                playerKeys->game_jump.fDown = true;           // hop so we come down on them, not walk into them
        }
    } else {
        // Same level: jump to get above them, then come down on them
        toward(aimDx);
        if (!pPlayer->inair && abs(dx) < SAME_LEVEL_JUMP)
            playerKeys->game_jump.fDown = true;
        else if (pPlayer->inair && pPlayer->vely < 0.0f)
            playerKeys->game_jump.fDown = true;               // full height
    }
}

// Keep the CPU from getting stuck: it hops over low obstacles right away, and after 3 turns of
// pushing without moving it jumps and turns around for a while. This runs every turn, whatever
// the CPU was doing.
void CPlayerAI::Stabilize(COutputControl * playerKeys)
{
    bool& left = playerKeys->game_left.fDown;
    bool& right = playerKeys->game_right.fDown;
    int dir = left == right ? 0 : left ? -1 : 1;

    if (unstickTimer > 0) {
        unstickTimer--;
        dir = unstickDir;
        // Still against something: hop
        if (!pPlayer->inair && pPlayer->ix == lastIx)
            playerKeys->game_jump.fDown = !pPlayer->lockjump;
    } else {
        // A low obstacle right in front of us (one tile, free above it): hop over it now (jumping
        // needs a fresh press of the button)
        if (dir != 0 && !pPlayer->inair && ainav::FreeRun(pPlayer->ix, pPlayer->iy, dir, 1) == 0 &&
                ainav::FreeRun(pPlayer->ix, pPlayer->iy - TILESIZE, dir, 1) > 0)
            playerKeys->game_jump.fDown = !pPlayer->lockjump;

        const bool pushing = dir != 0 && !pPlayer->inair && pPlayer->ix == lastIx && !pPlayer->isFrozen();
        stuckThinks = pushing ? static_cast<short>(stuckThinks + 1) : 0;
        if (stuckThinks >= 3) {
            stuckThinks = 0;
            navMove = 0;
            // Turn around (if there's room behind us) and jump: that clears most things
            const int back = ainav::FreeRun(pPlayer->ix, pPlayer->iy, -dir);
            unstickDir = static_cast<short>(back > 0 ? -dir : dir);
            unstickTimer = 6;
            playerKeys->game_jump.fDown = !pPlayer->lockjump;
            dir = unstickDir;
        }
    }

    left = dir < 0;
    right = dir > 0;
    lastIx = pPlayer->ix;
}

// Is an opponent about to land on us (they're above us, close, and on the way down)?
bool CPlayerAI::AboutToBeStomped() const
{
    for (const CPlayer* other : g_sim.players) {
        if (!other || other == pPlayer || !other->isready() || other->getTeamID() == pPlayer->getTeamID())
            continue;
        const int dx = ainav::HorizontalDistance(other->ix - pPlayer->ix, other->iy, pPlayer->iy);
        const int dy = other->iy - pPlayer->iy;   // < 0: above us
        if (dx > -40 && dx < 40 && dy < -8 && dy > -96 && other->vely > -1.0f)
            return true;
    }
    return false;
}

// The items that move us or protect us: the feather (an extra jump in the air), the leaf (shaking
// the tail slows a fall), the P-Wings (flight) and the tanooki suit (the statue can't be hurt, and
// comes down as a super stomp). Each starts with a fresh press of a button, so a button we hold
// is let go for a turn first.
void CPlayerAI::UseMovementItems(COutputControl * playerKeys, short actionType, const NearestObjects& nearest)
{
    if (pPlayer->isFrozen() || pPlayer->bobomb || pPlayer->kuriboshoe.is_on())
        return;

    // Where we want to go
    bool hasTarget = false;
    int tx = 0, ty = 0;
    if (actionType == 0 && nearest.player) {
        tx = nearest.player->ix + PW / 2;
        ty = nearest.player->iy + PH / 2;
        hasTarget = true;
    } else if (actionType == 1 && nearest.goal) {
        tx = nearest.goal->x() + nearest.goal->w() / 2;
        ty = nearest.goalIsBlock ? nearest.goalSpotRow * TILESIZE + TILESIZE / 2 : nearest.goal->y() + nearest.goal->h() / 2;
        hasTarget = true;
    }
    const int dx = hasTarget ? ainav::HorizontalDistance(tx - (pPlayer->ix + PW / 2), pPlayer->iy, ty) : 0;
    const int dy = hasTarget ? ty - (pPlayer->iy + PH / 2) : 0;
    const bool targetAbove = hasTarget && dy < -40 && dx > -220 && dx < 220;
    const bool fallingIntoDanger = iFallDanger != 0 && pPlayer->vely > 0.0f;
    bool& jump = playerKeys->game_jump.fDown;
    auto freshJump = [&]() { jump = !pPlayer->lockjump; };   // let go first if we're holding it

    // Tanooki: become a statue when someone is about to land on us or something dangerous is close
    if (pPlayer->tanookisuit.isOn()) {
        const bool threatened = AboutToBeStomped() || (actionType == 2 && nearest.threat && nearest.threatdistance < 64 * 64);
        if (pPlayer->tanookisuit.isStatue()) {
            // Stay a statue for as long as it lasts (letting go early measured worse)
            playerKeys->game_down.fDown = true;
            playerKeys->game_left.fDown = playerKeys->game_right.fDown = false;
            return;
        }
        if (threatened && !pPlayer->tail.isInUse()) {
            playerKeys->game_down.fDown = true;
            if (pPlayer->lockfire) {
                playerKeys->game_turbo.fDown = false;   // let go of turbo: the statue needs a fresh press
            } else {
                playerKeys->game_turbo.fDown = true;
                playerKeys->game_turbo.fPressed = true;
                aibench::count(aibench::ItemStatue, pPlayer->globalID);
            }
            return;
        }
    }

    if (!pPlayer->inair)
        return;

    // In a fight we want to come down on them, so we aim above them
    const bool fight = actionType == 0 && hasTarget;
    const int aboveDy = dy - 56;   // to a point a little above their head

    switch (pPlayer->powerup) {
        case 3: // Feather: an extra jump at the top of a jump
            if (pPlayer->extrajumps < game_values.featherjumps && pPlayer->vely > -1.0f) {
                // ... to get above an opponent next to us (not one right over us), to a goal above
                // us, or out of danger
                const bool overThem = fight && dx > -110 && dx < 110 && (dx < -40 || dx > 40) && dy > -70 && dy < 16;
                const bool upToGoal = actionType == 1 && targetAbove;
                if (overThem || upToGoal || fallingIntoDanger) {
                    freshJump();
                    aibench::count(aibench::ItemFeather, pPlayer->globalID);
                }
            }
            break;

        case 7: // Leaf: shake the tail to glide over danger or across to a goal
            if (pPlayer->vely > 1.0f && (fallingIntoDanger || (actionType == 1 && hasTarget && dy > -40 && (dx < -64 || dx > 64)))) {
                freshJump();
                aibench::count(aibench::ItemLeaf, pPlayer->globalID);
            }
            break;

        case 8: // P-Wings: fly over an opponent and dive onto them, up to a goal, or out of danger
            if (pPlayer->flying) {
                jump = true;   // letting go of jump ends the flight
                if (hasTarget) {
                    const int wantDy = fight ? aboveDy : dy;
                    const bool overHead = fight && dx > -16 && dx < 16 && dy > 8;
                    playerKeys->game_down.fDown = !fallingIntoDanger && (overHead || wantDy > 24);
                    playerKeys->game_left.fDown = dx < -8;
                    playerKeys->game_right.fDown = dx > 8;
                }
            } else if (pPlayer->extrajumps == 0 && pPlayer->vely > -2.0f &&
                    (fallingIntoDanger || (hasTarget && (dy < -64 || (fight && (dx < -120 || dx > 120)))))) {
                freshJump();
                aibench::count(aibench::ItemPWings, pPlayer->globalID);
            }
            break;

        default:
            break;
    }
}

// Fire the held weapon when an opponent is in its line of fire, instead of at random moments.
// The weapon fires on the press of the turbo button (CPlayer::useSpecialPowerup), so it is
// released for one frame before a shot.
void CPlayerAI::AimWeapon(COutputControl * playerKeys, short actionType)
{
    const short powerup = pPlayer->powerup;
    if (pPlayer->bobomb || pPlayer->carriedItem || powerup < 1 || powerup > 7)
        return;

    const int px = pPlayer->ix + PW / 2;
    const int py = pPlayer->iy + PH / 2;
    const bool facingRight = pPlayer->isFacingRight();

    bool aligned = false;
    for (CPlayer* other : g_sim.players) {
        if (!other || other == pPlayer || !other->isready() || other->getTeamID() == pPlayer->getTeamID() || other->isInvincible())
            continue;

        const int dx = ainav::HorizontalDistance(other->ix + PW / 2 - px, pPlayer->iy, other->iy);
        const int dy = other->iy + PH / 2 - py;   // > 0: the opponent is lower
        const int adx = abs(dx);
        const bool inFront = dx == 0 || (dx > 0) == facingRight;

        switch (powerup) {
            case 1: // fireballs roll along the floor: same level or a little lower
                aligned = inFront && adx < 260 && dy > -20 && dy < 48;
                break;
            case 5: // ice blast flies straight
                aligned = inFront && adx < 300 && dy > -24 && dy < 24;
                break;
            case 4: // boomerang flies straight and comes back
                aligned = inFront && adx < 320 && dy > -28 && dy < 28;
                break;
            case 2: // hammers arc up and forward
                aligned = inFront && adx > 16 && adx < 180 && dy > -120 && dy < 40;
                break;
            case 6: // bombs are thrown in a short arc
                aligned = inFront && adx > 24 && adx < 170 && dy > -60 && dy < 60;
                break;
            case 3: // cape and tail spin hit right next to us
            case 7:
                aligned = adx < 44 && dy > -30 && dy < 30;
                break;
        }
        if (aligned)
            break;
    }

    // Turbo is also the run button: keep running while we hold fire
    if (aligned)
        playerKeys->game_turbo.fDown = !pPlayer->lockfire;
    else if (actionType != 2)
        playerKeys->game_turbo.fDown = true;
}

/**************************************************
* CSimpleAI class
***************************************************/
//This simple ai makes the player jump every so often
//The jump is to the maximum jump height, then the game_jump.fDown is released
void CSimpleAI::Think(COutputControl * playerKeys)
{
    playerKeys->game_left.fDown = false;
    playerKeys->game_right.fDown = false;

    playerKeys->game_down.fDown = false;
    playerKeys->game_turbo.fDown = false;
    playerKeys->game_powerup.fDown = false;

    if (pPlayer->isdead() || pPlayer->isspawning())
        return;

    //Hold down jump until player starts moving down again, then release jump button
    if (pPlayer->inair) {
        if (pPlayer->vely > 0.0f)
            playerKeys->game_jump.fDown = false;
        else
            playerKeys->game_jump.fDown = true;
    } else {
        //Try to jump 1 out of 50 chances when on ground
        if (aiRandom().getBoolean(50))
            playerKeys->game_jump.fDown = true;
        else
            playerKeys->game_jump.fDown = false;
    }
}

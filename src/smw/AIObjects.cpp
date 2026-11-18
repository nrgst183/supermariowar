#include "AINav.h"

#include "GameValues.h"
#include "movingplatform.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/Race.h"
#include "gamemodes/Star.h"
#include "objects/IO_FlameCannon.h"
#include "objects/blocks/PowerupBlock.h"
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
#include "objects/powerup/PU_PowPowerup.h"
#include "objects/powerup/PU_PWingsPowerup.h"
#include "objects/powerup/PU_SecretPowerup.h"
#include "objects/powerup/PU_StarPowerup.h"
#include "objects/powerup/PU_Tanooki.h"
#include "objects/powerup/PU_TreasureChestBonus.h"

#include "objects/moving/MO_Boomerang.h"
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
#include "objects/carriable/CO_Bomb.h"
#include "objects/carriable/CO_Egg.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Star.h"
#include "objects/carriable/CO_ThrowBox.h"

#include "objects/overmap/WO_Area.h"
#include "objects/overmap/WO_Phanto.h"
#include "objects/overmap/WO_PipeBonus.h"
#include "objects/overmap/WO_PipeCoin.h"
#include "objects/overmap/WO_RaceGoal.h"

#include <memory>
#include <cstdlib> // abs()

void CPlayerAI::GetNearestObjects()
{
    nearestObjects.Reset();

    ScanMiddleLayerObjects();
    ScanBackLayerObjects();
    ScanFrontLayerObjects();
    ScanPlayers();
    ScanPowerupBlocks();

    if (nearestObjects.goal && !nearestObjects.goalIsBlock)
        nearestObjects.goalValue = ItemValue(nearestObjects.goal);
}

short CPlayerAI::ItemValue(const CObject* item) const
{
    const auto* moving = dynamic_cast<const IO_MovingObject*>(item);
    if (!moving || moving->getMovingObjectType() != movingobject_powerup)
        return -1;

    const short have = pPlayer->powerup;
    const bool reserveFree = game_values.gamepowerups[pPlayer->globalID] < 0;
    const GameModeType mode = game_values.gamemode ? static_cast<GameModeType>(game_values.gamemode->gamemode) : game_mode_classic;
    // What a suit or weapon (powerup 1..8) is worth: full if we have none, little if it's ours
    // already (it only fills the reserve), some if it replaces another (that goes to the reserve)
    auto held = [&](short which, short worth) -> short {
        if (have == which)
            return reserveFree ? 20 : 5;
        if (have > 0)
            return static_cast<short>(worth * 2 / 3);
        return worth;
    };
    // The items kept in the reserve until used (clock, POW, MOd, bullet bill, podoboo, shells)
    auto kept = [&](short worth) -> short {
        return reserveFree ? worth : static_cast<short>(worth / 4);
    };

    if (dynamic_cast<const PU_StarPowerup*>(item))
        return pPlayer->isInvincible() ? 15 : 100;
    if (dynamic_cast<const PU_HammerPowerup*>(item))
        return held(2, 75);
    if (dynamic_cast<const PU_FirePowerup*>(item))
        return held(1, 65);
    if (dynamic_cast<const PU_BoomerangPowerup*>(item))
        return held(4, 65);
    if (dynamic_cast<const PU_IceWandPowerup*>(item))
        return held(5, 60);
    if (dynamic_cast<const PU_BombPowerup*>(item))
        return held(6, 60);
    if (dynamic_cast<const PU_LeafPowerup*>(item))      //(a feather too: first)
        return held(7, 55);
    if (dynamic_cast<const PU_FeatherPowerup*>(item))
        return held(3, 55);
    if (dynamic_cast<const PU_PWingsPowerup*>(item))
        return held(8, 60);
    if (dynamic_cast<const PU_Tanooki*>(item))
        return pPlayer->tanookisuit.isOn() ? kept(20) : 65;
    if (dynamic_cast<const PU_BobombPowerup*>(item))
        return pPlayer->bobomb ? kept(20) : 55;
    if (dynamic_cast<const PU_ClockPowerup*>(item) || dynamic_cast<const PU_PowPowerup*>(item))
        return kept(70);
    if (dynamic_cast<const PU_ModPowerup*>(item) || dynamic_cast<const PU_BulletBillPowerup*>(item))
        return kept(65);
    if (dynamic_cast<const PU_PodoboPowerup*>(item))
        return kept(55);
    if (dynamic_cast<const PU_ExtraGuyPowerup*>(item))
        return 50;
    if (dynamic_cast<const PU_ExtraHeartPowerup*>(item))
        return mode == game_mode_health ? 80 : 5;
    if (dynamic_cast<const PU_ExtraTimePowerup*>(item))
        return mode == game_mode_timelimit || mode == game_mode_star ? 40 : 5;
    if (dynamic_cast<const PU_CoinPowerup*>(item))
        return mode == game_mode_coins || mode == game_mode_greed ? 85 : 5;
    if (dynamic_cast<const PU_JailKeyPowerup*>(item))
        return pPlayer->jail.isActive() ? 95 : (reserveFree && mode == game_mode_jail ? 40 : 10);
    if (dynamic_cast<const PU_MysteryMushroomPowerup*>(item))
        return 25;
    if (dynamic_cast<const PU_SecretPowerup*>(item))
        return 0;
    if (dynamic_cast<const PU_TreasureChestBonus*>(item))
        return 60;
    return 40;
}

void CPlayerAI::ItemAimPoint(const CObject* item, int& x, int& y) const
{
    x = item->x() + item->w() / 2;
    y = item->y() + item->h() / 2;
    if (const auto* feather = dynamic_cast<const PU_FeatherPowerup*>(item)) {
        // It rises out of its block, then sways 45 pixels either side of a middle while sinking:
        // under the middle we're where it comes back to
        x = static_cast<int>(feather->swayCenterX()) + item->w() / 2;
        return;
    }
    const auto* moving = dynamic_cast<const IO_MovingObject*>(item);
    if (!moving || moving->velX() == 0.0f)
        return;
    // Sliding along (a star, a 1-up, a mystery mushroom): where it'll be when we get there, at
    // our running speed (not further ahead than a second: it bounces off walls)
    int dx = ainav::HorizontalDistance(x - (pPlayer->ix + PW / 2), pPlayer->iy, y);
    const float closing = VELMOVING + ((dx > 0) == (moving->velX() < 0.0f) ? std::abs(moving->velX()) : -std::abs(moving->velX()) * 0.5f);
    int ticks = static_cast<int>(std::abs(dx) / std::max(closing, 1.0f));
    if (ticks > 60)
        ticks = 60;
    x += static_cast<int>(moving->velX() * static_cast<float>(ticks));
    x = ((x % App::screenWidth) + App::screenWidth) % App::screenWidth;
}

// GetNearestObjects(): the middle layer's objects (most items and enemies)
void CPlayerAI::ScanMiddleLayerObjects()
{
    MO_CarriedObject * carriedItem = pPlayer->carriedItem;
    bool fInvincible = pPlayer->isInvincible() || pPlayer->isShielded() || pPlayer->shyguy;
    short iTeamID = pPlayer->teamID;
    std::map<int, AttentionObject*>::iterator lim = attentionObjects.end();

    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
        if (attentionObjects.find(obj->networkId()) != lim) {
            continue;
        }

        ObjectType type = obj->getObjectType();

        switch (type) {
        case object_moving: {
            IO_MovingObject * movingobject = static_cast<IO_MovingObject*>(obj.get());
            MovingObjectType movingtype = movingobject->getMovingObjectType();

            if (carriedItem == movingobject)
                continue;

            if (movingobject_shell == movingtype) {
                CO_Shell * shell = static_cast<CO_Shell*>(movingobject);

                if (shell->IsThreat()) {
                    if (fInvincible)
                        continue;

                    DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                } else if (carriedItem) {
                    continue;
                } else {
                    DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
                }
            } else if (movingobject_throwblock == movingtype || (movingobject_throwbox == movingtype && (static_cast<CO_ThrowBox*>(movingobject))->HasKillVelocity())) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_pirhanaplant == movingtype) {
                MO_PirhanaPlant * plant = static_cast<MO_PirhanaPlant*>(movingobject);
                if (plant->state > 0)
                    DistanceToObjectCenter(plant, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_flag == movingtype) {
                CO_Flag * flag = static_cast<CO_Flag*>(movingobject);

                if (flag->GetInBase() && flag->GetTeamID() == iTeamID)
                    continue;

                if (carriedItem && carriedItem->getMovingObjectType() == movingobject_flag)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_yoshi == movingtype) {
                MO_Yoshi * yoshi = static_cast<MO_Yoshi*>(movingobject);

                if (!carriedItem || carriedItem->getMovingObjectType() != movingobject_egg)
                    continue;

                CO_Egg * egg = static_cast<CO_Egg*>(carriedItem);

                if (yoshi->getColor() != egg->getColor())
                    continue;

                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_egg == movingtype) {
                if (carriedItem && carriedItem->getMovingObjectType() == movingobject_egg)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_throwbox == movingtype) {
                if (carriedItem)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_star == movingtype) {
                if (carriedItem && carriedItem->getMovingObjectType() == movingobject_star)
                    continue;

                CGM_Star * starmode = static_cast<CGM_Star*>(game_values.gamemode);
                CO_Star * star = static_cast<CO_Star*>(movingobject);

                if (star->getType() == 1 || starmode->isplayerstar(pPlayer)) {
                    DistanceToObject(star, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
                } else {
                    DistanceToObject(star, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                }
            } else if (movingobject_coin == movingtype || movingobject_phantokey == movingtype) {
                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_collectioncard == movingtype) {
                int iNumHeldCards = pPlayer->score->subscore[0];
                int iHeldCards = pPlayer->score->subscore[1];

                if (iNumHeldCards == 3) {
                    int iThreeHeldCards = iHeldCards & 63;

                    //If bot is holding all of the same type of card, then ignore all other cards
                    if (iThreeHeldCards == 42 || iThreeHeldCards == 21 || iThreeHeldCards == 0) {
                        DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                        break;
                    }

                    int iTwoHeldCards = iHeldCards & 15;
                    if (iTwoHeldCards == 10 || iTwoHeldCards == 5 || iTwoHeldCards == 0) {
                        DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
                        break;
                    }
                } else {
                    MO_CollectionCard * card = static_cast<MO_CollectionCard*>(movingobject);

                    if (iNumHeldCards == 2) {
                        int iTwoHeldCards = iHeldCards & 15;

                        if (iTwoHeldCards == 10 || iTwoHeldCards == 5 || iTwoHeldCards == 0) {
                            int iCardValue = card->getValue();
                            if (card->getType() == 1 && ((iTwoHeldCards == 10 && iCardValue != 2) || (iTwoHeldCards == 5 && iCardValue != 1) || (iTwoHeldCards == 0 && iCardValue != 0))) {
                                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                                break;
                            }
                        }
                    } else if (iNumHeldCards == 1) {
                        int iHeldCard = iHeldCards & 3;
                        int iCardValue = card->getValue();
                        if (card->getType() == 1 || iHeldCard != iCardValue) {
                            DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                            break;
                        }
                    }
                }

                if (iNumHeldCards == 3)
                    DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                else
                    DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            }

            break;
        }
        case object_frenzycard: {
            DistanceToObject(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }

        case object_pipe_coin: {
            OMO_PipeCoin * coin = static_cast<OMO_PipeCoin*>(obj.get());

            if (coin->GetColor() != 0) {
                if (coin->GetTeam() == -1 || coin->GetTeam() == pPlayer->teamID)
                    DistanceToObject(coin, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else {
                DistanceToObject(coin, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            }

            break;
        }

        case object_pipe_bonus: {
            OMO_PipeBonus * bonus = static_cast<OMO_PipeBonus*>(obj.get());

            if (bonus->GetType() != 5) {
                DistanceToObject(bonus, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else {
                DistanceToObject(bonus, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            }

            break;
        }

        case object_pathhazard:
        case object_orbithazard: {
            DistanceToObject(obj.get(), &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            break;
        }

        case object_phanto: {
            OMO_Phanto * phanto = static_cast<OMO_Phanto*>(obj.get());

            if (phanto->GetType() == 2 || (carriedItem && carriedItem->getMovingObjectType() == movingobject_phantokey)) {
                DistanceToObjectCenter(obj.get(), &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            }

            break;
        }

        case object_flamecannon: {
            IO_FlameCannon * cannon = static_cast<IO_FlameCannon*>(obj.get());

            if (cannon->state > 0)
                DistanceToObject(cannon, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);

            break;
        }

        default: {
            DistanceToObject(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }
        }
    }
}

// GetNearestObjects(): the back layer's objects
void CPlayerAI::ScanBackLayerObjects()
{
    MO_CarriedObject * carriedItem = pPlayer->carriedItem;
    bool fInvincible = pPlayer->isInvincible() || pPlayer->isShielded() || pPlayer->shyguy;
    short iTeamID = pPlayer->teamID;
    std::map<int, AttentionObject*>::iterator lim = attentionObjects.end();

    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[0].list()) {
        if (attentionObjects.find(obj->networkId()) != lim) {
            continue;
        }

        ObjectType type = obj->getObjectType();

        switch (type) {
        case object_moving: {
            IO_MovingObject * movingobject = static_cast<IO_MovingObject*>(obj.get());
            MovingObjectType movingtype = movingobject->getMovingObjectType();

            if (carriedItem == movingobject)
                continue;

            if (movingobject_powerup == movingtype) {
                if (!improved()) {
                    DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
                } else {
                    // As far away as it is, but further the less it's worth to us now (a weapon we
                    // have already twice as far as a star): never nearer than it is, which would put
                    // it ahead of the game's goals (flags, coins, zones). None we're better off without.
                    const short value = ItemValue(movingobject);
                    if (value <= 0)
                        continue;
                    const int weight = std::clamp(10000 / value, 100, 400);
                    DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap, weight);
                }
            } else if ((movingobject_fireball == movingtype && (static_cast<MO_Fireball*>(movingobject))->iTeamID != iTeamID)
                      || movingobject_poisonpowerup == movingtype) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if ((movingobject_goomba == movingtype || movingobject_koopa == movingtype) && movingobject->GetState() == 1) {
                DistanceToObject(movingobject, &nearestObjects.stomp, &nearestObjects.stompdistance, &nearestObjects.stompwrap);
            } else if (movingobject_sledgebrother == movingtype) {
                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_treasurechest == movingtype) {
                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else if (movingobject_flagbase == movingtype) {
                MO_FlagBase * flagbase = static_cast<MO_FlagBase*>(movingobject);

                if (!carriedItem || carriedItem->getMovingObjectType() != movingobject_flag || flagbase->GetTeamID() != iTeamID)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            } else {
                continue;
            }
            break;
        }

        case object_area: {
            if ((static_cast<OMO_Area*>(obj.get()))->getColorID() == pPlayer->colorID)
                continue;

            DistanceToObject(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }

        case object_kingofthehill_area: {
            DistanceToObjectCenter(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }

        default: {
            DistanceToObject(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }
        }
    }
}

// GetNearestObjects(): the front layer's objects
void CPlayerAI::ScanFrontLayerObjects()
{
    MO_CarriedObject * carriedItem = pPlayer->carriedItem;
    bool fInvincible = pPlayer->isInvincible() || pPlayer->isShielded() || pPlayer->shyguy;
    short iTeamID = pPlayer->teamID;
    std::map<int, AttentionObject*>::iterator lim = attentionObjects.end();

    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[2].list()) {
        if (attentionObjects.find(obj->networkId()) != lim) {
            continue;
        }

        ObjectType type = obj->getObjectType();

        switch (type) {
        case object_moving: {
            IO_MovingObject * movingobject = static_cast<IO_MovingObject*>(obj.get());
            MovingObjectType movingtype = movingobject->getMovingObjectType();

            if (carriedItem == movingobject)
                continue;

            if (movingobject_cheepcheep == movingtype) {
                DistanceToObject(movingobject, &nearestObjects.stomp, &nearestObjects.stompdistance, &nearestObjects.stompwrap);
            } else if (movingobject_bulletbill == movingtype) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_hammer == movingtype && (static_cast<MO_Hammer*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_iceblast == movingtype && (static_cast<MO_IceBlast*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_boomerang == movingtype && (static_cast<MO_Boomerang*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_bomb == movingtype && (static_cast<CO_Bomb*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_podobo == movingtype && (static_cast<MO_Podobo*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObject(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            } else if (movingobject_explosion == movingtype && (static_cast<MO_Explosion*>(movingobject))->iTeamID != iTeamID) {
                if (fInvincible)
                    continue;

                DistanceToObjectCenter(movingobject, &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
                break;
            }

            break;
        }
        case object_thwomp:
        case object_bowserfire: {
            if (fInvincible)
                continue;

            DistanceToObjectCenter(obj.get(), &nearestObjects.threat, &nearestObjects.threatdistance, &nearestObjects.threatwrap);
            break;
        }
        case object_race_goal: {
            if (game_values.gamemode->gamemode != game_mode_race)
                continue;

            OMO_RaceGoal * racegoal = static_cast<OMO_RaceGoal*>(obj.get());

            if (racegoal->getGoalID() != (static_cast<CGM_Race*>(game_values.gamemode))->getNextGoal(iTeamID))
                continue;

            DistanceToObject(racegoal, &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }

        default: {
            DistanceToObject(obj.get(), &nearestObjects.goal, &nearestObjects.goaldistance, &nearestObjects.goalwrap);
            break;
        }
        }
    }
}

// GetNearestObjects(): the other players
void CPlayerAI::ScanPlayers()
{
    short iTeamID = pPlayer->teamID;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);

    //Figure out where the other players are
    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
        if (iPlayer == static_cast<size_t>(pPlayer->localID) || g_sim.players[iPlayer]->state != PlayerState::Ready)
            continue;

        //Find players in jail on own team to tag
        if (game_values.gamemode->gamemode == game_mode_jail) {
            if (g_sim.players[iPlayer]->jail.isActive() && g_sim.players[iPlayer]->teamID == iTeamID) {
                DistanceToPlayer(g_sim.players[iPlayer], &nearestObjects.teammate, &nearestObjects.teammatedistance, &nearestObjects.teammatewrap);
            }
        }

        if (g_sim.players[iPlayer]->teamID == iTeamID || g_sim.players[iPlayer]->state != PlayerState::Ready)
            continue;

        //If there is a chicken, only focus on stomping him
        if (gmChicken && gmChicken->chicken()) {
            if (gmChicken->chicken()->teamID != iTeamID && gmChicken->chicken() != g_sim.players[iPlayer])
                continue;
        }

        DistanceToPlayer(g_sim.players[iPlayer], &nearestObjects.player, &nearestObjects.playerdistance, &nearestObjects.playerwrap);
    }
}

// GetNearestObjects(): the ? blocks (improved CPUs)
void CPlayerAI::ScanPowerupBlocks()
{
    std::map<int, AttentionObject*>::iterator lim = attentionObjects.end();

    //The ? blocks ready to give a power-up: a goal too (hit from the spot under them), a little
    //ahead of other things as far away: the power-ups are what the bots were missing out on
    if (improved() && g_map) {
        for (short c = 0; c < MAPWIDTH; c++) {
            for (short r = 0; r < MAPHEIGHT; r++) {
                IO_Block* b = g_map->block(c, r);
                if (!b || b->GetState() != 0 || b->isHidden() || !dynamic_cast<B_PowerupBlock*>(b))
                    continue;
                if (attentionObjects.find(b->networkId()) != lim)
                    continue;
                const int spotRow = ainav::SpotBelow(c, r);
                if (spotRow < 0)
                    continue;
                short tx = static_cast<short>(c * TILESIZE + TILESIZE / 2 - (pPlayer->ix + PW / 2));
                const short ty = static_cast<short>(spotRow * TILESIZE + TILESIZE / 2 - (pPlayer->iy + PH / 2));
                bool wrap = false;
                if ((tx > App::screenWidth / 2 || tx < -App::screenWidth / 2) && ainav::CanCrossEdge(pPlayer->iy, pPlayer->iy + ty)) {
                    tx = tx > 0 ? App::screenWidth - tx : App::screenWidth + tx;
                    wrap = true;
                }
                const int distance = (tx * tx + ty * ty) * 9 / 16;
                if (distance < nearestObjects.goaldistance) {
                    nearestObjects.goal = b;
                    nearestObjects.goaldistance = distance;
                    nearestObjects.goalwrap = wrap;
                    nearestObjects.goalIsBlock = true;
                    nearestObjects.goalSpotRow = spotRow;
                }
            }
        }
    }
}

void CPlayerAI::DistanceToObject(CObject * object, CObject ** target, int * nearest, bool * wrap, int weightPct)
{
    //Calculate normal screen
    short tx = object->x() - pPlayer->ix;
    short ty = object->y() - pPlayer->iy;
    bool fScreenWrap = false;

    //See if it is a shorter distance wrapping around the screen (and whether the map lets us)
    if (tx > App::screenWidth/2 || tx < -App::screenWidth/2) {
        if (!improved() || ainav::CanCrossEdge(pPlayer->iy, pPlayer->iy + ty)) {
            tx = tx > 0 ? App::screenWidth - tx : App::screenWidth + tx;
            fScreenWrap = true;
        }
    }

    int distance_player_pow2 = tx*tx + ty*ty;    // a^2 = b^2 + c^2 :)
    if (weightPct != 100)
        distance_player_pow2 = static_cast<int>(static_cast<long long>(distance_player_pow2) * weightPct / 100);

    if (distance_player_pow2 < *nearest) {
        *target = object;
        *nearest = distance_player_pow2;
        *wrap = fScreenWrap;
    }
}

void CPlayerAI::DistanceToObjectCenter(CObject * object, CObject ** target, int * nearest, bool * wrap)
{
    //Calculate normal screen
    short tx = object->x() + (object->collisionRectW() / 2) - pPlayer->ix - HALFPW;
    short ty = object->y() + (object->collisionRectH() / 2) - pPlayer->iy - HALFPH;
    bool fScreenWrap = false;

    //See if it is a shorter distance wrapping around the screen (and whether the map lets us)
    if (tx > App::screenWidth/2 || tx < -App::screenWidth/2) {
        if (!improved() || ainav::CanCrossEdge(pPlayer->iy, pPlayer->iy + ty)) {
            tx = tx > 0 ? App::screenWidth - tx : App::screenWidth + tx;
            fScreenWrap = true;
        }
    }

    int distance_player_pow2 = tx*tx + ty*ty;

    if (distance_player_pow2 < *nearest) {
        *target = object;
        *nearest = distance_player_pow2;
        *wrap = fScreenWrap;
    }
}

void CPlayerAI::DistanceToPlayer(CPlayer * player, CPlayer ** target, int * nearest, bool * wrap)
{
    //Calculate normal screen
    short tx = player->ix - pPlayer->ix;
    short ty = player->iy - pPlayer->iy;
    bool fScreenWrap = false;

    //See if it is a shorter distance wrapping around the screen (and whether the map lets us)
    if (tx > App::screenWidth/2 || tx < -App::screenWidth/2) {
        if (!improved() || ainav::CanCrossEdge(pPlayer->iy, pPlayer->iy + ty)) {
            tx = tx > 0 ? App::screenWidth - tx : App::screenWidth + tx;
            fScreenWrap = true;
        }
    }

    int distance_player_pow2 = tx*tx + ty*ty;

    if (distance_player_pow2 < *nearest) {
        *target = player;
        *nearest = distance_player_pow2;
        *wrap = fScreenWrap;
    }
}

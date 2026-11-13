#include "player.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "map.h"
#include "movingplatform.h"
#include "net.h"
#include "ObjectContainer.h"
#include "objectgame.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/ShyGuyTag.h"
#include "gamemodes/Star.h"
#include "gamemodes/Tag.h"
#include "IO_Block.h"
#include "objects/carriable/CO_Bomb.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "objects/moving/MO_Boomerang.h"
#include "objects/moving/MO_Explosion.h"
#include "objects/moving/MO_Fireball.h"
#include "objects/moving/MO_Hammer.h"
#include "objects/moving/MO_IceBlast.h"
#include "objects/moving/MO_Podobo.h"

#include <cassert>
#include <cmath>

void CPlayer::AddKillerAward(CPlayer* killed, KillStyle style) {
    awardeffects.addKillerAward(*this, killed, style);
}

void CPlayer::DeathAwards() {
    awardeffects.addDeathAward(*this);
}

void CPlayer::AddKillsInRowInAirAward() {
    awardeffects.addKillsInRowInAirAward(*this);
}

PlayerKillType PlayerKilledPlayer(short iKiller, CPlayer * killed, PlayerDeathStyle deathstyle, KillStyle style, bool fForce, bool fKillCarriedItem)
{
    CPlayer * killer = GetPlayerFromGlobalID(iKiller);

    if (killer && killer->globalID != killed->globalID) {
        return killer->KilledPlayer(killed, deathstyle, style, fForce, fKillCarriedItem);
    } else {
        killed->DeathAwards();

        PlayerKillType iKillType = game_values.gamemode->playerkilledself(*killed, style);

        if (PlayerKillType::Normal == iKillType || (fForce && PlayerKillType::NonKill == iKillType))
            killed->die(PlayerDeathStyle::Jump, false, fKillCarriedItem);

        if (PlayerKillType::NonKill != iKillType) {
            if (deathstyle == PlayerDeathStyle::Shatter)
                ifSoundOnPlay(rm->sfx_breakblock);
            else
                ifSoundOnPlay(rm->sfx_deathsound);
        }

        return iKillType;
    }
}

PlayerKillType CPlayer::KilledPlayer(CPlayer * killed, PlayerDeathStyle deathstyle, KillStyle style, bool fForce, bool fKillCarriedItem)
{
    CPlayer * killer = this;

    //If this player is already dead, then don't kill him again
    if (killed->state != PlayerState::Ready)
        return PlayerKillType::None;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);
    if (gmChicken && gmChicken->chicken() == killer && style != KillStyle::Pow)
        ifSoundOnPlay(rm->sfx_chicken);

    if (killed->frozen)
        deathstyle = PlayerDeathStyle::Shatter;

    if (killer->teamID != killed->teamID)
        killer->AddKillerAward(killed, style);

    if (game_values.awardstyle != AwardStyle::None)
        killed->DeathAwards();

    //now kill the player (don't call this function earlier because we need the old position, etc.
    PlayerKillType iKillType = game_values.gamemode->playerkilledplayer(*killer, *killed, style);

    if (PlayerKillType::NonKill != iKillType || fForce) {
        if (killed->bobomb) {
            killed->diedas = 2;
            killer->SetPowerup(0);
        }

        if (deathstyle == PlayerDeathStyle::Jump)
            ifSoundOnPlay(rm->sfx_deathsound);
        else if (deathstyle == PlayerDeathStyle::Squish)
            ifSoundOnPlay(rm->sfx_mip);
        else if (deathstyle == PlayerDeathStyle::Shatter)
            ifSoundOnPlay(rm->sfx_breakblock);
    }

    if (PlayerKillType::Normal == iKillType || (fForce && PlayerKillType::NonKill == iKillType))
        killed->die(deathstyle, false, fKillCarriedItem);

    return iKillType;
}


void CPlayer::TransferTag(CPlayer* o2)
{
    CPlayer * o1 = this;

    auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode);
    if (!gmTag)
        return;

    if (!o1->isready() || !o2->isready())
        return;

    if (gmTag->tagged() == o1 && !o2->isShielded() && !o2->isInvincible()) {
        gmTag->setTagged(o2);
        o1->shield.turn_on();
        eyecandy[2].emplace<EC_GravText>(&rm->game_font_large, gmTag->tagged()->ix + HALFPW, gmTag->tagged()->iy + PH, "Tagged!", -VELJUMP*1.5);
        eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, gmTag->tagged()->ix + HALFPW - 16, gmTag->tagged()->iy + HALFPH - 16, 3, 8);
        ifSoundOnPlay(rm->sfx_transform);
    } else if (gmTag->tagged() == o2 && !o1->isShielded() && !o1->isInvincible()) {
        gmTag->setTagged(o1);
        o2->shield.turn_on();
        eyecandy[2].emplace<EC_GravText>(&rm->game_font_large, gmTag->tagged()->ix + HALFPW, gmTag->tagged()->iy + PH, "Tagged!", -VELJUMP*1.5);
        eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, gmTag->tagged()->ix + HALFPW - 16, gmTag->tagged()->iy + HALFPH - 16, 3, 8);
        ifSoundOnPlay(rm->sfx_transform);
    }
}

void CPlayer::TransferShyGuy(CPlayer* o2)
{
    CPlayer * o1 = this;

    //Don't shyguy tag if this isn't shyguy tag mode or if tag transfers is set to kills only
    if (game_values.gamemode->gamemode != game_mode_shyguytag || game_values.gamemodesettings.shyguytag.tagtransfer == 1)
        return;

    if (!o1->isready() || !o2->isready())
        return;

    CGM_ShyGuyTag * sgt = static_cast<CGM_ShyGuyTag*>(game_values.gamemode);

    if (o1->shyguy && !o2->shyguy && !o2->isShielded() && !o2->isInvincible()) {
        sgt->SetShyGuy(o2->getTeamID());
    } else if (o2->shyguy && !o1->shyguy && !o1->isShielded() && !o1->isInvincible()) {
        sgt->SetShyGuy(o1->getTeamID());
    }
}

void CPlayer::BounceAssistPlayer(CPlayer* o2)
{
    CPlayer * o1 = this;

    if (o1->state == PlayerState::Ready && o1->fOldY + PH <= o2->fOldY && o1->iy + PH >= o2->iy && o1->playerKeys->game_jump.fDown) {
        o1->setYi(o2->iy - PH);		//set new position to top of other player
        o1->collisions.checktop(*o1);
        o1->platform = nullptr;
        o1->vely = -VELSUPERJUMP;

        o1->superstomp.reset();

        ifSoundOnPlay(rm->sfx_superspring);
    }
}

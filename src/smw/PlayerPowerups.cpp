#include "player.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "map.h"
#include "movingplatform.h"
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

void CPlayer::triggerPowerup()
{
    switch (powerupused.value()) {
    case PowerupType::PoisonMushroom: {
        break;
    }
    case PowerupType::ExtraLife1: {
        game_values.gamemode->playerextraguy(*this, 1);
        ifSoundOnPlay(rm->sfx_extraguysound);
        break;
    }
    case PowerupType::ExtraLife2: {
        game_values.gamemode->playerextraguy(*this, 2);
        ifSoundOnPlay(rm->sfx_extraguysound);
        break;
    }
    case PowerupType::ExtraLife3: {
        game_values.gamemode->playerextraguy(*this, 3);
        ifSoundOnPlay(rm->sfx_extraguysound);
        break;
    }
    case PowerupType::ExtraLife5: {
        game_values.gamemode->playerextraguy(*this, 5);
        ifSoundOnPlay(rm->sfx_extraguysound);
        break;
    }
    case PowerupType::Fire: {
        powerup = -1;
        SetPowerup(1);
        break;
    }
    case PowerupType::Star: {
        invincibility.turn_on(*this);
        break;
    }
    case PowerupType::Clock: {
        turnslowdownon();
        outofarena.reset();
        break;
    }
    case PowerupType::Bobomb: {
        powerup = -1;
        bobomb = false;
        SetPowerup(0);
        break;
    }
    case PowerupType::Pow: {
        ifSoundOnPlay(rm->sfx_thunder);
        game_values.flags.screenshaketimer = 20;
        game_values.flags.screenshakeplayerid = globalID;
        game_values.flags.screenshaketeamid = teamID;
        game_values.flags.screenshakekillinair = false;
        game_values.flags.screenshakekillscount = 0;
        break;
    }
    case PowerupType::BulletBill: {
        game_values.bulletbilltimer[globalID] = 400;
        game_values.bulletbillspawntimer[globalID] = 0;
        break;
    }
    case PowerupType::Hammer: {
        powerup = -1;
        SetPowerup(2);
        break;
    }
    case PowerupType::ShellGreen: {
        CO_Shell * shell = new CO_Shell(ShellType::Green, Vec2s::zero(), true, true, true, false);
        if (g_sim.objectcontainer[1].add(shell))
            shell->UsedAsStoredPowerup(this);
        break;
    }
    case PowerupType::ShellRed: {
        CO_Shell * shell = new CO_Shell(ShellType::Red, Vec2s::zero(), false, true, true, false);
        if (g_sim.objectcontainer[1].add(shell))
            shell->UsedAsStoredPowerup(this);
        break;
    }
    case PowerupType::ShellSpiny: {
        CO_Shell * shell = new CO_Shell(ShellType::Spiny, Vec2s::zero(), false, false, true, true);
        if (g_sim.objectcontainer[1].add(shell))
            shell->UsedAsStoredPowerup(this);
        break;
    }
    case PowerupType::ShellBuzzy: {
        CO_Shell * shell = new CO_Shell(ShellType::Buzzy, Vec2s::zero(), false, true, false, false);
        if (g_sim.objectcontainer[1].add(shell))
            shell->UsedAsStoredPowerup(this);
        break;
    }
    case PowerupType::Mod: {
        ifSoundOnPlay(rm->sfx_thunder);
        game_values.flags.screenshaketimer = 20;
        game_values.flags.screenshakeplayerid = globalID;
        game_values.flags.screenshaketeamid = teamID;
        game_values.flags.screenshakekillinair = true;
        break;
    }
    case PowerupType::Feather: {
        powerup = -1;
        SetPowerup(3);
        break;
    }
    case PowerupType::MysteryMushroom: {
        SwapPlayers(localID);
        break;
    }
    case PowerupType::Boomerang: {
        powerup = -1;
        SetPowerup(4);
        break;
    }
    case PowerupType::Tanooki: {
        tanookisuit.onPickup();
        break;
    }
    case PowerupType::IceWand: {
        powerup = -1;
        SetPowerup(5);
        break;
    }
    case PowerupType::Podobo: {
        short numPodobos = RANDOM_INT(6) + 10;
        for (short iPodobo = 0; iPodobo < numPodobos; iPodobo++) {
            // Draw random numbers in separate statements: argument evaluation order is
            // unspecified in C++, and every peer must draw them in the same order
            const short podoboX = static_cast<short>(RANDOM_INT(App::screenWidth * 0.95f));
            const float podoboVelY = -(float(RANDOM_INT(9)) / 2.0f) - 9.0f;
            g_sim.objectcontainer[2].add(new MO_Podobo(&rm->spr_podobo, {podoboX, App::screenHeight}, podoboVelY, globalID, teamID, colorID, false));
        }
        ifSoundOnPlay(rm->sfx_thunder);
        break;
    }
    case PowerupType::Bomb: {
        powerup = -1;
        SetPowerup(6);
        break;
    }
    case PowerupType::Leaf: {
        powerup = -1;
        SetPowerup(7);
        break;
    }
    case PowerupType::PWings: {
        powerup = -1;
        SetPowerup(8);
        break;
    }
    case PowerupType::JailKey: {
        jail.escape(*this);
        break;
    }
    }

    powerupused.reset();
}

void CPlayer::update_usePowerup()
{
    assert(powerupused.has_value());

    powerupradius -= static_cast<float>(game_values.storedpowerupdelay) / 2.0f;
    powerupangle += 0.05f;

    if (powerupradius < 0.0f)
        triggerPowerup();
}

PlayerPalette CPlayer::getPlayerPalette() const
{
    if (isInvincible()) {
        return invincibility.getPlayerPalette();
    }
    if (game_values.gamemode->gamemode == game_mode_star) {
        CGM_Star * starmode = static_cast<CGM_Star*>(game_values.gamemode);
        StarStyle starmodetype = starmode->getcurrentmodetype();
        if (starmodetype != StarStyle::Multi && starmode->isplayerstar(this)) {
            return (starmodetype == StarStyle::Ztar) ? PlayerPalette::ztarred : PlayerPalette::got_shine;
        }
    }
    if (auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode)) {
        if (gmTag->tagged() == this) {
            return PlayerPalette::tagged;
        }
    }
    if (frozen) {
        return PlayerPalette::frozen;
    }
    if (isShielded()) {
        return PlayerPalette::shielded;
    }
    return PlayerPalette::normal;
}

void CPlayer::tryReleasingPowerup()
{
    if (tanookisuit.isStatue() || game_values.gamemode->gamemode == game_mode_bonus)
        return;

    // Don't allow usage of the poison powerup, it sticks with you and don't allow shyguys to use powerups
    if (game_values.gamepowerups[globalID] <= 0 || shyguy)
        return;

    // Don't allow releasing another powerup when you're in the middle of releasing one
    if (powerupused.has_value())
        return;

    powerupused = static_cast<PowerupType>(game_values.gamepowerups[globalID]);
    game_values.gamepowerups[globalID] = -1;

    powerupradius = 100.0f;
    powerupangle = static_cast<float>(RANDOM_INT(1000) * 0.00628f);

    ifSoundOnPlay(rm->sfx_storedpowerupsound);
}

void CPlayer::useSpecialPowerup()
{
    fAcceptingItem = carriedItem == nullptr;

    if (!lockfire) {
        if (bobomb) { //If we're a bob-omb, explode
            action = PlayerAction::Bobomb;
        } else {
            if (powerup == 1 && projectiles < 2) {
                if (game_values.fireballlimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::Fireball;
                }
            } else if (powerup == 2 && projectiles < 2 && hammertimer == 0) {
                if (game_values.hammerlimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::Hammer;
                }
            } else if (powerup == 3 && !spin.isSpinInProgress() && !kuriboshoe.is_on()) {
                if (game_values.featherlimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::SpinCape;
                }
            } else if (powerup == 4 && projectiles < 1) { //only allow one boomerang
                if (game_values.boomeranglimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::Boomerang;
                }
            } else if (powerup == 5 && projectiles < 1) {
                if (game_values.wandlimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::Iceblast;
                }
            } else if (powerup == 6 && projectiles < 2 && hammertimer == 0) {
                if (game_values.bombslimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::Bomb;
                }
            } else if (powerup == 7 && !spin.isSpinInProgress() && !kuriboshoe.is_on()) { //Racoon tail spin
                if (game_values.leaflimit == 0 || projectilelimit > 0) {
                    action = PlayerAction::SpinTail;
                }
            }
        }

        lockfire = true;
    }
}

void CPlayer::releaseCarriedItem()
{
    lockfire = false;
    fAcceptingItem = false;

    if (carriedItem) {
        if (playerKeys->game_down.fDown)
            carriedItem->Drop();
        else {
            //Make sure the owner of the object we are kicking is this player
            carriedItem->owner = this;

            //Make sure the shell/block is out in front of player before kicking it
            if (carriedItem->getMovingObjectType() == movingobject_shell || carriedItem->getMovingObjectType() == movingobject_throwblock)
                carriedItem->MoveToOwner();

            carriedItem->Kick();
        }

        carriedItem = nullptr;
    }
}

void CPlayer::CommitAction()
{
    if (PlayerAction::Bobomb == action) {
        bobomb = false;
        g_sim.objectcontainer[2].add(new MO_Explosion(&rm->spr_explosion, {static_cast<short>(ix + HALFPW - 96), static_cast<short>(iy + HALFPH - 64)}, 2, 4, globalID, teamID, KillStyle::Bobomb));
        ifSoundOnPlay(rm->sfx_bobombsound);
    } else if (PlayerAction::Fireball == action) {
        g_sim.objectcontainer[0].add(new MO_Fireball(&rm->spr_fireball, {static_cast<short>(ix + 6), iy}, 4, isFacingRight(), 5, globalID, teamID, colorID));
        ifSoundOnPlay(rm->sfx_fireball);

        projectiles++;

        if (game_values.fireballlimit > 0)
            DecreaseProjectileLimit();
    } else if (PlayerAction::Hammer == action) {
        if (isFacingRight()) {
            g_sim.objectcontainer[2].add(new MO_Hammer(&rm->spr_hammer, {static_cast<short>(ix + 8), iy}, 6, {(game_values.reversewalk ? -velx : velx) + 2.0f, -HAMMERTHROW}, 5, globalID, teamID, colorID, false));
        } else {
            g_sim.objectcontainer[2].add(new MO_Hammer(&rm->spr_hammer, {static_cast<short>(ix - 14), iy}, 6, {(game_values.reversewalk ? -velx : velx) - 2.0f, -HAMMERTHROW}, 5, globalID, teamID, colorID, false));
        }
        projectiles++;

        hammertimer = game_values.hammerdelay;
        ifSoundOnPlay(rm->sfx_fireball);

        if (game_values.hammerlimit > 0)
            DecreaseProjectileLimit();
    } else if (PlayerAction::Boomerang == action) {
        g_sim.objectcontainer[2].add(new MO_Boomerang(&rm->spr_boomerang, {ix, static_cast<short>(iy + HALFPH - 16)}, 4, isFacingRight(), 5, globalID, teamID, colorID));
        projectiles++;

        if (game_values.boomeranglimit > 0)
            DecreaseProjectileLimit();
    } else if (PlayerAction::Iceblast == action) {
        if (isFacingRight())
            g_sim.objectcontainer[2].add(new MO_IceBlast(&rm->spr_iceblast, {static_cast<short>(ix + HALFPW - 2), static_cast<short>(iy + HALFPH - 16)}, 5.0f, globalID, teamID, colorID));
        else
            g_sim.objectcontainer[2].add(new MO_IceBlast(&rm->spr_iceblast, {static_cast<short>(ix + HALFPW - 30), static_cast<short>(iy + HALFPH - 16)}, -5.0f, globalID, teamID, colorID));

        projectiles++;

        ifSoundOnPlay(rm->sfx_wand);

        if (game_values.wandlimit > 0)
            DecreaseProjectileLimit();
    } else if (PlayerAction::Bomb == action) {
        // Add the bomb before the player picks it up, so the player never holds
        // a bomb that was rejected (and destroyed) by a full container.
        CO_Bomb * bomb = g_sim.objectcontainer[1].add(new CO_Bomb(&rm->spr_bomb, {static_cast<short>(ix + HALFPW - 14), static_cast<short>(iy - 8)}, {isFacingRight() ? 3.0f : -3.0f, -3.0f}, 4, globalID, teamID, colorID, RANDOM_INT(120) + 120));

        if (bomb && AcceptItem(bomb)) {
            bomb->owner = this;
            bomb->MoveToOwner();
        }

        projectiles++;

        hammertimer = 90;

        ifSoundOnPlay(rm->sfx_fireball);

        if (game_values.bombslimit > 0)
            DecreaseProjectileLimit();
    } else if (PlayerAction::SpinCape == action) {
        cape.spin(*this);
    } else if (PlayerAction::SpinTail == action) {
        tail.spin(*this);
    }

    if (action == PlayerAction::Fireball) {
        game_values.unlocksecret3part1[globalID]++;
        CheckSecret(2);
    } else if (action != PlayerAction::None) {
        game_values.unlocksecret3part1[globalID] = 0;
    }

    action = PlayerAction::None;
}

void CPlayer::SetPowerup(short iPowerup)
{
    if (shyguy) {
        ifSoundOnPlay(rm->sfx_stun);
        return;
    }

    if (iPowerup == 0) {
        if (bobomb)
            SetStoredPowerup(PowerupType::Bobomb);
        else {
            ifSoundOnPlay(rm->sfx_transform);
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_poof, ix + HALFPW - 24, iy + HALFPH - 24, 4, 5);
            bobomb = true;
        }
    } else if (iPowerup == 9) {
        if (tanookisuit.isOn())
            SetStoredPowerup(PowerupType::Tanooki);
        else
            tanookisuit.onPickup();
    } else if (iPowerup >= 10) {
        if (iPowerup == 10)
            SetStoredPowerup(PowerupType::Pow);
        else if (iPowerup == 11)
            SetStoredPowerup(PowerupType::Mod);
        else if (iPowerup == 12)
            SetStoredPowerup(PowerupType::BulletBill);
        else if (iPowerup == 13)
            SetStoredPowerup(PowerupType::Podobo);
        else if (iPowerup >= 14)
            SetStoredPowerup(iPowerup - 2); //Storing shells
    } else {
        if (iPowerup == 3 || iPowerup == 7 || iPowerup == 8) {
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, ix + HALFPW - 16, iy + HALFPH - 16, 3, 8);
        }

        if (powerup != iPowerup) {
            if (iPowerup == 3)
                ifSoundOnPlay(rm->sfx_collectfeather);
            else
                ifSoundOnPlay(rm->sfx_collectpowerup);
        }

        ClearPowerupStates();

        if (powerup == 1)
            SetStoredPowerup(PowerupType::Fire);
        else if (powerup == 2)
            SetStoredPowerup(PowerupType::Hammer);
        else if (powerup == 3)
            SetStoredPowerup(PowerupType::Feather);
        else if (powerup == 4)
            SetStoredPowerup(PowerupType::Boomerang);
        else if (powerup == 5)
            SetStoredPowerup(PowerupType::IceWand);
        else if (powerup == 6)
            SetStoredPowerup(PowerupType::Bomb);
        else if (powerup == 7)
            SetStoredPowerup(PowerupType::Leaf);
        else if (powerup == 8)
            SetStoredPowerup(PowerupType::PWings);

        powerup = iPowerup;
        projectilelimit = 0;

        flying = false;

        if (powerup == 1) {
            if (game_values.fireballlimit > 0)
                projectilelimit = game_values.fireballlimit;
        } else if (powerup == 2) {
            if (game_values.hammerlimit > 0)
                projectilelimit = game_values.hammerlimit;
        } else if (powerup == 3) {
            if (game_values.featherlimit > 0)
                projectilelimit = game_values.featherlimit;
        } else if (powerup == 4) {
            if (game_values.boomeranglimit > 0)
                projectilelimit = game_values.boomeranglimit;
        } else if (powerup == 5) {
            if (game_values.wandlimit > 0)
                projectilelimit = game_values.wandlimit;
        } else if (powerup == 6) {
            if (game_values.bombslimit > 0)
                projectilelimit = game_values.bombslimit;
        } else if (powerup == 7) {
            if (game_values.leaflimit > 0)
                projectilelimit = game_values.leaflimit;
        } else if (powerup == 8) {
            if (game_values.pwingslimit > 0)
                projectilelimit = game_values.pwingslimit;
        }
    }

    //Minor fix for becoming caped to draw animation correctly
    if (iPowerup == 3)
        cape.restart_animation(*this);
}

void CPlayer::SetStoredPowerup(short iPowerup)
{
    //If the player as the poison mushroom or the game is over, don't store the powerup
    if (game_values.gamepowerups[globalID] == static_cast<short>(PowerupType::PoisonMushroom) || game_values.gamemode->gameover) {
        ifSoundOnPlay(rm->sfx_stun);
        return;
    }

    game_values.gamepowerups[globalID] = iPowerup;
    ifSoundOnPlay(rm->sfx_storepowerup);
}

void CPlayer::ClearPowerupStates()
{
    tail.reset();
    spin.reset();
    wings.reset(*this);

    flying = false;
    flyingtimer = 0;
}

void CPlayer::DecreaseProjectileLimit()
{
    if (--projectilelimit <= 0) {
        projectilelimit = 0;
        powerup = -1;
        ifSoundOnPlay(rm->sfx_powerdown);
    }
}

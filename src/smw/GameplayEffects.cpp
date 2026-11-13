#include "GSGameplay.h"

#include "CmdArgs.h"
#include "eyecandy.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "joystick.h"
#include "GSMenu.h"
#include "net.h"
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

void checkWindEvent(short& iWindTimer, float& dNextWind)
{
    if (iWindTimer <= 0) {
        //Then trigger next wind event
        if (game_values.flags.gamewindx < dNextWind) {
            game_values.flags.gamewindx += 0.02f;

            if (game_values.flags.gamewindx >= dNextWind)
                iWindTimer = (RANDOM_INT(60)) + 30;
        } else if (game_values.flags.gamewindx >= dNextWind) {
            game_values.flags.gamewindx -= 0.02f;

            if (game_values.flags.gamewindx <= dNextWind)
                iWindTimer = (RANDOM_INT(60)) + 30;
        }
    } else {
        if (--iWindTimer <= 0) {
            dNextWind = static_cast<float>((RANDOM_INT(41)) - 20) / 4.0f;
        }
    }
}

void cleanDeadNonPlayerObjects()
{
    eyecandy[0].cleanDeadObjects();
    eyecandy[1].cleanDeadObjects();
    eyecandy[2].cleanDeadObjects();

    g_sim.objectcontainer[2].cleanDeadObjects();
    g_sim.objectcontainer[1].cleanDeadObjects();
    g_sim.objectcontainer[0].cleanDeadObjects();
    g_sim.noncolcontainer.cleanDeadObjects();
}

void animateDuringCountdown()
{
    //Move platforms
    g_map->updatePlatforms();

    cleanDeadNonPlayerObjects();

    //Keep updating map hazards
    g_sim.noncolcontainer.update();
    g_sim.objectcontainer[0].update();
    g_sim.objectcontainer[1].update();
    g_sim.objectcontainer[2].update();

    eyecandy[0].update();
    eyecandy[1].update();
    eyecandy[2].update();

    g_map->update();
}

void shakeScreen()
{
    if (game_values.flags.screenshaketimer <= 0) {
        //Make sure we zero out the shake value after it is done
        x_shake = 0;
        return;
    }

    game_values.flags.screenshaketimer--;

    static bool shakeleft = false;
    if (shakeleft) {
        x_shake -= 2;
        if (x_shake <= -2) {
            shakeleft = false;
        }
    } else {
        x_shake += 2;
        if (x_shake >= 2) {
            shakeleft = true;
        }
    }

    //Kill players touching the ground (or in air for MOd blocks)
    short iNumKillPlayers = 0;
    CPlayer * pKillPlayers[4];

    CPlayer * killer1 = GetPlayerFromGlobalID(game_values.flags.screenshakeplayerid);

    for (CPlayer* player : g_sim.players) {
        //Don't kill the player that triggered the POW/MOd
        if (player->globalID == game_values.flags.screenshakeplayerid)
            continue;

        //Don't kill players on his team either (if friendly fire is off)
        if (game_values.teamcollision != TeamCollisionStyle::On && game_values.flags.screenshaketeamid == player->teamID)
            continue;

        //Kill other players
        if (!player->IsInvincibleOnBottom() && player->isready()) {
            if (game_values.flags.screenshakekillinair == player->inair) {
                pKillPlayers[iNumKillPlayers++] = player;

                if (killer1) {
                    game_values.flags.screenshakekillscount++;

                    if (killer1->inair)
                        killer1->killsinrowinair--;  //Don't want to give both shake and in air award
                }
            }
        }
    }

    //Randomize the order in which the players are killed (so that game modes where order matters is fair)
    if (iNumKillPlayers > 0) {
        short iRandPlayer = RANDOM_INT( iNumKillPlayers);
        for (short iPlayer = 0; iPlayer < iNumKillPlayers; iPlayer++) {
            PlayerKilledPlayer(game_values.flags.screenshakeplayerid, pKillPlayers[iRandPlayer], PlayerDeathStyle::Jump, KillStyle::Pow, false, false);

            if (++iRandPlayer >= iNumKillPlayers)
                iRandPlayer = 0;
        }
    }

    //Kill goombas and koopas
    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[0].list()) {
        if (auto* movingobject = dynamic_cast<IO_MovingObject*>(obj.get())) {
            MovingObjectType type = movingobject->getMovingObjectType();

            if ((type == movingobject_goomba || type == movingobject_koopa || type == movingobject_buzzybeetle || type == movingobject_spiny)
                    && game_values.flags.screenshakekillinair == movingobject->inair) {
                CPlayer * killer = GetPlayerFromGlobalID(game_values.flags.screenshakeplayerid);

                if (killer) {
                    if (!game_values.gamemode->gameover)
                        killer->score->AdjustScore(1);

                    ifSoundOnPlay(rm->sfx_kicksound);
                    (static_cast<MO_WalkingEnemy*>(movingobject))->DieAndDropShell(true, true);

                    game_values.flags.screenshakekillscount++;

                    if (killer->inair)
                        killer->killsinrowinair--;  //Don't want to give both shake and in air award
                }
            }
        }
    }

    //Destroy throw blocks and flip shells over
    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
        if (auto* movingobject = dynamic_cast<IO_MovingObject*>(obj.get())) {
            if (game_values.flags.screenshakekillinair == movingobject->inair) {
                if (movingobject->getMovingObjectType() == movingobject_shell) {
                    CO_Shell * shell = static_cast<CO_Shell*>(movingobject);
                    if (shell->frozen || !shell->owner || shell->owner->inair == game_values.flags.screenshakekillinair)
                        shell->Flip();  //also breaks shells if frozen
                } else if (movingobject->getMovingObjectType() == movingobject_throwblock) {
                    CO_ThrowBlock * throwblock = static_cast<CO_ThrowBlock*>(movingobject);
                    if (throwblock->frozen || !throwblock->owner || throwblock->owner->inair == game_values.flags.screenshakekillinair)
                        throwblock->Die();
                } else if (movingobject->getMovingObjectType() == movingobject_throwbox) {
                    CO_ThrowBox * throwbox = static_cast<CO_ThrowBox*>(movingobject);
                    if (throwbox->frozen)
                        throwbox->Die();
                } else if (movingobject->getMovingObjectType() == movingobject_pirhanaplant) {
                    MO_PirhanaPlant * plant = static_cast<MO_PirhanaPlant*>(movingobject);
                    plant->KillPlant();
                } else if (movingobject->getMovingObjectType() == movingobject_bulletbill) {
                    MO_BulletBill * bulletbill = static_cast<MO_BulletBill*>(movingobject);
                    bulletbill->Die();
                }
            }
        }
    }

    //Add kills in row for kills from pow and mod
    if (game_values.flags.screenshakekillscount > 1 && game_values.awardstyle != AwardStyle::None) {
        game_values.flags.screenshakekillscount = 0;

        CPlayer * killer2 = GetPlayerFromGlobalID(game_values.flags.screenshakeplayerid);

        if (killer2)
            killer2->AddKillsInRowInAirAward();
    }
}

//Move the screen in a small circle
void GameplayState::spinScreen()
{
    if (!game_values.spinscreen)
        return;

    if (spindirection == 0 || spindirection == 2) {
        if (++spintimer >= 300) {
            spindirection++;
            spintimer = 0;
        }
    } else if (spindirection == 1) {
        spinspeed += 0.0008f;

        if (spinspeed >= 0.05f) {
            spinspeed = 0.05f;
            spindirection++;
        }
    } else {
        spinspeed -= 0.0008f;

        if (spinspeed <= -0.05f) {
            spinspeed = -0.05f;
            spindirection = 0;
        }
    }

    spinangle += spinspeed;

    if (spinangle >= TWO_PI) {
        spinangle -= TWO_PI;
    } else if (spinangle < 0.0f) {
        spinangle += TWO_PI;
    }

    float shakey = spinspeed * App::screenWidth * sin(spinangle);
    if (shakey < 0.0f)
        shakey -= 1.0f;

    x_shake = static_cast<short>(spinspeed * App::screenWidth * cos(spinangle));
    y_shake = static_cast<short>(shakey);
}

void updateScreenShake()
{
    if (y_shake > 0 && !game_values.spinscreen) {
        y_shake -= CRUNCHVELOCITY;

        if (y_shake < 0)
            y_shake = 0;
    }
}

// updates the bullet bills spawned by a powerup
void updateBulletBillPowerup()
{
    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        if (game_values.bulletbilltimer[iPlayer] > 0) {
            game_values.bulletbilltimer[iPlayer]--;

            if (--game_values.bulletbillspawntimer[iPlayer] <= 0) {
                game_values.bulletbillspawntimer[iPlayer] = static_cast<short>(RANDOM_INT(20) + 25);
                float speed = (static_cast<float>(RANDOM_INT(21) + 20)) / 10.0f;
                g_sim.objectcontainer[2].add(new MO_BulletBill(&rm->spr_bulletbill, &rm->spr_bulletbilldead, {0, static_cast<short>(RANDOM_INT(448))}, (RANDOM_INT(2) ? speed : -speed), iPlayer, false));
                ifSoundOnPlay(rm->sfx_bulletbillsound);
            }
        }
    }
}

void playSFX()
{
    //Play sound for skidding players
    if (game_values.flags.playskidsound) {
        if (!rm->sfx_skid.isPlaying())
            ifSoundOnPlay(rm->sfx_skid);
    } else {
        if (rm->sfx_skid.isPlaying())
            rm->sfx_skid.stop();
    }

    //Play sound for players using PWings
    if (game_values.flags.playflyingsound) {
        if (!rm->sfx_flyingsound.isPlaying())
            ifSoundOnPlay(rm->sfx_flyingsound);
    } else {
        if (rm->sfx_flyingsound.isPlaying())
            rm->sfx_flyingsound.stop();
    }
}

void playMusic()
{
    //Make sure music and sound effects keep playing
    if (game_values.flags.slowdownon != -1) {
        if (!rm->sfx_slowdownmusic.isPlaying())
            ifSoundOnPlay(rm->sfx_slowdownmusic);
    } else {
        if (rm->sfx_slowdownmusic.isPlaying())
            rm->sfx_slowdownmusic.stop();
    }

    if (game_values.flags.playinvinciblesound && game_values.musicvolume > 0) {
        if (!rm->sfx_invinciblemusic.isPlaying() && !rm->sfx_timewarning.isPlaying() && !rm->backgroundmusic[0].isPlaying())
            ifSoundOnPlay(rm->sfx_invinciblemusic);
    } else {
        if (rm->sfx_invinciblemusic.isPlaying())
            rm->sfx_invinciblemusic.stop();
    }

    //If no background music is playing, then play some
    if (!rm->backgroundmusic[0].isPlaying() && !rm->sfx_invinciblemusic.isPlaying() && !rm->sfx_timewarning.isPlaying() && !game_values.gamemode->gameover) {
        if (game_values.playnextmusic) {
            musiclist->setNextMusic(static_cast<MusicCategory>(g_map->musicCategoryID), maplist->currentShortmapname(), g_map->szBackgroundFile);
            rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
        }

        rm->backgroundmusic[0].play(game_values.playnextmusic, false);
    }
}

void PlayNextMusicTrack()
{
    if (game_values.gamemode->gameover || game_values.flags.playinvinciblesound || rm->sfx_timewarning.isPlaying())
        return;

    rm->backgroundmusic[0].stop();
    musiclist->setNextMusic(static_cast<MusicCategory>(g_map->musicCategoryID), maplist->currentShortmapname(), g_map->szBackgroundFile);
    rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
    rm->backgroundmusic[0].play(game_values.playnextmusic, false);
}

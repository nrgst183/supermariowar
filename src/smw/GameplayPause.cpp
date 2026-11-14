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
#include "rollback/NetGame.h"
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

void drawExitPauseDialog()
{
    if (game_values.flags.pausegame) {
        rm->spr_dialog.draw(224, 176);
        rm->menu_font_large.drawCentered(App::screenWidth/2, 194, "Pause");

        //menu_font_large.drawCentered(App::screenWidth/2, App::screenHeight/2, game_values.gamemode->GetModeName());
        short iMode = GetModeIconIndexFromMode(game_values.gamemode->gamemode);

        rm->menu_mode_large.draw(304, 224, {iMode << 5, 0, 32, 32});

        std::string szGoal = game_values.gamemode->GetGoalName() + ": ";

        if (game_values.gamemode->goal == -1)
            szGoal += "Unlimited";
        else
            szGoal += std::to_string(game_values.gamemode->goal);

        rm->menu_font_large.drawCentered(App::screenWidth/2, 264, szGoal.c_str());
    }

    if (game_values.flags.exitinggame) {
        rm->spr_dialog.draw(App::screenWidth * 0.35f, App::screenHeight*0.37f);
        if (rollback::NetGame::instance().inMatch()) {
            //Leaving disconnects us from the others: the match ends for everyone
            const int titleY = static_cast<int>(App::screenHeight*0.46f) - (rm->menu_font_large.getHeight() >> 1);
            rm->menu_font_large.drawCentered(App::screenWidth / 2, titleY - 14, "Leave online");
            rm->menu_font_large.drawCentered(App::screenWidth / 2, titleY + 6, "match?");
        } else {
            rm->menu_font_large.drawCentered(App::screenWidth * 0.5f, App::screenHeight*0.46f - (rm->menu_font_large.getHeight() >> 1), "Exit Game");
        }

        rm->spr_dialogbutton.draw(App::screenWidth * 0.37f, App::screenHeight*0.52f, {0, (game_values.flags.exityes ? 34 : 0), 80, 34});
        rm->spr_dialogbutton.draw(App::screenWidth * 0.51f, App::screenHeight*0.52f, {0, (game_values.flags.exityes ? 0 : 34), 80, 34});

        rm->menu_font_large.draw(App::screenWidth * 0.43f - (rm->menu_font_large.getWidth("Yes") >> 1),  App::screenHeight*0.56f - (rm->menu_font_large.getHeight() >> 1), "Yes");
        rm->menu_font_large.draw(App::screenWidth * 0.57f - (rm->menu_font_large.getWidth("No") >> 1),  App::screenHeight*0.56f - (rm->menu_font_large.getHeight() >> 1), "No");
    }
}

bool IsPauseAllowed()
{
    //Online, the match can't wait for one player
    return !game_values.flags.noexit && !rollback::NetGame::instance().inMatch();
}

bool IsExitAllowed()
{
    if (!game_values.flags.noexit || g_sim.players.empty())
        return true;

    for (CPlayer* player : g_sim.players) {
        if (game_values.playercontrol[player->getGlobalID()] == 1)
            return false;
    }

    return true;
}

bool updateExitPauseDialog(short iCountDownState) // true on exit
{
    //The world map's force exit timer that used to run here is simulation (see simulateTick)
    if (game_values.screenfade == 0 && iCountDownState <= COUNTDOWN_START_INDEX) {
        for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
            COutputControl * playerKeys = &game_values.playerInput.outputControls[iPlayer];

            //If the start key is pressed (pause key)
            if (playerKeys->game_start.fPressed && IsPauseAllowed()) {
                if (!game_values.flags.showscoreboard && !game_values.flags.exitinggame) {
                    game_values.flags.pausegame = !game_values.flags.pausegame;

                    if (game_values.flags.pausegame) {
                        //Stop the pwings sound if it is on
                        if (rm->sfx_flyingsound.isPlaying())
                            rm->sfx_flyingsound.stop();
                    }

                    //ifsoundonpause(rm->sfx_invinciblemusic);
                    //ifsoundonpause(rm->sfx_slowdownmusic);
                    ifSoundOnPlay(rm->sfx_pause);
                }
            }

            //Only player 1 is allowed to exit a game
            //if (iPlayer != 0)
            //  continue;

            if ((playerKeys->game_cancel.fPressed || (playerKeys->game_start.fPressed && game_values.gamemode->gameover)) && IsExitAllowed()) {
                if (game_values.gamemode->gameover) {
                    if (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament)
                        UpdateScoreBoard();

                    CleanUp();
                    game_values.appstate = AppState::Menu;

                    return true;
                } else {
                    if (!game_values.flags.pausegame && !game_values.flags.exitinggame) {
                        game_values.flags.exitinggame = true;
                        //ifsoundonpause(rm->sfx_invinciblemusic);
                        //ifsoundonpause(rm->sfx_slowdownmusic);

                        //Reset the keys each time we switch from menu to game and back
                        game_values.playerInput.ResetKeys();
                    }
                }
            }

            //Deal with input to game exit dialog box
            if (game_values.flags.exitinggame) {
                if (playerKeys->menu_left.fPressed)
                    game_values.flags.exityes = true;
                else if (playerKeys->menu_right.fPressed)
                    game_values.flags.exityes = false;

                if (playerKeys->menu_select.fPressed) {
                    if (game_values.flags.exityes) {
                        CleanUp();
                        game_values.flags.exitinggame = false;
                        game_values.flags.exityes = false;
                        game_values.appstate = AppState::Menu;
                        return true;
                    } else {
                        game_values.flags.exitinggame = false;
                        //ifsoundonpause(rm->sfx_invinciblemusic);
                        //ifsoundonpause(rm->sfx_slowdownmusic);

                        //Reset the keys each time we switch from menu to game and back
                        game_values.playerInput.ResetKeys();
                    }
                }
            }
        }
    }

    return false;
}

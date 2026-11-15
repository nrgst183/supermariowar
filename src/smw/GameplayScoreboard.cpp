#include "NetOverlay.h"
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
#include "RenderFrame.h"
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

void GameplayState::drawScoreboard(short iScoreTextOffset[4])
{
    g_iWinningPlayer = -1;

    //Draw scoreboards for all games (except special cases where we have a single player walking the map)
    short i;
    if (game_values.singleplayermode == -1) {
        short highestScore = 0;

        bool fReverseScoring = game_values.gamemode->GetReverseScoring();
        if (fReverseScoring)
            highestScore = 32000;

        for (i = 0; i < g_sim.score_cnt; i++) {
            int scoreValue = g_sim.score[i].score;
            if (game_values.gamemode->gamemode == game_mode_boxes_minigame)
                scoreValue = g_sim.score[i].subscore[0];

            if ((scoreValue > highestScore && !fReverseScoring) || (scoreValue < highestScore && fReverseScoring)) {
                highestScore = scoreValue;
                g_iWinningPlayer = i;
            } else if (scoreValue == highestScore) {
                g_iWinningPlayer = -1;
            }
        }

        //big end game scoreboard (sorted)
        if (game_values.flags.showscoreboard) {
            std::string gameovertext;
            if (game_values.gamemode->winningteam > -1) {
                if (game_values.teamcounts[game_values.gamemode->winningteam] == 1)
                    gameovertext = std::format("Player {} Wins!", game_values.teamids[game_values.gamemode->winningteam][0] + 1);
                else
                    gameovertext = std::format("Team {} Wins!", game_values.gamemode->winningteam + 1);
            } else {
                gameovertext = "Tie Game";
            }

            rm->game_font_large.drawCentered(App::screenWidth/2, 90, gameovertext.c_str());

        }

        //in game scoreboards
        for (i = 0; i < g_sim.score_cnt; i++) {
            if (game_values.gamemode->gamemode == game_mode_health || game_values.gamemode->gamemode == game_mode_collection || game_values.gamemode->gamemode == game_mode_boxes_minigame)
                rm->spr_shade[game_values.teamcounts[i] - 1].draw(g_sim.score[i].x, g_sim.score[i].y);
            else
                rm->spr_shade[game_values.teamcounts[i] - 1].draw(g_sim.score[i].x, g_sim.score[i].y, {0, 0, 256, 41});

            for (short k = 0; k < game_values.teamcounts[i]; k++) {
                short globalID = game_values.teamids[i][k];

                //If player is respawning, draw an animated egg counter
                if (g_sim.respawnCount[globalID] > 0 && !game_values.gamemode->gameover) {
                    if (render::advancesAnimations() && ++respawnanimationtimer[globalID] > 8) {
                        respawnanimationtimer[globalID] = 0;
                        respawnanimationframe[globalID] += 32;

                        if (respawnanimationframe[globalID] > 32)
                            respawnanimationframe[globalID] = 0;
                    }

                    short scorex = g_sim.score[i].x + scoreoffsets[k];
                    short scorey = g_sim.score[i].y + 2;
                    rm->spr_egg.draw(scorex, scorey, {respawnanimationframe[globalID], game_values.colorids[globalID] << 5, 32, 32});
                    rm->spr_eggnumbers.draw(scorex, scorey, {((g_sim.respawnCount[globalID] - 1) >> 1) << 5, game_values.colorids[globalID] << 5, 32, 32});
                } else { //otherwise draw the player's skin in the scoreboard
                    short iScoreboardSprite;
                    if (game_values.gamemode->gameover) {
                        if (g_iWinningPlayer != i)
                            iScoreboardSprite = PGFX_DEADFLYING;
                        else
                            iScoreboardSprite = PGFX_JUMPING_R;
                    } else {
                        iScoreboardSprite = PGFX_STANDING_R;
                    }

                    //Search for player state to display
                    CPlayer * player = GetPlayerFromGlobalID(globalID);

                    if (player && !game_values.gamemode->gameover) {
                        short iScoreOffsetX = g_sim.score[i].x + scoreoffsets[k];
                        short iScoreOffsetY = g_sim.score[i].y + 2;

                        if (player->ownerPlayerID > -1)
                            rm->spr_ownedtags.draw(iScoreOffsetX - 8, iScoreOffsetY - 8, {player->ownerColorOffsetX, 0, 48, 48});

                        player->GetScoreboardSprite()->at(iScoreboardSprite).draw(iScoreOffsetX, iScoreOffsetY, {player->iSrcOffsetX, 0, 32, 32});

                        //Display jail if player is jailed
                        if (player->jail.isActive())
                            rm->spr_jail.draw(iScoreOffsetX - 6, iScoreOffsetY - 6, {(player->jail.getColor() + 1) * 44, 0, 44, 44});

                        //Display current powerup if player is using one
                        if (player->powerup > 0)
                            rm->spr_storedpowerupsmall.draw(iScoreOffsetX, iScoreOffsetY + 16, {g_iPowerupToIcon[player->powerup - 1], 0, 16, 16});

                        //Display tanooki powerup if player has it
                        if (player->tanookisuit.isOn())
                            rm->spr_storedpowerupsmall.draw(iScoreOffsetX + 16, iScoreOffsetY + 16, {App::screenWidth/2, 0, 16, 16});
                    } else {
                        rm->spr_player[globalID][iScoreboardSprite].draw(g_sim.score[i].x + scoreoffsets[k], g_sim.score[i].y + 2, {0, 0, 32, 32});
                    }

                    //give crown to player(s) with most kills
                    if (g_iWinningPlayer == i)
                        rm->spr_crown.draw(g_sim.score[i].x + scoreoffsets[k] + 12, g_sim.score[i].y - 4);
                }

                //Online: their connection (NetOverlay.h), also while they respawn
                drawPingMeter(globalID, g_sim.score[i].x + scoreoffsets[k], g_sim.score[i].y + 2);

                short storedpowerupid = game_values.gamepowerups[globalID];

                //Draw stored powerup
                if (storedpowerupid != -1) {
                    if (!game_values.flags.swapplayers) {
                        rm->spr_storedpowerupsmall.draw(g_sim.score[i].x + scorepowerupoffsets[game_values.teamcounts[i] - 1][k], g_sim.score[i].y + 25, {storedpowerupid * 16, 0, 16, 16});
                    }
                }
            }

            //Draw hearts for health mode
            if (game_values.gamemode->gamemode == game_mode_health) {
                short iLife = g_sim.score[i].subscore[0];
                short iMax = g_sim.score[i].subscore[1];
                short iHeartX = g_sim.score[i].x + scorepowerupoffsets[game_values.teamcounts[i] - 1][0] - 32;

                for (short iHeart = 0; iHeart < iLife; iHeart++) {
                    if (iHeart == iMax - 1 && iHeart % 2 == 0)
                        rm->spr_scorehearts.draw(iHeartX + iHeart * 8, g_sim.score[i].y + 43, {32, 0, 8, 16});
                    else
                        rm->spr_scorehearts.draw(iHeartX + iHeart * 8, g_sim.score[i].y + 43, {(iHeart % 2) ? 8 : 0, 0, 8, 16});
                }

                for (short iHeart = iLife; iHeart < iMax; iHeart++) {
                    if (iHeart == iMax - 1 && iHeart % 2 == 0)
                        rm->spr_scorehearts.draw(iHeartX + iHeart * 8, g_sim.score[i].y + 43, {40, 0, 8, 16});
                    else
                        rm->spr_scorehearts.draw(iHeartX + iHeart * 8, g_sim.score[i].y + 43, {(iHeart % 2) ? 24 : 16, 0, 8, 16});
                }
            } else if (game_values.gamemode->gamemode == game_mode_collection) { //Draw cards for collection mode
                //Flash collected cards if 3 have been collected
                if (g_sim.score[i].subscore[0] < 3 || g_sim.score[i].subscore[2] % 20 < 10) {
                    short iNumCards = g_sim.score[i].subscore[0];
                    short iCardValues = g_sim.score[i].subscore[1];
                    short iCardX = g_sim.score[i].x + scorepowerupoffsets[game_values.teamcounts[i] - 1][0] - 20;

                    for (short iCard = 0; iCard < iNumCards; iCard++) {
                        rm->spr_scorecards.draw(iCardX + iCard * 20, g_sim.score[i].y + 43, {(iCardValues & 3) << 4, 0, 16, 16});
                        iCardValues >>= 2;
                    }
                }
            } else if (game_values.gamemode->gamemode == game_mode_boxes_minigame) { //Draw coins for boxes minigame
                //Flash collected cards if 3 have been collected
                short iNumCoins = g_sim.score[i].subscore[0];
                short iCoinX = g_sim.score[i].x + scorepowerupoffsets[game_values.teamcounts[i] - 1][0] - 32;

                short iCoin = 0;
                for (; iCoin < iNumCoins; iCoin++) {
                    rm->spr_scorecoins.draw(iCoinX + iCoin * 16, g_sim.score[i].y + 43, {0, 0, 16, 16});
                }

                for (short iEmptyCoin = iCoin; iEmptyCoin < 5; iEmptyCoin++) {
                    rm->spr_scorecoins.draw(iCoinX + iEmptyCoin * 16, g_sim.score[i].y + 43, {16, 0, 16, 16});
                }
            }

            short iScoreX = g_sim.score[i].x + iScoreTextOffset[i];
            short iScoreY = g_sim.score[i].y + 4;

            rm->spr_scoretext.draw(iScoreX, iScoreY, {g_sim.score[i].iDigitLeft, (g_sim.score[i].iDigitLeft == 0 ? 16 : 0), 16, 16});
            rm->spr_scoretext.draw(iScoreX + 18, iScoreY, {g_sim.score[i].iDigitMiddle, (g_sim.score[i].iDigitLeft == 0 && g_sim.score[i].iDigitMiddle == 0 ? 16 : 0), 16, 16});
            rm->spr_scoretext.draw(iScoreX + 36, iScoreY, {g_sim.score[i].iDigitRight, 0, 16, 16});
        }
    }
}

void UpdateScoreBoard()
{
    if (game_values.matchtype == MatchType::World) {
        //If no one won, then nothing on the world map has changed
        if (game_values.gamemode->winningteam < 0)
            return;

        //If this was the last stage, signal that the world is over
        if (game_values.tourstops[game_values.tourstopcurrent]->fEndStage) {
            game_values.tournamentwinner = 1;
            rm->backgroundmusic[4].play(true, true);
        }

        //Add up all the winnings so far and determine overall place in the standings
        for (short iScore = 0; iScore < g_sim.score_cnt; iScore++)
            game_values.tournament_scores[iScore].wins = 0;

        //Assign the order that players will show up on the scoreboard (ordered by score)
        for (short iMyScore = 0; iMyScore < g_sim.score_cnt; iMyScore++) {
            for (short iTheirScore = 0; iTheirScore < g_sim.score_cnt; iTheirScore++) {
                if (game_values.tournament_scores[iMyScore].total > game_values.tournament_scores[iTheirScore].total) {
                    game_values.tournament_scores[iTheirScore].wins++;
                }
            }
        }

        //Give players the item bonuses that were won
        TourStop * tourStop = game_values.tourstops[game_values.tourstopcurrent];

        for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
            for (short iBonus = 0; iBonus < tourStop->iNumBonuses; iBonus++) {
                if (tourStop->wsbBonuses[iBonus].iWinnerPlace == g_sim.score[iScore].place) {
                    if (game_values.worldpowerupcount[iScore] < 32)
                        game_values.worldpowerups[iScore][game_values.worldpowerupcount[iScore]++] = tourStop->wsbBonuses[iBonus].iBonus;
                    else
                        game_values.worldpowerups[iScore][31] = tourStop->wsbBonuses[iBonus].iBonus;
                }
            }
        }

        //Add powerups to player's world item inventory that they held at the end of the game
        for (short iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
            if (game_values.gamepowerups[iPlayer] != -1) {
                short iTeamId = LookupTeamID(iPlayer);
                if (game_values.worldpowerupcount[iTeamId] < 32)
                    game_values.worldpowerups[iTeamId][game_values.worldpowerupcount[iTeamId]++] = game_values.gamepowerups[iPlayer];
            }
        }
    } else if (game_values.matchtype == MatchType::Tour) {
        //If no one won (tied game), then there is no need to update the scores because nothing has changed
        if (game_values.gamemode->winningteam < 0)
            return;

        //For this game, set the player's place as the type of win
        for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
            game_values.tournament_scores[iScore].type[game_values.tourstopcurrent] = g_sim.score[iScore].place;
        }

        //Add up all the winnings so far and determine overall place in tour
        for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
            game_values.tournament_scores[iScore].total = 0;

            for (size_t iTourStop = 0; iTourStop <= game_values.tourstopcurrent; iTourStop++) {
                game_values.tournament_scores[iScore].total += (3 - game_values.tournament_scores[iScore].type[iTourStop]) * game_values.tourstops[iTourStop]->iPoints;
            }

            game_values.tournament_scores[iScore].wins = 0;
        }

        for (short iMyScore = 0; iMyScore < g_sim.score_cnt; iMyScore++) {
            for (short iTheirScore = 0; iTheirScore < g_sim.score_cnt; iTheirScore++) {
                if (game_values.tournament_scores[iMyScore].total > game_values.tournament_scores[iTheirScore].total) {
                    game_values.tournament_scores[iTheirScore].wins++;
                }
            }
        }

        if (++game_values.tourstopcurrent >= game_values.tourstops.size()) {
            //Calculate Tour Winner by counting up 1st, 2nd, 3rd, and 4th place wins

            short iWinningTeam = -2;  //Set winning team to -2 to signify a tie between teams
            short iWinningScore = 0;

            for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
                if (game_values.tournament_scores[iScore].total > iWinningScore) { //New winner found
                    iWinningTeam = iScore;
                    iWinningScore = game_values.tournament_scores[iScore].total;
                } else if (game_values.tournament_scores[iScore].total == iWinningScore) { //Winning position tied
                    iWinningTeam = -2;
                }
            }

            game_values.tournamentwinner = iWinningTeam;
            rm->backgroundmusic[4].play(true, true);
        }
    } else if (game_values.matchtype == MatchType::Tournament) {
        short maxScore = -1;  //Max score for game
        short maxTeam = -1;  //Team ID with the max score -> reset to -1 if two teams tied for win

        for (short i = 0; i < g_sim.score_cnt; i++) {
            if (g_sim.score[i].score > maxScore) {
                maxScore = g_sim.score[i].score;
                maxTeam = i;
            } else if (g_sim.score[i].score == maxScore) {
                maxTeam = -1;
            }
        }

        if (maxTeam > -1) {
            game_values.tournament_scores[maxTeam].type[game_values.tournament_scores[maxTeam].wins] = currentgamemode;

            if (++game_values.tournament_scores[maxTeam].wins >= game_values.tournamentgames) {
                game_values.tournamentwinner = maxTeam;

                if (game_values.music)
                    rm->backgroundmusic[4].play(true, true);
            }
        }
    }
}

void updateScoreboardAnimation() // scrolling to center at the end of game
{
    if (game_values.flags.showscoreboard) {
        if (game_values.flags.scorepercentmove < 1.0f) {
            game_values.flags.scorepercentmove += 0.01f;

            if (game_values.flags.scorepercentmove >= 1.0f)
                game_values.flags.scorepercentmove = 1.0f;
        } else {
           game_values.flags.scorepercentmove = 1.0f;
        }

        for (short i = 0; i < g_sim.score_cnt; i++) {
            g_sim.score[i].x = static_cast<short>(static_cast<float>(g_sim.score[i].destx - g_sim.score[i].fromx) * game_values.flags.scorepercentmove) + g_sim.score[i].fromx;
            g_sim.score[i].y = static_cast<short>(static_cast<float>(g_sim.score[i].desty - g_sim.score[i].fromy) * game_values.flags.scorepercentmove) + g_sim.score[i].fromy;
        }
    }
}

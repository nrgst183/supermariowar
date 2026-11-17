#include "MiniBoss.h"

#include "GameValues.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "objects/moving/MO_FrenzyCard.h"
#include "objects/moving/MO_Podobo.h"
#include "objects/moving/MO_SledgeBrother.h"
#include "objects/walkingenemy/WE_Koopa.h"
#include "Score.h"

//Boss Mode
//Person to score fatal hit to boss wins!
CGM_Boss_MiniGame::CGM_Boss_MiniGame() : CGameMode()
{
    gamemode = game_mode_boss_minigame;
    SetupModeStrings("Boss", "Lives", 5);
}

void CGM_Boss_MiniGame::init()
{
    CGameMode::init();

    fReverseScoring = false;

    enemytimer = static_cast<short>(RANDOM_INT(120) + 120);
    poweruptimer = 120;

    for (short iScore = 0; iScore < g_sim.score_cnt; iScore++)
        g_sim.score[iScore].SetScore(goal);

    g_sim.objectcontainer[0].add(new MO_SledgeBrother(&rm->spr_sledgebrothers, (iBossType == Boss::Hammer ? 256 : (iBossType == Boss::Bomb ? 256 : App::screenWidth/2)), iBossType));
}

void CGM_Boss_MiniGame::think()
{
    if (!gameover && g_sim.players.size() == 0) {
        gameover = true;

        if (game_values.music) {
            rm->sfx_invinciblemusic.stop();
            rm->sfx_timewarning.stop();
            rm->sfx_slowdownmusic.stop();
            ifSoundOnPlay(rm->sfx_gameover);

            rm->backgroundmusic[1].stop();
        }
    }

    if (gameover) {
        displayplayertext();
    } else {
        if (iBossType == Boss::Hammer) {
            //Randomly spawn koopas
            if (--enemytimer <= 0) {
                g_sim.objectcontainer[0].add(new MO_Koopa(&rm->spr_koopa, RANDOM_BOOL(), false, false, true));
                enemytimer = static_cast<short>(RANDOM_INT(120)) + 120;  //Spawn koopas slowly
            }
        } else if (iBossType == Boss::Bomb) {
        } else if (iBossType == Boss::Fire) {
            //Only create podobos if the difficulty is moderate or greater
            if (--enemytimer <= 0 && game_values.gamemodesettings.boss.difficulty >= 2) {
                // Draw random numbers in separate statements: argument evaluation order is
                // unspecified in C++, and every peer must draw them in the same order
                const short podoboX = static_cast<short>(RANDOM_INT(App::screenWidth * 0.95f));
                const float podoboVelY = -(float(RANDOM_INT(9)) / 2.0f) - 9.0f;
                g_sim.objectcontainer[2].add(new MO_Podobo(&rm->spr_podobo, {podoboX, App::screenHeight}, podoboVelY, -1, -1, -1, false));
                enemytimer = static_cast<short>(RANDOM_INT(80) + 60);
            }

            if (--poweruptimer <= 0) {
                poweruptimer = static_cast<short>(RANDOM_INT(80) + 60);

                if (g_sim.objectcontainer[1].countTypes(object_frenzycard) < g_sim.players.size()) {
                    g_sim.objectcontainer[1].add(new MO_FrenzyCard(&rm->spr_frenzycards, 0));
                }
            }
        }
    }
}

void CGM_Boss_MiniGame::draw_foreground()
{
    if (gameover) {
        if (winningteam == -1) {
            rm->game_font_large.drawCentered(App::screenWidth/2, 96, "You Failed To Defeat");

            switch (iBossType) {
                case Boss::Hammer:
                    rm->game_font_large.drawCentered(App::screenWidth/2, 118, "The Mighty Sledge Brother");
                    break;
                case Boss::Bomb:
                    rm->game_font_large.drawCentered(App::screenWidth/2, 118, "The Mighty Bomb Brother");
                    break;
                case Boss::Fire:
                    rm->game_font_large.drawCentered(App::screenWidth/2, 118, "The Mighty Flame Brother");
                    break;
            }
        }
    }
}

PlayerKillType CGM_Boss_MiniGame::playerkilledplayer(CPlayer &inflictor, CPlayer &other, KillStyle style)
{
    if (!gameover) {
        other.Score().AdjustScore(-1);

        if (!playedwarningsound) {
            short countscore = 0;
            for (short k = 0; k < g_sim.score_cnt; k++)
                countscore += g_sim.score[k].score;

            if (countscore <= 2)
                playwarningsound();
        }

        if (other.Score().score <= 0) {
            RemoveTeam(other.getTeamID());
            return PlayerKillType::Removed;
        }
    }

    return PlayerKillType::Normal;
}

PlayerKillType CGM_Boss_MiniGame::playerkilledself(CPlayer &player, KillStyle style)
{
    CGameMode::playerkilledself(player, style);

    if (!gameover) {
        player.Score().AdjustScore(-1);

        if (!playedwarningsound) {
            short countscore = 0;
            for (short k = 0; k < g_sim.score_cnt; k++)
                countscore += g_sim.score[k].score;

            if (countscore <= 2)
                playwarningsound();
        }

        if (player.Score().score <= 0) {
            RemoveTeam(player.getTeamID());
            return PlayerKillType::Removed;
        }
    }

    return PlayerKillType::Normal;
}

void CGM_Boss_MiniGame::playerextraguy(CPlayer &player, short iType)
{
    if (!gameover)
        player.Score().AdjustScore(iType);
}

bool CGM_Boss_MiniGame::SetWinner(CPlayer * player)
{
    winningteam = player->getTeamID();
    gameover = true;

    RemovePlayersButTeam(winningteam);

    for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
        if (winningteam == iScore)
            continue;

        g_sim.score[iScore].SetScore(0);
    }

    SetupScoreBoard(false);
    ShowScoreBoard();

    if (game_values.music) {
        rm->sfx_invinciblemusic.stop();
        rm->sfx_timewarning.stop();
        rm->sfx_slowdownmusic.stop();

        rm->backgroundmusic[1].play(true, false);
    }

    return true;
}

void CGM_Boss_MiniGame::SetBossType(Boss bosstype)
{
    iBossType = bosstype;
}

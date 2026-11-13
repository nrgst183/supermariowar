#include "MiniBoxes.h"

#include "GameValues.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "objects/moving/MO_Coin.h"
#include "Score.h"

#include <cmath>


/*
TODO
1) Create new box object that breaks on contact and either gives a coin/bonus/penalty
2) Create levels approprate for distributing boxes onto
3) Add level hazards like thwomps, podobos, fireballs
4) Add hurry up that kicks in after 3 minutes of play that adds more coins or lowers bar to win
*/

//Boxes Bonus Mini Game (used in world mode)
//Try to collect all coins from boxes and players
CGM_Boxes_MiniGame::CGM_Boxes_MiniGame() : CGameMode()
{
    goal = 10;
    gamemode = game_mode_boxes_minigame;

    SetupModeStrings("Boxes Minigame", "Lives", 5);
};

void CGM_Boxes_MiniGame::init()
{
    CGameMode::init();

    fReverseScoring = false;

    for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
        g_sim.score[iScore].SetScore(goal);
        g_sim.score[iScore].subscore[0] = 0;
    }
}


void CGM_Boxes_MiniGame::think()
{
    if (gameover) {
        displayplayertext();
        return;
    }
}

PlayerKillType CGM_Boxes_MiniGame::playerkilledplayer(CPlayer &inflictor, CPlayer &other, KillStyle style)
{
    if (gameover)
        return PlayerKillType::Normal;

    ReleaseCoin(other);

    other.Score().AdjustScore(-1);

    if (!playedwarningsound) {
        short countscore = 0;
        for (short k = 0; k < g_sim.score_cnt; k++) {
            if (&inflictor.Score() == &g_sim.score[k])
                continue;

            countscore += g_sim.score[k].score;
        }

        if (countscore <= 2) {
            playwarningsound();
        }
    }

    if (other.Score().score <= 0) {
        ReleaseAllCoinsFromTeam(other);
        RemoveTeam(other.getTeamID());
        return PlayerKillType::Removed;
    }

    return PlayerKillType::Normal;
}

PlayerKillType CGM_Boxes_MiniGame::playerkilledself(CPlayer &player, KillStyle style)
{
    if (gameover)
        return PlayerKillType::Normal;

    ReleaseCoin(player);

    player.Score().AdjustScore(-1);

    if (!playedwarningsound) {
        short countscore = 0;
        bool playwarning = false;
        for (short j = 0; j < g_sim.score_cnt; j++) {
            for (short k = 0; k < g_sim.score_cnt; k++) {
                if (j == k)
                    continue;

                countscore += g_sim.score[k].score;
            }

            if (countscore <= 2) {
                playwarning = true;
                break;
            }

            countscore = 0;
        }

        if (playwarning)
            playwarningsound();
    }

    if (player.Score().score <= 0) {
        ReleaseAllCoinsFromTeam(player);
        RemoveTeam(player.getTeamID());
        return PlayerKillType::Removed;
    }

    return PlayerKillType::Normal;
}

void CGM_Boxes_MiniGame::playerextraguy(CPlayer &player, short iType)
{
    if (gameover)
        return;

    player.Score().AdjustScore(iType);
}

PlayerKillType CGM_Boxes_MiniGame::CheckWinner(CPlayer * player)
{
    if (player->Score().subscore[0] >= 5) {
        player->Score().subscore[0] = 5;

        winningteam = player->getTeamID();
        gameover = true;

        RemovePlayersButTeam(winningteam);
        SetupScoreBoard(false);
        ShowScoreBoard();
    } else if (player->Score().subscore[0] >= 4 && !playedwarningsound) {
        playwarningsound();
    }

    return PlayerKillType::Normal;
}

void CGM_Boxes_MiniGame::ReleaseCoin(CPlayer &player)
{
    if (player.Score().subscore[0] > 0) {
        player.Score().subscore[0]--;

        Vec2s pos(player.centerX() - 16, player.centerY() - 16);

        float speed = 7.0f + static_cast<float>(RANDOM_INT(9)) / 2.0f;
        float angle = -static_cast<float>(RANDOM_INT(314)) / 100.0f;
        Vec2f vel(speed * cos(angle), speed * sin(angle));

        ifSoundOnPlay(rm->sfx_coin);

        g_sim.objectcontainer[1].add(new MO_Coin(&rm->spr_coin, vel, pos, 2, -1, 2, 30, false));
    }
}

void CGM_Boxes_MiniGame::ReleaseAllCoinsFromTeam(CPlayer &player)
{
    while (player.Score().subscore[0] > 0)
        ReleaseCoin(player);
}

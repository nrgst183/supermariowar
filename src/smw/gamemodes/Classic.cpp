#include "Classic.h"

#include "GameValues.h"
#include "player.h"
#include "ResourceManager.h"
#include "Score.h"
#include "SimulationState.h"


//mariowar (x lives - counting down)
CGM_Classic::CGM_Classic() : CGameMode()
{
    goal = 10;
    gamemode = game_mode_classic;

    SetupModeStrings("Classic", "Lives", 5);
};

void CGM_Classic::init()
{
    CGameMode::init();

    fReverseScoring = goal == -1;

    for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
        if (fReverseScoring)
            g_sim.score[iScore].SetScore(0);
        else
            g_sim.score[iScore].SetScore(goal);
    }
}


PlayerKillType CGM_Classic::playerkilledplayer(CPlayer &inflictor, CPlayer &other, KillStyle style)
{
    if (gameover)
        return PlayerKillType::Normal;

    //If we are playing classic "sumo" mode, then only score hazard kills
    if (game_values.gamemode->gamemode != game_mode_classic || game_values.gamemodesettings.classic.scoring == ScoringStyle::AllKills || style == KillStyle::Push) {
        if (fReverseScoring) {
            other.Score().AdjustScore(1);
        } else {
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
                RemoveTeam(other.getTeamID());
                return PlayerKillType::Removed;
            }
        }
    }

    if (game_values.gamemode->gamemode == game_mode_classic && game_values.gamemodesettings.classic.style == DeathStyle::Shield) {
        ifSoundOnPlay(rm->sfx_powerdown);
        other.Shield().reset();
        return PlayerKillType::NonKill;
    }

    return PlayerKillType::Normal;
}

PlayerKillType CGM_Classic::playerkilledself(CPlayer &player, KillStyle style)
{
    CGameMode::playerkilledself(player, style);

    if (!gameover) {
        if (fReverseScoring) {
            player.Score().AdjustScore(1);
        } else {
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
                RemoveTeam(player.getTeamID());
                return PlayerKillType::Removed;
            }
        }

        if (game_values.gamemode->gamemode == game_mode_classic && game_values.gamemodesettings.classic.style == DeathStyle::Shield) {
            ifSoundOnPlay(rm->sfx_powerdown);
            player.Shield().reset();
            return PlayerKillType::NonKill;
        }
    }

    return PlayerKillType::Normal;
}

void CGM_Classic::playerextraguy(CPlayer &player, short iType)
{
    if (!gameover) {
        if (fReverseScoring)
            player.Score().AdjustScore(-iType);
        else
            player.Score().AdjustScore(iType);
    }
}

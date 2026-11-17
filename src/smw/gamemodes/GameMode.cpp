#include "GameMode.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "ObjectContainer.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "Score.h"
#include "SimulationState.h"
#include "WorldTourStop.h"
#include "objects/powerup/PU_TreasureChestBonus.h"


void RemovePlayersButTeam(short teamid)
{
    for (CPlayer* player : g_sim.players) {
        if (player->getTeamID() != teamid) {
            player->state = PlayerState::Dead;
        }
    }
}

void RemovePlayersButHighestScoring()
{
    short iMaxScore = -1;

    //Figure out what the maximum score is
    for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
        if (g_sim.score[iScore].score > iMaxScore)
            iMaxScore = g_sim.score[iScore].score;
    }

    //Remove all players that don't have that max score
    for (CPlayer* player : g_sim.players) {
        if (player->Score().score < iMaxScore) {
            player->state = PlayerState::Dead;
        }
    }
}

void SetupScoreBoard(bool fOrderMatters)
{
    short i, j;

    bool doneWithScore[4] = {false, false, false, false};

    short oldmax = -1;
    short max = -1;
    for (i = 0; i < g_sim.score_cnt; i++) {
        oldmax = max;
        max = -1;

        for (j = 0; j < g_sim.score_cnt; j++) {
            if (!doneWithScore[j]) {
                //The boxes minigame doesn't use "score" it uses "subscore[0]" to determine the winner
                if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                    if (max == -1 || g_sim.score[j].subscore[0] > g_sim.score[max].subscore[0] ||
                            (g_sim.score[j].subscore[0] == g_sim.score[max].subscore[0] && g_sim.score[j].score > g_sim.score[max].score)) { //or it is tied but they died later in the game
                        max = j;
                    }
                } else {
                    //If this player's score is bigger
                    if (max == -1 || g_sim.score[j].score > g_sim.score[max].score ||
                            (g_sim.score[j].score == g_sim.score[max].score && g_sim.score[j].order > g_sim.score[max].order)) { //or it is tied but they died later in the game
                        max = j;
                    }
                }
            }
        }

        g_sim.score[max].displayorder = i;
        g_sim.score[max].place = i;

        if (!fOrderMatters && i > 0) {
            if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                if (g_sim.score[oldmax].subscore[0] == g_sim.score[max].subscore[0] &&
                        g_sim.score[oldmax].score == g_sim.score[max].score)
                    g_sim.score[max].place = g_sim.score[oldmax].place;
            } else {
                if (g_sim.score[oldmax].score == g_sim.score[max].score)
                    g_sim.score[max].place = g_sim.score[oldmax].place;
            }
        }

        doneWithScore[max] = true;	//this is the next biggest score - it doesn't belong to the remaining scores from now on
    }

    //Add the treasure chests to the map in world mode if there were any awards for winning this match
    if (game_values.matchtype == MatchType::World && game_values.gamemode->winningteam > -1 && game_values.gamemode->gamemode != game_mode_bonus) {
        TourStop * tourStop = game_values.tourstops[game_values.tourstopcurrent];
        short iNumBonuses = tourStop->iNumBonuses;

        for (short iBonus = 0; iBonus < iNumBonuses; iBonus++) {
            if (tourStop->wsbBonuses[iBonus].iWinnerPlace == 0) {
                short iBonusType = tourStop->wsbBonuses[iBonus].iBonus;
                g_sim.objectcontainer[0].add(new PU_TreasureChestBonus(&rm->spr_bonuschest, 1, 0, 30, 30, 1, 1, iBonusType));
                game_values.flags.noexittimer = 0;
                game_values.flags.noexit = false;
            }
        }
    }
}

void ShowScoreBoard()
{
    game_values.flags.showscoreboard = true;

    short iScoreboardElementHeight = 45;
    if (game_values.gamemode->gamemode == game_mode_health || game_values.gamemode->gamemode == game_mode_collection || game_values.gamemode->gamemode == game_mode_boxes_minigame)
        iScoreboardElementHeight = 63;

    for (short i = 0; i < g_sim.score_cnt; i++) {
        g_sim.score[i].destx = 309 - 34 * game_values.teamcounts[i];
        g_sim.score[i].desty = g_sim.score[i].displayorder * iScoreboardElementHeight + 140;
    }

    if (game_values.music) {
        rm->sfx_invinciblemusic.stop();
        rm->sfx_timewarning.stop();
        rm->sfx_slowdownmusic.stop();

        rm->backgroundmusic[1].play(true, false);
    }
}

//Returns true if all but one team is dead
bool RemoveTeam(short teamid)
{
    //If we have already removed this team then return
    if (g_sim.score[teamid].order > -1)
        return game_values.flags.teamdeadcounter == g_sim.score_cnt - 1;

    //kill all players on the dead team
    short iAnnouncementColor = -1;
    for (CPlayer* player : g_sim.players) {
        if (player->getTeamID() == teamid) {
            if (iAnnouncementColor == -1)
                iAnnouncementColor = player->getColorID();

            player->state = PlayerState::Dead;
        }
    }

    g_sim.score[teamid].order = game_values.flags.teamdeadcounter++;

    //Announce that a team was removed
    if (game_values.deadteamnotice && game_values.flags.teamdeadcounter < g_sim.score_cnt - 1) {
        eyecandy[2].emplace<EC_Announcement>(&rm->game_font_large, &rm->spr_announcementicons, "Team Removed!", iAnnouncementColor, 90, 200);
        rm->sfx_announcer[iAnnouncementColor + 16].play();
    }

    return game_values.flags.teamdeadcounter == g_sim.score_cnt - 1;
}


CGameMode::CGameMode()
{
    winningteam = -1;
    gamemode = game_mode_frag;
    SetupModeStrings("Free Play", "Frags", 5);
    fReverseScoring = false;
}

CGameMode::~CGameMode()
{}

void CGameMode::init()  //called once when the game is started
{
    if (goal == 1000)
        goal = 999; //Cap goal for 3 digit scoreboard

    winningteam = -1;
    gameover = false;
    playedwarningsound = false;

    //Every team starts from zero, the subscores too (health, cards, boxes:
    //the modes that use them set them after this); the last match's must not
    //carry over (replays and online peers start without them)
    for (CScore& score : g_sim.score) {
        score.SetScore(0);
        score.subscore.fill(0);
    }
};

void CGameMode::think()
{
    if (gameover)
        displayplayertext();
}

void CGameMode::displayplayertext()
{
    if (winningteam > -1) {
        for (CPlayer* player : g_sim.players) {
            if (player->getTeamID() == winningteam)
                player->spawnText("Winner!");
        }
    }
}

void CGameMode::playwarningsound()
{
    if (playedwarningsound)
        return;

    playedwarningsound = true;
    rm->sfx_invinciblemusic.stop();

    if (game_values.music && game_values.sound)
        rm->backgroundmusic[0].stop();

    ifSoundOnPlay(rm->sfx_timewarning);
}

void CGameMode::SetupModeStrings(const char * szMode, const char * szGoal, short iGoalSpacing)
{
    szModeName = szMode;
    szGoalName = szGoal;

    for (short iMode = 0; iMode < GAMEMODE_NUM_OPTIONS; iMode++) {
        if (iMode == GAMEMODE_NUM_OPTIONS - 1) {
            modeOptions[iMode].iValue = -1;
            modeOptions[iMode].szName = "Unlimited";
        } else {
            modeOptions[iMode].iValue = (iMode + 1) * iGoalSpacing;
            modeOptions[iMode].szName = std::to_string(modeOptions[iMode].iValue);
        }
    }
}

PlayerKillType CGameMode::playerkilledplayer(CPlayer &inflictor, CPlayer &other, KillStyle style)
{
    //Penalize killing your team mates
    if (!gameover) {
        if (inflictor.getTeamID() == other.getTeamID())
            inflictor.Score().AdjustScore(-1);
        else
            inflictor.Score().AdjustScore(1);
    }

    return PlayerKillType::Normal;
};

PlayerKillType CGameMode::playerkilledself(CPlayer &player, KillStyle style)
{
    if (player.IsBobomb())
        player.SetCorpseType(2); //flag to use bobomb corpse sprite

    return PlayerKillType::Normal;
}

void CGameMode::playerextraguy(CPlayer &player, short iType)
{
    if (!gameover)
        player.Score().AdjustScore(iType);
}

CPlayer * CGameMode::GetHighestScorePlayer(bool fGetHighest)
{
    size_t i, j;
    short count = 1;
    short tiedplayers[4];
    tiedplayers[0] = 0;

    //Find the first non-dead player and use them for the first player to compare to
    for (j = 0; j < g_sim.players.size(); j++) {
        if (!g_sim.players[j]->isdead()) {
            count = 1;
            tiedplayers[0] = j;
            break;
        }
    }

    //Loop through all players, comparing scores to find the highest/lowest
    for (i = j + 1; i < g_sim.players.size(); i++) {
        if (!g_sim.players[i]->isdead()) {
            if ((!fGetHighest && g_sim.players[i]->Score().score < g_sim.players[tiedplayers[0]]->Score().score) ||
                    (fGetHighest && g_sim.players[i]->Score().score > g_sim.players[tiedplayers[0]]->Score().score)) {
                count = 1;
                tiedplayers[0] = i;
            } else if (g_sim.players[i]->Score().score == g_sim.players[tiedplayers[0]]->Score().score) {
                tiedplayers[count] = i;
                count++;
            }
        }
    }

    return g_sim.players[tiedplayers[RANDOM_INT(count)]];
}

//Returns number of players in list
short CGameMode::GetScoreRankedPlayerList(CPlayer * outPlayers[4], bool fGetHighest)
{
    short iNumPlayersInList = 0;

    for (CPlayer* player : g_sim.players) {
        if (player->isdead())
            continue;

        outPlayers[iNumPlayersInList++] = player;
    }

    //Bubble sort players in to score order
    bool fNeedSwap = true;

    while (fNeedSwap) {
        fNeedSwap = false;
        short iRandom = 0;
        for (short iIndex = 0; iIndex < iNumPlayersInList - 1; iIndex++) {
            if ((fGetHighest && outPlayers[iIndex]->Score().score < outPlayers[iIndex + 1]->Score().score) ||
                    (!fGetHighest && outPlayers[iIndex]->Score().score > outPlayers[iIndex + 1]->Score().score) ||
                    (outPlayers[iIndex]->Score().score == outPlayers[iIndex + 1]->Score().score && RANDOM_BOOL() && iRandom++ < 5)) {
                CPlayer * pTemp = outPlayers[iIndex];
                outPlayers[iIndex] = outPlayers[iIndex + 1];
                outPlayers[iIndex + 1] = pTemp;

                fNeedSwap = true;
            }
        }
    }

    return iNumPlayersInList;
}

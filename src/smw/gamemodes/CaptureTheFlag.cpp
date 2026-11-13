#include "CaptureTheFlag.h"

#include "GameValues.h"
#include "ObjectContainer.h"
#include "player.h"
#include "SimulationState.h"
#include "objects/carriable/CO_Flag.h"
#include "objects/moving/MO_FlagBase.h"
#include "ResourceManager.h"


//Capture The Flag mode - each team has a base and a flag
//Protect your colored flag from being taken and score a point
//for stealing another teams flag and returning it to your base
CGM_CaptureTheFlag::CGM_CaptureTheFlag() : CGameMode()
{
    goal = 20;
    gamemode = game_mode_ctf;

    SetupModeStrings("Capture The Flag", "Flags", 5);
};

void CGM_CaptureTheFlag::init()
{
    CGameMode::init();

    bool fTeamUsed[4] = {false, false, false, false};

    for (CPlayer* player : g_sim.players) {
        short iTeamID = player->getTeamID();
        if (!fTeamUsed[iTeamID]) {
            fTeamUsed[iTeamID] = true;

            short iColorID = player->getColorID();
            MO_FlagBase * base = g_sim.objectcontainer[0].add(new MO_FlagBase(&rm->spr_flagbases, iTeamID, iColorID));

            if (!game_values.gamemodesettings.flag.centerflag)
                g_sim.objectcontainer[1].add(new CO_Flag(&rm->spr_flags, base, iTeamID, iColorID));
        }
    }

    if (game_values.gamemodesettings.flag.centerflag) {
        g_sim.objectcontainer[1].add(new CO_Flag(&rm->spr_flags, nullptr, -1, -1));
    }
}

PlayerKillType CGM_CaptureTheFlag::playerkilledplayer(CPlayer &, CPlayer &, KillStyle)
{
    return PlayerKillType::Normal;
}

PlayerKillType CGM_CaptureTheFlag::playerkilledself(CPlayer &player, KillStyle style)
{
    CGameMode::playerkilledself(player, style);

    if (player.carriedItem && player.carriedItem->getMovingObjectType() == movingobject_flag) {
        (static_cast<CO_Flag*>(player.carriedItem))->placeFlag();
        ifSoundOnPlay(rm->sfx_transform);
    }

    return PlayerKillType::Normal;
}

void CGM_CaptureTheFlag::playerextraguy(CPlayer &player, short iType)
{
    if (gameover)
        return;

    player.Score().AdjustScore(iType);
    CheckWinner(&player);
}

PlayerKillType CGM_CaptureTheFlag::CheckWinner(CPlayer * player)
{
    if (goal > -1) {
        if (player->Score().score >= goal) {
            player->Score().SetScore(goal);
            winningteam = player->getTeamID();
            gameover = true;

            RemovePlayersButTeam(winningteam);
            SetupScoreBoard(false);
            ShowScoreBoard();
        } else if (player->Score().score >= goal - 2 && !playedwarningsound) {
            playwarningsound();
        }
    }

    return PlayerKillType::Normal;
}

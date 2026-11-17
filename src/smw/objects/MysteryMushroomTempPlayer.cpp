#include "MysteryMushroomTempPlayer.h"

#include "GameValues.h"
#include "GSGameplay.h"
#include "player.h"
#include "objects/moving/MO_CarriedObject.h"

void MysteryMushroomTempPlayer::SetPlayer(CPlayer* player, short iPowerup)
{
    fx = player->fx;
    fy = player->fy;

    fOldX = player->fOldX;
    fOldY = player->fOldY;

    velx = player->velx;
    vely = player->vely;

    burnupstarttimer = player->burnup.starttimer;
    burnuptimer = player->burnup.timer;

    inair = player->inair;
    onice = player->onice;

    platform = player->platform;

    gamepowerup = iPowerup;

    iOldPowerupX = player->Score().x + scorepowerupoffsets[game_values.teamcounts[player->teamID] - 1][player->subTeamID];
    iOldPowerupY = player->Score().y + 25;
}

void MysteryMushroomTempPlayer::GetPlayer(CPlayer* player, short* iPowerup)
{
    player->fNewSwapX = fx;
    player->fNewSwapY = fy;

    player->iOldPowerupX = iOldPowerupX;
    player->iOldPowerupY = iOldPowerupY;

    player->fOldX = fOldX;
    player->fOldY = fOldY;

    player->velx = velx;
    player->vely = vely;

    player->burnup.starttimer = burnupstarttimer;
    player->burnup.timer = burnuptimer;

    player->inair = inair;
    player->onice = onice;

    player->platform = platform;

    *iPowerup = gamepowerup;

    if (player->carriedItem)
        player->carriedItem->MoveToOwner();
}

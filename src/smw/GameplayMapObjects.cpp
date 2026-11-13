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

/// NOTE: The elements are ordered!
enum MapBlockType {
    MapBlock_BrickYellow,
    MapBlock_Powerup,
    MapBlock_Donut,
    MapBlock_Flip,
    MapBlock_Bounce,
    MapBlock_NoteGray,
    MapBlock_BrickBlue,
    MapBlock_SwitchToggleRed,
    MapBlock_SwitchToggleGreen,
    MapBlock_SwitchToggleYellow,
    MapBlock_SwitchToggleBlue,
    MapBlock_SwitchBlockRed,
    MapBlock_SwitchBlockGreen,
    MapBlock_SwitchBlockYellow,
    MapBlock_SwitchBlockBlue,
    MapBlock_View,
    MapBlock_BrickRed,
    MapBlock_NoteRed,
    MapBlock_NoteBlue,
    MapBlock_BrickGray,
    MapBlock_WpnBreakFireball,
    MapBlock_WpnBreakFeather,
    MapBlock_WpnBreakShell,
    MapBlock_WpnBreakBomb,
    MapBlock_WpnBreakBoomerang,
    MapBlock_WpnBreakHammer,
    MapBlock_WpnBreakKuriboShoe,
    MapBlock_WpnBreakPWings,
    MapBlock_WpnBreakStar,
    MapBlock_WpnBreakLeaf,
};

IO_Block* spawnMapBlock(short typeId, Vec2s drawPos, const MapBlock& objdata, const std::array<short, 4>& switchStates)
{
    switch (typeId) {
        case MapBlock_BrickYellow: return new B_BreakableBlock(&rm->spr_breakableblock, drawPos, 4, 10);
        case MapBlock_Powerup: return new B_PowerupBlock(&rm->spr_powerupblock, drawPos, 4, 10, objdata.fHidden, objdata.iSettings);
        case MapBlock_Donut: return new B_DonutBlock(&rm->spr_donutblock, drawPos);
        case MapBlock_Flip: return new B_FlipBlock(&rm->spr_flipblock, drawPos, objdata.fHidden);
        case MapBlock_Bounce: return new B_BounceBlock(&rm->spr_bounceblock, drawPos, objdata.fHidden);
        case MapBlock_NoteGray: return new B_NoteBlock(&rm->spr_noteblock, drawPos, 4, 10, NoteBlockType::Gray, objdata.fHidden);
        case MapBlock_BrickBlue: return new B_ThrowBlock(&rm->spr_throwblock, drawPos, 4, 10, ThrowBlockType::Blue);
        case MapBlock_SwitchToggleRed: return new B_OnOffSwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Red, switchStates[0]);
        case MapBlock_SwitchToggleGreen: return new B_OnOffSwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Green, switchStates[1]);
        case MapBlock_SwitchToggleYellow: return new B_OnOffSwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Yellow, switchStates[2]);
        case MapBlock_SwitchToggleBlue: return new B_OnOffSwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Blue, switchStates[3]);
        case MapBlock_SwitchBlockRed: return new B_SwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Red, objdata.iSettings[0]);
        case MapBlock_SwitchBlockGreen: return new B_SwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Green, objdata.iSettings[0]);
        case MapBlock_SwitchBlockYellow: return new B_SwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Yellow, objdata.iSettings[0]);
        case MapBlock_SwitchBlockBlue: return new B_SwitchBlock(&rm->spr_switchblocks, drawPos, SwitchColor::Blue, objdata.iSettings[0]);
        case MapBlock_View: return new B_ViewBlock(&rm->spr_viewblock, drawPos, objdata.fHidden, objdata.iSettings);
        case MapBlock_BrickRed: return new B_ThrowBlock(&rm->spr_throwblock, drawPos, 4, 10, ThrowBlockType::Red);
        case MapBlock_NoteRed: return new B_NoteBlock(&rm->spr_noteblock, drawPos, 4, 10, NoteBlockType::Red, objdata.fHidden);
        case MapBlock_NoteBlue: return new B_NoteBlock(&rm->spr_noteblock, drawPos, 4, 10, NoteBlockType::Blue, objdata.fHidden);
        case MapBlock_BrickGray: return new B_ThrowBlock(&rm->spr_throwblock, drawPos, 4, 10, ThrowBlockType::Gray);
        case MapBlock_WpnBreakFireball: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Fireball);
        case MapBlock_WpnBreakFeather: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Feather);
        case MapBlock_WpnBreakShell: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Shell);
        case MapBlock_WpnBreakBomb: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Bomb);
        case MapBlock_WpnBreakBoomerang: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Boomerang);
        case MapBlock_WpnBreakHammer: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Hammer);
        case MapBlock_WpnBreakKuriboShoe: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::KuriboShoe);
        case MapBlock_WpnBreakPWings: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::PWings);
        case MapBlock_WpnBreakStar: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Star);
        case MapBlock_WpnBreakLeaf: return new B_WeaponBreakableBlock(&rm->spr_weaponbreakableblock, drawPos, WeaponDamageType::Leaf);
        default: return nullptr;
    }
}

void LoadMapObjects(bool fPreview)
{
    LoadMapHazards(fPreview);

    //Clear all the previous switch settings
    for (short iSwitch = 0; iSwitch < 8; iSwitch++)
        g_map->switchBlocks[iSwitch].clear();

    //Add blocks (breakable, note, switch, throwable, etc)
    for (short x = 0; x < MAPWIDTH; x++) {
        for (short y = 0; y < MAPHEIGHT; y++) {
            const short typeId = g_map->objectdata[x][y].iType;
            IO_Block* block = spawnMapBlock(typeId, {static_cast<short>(x * 32), static_cast<short>(y * 32)}, g_map->objectdata[x][y], g_map->iSwitches);
            if (block)
                block = g_sim.noncolcontainer.add(block);  // nullptr if the container is full

            g_map->blockdata[x][y] = block;

            switch (typeId) {
                case MapBlock_SwitchToggleRed:
                case MapBlock_SwitchToggleGreen:
                case MapBlock_SwitchToggleYellow:
                case MapBlock_SwitchToggleBlue:
                case MapBlock_SwitchBlockRed:
                case MapBlock_SwitchBlockGreen:
                case MapBlock_SwitchBlockYellow:
                case MapBlock_SwitchBlockBlue:
                    if (block)
                        g_map->switchBlocks[typeId - MapBlock_SwitchToggleRed].push_back(block);
                    break;
            }
        }
    }

    //Scan for throw box objects and add items to them if approprate
    //Add special coins to them for the boxes minigame
    short iCountWeight = 0;
    for (short iPowerup = 0; iPowerup < NUM_POWERUPS; iPowerup++)
        iCountWeight += game_values.powerupweights[iPowerup];

    short iThrowBoxCount = 0;
    std::vector<bool> fBoxHasCoin;

    if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
        for (const MapItem& item : g_map->mapitems) {
            if (item.itype == 5)
                iThrowBoxCount++;
        }

        fBoxHasCoin.assign(iThrowBoxCount, false);

        //Randomly choose boxes to put 5 coins in
        for (short iItem = 0; iItem < 5 && iItem < iThrowBoxCount; iItem++) {
            short iBoxIndex = RANDOM_INT(iThrowBoxCount);

            while (fBoxHasCoin[iBoxIndex]) {
                if (++iBoxIndex >= iThrowBoxCount)
                    iBoxIndex = 0;
            }

            fBoxHasCoin[iBoxIndex] = true;
        }

        //If map has less than 5 boxes, then insert coins into map in random locations
        short iExtraCoinsNeeded = 5 - iThrowBoxCount;
        for (short iExtraCoin = 0; iExtraCoin < iExtraCoinsNeeded; iExtraCoin++) {
            g_sim.objectcontainer[1].add(new MO_Coin(&rm->spr_coin, Vec2f::zero(), Vec2s::zero(), 2, -1, 2, 0, true));
        }
    }

    //Add map objects like springs, shoes and spikes
    short iAddThrowBoxIndex = 0;
    for (const MapItem& item : g_map->mapitems) {
        const short iType = item.itype;
        const Vec2s pos(item.ix * 32, item.iy * 32);

        if (iType == 0)
            g_sim.objectcontainer[1].add(new CO_Spring(&rm->spr_spring, pos, false));
        else if (iType == 1)
            g_sim.objectcontainer[1].add(new CO_Spike(&rm->spr_spike, pos));
        else if (iType == 2)
            g_sim.objectcontainer[1].add(new CO_KuriboShoe(&rm->spr_kuriboshoe, pos, false));
        else if (iType == 3)
            g_sim.objectcontainer[1].add(new CO_Spring(&rm->spr_spring, pos, true));
        else if (iType == 4)
            g_sim.objectcontainer[1].add(new CO_KuriboShoe(&rm->spr_kuriboshoe, pos, true));
        else if (iType == 5) {
            short iItem = NO_POWERUP;
            if (!fPreview) {
                if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                    if (fBoxHasCoin[iAddThrowBoxIndex])
                        iItem = MINIGAME_COIN;
                } else if (game_values.gamemode->gamemode == game_mode_health && RANDOM_INT(100) < game_values.gamemodesettings.health.percentextralife) {
                    iItem = HEALTH_POWERUP;
                } else if ((game_values.gamemode->gamemode == game_mode_timelimit && RANDOM_INT(100) < game_values.gamemodesettings.time.percentextratime) ||
                          (game_values.gamemode->gamemode == game_mode_star && RANDOM_INT(100) < game_values.gamemodesettings.star.percentextratime)) {
                    iItem = TIME_POWERUP;
                } else if ((game_values.gamemode->gamemode == game_mode_coins && RANDOM_INT(100) < game_values.gamemodesettings.coins.percentextracoin) ||
                          (game_values.gamemode->gamemode == game_mode_greed && RANDOM_INT(100) < game_values.gamemodesettings.greed.percentextracoin)) {
                    iItem = COIN_POWERUP;
                } else if (game_values.gamemode->gamemode == game_mode_jail && (RANDOM_INT(100)) < game_values.gamemodesettings.jail.percentkey) {
                    iItem = JAIL_KEY_POWERUP;
                } else if (iCountWeight > 0 && (RANDOM_INT(100)) < 40) {
                    int iRandPowerup = RANDOM_INT( iCountWeight )+ 1;
                    iItem = 0;

                    int iPowerupWeightCount = game_values.powerupweights[iItem];

                    while (iPowerupWeightCount < iRandPowerup)
                        iPowerupWeightCount += game_values.powerupweights[++iItem];
                }
            }

            g_sim.objectcontainer[1].add(new CO_ThrowBox(&rm->spr_throwbox, pos, iItem));
            iAddThrowBoxIndex++;
        }
    }

    //Set all the 1x1 gaps up so players can run across them
    g_map->UpdateAllTileGaps();
}

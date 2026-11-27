#include "CmdArgs.h"
#include "player.h"

#include "eyecandy.h"
#include "GameValues.h"
#include "movingplatform.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "gamemodes/Chicken.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "rollback/MatchConfig.h"

#include <cassert>
#include <cmath>

void CScore::AdjustScore(short iValue)
{
    if (game_values.gamemode->gameover)
        return;

    score += iValue;

    if (score < 0)
        score = 0;

    SetDigitCounters();
}

CPlayer * GetPlayerFromGlobalID(short iGlobalID)
{
    for (CPlayer* player : g_sim.players) {
        if (player->globalID == iGlobalID)
            return player;
    }

    return nullptr;
}

CPlayer::CPlayer(short iGlobalID, short iLocalID, short iTeamID, short iSubTeamID, short iColorID,
    SpriteStrip& nsprites, CScore *nscore, short * sRespawnCounter, CPlayerAI * ai)
    : shyguy(false)
    , globalID(iGlobalID)
    , teamID(iTeamID)
    , score(nscore)
    , localID(iLocalID)
    , subTeamID(iSubTeamID)
    , colorID(iColorID)
    , lockfall(false)
    , lockfire(false)
    , pPlayerAI(ai)
    , sprite_state(PGFX_JUMPING_R)
    , sprswitch(0)
    , projectiles(0)
    , spawnradius(0.0f)
    , spawnangle(0.0f)
    , respawncounter(sRespawnCounter)
    , powerupradius(0.0f)
    , powerupangle(0.0f)
    , fAcceptingItem(false)
    , fPressedAcceptItem(false)
    , carriedItem(nullptr)
    , ownerPlayerID(-1)
    , ownerColorOffsetX(0)
    , spawntext(20)  // set it to 20 so there is an immediate text spawned upon winning
    , iSuicideCreditPlayerID(-1)
    , iSuicideCreditTimer(0)
{
    //AI stuff
    if (pPlayerAI)
        pPlayerAI->SetPlayer(this);

    // The key state is simulation state, fed by the tick's InputFrame (or the AI)
    playerKeys = &g_sim.playerKeys[globalID];

    playerDevice = rollback::simulationPlayerDevice(globalID);    //from the match config online
    sprites = &nsprites;

    //Do this so we have a valid x,y to say the player is so other items that init with the player will get valid positions
    //The actual choosing of a spawning position happens later
    FindSpawnPoint();

    SetupNewPlayer();
    *respawncounter = 0;

    game_values.unlocksecret1part1[globalID] = false;
}

CPlayer::~CPlayer()
{
    //The carried object may outlive the player (e.g. when the game is cleaned up): don't leave it a dangling owner
    if (carriedItem)
        carriedItem->owner = nullptr;

    if (pPlayerAI)
        delete pPlayerAI;
}

void CPlayer::Init()
{
    printf("CPlayer::init()\n");
    if (pPlayerAI)
        pPlayerAI->Init();
}

CScore& CPlayer::Score() {
    assert(score);
    return *score;
}

void CPlayer::SetCorpseType(short type) {
    diedas = type;
}

void CPlayer::beginEditing()
{
    editing = true;
    if (carriedItem) {
        carriedItem->Drop();
        carriedItem = nullptr;
    }
    if (state != PlayerState::Waiting)
        SetupNewPlayer();
}

void CPlayer::endEditing(short spawnX, short spawnY)
{
    editing = false;
    *respawncounter = 0;    //(it spawns at once)
    spawnAtX = spawnX;
    spawnAtY = spawnY;
}

void CPlayer::update_waitingForRespawn()
{
    assert(state == PlayerState::Waiting);
    if (editing)
        return;    //(building: out of play until it's done)

    //use 31 frames to do 0.5 second increments
    if (*respawncounter > 0 && ++waittimer >= 31) {
        waittimer = 0;
        (*respawncounter)--;
    }

    if (*respawncounter <= 0) {
        *respawncounter = 0;

        //(the Map Maker: where the builder's cursor was, if nothing solid is there)
        bool placed = false;
        if (spawnAtX >= 0) {
            const short col = static_cast<short>((spawnAtX + HALFPW) / TILESIZE), row = static_cast<short>((spawnAtY + PH - 1) / TILESIZE);
            const IO_Block* block = g_map->block(col, row);
            if (col >= 0 && col < MAPWIDTH && row >= 0 && row < MAPHEIGHT && !(g_map->map(col, row) & tile_flag_solid) && !block) {
                setXi(spawnAtX);
                setYi(spawnAtY);
                placed = true;
            }
            spawnAtX = spawnAtY = -1;
        }
        if (placed || FindSpawnPoint()) {
            //Make sure spawn point isn't inside a tile
            collisions.checksides(*this);

            state = PlayerState::Spawning;

            if (game_values.spawnstyle == SpawnStyle::Instant) {
                eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, ix + HALFPW - 16, iy + HALFPH - 16, 3, 8);
            } else if (game_values.spawnstyle == SpawnStyle::Door) {
                eyecandy[0].emplace<EC_Door>(&rm->spr_spawndoor, &sprites->at(sprite_state), ix + HALFPW - 16, iy + HALFPH - 16, 1, iSrcOffsetX, colorID);
            }
        }
    }
}

void CPlayer::update_respawning()
{
    assert(isspawning());

    switch (game_values.spawnstyle) {
        case SpawnStyle::Instant:
            state = PlayerState::Ready;
            break;
        case SpawnStyle::Door:
            //Wait for door eyecandy to open to let mario out (20 frames for door to appear and 16 frames to open)
            if (++spawntimer > 36) {
                spawntimer = 0;
                state = PlayerState::Ready;
            }
            break;
        case SpawnStyle::Swirl:
            if (++spawntimer >= 50) {
                state = PlayerState::Ready;
            } else if (spawntimer % 2) {
                short swirlindex = spawntimer >> 1;
                short ixoffset = ix - PWOFFSET;
                short iyoffset = iy - PHOFFSET;
                short iColorIdOffset = colorID << 5;

                for (short iSwirl = 0; iSwirl < 4; iSwirl++)
                    eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_spawnsmoke, ixoffset + g_iSwirlSpawnLocations[iSwirl][0][swirlindex], iyoffset + g_iSwirlSpawnLocations[iSwirl][1][swirlindex], 4, 4, 0, iColorIdOffset, 32, 32);
            }
            break;
    }
}

void CPlayer::cpu_think()
{
    pPlayerAI->Think(playerKeys);
}

void CPlayer::thinkByInput(CPlayerAI& ai, COutputControl& keys, bool& unlockFire)
{
    const bool wasLocked = lockfire;
    const short star = throw_star;
    ai.SetPlayer(this);
    ai.Think(&keys);
    unlockFire = wasLocked && !lockfire;
    if (throw_star != star)
        ai.threwStar(g_sim.tick + throw_star);
    lockfire = wasLocked;
    throw_star = star;
}

void CPlayer::die(PlayerDeathStyle deathStyle, bool fTeamRemoved, bool fKillCarriedItem)
{
    if (cmd::g_autotest.aiBench && state == PlayerState::Ready && !fTeamRemoved && globalID >= 0 && globalID < 4)
        aibench::deaths[globalID]++, aibench::selfDeaths[globalID]++, aibench::countDeath(globalID, ix, iy);

    //Only show the death gfx if the player is alive when he died
    //If he is spawning or already dead, then don't show anything
    if (state >= PlayerState::Dead) {
        short iDeathSprite = deathStyle == PlayerDeathStyle::Jump ? PGFX_DEADFLYING : PGFX_DEAD;

        gfxSprite* corpseSprite = &sprites->at(iDeathSprite);
        auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);

        //If the player was a bobomb or chicken, make sure their death sprite matches
        if (diedas == 1 || (gmChicken && gmChicken->chicken() == this))
            corpseSprite = &rm->spr_chocobo[colorID][iDeathSprite];
        else if (diedas == 2 || bobomb)
            corpseSprite = &rm->spr_bobomb[colorID][iDeathSprite];
        else if (diedas == 3 || shyguy)
            corpseSprite = &rm->spr_shyguy[colorID][iDeathSprite];

        //Add eyecandy for the dead player
        if (deathStyle == PlayerDeathStyle::Shatter || frozen) {
            eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokeniceblock, ix + HALFPW - 16, iy + HALFPH - 16, -1.5f, -7.0f, 4, 2, 0, 0, 16, 16);
            eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokeniceblock, ix + HALFPW, iy + HALFPH - 16, 1.5f, -7.0f, 4, 2, 0, 0, 16, 16);
            eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokeniceblock, ix + HALFPW - 16, iy + HALFPH, -1.5f, -4.0f, 4, 2, 0, 0, 16, 16);
            eyecandy[2].emplace<EC_FallingObject>(&rm->spr_brokeniceblock, ix + HALFPW, iy + HALFPH, 1.5f, -4.0f, 4, 2, 0, 0, 16, 16);

            game_values.unlocksecret2part2++;
        } else if (deathStyle == PlayerDeathStyle::Jump) {
            eyecandy[2].emplace<EC_FallingObject>(corpseSprite, ix + HALFPW - 16, iy + PH - 32, 0.0f, -VELTURBOJUMP, 1, 0, iSrcOffsetX, 0, 32, 32);
        } else if (deathStyle == PlayerDeathStyle::Squish) {
            eyecandy[1].emplace<EC_Corpse>(corpseSprite, static_cast<float>(ix - PWOFFSET), static_cast<float>(iy+PH-32), iSrcOffsetX);
        }
    }

    //Drop any item the dead player was carrying
    if (carriedItem) {
        if (fKillCarriedItem)
            carriedItem->KillObjectMapHazard();
        else
            carriedItem->Drop();

        carriedItem = nullptr;
    }

    //Drop a shoe item if the player died in one
    if (kuriboshoe.is_on()) {
        CO_KuriboShoe * shoe = new CO_KuriboShoe(&rm->spr_kuriboshoe, {static_cast<short>(ix - PWOFFSET), static_cast<short>(iy - PHOFFSET)}, kuriboshoe.getType() == STICKY);
        shoe->collision_detection_checksides();

        g_sim.objectcontainer[1].add(shoe);
    }

    //If the game mode didn't say to remove the team from the game, then respawn the player
    if (!fTeamRemoved) {
        //Screen crunch (cosmetic)
        if (!sim::isResimulating()) {
            if (game_values.screencrunch && !game_values.spinscreen)
                y_shake += CRUNCHAMOUNT;

            if (y_shake > CRUNCHMAX)
                y_shake = CRUNCHMAX;
        }

        SetupNewPlayer();
    }
}

// Called by CO_KuriboShoe
void CPlayer::SetKuriboShoe(KuriboShoeType type)
{
    assert(type > 0);
    kuriboshoe.setType(type);

    if (carriedItem && !carriedItem->IsCarriedByKuriboShoe()) {
        carriedItem->Drop();
        carriedItem = nullptr;
    }

    //Clear out powerup states that the player might be in the middle of
    ClearPowerupStates();
}

void CPlayer::SetupNewPlayer()
{
    playerRenderState(*this).scoreboardSprite = sprites;
    action = PlayerAction::None;

    velx = 0.0f;
    oldvelx = velx;

    if (game_values.spawnstyle == SpawnStyle::Door)
        vely = 0.0f;
    else
        vely = -(VELJUMP / 2);	//make the player jump up on respawn

    fOldX = fx;
    fOldY = fy;

    inair = true;
    onice = false;
    lockjump = true;
    superjumptimer = 0;
    superjumptype = 0;
    projectilelimit = 0;
    hammertimer = 0;

    frozen = false;
    frozentimer = 0;

    killsinrow = 0;
    killsinrowinair = 0;
    extrajumps = 0;

    StripPowerups();
    ClearPowerupStates();
    //(an AI comparison of an item's use: every player has it)
    if (cmd::g_autotest.enabled && cmd::g_autotest.powerup >= 1 && cmd::g_autotest.powerup <= 8)
        powerup = static_cast<short>(cmd::g_autotest.powerup);

    cape.reset(*this);

    frictionslidetimer = 0;
    playerRenderState(*this).bobombSmokeTimer = 0;
    rainsteptimer = 0;

    spawntimer = 0;
    waittimer = 0;
    *respawncounter = game_values.respawn;
    state = PlayerState::Waiting;

    fallthrough = false;
    diedas = 0;
    iSrcOffsetX = 0;

    outofarena.reset();

    platform = nullptr;
    iHorizontalPlatformCollision = -1;
    iVerticalPlatformCollision = -1;
    iPlatformCollisionPlayerId = -1;

    shield.reset();
    kuriboshoe.reset();
    secretcode.reset();
    cardcollection.reset();
    superstomp.reset();
    suicidetimer.reset();

    throw_star = 0;

    game_values.unlocksecret3part1[globalID] = 0;
    game_values.unlocksecret3part2[globalID] = 0;
}

void CPlayer::StripPowerups()
{
    bobomb = false;
    powerup	= -1;

    tanookisuit.reset();
    invincibility.reset();
    powerupused.reset();
}

bool CPlayer::FindSpawnPoint()
{
    bool fRet = g_map->findspawnpoint(teamID + 1, &ix, &iy, PW, PH, false);

    fx = static_cast<float>(ix);
    fy = static_cast<float>(iy);

    return fRet;
}

void CPlayer::spawnText(const char * szText)
{
    if (++spawntext >= 20) {
        eyecandy[2].emplace<EC_GravText>(&rm->game_font_large, ix + HALFPW, iy, szText, -VELJUMP);
        spawntext = 0;	//spawn text every 20 frames
    }
}

void CPlayer::makeinvincible()
{
    invincibility.turn_on(*this);
}

void CPlayer::makefrozen(short iTime)
{
    if (!frozen) {
        frozen = true;
        frozentimer = iTime;

        eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, ix + HALFPW - 16, iy + HALFPH - 16, 3, 8);
    }
}

void CPlayer::turnslowdownon()
{
    game_values.flags.slowdownon = teamID;
    game_values.flags.slowdowncounter = 0;
}

//Returns true if player facing right, false if left
bool CPlayer::isFacingRight() const
{
    bool fLeft = game_values.reversewalk;
    bool fRight = !fLeft;

    if (game_values.flags.swapplayers && game_values.swapstyle == MysteryMushroomSwapStyle::Walk) {
        if (fNewSwapX < fOldSwapX)
            return fLeft;
        else
            return fRight;
    }

    if (state == PlayerState::Ready) {
        if (playerKeys->game_left.fDown && playerKeys->game_right.fDown && velx != 0.0f) {
            if (velx > 0.0f)
                return fRight;
            else
                return fLeft;
        } else {
            if (playerKeys->game_left.fDown)
                return fLeft;
            else if (playerKeys->game_right.fDown)
                return fRight;
        }
    }

    if (sprite_state == PGFX_STOPPING_R)
        return false;
    else if (sprite_state == PGFX_STOPPING_L)
        return true;
    else if (!(sprite_state & PGFX_FACING_LEFT_BIT))
        return true;

    return false;
}

bool CPlayer::AcceptItem(MO_CarriedObject * item)
{
    if (fAcceptingItem && tanookisuit.notStatue() && (!kuriboshoe.is_on() || item->IsCarriedByKuriboShoe())) {
        action = PlayerAction::None;

        carriedItem = item;
        item->owner = this;
        carriedItem->velx = 0.0f;
        carriedItem->vely = 0.0f;

        fAcceptingItem = false;
        return true;
    }

    return false;
}

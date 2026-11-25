#include "CpuDriver.h"
#include "LoadLog.h"
#include "GSGameplay.h"

#include "CmdArgs.h"
#include "DebugProfile.h"
#include "Determinism.h"
#include "eyecandy.h"
#include "FileList.h"
#include "GameValues.h"
#include "joystick.h"
#include "GSMenu.h"
#include "map.h"
#include "MapList.h"
#include "Platform.h"
#include "ResourceManager.h"
#include "TilesetManager.h"
#include "SimulationState.h"
#include "WorldTourStop.h"
#include "NetOverlay.h"
#include "rollback/NetGame.h"
#include "rollback/Replay.h"
#include "MatchHistory.h"
#include "rollback/StateChecksum.h"
#include "rollback/SyncTest.h"
#include "gamemodes/Chicken.h"
#include "gamemodes/Tag.h"
#include "objects/MysteryMushroomTempPlayer.h"
#include "objects/carriable/CO_KuriboShoe.h"
#include "objects/carriable/CO_Shell.h"
#include "objects/carriable/CO_Spike.h"
#include "objects/carriable/CO_ThrowBox.h"
#include "objects/powerup/PU_BobombPowerup.h"
#include "objects/powerup/PU_BombPowerup.h"
#include "objects/powerup/PU_BoomerangPowerup.h"
#include "objects/powerup/PU_BulletBillPowerup.h"
#include "objects/powerup/PU_ClockPowerup.h"
#include "objects/powerup/PU_ExtraGuyPowerup.h"
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

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

// main.cpp: --at-perf
namespace perfstats {
void reset();
void report(const std::string& map, int mode);
}

SimulationState g_sim;

CEyecandyContainer eyecandy[3];
SpotlightManager spotlightManager;

short g_iWinningPlayer;
short scorepowerupoffsets[3][3] = { {  37,  0,    0 },
                                    {  71,  89,   0 },
                                    { 105, 123, 141 } };

short GameplayState::scoreoffsets[3] = {2, 36, 70};
short GameplayState::g_iPowerupToIcon[8] = {80, 176, 272, 304, 336, 368, 384, 400};

GameplayState::GameplayState()
{
    for (short p = 0; p < 4; p++) {
        g_sim.respawnCount[p] = 0;
        respawnanimationtimer[p] = 0;
        respawnanimationframe[p] = 0;
    }

    //Vars that keep track of spinning the screen
    spinangle = 0.0f;
    spinspeed = 0.0f;
    spindirection = 1;
    spintimer = 0;
}

GameplayState::~GameplayState() = default;

GameplayState& GameplayState::instance() {
    static GameplayState gps;
    return gps;
}

//-----------------------------------------------------------------------------
// THE GAME LOOP
//-----------------------------------------------------------------------------

constexpr short iCountDownTimes[28] = {3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 15, 3, 3, 3, 3, 3, 3, 45, 3, 3, 3};
constexpr short iCountDownAnnounce[28] = {-1, -1, -1, 12, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1, -1, 14, -1, -1, -1, -1, -1, -1, 15, -1, -1, -1};

#ifdef _DEBUG
short iSledgeBrotherSetting = 0;
short iSledgeBrotherAttribute = 0;

bool g_fAutoTest = false;
bool g_fRecordTest = false;
int exitgametimer = 0;
#endif

// Opt-in --autotest harness state
int g_autoTestMatchesDone = 0;
std::string g_autoTestStageReport;
uint64_t g_autoTestLoadStartNs = 0;
double g_autoTestSlowestLoadMs = 0;
std::string g_autoTestSlowestLoadMap;
static int autoTestFrames = 0;
static int autoTestOverFrames = 0;

namespace {
// --at-humans: random but plausible input for the "human" players, made
// outside the simulation the way a netplay peer's input would arrive.
struct HumanBot {
    uint8_t direction = 0;
    int directionTicks = 0;
    bool turbo = false;
    int jumpTicks = 0;
    int downTicks = 0;

    // `next(max)` is a random number below max
    template <typename Next>
    uint8_t step(Next&& next)
    {
        if (--directionTicks <= 0) {
            const int r = next(10);
            direction = r < 4 ? InputBit::Right : (r < 8 ? InputBit::Left : 0);
            directionTicks = 10 + next(80);
            turbo = next(3) != 0;
        }

        if (jumpTicks > 0)
            --jumpTicks;
        else if (next(25) == 0)
            jumpTicks = 1 + next(30);

        if (downTicks > 0)
            --downTicks;
        else if (next(150) == 0)
            downTicks = 5 + next(40);

        uint8_t byte = direction;
        if (turbo)
            byte |= InputBit::Turbo;
        if (jumpTicks > 0)
            byte |= InputBit::Jump;
        if (downTicks > 0)
            byte |= InputBit::Down;
        if (next(120) == 0)
            byte |= InputBit::Powerup;
        return byte;
    }
};

//xorshift32
int nextRandom(uint32_t& state, int max)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<int>(state % static_cast<uint32_t>(max));
}

// Offline: one generator for all human players, seeded from --at-seed
// The Map Maker: an auto-play human builds a while now and then (its random buttons paint, erase,
// page the palette), so that the sync tests, the online tests and replays go through the building too
template <typename Next>
uint8_t mapMakerBotByte(int slot, uint8_t byte, Next&& next)
{
    if (!game_values.gamemode || game_values.gamemode->gamemode != game_mode_mapmaker)
        return byte;
    //(the host gives everyone the right to build first, by its commands: 0xF8 | on | slot)
    const int host = matchedit::hostSlot();
    if (slot == host && g_sim.tick >= 20 && g_sim.tick < 24) {
        const int target = g_sim.tick - 20;
        if (target != host && game_values.playercontrol[target] == 1)
            return static_cast<uint8_t>(0xF8 | 4 | target);
    }
    const int phase = (g_sim.tick + slot * 37) % 300;
    if (matchedit::building(slot)) {
        if (phase == 250)
            return 0xC0;    //(done)
        byte = static_cast<uint8_t>(byte | (next(3) == 0 ? 0x20 : 0) | (next(16) == 0 ? 0x40 : 0) | (next(16) == 1 ? 0x80 : 0));
        return byte == 0xC0 ? 0x40 : byte;
    }
    return phase == 100 ? static_cast<uint8_t>(byte | 0x40) : byte;    //(Back: building)
}

InputFrame autoTestHumanInput()
{
    static HumanBot bots[4];
    static uint32_t state = 0x9e3779b9u ^ static_cast<uint32_t>(cmd::g_autotest.seed);
    auto next = [](int max) { return nextRandom(state, max); };

    InputFrame input;
    for (int i = 0; i < INPUT_MAX_PLAYERS; i++) {
        if (game_values.playercontrol[i] != 1)
            continue;
        input.players[i] = mapMakerBotByte(i, bots[i].step(next), next);
    }
    return input;
}

// Online: each local human player has its own generator, seeded from
// --at-seed and the slot, so that the players of different peers differ
uint8_t autoTestNetHumanInput(int slot)
{
    static HumanBot bots[4];
    static uint32_t states[4] = {};
    uint32_t& state = states[slot];
    if (state == 0)
        state = (0x9e3779b9u ^ static_cast<uint32_t>(cmd::g_autotest.seed + 1 + slot) * 2654435761u) | 1u;
    auto next = [&state](int max) { return nextRandom(state, max); };
    return mapMakerBotByte(slot, bots[slot].step(next), next);
}

// Cheap FNV-1a checksum of the simulation state the --autotest harness prints
// ("atsum:" lines), so that two runs can be compared beyond the match results

uint32_t autoTestChecksum()
{
    uint32_t hash = 2166136261u;
    auto mix = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++)
            hash = (hash ^ bytes[i]) * 16777619u;
    };
    auto mixInt = [&mix](int value) { mix(&value, sizeof(value)); };
    auto mixFloat = [&mix](float value) { mix(&value, sizeof(value)); };

    const RandomNumberGenerator::State& rng = g_sim.rng.state();
    mix(rng.words.data(), sizeof(rng.words));
    mixInt(rng.index);

    for (const CPlayer* player : g_sim.players) {
        mixInt(player->getGlobalID());
        mixInt(player->leftX());
        mixInt(player->topY());
        mixFloat(player->getVelX());
        mixFloat(player->getVelY());
        mixInt(player->isdead());
    }

    for (short i = 0; i < g_sim.score_cnt; i++) {
        mixInt(g_sim.score[i].score);
        for (short subscore : g_sim.score[i].subscore)
            mixInt(subscore);
    }

    for (const CObjectContainer* container : {&g_sim.noncolcontainer, &g_sim.objectcontainer[0], &g_sim.objectcontainer[1], &g_sim.objectcontainer[2]}) {
        mixInt(static_cast<int>(container->list().size()));
        for (const std::unique_ptr<CObject>& obj : container->list()) {
            mixInt(obj->x());
            mixInt(obj->y());
        }
    }

    mixFloat(game_values.flags.gamewindx);
    mixInt(game_values.screenfade);
    mixInt(g_sim.countdownState);
    return hash;
}
} // namespace

//Must only be called after organizeteams() is called
short LookupTeamID(short id, short * teamID, short * subTeamID)
{
    for (short i = 0; i < g_sim.score_cnt; i++) {
        for (short j = 0; j < game_values.teamcounts[i]; j++) {
            if (game_values.teamids[i][j] == id) {
                if (teamID)
                    *teamID = i;

                if (subTeamID)
                    *subTeamID = j;

                return i;
            }
        }
    }

    if (teamID)
        *teamID = -1;

    if (subTeamID)
        *subTeamID = -1;

    return -1;
}

short LookupTeamID(short id)
{
    return LookupTeamID(id, nullptr, nullptr);
}

void SDLCALL musicfinished(void*, MIX_Track*)
{
    if (!game_values.music)
        return;

    if (game_values.appstate == AppState::Game && !game_values.gamemode->gameover)
        return;

    if (sfxMusic::fResumeMusic)
        rm->backgroundmusic[3].play(false, false);
}

short GetModeIconIndexFromMode(short iMode)
{
    if (iMode == game_mode_pipe_minigame)
        iMode = 25;
    else if (iMode == game_mode_boss_minigame)
        iMode = 26;
    else if (iMode == game_mode_boxes_minigame)
        iMode = 27;
    else if (iMode == game_mode_mapmaker)
        iMode = 23;    //(the hammer)

    return iMode;
}

short CountAliveTeams(short * lastteam)
{
    short findlastteam = 0;

    bool teamalive[4] = {false, false, false, false};
    for (CPlayer* player : g_sim.players) {
        if (!player->isdead())
            teamalive[player->teamID] = true;
    }

    short numteams = 0;
    for (short k = 0; k < 4; k++) {
        if (teamalive[k]) {
            findlastteam = k;
            numteams++;
        }
    }

    if (lastteam != nullptr) {
        if (numteams == 1)
            *lastteam = findlastteam;
        else
            *lastteam = -1;
    }

    return numteams;
}

void GameplayState::CleanDeadPlayers()
{
    bool fCheckForGameOver = false;

    auto* gmChicken = dynamic_cast<CGM_Chicken*>(game_values.gamemode);
    auto* gmTag = dynamic_cast<CGM_Tag*>(game_values.gamemode);

    for (size_t i = 0; i < g_sim.players.size(); i++) {
        if (g_sim.players[i]->state == PlayerState::Dead) {
            fCheckForGameOver = true;

            if (g_sim.respawnCount[g_sim.players[i]->globalID] <= 0)
                g_sim.players[i]->die(PlayerDeathStyle::Jump, true, false);

            //Set this to zero so we don't display a spawn egg when player is removed from game
            g_sim.respawnCount[g_sim.players[i]->globalID] = 0;

            if (gmTag && gmTag->tagged() == g_sim.players[i])
                gmTag->setTagged(nullptr);

            if (gmChicken && gmChicken->chicken() == g_sim.players[i])
                gmChicken->clearChicken();

            // Note: the player that moves into slot i is only checked next frame
            g_sim.players.erase(i);

            for (size_t j = i; j < g_sim.players.size(); j++)
                g_sim.players[j]->localID = j;
        }
    }

    if (fCheckForGameOver && game_values.gamemode->gamemode != game_mode_bonus && game_values.gamemode->gamemode != game_mode_boss_minigame) {
        short lastteam = -1;
        if (!game_values.gamemode->gameover && CountAliveTeams(&lastteam) <= 1) {
            game_values.gamemode->gameover = true;
            game_values.gamemode->winningteam = lastteam;
            SetupScoreBoard(true);  //pass true because the order the players died in matters
            ShowScoreBoard();
        }
    }
}

void CleanUp()
{
    closeOnlinePanel();
    //delete object list
    g_sim.players.clear();

    eyecandy[0].clean();
    eyecandy[1].clean();
    eyecandy[2].clean();
    spotlightManager.ClearSpotlights();

    g_sim.noncolcontainer.clean();

    g_sim.objectcontainer[0].clean();
    g_sim.objectcontainer[1].clean();
    g_sim.objectcontainer[2].clean();

    LoadMapObjects(true);
    g_map->clearWarpLocks();
    g_map->resetPlatforms();

    //Stop all game sounds
    sfx_stopallsounds();
    rm->sfx_invinciblemusic.stop();
    rm->sfx_slowdownmusic.stop();

    x_shake = 0;
    y_shake = 0;
}

void SetGameModeSettingsFromMenu()
{
    //If this is a tour stop and the tour has settings in it, use those.  Otherwise use the menu settings.
    if (game_values.tourstopcurrent < game_values.tourstops.size() && game_values.tourstops[game_values.tourstopcurrent]->fUseSettings &&
            (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::World))
        memcpy(&game_values.gamemodesettings, &game_values.tourstops[game_values.tourstopcurrent]->gmsSettings, sizeof(GameModeSettings));
    else
        memcpy(&game_values.gamemodesettings, &game_values.gamemodemenusettings, sizeof(GameModeSettings));
}

namespace {
bool g_firstFrameLogged = true;    //(loadlog: the first frame of a match, timed once)
}

void GameplayState::onEnterState()
{
    //(a crash report says what was played)
    platform::setCrashContext(std::format("{}: {}, {}", replay::playing() ? "A replay" : "A match",
        maplist ? maplist->currentShortmapname() : "", game_values.gamemode ? game_values.gamemode->GetModeName() : ""));
    //(low memory: the menus' graphics went as the match was set up; the match's sprites are loaded now,
    //not one by one as they're first drawn)
    if (platform::lowMemory()) {
        //(not the tilesets: the match's own are loaded by now; the previews' went at its setup)
        mapPreviewWaitForLoads();
        gfxSprite::unloadGroup(SpriteGroup::Menu);
        if (game_values.matchtype != MatchType::World) {
            gfxSprite::unloadGroup(SpriteGroup::World);
            rm->freeFullWorldGraphics();
        }
        gfxSprite::loadGroup(SpriteGroup::Game);
        gfxSprite::loadGroup(SpriteGroup::Shared);
    }
    g_firstFrameLogged = false;
    matchedit::reset();
    g_sim.countdownState = 0;
    g_sim.countdownTimer = 0;

    //(not the Map Maker's: it starts as you get there)
    const bool mapMaker = game_values.gamemode && game_values.gamemode->gamemode == game_mode_mapmaker;
    if (game_values.startgamecountdown && game_values.singleplayermode == -1 && !mapMaker) {
        g_sim.countdownState = 28;
        g_sim.countdownTimer = iCountDownTimes[0];
    }

    current_playerKeys = &game_values.playerInput.outputControls[0];
    previous_playerKeys = *current_playerKeys;

    initRunGame();

    //The wind step of the first tick (each tick ends with the next one's, see simulateTick)
    updateWind();

    lastUpdateTicked = false;

#ifdef _DEBUG
    exitgametimer = 0;
#endif
    autoTestFrames = 0;
    autoTestOverFrames = 0;
    replayEndFrames = 0;
    resetTheater();
    //(online, the match starts while the lobby is open)
    matchOnline = rollback::NetGame::instance().active() && !replay::playing();
}

// Every way out of a match passes here: the match goes in the history, with
// its replay (a replay itself leaves nothing)
void GameplayState::onLeaveState()
{
    if (replay::playing()) {
        replay::stopPlayback();
        //--replay with --autotest: done
        if (!cmd::g_replay.empty() && cmd::g_autotest.enabled)
            game_values.appstate = AppState::Quit;
        return;
    }
    //(the determinism test's matches aren't the player's: nothing kept of them)
    if (determinism::active()) {
        replay::cancelRecording();
        return;
    }
#ifdef SMW_HAS_EDITORS
    //(the editor's map, tried out: not a match of the history's)
    if (editors::testPlaying()) {
        replay::cancelRecording();
        return;
    }
#endif
    const std::string replayFile = replay::finishRecording();
    history::recordMatchEnd(replayFile, matchOnline);
}

void GameplayState::handleInput()
{
    //(the quit dialog and the online panel read the menus' keys: up, down, left, right, A, B)
    const short keysOf = game_values.flags.exitinggame || onlinePanelOpen() ? 1 : 0;
    game_values.playerInput.ClearPressedKeys(keysOf);
    //(the Map Maker's builders: their keys read as the menus' too)
    const bool mapMaker = matchedit::allowed();
    if (mapMaker)
        matchedit::beginFrame();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        gfx_eventtoscreen(&event);    //(the mouse in the screen's pixels at any window size)
        switch (event.type) {
        case SDL_EVENT_QUIT: {
            CleanUp();
            game_values.appstate = AppState::Quit;
            return;
        }
        break;

        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            JoystickManager::instance().handleEvent(event);
            break;

        case SDL_EVENT_KEY_DOWN: {
            if (event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                if (event.key.key == SDLK_F4) {
                    CleanUp();
                    game_values.appstate = AppState::Quit;
                    return;
                } else if (event.key.key == SDLK_RETURN) {
                    game_values.fullscreen = !game_values.fullscreen;
                    gfx_changefullscreen(game_values.fullscreen);
                    blitdest = screen;

                    //Continue with input -> don't feed this event to the input
                    //otherwise it will pause the game when switching to full/windowed screen
                    continue;
                }
            }
            if (event.key.key == SDLK_F1) {
                if (!replay::playing())    //(the theater's controls)
                    game_values.showfps = !game_values.showfps;
            } else if (event.key.key == SDLK_F3) {
                game_values.shownetstats = !game_values.shownetstats;    //only drawn online
            } else if (event.key.key == SDLK_ESCAPE) {
                game_values.playerInput.outputControls[0].game_cancel.fPressed = true;
            } else if (event.key.key == SDLK_TAB) {
                if (!matchedit::allowed())    //(the Map Maker: Tab switches building and playing)
                    PlayNextMusicTrack();
            } else if (event.key.key == SDLK_INSERT) {
                gfx_take_screenshot();
            }
#ifdef _DEBUG
            else if (rollback::NetGame::instance().inMatch() || replay::playing()) {
                //No debug keys online (they change the simulation on one peer only) or in replays
            }
            else if (event.key.key == SDLK_LEFTBRACKET) {
                game_values.framelimiter++;
            } else if (event.key.key == SDLK_RIGHTBRACKET) {
                if (game_values.framelimiter > 0)
                    game_values.framelimiter--;
            } else if (event.key.key == SDLK_F2) {
                game_values.frameadvance = !game_values.frameadvance;
            } else if (event.key.key == SDLK_F5) {
                game_values.autokill = !game_values.autokill;
            } else if (event.key.key == SDLK_Z) {
                for (CPlayer* player : g_sim.players) {
                    player->DeathAwards();

                    if (game_values.gamemode->playerkilledself(*player, KillStyle::Environment) == PlayerKillType::Normal)
                        player->die(PlayerDeathStyle::Jump, false, false);
                }
            } else if (event.key.key == SDLK_X) {
                short iplayer = RANDOM_INT(g_sim.players.size());
                g_sim.players[iplayer]->makefrozen(300);
            } else if (event.key.key == SDLK_C) {
                short iplayer = RANDOM_INT(g_sim.players.size());
                g_sim.players[iplayer]->shield.type = (PlayerShieldType)(RANDOM_INT(3) + 1);
                g_sim.players[iplayer]->shield.timer = 620;
            } else if (event.key.key == SDLK_1) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_IceWandPowerup(&rm->spr_icewandpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BobombPowerup(&rm->spr_bobombpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_StarPowerup(&rm->spr_starpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 4, true, 2, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_2) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_BombPowerup(&rm->spr_bombpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_PowPowerup(&rm->spr_powpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 8, true, 8, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_1uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 1));
            } else if (event.key.key == SDLK_3) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_PodoboPowerup(&rm->spr_podobopowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BulletBillPowerup(&rm->spr_bulletbillpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_2uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 2));
            } else if (event.key.key == SDLK_4) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_Tanooki({static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Green, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true, true, true, false));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_3uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 3));
            } else if (event.key.key == SDLK_5) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_PWingsPowerup(&rm->spr_pwingspowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Red, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, true, true, false));
                else
                    g_sim.objectcontainer[0].add(new PU_ExtraGuyPowerup(&rm->spr_5uppowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1, 5));
            } else if (event.key.key == SDLK_6) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_Spring(&rm->spr_spring, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Spiny, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, false, true, true));
                else
                    g_sim.objectcontainer[0].add(new PU_FirePowerup(&rm->spr_firepowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_7) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_ThrowBox(&rm->spr_throwbox, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, (RANDOM_INT(NUM_POWERUPS) + 3) - 3));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[1].add(new CO_Shell(ShellType::Buzzy, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, false, true, false, false));
                else
                    g_sim.objectcontainer[0].add(new PU_HammerPowerup(&rm->spr_hammerpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_8) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_Spike(&rm->spr_spike, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_ModPowerup(&rm->spr_modpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 8, true, 8, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_PoisonPowerup(&rm->spr_poisonpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_9) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[1].add(new CO_KuriboShoe(&rm->spr_kuriboshoe, {static_cast<short>(g_sim.players[0]->leftX() + 32), g_sim.players[0]->topY()}, true));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_FeatherPowerup(&rm->spr_featherpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_ClockPowerup(&rm->spr_clockpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_0) {
                if (event.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
                    g_sim.objectcontainer[0].add(new PU_LeafPowerup(&rm->spr_leafpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, 0, 30, 30, 1, 1));
                else if (event.key.mod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
                    g_sim.objectcontainer[0].add(new PU_BoomerangPowerup(&rm->spr_boomerangpowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
                else
                    g_sim.objectcontainer[0].add(new PU_MysteryMushroomPowerup(&rm->spr_mysterymushroompowerup, {static_cast<short>(g_sim.players[0]->leftX() + 32), static_cast<short>(g_sim.players[0]->topY() - 1)}, 1, true, 0, 30, 30, 1, 1));
            } else if (event.key.key == SDLK_F8) {
                g_fAutoTest = !g_fAutoTest;
            }
#endif
            break;
        }

        default:
            break;
        }

        //A replay: the theater's mouse and keys (GameplayTheater.cpp)
        if (replay::playing())
            theaterEvent(event);

        //Feed the player control structures with input data
        //Use menu controls when exit game dialog is up
        game_values.playerInput.Update(event, keysOf);
        if (mapMaker)
            matchedit::noteEvent(event);
    }
}

bool shouldUpdate()
{
    if (game_values.flags.pausegame || game_values.flags.exitinggame)
        return false;

    return true;
}

void start_gameplay()
{
    CleanUp();
    SetGameModeSettingsFromMenu();
    game_values.appstate = AppState::Game;

    if (game_values.music) {
        musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), "", "");
        rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
        rm->backgroundmusic[0].play(game_values.playnextmusic, false);
        replay::noteMusic();
    }
}

void end_gameplay()
{
    CleanUp();
    game_values.appstate = AppState::Menu;
    game_values.screenfadespeed = -8;
    GameStateManager::instance().changeStateTo(&MenuState::instance());
}

void GameplayState::update_countdown_timer()
{
    //Count down start timer before each game
    if (g_sim.countdownState > 0 && --g_sim.countdownTimer <= 0) {
        //There is one extra count here so we hit all the numbers.  When we
        //reach 0 that means we are done counting (index 28 would be out of
        //bounds on this array)
        if (--g_sim.countdownState != 0) {
            //28 is the magic number as there are 28 frames of animation
            //spread over a few seconds for the countdown numbers
            g_sim.countdownTimer = iCountDownTimes[28 - g_sim.countdownState];

            short countDownAnnounce = iCountDownAnnounce[28 - g_sim.countdownState];
            if (countDownAnnounce >= 0)
                rm->sfx_announcer[countDownAnnounce].play();
        }
    }
}

void update_playerswap()
{
    if (game_values.flags.swapplayers) {
        for (CPlayer* player : g_sim.players) {
            player->updateswap();
        }
    }
}

void GameplayState::update_world()
{
    shakeScreen();
    spinScreen();
    updateBulletBillPowerup();

    if (game_values.matchtype == MatchType::World &&
        game_values.gamemode->gameover &&
        game_values.flags.forceexittimer <= 0) {
        if (--game_values.flags.noexittimer <= 0)
            game_values.flags.noexit = false;
    }

    //------------- update objects -----------------------

#ifdef _DEBUG
    debugAutoKillEveryone();
#endif

    //Advance the cpu's turn (AI only calculates player's actions 1 out of 4 frames)
    if (++game_values.cputurn > 3)
        game_values.cputurn = 0;

    handleP2PCollisions();

    determinism::checkFpu("the start of the world's update");
    //Move platforms
    g_map->updatePlatforms();
    determinism::checkFpu("the platforms");

    game_values.flags.playskidsound = false;
    game_values.flags.playinvinciblesound = false;
    game_values.flags.playflyingsound = false;

    for (CPlayer* player : g_sim.players)
        player->move();    //move all objects before doing object-object collision detection in
    //->think(), so we test against the new position after object-map collision detection
    determinism::checkFpu("the players' moves");

    playSFX();
    determinism::checkFpu("the sounds");

    g_sim.noncolcontainer.update();
    g_sim.objectcontainer[0].update();
    g_sim.objectcontainer[1].update();
    g_sim.objectcontainer[2].update();
    determinism::checkFpu("the objects' updates");

    handleP2ObjCollisions();
    if (game_values.flags.swapplayers) {
        update_playerswap();
        return;
    }

    handleObj2ObjCollisions();

    //Commit all player actions at this point (after we have collided with any objects
    //that the player might have picked up)
    for (CPlayer* player : g_sim.players)
        player->CommitAction();

    cleanDeadNonPlayerObjects();
    CleanDeadPlayers();

    eyecandy[0].update();
    eyecandy[1].update();
    eyecandy[2].update();

    game_values.gamemode->think();

    if (game_values.flags.slowdownon != -1 && ++game_values.flags.slowdowncounter > 580) {
        game_values.flags.slowdownon = -1;
        game_values.flags.slowdowncounter = 0;
    }

    g_map->update();

    updateScreenShake();

    updateScoreboardAnimation();
}

// Opt-in --autotest harness: ends the match after a frame limit (or shortly
// after it is over). Returns true if it ended the match.
bool GameplayState::updateAutoTest()
{
    if (!cmd::g_autotest.enabled)
        return false;

    ++autoTestFrames;
    if (game_values.gamemode->gameover)
        ++autoTestOverFrames;
    //(--at-role matchedit: the editor in the match, worked by a script; it has the frame while editing)
    if (cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "matchedit" && autoTestEditScript(autoTestFrames))
        return true;
    if (cmd::g_autotest.everything && cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Single) {
        if (autoTestFrames == 240)
            game_values.flags.pausegame = true;
        if (autoTestFrames == 300)
            game_values.flags.pausegame = false;
    }

    //(--at-role testplay: the editor's map being played, on the screen)
    if (cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "testplay" && autoTestFrames == 90 &&
            !cmd::g_autotest.norender)
        gfx_save_screenshot(cmd::g_autotest.shotsDir + "/p2_playing.png");
    const bool autoTestEnd = autoTestFrames >= cmd::g_autotest.frames || autoTestOverFrames >= 120;
    if (autoTestEnd || autoTestFrames % 250 == 0)
        printf("atsum: frame %d %08x\n", autoTestFrames, autoTestChecksum());
    determinism::sample(g_autoTestMatchesDone, autoTestFrames, rollback::peerChecksum());
    //(the determinism test run without drawing, fast: how far it is, on a plain screen, now and then)
    if (determinism::active() && cmd::g_autotest.norender && autoTestFrames % 300 == 1 && screen) {
        SDL_FillSurfaceRect(screen, nullptr, SDL_MapSurfaceRGB(screen, 0, 40, 104));
        const std::string line = "Determinism test: match " + std::to_string(g_autoTestMatchesDone + 1) + " of " +
                                 std::to_string(cmd::g_autotest.matches);
        rm->menu_font_large.drawCentered(320, 220, line.c_str());
        rm->menu_font_small.drawCentered(320, 250, "Running without drawing, as fast as it goes");
        gfx_flipscreen();
    }
    if (cmd::g_autotest.peersum > 0 && (autoTestEnd || autoTestFrames % cmd::g_autotest.peersum == 0))
        printf("peersum: frame %d %016llx\n", autoTestFrames, static_cast<unsigned long long>(rollback::peerChecksum()));

    if (!autoTestEnd)
        return false;

    if (syncTest) {
        const rollback::SyncTest::Stats& st = syncTest->stats();
        auto us = [](double seconds, int64_t count) { return count > 0 ? seconds * 1e6 / static_cast<double>(count) : 0.0; };
        printf("synctest: match %d: %lld ticks, %lld re-simulated, %lld mismatches\n", g_autoTestMatchesDone + 1,
            static_cast<long long>(st.ticks), static_cast<long long>(st.resimulatedTicks), static_cast<long long>(st.mismatches));
        printf("synctest: save %.1f us, load %.1f us, hash %.1f us, peer checksum %.1f us; snapshot up to %zu bytes (arena %zu)\n",
            us(st.saveSeconds, st.saves), us(st.loadSeconds, st.loads), us(st.hashSeconds, st.hashes), us(st.peerChecksumSeconds, st.ticks),
            st.maxSnapshotBytes, st.maxArenaBytes);
        syncTest->resetStats();
    }

    if (game_values.gamemode->gameover && (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament))
        UpdateScoreBoard();

    if (cmd::g_autotest.aiBench)
        aibench::print();
    if (cmd::g_autotest.perf)
        perfstats::report(g_map->szLoadedFile, game_values.gamemode ? game_values.gamemode->gamemode : -1);

    CleanUp();
    game_values.flags.exitinggame = false;
    game_values.flags.pausegame = false;
    game_values.appstate = AppState::Menu;
    ++g_autoTestMatchesDone;
    printf("autotest: match %d ended after %d frames\n", g_autoTestMatchesDone, autoTestFrames);
    GameStateManager::instance().changeStateTo(&MenuState::instance());
    return true;
}

// Match start/end transitions happen between ticks, once the screen has faded
// to black (screenfade reaches 255 at the end of a tick). They are frame logic,
// not simulation: they tear down the match (CleanUp) and switch the app state.
// Returns true if the match ended.
bool GameplayState::updateMatchTransition()
{
    if (game_values.screenfade != 255)
        return false;

    if (game_values.appstate == AppState::StartGame) {
        start_gameplay();
        return true;
    } else if (game_values.appstate == AppState::EndGame) {
        end_gameplay();
        return true;
    }

    return false;
}

void GameplayState::update()
{
    lastUpdateTicked = false;
    //(run-ahead: back to the real state, before anything looks at it)
    leaveRunAhead();

    if (rollback::NetGame::instance().inMatch()) {
        updateNetplay();
        return;
    }

#ifdef _DEBUG
    debug_gameplay();
#endif

    if (replay::playing()) {
        updateReplay();
        return;
    }

    if (updateAutoTest())
        return;

    handleInput();

    //(the Map Maker: building, the right to build; before the pause and quit keys: Back builds)
    updateEditMode();

    if (updateExitPauseDialog(g_sim.countdownState)) {
        GameStateManager::instance().changeStateTo(&MenuState::instance());
        return;
    }

    //While paused, no ticks run
    if (shouldUpdate()) {
        if (updateMatchTransition())
            return;

        InputFrame input = gatherLocalInput(game_values.playerInput, game_values.playercontrol);
        //(the Map Maker's builders: their buttons in their bytes)
        for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++)
            if (game_values.playercontrol[slot] == 1)
                input.players[slot] = matchedit::inputByte(slot, slot, input.players[slot]);
        if (cmd::g_autotest.enabled && cmd::g_autotest.humans > 0)
            input = autoTestHumanInput();
        //The CPU players' buttons: their AI runs here, outside the simulation
        for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
            if (cpudriver::playsByInput(slot))
                input.players[slot] = cpudriver::next(slot);
        }

        if (cmd::g_autotest.enabled && cmd::g_autotest.synctest > 0) {
            if (!syncTest)
                syncTest = std::make_unique<rollback::SyncTest>(cmd::g_autotest.synctest);
            syncTest->runTick(input, [this](const InputFrame& tickInput) { advanceTick(tickInput); });
        } else if (cmd::g_autotest.resim) {
            interpolator.capture();
            //Debug check: suppressing the side effects must not change the simulation
            sim::ResimulationScope resim;
            simulateTick(input);
        } else {
            runRealTick(input);
        }
        lastUpdateTicked = true;
    }

    playMusic();
}

// --autotest online, in a tournament, tour or world: a match that the frame limit ended without a
// winner is won by the leading team (the first of them if tied), so that the series goes on. The
// state is the confirmed final one: every peer decides the same.
void GameplayState::autoTestDecideSeriesMatch()
{
    CGameMode* mode = game_values.gamemode;
    if (mode->gameover || mode->gamemode == game_mode_bonus || g_sim.score_cnt < 1)
        return;
    short leader = 0;
    bool tied = false;
    for (short t = 1; t < g_sim.score_cnt; t++) {
        if (g_sim.score[t].score > g_sim.score[leader].score) {
            leader = t;
            tied = false;
        } else if (g_sim.score[t].score == g_sim.score[leader].score) {
            tied = true;
        }
    }
    if (tied)
        g_sim.score[leader].score++;
    mode->gameover = true;
    mode->winningteam = leader;
    SetupScoreBoard(false);
    printf("autotest: the frame limit ended the match: team %d wins\n", leader);
}

// One frame of an online match (see rollback/NetGame.h). The session decides
// how many ticks run: usually one, none while it waits for the other peers,
// more after a rollback. The pause dialog is disabled online; leaving the
// match disconnects.
void GameplayState::updateNetplay()
{
    rollback::NetGame& net = rollback::NetGame::instance();

    handleInput();
    if (game_values.appstate == AppState::Quit) {
        net.leaveMatch("quit");
        return;
    }
    updateEditMode();    //(the Map Maker: Back builds)

    const bool quitTest = cmd::g_net.quitAt > 0 && g_sim.tick >= cmd::g_net.quitAt;
    if (updateExitPauseDialog(g_sim.countdownState) || quitTest) {
        net.leaveMatch(quitTest ? "--net-quit-at" : "the player left");
        if (quitTest)
            CleanUp();    //the dialog cleans up itself
        game_values.appstate = AppState::Menu;
        net.onMatchLeft();
        GameStateManager::instance().changeStateTo(&MenuState::instance());
        return;
    }

    //The local players use the first input devices, whatever their slots; the host's machine plays
    //the CPU players (their AI's buttons)
    InputFrame input;
    int device = 0;
    for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
        if (!net.isLocalSlot(slot))
            continue;
        if (game_values.playercontrol[slot] == 2) {
            input.players[slot] = cpudriver::next(slot);
            continue;
        }
        if (cmd::g_autotest.enabled && cmd::g_autotest.humans > 0)
            input.players[slot] = autoTestNetHumanInput(slot);
        else if (onlinePanelOpen())
            input.players[slot] = 0;    //(the online panel has the buttons: this computer's players stand still)
        else
            input.players[slot] = toInputByte(game_values.playerInput.outputControls[device]);
        if (!onlinePanelOpen())
            input.players[slot] = matchedit::inputByte(slot, device, input.players[slot]);    //(the Map Maker's builders)
        device++;
    }

    const rollback::NetGame::Outcome outcome = net.advance(input);
    lastUpdateTicked = outcome.ticks > 0;
    if (!outcome.ended && !outcome.failed && net.inMatch()) {
        //Run-ahead: the others' last known input, ours the newest
        InputFrame ahead = lastTickInput;
        for (int slot = 0; slot < INPUT_MAX_PLAYERS; slot++) {
            if (net.isLocalSlot(slot))
                ahead.players[slot] = input.players[slot];
        }
        runAhead(ahead);
    }

    if (outcome.ended || outcome.failed) {
        if (cmd::g_autotest.enabled) {
            ++g_autoTestMatchesDone;
            printf("autotest: match %d ended after %d frames\n", g_autoTestMatchesDone, g_sim.tick);
            if (outcome.ended && net.seriesActive())
                autoTestDecideSeriesMatch();
        }
        if (game_values.gamemode->gameover && (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament))
            UpdateScoreBoard();

        CleanUp();
        game_values.flags.exitinggame = false;
        game_values.flags.pausegame = false;
        game_values.appstate = AppState::Menu;
        game_values.screenfadespeed = -8;
        net.onMatchLeft();
        GameStateManager::instance().changeStateTo(&MenuState::instance());
        return;
    }

    playMusic();
}

void GameplayState::render(float alpha)
{
    //(the first frame's drawing: on the Xbox, the tilesets' images load as they're first drawn)
    struct FirstFrame {
        ~FirstFrame() {
            if (!g_firstFrameLogged) {
                g_firstFrameLogged = true;
                loadlog::mark("match: first frame drawn");
                if (g_autoTestLoadStartNs) {
                    const double ms = static_cast<double>(SDL_GetTicksNS() - g_autoTestLoadStartNs) / 1e6;
                    g_autoTestLoadStartNs = 0;
                    if (ms > g_autoTestSlowestLoadMs) {
                        g_autoTestSlowestLoadMs = ms;
                        g_autoTestSlowestLoadMap = maplist ? maplist->currentShortmapname() : "";
                    }
                }
            }
        }
    } firstFrame;
    for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
        iScoreTextOffset[iTeam] = 34 * game_values.teamcounts[iTeam] + 1;
    }

    //Paused (or the exit dialog up) on this machine: the match under the shade doesn't move, so it's
    //drawn and shaded once and that picture shown again, not the whole scene drawn and the screen
    //darkened pixel by pixel every frame (about 10 ms a frame on the Xbox: the pause dropped to 44 FPS)
    const bool paused = game_values.flags.pausegame || game_values.flags.exitinggame;
    const bool frozen = paused && !rollback::NetGame::instance().inMatch() && !replay::playing();
    static gfxSprite s_pausedPicture;
    static bool s_pausedPictureValid = false;
    if (!frozen)
        s_pausedPictureValid = false;

    //(the screen still has that picture from the last frame: only what's drawn over it is put back, the
    //dialogs' place and, while it's up, the Xbox debug overlay's; the whole screen is a megabyte a frame)
    //(the overlay's place the frame after it's closed too: its last picture is on the screen)
    static bool s_pausedPictureShown = false;
    static bool s_debugOverlayWasOn = false;
    const bool debugOverlayOn = prof::on();
    const SDL_Rect dialogs {124, 166, 392, 208};
    const SDL_Rect debugOverlay {0, 0, 360, App::screenHeight};
    const bool restored = frozen && s_pausedPictureValid && (s_pausedPictureShown
        ? gfx_copyrect(s_pausedPicture.getSurface(), blitdest, dialogs)
            && (!(debugOverlayOn || s_debugOverlayWasOn) || gfx_copyrect(s_pausedPicture.getSurface(), blitdest, debugOverlay))
        : gfx_copypixels(s_pausedPicture.getSurface(), blitdest));
    s_pausedPictureShown = restored;
    s_debugOverlayWasOn = debugOverlayOn;
    if (restored) {
        drawExitPauseDialog();
    } else {
        //Smooth motion: draw between the previous and the latest tick (not while paused)
        //(the theater: at every speed, see theaterAlpha)
        const bool smooth = game_values.smoothmotion && (replay::playing() ? theaterAlpha(alpha) : lastUpdateTicked && alpha < 1.0f);
        if (smooth)
            interpolator.apply(alpha);
        correctionSmoother.apply();

        matchedit::syncVisuals();    //(the Map Maker: the layer pictures follow the map)
        drawEverything(g_sim.countdownState, iScoreTextOffset);
        drawEditMode();    //(the Map Maker's builders' cursors and palettes)

        correctionSmoother.restore();
        if (smooth)
            interpolator.restore();

        if (paused) {
            rm->drawShade(App::menuTransparency);
            if (frozen && blitdest) {
                if (!s_pausedPicture || s_pausedPicture.getWidth() != blitdest->w || s_pausedPicture.getHeight() != blitdest->h
                    || s_pausedPicture.getSurface()->format != blitdest->format)
                    s_pausedPicture = gfxSprite(SdlSurfacePtr(SDL_CreateSurface(blitdest->w, blitdest->h, blitdest->format)), std::nullopt);
                s_pausedPictureValid = gfx_copypixels(blitdest, s_pausedPicture.getSurface());
            }
            drawExitPauseDialog();
        }
    }

    //(what's drawn over the match doesn't shake with it: an online match goes on behind its dialog)
    const short matchShakeX = x_shake;
    const short matchShakeY = y_shake;
    x_shake = 0;
    y_shake = 0;
    drawNetOverlay();    //online only; pure drawing
    drawOnlinePanel();
    drawReplayOverlay();
    x_shake = matchShakeX;
    y_shake = matchShakeY;

#ifdef _DEBUG
    if (g_fAutoTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Auto");
#endif
}

bool SwapPlayers(short iUsingPlayerID)
{
    //Count available players to switch with
    short iNumAvailablePlayers = 0;
    for (CPlayer* player : g_sim.players) {
        if (player->isready()) {
            iNumAvailablePlayers++;
            player->fOldSwapX = player->leftX();
            player->fOldSwapY = player->topY();

            player->iNewPowerupX = player->score->x + scorepowerupoffsets[game_values.teamcounts[player->teamID] - 1][player->subTeamID];
            player->iNewPowerupY = player->score->y + 25;
        }
    }

    if (iNumAvailablePlayers <= 1)
        return false;

    if (game_values.swapstyle != MysteryMushroomSwapStyle::Instant) {
        game_values.flags.swapplayers = true;
        game_values.flags.swapplayersposition = 0.0f;

        if (game_values.swapstyle == MysteryMushroomSwapStyle::Blink) {
            game_values.flags.swapplayersblink = false;
            game_values.flags.swapplayersblinkcount = 0;
        } else {
            game_values.screenfade = App::menuTransparency;
        }
    }

    short iIncrement = RANDOM_INT (iNumAvailablePlayers - 1);

    MysteryMushroomTempPlayer spots[4];
    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
        CPlayer* player  = g_sim.players[iPlayer];

        if (!player->isready())
            continue;

        short iNewSpot = iPlayer + iIncrement;

        do {
            if (++iNewSpot >= static_cast<short>(g_sim.players.size()))
                iNewSpot = 0;
        } while (spots[iNewSpot].fUsed || !g_sim.players[iNewSpot]->isready());

        spots[iNewSpot].fUsed = true;
        spots[iPlayer].SetPlayer(g_sim.players[iNewSpot], game_values.gamepowerups[g_sim.players[iNewSpot]->getGlobalID()]);

        //Give credit for deaths to the player that used the mystery mushroom
        if (iUsingPlayerID == iNewSpot) {
            player->iSuicideCreditPlayerID = g_sim.players[iNewSpot]->globalID;
            player->iSuicideCreditTimer = 62;
        }
    }

    for (size_t iPlayer = 0; iPlayer < g_sim.players.size(); iPlayer++) {
        CPlayer* player  = g_sim.players[iPlayer];

        if (!player->isready())
            continue;

        spots[iPlayer].GetPlayer(player, &game_values.gamepowerups[player->getGlobalID()]);

        if (game_values.swapstyle != MysteryMushroomSwapStyle::Blink)
            eyecandy[2].emplace<EC_SingleAnimation>(&rm->spr_fireballexplosion, static_cast<short>(player->fNewSwapX) + (HALFPW) - 16, static_cast<short>(player->fNewSwapY) + (HALFPH) - 16, 3, 8);

        if (game_values.swapstyle == MysteryMushroomSwapStyle::Instant) {
            player->setXf(player->fNewSwapX);
            player->setYf(player->fNewSwapY);

            if (player->carriedItem)
                player->carriedItem->MoveToOwner();
        }
    }

    return true;
}

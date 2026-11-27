#ifndef GAMESTATE_GAMEPLAY_H
#define GAMESTATE_GAMEPLAY_H

#include "GameplayCorrection.h"
#include "GameplayInterpolation.h"
#include "GameState.h"
#include "input.h"
#include "InputFrame.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rollback { class SyncTest; class Snapshot; }

// The gameplay state. Every 16 ms, update() runs the frame logic around the
// simulation: input gathering, the pause and exit dialogs, autotest
// bookkeeping and the match start/end transitions.
// Then it advances the simulation by one tick with simulateTick(). render()
// draws the result, as often as the frame rate setting asks for.
class GameplayState : public GameState
{
    public:
        void update() override;

        // Exactly one deterministic tick of gameplay (see GameplayTick.cpp)
        void simulateTick(const InputFrame& input);
        // Remembers the drawn positions for smooth motion, then runs the tick.
        // This is what rollback re-runs: the remembered positions are part of
        // the objects, so they are captured before every tick, re-simulated
        // or not.
        void advanceTick(const InputFrame& input);
        // Online, a player's peer is gone: the CPU plays these slots (bit s)
        // from the next tick on. Simulation state (rolled back with it);
        // repeating it changes nothing.
        void takeOverByCpu(uint8_t slotMask);

        // Draws the current state, `alpha` (0..1) of the way from the
        // previous tick to the latest one (smooth motion). Doesn't change the
        // simulation.
        bool rendersBetweenTicks() const override { return true; }
        void render(float alpha) override;

        // Run-ahead (GameplayTick.cpp): after the frame's tick, the match runs a few ticks further
        // with the same input, and that is what is drawn (and heard): the effect of a button shows
        // that many ticks sooner. The next frame goes back to the real state first.
        int runAheadFrames() const;
        void runAhead(const InputFrame& input);
        // Online: CPU slots (bit s) that players who joined the match play from this tick on
        void handOverToHumans(uint8_t slotMask);
        void leaveRunAhead();
        void runRealTick(const InputFrame& input);

        // Online: the corrections after a rollback, drawn smoothly (GameplayCorrection.h)
        CorrectionSmoother& correction() { return correctionSmoother; }

        static GameplayState& instance();

    private:
        GameplayState();
        ~GameplayState();

        void onEnterState() override;
        void onLeaveState() override;

        void createPlayers();
        void initScoreDisplayPosition();
        void initEyeCandy();
        void initRunGame();
        void CleanDeadPlayers();

        // The Map Maker's building (GameplayEdit.cpp)
        void updateEditMode();
        void drawEditMode();
        bool autoTestEditScript(int frame);    // --at-role matchedit (true: it had the frame)

        void handleInput();
        bool updateAutoTest();
        void updateNetplay();
        // A replay: the theater (GameplayTheater.cpp)
        void updateReplay();
        void leaveReplay();
        void drawReplayOverlay();
        bool theaterDrawsScores();
        void theaterEvent(const union SDL_Event& event);
        void resetTheater();
        bool theaterAlpha(float& alpha) const;
        void autoTestDecideSeriesMatch();
        bool updateMatchTransition();

        void applyTickInput(const InputFrame& input);
        void resetTickInput();
        void updateScreenFade();
        void updatePlayerSwapAnimation();

        void drawFrontLayer();
        void drawBackLayer();
        void drawMiddleLayer();

        void spinScreen();
        void drawScreenFade();
        void drawPlayerSwap();
        void drawScreenShakeBackground();
        void drawScoreboard(short iScoreTextOffset[4]);
        void drawWindMeter();
        void drawCountdown();
        void drawSpotlights();
        void drawOutOfScreenIndicators();
        void drawEverything(short iCountDownState, short iScoreTextOffset[4]);
        // Steps of the tick (GameplayTick.cpp runs them)
        void update_countdown_timer();
        void update_world();

        short iScoreTextOffset[4] = {};

        RenderInterpolator interpolator;
        CorrectionSmoother correctionSmoother;
        bool lastUpdateTicked = false;  // whether the latest update() ran a tick (not paused)
        int replayEndFrames = 0;        // frames since the replay's last tick
        bool matchOnline = false;       // the match is an online one (for the match history)

        std::unique_ptr<rollback::SyncTest> syncTest;    // --at-synctest

        std::unique_ptr<rollback::Snapshot> aheadReal;   // run-ahead: the real state, while the one ahead is shown
        bool aheadShown = false;                         // ... (it is)
        bool runningAhead = false;                       // the ticks run ahead now (not recorded in the replay)
        InputFrame lastTickInput {};                     // the input of the latest tick run

        short respawnanimationtimer[4];
        short respawnanimationframe[4];

        COutputControl* current_playerKeys = nullptr;
        COutputControl previous_playerKeys;

        static short scoreoffsets[3];
        static short g_iPowerupToIcon[8];

        //Vars that keep track of spinning the screen
        float spinangle;
        float spinspeed;
        short spindirection;
        short spintimer;

};

// Defined in GSGameplay.cpp
extern short g_iWinningPlayer;
extern short scorepowerupoffsets[3][3];
extern bool g_fAutoTest;
extern bool g_fRecordTest;
// Number of matches ended by the opt-in --autotest harness
extern int g_autoTestMatchesDone;
// --at-match everything: each stage's result, a line each (the test's report)
extern std::string g_autoTestStageReport;
// The test's match loads: when the one loading started (ns, 0: none), and the slowest so far (ms, its map)
extern uint64_t g_autoTestLoadStartNs;
extern double g_autoTestSlowestLoadMs;
extern std::string g_autoTestSlowestLoadMap;

// Defined in GSGameplay.cpp and separately in the world editor
short LookupTeamID(short id);

// Defined in GSGameplay.cpp
short CountAliveTeams(short* lastteam);
bool SwapPlayers(short iUsingPlayerID);
void UpdateScoreBoard();
void SetGameModeSettingsFromMenu();
void LoadMapObjects(bool fPreview);
// The block of the map at x, y made anew in the running match, the one there before taken out (the
// editor in the match)
void SetMapBlockAt(short x, short y);
// A map item's object put in the running match; and the objects of the items at x, y taken out of it
struct MapItem;
struct MapHazard;
void SpawnMapItem(const MapItem& item);
void RemoveMapItemObjectsAt(short x, short y);
// The hazards' objects gone and made again from `hazards` (the Map Maker's list, changed)
void RespawnMapHazards(const MapHazard* hazards, int count);
// The switches' lists of blocks made again from the blocks (a rollback restored them)
void RebuildSwitchBlocks();
// The match's pictures of the map's layers and its animated tiles (GSMenu.cpp)
void BuildMatchMapLayers();

// The Map Maker's building (GameplayEdit.cpp): players build the map while the others play it. The
// building is simulation (the builders' buttons go in their input bytes), so it plays online and rolls back.
struct InputFrame;
struct MapMakerBuilder;

namespace matchedit {
// A builder's hazard and platform settings as they start (each type's defaults)
void defaultSettings(MapMakerBuilder& b);
// The match's map just loaded: the Map Maker's blank map made of it, if its rules want one
void prepareMap();
// The pause screen (offline): its menu (Resume, who may build, Save, Quit), worked by every local
// player's buttons; drawn over the match
void pauseMenuInput();
void drawPauseMenu();
// (tests) a row of it chosen and pressed: 0 Resume, 1 the rights, 2 Save, 3 Quit
void pauseMenuPress(int row);
bool allowed();    // a Map Maker match played here (online too; not a replay, not the determinism test)
bool active();     // someone is building now
bool building(int slot);    // that player builds now
bool edited();     // this match's map was edited: it differs from its file
int hostSlot();    // the host's player: it builds always, it gives the rights
void reset();      // a new match (this machine's state)
// The builders' keys are read as the menus' (the match reads the game's): each frame, and its events
void beginFrame();
void noteEvent(const union SDL_Event& event);
// A local player's input byte: its builder's buttons while it builds, Back's start, the host's rights
// commands (`device`: the player's input device; `gameByte`: its byte as a player)
uint8_t inputByte(int slot, int device, uint8_t gameByte);
// The simulation's part, each tick before the players' input: the builders and the rights
void simulate(const InputFrame& input);
void afterLoadState();    // a rollback restored the level data: what's made from it again
void syncVisuals();       // each frame drawn: the layer pictures follow the map
uint32_t levelHash();     // the level data, for the checksums (0: not the Map Maker)
bool rights(int slot);
bool isHostHere();        // the host's player is at this machine's controls
// The host's panel (the pause screen, the online panel): a player's right, Save
void hostPanelInput(bool left, bool right, bool down, bool select);
bool sheetCoversScreen();
// The scoreboard's portrait of a builder (x, y: its 32 by 32): its cursor's glove, in place of the
// character (true: drawn)
bool drawBuilderPortrait(int slot, int x, int y);    // the controls sheet up, or shut since the last frame: the whole screen drawn again
void setPanelRow(int row);    // 0 the rights, 1 Save, 2 Clear map, -1 none (the online panel's other row)
void drawPauseRights(int y, bool withSave);
}

#endif // GAMESTATE_GAMEPLAY_H

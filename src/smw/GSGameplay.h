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

        void handleInput();
        bool updateAutoTest();
        void updateNetplay();
        // A replay: the theater (GameplayTheater.cpp)
        void updateReplay();
        void leaveReplay();
        void drawReplayOverlay();
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

#endif // GAMESTATE_GAMEPLAY_H

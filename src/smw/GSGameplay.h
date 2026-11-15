#ifndef GAMESTATE_GAMEPLAY_H
#define GAMESTATE_GAMEPLAY_H

#include "GameplayInterpolation.h"
#include "GameState.h"
#include "input.h"

#include <memory>

namespace rollback { class SyncTest; }

struct InputFrame;

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
        //.
        //..
        //...
        //TODO
        void update_countdown_timer();
        void update_world();

        short iScoreTextOffset[4] = {};

        RenderInterpolator interpolator;
        bool lastUpdateTicked = false;  // whether the latest update() ran a tick (not paused)
        int replayEndFrames = 0;        // frames since the replay's last tick
        bool matchOnline = false;       // the match is an online one (for the match history)

        std::unique_ptr<rollback::SyncTest> syncTest;    // --at-synctest

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

// Defined in GSGameplay.cpp and separately in the world editor
short LookupTeamID(short id);

// Defined in GSGameplay.cpp
short CountAliveTeams(short* lastteam);
bool SwapPlayers(short iUsingPlayerID);
void UpdateScoreBoard();
void SetGameModeSettingsFromMenu();
void LoadMapObjects(bool fPreview);

#endif // GAMESTATE_GAMEPLAY_H

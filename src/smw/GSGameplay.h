#ifndef GAMESTATE_GAMEPLAY_H
#define GAMESTATE_GAMEPLAY_H

#include "GameState.h"
#include "input.h"

class GameplayState : public GameState
{
    public:
        void update() override;

        static GameplayState& instance();

    private:
        GameplayState();

        void onEnterState() override;

        void createPlayers();
        void initScoreDisplayPosition();
        void initEyeCandy();
        void initRunGame();
        void CleanDeadPlayers();

        void handleInput();

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
        void read_network();
        void update_countdown_timer();
        void update_world();

        short iCountDownState = 0;
        short iCountDownTimer = 0;
        short iWindTimer = 0;
        float dNextWind = 0.0f;
        short iScoreTextOffset[4] = {};

        short respawnCount[4];
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

// Defined in GSGameplay.cpp and separately in the world editor
short LookupTeamID(short id);

// Defined in GSGameplay.cpp
short CountAliveTeams(short* lastteam);
bool SwapPlayers(short iUsingPlayerID);
void UpdateScoreBoard();
void SetGameModeSettingsFromMenu();
void LoadMapObjects(bool fPreview);

#endif // GAMESTATE_GAMEPLAY_H

#ifndef GAMESTATE_GAMEPLAY_INTERNAL_H
#define GAMESTATE_GAMEPLAY_INTERNAL_H

// Declarations shared between the translation units that implement the
// gameplay state (GSGameplay.cpp and Gameplay*.cpp). Not part of the public
// interface of GSGameplay.h.

constexpr int COUNTDOWN_START_INDEX = 4;

// The run-ahead (ticks, 0..RUN_AHEAD_MAX) of a setting: -1 is Auto, as much as the input delay (what
// hides the delay, and no more: past it, the run-ahead only guesses). A tick run ahead costs about
// what a tick does (10 us on a PC)
constexpr int RUN_AHEAD_MAX = 10;
constexpr int runAheadSetting(int setting, int inputDelay)
{
    const int ticks = setting < 0 ? inputDelay : setting;
    return ticks < 0 ? 0 : ticks > RUN_AHEAD_MAX ? RUN_AHEAD_MAX : ticks;
}

#ifdef _DEBUG
extern int exitgametimer;
#endif

// Defined in GSGameplay.cpp
short LookupTeamID(short id, short* teamID, short* subTeamID);
short GetModeIconIndexFromMode(short iMode);
void CleanUp();
void update_playerswap();

// Defined in GameplayTick.cpp
void updateWind();
void updateForceExitTimer();

// Defined in GameplayEffects.cpp
void checkWindEvent(short& iWindTimer, float& dNextWind);
void cleanDeadNonPlayerObjects();
void animateDuringCountdown();
void shakeScreen();
void updateScreenShake();
void updateBulletBillPowerup();
void playSFX();
void playMusic();
void PlayNextMusicTrack();

// Defined in GameplayCollisions.cpp
void handleP2PCollisions();
void handleP2ObjCollisions();
void handleObj2ObjCollisions();

// Defined in GameplayPause.cpp
void drawExitPauseDialog();
bool updateExitPauseDialog(short iCountDownState); // true on exit
// Online, the pause button opens a panel instead (the match goes on): ping, input delay, run-ahead
bool onlinePanelOpen();
void closeOnlinePanel();
void drawOnlinePanel();

// Defined in GameplayScoreboard.cpp
void updateScoreboardAnimation();

#ifdef _DEBUG
// Defined in GameplayDebug.cpp
void debugAutoKillEveryone();
void debug_gameplay();
#endif

#endif // GAMESTATE_GAMEPLAY_INTERNAL_H

#ifndef GAMESTATE_GAMEPLAY_INTERNAL_H
#define GAMESTATE_GAMEPLAY_INTERNAL_H

// Declarations shared between the translation units that implement the
// gameplay state (GSGameplay.cpp and Gameplay*.cpp). Not part of the public
// interface of GSGameplay.h.

constexpr int COUNTDOWN_START_INDEX = 4;

#ifdef _DEBUG
extern int exitgametimer;
#endif

// Defined in GSGameplay.cpp
short LookupTeamID(short id, short* teamID, short* subTeamID);
short GetModeIconIndexFromMode(short iMode);
void CleanUp();

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

// Defined in GameplayScoreboard.cpp
void updateScoreboardAnimation();

// Defined in GameplayNetwork.cpp
void network_send_local_input();
void network_broadcast_game_state();

#ifdef _DEBUG
// Defined in GameplayDebug.cpp
void debugAutoKillEveryone();
void debug_gameplay();
#endif

#endif // GAMESTATE_GAMEPLAY_INTERNAL_H

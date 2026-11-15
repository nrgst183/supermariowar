#ifndef AI_H
#define AI_H

#include <cstdio>
#include <map>

#include "Game.h"
#include "SimArena.h"

class CObject;

// AI comparisons in autotests (not simulation state): kills, deaths and self-deaths per player
namespace aibench {
extern int kills[4];
extern int deaths[4];
extern int selfDeaths[4];
extern int styleKills[32];
extern int stuck[4];         // turns pushing left/right without moving
extern int navTried[3];     // path moves: jump, drop, walk
extern int navReached[3];
void reset();
void print();
}
class CPlayer;
struct COutputControl;

class NearestObjects
{
public:
    NearestObjects() {
        Reset();
    }

    void Reset() {
        player = nullptr;
        goal = nullptr;
        stomp = nullptr;
        teammate = nullptr;
        threat = nullptr;

        playerdistance = App::screenWidth * 1000;
        goaldistance = App::screenWidth * 1000;
        stompdistance = App::screenWidth * 1000;
        teammatedistance = App::screenWidth * 1000;
        threatdistance = App::screenWidth * 1000;

        playerwrap = false;
        goalwrap = false;
        stompwrap = false;
        teammatewrap = false;
        threatwrap = false;
    }

    CPlayer * player;
    CPlayer * teammate;

    CObject * goal;
    CObject * stomp;
    CObject * threat;

    int playerdistance;
    int teammatedistance;
    int goaldistance;
    int stompdistance;
    int threatdistance;

    bool playerwrap;
    bool teammatewrap;
    bool goalwrap;
    bool stompwrap;
    bool threatwrap;
};

struct AttentionObject : sim::ArenaAllocated {
    int iID;	  //Global ID of this object
    short iType;  //Ignore it, high priority, etc.
    short iTimer;  //When it the attention expires, 0 for never
};

// Simulation state: lives in the simulation arena (see SimArena.h)
class CPlayerAI : public sim::ArenaAllocated
{
public:
    CPlayerAI();
    virtual ~CPlayerAI();

    virtual void Init();

    void SetPlayer(CPlayer * player) {
        pPlayer = player;
    }

    virtual void Think(COutputControl * playerKeys);

    void GetNearestObjects();
    void DistanceToObject(CObject * object, CObject ** target, int * nearest, bool * wrap);
    void DistanceToObjectCenter(CObject * object, CObject ** target, int * nearest, bool * wrap);
    void DistanceToPlayer(CPlayer * player, CPlayer ** target, int * nearest, bool * wrap);
    void AimWeapon(COutputControl * playerKeys, short actionType);
    void FightPlayer(COutputControl * playerKeys, CPlayer * player, bool wrap);
    // Follow the path toward (x,y) (see AINav.h); false when there is no path (the caller decides then)
    bool NavigateTo(COutputControl * playerKeys, int x, int y);
    bool NeedsPath(int x, int y) const;

protected:
    bool improved() const;
    bool improvedFighting() const;

    CPlayer * pPlayer = nullptr;

private:
    short iFallDanger = 0;
    // The path move in progress (AINav): kept while we're in the air
    short navCol = -1;
    short navRow = -1;
    short navMove = 0;
    short navLastCol = -1;    // where we stood when we chose that move
    short navLastRow = -1;
    bool navActive = false;   // this think followed a path
    NearestObjects nearestObjects;

    sim::map<int, AttentionObject*> attentionObjects;
    AttentionObject currentAttentionObject;
};


/**************************************************
* Inherit from the CPlayerAI class and create your own!
***************************************************/
class CSimpleAI : public CPlayerAI
{
public:
    void Think(COutputControl * playerKeys) override;

};

#endif // AI_H

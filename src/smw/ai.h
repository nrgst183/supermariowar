#ifndef AI_H
#define AI_H

#include <cstdio>
#include <map>

#include "Game.h"
#include "SimArena.h"

class MO_CarriedObject;
enum class PowerupType : unsigned char;
class CObject;
class RandomNumberGenerator;

// AI comparisons in autotests (not simulation state): kills, deaths and self-deaths per player
namespace aibench {
extern int kills[4];
extern int deaths[4];
extern int selfDeaths[4];
extern int styleKills[32];
extern int stuck[4];
enum ItemUse { ItemFeather, ItemLeaf, ItemPWings, ItemStatue, ItemKinds };
extern int itemUses[ItemKinds][4];   // times the CPU used these items
void count(ItemUse kind, int player);
extern int flyThinks[4];     // turns flying (the P-Wings)
extern int airThinks[4];     // turns in the air
extern int reversals[4];     // turns that reversed the direction         // turns pushing left/right without moving
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
        goalIsBlock = false;
        goalValue = -1;
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
    bool goalIsBlock;   // the goal is a ? block to hit from below (the spot under it: goalSpotRow)
    short goalValue;    // the goal is an item worth this much to us now (CPlayerAI::ItemValue), -1 if not one
    int goalSpotRow;
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
    // What Think() goes for this time (the nearest one, NearestObjects)
    enum Action : short { ACTION_FIGHT = 0, ACTION_GOAL = 1, ACTION_EVADE = 2, ACTION_TEAMMATE = 3, ACTION_STOMP = 4 };

    CPlayerAI();
    virtual ~CPlayerAI();

    virtual void Init();

    void SetPlayer(CPlayer * player) {
        pPlayer = player;
    }

    // The P-Wings' flight lasts while jump is held: the jump the game lets go of for a CPU coming
    // down (CPlayer::move) stays held for this one while it flies (its dives come down)
    bool keepsFlying() const { return improved(); }

    virtual void Think(COutputControl * playerKeys);

    // The random numbers the AI uses: the gameplay generator while it thinks inside the
    // simulation, `rng` while it plays by input from outside it (CpuDriver; nullptr: back)
    static void useRandom(RandomNumberGenerator* rng);
    // A star it threw can be taken back
    bool canTakeStar() const;
    // Played by input: it threw a star away, and leaves it until this tick
    void threwStar(int untilTick) { starThrowUntil = untilTick; }

    void GetNearestObjects();
    void ScanMiddleLayerObjects();
    void ScanBackLayerObjects();
    void ScanFrontLayerObjects();
    void ScanPlayers();
    void ScanPowerupBlocks();
    void DistanceToObject(CObject * object, CObject ** target, int * nearest, bool * wrap, int weightPct = 100);
public:
    /// What an item lying about is worth to this player now, 0..100 (-1: not an item): what it does
    /// (a star, a weapon, the attack items kept for later, a mode's own items), what we have already
    /// (the same weapon again only goes to the reserve) and the game mode (a heart only counts in
    /// Health). Bad ones (the secret items: wind, spinning screen, reversed controls) are worth 0.
    short ItemValue(const CObject* item) const;
    /// Where an item will be by the time we get there: a feather or leaf sways about the middle of
    /// its float, one sliding along keeps going
    void ItemAimPoint(const CObject* item, int& x, int& y) const;
private:
    void DistanceToObjectCenter(CObject * object, CObject ** target, int * nearest, bool * wrap);
    void DistanceToPlayer(CPlayer * player, CPlayer ** target, int * nearest, bool * wrap);
    void AimWeapon(COutputControl * playerKeys, Action actionType);
    void FightPlayer(COutputControl * playerKeys, CPlayer * player, bool wrap);
    // Follow the path toward (x,y) (see AINav.h); false when there is no path (the caller decides then)
    bool NavigateTo(COutputControl * playerKeys, int x, int y);
    bool NeedsPath(int x, int y) const;
    void Stabilize(COutputControl * playerKeys);
    bool AboutToBeStomped() const;
    void UseMovementItems(COutputControl * playerKeys, Action actionType, const NearestObjects& nearest);
    // Think()'s actions (ix, iy: where the player is; carriedItem: what it holds)
    void ChaseNearestPlayer(COutputControl * playerKeys, short ix, short iy, MO_CarriedObject * carriedItem);
    void HitGoalBlock(COutputControl * playerKeys, short ix, short iy);
    void GoForGoal(COutputControl * playerKeys, short ix, short iy, MO_CarriedObject * carriedItem);
    void EvadeThreat(COutputControl * playerKeys, short ix, short iy);
    void TagTeammate(COutputControl * playerKeys, short ix, short iy);
    void StompEnemy(COutputControl * playerKeys, short ix, short iy, MO_CarriedObject * carriedItem);
    void CheckFallDanger(COutputControl * playerKeys, short ix, short iy, short iDeathX1, short iDeathX2);
    void AvoidJumpingIntoDeath(COutputControl * playerKeys, short iy, short iDeathX1, short iDeathX2);
    void AvoidDeadlyLanding(COutputControl * playerKeys);
    void UseStoredPowerup(COutputControl * playerKeys, PowerupType powerup, MO_CarriedObject * carriedItem, short iTeamID);
    void CountBenchmarkStats(COutputControl * playerKeys);

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
    short navPlatform = -1;   // Board, Ride: the platform (AINav's index)
    bool navActive = false;   // this think followed a path
    // Stabilize(): where we were last turn, and getting unstuck
    short lastIx = -1000;
    short stuckThinks = 0;
    short unstickTimer = 0;
    short unstickDir = 0;
    NearestObjects nearestObjects;
    // Played by input (CpuDriver): it can't set the player's throw_star, so it waits this long
    // itself before taking a star it threw back
    int starThrowUntil = 0;

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

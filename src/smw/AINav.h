#pragma once

// Path finding for the CPU players over the map's tiles.
//
// The map is a grid of MAPWIDTH x MAPHEIGHT tiles. A "spot" is a tile a player can
// stand in: the tile itself is free and the one under it holds the player up. Spots are
// linked by the moves a player can make: walking, walking off an edge, dropping through
// a platform and jumping (up to a few tiles high and wide; 4 tiles up with a running jump: a tile of
// run-up, then up beside the ledge and onto it), warp pipes and riding moving platforms. The graph
// is rebuilt when the map's tiles, blocks or platforms change (a match starts, a block breaks).
//
// Nothing here is simulation state: the graph is computed from the map, the same way
// on every machine, and is not kept in snapshots.

class MovingPlatform;

namespace ainav {

enum class Move : unsigned char {
    None,   // we're there, or there is no way
    Walk,   // walk (or fall) toward the next column
    Jump,   // jump and steer toward the next column
    Drop,   // fall through the platform we stand on
    RunJump,// a running jump (run held, faster than walking): 4 tiles up beside a ledge 2 columns over
            // (dir: the way to it; a tile of run-up behind), then onto it
    Warp,   // into a warp pipe next to us (dir -1/1: walk into its side; 0: press down on it), out of
            // one of its exits (where its connection leads)
    Board,  // onto a moving platform: wait beside its way until it's in reach (BoardNow), then walk,
            // jump or drop onto it (board)
    Ride,   // stay on the platform we're on (it carries us toward where we get off)
};

// How a Board move gets onto the platform
enum BoardKind : unsigned char {
    BoardNone,
    BoardWalk,   // it comes level with our floor, beside us
    BoardJump,   // up onto it
    BoardDrop,   // off our ledge, down onto it
};

struct Step {
    Move move = Move::None;
    int toCol = -1;      // the spot the move leads to (Board, Ride: none)
    int toRow = -1;
    int dir = 0;         // -1 left, 1 right, 0 straight up/down (shortest way around the screen); RunJump: the run's
                         // way; Warp: into the pipe (0: down)
    int distance = 0;    // path cost to the target (rough number of moves)
    int platform = -1;   // Board, Ride: which platform (an index of ours, for BoardNow and PlatformAim)
    int point = -1;      // Board: the point of its way the path gets on at
    BoardKind board = BoardNone;
};

// The spot a player at (ix,iy) (top left) stands on, or lands on; false when none
bool PlayerSpot(int ix, int iy, int& col, int& row);

// Whether a player around height y1 or y2 (pixels) can cross the left/right screen edge: some maps
// close the edges with walls, there the way around the screen doesn't exist. A row counts when
// the edge tiles on both sides are free and is within a jump of either height.
bool CanCrossEdge(int y1, int y2);

// The horizontal distance dx = (to - from) the short way around the screen when the edge can be
// crossed near those heights, otherwise straight across; wrapped tells which one it is
int HorizontalDistance(int dx, int y1, int y2, bool* wrapped = nullptr);

// How many tiles a player at (ix,iy) (top left) can run in direction dir (-1/1) before a wall,
// up to max (the screen edge counts as a wall where the map closes it)
int FreeRun(int ix, int iy, int dir, int max = 5);

// The spot under a block at (col,row) from which a jump hits it (at most 3 tiles below), or -1
int SpotBelow(int col, int row);

// Whether a running jump starts from the spot (col,row) in direction dir: walking onto it, hold run
// (so we get there fast enough to take off)
bool RunJumpFrom(int col, int row, int dir);

// A jump up from (col,row) to (toCol,toRow): the column furthest toward the target we can already
// be in while rising (every tile from our row up to the target's is open), the target's own column
// when only a floor we can jump through holds it up. Moving there on the way up, rather than at
// the top, gets us onto ledges a jump barely clears (one 3 tiles up: the feet are over it for a few
// frames only).
int RiseColumn(int col, int row, int toCol, int toRow);

// The first move of the shortest path from (col,row) to the spot nearest to the point (x,y)
Step NextStep(int col, int row, int targetX, int targetY);

// Moving platforms (not the falling ones): each one's way is cut into points a short ride apart;
// a path gets on at a point (Board), rides from point to point (Ride) and gets off at one (Walk,
// Jump: from the platform to a spot), all worked out from the platforms' paths, so the same on
// every machine.

// NextStep for a player at (ix,iy) (top left) standing on a moving platform; Move::None when it
// isn't one we know or there is no way
Step NextStepFromPlatform(const MovingPlatform* platform, int ix, int iy, int targetX, int targetY);

// A Board step from the spot (col,row) toward (targetX,targetY): how to get on now, when the platform
// is in reach and the way on from where it is now is no longer than the path's; BoardNone: wait.
// aimX: where to steer (the player's left side)
BoardKind BoardNow(const Step& step, int col, int row, int targetX, int targetY, int& aimX);

// Where to be (the player's left side) to stand on the platform (an index from a Step), as near to
// ix as its deck allows, with the platform where it will be in ahead ticks; false if there's none
bool PlatformAim(int platform, int ix, int ahead, int& aimX);

// Whether a player at (ix,iy) (top left) is above the platform's deck (or near enough to steer onto
// it), with the platform where it will be in ahead ticks
bool OverPlatform(int platform, int ix, int iy, int ahead);

}

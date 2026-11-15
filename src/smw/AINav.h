#pragma once

// Path finding for the CPU players over the map's tiles.
//
// The map is a grid of MAPWIDTH x MAPHEIGHT tiles. A "spot" is a tile a player can
// stand in: the tile itself is free and the one under it holds the player up. Spots are
// linked by the moves a player can make: walking, walking off an edge, dropping through
// a platform and jumping (up to a few tiles high and wide). The graph is rebuilt when
// the map's tiles or blocks change (a match starts, a block breaks).
//
// Nothing here is simulation state: the graph is computed from the map, the same way
// on every machine, and is not kept in snapshots.

namespace ainav {

enum class Move : unsigned char {
    None,   // we're there, or there is no way
    Walk,   // walk (or fall) toward the next column
    Jump,   // jump and steer toward the next column
    Drop,   // fall through the platform we stand on
};

struct Step {
    Move move = Move::None;
    int toCol = -1;      // the spot the move leads to
    int toRow = -1;
    int dir = 0;         // -1 left, 1 right, 0 straight up/down (shortest way around the screen)
    int distance = 0;    // path cost to the target (rough number of moves)
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

// The first move of the shortest path from (col,row) to the spot nearest to the point (x,y)
Step NextStep(int col, int row, int targetX, int targetY);

}

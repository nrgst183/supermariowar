#pragma once

// Smooth motion: rendering more often than the simulation ticks draws moving
// things between their positions of the last two ticks. These types are
// render-only: the simulation never reads them.

// A drawable position (the float and the integer coordinates kept by players,
// objects and moving platforms)
struct RenderPosition {
    float fx = 0.0f;
    float fy = 0.0f;
    short ix = 0;
    short iy = 0;
};

// The position at the start of the latest tick
struct RenderPrevious {
    RenderPosition pos;
    bool valid = false;
};

// Moves larger than this between two ticks (warps, wrapping around the screen
// edge, respawns, teleports) are drawn at the new position, not in between
constexpr int RENDER_SNAP_DISTANCE = 32;

// The position to draw at, `alpha` (0..1) of the way from the previous to the
// current position
RenderPosition interpolatePosition(const RenderPrevious& previous, const RenderPosition& current, float alpha);

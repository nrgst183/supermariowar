#pragma once

#include "RenderInterpolation.h"

#include <vector>

class CObject;
class CPlayer;
class MovingPlatform;

// Smooth motion for the gameplay: draws players, objects and moving platforms
// between their positions of the last two ticks.
//
// Render-only: capture() runs before each tick and only fills the render-only
// RenderPrevious fields. apply() moves everything to its interpolated position
// right before drawing, and restore() puts back the exact simulated positions
// right after, so the simulation never sees an interpolated position.
class RenderInterpolator {
public:
    void capture();
    void apply(float alpha);
    void restore();

private:
    template <typename T>
    struct Saved {
        T* thing;
        RenderPosition position;
    };

    std::vector<Saved<CPlayer>> m_players;
    std::vector<Saved<CObject>> m_objects;
    std::vector<Saved<MovingPlatform>> m_platforms;
};

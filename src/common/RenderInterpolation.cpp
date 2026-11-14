#include "RenderInterpolation.h"

#include <cmath>
#include <cstdlib>


RenderPosition interpolatePosition(const RenderPrevious& previous, const RenderPosition& current, float alpha)
{
    if (!previous.valid || alpha >= 1.0f)
        return current;

    const RenderPosition& prev = previous.pos;
    if (std::abs(current.ix - prev.ix) > RENDER_SNAP_DISTANCE || std::abs(current.iy - prev.iy) > RENDER_SNAP_DISTANCE)
        return current;

    if (alpha < 0.0f)
        alpha = 0.0f;

    RenderPosition pos;
    pos.fx = prev.fx + (current.fx - prev.fx) * alpha;
    pos.fy = prev.fy + (current.fy - prev.fy) * alpha;
    pos.ix = static_cast<short>(prev.ix + std::lround((current.ix - prev.ix) * alpha));
    pos.iy = static_cast<short>(prev.iy + std::lround((current.iy - prev.iy) * alpha));
    return pos;
}

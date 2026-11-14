#include "doctest.h"

#include "RenderInterpolation.h"


TEST_CASE("interpolatePosition draws between the last two ticks") {
    const RenderPrevious previous {{10.0f, 20.0f, 10, 20}, true};
    const RenderPosition current {18.0f, 16.0f, 18, 16};

    const RenderPosition half = interpolatePosition(previous, current, 0.5f);
    CHECK(half.fx == doctest::Approx(14.0f));
    CHECK(half.fy == doctest::Approx(18.0f));
    CHECK(half.ix == 14);
    CHECK(half.iy == 18);

    const RenderPosition start = interpolatePosition(previous, current, 0.0f);
    CHECK(start.ix == 10);
    CHECK(start.iy == 20);

    const RenderPosition end = interpolatePosition(previous, current, 1.0f);
    CHECK(end.ix == 18);
    CHECK(end.iy == 16);
}

TEST_CASE("interpolatePosition snaps on large moves and without a previous position") {
    const RenderPosition current {600.0f, 100.0f, 600, 100};

    // wrapped around the screen edge
    const RenderPrevious wrapped {{2.0f, 100.0f, 2, 100}, true};
    CHECK(interpolatePosition(wrapped, current, 0.5f).ix == 600);

    // warped / respawned vertically
    const RenderPrevious warped {{600.0f, 400.0f, 600, 400}, true};
    CHECK(interpolatePosition(warped, current, 0.5f).iy == 100);

    // created during the latest tick
    const RenderPrevious none {};
    CHECK(interpolatePosition(none, current, 0.5f).ix == 600);
}

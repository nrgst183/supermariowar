#pragma once

#include "RenderInterpolation.h"

#include <array>

class CObject;
class CPlayer;

// Online: smooth corrections. When a late input of another player turns out
// to differ from the prediction, the session rolls back and re-simulates
// (rollback/RollbackSession.h): the players are then somewhere else than
// where they were just drawn, and would jump there. Instead, the difference
// is kept as an offset that is drawn on top of the corrected position and
// shrinks to nothing within a few ticks, so a correction glides.
//
// Render-only, like RenderInterpolator: it never changes the simulation. Big
// differences (a respawn, a warp, wrapping around the screen) are drawn at once,
// as are players that are not plainly running around (dying, spawning, warping).
class CorrectionSmoother {
public:
    // A rollback starts: remembers where every player is now (the state just drawn)
    void beforeRollback();
    // After each re-simulated tick: once back at the frame it started from, the offsets
    void afterResimulatedTick();
    // After every tick that is shown (not re-simulated): the offsets shrink
    void afterTick();
    void reset();

    // Around drawing: the players moved by their offsets, then put back
    void apply();
    void restore();

    // For the statistics: corrections smoothed and drawn at once
    int smoothed() const { return m_smoothed; }
    int snapped() const { return m_snapped; }

private:
    static constexpr int SLOTS = 4;
    struct Shown {
        bool valid = false;
        short x = 0, y = 0;
    };
    struct Offset {
        float x = 0, y = 0;
        bool active = false;
    };
    struct Saved {
        CPlayer* player = nullptr;
        RenderPosition position;
        CObject* held = nullptr;    // (what it carries moves with it)
        RenderPosition heldPosition;
    };

    int m_rollbackTick = -1;    // the frame the rollback started from (-1: none pending)
    std::array<Shown, SLOTS> m_shown {};
    std::array<Offset, SLOTS> m_offset {};
    std::array<Saved, SLOTS> m_saved {};
    int m_savedCount = 0;
    int m_smoothed = 0;
    int m_snapped = 0;
};

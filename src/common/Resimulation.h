#pragma once

// Side-effect gate for re-simulated gameplay ticks.
//
// Rollback netcode rewinds the simulation and runs already-shown ticks again
// within one rendered frame. Those ticks must produce the same simulation
// state, but none of the things the player already saw or heard the first
// time: sound effects, music changes, eyecandy, spotlights, announcer calls,
// screen shake. Code that triggers such a purely cosmetic effect checks
// sim::isResimulating() and skips the effect.
//
// The rule for everything gated here: skipping it must not change the
// simulation (no gameplay random numbers, no gameplay state).
namespace sim {

namespace detail {
inline bool resimulating = false;
} // namespace detail

inline bool isResimulating()
{
    return detail::resimulating;
}

// Marks the ticks run during its lifetime as re-simulated. Nests.
class ResimulationScope {
public:
    ResimulationScope()
        : m_previous(detail::resimulating)
    {
        detail::resimulating = true;
    }

    ~ResimulationScope()
    {
        detail::resimulating = m_previous;
    }

    ResimulationScope(const ResimulationScope&) = delete;
    ResimulationScope& operator=(const ResimulationScope&) = delete;

private:
    bool m_previous;
};

} // namespace sim

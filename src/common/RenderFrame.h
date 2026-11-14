#pragma once

// Rendering is decoupled from the 62.5 Hz simulation: with a high refresh rate
// the same tick can be drawn several times. Draw code that advances a purely
// cosmetic animation (a flapping cape, smoke puffs, spotlights fading in) does
// so only when render::advancesAnimations() is true, which is the case for the
// first render after a tick, so these animations keep their speed.
namespace render {

namespace detail {
inline bool advancesAnimations = true;
} // namespace detail

inline bool advancesAnimations()
{
    return detail::advancesAnimations;
}

inline void setAdvancesAnimations(bool advance)
{
    detail::advancesAnimations = advance;
}

} // namespace render

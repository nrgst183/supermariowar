#ifndef FPSLIMITER_H
#define FPSLIMITER_H

#include "GameValues.h"

#include <cstdint>

// Paces the main loop. The game ticks at a fixed rate (TICK_NS); rendering is
// decoupled from it and paced by the "Frame Rate" setting (FrameRateMode).
//
// Each main loop iteration:
//   const int ticks = limiter.frameStart();   // ticks due now (0..MAX_TICKS_PER_FRAME)
//   ...run `ticks` updates...
//   if (limiter.shouldRender(ticks, rendersBetweenTicks)) {
//       ...render with limiter.alpha()...
//       limiter.beforeFlip(); flip; limiter.afterFlip();
//   } else {
//       limiter.idle();                          // sleeps until something is due
//   }
//
// The --autotest harness runs unthrottled: one tick and one render per
// iteration (unless --at-realtime is given).
class FPSLimiter
{
    public:
        // The game was designed around 16 ms ticks (62.5 per second): movement,
        // physics and in-game timers (e.g. "62 frames = 1 second") assume it
        static constexpr uint64_t TICK_NS = 16'000'000;

        // When the game falls behind (slow machine, debugger break, window
        // drag), at most this many ticks are caught up at once; the rest of
        // the backlog is dropped instead of fast-forwarding
        static constexpr int MAX_TICKS_PER_FRAME = 5;

        int frameStart();
        bool shouldRender(int ticks, bool rendersBetweenTicks);
        // How far the current time is between the last tick and the next one (0..1)
        float alpha() const;

        void beforeFlip();
        void afterFlip();
        void idle();

        static FPSLimiter& instance();

    private:
        void applyFrameRateMode(FrameRateMode mode);
        void updateStats(uint64_t now);
        uint64_t tickNs() const;
        bool unthrottled() const;

        FrameRateMode   mode = FrameRateMode::VSync;
        bool            modeApplied = false;
        bool            vsyncActive = false;   // the renderer waits for the display
        uint64_t        renderPeriodNs = 0;    // paces renders when VSync is not available

        uint64_t        frameStartNs = 0;
        uint64_t        nextTickNs = 0;        // deadline of the next tick (drift-free schedule)
        uint64_t        nextRenderNs = 0;
        uint64_t        workNs = 0;            // time spent updating and drawing in the last iteration

        // Measured over the last second
        uint64_t        statsStartNs = 0;
        int             statsTicks = 0;
        int             statsRenders = 0;
        float           tickRate = 0.0f;
        float           renderRate = 0.0f;

        // disabled constructors
        FPSLimiter() = default;
};

#endif // FPSLIMITER_H

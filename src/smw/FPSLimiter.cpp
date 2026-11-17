#include "FPSLimiter.h"

#include "CmdArgs.h"
#include "gfx.h"
#include "RenderFrame.h"
#include "ResourceManager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>

namespace {
constexpr uint64_t NS_PER_SECOND = 1'000'000'000ull;

// Used when VSync is not available and the display's refresh rate is unknown
constexpr float FALLBACK_REFRESH_RATE = 60.0f;

const char* modeName(FrameRateMode mode)
{
    switch (mode) {
        case FrameRateMode::Uncapped: return "Uncapped";
        case FrameRateMode::TickRate: return "62.5";
        default: return "VSync";
    }
}

void delayUntil(uint64_t deadlineNs)
{
#ifndef __EMSCRIPTEN__  // the browser drives the loop
    const uint64_t now = SDL_GetTicksNS();
    if (now < deadlineNs)
        SDL_DelayPrecise(deadlineNs - now);
#endif
}
} // namespace


FPSLimiter& FPSLimiter::instance()
{
    static FPSLimiter instance;
    return instance;
}

bool FPSLimiter::unthrottled() const
{
    return cmd::g_autotest.enabled && cmd::g_autotest.realtime == 0;
}

uint64_t FPSLimiter::tickNs() const
{
#ifdef _DEBUG
    // Debug: slow the game down to 1 tick per second to see exact movement
    if (game_values.frameadvance)
        return NS_PER_SECOND;
#endif
    const uint64_t tick = lockedToDisplay ? renderPeriodNs : TICK_NS;
    return tickStretch == 1.0f ? tick : static_cast<uint64_t>(static_cast<double>(tick) * tickStretch);
}

void FPSLimiter::setTickStretch(float stretch)
{
    tickStretch = std::clamp(stretch, 1.0f, 1.25f);
}

void FPSLimiter::applyFrameRateMode(FrameRateMode newMode)
{
    mode = newMode;
    modeApplied = true;

    vsyncActive = gfx_setvsync(mode == FrameRateMode::VSync && !unthrottled());
    if (mode != FrameRateMode::VSync)
        vsyncActive = false;

#ifdef __EMSCRIPTEN__
    // The browser presents once per display refresh anyway
    vsyncActive = mode == FrameRateMode::VSync;
#endif

    // Without VSync, "VSync" renders are paced to the display's refresh rate
    const float refreshRate = gfx_refreshrate();
    renderPeriodNs = static_cast<uint64_t>(NS_PER_SECOND / (refreshRate > 0.0f ? refreshRate : FALLBACK_REFRESH_RATE));

    //The Xbox at about 60 Hz: one tick per refresh (ticks of 1/60 s, 4% longer than 16 ms). With the
    //62.5 ticks a second of the PCs, every 25th frame would run two ticks: a frame twice the work
    //(what can miss the refresh), and a jolt. Online, the PCs' ticks stretch to stay with it (the
    //time sync, setTickStretch). A frame that misses its refresh still catches up (two ticks).
    lockedToDisplay = false;
#ifdef SMW_XBOX
    lockedToDisplay = vsyncActive && refreshRate >= 58.0f && refreshRate <= 63.0f;
#endif
    nextTickNs = 0;

    printf("[gfx] Frame rate: %s (VSync %s, display %.2f Hz%s)\n", modeName(mode), vsyncActive ? "on" : "off", refreshRate,
        lockedToDisplay ? ", one tick per refresh" : "");
}

int FPSLimiter::frameStart()
{
    const FrameRateMode wantedMode = GetFrameRateMode(game_values.framelimiter);
    if (!modeApplied || wantedMode != mode)
        applyFrameRateMode(wantedMode);

    const uint64_t now = SDL_GetTicksNS();
    frameStartNs = now;
    updateStats(now);

    // The --autotest harness runs the game as fast as possible: one tick per iteration
    if (unthrottled()) {
        statsTicks++;
        return 1;
    }

    const uint64_t tick = tickNs();
    if (nextTickNs == 0)
        nextTickNs = now;

    //(locked to the display: the frames start a little after each refresh, early or late by some
    //microseconds; half a tick of slack makes that one tick, every time)
    const uint64_t slack = lockedToDisplay ? tick / 2 : 0;
    int ticks = 0;
    while (now + slack >= nextTickNs && ticks < MAX_TICKS_PER_FRAME) {
        nextTickNs += tick;
        ticks++;
    }

    // Too far behind: drop the backlog rather than fast-forward
    if (now + slack >= nextTickNs)
        nextTickNs = now + tick;

    statsTicks += ticks;
    return ticks;
}

float FPSLimiter::alpha() const
{
    //(locked to the display, what's drawn is the tick: nothing to interpolate)
    if (unthrottled() || mode == FrameRateMode::TickRate || lockedToDisplay)
        return 1.0f;

    const uint64_t tick = tickNs();
    const uint64_t lastTickNs = nextTickNs - tick;
    if (frameStartNs <= lastTickNs)
        return 0.0f;

    return std::min(1.0f, static_cast<float>(frameStartNs - lastTickNs) / static_cast<float>(tick));
}

bool FPSLimiter::shouldRender(int ticks, bool rendersBetweenTicks)
{
    // Draw-time cosmetic animations only advance on the first render after a tick
    render::setAdvancesAnimations(ticks > 0);

    if (unthrottled())
        return true;

    bool render = ticks > 0;
    if (!render && rendersBetweenTicks) {
        switch (mode) {
            case FrameRateMode::TickRate:
                break;
            case FrameRateMode::Uncapped:
                render = true;
                break;
            default:
                render = vsyncActive || frameStartNs >= nextRenderNs;
                break;
        }
    }

    return render;
}

void FPSLimiter::beforeFlip()
{
    workNs = SDL_GetTicksNS() - frameStartNs;

#if !_DEBUG
    if (game_values.showfps)
#endif
    {
        const float potential = workNs ? static_cast<float>(NS_PER_SECOND) / static_cast<float>(workNs) : 0.0f;
        rm->menu_font_large.draw(0, 480 - rm->menu_font_large.getHeight(),
            std::format("Render:{:.1f} ({}), Ticks:{:.1f}/{:.1f}, Potential:{:.0f}",
                renderRate, modeName(mode), tickRate, static_cast<float>(NS_PER_SECOND) / TICK_NS, potential));
    }
}

void FPSLimiter::afterFlip()
{
    statsRenders++;

    const uint64_t now = SDL_GetTicksNS();
    if (now >= nextRenderNs + renderPeriodNs)
        nextRenderNs = now + renderPeriodNs;  // resync after a stall
    else
        nextRenderNs += renderPeriodNs;

    // VSync that doesn't actually wait (some drivers ignore it): pace renders ourselves
    if (vsyncActive && renderRate > 0.0f && renderRate * renderPeriodNs > 1.5f * NS_PER_SECOND) {
        printf("[gfx] VSync doesn't limit the frame rate (%.1f FPS); pacing renders instead\n", renderRate);
        vsyncActive = false;
        lockedToDisplay = false;
    }
}

void FPSLimiter::idle()
{
    if (unthrottled())
        return;

    uint64_t deadline = nextTickNs;
    if (mode == FrameRateMode::VSync && !vsyncActive)
        deadline = std::min(deadline, nextRenderNs);

    delayUntil(deadline);
}

void FPSLimiter::updateStats(uint64_t now)
{
    if (statsStartNs == 0)
        statsStartNs = now;

    const uint64_t elapsed = now - statsStartNs;
    if (elapsed < NS_PER_SECOND)
        return;

    tickRate = static_cast<float>(statsTicks) * NS_PER_SECOND / static_cast<float>(elapsed);
    renderRate = static_cast<float>(statsRenders) * NS_PER_SECOND / static_cast<float>(elapsed);
    statsTicks = 0;
    statsRenders = 0;
    statsStartNs = now;

    if (cmd::g_autotest.enabled && cmd::g_autotest.realtime)
        printf("atfps: ticks %.2f/s renders %.1f/s (%s, VSync %s)\n", tickRate, renderRate, modeName(mode), vsyncActive ? "on" : "off");
}

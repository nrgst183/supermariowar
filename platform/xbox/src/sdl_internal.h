// Shared between the Xbox SDL3 compatibility layer's source files.
// Must not include Xbox headers (included by TUs that don't use them).
#pragma once

#include <SDL3/SDL.h>

// Events (sdl_events.cpp)
void sdlx_events_init();
void sdlx_events_quit();
void sdlx_push_event(const SDL_Event& event);

// Audio (sdl_mixer.cpp): delivers the "track stopped" callbacks of tracks that
// finished on the mixer thread, on the game's thread
void sdlx_mixer_dispatch_callbacks();

// Video (sdl_video.cpp)
void sdlx_video_quit();

// Time (sdl_core.cpp)
Uint64 sdlx_ticks_ns();

// Debug output: the debugger/xbWatson log, and stdout
void sdlx_log(const char* fmt, ...);

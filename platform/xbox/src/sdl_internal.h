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

// Files (sdl_core.cpp): a folder made (true if it's there), and the total size of the files in one
bool sdlx_make_dir(const char* path);
Uint64 sdlx_dir_bytes(const char* path);

// Files read and written with the system's calls, in big blocks (sdl_core.cpp). The C library's stdio
// goes through a 512-byte buffer: a big file read with fread is thousands of small reads, about
// 2 MB/s from the hard disk (the image cache: 32 MB at startup, 15 seconds). A file is nullptr if
// it can't be opened; reads and writes are all of `bytes` or false.
void* sdlx_open(const char* path, bool write);
Sint64 sdlx_size(void* file);
bool sdlx_read(void* file, void* dst, Uint64 bytes);
bool sdlx_write(void* file, const void* src, Uint64 bytes);
void sdlx_close(void* file);

// Load stats (SDL_XboxTakeLoadStats): the images' (sdl_image.cpp) and the sounds' (sdl_mixer.cpp)
extern SDL_XboxLoadStats g_sdlx_image_stats;
extern SDL_XboxLoadStats g_sdlx_sound_stats;

// Sets the x87 FPU to 53-bit (double) precision, so double maths gives the same
// results every time (game replays and rollback need that); floats use SSE.
// Called at startup and every frame, in case a library changed it.
void sdlx_set_fpu_precision();

// Debug output: the debugger/xbWatson log, and stdout
void sdlx_log(const char* fmt, ...);

// Profiler: where the frame time goes, shown on screen (L3 + R3 toggles it).
// Times are CPU cycles (rdtsc), summed until the overlay takes them each second.
struct SdlxProfile {
    volatile Uint64 blit = 0;      // SDL_BlitSurface / SDL_BlitSurfaceScaled
    volatile Uint64 fill = 0;      // SDL_FillSurfaceRect
    volatile Uint64 copy = 0;      // frame copy into the back buffer
    volatile Uint64 swap = 0;      // D3D swap (includes the wait for the vertical blank)
    volatile Uint64 mixer = 0;     // audio mixing and decoding (mixer thread)
    volatile Uint32 blits = 0;
    volatile Uint32 blit_pixels = 0;
};
extern SdlxProfile g_sdlx_profile;
extern bool g_sdlx_overlay;

inline Uint64 sdlx_cycles()
{
    return __builtin_ia32_rdtsc();
}

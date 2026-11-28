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

// Audio stopped for good (sdl_mixer.cpp): the mixer thread ended, the stream and DirectSound
// released, so the sound chip isn't left playing into memory the next program (the dashboard) uses
void sdlx_audio_shutdown();

// Video (sdl_video.cpp)
void sdlx_video_quit();
// The GPU's queued work finished and the last frame kept on screen: nothing left for it to do
// when the next program starts
void sdlx_video_shutdown();

// Leaving for the dashboard (sdl_core.cpp): the audio, the GPU and the network put to rest first
// (once), then XLaunchNewImage; an in-game reset's (Back + Start + both triggers, sdl_events.cpp)
// as well as the game's own exit
[[noreturn]] void sdlx_leave_to_dashboard();
// The back buffers wanted: 1 (double buffering), 2 (triple); the disc's --xbox-buffers N (sdl_main_xbox.cpp)
extern int g_sdlx_wanted_back_buffers;
extern bool g_sdlx_no_audio;
extern bool g_sdlx_fresh_cache;    // --xbox-fresh-cache: the cache drive formatted at the start (a first boot; tests)    // --xbox-no-audio: no DirectSound, no mixer thread (tests)

// The debug overlay (sdl_overlay.cpp): a frame begins presenting (the game's work is done), the
// present's own time (cycles), and the overlay drawn into the frame's pixels (when it's on)
void sdlx_overlay_frame();
void sdlx_overlay_presented(Uint64 cycles);
// (`bpp`: the pixels' size: 4 for X8R8G8B8, 2 for the screen's ARGB1555)
void sdlx_overlay_draw(Uint8* pixels, int pitch, int bpp);

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
// The log's last lines, oldest first (the crash screen shows them): up to `count`; returns how many
int sdlx_log_tail(const char** lines, int count);

// A crash (the exception filter, sdl_main_xbox.cpp; an error message box, the game's last catch):
// the error on the screen, with what the game was doing (SDL_XboxSetCrashContext), the memory
// and the log's last lines, until a controller's button; then the dashboard (sdl_video.cpp)
[[noreturn]] void sdlx_fatal(const char* title, const char* message);
// The crash screen's picture (sdl_overlay.cpp) and the game's last context (sdl_core.cpp)
void sdlx_fatal_draw(Uint8* pixels, int pitch, int bpp, const char* title, const char* message);
const char* sdlx_crash_context();
// Waits for a controller's button to be pressed, after all were released (sdl_events.cpp)
void sdlx_wait_for_button();

// Memory: the surfaces' pixels (sdl_surface.cpp) and the sounds' samples (sdl_mixer.cpp), in bytes,
// and the lowest free memory seen (sampled at each present and log line: sdl_core.cpp)
extern Uint64 g_sdlx_surface_bytes;
// Big blocks (64 KB or more) in pages of their own, given back when freed (sdl_memory.cpp: the C
// library's heap only grows, and fragments); C++'s new and delete use them too
extern "C" {
void* sdlx_malloc(size_t size);
void* sdlx_calloc(size_t count, size_t size);
void* sdlx_realloc(void* block, size_t size);
void sdlx_free(void* block);
}
// The live surfaces, named (an image's file; "blank WxH" otherwise): the crash and report screens
// list the biggest, to see where the memory is
void sdlx_surface_name(SDL_Surface* surface, const char* name);
// The `count` biggest, as "1200K name" lines; returns how many
int sdlx_biggest_surfaces(char (*lines)[64], int count);
// An ARGB8888 surface of these pixels (from sdlx_malloc) (malloc'ed, w * 4 bytes a row), which it takes over (and frees)
SDL_Surface* sdlx_adopt_pixels(int w, int h, void* pixels);
extern Uint64 g_sdlx_sound_bytes;
void sdlx_memory_sample();
// An error reported (SDL_SetError): counted for the test report, its first time logged
void sdlx_note_error(const char* text);
// The lowest free memory seen (bytes), for the readout
Uint64 sdlx_memory_lowest();
// The crash screen's picture with another heading (a report: the memory test's)
void sdlx_report_draw(Uint8* pixels, int pitch, int bpp, const char* heading, Uint32 color, const char* title, const char* message);

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

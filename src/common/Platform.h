#pragma once

// What differs between the platforms, in one place: the game calls these, and the Xbox's answers
// come from its SDL layer (platform/xbox), the others' are plain. No #ifdef SMW_XBOX at the calls.

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace platform {

/// Low memory: graphics loaded when needed and freed when not (the menus' in a match, the
/// match's in the menus, World mode's outside it, an editor's after its session, the tilesets of
/// the previews when a match starts; a map's second pair of layers only with animated tiles). On
/// by default on the Xbox (64 MB); elsewhere everything stays loaded, unless --low-memory (the same
/// paths, tested on a PC).
bool lowMemory();
void setLowMemory(bool on);

/// The screen's pixel format, and so the format of the surfaces drawn on and then to it: 16 bits on
/// the Xbox (ARGB1555, as the original game's screen), 32 elsewhere
SDL_PixelFormat screenFormat();

/// An image only drawn from, in less memory: a new surface (the old one stays the caller's), or
/// nullptr to keep it as it is (always, but on the Xbox: 16 bits)
SDL_Surface* compactSurface(SDL_Surface* surface);
/// The compact images kept on the cache drive (the Xbox's): `key` is the file and how it was loaded
SDL_Surface* loadCompactImage(const std::string& file, const std::string& key);
void saveCompactImage(const std::string& file, const std::string& key, SDL_Surface* surface);

/// What the game is doing, for a crash report (the Xbox's crash screen shows it)
void setCrashContext(const std::string& what);
/// A line in the log (the Xbox's: its crash screen shows the last ones; elsewhere stderr)
void log(const std::string& line);
/// The free memory and the images' memory, in bytes (0: not known here)
uint64_t freeMemory();
uint64_t imageBytes();

/// The floating point units as the simulation needs them (the Xbox's, settled every tick); what
/// they were is returned. False where there's nothing to do
bool settleFpu(unsigned short* x87Found, unsigned int* mxcsrFound);

/// The test's measurements (--at-match everything), since the last call: the lowest free memory,
/// the frames shown, the images decoded and read from a cache. known: false where not measured
struct StageStats {
    bool known = false;
    double lowestFreeMb = 0;
    int frames = 0;
    double averageMs = 0;
    double worstMs = 0;
    int slowFrames = 0;    // over 20 ms
    int decoded = 0;
    int cached = 0;
};
StageStats takeStageStats();
/// A line at the top of the screen: the memory, and `label` (the test's stage); the Xbox's
void setMemoryHud(bool on, const std::string& label);
/// The memory and the errors seen, as text (empty where not known)
std::string memoryReport();
std::string errorReport();
/// A report shown until a button is pressed where there's no console (the Xbox); printed elsewhere
void showReport(const std::string& heading, const std::string& title, const std::string& text);
/// The test's results stay on the screen (no console to print them to)
bool resultsStayOnScreen();

} // namespace platform

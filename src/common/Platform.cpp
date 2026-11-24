#include "Platform.h"

#include <cstdio>

namespace platform {

namespace {
#ifdef SMW_XBOX
bool g_lowMemory = true;
#else
bool g_lowMemory = false;
#endif
} // namespace

bool lowMemory()
{
    return g_lowMemory;
}

void setLowMemory(bool on)
{
    g_lowMemory = on;
}

#ifdef SMW_XBOX

SDL_PixelFormat screenFormat()
{
    return SDL_PIXELFORMAT_ARGB1555;
}

SDL_Surface* compactSurface(SDL_Surface* surface)
{
    return SDL_XboxCompactSurface(surface);
}

SDL_Surface* loadCompactImage(const std::string& file, const std::string& key)
{
    return SDL_XboxLoadCompactImage(file.c_str(), key.c_str());
}

void saveCompactImage(const std::string& file, const std::string& key, SDL_Surface* surface)
{
    SDL_XboxSaveCompactImage(file.c_str(), key.c_str(), surface);
}

void setCrashContext(const std::string& what)
{
    SDL_XboxSetCrashContext(what.c_str());
}

void log(const std::string& line)
{
    SDL_XboxLog(line.c_str());
}

uint64_t freeMemory()
{
    return SDL_XboxFreeMemory(nullptr);
}

uint64_t imageBytes()
{
    return SDL_XboxImageBytes();
}

bool settleFpu(unsigned short* x87Found, unsigned int* mxcsrFound)
{
    SDL_XboxSettleFpu(x87Found, mxcsrFound);
    return true;
}

StageStats takeStageStats()
{
    StageStats s;
    s.known = true;
    s.lowestFreeMb = static_cast<double>(SDL_XboxTakeStageLowest()) / (1024.0 * 1024.0);
    SDL_XboxTakeFrameStats(&s.frames, &s.averageMs, &s.worstMs, &s.slowFrames);
    SDL_XboxTakeImageCounts(&s.decoded, &s.cached);
    return s;
}

void setMemoryHud(bool on, const std::string& label)
{
    SDL_XboxSetMemoryHud(on, label.c_str());
}

std::string memoryReport()
{
    char text[700];
    SDL_XboxMemoryReport(text, sizeof(text));
    return text;
}

std::string errorReport()
{
    char text[1600];
    SDL_XboxErrorReport(text, sizeof(text));
    return text;
}

void showReport(const std::string& heading, const std::string& title, const std::string& text)
{
    SDL_XboxShowReport(heading.c_str(), title.c_str(), text.c_str());
}

bool resultsStayOnScreen()
{
    return true;
}

#else

SDL_PixelFormat screenFormat()
{
    return SDL_PIXELFORMAT_ARGB8888;
}

SDL_Surface* compactSurface(SDL_Surface*)
{
    return nullptr;
}

SDL_Surface* loadCompactImage(const std::string&, const std::string&)
{
    return nullptr;
}

void saveCompactImage(const std::string&, const std::string&, SDL_Surface*) {}

void setCrashContext(const std::string&) {}

void log(const std::string& line)
{
    fprintf(stderr, "%s\n", line.c_str());
}

uint64_t freeMemory()
{
    return 0;
}

uint64_t imageBytes()
{
    return 0;
}

bool settleFpu(unsigned short*, unsigned int*)
{
    return false;
}

StageStats takeStageStats()
{
    return {};
}

void setMemoryHud(bool, const std::string&) {}

std::string memoryReport()
{
    return {};
}

std::string errorReport()
{
    return {};
}

void showReport(const std::string& heading, const std::string& title, const std::string& text)
{
    printf("\n%s: %s\n%s\n", heading.c_str(), title.c_str(), text.c_str());
}

bool resultsStayOnScreen()
{
    return false;
}

#endif

} // namespace platform

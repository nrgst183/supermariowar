#include "LoadLog.h"

#include "path.h"
#include "Platform.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <utility>

namespace loadlog {
namespace {
Uint64 g_first = 0;
Uint64 g_last = 0;
bool g_started = false;

void write(const std::string& line)
{
    printf("[load] %s\n", line.c_str());
    //(the file: started again by each run)
    static FILE* file = [] {
        const std::string path = GetHomeDirectory() + "loadtimes.txt";
        return fopen(path.c_str(), "w");
    }();
    if (file) {
        fprintf(file, "%s\n", line.c_str());
        fflush(file);
    }
}

std::string ms(Uint64 ns)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu ms", static_cast<unsigned long long>(ns / 1000000));
    return buffer;
}

struct Counter {
    const char* what = nullptr;
    Uint64 times = 0;
    Uint64 ns = 0;
    Uint64 bytes = 0;
};
Counter g_counters[24];

void line(const char* what, Uint64 times, Uint64 ns, Uint64 bytes)
{
    char buffer[160];
    snprintf(buffer, sizeof(buffer), "    %s: %llu, %llu ms, %llu KB", what, static_cast<unsigned long long>(times),
        static_cast<unsigned long long>(ns / 1000000), static_cast<unsigned long long>(bytes / 1024));
    write(buffer);
}

// What count() summed, and (the Xbox) the system layer's load stats; then from 0 again
void writeCounters()
{
    for (Counter& c : g_counters) {
        if (c.what && c.times > 0)
            line(c.what, c.times, c.ns, c.bytes);
        c.times = c.ns = c.bytes = 0;
    }
#ifdef SMW_XBOX
    SDL_XboxLoadStats images {}, sounds {};
    SDL_XboxTakeLoadStats(&images, &sounds);
    for (const auto& [kind, s] : {std::pair {"images", images}, std::pair {"sounds", sounds}}) {
        if (s.sizeChecks + s.cacheHits + s.discReads == 0)
            continue;
        const std::string k = kind;
        line((k + " size checks on the disc").c_str(), s.sizeChecks, s.sizeCheckNs, 0);
        line((k + " from the cache drive").c_str(), s.cacheHits, s.cacheReadNs, s.cacheReadBytes);
        line((k + " read from the disc").c_str(), s.discReads, s.discReadNs, s.discReadBytes);
        line((k + " decoded").c_str(), s.decodes, s.decodeNs, 0);
        line((k + " written to the cache drive").c_str(), s.cacheWrites, s.cacheWriteNs, s.cacheWriteBytes);
        if (s.cacheWritesSkipped)
            line((k + " NOT written to the cache (full, or no drive)").c_str(), s.cacheWritesSkipped, 0, 0);
    }
#endif
}
} // namespace

unsigned long long nowNs()
{
    return SDL_GetTicksNS();
}

void count(const char* what, unsigned long long ns, unsigned long long bytes)
{
    for (Counter& c : g_counters) {
        if (!c.what)
            c.what = what;
        if (c.what == what) {
            c.times++;
            c.ns += ns;
            c.bytes += bytes;
            return;
        }
    }
}

void mark(const char* what)
{
    const Uint64 now = SDL_GetTicksNS();
    if (!g_started) {
        g_started = true;
        g_first = g_last = now;
    }
    std::string line = std::string(what) + "  " + ms(now - g_last) + "  (at " + ms(now - g_first) + ")";
    //(the memory where it's known: the Xbox's, each phase in its log too)
    if (const uint64_t free = platform::freeMemory()) {
        char memory[48];
        snprintf(memory, sizeof(memory), "  %.1f MB free, img %.1f", static_cast<double>(free) / (1024.0 * 1024.0),
                 static_cast<double>(platform::imageBytes()) / (1024.0 * 1024.0));
        line += memory;
        platform::log("[load] " + line);
    }
    write(line);
    writeCounters();
    g_last = now;
}

Scope::Scope(const char* what)
    : m_what(what)
    , m_start(SDL_GetTicksNS())
{}

Scope::~Scope()
{
    if (m_what) {
        write(std::string(m_what) + "  " + ms(SDL_GetTicksNS() - m_start));
        writeCounters();
    }
}

} // namespace loadlog

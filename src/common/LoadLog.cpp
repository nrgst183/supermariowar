#include "LoadLog.h"

#include "path.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

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
} // namespace

void mark(const char* what)
{
    const Uint64 now = SDL_GetTicksNS();
    if (!g_started) {
        g_started = true;
        g_first = g_last = now;
    }
    write(std::string(what) + "  " + ms(now - g_last) + "  (at " + ms(now - g_first) + ")");
    g_last = now;
}

Scope::Scope(const char* what)
    : m_what(what)
    , m_start(SDL_GetTicksNS())
{}

Scope::~Scope()
{
    if (m_what)
        write(std::string(m_what) + "  " + ms(SDL_GetTicksNS() - m_start));
}

} // namespace loadlog

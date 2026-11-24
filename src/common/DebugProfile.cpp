#include "DebugProfile.h"

#ifdef SMW_XBOX
#include <SDL3/SDL.h>

namespace prof {
namespace {
constexpr const char* k_sectionNames[] = {
    "UPDATE", "DRAW", "AI", "PATH GRAPH", "THEATER SEEK", "CHECKSUM", "SNAPSHOT SAVE", "SNAPSHOT LOAD", "MUSIC LOAD",
};
static_assert(sizeof(k_sectionNames) / sizeof(k_sectionNames[0]) == static_cast<int>(Section::Count));

constexpr const char* k_counterNames[] = {"TICKS", "OBJECTS", "EYECANDY", "PV TOTAL", "PV MAP", "PV BG", "PV DRAW", "PV TILES"};
static_assert(sizeof(k_counterNames) / sizeof(k_counterNames[0]) == static_cast<int>(Counter::Count));
} // namespace

bool on()
{
    return SDL_XboxProfileOn();
}

void add(Section section, uint64_t ns)
{
    const int id = static_cast<int>(section);
    SDL_XboxProfileAdd(id, k_sectionNames[id], section == Section::Update || section == Section::Draw, ns);
}

void count(Counter counter, int value)
{
    const int id = static_cast<int>(counter);
    SDL_XboxProfileCount(id, k_counterNames[id], value);
}

uint64_t nowNs()
{
    return SDL_GetTicksNS();
}

} // namespace prof
#endif

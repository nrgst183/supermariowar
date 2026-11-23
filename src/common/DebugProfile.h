#pragma once

// The game's timings for the Xbox debug overlay (L3 + R3: see platform/xbox/src/sdl_overlay.cpp):
// sections of each frame and counters. Measured only while the overlay is up; elsewhere (and on
// the other platforms) nothing is.

#include <cstdint>

namespace prof {

enum class Section : int {
    // The frame's parts (they don't overlap: the overlay's chart stacks them)
    Update,      // the ticks run this frame (the simulation, menus, the theater)
    Draw,        // drawing the frame
    // Inside those
    Ai,          // the CPU players' thinking
    NavBuild,    // the CPU players' path graph built again (a match starts, a block breaks)
    Seek,        // the theater jumping (a snapshot loaded and ticks run quietly)
    Checksum,    // a replay's or the network's state checksum
    SaveState,   // a rollback snapshot taken
    LoadState,   // a rollback snapshot loaded
    Music,       // a song loaded
    Count
};

enum class Counter : int {
    Ticks,       // the ticks run this frame
    Objects,
    Eyecandy,
    Count
};

#ifdef SMW_XBOX
bool on();
void add(Section section, uint64_t ns);
void count(Counter counter, int value);
uint64_t nowNs();

class Scope {
public:
    explicit Scope(Section section)
        : m_section(section)
        , m_start(on() ? nowNs() : 0)
    {}
    ~Scope()
    {
        if (m_start)
            add(m_section, nowNs() - m_start);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Section m_section;
    uint64_t m_start;
};
#else
inline bool on() { return false; }
inline void add(Section, uint64_t) {}
inline void count(Counter, int) {}
inline uint64_t nowNs() { return 0; }

class Scope {
public:
    explicit Scope(Section) {}
};
#endif

} // namespace prof

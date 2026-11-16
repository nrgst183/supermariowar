#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CGameMode;

// SAVING AND RESTORING THE SIMULATION (rollback netcode)
//
// saveState() copies everything that affects future ticks into a Snapshot;
// loadState() puts it back, so that the following ticks run exactly as they
// did after the save. A snapshot is a flat copy of memory, in two parts:
//
//  1. The simulation arena (common/SimArena.h): every player, object, AI,
//     moving platform and path, and the buffers of the simulation's
//     containers, with all the pointers between them. The arena never moves,
//     so copying its used bytes back restores the whole object graph. The
//     unused capacity of the object lists isn't copied: they reserve room
//     for 300 objects each (about half of a typical match's arena), and
//     nothing reads past a vector's size.
//
//  2. A fixed list of state outside the arena, copied byte for byte (see
//     forEachStateRegion() in Snapshot.cpp):
//     - g_sim (SimulationState.h): its containers' buffers are in the arena
//     - the runtime fields of game_values (flags, screen fade, app state,
//       powerup timers, secret counters, tournament scores, ...). The pause
//       and exit dialog flags in game_values.flags are frame logic: loading
//       keeps their current values.
//     - the runtime state of g_map (CMap::forEachRuntimeRegion)
//     - the active game mode object (game modes are registered with their
//       size when they are created, see registerGameMode())
//     - the next deterministic object id (CObject::nextNetworkId)
//
// Restoring copies bytes over live C++ objects that sit at the same address
// as when they were saved. That is what makes this cheap and complete; it
// relies on every byte that matters being inside the copied memory, which is
// the rule below.
//
// THE RULE FOR SIMULATION CODE
//  Simulation state must live in the simulation arena (allocate it from an
//  sim::ArenaAllocated class or a sim:: container), in g_sim, or in a region
//  listed in forEachStateRegion(). In particular:
//  - no `static` variables or globals that the simulation writes;
//  - no std:: containers or other heap memory owned by simulation objects;
//  - simulation objects may point at each other, at loaded resources (rm),
//    at g_map, g_sim and the game modes, but never at cosmetic things
//    (eyecandy, spotlights, sounds), which are not rolled back;
//  - cosmetic state that the tick advances (eyecandy, screen shake, tile
//    animation, ...) stays out of the snapshot and is not advanced while
//    sim::isResimulating() is set;
//  - drawing must not write into simulation objects (cosmetic animation
//    state that drawing advances lives outside, e.g. PlayerRenderState).
//  The --autotest --at-synctest N harness (SyncTest.h) finds violations.
//
// The byte checksums here (Snapshot::hash) are for comparing states on the
// same machine within the same run: the arena contains pointers. To compare
// states between machines, use peerChecksum() (StateChecksum.h).
namespace rollback {

class Snapshot {
public:
    struct Region {
        void* data = nullptr;
        size_t size = 0;
        size_t offset = 0;    // in bytes()
        const char* name = "";
    };

    bool empty() const { return m_regionCount == 0; }
    int tick() const { return m_tick; }

    // The saved bytes: the arena's spans, then the other regions
    size_t size() const { return m_size; }
    // The arena's used bytes (some of them not saved, see above), and those saved
    size_t arenaSize() const { return m_arenaUsed; }
    size_t arenaSaved() const { return m_arenaSaved; }
    const std::byte* bytes() const { return m_bytes.data(); }
    // The arena as it was saved, whole (the bytes not saved as zero): for comparing snapshots
    std::vector<std::byte> arenaBytes() const;

    size_t regionCount() const { return m_regionCount; }
    const Region& region(size_t index) const { return m_regions[index]; }

    int nextNetworkId() const { return m_nextNetworkId; }

    // 64-bit checksum of all saved bytes (same machine and run only)
    uint64_t hash() const;

private:
    friend void saveState(Snapshot& snapshot);
    friend void loadState(const Snapshot& snapshot);

    static constexpr size_t MAX_REGIONS = 48;

    // A run of arena bytes [offset, offset + size)
    struct Span {
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    std::vector<std::byte> m_bytes;    // grows to the largest state seen, then stays
    size_t m_size = 0;
    size_t m_arenaUsed = 0;
    size_t m_arenaSaved = 0;
    std::vector<Span> m_spans;    // the arena bytes saved, one after the other at the start of m_bytes
    std::array<Region, MAX_REGIONS> m_regions {};
    size_t m_regionCount = 0;
    int m_nextNetworkId = 0;
    int m_tick = 0;
};

void saveState(Snapshot& snapshot);
void loadState(const Snapshot& snapshot);

// Game modes hold their match state (timers, goals, pointers to objects)
// together with their settings. They are created once at startup; each one
// is registered here with the size of its class so that the active one can
// be saved.
void registerGameMode(const CGameMode* mode, size_t size);

// 64-bit checksum of a block of bytes (fast, not cryptographic)
uint64_t hashBytes(const void* data, size_t size, uint64_t seed = 0);

// Describes where two snapshots differ (for the sync test): the regions that
// differ and, for the arena, which objects the differing bytes belong to.
std::string describeDifference(const Snapshot& expected, const Snapshot& actual, size_t maxLines = 12);

} // namespace rollback

#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <new>
#include <vector>

// The simulation memory arena.
//
// Rollback netcode saves the whole simulation state every tick and restores it
// when a late input arrives. The simulation is a graph of polymorphic objects
// (players, ~150 object classes, the AI, moving platforms) that point at each
// other with raw pointers. Instead of serializing every class, everything the
// simulation allocates comes from this one arena: a fixed block of memory whose
// address never changes, with a deterministic allocator whose bookkeeping lives
// inside the block. A snapshot copies the used bytes of the arena; restoring
// them brings back every object, every container buffer, every pointer between
// them and the allocator's free lists, exactly as they were.
//
// WHAT ALLOCATES HERE
//  - every CObject, CPlayer, CPlayerAI, AttentionObject, MovingPlatform and
//    MovingPlatformPath (they derive from sim::ArenaAllocated)
//  - the buffers of the simulation's containers (sim::vector, sim::list,
//    sim::map), e.g. the object lists in g_sim and the AI's attention map
//
// THE RULE
//  An object that belongs to the simulation must not own memory from the
//  normal heap: no std::vector/std::string/std::map members that change during
//  a match, no SDL surfaces, no `new` of a type that is not ArenaAllocated.
//  Use the sim:: containers, or keep the data in fixed-size members. Cosmetic
//  things (eyecandy, spotlights, sounds) stay on the normal heap and must not
//  be pointed at from arena objects.
//
// The allocator is not thread-safe; the simulation is single-threaded.
namespace sim {

class Arena {
public:
    // The arena of the game (created on first use, never moves)
    static Arena& instance();

    explicit Arena(size_t capacity);
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(size_t size);
    // `size` must be the size passed to allocate()
    void deallocate(void* ptr, size_t size);

    bool contains(const void* ptr) const {
        const auto* p = static_cast<const std::byte*>(ptr);
        return p >= m_base && p < m_base + m_capacity;
    }

    // The bytes a snapshot has to copy: the bookkeeping and every block handed
    // out so far, live or free. Everything after them is zero.
    std::byte* base() const { return m_base; }
    size_t used() const;
    size_t capacity() const { return m_capacity; }

    // Restores the arena from `used` saved bytes, and zeroes what was used
    // after them, so that the memory beyond the used bytes is zero again (as
    // it is when the arena has never been rolled back).
    void restore(const std::byte* saved, size_t used);

    size_t liveBytes() const;
    size_t liveCount() const;
    // True once an allocation didn't fit and went to the normal heap: from
    // then on snapshots are incomplete. The capacity is chosen so that this
    // doesn't happen; it is a safety net, not an expected path.
    bool overflowed() const { return m_overflowed; }

private:
    struct Header;
    Header& header() const;

    std::byte* m_base = nullptr;
    size_t m_capacity = 0;
    bool m_overflowed = false;
};

// While one exists, the arena hands out normal heap memory instead (and takes
// it back: deallocate() frees what isn't in the block): for objects of the
// simulation's types that live outside it, the CPU players' AI that thinks on
// one machine (CpuDriver.h). Snapshots never see them.
class OutsideScope {
public:
    OutsideScope();
    ~OutsideScope();
    OutsideScope(const OutsideScope&) = delete;
    OutsideScope& operator=(const OutsideScope&) = delete;
};

// Base class for types whose objects live in the arena. The class-specific
// operator new/delete are inherited, and for classes with a virtual destructor
// the sized delete receives the size of the most derived type.
struct ArenaAllocated {
    static void* operator new(size_t size) { return Arena::instance().allocate(size); }
    static void operator delete(void* ptr, size_t size) { Arena::instance().deallocate(ptr, size); }
};

// Standard allocator for the simulation's containers
template <typename T>
struct ArenaAllocator {
    using value_type = T;
    using is_always_equal = std::true_type;

    ArenaAllocator() noexcept = default;
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>&) noexcept {}

    T* allocate(size_t count) {
        return static_cast<T*>(Arena::instance().allocate(count * sizeof(T)));
    }
    void deallocate(T* ptr, size_t count) noexcept {
        Arena::instance().deallocate(ptr, count * sizeof(T));
    }

    template <typename U>
    bool operator==(const ArenaAllocator<U>&) const noexcept { return true; }
};

template <typename T>
using vector = std::vector<T, ArenaAllocator<T>>;
template <typename T>
using list = std::list<T, ArenaAllocator<T>>;
template <typename K, typename V, typename Compare = std::less<K>>
using map = std::map<K, V, Compare, ArenaAllocator<std::pair<const K, V>>>;

} // namespace sim

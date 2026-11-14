#include "SimArena.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sim {

namespace {
// Blocks are handed out in size classes: multiples of 16 bytes up to 1 KiB,
// then powers of two. Freed blocks go to the free list of their class, so the
// arena only grows when no freed block of the right class is left.
constexpr size_t GRANULE = 16;
constexpr size_t SMALL_LIMIT = 1024;
constexpr size_t SMALL_CLASSES = SMALL_LIMIT / GRANULE;    // 64
constexpr size_t LARGE_CLASSES = 32;                       // 2 KiB .. 8 TiB
constexpr size_t NUM_CLASSES = SMALL_CLASSES + LARGE_CLASSES;

// The game's arena. A typical match uses 20-30 KiB (the four object lists
// reserve 2.4 KiB each, a player is ~400 bytes, an object ~100-300 bytes);
// even 1200 objects would stay far below this. calloc only commits the pages
// that are used.
constexpr size_t GAME_ARENA_CAPACITY = 2u << 20;

size_t sizeClass(size_t size)
{
    if (size == 0)
        size = 1;
    if (size <= SMALL_LIMIT)
        return (size + GRANULE - 1) / GRANULE - 1;

    size_t cls = SMALL_CLASSES;
    size_t blockSize = SMALL_LIMIT * 2;
    while (blockSize < size) {
        blockSize <<= 1;
        cls++;
    }
    return cls;
}

size_t classSize(size_t cls)
{
    if (cls < SMALL_CLASSES)
        return (cls + 1) * GRANULE;
    return (SMALL_LIMIT * 2) << (cls - SMALL_CLASSES);
}
} // namespace

// The bookkeeping lives at the start of the arena, so a snapshot of the used
// bytes includes it. Free list links are offsets from the arena base (0 = end
// of list), stored in the first bytes of each free block.
struct Arena::Header {
    uint64_t used;
    uint64_t liveBytes;
    uint64_t liveCount;
    uint64_t freeHead[NUM_CLASSES];
};

namespace {
constexpr size_t HEADER_SIZE = (sizeof(uint64_t) * (3 + NUM_CLASSES) + 63) / 64 * 64;
}

Arena& Arena::instance()
{
    static Arena arena(GAME_ARENA_CAPACITY);
    return arena;
}

Arena::Arena(size_t capacity)
    : m_capacity(capacity)
{
    static_assert(sizeof(Header) <= HEADER_SIZE);

    // calloc: zeroed, and the pages are only touched once the arena grows into them
    m_base = static_cast<std::byte*>(std::calloc(capacity, 1));
    if (!m_base) {
        fprintf(stderr, "[sim] Could not reserve %zu bytes for the simulation arena\n", capacity);
        std::abort();
    }
    header().used = HEADER_SIZE;
}

Arena::~Arena()
{
    std::free(m_base);
}

Arena::Header& Arena::header() const
{
    return *reinterpret_cast<Header*>(m_base);
}

size_t Arena::used() const
{
    return header().used;
}

size_t Arena::liveBytes() const
{
    return header().liveBytes;
}

size_t Arena::liveCount() const
{
    return header().liveCount;
}

void* Arena::allocate(size_t size)
{
    Header& h = header();
    const size_t cls = sizeClass(size);
    const size_t blockSize = classSize(cls);

    std::byte* block = nullptr;
    if (h.freeHead[cls] != 0) {
        block = m_base + h.freeHead[cls];
        uint64_t next;
        std::memcpy(&next, block, sizeof(next));
        h.freeHead[cls] = next;
    } else if (blockSize <= m_capacity - h.used) {
        block = m_base + h.used;
        h.used += blockSize;
    } else {
        if (!m_overflowed)
            fprintf(stderr, "[sim] The simulation arena is full (%zu bytes): rollback snapshots are incomplete from now on\n", m_capacity);
        m_overflowed = true;
        return ::operator new(size);
    }

    h.liveBytes += blockSize;
    h.liveCount++;
    return block;
}

void Arena::deallocate(void* ptr, size_t size)
{
    if (!ptr)
        return;
    if (!contains(ptr)) {
        ::operator delete(ptr);
        return;
    }

    Header& h = header();
    const size_t cls = sizeClass(size);

    const uint64_t next = h.freeHead[cls];
    std::memcpy(ptr, &next, sizeof(next));
    h.freeHead[cls] = static_cast<uint64_t>(static_cast<std::byte*>(ptr) - m_base);

    h.liveBytes -= classSize(cls);
    h.liveCount--;
}

void Arena::restore(const std::byte* saved, size_t savedUsed)
{
    const size_t currentUsed = used();
    std::memcpy(m_base, saved, savedUsed);
    if (currentUsed > savedUsed)
        std::memset(m_base + savedUsed, 0, currentUsed - savedUsed);
}

} // namespace sim

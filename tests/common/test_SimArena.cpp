#include "doctest.h"

#include "SimArena.h"

#include <cstring>
#include <vector>


TEST_CASE("Arena hands out aligned, distinct blocks and reuses freed ones") {
    sim::Arena arena(64 * 1024);

    void* a = arena.allocate(24);
    void* b = arena.allocate(24);
    void* c = arena.allocate(3000);
    CHECK(a != b);
    CHECK(arena.contains(a));
    CHECK(arena.contains(c));
    CHECK(reinterpret_cast<uintptr_t>(a) % 16 == 0);
    CHECK(reinterpret_cast<uintptr_t>(c) % 16 == 0);
    CHECK(arena.liveCount() == 3);

    // Same size class: the freed block comes back, the arena doesn't grow
    const size_t used = arena.used();
    arena.deallocate(b, 24);
    void* d = arena.allocate(20);
    CHECK(d == b);
    CHECK(arena.used() == used);

    arena.deallocate(a, 24);
    arena.deallocate(c, 3000);
    arena.deallocate(d, 20);
    CHECK(arena.liveCount() == 0);
    CHECK(arena.liveBytes() == 0);
}

TEST_CASE("Restoring the arena's used bytes brings back the objects and the allocator") {
    sim::Arena arena(64 * 1024);

    auto* first = static_cast<int*>(arena.allocate(sizeof(int) * 4));
    first[0] = 11;
    auto* second = static_cast<int*>(arena.allocate(sizeof(int) * 4));
    second[0] = 22;

    std::vector<std::byte> saved(arena.base(), arena.base() + arena.used());

    // Change the future: free one block, allocate others, overwrite data
    arena.deallocate(first, sizeof(int) * 4);
    auto* third = static_cast<int*>(arena.allocate(sizeof(int) * 4));
    CHECK(third == first);
    third[0] = 33;
    second[0] = 44;
    auto* big = static_cast<std::byte*>(arena.allocate(5000));
    std::memset(big, 0xab, 5000);
    const size_t usedAfter = arena.used();

    arena.restore(saved.data(), saved.size());

    CHECK(first[0] == 11);
    CHECK(second[0] == 22);
    CHECK(arena.used() == saved.size());
    CHECK(arena.liveCount() == 2);

    // Memory beyond the restored used bytes is zero again
    for (size_t i = saved.size(); i < usedAfter; i++)
        REQUIRE(arena.base()[i] == std::byte {0});

    // The allocator continues exactly as after the save: the next block is
    // the one the discarded future got first
    void* again = arena.allocate(sizeof(int) * 4);
    CHECK(again == static_cast<void*>(big));
}

TEST_CASE("Allocation is deterministic: the same calls give the same addresses") {
    sim::Arena one(64 * 1024);
    sim::Arena two(64 * 1024);

    auto run = [](sim::Arena& arena) {
        std::vector<size_t> offsets;
        std::vector<std::pair<void*, size_t>> live;
        const size_t sizes[] = {8, 40, 200, 1500, 16, 640, 4096, 24};
        for (int round = 0; round < 50; round++) {
            const size_t size = sizes[round % 8];
            void* p = arena.allocate(size);
            offsets.push_back(static_cast<size_t>(static_cast<std::byte*>(p) - arena.base()));
            live.emplace_back(p, size);
            if (round % 3 == 2) {
                arena.deallocate(live.front().first, live.front().second);
                live.erase(live.begin());
            }
        }
        return offsets;
    };

    CHECK(run(one) == run(two));
}

TEST_CASE("Simulation containers allocate from the game's arena") {
    sim::vector<int> numbers;
    numbers.reserve(100);
    CHECK(sim::Arena::instance().contains(numbers.data()));

    sim::list<int> list {1, 2, 3};
    CHECK(sim::Arena::instance().contains(&list.front()));

    struct Thing : sim::ArenaAllocated {
        int value = 5;
    };
    auto* thing = new Thing;
    CHECK(sim::Arena::instance().contains(thing));
    delete thing;
}

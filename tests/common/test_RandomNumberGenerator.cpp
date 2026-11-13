#include "doctest.h"

#include "RandomNumberGenerator.h"


TEST_CASE("RandomNumberGenerator is deterministic for a given seed") {
    RandomNumberGenerator a(1234);
    RandomNumberGenerator b(5678);
    b.seed(1234);

    for (int i = 0; i < 100; i++)
        CHECK(a.getInteger(1000) == b.getInteger(1000));
}

TEST_CASE("RandomNumberGenerator state can be saved and restored") {
    RandomNumberGenerator rng(42);
    for (int i = 0; i < 10; i++)
        rng.getInteger(100);

    const RandomNumberGenerator snapshot = rng;
    const RandomNumberGenerator::State state = rng.state();

    int first[20];
    for (int& value : first)
        value = rng.getInteger(-50, 50);

    RandomNumberGenerator restored(0);
    restored.setState(state);
    CHECK(restored == snapshot);

    RandomNumberGenerator copy = snapshot;
    for (int value : first) {
        CHECK(restored.getInteger(-50, 50) == value);
        CHECK(copy.getInteger(-50, 50) == value);
    }
}

TEST_CASE("RandomNumberGenerator ranges") {
    RandomNumberGenerator rng(7);
    for (int i = 0; i < 1000; i++) {
        const int v = rng.getInteger(5);
        CHECK(v >= 0);
        CHECK(v < 5);
        const int w = rng.getInteger(-3, 3);
        CHECK(w >= -3);
        CHECK(w < 3);
        CHECK(rng.getInteger(1) == 0);
    }
}

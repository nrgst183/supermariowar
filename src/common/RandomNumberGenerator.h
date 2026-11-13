#ifndef RANDOMNUMBERGENERATOR_H
#define RANDOMNUMBERGENERATOR_H

#include <array>
#include <cstdint>

// Every random number in the game comes from one of two RandomNumberGenerator
// instances:
//
//  - GAMEPLAY (RANDOM_INT / RANDOM_BOOL): everything that affects the
//    simulation or the match setup: spawns, AI decisions, powerups, item boxes,
//    game mode logic, map hazards, world map AI, random menu choices that end
//    up in the match configuration. Values stored inside simulated objects use
//    this generator too, even when they are only drawn (e.g. a colour), so that
//    two peers simulating the same inputs end up with identical object state.
//    In smw the instance lives in GameplayState (g_gs.rng) and is bound here at
//    startup; tools that have no GameplayState use a built-in fallback.
//
//  - COSMETIC (COSMETIC_RANDOM_INT / COSMETIC_RANDOM_BOOL): state that lives
//    outside the simulation and may differ between peers or between a
//    simulation and its re-simulation: eyecandy particles, background
//    decoration, menu/scoreboard animations, music and announcer picks, and
//    the debug automation scripts that fake input.
//
// When in doubt, use the gameplay generator.
#define RANDOM_INT(rMaxInt) RandomNumberGenerator::gameplay().getInteger(rMaxInt)
#define RANDOM_BOOL() RandomNumberGenerator::gameplay().getBoolean()
#define COSMETIC_RANDOM_INT(rMaxInt) RandomNumberGenerator::cosmetic().getInteger(rMaxInt)
#define COSMETIC_RANDOM_BOOL() RandomNumberGenerator::cosmetic().getBoolean()

// WELL512 random number generator. A plain value type: copying it copies the
// full generator state, so it can be saved and restored for snapshots.
class RandomNumberGenerator {
public:
    struct State {
        std::array<uint32_t, 16> words {};
        uint32_t index = 0;

        bool operator==(const State&) const = default;
    };

    // Seeds from the current time.
    RandomNumberGenerator();
    explicit RandomNumberGenerator(uint32_t seed);

    // Deterministic seeding: the same seed always produces the same sequence.
    void seed(uint32_t seed);
    // Seeds from the current time, mixed with `salt` so that generators seeded
    // within the same second still produce different sequences.
    void seedFromTime(uint32_t salt = 0);

    const State& state() const { return m_state; }
    void setState(const State& state) { m_state = state; }

    // generate uniformly distributed random number
    // rMax is not part of the set
    int getInteger(int rMax);
    // uniformly distributed in [rMin, rMax)
    int getInteger(int rMin, int rMax);

    bool getBoolean();

    // will return true only if 1/scaleMax is verified
    bool getBoolean(int scaleMax);

    // make a decision using 'scaleMax' steps and 'positiveThreshold' as threshold
    // returns true only when random value is above the threshold
    bool getBoolean(int scaleMax, int positiveThreshold);

    bool operator==(const RandomNumberGenerator&) const = default;

    // The generator used for all simulation randomness (see above).
    static RandomNumberGenerator& gameplay();
    // The generator used for all non-simulation randomness (see above).
    static RandomNumberGenerator& cosmetic();
    // Makes gameplay() return `rng` (not owned). Pass nullptr to go back to
    // the built-in fallback instance.
    static void bindGameplay(RandomNumberGenerator* rng);

private:
    /* return 32 bit random number */
    uint32_t getNext();

    State m_state;
};

#endif // RANDOMNUMBERGENERATOR_H

#include "RandomNumberGenerator.h"

#include <cassert>
#include <climits>
#include <ctime>

namespace {
// Salt for the cosmetic generator's time seed, so it does not mirror the
// gameplay generator when both are seeded in the same second.
constexpr uint32_t COSMETIC_SEED_SALT = 0x9E3779B9u;

RandomNumberGenerator*& boundGameplayRng()
{
    static RandomNumberGenerator* rng = nullptr;
    return rng;
}
} // namespace

//
// SEEDING
//

RandomNumberGenerator::RandomNumberGenerator()
{
    seedFromTime();
}

RandomNumberGenerator::RandomNumberGenerator(uint32_t seedValue)
{
    seed(seedValue);
}

void RandomNumberGenerator::seed(uint32_t seedValue)
{
    m_state.index = 0;
    m_state.words[0] = seedValue;
    for (size_t i = 1; i < m_state.words.size(); i++)
        // see Linear congruential generator
        m_state.words[i] = ((m_state.words[i-1] * 1103515245) + 12345) & 0x7fffffff;
}

void RandomNumberGenerator::seedFromTime(uint32_t salt)
{
    seed(static_cast<uint32_t>(time(nullptr)) ^ salt);
}

//
// GENERATION
//

uint32_t RandomNumberGenerator::getNext()
{
    auto& state = m_state.words;
    uint32_t& index = m_state.index;

    uint32_t a, b, c, d;
    a = state[index];
    c = state[(index+13)&15];
    b = a^c^(a<<16)^(c<<15);
    c = state[(index+9)&15];
    c ^= (c>>11);
    a = state[index] = b^c;
    d = a^((a<<5)&0xDA442D20UL);
    index = (index + 15)&15;
    a = state[index];
    state[index] = a^b^d^(a<<2)^(b<<18)^(c<<28);
    return state[index];
}

int RandomNumberGenerator::getInteger(int rMin, int rMax)
{
    assert(rMax > rMin);
    int rVal = (static_cast<double>(getNext()) / ((static_cast<float>(UINT_MAX))+1)) * (rMax-rMin) + rMin;
    assert(rVal < rMax && rVal >= rMin);
    return rVal;
}

// generate uniformly distributed random number
// rMax is not part of the set
int RandomNumberGenerator::getInteger(int rMax)
{
    return getInteger(0, rMax);
}

bool RandomNumberGenerator::getBoolean()
{
    return getBoolean(2);
}

// will return true only if 1/scaleMax is verified
bool RandomNumberGenerator::getBoolean(int scaleMax)
{
    // maybe we shall match center of range, like (scaleMax/2 - 1) ?
    return 0 == getInteger(scaleMax);
}

// make a decision using 'scaleMax' steps and 'positiveThreshold' as threshold
// returns true only when random value is above the threshold
bool RandomNumberGenerator::getBoolean(int scaleMax, int positiveThreshold)
{
    assert(positiveThreshold < scaleMax && positiveThreshold >= 0);
    return getInteger(scaleMax) > positiveThreshold;
}

//
// GLOBAL ACCESS
//

RandomNumberGenerator& RandomNumberGenerator::gameplay()
{
    if (RandomNumberGenerator* rng = boundGameplayRng())
        return *rng;

    static RandomNumberGenerator fallback;
    return fallback;
}

RandomNumberGenerator& RandomNumberGenerator::cosmetic()
{
    static RandomNumberGenerator rng = [] {
        RandomNumberGenerator r(0);
        r.seedFromTime(COSMETIC_SEED_SALT);
        return r;
    }();
    return rng;
}

void RandomNumberGenerator::bindGameplay(RandomNumberGenerator* rng)
{
    boundGameplayRng() = rng;
}

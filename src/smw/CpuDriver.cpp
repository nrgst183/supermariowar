#include "CpuDriver.h"

#include "ai.h"
#include "GameValues.h"
#include "InputFrame.h"
#include "input.h"
#include "player.h"
#include "RandomNumberGenerator.h"
#include "SimArena.h"
#include "SimulationState.h"
#include "rollback/Replay.h"

#include <array>

extern CGameValues game_values;

namespace cpudriver {
namespace {

constexpr uint8_t TURBO = InputBit::Turbo;

struct Slot {
    bool valid = false;
    int tick = -1;           // the tick `byte` is for
    uint8_t byte = 0;        // the last byte
    uint8_t pending = 0;     // to hold (and press) next tick: a key let go of for a tick first
    CPlayerAI* ai = nullptr; // its AI (normal heap memory: not simulation state)
};

std::array<Slot, INPUT_MAX_PLAYERS> g_slots;
RandomNumberGenerator g_random(1);

CPlayer* playerOf(int slot)
{
    for (CPlayer* player : g_sim.players) {
        if (player->getGlobalID() == slot)
            return player;
    }
    return nullptr;
}

}  // namespace

void reset()
{
    for (Slot& st : g_slots)
        delete st.ai;
    g_slots = {};
    g_random.seed(RandomNumberGenerator::gameplay().state().words[0] ^ 0x5eed1234u);
}

bool playsByInput(int slot)
{
    return slot >= 0 && slot < INPUT_MAX_PLAYERS && game_values.playercontrol[slot] == 2 && replay::cpuInputs() &&
           !(g_sim.cpuTakeover & (1u << slot)) && !(g_sim.humanTakeover & (1u << slot));
}

uint8_t next(int slot)
{
    if (!playsByInput(slot))
        return 0;
    Slot& st = g_slots[static_cast<size_t>(slot)];
    if (st.valid && st.tick == g_sim.tick)
        return st.byte;    // decided already (the state hasn't moved on)
    if (st.valid && g_sim.tick < st.tick) {
        delete st.ai;      // a new match
        st = Slot();
    }

    //Between thinks: the same keys held, nothing pressed (and what waited a tick)
    uint8_t out = static_cast<uint8_t>((st.byte & InputBit::MASK) | st.pending);
    st.pending = 0;

    //It thinks in the tick that moves cputurn to it, if it's in play (CPlayer::move)
    CPlayer* player = playerOf(slot);
    const bool thinks = player && player->hasAI() && player->isready() && (game_values.cputurn + 1) % 4 == slot;
    if (thinks) {
        const COutputControl& now = g_sim.playerKeys[slot];
        COutputControl keys = now;
        for (CKeyState& key : keys.keys)
            key.fPressed = false;

        //Its own random numbers, and the player as it was (it may only change its buttons)
        bool unlockFire = false;
        sim::OutsideScope outside;
        if (!st.ai) {
            st.ai = new CPlayerAI();
            st.ai->SetPlayer(player);
            st.ai->Init();
        }
        CPlayerAI::useRandom(&g_random);
        player->thinkByInput(*st.ai, keys, unlockFire);
        CPlayerAI::useRandom(nullptr);

        const uint8_t previous = st.byte;
        out = 0;
        for (int k = 0; k < InputBit::COUNT; k++) {
            const uint8_t bit = static_cast<uint8_t>(1u << k);
            if (!keys.keys[k].fDown)
                continue;
            if (!now.keys[k].fDown && (previous & bit))
                st.pending |= bit;    //the simulation let go of it: let go too, hold it again next tick
            else
                out |= bit;
        }
        if (keys.game_jump.fPressed)
            out |= InputBit::JumpPress;
        if (keys.game_turbo.fPressed) {
            if (unlockFire && (previous & TURBO)) {
                //Fire again: a tick without turbo first (that unlocks it), then the press
                out &= static_cast<uint8_t>(~TURBO);
                st.pending |= TURBO | InputBit::TurboPress;
            } else {
                out |= InputBit::TurboPress;
            }
        } else if (unlockFire && (out & TURBO) && (previous & TURBO)) {
            out &= static_cast<uint8_t>(~TURBO);
            st.pending |= TURBO;
        }
    }

    st.valid = true;
    st.tick = g_sim.tick;
    st.byte = out;
    return out;
}

}  // namespace cpudriver

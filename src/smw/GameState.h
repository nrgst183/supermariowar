#pragma once

#include <cassert>


class GameState {
public:
    virtual ~GameState() = default;

    virtual bool init() { return true; }
    // One tick (16 ms) of the state. States other than gameplay also draw here.
    virtual void update() = 0;
    // Draws the state between ticks (see FPSLimiter), `alpha` (0..1) of the
    // way from the last tick to the next one. Only called for states that
    // render between ticks; for the others the frame drawn in update() is shown.
    virtual bool rendersBetweenTicks() const { return false; }
    virtual void render(float /*alpha*/) {}
    virtual void cleanup() {}

protected:
    virtual void onEnterState() {}
    virtual void onLeaveState() {}

friend class GameStateManager;
};


class GameStateManager {
public:
    GameState* currentState = nullptr;

    void changeStateTo(GameState* newState) {
        assert(newState);
        currentState->onLeaveState();
        currentState = newState;
        currentState->onEnterState();
    }

    static GameStateManager& instance() {
        static GameStateManager gsm;
        return gsm;
    }
};

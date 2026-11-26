#ifndef GAMESTATE_SPLASHSCREEN_H
#define GAMESTATE_SPLASHSCREEN_H

#include "GameState.h"
#include "gfx.h"

#include <SDL3/SDL.h>

#include <memory>

class SplashScreenState : public GameState
{
    public:
        bool init() override;
        void update() override;

        static SplashScreenState& instance();
        // The splash under the loading screen's bar (LoadScreen.h): the backdrop, the logo, the credits
        static void drawLoadingScreen();

    private:
        void onLeaveState() override;

        SDL_Event loop_event;
        std::unique_ptr<gfxSprite> menu_credits;
        int alpha;
        int state;
        int timer;
        bool firstFrame;

        SplashScreenState();
};

#endif // GAMESTATE_SPLASHSCREEN_H

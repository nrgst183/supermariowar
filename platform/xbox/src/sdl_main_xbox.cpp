// Xbox SDL3 compatibility layer: the title's entry point.
//
// The RXDK startup calls main() without arguments, so main can't take the
// usual argc/argv. As with SDL on other platforms, the game's main is renamed
// to SDL_main (SDL_main.h) and called from here with a proper argv. When the
// game returns, the console goes back to the dashboard.

#include "sdl_internal.h"

extern "C" int SDL_main(int argc, char* argv[]);

int main(void)
{
    static char arg0[] = "D:\\default.xbe";
    char* argv[] = {arg0, nullptr};

    sdlx_set_fpu_precision();

    // The network starts now: its address (DHCP) comes while the menus are up
    SDL_XboxStartNetwork();

    const int result = SDL_main(1, argv);
    sdlx_log("[xbox] The game exited with %d\n", result);

    SDL_XboxReturnToDashboard();
    return result;
}

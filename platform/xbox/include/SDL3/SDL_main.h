#pragma once
// Xbox SDL3 compatibility layer: the RXDK startup calls a plain `int main(void)`
// (platform/xbox/src/sdl_main_xbox.cpp), which hands the game's main a proper
// argc/argv. Like real SDL, the game's own main is renamed to SDL_main.
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C"
#endif
int SDL_main(int argc, char* argv[]);

#define main SDL_main

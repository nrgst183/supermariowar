#pragma once
// SDL3_image compatibility for the original Xbox: PNG/BMP/TGA/JPEG loading via
// stb_image, PNG writing via stb_image_write (platform/xbox/src/sdl_image.cpp).
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_IMAGE_MAJOR_VERSION 3
#define SDL_IMAGE_MINOR_VERSION 4
#define SDL_IMAGE_MICRO_VERSION 0

int IMG_Version(void);
// Always returns an SDL_PIXELFORMAT_ARGB8888 surface
SDL_Surface* IMG_Load(const char* file);
bool IMG_SavePNG(SDL_Surface* surface, const char* file);

#ifdef __cplusplus
}
#endif

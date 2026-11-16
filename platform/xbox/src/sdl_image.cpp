// Xbox SDL3 compatibility layer: SDL3_image (stb_image / stb_image_write).

#include <SDL3_image/SDL_image.h>

#include "sdl_internal.h"

#define STBI_NO_STDIO
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool readFile(const char* path, std::vector<unsigned char>& out)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return SDL_SetError("Couldn't open %s", path);

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return SDL_SetError("Couldn't read %s", path);
    }

    out.resize(static_cast<size_t>(size));
    const size_t read = fread(out.data(), 1, out.size(), file);
    fclose(file);
    if (read != out.size())
        return SDL_SetError("Couldn't read %s", path);
    return true;
}

void writeToFile(void* context, void* data, int size)
{
    fwrite(data, 1, static_cast<size_t>(size), static_cast<FILE*>(context));
}
} // namespace


int IMG_Version(void)
{
    return SDL_VERSIONNUM(SDL_IMAGE_MAJOR_VERSION, SDL_IMAGE_MINOR_VERSION, SDL_IMAGE_MICRO_VERSION);
}

SDL_Surface* IMG_Load(const char* file)
{
    if (!file) {
        SDL_SetError("Parameter 'file' is invalid");
        return nullptr;
    }

    std::vector<unsigned char> data;
    if (!readFile(file, data))
        return nullptr;

    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    if (!rgba) {
        SDL_SetError("Couldn't decode %s: %s", file, stbi_failure_reason());
        return nullptr;
    }
    // Free the file data before allocating the surface (big backgrounds)
    std::vector<unsigned char>().swap(data);

    SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        stbi_image_free(rgba);
        return nullptr;
    }

    const unsigned char* src = rgba;
    bool opaque = true;
    for (int y = 0; y < h; y++) {
        Uint32* dst = reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch);
        for (int x = 0; x < w; x++, src += 4) {
            dst[x] = (static_cast<Uint32>(src[3]) << 24) | (static_cast<Uint32>(src[0]) << 16) | (static_cast<Uint32>(src[1]) << 8) | src[2];
            opaque = opaque && src[3] == 255;
        }
    }
    stbi_image_free(rgba);

    // Opaque images don't need blending (as in SDL_image, where an image
    // without an alpha channel loads into a format without alpha). This also
    // goes for PNGs with an alpha channel that is opaque everywhere: their
    // blits become plain copies. The game turns blending on when it sets a
    // color key or an alpha.
    if (opaque)
        surface->blend_mode = SDL_BLENDMODE_NONE;

    return surface;
}

bool IMG_SavePNG(SDL_Surface* surface, const char* file)
{
    if (!surface || !surface->pixels || !file)
        return SDL_SetError("Invalid surface or file");

    std::vector<unsigned char> rgba(static_cast<size_t>(surface->w) * surface->h * 4);
    unsigned char* dst = rgba.data();
    const bool alpha = surface->format == SDL_PIXELFORMAT_ARGB8888;
    for (int y = 0; y < surface->h; y++) {
        const Uint32* src = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch);
        for (int x = 0; x < surface->w; x++, dst += 4) {
            const Uint32 p = src[x];
            dst[0] = static_cast<unsigned char>(p >> 16);
            dst[1] = static_cast<unsigned char>(p >> 8);
            dst[2] = static_cast<unsigned char>(p);
            dst[3] = alpha ? static_cast<unsigned char>(p >> 24) : 255;
        }
    }

    FILE* out = fopen(file, "wb");
    if (!out)
        return SDL_SetError("Couldn't open %s for writing", file);
    const int ok = stbi_write_png_to_func(writeToFile, out, surface->w, surface->h, 4, rgba.data(), surface->w * 4);
    fclose(out);
    if (!ok)
        return SDL_SetError("Couldn't encode %s", file);
    return true;
}

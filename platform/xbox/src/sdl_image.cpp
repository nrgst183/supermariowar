// Xbox SDL3 compatibility layer: SDL3_image (stb_image / stb_image_write).

#include <SDL3_image/SDL_image.h>

#include "sdl_internal.h"

#define STBI_NO_STDIO
#include "stb_image.h"
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
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

// ---- Decoded images, kept on the cache drive
//
// An image read from the disc is slow to load on the Xbox: the DVD read, then the PNG's inflate on a
// 733 MHz CPU. Every one (the small ones too: their DVD seeks, a few hundred at startup, were most of
// the loading time) is written decoded to the
// cache drive the first time, and read from there afterwards straight into the surface. An entry
// is the image's path (hashed), and its file's size (a changed file: decoded again); the cache
// stops growing at CACHE_BUDGET.
constexpr Uint32 CACHE_MAGIC = 0x49574D53;    // "SMWI"
constexpr Uint32 CACHE_VERSION = 1;
constexpr Uint64 CACHE_BUDGET = 400ull << 20;

struct CacheHeader {
    Uint32 magic;
    Uint32 version;
    Uint32 sourceSize;
    Sint32 w;
    Sint32 h;
    Uint32 opaque;
};

std::atomic<Sint64> s_cacheUsed {-1};    // (bytes in the cache folder: counted at the first write)

bool fromDisc(const char* file)
{
    return (file[0] == 'D' || file[0] == 'd') && file[1] == ':';
}

std::string cacheDir()
{
    return std::string(SDL_XboxGetCachePath()) + "img/";
}

std::string cacheFile(const char* file)
{
    //(FNV-1a of the path, its case and slashes evened out)
    Uint64 hash = 1469598103934665603ull;
    for (const char* c = file; *c; c++) {
        char ch = *c == '\\' ? '/' : *c;
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        hash = (hash ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
    }
    char name[32];
    snprintf(name, sizeof(name), "%016llx.raw", static_cast<unsigned long long>(hash));
    return cacheDir() + name;
}

long fileSize(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return -1;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fclose(file);
    return size;
}

SDL_Surface* loadCached(const char* file, long sourceSize)
{
    FILE* in = fopen(cacheFile(file).c_str(), "rb");
    if (!in)
        return nullptr;
    CacheHeader header {};
    SDL_Surface* surface = nullptr;
    if (fread(&header, sizeof(header), 1, in) == 1 && header.magic == CACHE_MAGIC && header.version == CACHE_VERSION &&
        header.sourceSize == static_cast<Uint32>(sourceSize) && header.w > 0 && header.h > 0 && header.w <= 8192 && header.h <= 8192) {
        surface = SDL_CreateSurface(header.w, header.h, SDL_PIXELFORMAT_ARGB8888);
        if (surface) {
            const size_t row = static_cast<size_t>(header.w) * 4;
            bool ok = true;
            if (static_cast<size_t>(surface->pitch) == row) {
                ok = fread(surface->pixels, row * header.h, 1, in) == 1;
            } else {
                for (int y = 0; ok && y < header.h; y++)
                    ok = fread(static_cast<Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch, row, 1, in) == 1;
            }
            if (!ok) {
                //(cut short: decoded again, and written again)
                SDL_DestroySurface(surface);
                surface = nullptr;
            } else if (header.opaque) {
                surface->blend_mode = SDL_BLENDMODE_NONE;
            }
        }
    }
    fclose(in);
    return surface;
}

void saveCached(const char* file, long sourceSize, SDL_Surface* surface, bool opaque)
{
    const size_t row = static_cast<size_t>(surface->w) * 4;
    const Uint64 bytes = sizeof(CacheHeader) + row * surface->h;
    if (sourceSize <= 0)
        return;
    if (s_cacheUsed < 0)
        s_cacheUsed = static_cast<Sint64>(sdlx_dir_bytes(cacheDir().c_str()));
    if (static_cast<Uint64>(s_cacheUsed) + bytes > CACHE_BUDGET)
        return;
    if (!sdlx_make_dir(cacheDir().c_str()))
        return;
    FILE* out = fopen(cacheFile(file).c_str(), "wb");
    if (!out)
        return;
    const CacheHeader header {CACHE_MAGIC, CACHE_VERSION, static_cast<Uint32>(sourceSize), surface->w, surface->h, opaque ? 1u : 0u};
    bool ok = fwrite(&header, sizeof(header), 1, out) == 1;
    for (int y = 0; ok && y < surface->h; y++)
        ok = fwrite(static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch, row, 1, out) == 1;
    fclose(out);
    if (ok)
        s_cacheUsed += static_cast<Sint64>(bytes);
    else
        std::remove(cacheFile(file).c_str());    //(the drive is full: not kept half written)
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

    //(a big image from the disc: decoded already, on the cache drive)
    const long sourceSize = fromDisc(file) ? fileSize(file) : -1;
    if (sourceSize > 0) {
        if (SDL_Surface* cached = loadCached(file, sourceSize))
            return cached;
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

    if (sourceSize > 0)
        saveCached(file, sourceSize, surface, opaque);
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

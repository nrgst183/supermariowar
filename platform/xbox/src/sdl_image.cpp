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
// (the system's reads, in big blocks: sdlx_open)
bool readFile(const char* path, std::vector<unsigned char>& out)
{
    void* file = sdlx_open(path, false);
    if (!file)
        return SDL_SetError("Couldn't open %s", path);
    const Sint64 size = sdlx_size(file);
    if (size <= 0) {
        sdlx_close(file);
        return SDL_SetError("Couldn't read %s", path);
    }
    out.resize(static_cast<size_t>(size));
    const bool ok = sdlx_read(file, out.data(), out.size());
    sdlx_close(file);
    if (!ok)
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
    void* file = sdlx_open(path, false);
    const long size = static_cast<long>(sdlx_size(file));
    sdlx_close(file);
    return size;
}

SDL_Surface* loadCached(const char* file, long sourceSize)
{
    //(the pixels read straight into the surface, in one call)
    void* in = sdlx_open(cacheFile(file).c_str(), false);
    if (!in)
        return nullptr;
    CacheHeader header {};
    SDL_Surface* surface = nullptr;
    if (sdlx_read(in, &header, sizeof(header)) && header.magic == CACHE_MAGIC && header.version == CACHE_VERSION &&
        header.sourceSize == static_cast<Uint32>(sourceSize) && header.w > 0 && header.h > 0 && header.w <= 8192 && header.h <= 8192) {
        surface = SDL_CreateSurface(header.w, header.h, SDL_PIXELFORMAT_ARGB8888);
        if (surface) {
            const size_t row = static_cast<size_t>(header.w) * 4;
            bool ok = true;
            if (static_cast<size_t>(surface->pitch) == row) {
                ok = sdlx_read(in, surface->pixels, row * header.h);
            } else {
                for (int y = 0; ok && y < header.h; y++)
                    ok = sdlx_read(in, static_cast<Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch, row);
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
    sdlx_close(in);
    return surface;
}

bool saveCached(const char* file, long sourceSize, SDL_Surface* surface, bool opaque)
{
    const size_t row = static_cast<size_t>(surface->w) * 4;
    const Uint64 bytes = sizeof(CacheHeader) + row * surface->h;
    if (sourceSize <= 0)
        return false;
    if (s_cacheUsed < 0)
        s_cacheUsed = static_cast<Sint64>(sdlx_dir_bytes(cacheDir().c_str()));
    if (static_cast<Uint64>(s_cacheUsed) + bytes > CACHE_BUDGET)
        return false;
    if (!sdlx_make_dir(cacheDir().c_str()))
        return false;
    void* out = sdlx_open(cacheFile(file).c_str(), true);
    if (!out)
        return false;
    const CacheHeader header {CACHE_MAGIC, CACHE_VERSION, static_cast<Uint32>(sourceSize), surface->w, surface->h, opaque ? 1u : 0u};
    bool ok = sdlx_write(out, &header, sizeof(header));
    if (static_cast<size_t>(surface->pitch) == row) {
        ok = ok && sdlx_write(out, surface->pixels, row * surface->h);
    } else {
        for (int y = 0; ok && y < surface->h; y++)
            ok = sdlx_write(out, static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch, row);
    }
    sdlx_close(out);
    if (ok)
        s_cacheUsed += static_cast<Sint64>(bytes);
    else
        std::remove(cacheFile(file).c_str());    //(the drive is full: not kept half written)
    return ok;
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

    SDL_XboxLoadStats& stats = g_sdlx_image_stats;

    //(an image from the disc: decoded already, on the cache drive. Its size, to know the copy is of
    //this file: from its folder's listing, not a DVD seek to the file)
    Uint64 t = sdlx_ticks_ns();
    long sourceSize = -1;
    if (fromDisc(file)) {
        sourceSize = SDL_XboxDiscFileSize(file);
        if (sourceSize <= 0)
            sourceSize = fileSize(file);
        stats.sizeChecks++;
        stats.sizeCheckNs += sdlx_ticks_ns() - t;
    }
    if (sourceSize > 0) {
        t = sdlx_ticks_ns();
        if (SDL_Surface* cached = loadCached(file, sourceSize)) {
            stats.cacheHits++;
            stats.cacheReadNs += sdlx_ticks_ns() - t;
            stats.cacheReadBytes += static_cast<Uint64>(cached->w) * cached->h * 4;
            return cached;
        }
        stats.cacheReadNs += sdlx_ticks_ns() - t;
    }

    t = sdlx_ticks_ns();
    std::vector<unsigned char> data;
    if (!readFile(file, data))
        return nullptr;
    stats.discReads++;
    stats.discReadNs += sdlx_ticks_ns() - t;
    stats.discReadBytes += data.size();

    t = sdlx_ticks_ns();
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
    stats.decodes++;
    stats.decodeNs += sdlx_ticks_ns() - t;

    if (sourceSize > 0) {
        t = sdlx_ticks_ns();
        if (saveCached(file, sourceSize, surface, opaque)) {
            stats.cacheWrites++;
            stats.cacheWriteBytes += static_cast<Uint64>(surface->w) * surface->h * 4;
        } else {
            stats.cacheWritesSkipped++;
        }
        stats.cacheWriteNs += sdlx_ticks_ns() - t;
    }
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

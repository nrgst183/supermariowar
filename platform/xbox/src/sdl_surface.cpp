// Xbox SDL3 compatibility layer: software surfaces and blitting.
//
// The game draws everything on the CPU into a 640x480 ARGB8888 surface, like it
// does on SDL's software renderer path. Surfaces are 32-bit (ARGB8888, or
// XRGB8888 where the alpha channel is ignored), except loaded images made
// compact (SDL_XboxCompactSurface): 16-bit, ARGB1555 or ARGB4444, drawn from
// only (a blit widens their rows to 32 bits as it reads them). The blending
// rules follow SDL3: color key first, then the blend mode with the alpha
// modulation.

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <xmmintrin.h>

namespace {
bool isSupportedFormat(SDL_PixelFormat format)
{
    return format == SDL_PIXELFORMAT_ARGB8888 || format == SDL_PIXELFORMAT_XRGB8888 ||
           format == SDL_PIXELFORMAT_ARGB1555 || format == SDL_PIXELFORMAT_ARGB4444;
}

bool hasAlpha(SDL_PixelFormat format)
{
    return format != SDL_PIXELFORMAT_XRGB8888;
}

bool is16(SDL_PixelFormat format)
{
    return format == SDL_PIXELFORMAT_ARGB1555 || format == SDL_PIXELFORMAT_ARGB4444;
}

const SDL_PixelFormatDetails k_argb1555 = {
    SDL_PIXELFORMAT_ARGB1555, 16, 2, {0, 0},
    0x7C00u, 0x03E0u, 0x001Fu, 0x8000u,
    5, 5, 5, 1,
    10, 5, 0, 15,
};

const SDL_PixelFormatDetails k_argb4444 = {
    SDL_PIXELFORMAT_ARGB4444, 16, 2, {0, 0},
    0x0F00u, 0x00F0u, 0x000Fu, 0xF000u,
    4, 4, 4, 4,
    8, 4, 0, 12,
};

// 16-bit pixels widened to ARGB8888: each byte's part looked up (a channel's widening is shifts and
// ORs of its bits, so the two bytes' parts OR together)
struct Widen {
    Uint32 lo[256];
    Uint32 hi[256];
};

Uint32 widen1555(Uint32 v)
{
    const Uint32 r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    return ((v & 0x8000u) ? 0xFF000000u : 0u) | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

Uint32 widen4444(Uint32 v)
{
    const Uint32 a = (v >> 12) & 15, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    return ((a * 17) << 24) | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
}

const Widen& widenTable(SDL_PixelFormat format)
{
    static Widen t1555, t4444;
    static bool made = false;
    if (!made) {
        for (Uint32 i = 0; i < 256; i++) {
            t1555.lo[i] = widen1555(i);
            t1555.hi[i] = widen1555(i << 8);
            t4444.lo[i] = widen4444(i);
            t4444.hi[i] = widen4444(i << 8);
        }
        made = true;
    }
    return format == SDL_PIXELFORMAT_ARGB1555 ? t1555 : t4444;
}

const SDL_PixelFormatDetails k_argb8888 = {
    SDL_PIXELFORMAT_ARGB8888, 32, 4, {0, 0},
    0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u,
    8, 8, 8, 8,
    16, 8, 0, 24,
};

const SDL_PixelFormatDetails k_xrgb8888 = {
    SDL_PIXELFORMAT_XRGB8888, 32, 4, {0, 0},
    0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00000000u,
    8, 8, 8, 0,
    16, 8, 0, 0,
};

constexpr Uint32 RGB_MASK = 0x00FFFFFFu;

// Clipped blit geometry
struct BlitArea {
    int sx, sy;  // source origin
    int dx, dy;  // destination origin
    int w, h;
};

bool clipBlit(const SDL_Surface* src, const SDL_Rect* srcrect, const SDL_Surface* dst, const SDL_Rect* dstrect, BlitArea& out)
{
    int sx = 0, sy = 0, w = src->w, h = src->h;
    if (srcrect) {
        sx = srcrect->x;
        sy = srcrect->y;
        w = srcrect->w;
        h = srcrect->h;
    }
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;

    // Clip to the source surface
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;

    // Clip to the destination's clip rect (inside the surface)
    const SDL_Rect& clip = dst->clip_rect;
    if (dx < clip.x) { const int c = clip.x - dx; w -= c; sx += c; dx = clip.x; }
    if (dy < clip.y) { const int c = clip.y - dy; h -= c; sy += c; dy = clip.y; }
    if (dx + w > clip.x + clip.w) w = clip.x + clip.w - dx;
    if (dy + h > clip.y + clip.h) h = clip.y + clip.h - dy;

    if (w <= 0 || h <= 0)
        return false;

    out = {sx, sy, dx, dy, w, h};
    return true;
}

inline Uint32* rowPtr(SDL_Surface* surface, int y)
{
    return reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch);
}

inline const Uint32* rowPtr(const SDL_Surface* surface, int y)
{
    return reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch);
}

inline Uint16 pack1555(Uint32 p)
{
    return static_cast<Uint16>(((p >> 24) >= 128 ? 0x8000u : 0u) | ((p >> 9) & 0x7C00u) | ((p >> 6) & 0x03E0u) | ((p >> 3) & 0x001Fu));
}

// A 16-bit target's rows: widened to ARGB8888 for a blit to work on, packed back after
struct TargetRow {
    SDL_Surface* surface;
    const Widen* table;
    Uint32 buffer[1024];
    Uint16* row = nullptr;
    int n = 0;

    explicit TargetRow(SDL_Surface* s) : surface(s), table(&widenTable(s->format)) {}

    Uint32* begin(int y, int x, int count)
    {
        n = count > 1024 ? 1024 : count;
        row = reinterpret_cast<Uint16*>(static_cast<Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch) + x;
        for (int i = 0; i < n; i++)
            buffer[i] = table->lo[row[i] & 0xFF] | table->hi[row[i] >> 8];
        return buffer;
    }
    void end()
    {
        for (int i = 0; i < n; i++)
            row[i] = pack1555(buffer[i]);
    }
};

// A source's pixels as ARGB8888: a 32-bit surface's own, a 16-bit one's widened into a buffer
struct SourceRows {
    const SDL_Surface* surface;
    bool wide;
    const Widen* table = nullptr;
    Uint32 local[1024];
    Uint32* buffer = local;
    Uint32* heap = nullptr;

    //(`width`: the most pixels a row is asked for: the blit's, not the surface's; a wide sheet's
    //blits mostly fit the local buffer, no allocation each)
    SourceRows(const SDL_Surface* s, int width)
        : surface(s)
        , wide(is16(s->format))
    {
        if (wide) {
            table = &widenTable(s->format);
            if (width > 1024) {
                heap = static_cast<Uint32*>(malloc(static_cast<size_t>(width) * 4));
                buffer = heap ? heap : local;
            }
        }
    }
    ~SourceRows() { free(heap); }
    SourceRows(const SourceRows&) = delete;
    SourceRows& operator=(const SourceRows&) = delete;

    // Row y's pixels from x on (n of them); [0] is pixel x
    const Uint32* row(int y, int x, int n)
    {
        if (!wide)
            return rowPtr(surface, y) + x;
        if (heap == nullptr && n > 1024)
            n = 1024;
        const Uint16* s = reinterpret_cast<const Uint16*>(static_cast<const Uint8*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch) + x;
        for (int i = 0; i < n; i++)
            buffer[i] = table->lo[s[i] & 0xFF] | table->hi[s[i] >> 8];
        return buffer;
    }
};

// a: 0..256
inline Uint32 blendRgb(Uint32 s, Uint32 d, Uint32 a)
{
    const Uint32 ia = 256 - a;
    const Uint32 rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    const Uint32 g = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
    return rb | g;
}

// x / 255, rounded to nearest (same as (x + 127) / 255 for 0..65535),
// without a division
inline Uint32 div255(Uint32 x)
{
    x += 128;
    return (x + (x >> 8)) >> 8;
}

// How a source pixel is written; decided once per blit
struct PixelOp {
    bool colorkey;
    Uint32 key;
    bool blend;        // SDL_BLENDMODE_BLEND (or ADD/MOD, approximated as BLEND)
    Uint32 alpha_mod;  // 0..255
    bool src_alpha;    // source has an alpha channel
    bool dst_alpha;    // destination keeps an alpha channel

    // Writes source pixel `s` over `*d`
    inline void apply(Uint32 s, Uint32* d) const
    {
        if (colorkey && (s & RGB_MASK) == key)
            return;

        if (!src_alpha)
            s |= 0xFF000000u;

        if (!blend) {
            *d = s;
            return;
        }

        Uint32 sa = s >> 24;
        if (alpha_mod != 255)
            sa = div255(sa * alpha_mod);

        if (sa == 0)
            return;
        if (sa == 255) {
            *d = s | 0xFF000000u;
            return;
        }

        const Uint32 dv = *d;
        const Uint32 rgb = blendRgb(s, dv, sa + (sa >> 7));
        Uint32 da = 0xFFu;
        if (dst_alpha) {
            const Uint32 dstA = dv >> 24;
            da = sa + div255(dstA * (255 - sa));
        }
        *d = rgb | (da << 24);
    }
};

PixelOp makePixelOp(const SDL_Surface* src, const SDL_Surface* dst)
{
    PixelOp op {};
    op.colorkey = src->has_colorkey;
    op.key = src->colorkey & RGB_MASK;
    op.blend = src->blend_mode != SDL_BLENDMODE_NONE;
    op.alpha_mod = src->alpha_mod;
    op.src_alpha = hasAlpha(src->format);
    op.dst_alpha = hasAlpha(dst->format);
    // Without an alpha channel and without alpha modulation, blending is a copy
    if (op.blend && !op.src_alpha && op.alpha_mod == 255)
        op.blend = false;
    return op;
}

bool checkSurface(const SDL_Surface* surface, const char* name)
{
    if (!surface)
        return SDL_SetError("Parameter '%s' is invalid", name);
    if (!surface->pixels)
        return SDL_SetError("Surface '%s' has no pixels", name);
    return true;
}

SDL_Surface* allocSurface(int width, int height, SDL_PixelFormat format)
{
    auto* surface = static_cast<SDL_Surface*>(calloc(1, sizeof(SDL_Surface)));
    if (!surface) {
        SDL_SetError("Out of memory");
        return nullptr;
    }
    surface->format = format;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * (is16(format) ? 2 : 4);
    surface->refcount = 1;
    surface->has_colorkey = false;
    surface->colorkey = 0;
    surface->alpha_mod = 255;
    surface->blend_mode = hasAlpha(format) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    surface->clip_rect = {0, 0, width, height};
    return surface;
}
} // namespace


/* ---- Pixel formats ------------------------------------------------------- */

const SDL_PixelFormatDetails* SDL_GetPixelFormatDetails(SDL_PixelFormat format)
{
    switch (format) {
        case SDL_PIXELFORMAT_ARGB8888: return &k_argb8888;
        case SDL_PIXELFORMAT_XRGB8888: return &k_xrgb8888;
        case SDL_PIXELFORMAT_ARGB1555: return &k_argb1555;
        case SDL_PIXELFORMAT_ARGB4444: return &k_argb4444;
        default:
            SDL_SetError("Unsupported pixel format 0x%08x", static_cast<unsigned>(format));
            return nullptr;
    }
}

Uint32 SDL_MapRGB(const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGBA(format, palette, r, g, b, 255);
}

Uint32 SDL_MapRGBA(const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void)palette;
    const Uint32 alpha = (format && format->Amask) ? a : 0xFFu;
    return (alpha << 24) | (static_cast<Uint32>(r) << 16) | (static_cast<Uint32>(g) << 8) | b;
}

void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8* r, Uint8* g, Uint8* b)
{
    (void)format;
    (void)palette;
    if (r) *r = static_cast<Uint8>(pixel >> 16);
    if (g) *g = static_cast<Uint8>(pixel >> 8);
    if (b) *b = static_cast<Uint8>(pixel);
}

void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8* r, Uint8* g, Uint8* b, Uint8* a)
{
    SDL_GetRGB(pixel, format, palette, r, g, b);
    if (a) *a = (format && format->Amask) ? static_cast<Uint8>(pixel >> 24) : 255;
}


/* ---- Surfaces ------------------------------------------------------------ */

namespace {
void trackSurface(SDL_Surface* surface);
} // namespace

SDL_Surface* SDL_CreateSurface(int width, int height, SDL_PixelFormat format)
{
    if (width < 0 || height < 0) {
        SDL_SetError("Invalid surface size %dx%d", width, height);
        return nullptr;
    }
    if (!isSupportedFormat(format)) {
        SDL_SetError("Unsupported pixel format 0x%08x", static_cast<unsigned>(format));
        return nullptr;
    }

    SDL_Surface* surface = allocSurface(width, height, format);
    if (!surface)
        return nullptr;

    const size_t size = static_cast<size_t>(surface->pitch) * height;
    surface->pixels = sdlx_calloc(1, size ? size : 4);
    if (!surface->pixels) {
        free(surface);
        SDL_SetError("Out of memory (%dx%d surface)", width, height);
        return nullptr;
    }
    g_sdlx_surface_bytes += size;
    trackSurface(surface);
    return surface;
}

namespace {
// The live surfaces with pixels of their own (a fixed table: listed on the crash screen, which
// mustn't allocate)
struct LiveSurface {
    SDL_Surface* surface;
    Uint32 bytes;
    char name[48];
};
constexpr int k_live = 4096;
LiveSurface g_live[k_live];
int g_live_count = 0;
//(the map preview's worker makes surfaces too. A critical section, not a spin: the worker runs at a
//low priority on the one core, so the game's thread spinning on the worker's lock never let it run
//to give it back)
CRITICAL_SECTION& liveLock()
{
    static struct Section {
        CRITICAL_SECTION section;
        Section() { InitializeCriticalSection(&section); }
    } s;
    return s.section;
}

struct LiveLock {
    LiveLock() { EnterCriticalSection(&liveLock()); }
    ~LiveLock() { LeaveCriticalSection(&liveLock()); }
};

void trackSurface(SDL_Surface* surface)
{
    LiveLock lock;
    if (g_live_count >= k_live)
        return;
    LiveSurface& live = g_live[g_live_count++];
    live.surface = surface;
    live.bytes = static_cast<Uint32>(surface->pitch) * static_cast<Uint32>(surface->h);
    snprintf(live.name, sizeof(live.name), "blank %dx%d", surface->w, surface->h);
}

void untrackSurface(SDL_Surface* surface)
{
    LiveLock lock;
    for (int i = 0; i < g_live_count; i++) {
        if (g_live[i].surface == surface) {
            g_live[i] = g_live[--g_live_count];
            return;
        }
    }
}

const char* nameOf(const SDL_Surface* surface)
{
    for (int i = 0; i < g_live_count; i++) {
        if (g_live[i].surface == surface)
            return g_live[i].name;
    }
    return nullptr;
}
} // namespace

void sdlx_surface_name(SDL_Surface* surface, const char* name)
{
    if (!surface || !name)
        return;
    //(a file's last two parts: "menu/menu_shade.png")
    const char* start = name;
    int slashes = 0;
    for (const char* c = name + strlen(name); c > name; c--) {
        if (c[-1] == '/' || c[-1] == '\\') {
            if (++slashes == 2) {
                start = c;
                break;
            }
        }
    }
    LiveLock lock;
    for (int i = 0; i < g_live_count; i++) {
        if (g_live[i].surface == surface) {
            snprintf(g_live[i].name, sizeof(g_live[i].name), "%s", start);
            return;
        }
    }
}

int sdlx_biggest_surfaces(char (*lines)[64], int count)
{
    //(no lock: the crash screen may come while it's held; a list a little off is fine there)
    int have = 0;
    Uint32 below = 0xFFFFFFFFu;
    const LiveSurface* lastPicked = nullptr;
    for (int n = 0; n < count; n++) {
        const LiveSurface* best = nullptr;
        for (int i = 0; i < g_live_count; i++) {
            const LiveSurface& live = g_live[i];
            if (live.bytes > below || (live.bytes == below && &live <= lastPicked))
                continue;
            if (!best || live.bytes > best->bytes)
                best = &live;
        }
        if (!best)
            break;
        snprintf(lines[have++], 64, "%4uK %s", static_cast<unsigned>(best->bytes / 1024), best->name);
        below = best->bytes;
        lastPicked = best;
    }
    return have;
}

SDL_Surface* sdlx_adopt_pixels(int w, int h, void* pixels)
{
    SDL_Surface* surface = allocSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
        return nullptr;
    surface->pixels = pixels;
    g_sdlx_surface_bytes += static_cast<Uint64>(surface->pitch) * h;
    trackSurface(surface);
    return surface;
}

SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void* pixels, int pitch)
{
    if (!isSupportedFormat(format)) {
        SDL_SetError("Unsupported pixel format 0x%08x", static_cast<unsigned>(format));
        return nullptr;
    }
    SDL_Surface* surface = allocSurface(width, height, format);
    if (!surface)
        return nullptr;
    surface->flags |= SDL_SURFACE_PREALLOCATED;
    surface->pixels = pixels;
    surface->pitch = pitch;
    return surface;
}

void SDL_DestroySurface(SDL_Surface* surface)
{
    if (!surface)
        return;
    if (--surface->refcount > 0)
        return;
    if (!(surface->flags & SDL_SURFACE_PREALLOCATED)) {
        g_sdlx_surface_bytes -= static_cast<Uint64>(surface->pitch) * surface->h;
        untrackSurface(surface);
        sdlx_free(surface->pixels);
    }
    free(surface);
}

SDL_Surface* SDL_ConvertSurface(SDL_Surface* surface, SDL_PixelFormat format)
{
    if (!checkSurface(surface, "surface"))
        return nullptr;

    SDL_Surface* out = SDL_CreateSurface(surface->w, surface->h, format);
    if (out) {
        char name[48] = "";
        {
            LiveLock lock;
            if (const char* from = nameOf(surface))
                snprintf(name, sizeof(name), "%s", from);
        }
        if (name[0] && strncmp(name, "blank", 5) != 0)
            sdlx_surface_name(out, name);
    }
    if (!out)
        return nullptr;

    if (is16(format)) {
        SDL_DestroySurface(out);
        SDL_SetError("Can't convert to a 16-bit format (SDL_XboxCompactSurface does)");
        return nullptr;
    }
    const bool srcAlpha = hasAlpha(surface->format);
    SourceRows rows(surface, surface->w);
    for (int y = 0; y < surface->h; y++) {
        const Uint32* s = rows.row(y, 0, surface->w);
        Uint32* d = rowPtr(out, y);
        if (srcAlpha) {
            memcpy(d, s, static_cast<size_t>(surface->w) * 4);
        } else {
            for (int x = 0; x < surface->w; x++)
                d[x] = s[x] | 0xFF000000u;
        }
    }

    out->alpha_mod = surface->alpha_mod;
    out->blend_mode = surface->blend_mode;

    if (surface->has_colorkey) {
        if (hasAlpha(format)) {
            // As SDL does: with an alpha channel, the color key becomes
            // transparency, and the surface blends
            const Uint32 key = surface->colorkey & RGB_MASK;
            for (int y = 0; y < out->h; y++) {
                Uint32* d = rowPtr(out, y);
                for (int x = 0; x < out->w; x++) {
                    if ((d[x] & RGB_MASK) == key)
                        d[x] = key;  // alpha 0
                }
            }
            out->has_colorkey = false;
            out->blend_mode = SDL_BLENDMODE_BLEND;
        } else {
            out->has_colorkey = true;
            out->colorkey = surface->colorkey;
        }
    }
    return out;
}

SDL_Surface* SDL_DuplicateSurface(SDL_Surface* surface)
{
    if (!checkSurface(surface, "surface"))
        return nullptr;
    SDL_Surface* out = SDL_CreateSurface(surface->w, surface->h, surface->format);
    if (out) {
        char name[48] = "";
        {
            LiveLock lock;
            if (const char* from = nameOf(surface))
                snprintf(name, sizeof(name), "%s", from);
        }
        if (name[0] && strncmp(name, "blank", 5) != 0)
            sdlx_surface_name(out, name);
    }
    if (!out)
        return nullptr;
    for (int y = 0; y < surface->h; y++)
        memcpy(rowPtr(out, y), rowPtr(static_cast<const SDL_Surface*>(surface), y), static_cast<size_t>(surface->w) * SDL_BYTESPERPIXEL(surface->format));
    out->has_colorkey = surface->has_colorkey;
    out->colorkey = surface->colorkey;
    out->alpha_mod = surface->alpha_mod;
    out->blend_mode = surface->blend_mode;
    return out;
}

SDL_Palette* SDL_GetSurfacePalette(SDL_Surface* surface)
{
    (void)surface;
    return nullptr;
}

bool SDL_LockSurface(SDL_Surface* surface)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    surface->flags |= SDL_SURFACE_LOCKED;
    return true;
}

void SDL_UnlockSurface(SDL_Surface* surface)
{
    if (surface)
        surface->flags &= ~SDL_SURFACE_LOCKED;
}

bool SDL_SetSurfaceRLE(SDL_Surface* surface, bool enabled)
{
    (void)enabled;
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    return true;
}

bool SDL_SetSurfaceColorKey(SDL_Surface* surface, bool enabled, Uint32 key)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    surface->has_colorkey = enabled;
    surface->colorkey = key;
    return true;
}

bool SDL_SurfaceHasColorKey(SDL_Surface* surface)
{
    return surface && surface->has_colorkey;
}

bool SDL_GetSurfaceColorKey(SDL_Surface* surface, Uint32* key)
{
    if (!surface || !surface->has_colorkey)
        return SDL_SetError("Surface doesn't have a colorkey");
    if (key)
        *key = surface->colorkey;
    return true;
}

bool SDL_SetSurfaceAlphaMod(SDL_Surface* surface, Uint8 alpha)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    surface->alpha_mod = alpha;
    return true;
}

bool SDL_GetSurfaceAlphaMod(SDL_Surface* surface, Uint8* alpha)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    if (alpha)
        *alpha = surface->alpha_mod;
    return true;
}

bool SDL_SetSurfaceBlendMode(SDL_Surface* surface, SDL_BlendMode blendMode)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    surface->blend_mode = blendMode;
    return true;
}

bool SDL_GetSurfaceBlendMode(SDL_Surface* surface, SDL_BlendMode* blendMode)
{
    if (!surface)
        return SDL_SetError("Parameter 'surface' is invalid");
    if (blendMode)
        *blendMode = surface->blend_mode;
    return true;
}

Uint32 SDL_MapSurfaceRGB(SDL_Surface* surface, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapSurfaceRGBA(surface, r, g, b, 255);
}

Uint32 SDL_MapSurfaceRGBA(SDL_Surface* surface, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    const SDL_PixelFormatDetails* details = surface ? SDL_GetPixelFormatDetails(surface->format) : &k_argb8888;
    return SDL_MapRGBA(details, nullptr, r, g, b, a);
}


/* ---- Drawing ------------------------------------------------------------- */

namespace {
// Adds the time of a scope to a profiler counter
struct ProfileScope {
    volatile Uint64& counter;
    Uint64 start;
    explicit ProfileScope(volatile Uint64& c) : counter(c), start(sdlx_cycles()) {}
    ~ProfileScope() { counter = counter + (sdlx_cycles() - start); }
};
} // namespace

bool SDL_SetSurfaceClipRect(SDL_Surface* surface, const SDL_Rect* rect)
{
    if (!checkSurface(surface, "surface"))
        return false;
    // The rect kept is its part inside the surface (all of it with no rect)
    SDL_Rect r {0, 0, surface->w, surface->h};
    if (rect) {
        const int x0 = SDL_max(rect->x, 0), y0 = SDL_max(rect->y, 0);
        const int x1 = SDL_min(rect->x + rect->w, surface->w), y1 = SDL_min(rect->y + rect->h, surface->h);
        r = {x0, y0, SDL_max(x1 - x0, 0), SDL_max(y1 - y0, 0)};
    }
    surface->clip_rect = r;
    return r.w > 0 && r.h > 0;
}

bool SDL_GetSurfaceClipRect(SDL_Surface* surface, SDL_Rect* rect)
{
    if (!checkSurface(surface, "surface") || !rect)
        return false;
    *rect = surface->clip_rect;
    return true;
}

bool SDL_FillSurfaceRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color)
{
    ProfileScope profile(g_sdlx_profile.fill);
    if (!checkSurface(dst, "dst"))
        return false;
    if (dst->format == SDL_PIXELFORMAT_ARGB4444)
        return SDL_SetError("Can't fill an ARGB4444 surface");

    const SDL_Rect& clip = dst->clip_rect;
    int x0 = clip.x, y0 = clip.y, x1 = clip.x + clip.w, y1 = clip.y + clip.h;
    if (rect) {
        x0 = SDL_max(rect->x, x0);
        y0 = SDL_max(rect->y, y0);
        x1 = SDL_min(rect->x + rect->w, x1);
        y1 = SDL_min(rect->y + rect->h, y1);
    }
    if (x1 <= x0 || y1 <= y0)
        return true;

    if (is16(dst->format)) {
        const Uint16 c = pack1555(dst->format == SDL_PIXELFORMAT_ARGB1555 ? color : color | 0xFF000000u);
        const Uint32 pair = static_cast<Uint32>(c) | (static_cast<Uint32>(c) << 16);
        for (int y = y0; y < y1; y++) {
            Uint16* d = reinterpret_cast<Uint16*>(static_cast<Uint8*>(dst->pixels) + static_cast<size_t>(y) * dst->pitch) + x0;
            Uint16* const end = d + (x1 - x0);
            if (d < end && (reinterpret_cast<uintptr_t>(d) & 2))
                *d++ = c;
            for (; end - d >= 2; d += 2)
                *reinterpret_cast<Uint32*>(d) = pair;
            if (d < end)
                *d = c;
        }
        return true;
    }

    // 16 bytes (4 pixels) per store with SSE (moves keep the bits; SSE1 has
    // no integer stores, so the color goes through a float register)
    float colorBits;
    memcpy(&colorBits, &color, sizeof(colorBits));
    const __m128 color4 = _mm_set1_ps(colorBits);
    for (int y = y0; y < y1; y++) {
        Uint32* d = rowPtr(dst, y) + x0;
        Uint32* const end = d + (x1 - x0);
        while (d < end && (reinterpret_cast<uintptr_t>(d) & 15))
            *d++ = color;
        for (; end - d >= 4; d += 4)
            _mm_store_ps(reinterpret_cast<float*>(d), color4);
        while (d < end)
            *d++ = color;
    }
    return true;
}

bool SDL_BlitSurface(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, const SDL_Rect* dstrect)
{
    ProfileScope profile(g_sdlx_profile.blit);
    g_sdlx_profile.blits = g_sdlx_profile.blits + 1;
    if (!checkSurface(src, "src") || !checkSurface(dst, "dst"))
        return false;

    BlitArea area;
    if (!clipBlit(src, srcrect, dst, dstrect, area))
        return true;
    g_sdlx_profile.blit_pixels = g_sdlx_profile.blit_pixels + static_cast<Uint32>(area.w * area.h);

    if (dst->format == SDL_PIXELFORMAT_ARGB4444)
        return SDL_SetError("Can't draw on an ARGB4444 surface");
    const PixelOp op = makePixelOp(src, dst);
    SourceRows rows(src, area.w);

    if (is16(dst->format)) {
        //A 16-bit target (the Xbox's screen and map layers, ARGB1555, as the original game drew):
        //the common cases first, without widening anything
        auto dstRow = [&](int y) {
            return reinterpret_cast<Uint16*>(static_cast<Uint8*>(dst->pixels) + static_cast<size_t>(area.dy + y) * dst->pitch) + area.dx;
        };
        const bool src1555 = src->format == SDL_PIXELFORMAT_ARGB1555;
        auto srcRow16 = [&](int y) {
            return reinterpret_cast<const Uint16*>(static_cast<const Uint8*>(src->pixels) + static_cast<size_t>(area.sy + y) * src->pitch) + area.sx;
        };
        // 16 bits onto 16: a copy is the rows' bytes (an alpha modulation without blending is one
        // too, as in SDL); a sprite (opaque or see-through) a bit test
        if (src1555 && !op.colorkey && (op.alpha_mod == 255 || !op.blend)) {
            if (!op.blend) {
                for (int y = 0; y < area.h; y++)
                    memcpy(dstRow(y), srcRow16(y), static_cast<size_t>(area.w) * 2);
            } else {
                for (int y = 0; y < area.h; y++) {
                    const Uint16* s = srcRow16(y);
                    Uint16* d = dstRow(y);
                    for (int x = 0; x < area.w; x++) {
                        const Uint16 p = s[x];
                        if (p & 0x8000u)
                            d[x] = p;
                    }
                }
            }
            return true;
        }
        // 16 bits onto 16 with an alpha modulation (the screen fades, the menus' backdrop fading in,
        // see-through sprites): the opaque pixels all get the same alpha, the channels blended in
        // place (red and blue in one multiply, 16 bits apart so they can't carry into each other),
        // not widened to 32 bits and packed back. Within a level of the pixel rules' result
        if (src1555 && !op.colorkey && op.blend) {
            const Uint32 sa = op.alpha_mod;
            if (sa == 0)
                return true;
            const Uint32 a = sa + (sa >> 7), ia = 256 - a;
            //(the target's alpha bit, as the pixel rules have it: set if it was, or if the source's
            //alpha is at least half)
            const Uint32 alphaBit = sa >= 128 ? 0x8000u : 0u;
            for (int y = 0; y < area.h; y++) {
                const Uint16* s = srcRow16(y);
                Uint16* d = dstRow(y);
                for (int x = 0; x < area.w; x++) {
                    const Uint32 p = s[x];
                    if (!(p & 0x8000u))
                        continue;
                    const Uint32 q = d[x];
                    const Uint32 rbS = ((p & 0x7C00u) << 6) | (p & 0x001Fu);    // r 16-20, b 0-4
                    const Uint32 rbD = ((q & 0x7C00u) << 6) | (q & 0x001Fu);
                    const Uint32 rb = ((rbS * a + rbD * ia) >> 8) & 0x001F001Fu;
                    const Uint32 g = (((p >> 5) & 31u) * a + ((q >> 5) & 31u) * ia) >> 8;
                    d[x] = static_cast<Uint16>(((rb >> 6) & 0x7C00u) | (rb & 0x001Fu) | (g << 5) | (q & 0x8000u) | alphaBit);
                }
            }
            return true;
        }
        // 32 bits onto 16: packed; only partly see-through pixels blended (with the target's pixel)
        const Widen& wd = widenTable(dst->format);
        if (!src1555 && !is16(src->format) && op.alpha_mod == 255 && (!op.colorkey || !op.blend)) {
            for (int y = 0; y < area.h; y++) {
                const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(src), area.sy + y) + area.sx;
                Uint16* d = dstRow(y);
                if (!op.blend && !op.colorkey) {
                    for (int x = 0; x < area.w; x++)
                        d[x] = pack1555(op.src_alpha ? s[x] : s[x] | 0xFF000000u);
                } else if (!op.blend) {
                    //(a color key only)
                    for (int x = 0; x < area.w; x++) {
                        if ((s[x] & RGB_MASK) != op.key)
                            d[x] = pack1555(s[x] | 0xFF000000u);
                    }
                } else {
                    for (int x = 0; x < area.w; x++) {
                        const Uint32 p = s[x];
                        const Uint32 a = op.src_alpha ? p >> 24 : 255;
                        if (a == 255) {
                            d[x] = pack1555(p | 0xFF000000u);
                        } else if (a != 0) {
                            Uint32 wide = wd.lo[d[x] & 0xFF] | wd.hi[d[x] >> 8];
                            op.apply(p, &wide);
                            d[x] = pack1555(wide);
                        }
                    }
                }
            }
            return true;
        }

        // The rest (an alpha modulation, a fade): each row widened, the pixel rules, packed back
        TargetRow target(dst);
        const bool copy = !op.blend && !op.colorkey;
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rows.row(area.sy + y, area.sx, area.w);
            Uint16* d16 = reinterpret_cast<Uint16*>(static_cast<Uint8*>(dst->pixels) + static_cast<size_t>(area.dy + y) * dst->pitch) + area.dx;
            if (copy) {
                //(a copy needs no widening of what it covers)
                for (int x = 0; x < area.w; x++)
                    d16[x] = pack1555(op.src_alpha ? s[x] : s[x] | 0xFF000000u);
                continue;
            }
            for (int x0 = 0; x0 < area.w; x0 += 1024) {
                const int n = area.w - x0 < 1024 ? area.w - x0 : 1024;
                Uint32* d = target.begin(area.dy + y, area.dx + x0, n);
                for (int x = 0; x < n; x++)
                    op.apply(s[x0 + x], d + x);
                target.end();
            }
        }
        return true;
    }

    // Plain copy
    if (!op.blend && !op.colorkey && (op.src_alpha || !op.dst_alpha)) {
        for (int y = 0; y < area.h; y++) {
            memcpy(rowPtr(dst, area.dy + y) + area.dx, rows.row(area.sy + y, area.sx, area.w),
                   static_cast<size_t>(area.w) * 4);
        }
        return true;
    }

    // Color key only: copy what isn't the key
    if (!op.blend && op.colorkey && op.src_alpha) {
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rows.row(area.sy + y, area.sx, area.w);
            Uint32* d = rowPtr(dst, area.dy + y) + area.dx;
            for (int x = 0; x < area.w; x++) {
                const Uint32 p = s[x];
                if ((p & RGB_MASK) != op.key)
                    d[x] = p;
            }
        }
        return true;
    }

    // An alpha modulation without a color key (the screen fades and the menu
    // shade: opaque images drawn half see-through): the opaque pixels all get
    // the same alpha, worked out once
    if (op.blend && !op.colorkey && op.alpha_mod != 255) {
        const Uint32 sa = op.alpha_mod;
        if (sa == 0)
            return true;
        const Uint32 a256 = sa + (sa >> 7);
        Uint8 dstAlpha[256];
        for (Uint32 i = 0; i < 256; i++)
            dstAlpha[i] = static_cast<Uint8>(sa + div255(i * (255 - sa)));
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rows.row(area.sy + y, area.sx, area.w);
            Uint32* d = rowPtr(dst, area.dy + y) + area.dx;
            for (int x = 0; x < area.w; x++) {
                const Uint32 p = s[x];
                if (!op.src_alpha || (p >> 24) == 0xFF) {
                    const Uint32 dv = d[x];
                    const Uint32 da = op.dst_alpha ? dstAlpha[dv >> 24] : 0xFFu;
                    d[x] = blendRgb(p, dv, a256) | (da << 24);
                } else {
                    op.apply(p, d + x);
                }
            }
        }
        return true;
    }

    // The usual sprite: per-pixel alpha (mostly 0 or 255), no color key or
    // alpha modulation
    if (op.blend && op.src_alpha && !op.colorkey && op.alpha_mod == 255) {
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rows.row(area.sy + y, area.sx, area.w);
            Uint32* d = rowPtr(dst, area.dy + y) + area.dx;
            for (int x = 0; x < area.w; x++) {
                const Uint32 p = s[x];
                const Uint32 a = p >> 24;
                if (a == 255)
                    d[x] = p;
                else if (a != 0)
                    op.apply(p, d + x);
            }
        }
        return true;
    }

    for (int y = 0; y < area.h; y++) {
        const Uint32* s = rows.row(area.sy + y, area.sx, area.w);
        Uint32* d = rowPtr(dst, area.dy + y) + area.dx;
        for (int x = 0; x < area.w; x++)
            op.apply(s[x], d + x);
    }
    return true;
}

bool SDL_BlitSurfaceScaled(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, const SDL_Rect* dstrect, SDL_ScaleMode scaleMode)
{
    (void)scaleMode;  // always nearest neighbour (the game asks for pixel art scaling)
    if (!checkSurface(src, "src") || !checkSurface(dst, "dst"))
        return false;

    SDL_Rect sr = srcrect ? *srcrect : SDL_Rect {0, 0, src->w, src->h};
    SDL_Rect dr = dstrect ? *dstrect : SDL_Rect {0, 0, dst->w, dst->h};

    // Clip the source rect to the source surface (moving the destination along)
    if (sr.x < 0 || sr.y < 0 || sr.x + sr.w > src->w || sr.y + sr.h > src->h) {
        if (sr.w <= 0 || sr.h <= 0)
            return true;
        const float fx = static_cast<float>(dr.w) / sr.w;
        const float fy = static_cast<float>(dr.h) / sr.h;
        int x0 = SDL_max(sr.x, 0), y0 = SDL_max(sr.y, 0);
        int x1 = SDL_min(sr.x + sr.w, src->w), y1 = SDL_min(sr.y + sr.h, src->h);
        dr.x += static_cast<int>((x0 - sr.x) * fx);
        dr.y += static_cast<int>((y0 - sr.y) * fy);
        dr.w = static_cast<int>((x1 - x0) * fx);
        dr.h = static_cast<int>((y1 - y0) * fy);
        sr = {x0, y0, x1 - x0, y1 - y0};
    }
    if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0)
        return true;

    // Same size: a normal blit
    if (sr.w == dr.w && sr.h == dr.h)
        return SDL_BlitSurface(src, &sr, dst, &dr);

    ProfileScope profile(g_sdlx_profile.blit);
    g_sdlx_profile.blits = g_sdlx_profile.blits + 1;

    if (dst->format == SDL_PIXELFORMAT_ARGB4444)
        return SDL_SetError("Can't draw on an ARGB4444 surface");
    const PixelOp op = makePixelOp(src, dst);
    SourceRows rows(src, sr.w);

    const SDL_Rect& clip = dst->clip_rect;
    const int dx0 = SDL_max(dr.x, clip.x), dy0 = SDL_max(dr.y, clip.y);
    const int dx1 = SDL_min(dr.x + dr.w, clip.x + clip.w), dy1 = SDL_min(dr.y + dr.h, clip.y + clip.h);
    if (dx1 <= dx0 || dy1 <= dy0)
        return true;

    // 16.16 fixed point steps
    const Uint32 stepX = (static_cast<Uint32>(sr.w) << 16) / dr.w;
    const Uint32 stepY = (static_cast<Uint32>(sr.h) << 16) / dr.h;

    for (int y = dy0; y < dy1; y++) {
        int sy = sr.y + static_cast<int>(((static_cast<Uint32>(y - dr.y) * stepY) + (stepY >> 1)) >> 16);
        if (sy >= sr.y + sr.h) sy = sr.y + sr.h - 1;
        const Uint32* s = rows.row(sy, sr.x, sr.w) - sr.x;
        Uint32 fx = static_cast<Uint32>(dx0 - dr.x) * stepX + (stepX >> 1);
        if (is16(dst->format)) {
            const Widen& wd = widenTable(dst->format);
            Uint16* d = reinterpret_cast<Uint16*>(static_cast<Uint8*>(dst->pixels) + static_cast<size_t>(y) * dst->pitch);
            if (!op.blend && !op.colorkey) {
                //(a copy: the target's pixels aren't read)
                for (int x = dx0; x < dx1; x++, fx += stepX) {
                    int sx = sr.x + static_cast<int>(fx >> 16);
                    if (sx >= sr.x + sr.w) sx = sr.x + sr.w - 1;
                    d[x] = pack1555(op.src_alpha ? s[sx] : s[sx] | 0xFF000000u);
                }
                continue;
            }
            for (int x = dx0; x < dx1; x++, fx += stepX) {
                int sx = sr.x + static_cast<int>(fx >> 16);
                if (sx >= sr.x + sr.w) sx = sr.x + sr.w - 1;
                Uint32 wide = wd.lo[d[x] & 0xFF] | wd.hi[d[x] >> 8];
                op.apply(s[sx], &wide);
                d[x] = pack1555(wide);
            }
            continue;
        }
        Uint32* d = rowPtr(dst, y);
        for (int x = dx0; x < dx1; x++, fx += stepX) {
            int sx = sr.x + static_cast<int>(fx >> 16);
            if (sx >= sr.x + sr.w) sx = sr.x + sr.w - 1;
            op.apply(s[sx], d + x);
        }
    }
    return true;
}

SDL_Surface* SDL_XboxCompactSurface(SDL_Surface* surface)
{
    if (!checkSurface(surface, "surface") || is16(surface->format))
        return nullptr;
    const bool alpha = hasAlpha(surface->format);
    const bool keyed = surface->has_colorkey;
    const Uint32 key = surface->colorkey & RGB_MASK;
    const bool opaqueAll = surface->blend_mode == SDL_BLENDMODE_NONE && !keyed;

    // Partly see-through pixels need ARGB4444; otherwise ARGB1555 (5 bits a color)
    bool partial = false;
    if (alpha && !opaqueAll) {
        for (int y = 0; y < surface->h && !partial; y++) {
            const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(surface), y);
            for (int x = 0; x < surface->w; x++) {
                const Uint32 a = s[x] >> 24;
                if (a != 0 && a != 255 && !(keyed && (s[x] & RGB_MASK) == key)) {
                    partial = true;
                    break;
                }
            }
        }
    }
    //(partly see-through pixels: kept in 32 bits; 4 bits a color was too few: their sheets showed
    //off colors. Most images are opaque, or see-through or not: those are the memory)
    if (partial)
        return nullptr;
    const SDL_PixelFormat format = SDL_PIXELFORMAT_ARGB1555;
    SDL_Surface* out = SDL_CreateSurface(surface->w, surface->h, format);
    if (!out)
        return nullptr;
    {
        char name[48] = "";
        {
            LiveLock lock;
            if (const char* from = nameOf(surface))
                snprintf(name, sizeof(name), "%s", from);
        }
        if (name[0])
            sdlx_surface_name(out, name);
    }
    for (int y = 0; y < surface->h; y++) {
        const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(surface), y);
        Uint16* d = reinterpret_cast<Uint16*>(static_cast<Uint8*>(out->pixels) + static_cast<size_t>(y) * out->pitch);
        for (int x = 0; x < surface->w; x++) {
            Uint32 p = s[x];
            Uint32 a = (alpha && !opaqueAll) ? (p >> 24) : 255;
            if (keyed && (p & RGB_MASK) == key)
                a = 0;
            if (partial) {
                //(rounded to 4 bits: +8 then the top nibble, kept in range)
                const Uint32 a4 = (a * 15 + 127) / 255;
                const Uint32 r4 = (((p >> 16) & 0xFF) * 15 + 127) / 255;
                const Uint32 g4 = (((p >> 8) & 0xFF) * 15 + 127) / 255;
                const Uint32 b4 = ((p & 0xFF) * 15 + 127) / 255;
                d[x] = static_cast<Uint16>(a4 == 0 ? 0 : (a4 << 12) | (r4 << 8) | (g4 << 4) | b4);
            } else if (a == 0) {
                d[x] = 0;
            } else {
                d[x] = static_cast<Uint16>(0x8000u | ((p >> 9) & 0x7C00u) | ((p >> 6) & 0x03E0u) | ((p >> 3) & 0x001Fu));
            }
        }
    }
    out->alpha_mod = surface->alpha_mod;
    out->blend_mode = opaqueAll ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND;
    return out;
}

bool SDL_GetRectIntersection(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* result)
{
    if (!a || !b || !result)
        return false;
    const int x0 = a->x > b->x ? a->x : b->x;
    const int y0 = a->y > b->y ? a->y : b->y;
    const int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    const int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if (x1 <= x0 || y1 <= y0) {
        *result = {0, 0, 0, 0};
        return false;
    }
    *result = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

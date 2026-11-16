// Xbox SDL3 compatibility layer: software surfaces and blitting.
//
// The game draws everything on the CPU into a 640x480 ARGB8888 surface, like it
// does on SDL's software renderer path. All surfaces here are 32-bit
// (ARGB8888, or XRGB8888 where the alpha channel is ignored). The blending
// rules follow SDL3: color key first, then the blend mode with the alpha
// modulation.

#include "sdl_internal.h"

#include <cstdlib>
#include <cstring>

namespace {
bool isSupportedFormat(SDL_PixelFormat format)
{
    return format == SDL_PIXELFORMAT_ARGB8888 || format == SDL_PIXELFORMAT_XRGB8888;
}

bool hasAlpha(SDL_PixelFormat format)
{
    return format == SDL_PIXELFORMAT_ARGB8888;
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

    // Clip to the destination surface
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (dx + w > dst->w) w = dst->w - dx;
    if (dy + h > dst->h) h = dst->h - dy;

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

// a: 0..256
inline Uint32 blendRgb(Uint32 s, Uint32 d, Uint32 a)
{
    const Uint32 ia = 256 - a;
    const Uint32 rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    const Uint32 g = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
    return rb | g;
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
            sa = (sa * alpha_mod + 127) / 255;

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
            da = sa + (dstA * (255 - sa) + 127) / 255;
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
    surface->pitch = width * 4;
    surface->refcount = 1;
    surface->has_colorkey = false;
    surface->colorkey = 0;
    surface->alpha_mod = 255;
    surface->blend_mode = hasAlpha(format) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    return surface;
}
} // namespace


/* ---- Pixel formats ------------------------------------------------------- */

const SDL_PixelFormatDetails* SDL_GetPixelFormatDetails(SDL_PixelFormat format)
{
    switch (format) {
        case SDL_PIXELFORMAT_ARGB8888: return &k_argb8888;
        case SDL_PIXELFORMAT_XRGB8888: return &k_xrgb8888;
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
    surface->pixels = calloc(1, size ? size : 4);
    if (!surface->pixels) {
        free(surface);
        SDL_SetError("Out of memory (%dx%d surface)", width, height);
        return nullptr;
    }
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
    if (!(surface->flags & SDL_SURFACE_PREALLOCATED))
        free(surface->pixels);
    free(surface);
}

SDL_Surface* SDL_ConvertSurface(SDL_Surface* surface, SDL_PixelFormat format)
{
    if (!checkSurface(surface, "surface"))
        return nullptr;

    SDL_Surface* out = SDL_CreateSurface(surface->w, surface->h, format);
    if (!out)
        return nullptr;

    const bool srcAlpha = hasAlpha(surface->format);
    for (int y = 0; y < surface->h; y++) {
        const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(surface), y);
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
    if (!out)
        return nullptr;
    for (int y = 0; y < surface->h; y++)
        memcpy(rowPtr(out, y), rowPtr(static_cast<const SDL_Surface*>(surface), y), static_cast<size_t>(surface->w) * 4);
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

bool SDL_FillSurfaceRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color)
{
    ProfileScope profile(g_sdlx_profile.fill);
    if (!checkSurface(dst, "dst"))
        return false;

    int x0 = 0, y0 = 0, x1 = dst->w, y1 = dst->h;
    if (rect) {
        x0 = SDL_max(rect->x, 0);
        y0 = SDL_max(rect->y, 0);
        x1 = SDL_min(rect->x + rect->w, dst->w);
        y1 = SDL_min(rect->y + rect->h, dst->h);
    }
    if (x1 <= x0 || y1 <= y0)
        return true;

    for (int y = y0; y < y1; y++) {
        Uint32* d = rowPtr(dst, y) + x0;
        for (int x = x0; x < x1; x++)
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

    const PixelOp op = makePixelOp(src, dst);

    // Plain copy
    if (!op.blend && !op.colorkey && (op.src_alpha || !op.dst_alpha)) {
        for (int y = 0; y < area.h; y++) {
            memcpy(rowPtr(dst, area.dy + y) + area.dx,
                   rowPtr(static_cast<const SDL_Surface*>(src), area.sy + y) + area.sx,
                   static_cast<size_t>(area.w) * 4);
        }
        return true;
    }

    // Color key only: copy what isn't the key
    if (!op.blend && op.colorkey && op.src_alpha) {
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(src), area.sy + y) + area.sx;
            Uint32* d = rowPtr(dst, area.dy + y) + area.dx;
            for (int x = 0; x < area.w; x++) {
                const Uint32 p = s[x];
                if ((p & RGB_MASK) != op.key)
                    d[x] = p;
            }
        }
        return true;
    }

    // The usual sprite: per-pixel alpha (mostly 0 or 255), no color key or
    // alpha modulation
    if (op.blend && op.src_alpha && !op.colorkey && op.alpha_mod == 255) {
        for (int y = 0; y < area.h; y++) {
            const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(src), area.sy + y) + area.sx;
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
        const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(src), area.sy + y) + area.sx;
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

    const PixelOp op = makePixelOp(src, dst);

    const int dx0 = SDL_max(dr.x, 0), dy0 = SDL_max(dr.y, 0);
    const int dx1 = SDL_min(dr.x + dr.w, dst->w), dy1 = SDL_min(dr.y + dr.h, dst->h);
    if (dx1 <= dx0 || dy1 <= dy0)
        return true;

    // 16.16 fixed point steps
    const Uint32 stepX = (static_cast<Uint32>(sr.w) << 16) / dr.w;
    const Uint32 stepY = (static_cast<Uint32>(sr.h) << 16) / dr.h;

    for (int y = dy0; y < dy1; y++) {
        int sy = sr.y + static_cast<int>(((static_cast<Uint32>(y - dr.y) * stepY) + (stepY >> 1)) >> 16);
        if (sy >= sr.y + sr.h) sy = sr.y + sr.h - 1;
        const Uint32* s = rowPtr(static_cast<const SDL_Surface*>(src), sy);
        Uint32* d = rowPtr(dst, y);
        Uint32 fx = static_cast<Uint32>(dx0 - dr.x) * stepX + (stepX >> 1);
        for (int x = dx0; x < dx1; x++, fx += stepX) {
            int sx = sr.x + static_cast<int>(fx >> 16);
            if (sx >= sr.x + sr.w) sx = sr.x + sr.w - 1;
            op.apply(s[sx], d + x);
        }
    }
    return true;
}

#include "gfx.h"

#include "gfx/Color.h"
#include "gfx/gfxPalette.h"
#include "gfx/gfxSDL.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <format>
#include <iostream>
#include <string>

using namespace std;

namespace fs = std::filesystem;

GraphicsSDL gfx;
gfxPalette gfx_palette;

namespace {
/**
 * Makes team-colored skin surface frame from a loaded sprite strip.
 * @param  source       skin sprite strip surface
 * @param  sourceFrame  frame index [0-5]
 * @param  team         team index for color [0-3]
 * @param  allStates    render all player states
 * @param  mirrored     render horizontally mirrored
 * @return              team-colored surface
 */
gfxSprite createSkinSurface(
    const gfxSprite& source,
    size_t sourceFrame,
    size_t team,
    bool allStates,
    bool mirrored)
{
    //Take the loaded skin and colorize it for each state (normal, 3 frames of invincibility, shielded, tagged, ztarred, got shine, frozen)
    const size_t outFrameCount = allStates ? PlayerPalette::COUNT : 1;

    auto out = gfxSprite::blank(32 * outFrameCount, 32);
    SDL_Surface* const dst = out.getSurface();    //(ARGB8888, see gfxSprite::blank)

    if (SDL_MUSTLOCK(dst))
        SDL_LockSurface(dst);

    if (SDL_MUSTLOCK(source.getSurface()))
        SDL_LockSurface(source.getSurface());

    //(opaque ARGB8888: what SDL_MapSurfaceRGB gives for that format, without its lookup per pixel)
    auto argb = [](const RGB& c) {
        return 0xff000000u | (static_cast<Uint32>(c.r) << 16) | (static_cast<Uint32>(c.g) << 8) | static_cast<Uint32>(c.b);
    };

    const int startX = sourceFrame * 32;

    for (int y = 0; y < 32; y++) {
        Uint32* const row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(dst->pixels) + static_cast<ptrdiff_t>(y) * dst->pitch);
        for (int srcX = 0; srcX < 32; srcX++) {
            const int dstX = mirrored ? (31 - srcX) : srcX;

            const RGB pixelColor = getRgb(source.getSurface(), startX + srcX, y);

            const auto it = gfx_palette.colorSheets().find(pixelColor);
            if (it != gfx_palette.colorSheets().cend()) {
                const ColorSheet& sheet = it->second;
                for (size_t outFrame = 0; outFrame < outFrameCount; outFrame++)
                    row[outFrame * 32 + dstX] = argb(sheet.replacementFor(team, static_cast<PlayerPalette>(outFrame)));
            } else {
                const Uint32 pixel = argb(pixelColor);
                for (size_t outFrame = 0; outFrame < outFrameCount; outFrame++)
                    row[outFrame * 32 + dstX] = pixel;
            }
        }
    }

    SDL_UnlockSurface(source.getSurface());
    SDL_UnlockSurface(dst);

    const auto color_key = SDL_MapSurfaceRGB(out.getSurface(), colors::MAGENTA.r, colors::MAGENTA.g, colors::MAGENTA.b);
    if (!SDL_SetSurfaceColorKey(out.getSurface(), true, color_key)) {
        throw std::format("Couldn't set color key for new skin surface: {}", SDL_GetError());
    }

    return out;
}

bool validSkinSurface(const gfxSprite& skin)
{
    return skin.getWidth() == 192 && skin.getHeight() == 32;
}
} // namespace

Uint32 getRawPixel(SDL_Surface* surf, int x, int y)
{
    assert(surf);
    assert(0 <= x && x < surf->w);
    assert(0 <= y && y < surf->h);

    const Uint8 bpp = SDL_BYTESPERPIXEL(surf->format);
    const size_t idx = y * surf->pitch + x * bpp;
    const auto* pixel8 = static_cast<Uint8*>(surf->pixels) + idx;

    switch (bpp) {
        case 1: return *pixel8;
        case 2: return *reinterpret_cast<const Uint16*>(pixel8);
        case 3: return (SDL_BYTEORDER == SDL_BIG_ENDIAN)
            ? pixel8[0] << 16 | pixel8[1] << 8 | pixel8[2]
            : pixel8[0] | pixel8[1] << 8 | pixel8[2] << 16;
        case 4: return *reinterpret_cast<const Uint32*>(pixel8);
    }
    assert(false);
    return 0x0;
}

RGB getRgb(SDL_Surface* surf, int x, int y)
{
    const Uint32 rawPixel = getRawPixel(surf, x, y);
    RGB color {};
    SDL_GetRGB(rawPixel, SDL_GetPixelFormatDetails(surf->format), SDL_GetSurfacePalette(surf), &color.r, &color.g, &color.b);
    return color;
}

bool gfx_init(int w, int h, bool fullscreen) {
    return gfx.init(fullscreen);
}

void gfx_changefullscreen(bool fullscreen) {
#ifdef SMW_XBOX
    //(the Xbox is always full screen: Alt+Enter and a saved setting do nothing)
    (void)fullscreen;
#else
    gfx.changeFullScreen(fullscreen);
#endif
}

void gfx_eventtoscreen(SDL_Event* event)
{
    if (event)
        gfx.eventToScreen(*event);
}

SDL_MouseButtonFlags gfx_mousestate(float* x, float* y)
{
    float mx = 0;
    float my = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    gfx.windowToScreen(mx, my);
    if (x)
        *x = mx;
    if (y)
        *y = my;
    return buttons;
}

bool gfx_isfullscreen() {
    return gfx.isFullScreen();
}

void gfx_flipscreen() {
    gfx.flipScreen();
}

bool gfx_setvsync(bool enabled) {
    return gfx.setVSync(enabled);
}

float gfx_refreshrate() {
    return gfx.refreshRate();
}

void gfx_settitle(const char* title) {
    gfx.setTitle(title);
}

void gfx_show_catched_error(const std::string& error)
{
    std::string message = ""
        "It seems the game has unexpectedly crashed. If you could tell us\n"
        "what happened exactly, we might be able to fix this bug. Consider\n"
        "reporting it on the link below, thanks!\n\n"
        "https://github.com/mmatyas/supermariowar/issues\n\n"
        "Sincerely,\nThe Developers";
    if (!error.empty()) {
        message += "\n\n\nThe error message:\n" + error;
    }
    fprintf(stderr, "\n%s\n", message.c_str());
    gfx.showErrorBox(message.c_str());
}

void gfx_take_screenshot() {
    gfx.takeScreenshot();
}

bool gfx_save_screenshot(const std::string& path) {
    return gfx.saveScreenshot(path);
}

void gfx_close() {}
bool gfx_loadpalette(const std::filesystem::path& palette_path) {
    return gfx_palette.load(palette_path);
}

SpriteStrip gfx_loadmenuskin(const fs::path& path, short colorScheme, bool fLoadBothDirections)
{
    const gfxSprite skin = ImageLoader(path).withoutColorKey().create();

    if (!validSkinSurface(skin))
        throw std::format("Invalid skin file: {} has incorrect dimensions", path.generic_string());

    SpriteStrip strip;

    for (size_t i = 0; i < 2; i++) {
        strip[i * 2] = createSkinSurface(skin, i, colorScheme, true, false);
    }

    if (fLoadBothDirections) {
        for (size_t i = 0; i < 2; i++) {
            strip[i * 2 + 1] = createSkinSurface(skin, i, colorScheme, true, true);
        }
    }

    return strip;
}

SpriteStrip gfx_loadfullskin(const fs::path& path, short colorScheme)
{
    const gfxSprite skin = ImageLoader(path).withoutColorKey().create();

    if (!validSkinSurface(skin))
        throw std::format("Invalid skin file: {} has incorrect dimensions", path.generic_string());

    SpriteStrip strip;

    for (size_t k = 0; k < 4; k++) {
        for (size_t j = 0; j < 2; j++) {
            strip[(k * 2) + j] = createSkinSurface(skin, k, colorScheme, true, j != 0);
        }
    }

    //Dead Flying Sprite
    strip[8] = createSkinSurface(skin, 4, colorScheme, true, false);

    //Dead Stomped Sprite
    strip[9] = createSkinSurface(skin, 5, colorScheme, true, false);

    return strip;
}

bool gfx_darken(SDL_Surface* surface, Uint8 alpha)
{
    if (!surface || SDL_BYTESPERPIXEL(surface->format) != 4)
        return false;
    if (alpha == 0)
        return true;
    if (!SDL_LockSurface(surface))
        return false;
    //Each color channel times (255 - alpha) / 255; the alpha channel is kept
    const Uint32 keep = 255u - alpha;
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(surface->format);
    const Uint32 alphaMask = details ? details->Amask : 0;
    //(the size in locals: the build has no strict aliasing, so the compiler would reload them after
    //every pixel written instead of vectorizing the loop)
    const int w = surface->w;
    const int h = surface->h;
    const int pitch = surface->pitch;
    Uint8* const pixels = static_cast<Uint8*>(surface->pixels);
    for (int y = 0; y < h; y++) {
        Uint32* const row = reinterpret_cast<Uint32*>(pixels + static_cast<ptrdiff_t>(y) * pitch);
        for (int x = 0; x < w; x++) {
            const Uint32 p = row[x];
            const Uint32 rb = (((p & 0x00ff00ffu) * keep + 0x00800080u) >> 8) & 0x00ff00ffu;
            const Uint32 ga = (((p >> 8) & 0x00ff00ffu) * keep + 0x00800080u) & 0xff00ff00u;
            row[x] = ((rb | ga) & ~alphaMask) | (p & alphaMask);
        }
    }
    SDL_UnlockSurface(surface);
    return true;
}

void gfx_cliprect(SDL_Rect& srcRect, SDL_Rect& dstRect, const SDL_Rect& clipArea)
{
    if (dstRect.x >= clipArea.x + clipArea.w
        || dstRect.x + dstRect.w < clipArea.x
        || dstRect.y >= clipArea.y + clipArea.h
        || dstRect.y + dstRect.h < clipArea.y)
    {
        srcRect.w = 0;
        srcRect.h = 0;
        return;
    }

    if (dstRect.x < clipArea.x) {
        int iDiffX = clipArea.x - dstRect.x;
        srcRect.x += iDiffX;
        srcRect.w -= iDiffX;
        dstRect.x = clipArea.x;
    }

    if (dstRect.x + dstRect.w >= clipArea.x + clipArea.w) {
        int iDiffX = dstRect.x + dstRect.w - clipArea.x - clipArea.w;
        srcRect.w -= iDiffX;
    }

    if (dstRect.y < clipArea.y) {
        int iDiffY = clipArea.y - dstRect.y;
        srcRect.y += iDiffY;
        srcRect.h -= iDiffY;
        dstRect.y = clipArea.y;
    }

    if (dstRect.y + dstRect.h >= clipArea.y + clipArea.h) {
        int iDiffY = dstRect.y + dstRect.h - clipArea.y - clipArea.h;
        srcRect.h -= iDiffY;
    }
}

bool gfx_adjusthiddenrects(SDL_Rect& src, SDL_Rect& dst, ClipEdge edge, int threshold)
{
    switch (edge) {
        case ClipEdge::Top: {
            if (threshold < dst.y) // fully visible
                break;
            if (dst.y + dst.h <= threshold) // fully hidden
                return true;

            const int diff = threshold - dst.y;
            src.y += diff;
            src.h -= diff;
            dst.y = threshold + y_shake;
            dst.h = src.h;
            break;
        }
        case ClipEdge::Right: {
            if (dst.x + src.w < threshold) // fully visible
                break;
            if (threshold <= dst.x) // fully hidden
                return true;

            src.w = threshold - dst.x;
            dst.w = src.w;
            break;
        }
        case ClipEdge::Bottom: {
            if (dst.y + src.h < threshold) // fully visible
                break;
            if (threshold <= dst.y) // fully hidden
                return true;

            src.h = threshold - dst.y;
            dst.h = src.h;
            break;
        }
        case ClipEdge::Left: {
            if (threshold < dst.x) // fully visible
                break;
            if (dst.x + src.w <= threshold) // fully hidden
                return true;

            int diff = threshold - dst.x;
            src.x += diff;
            src.w -= diff;
            dst.x = threshold + x_shake;
            dst.w = src.w;
            break;
        }
    }

    return false;
}

void gfx_drawpreview(
    gfxSprite& sprite,
    short dstX, short dstY,
    short srcX, short srcY,
    short iw, short ih,
    const SDL_Rect& clipRect,
    bool wrap,
    std::optional<std::pair<ClipEdge, int>> clip)
{
    //need to set source rect before each blit so it can be clipped correctly
    SDL_Rect rSrcRect = {srcX, srcY, iw, ih};
    SDL_Rect rDstRect = {dstX, dstY, iw, ih};

    gfx_cliprect(rSrcRect, rDstRect, clipRect);

    if (clip) {
        auto [edge, threshold] = *clip;
        if (gfx_adjusthiddenrects(rSrcRect, rDstRect, edge, threshold))
            return;
    }

    // Blit onto the screen surface
    sprite.draw(rSrcRect, blitdest, rDstRect);

    if (wrap) {
        //Deal with wrapping over sides of screen
        bool fBlitSide = false;
        if (dstX < clipRect.x) {
            rDstRect.x = dstX + 320; // TODO: Get it from a global setting.
            fBlitSide = true;
        } else if (dstX + iw >= clipRect.x + clipRect.w) {
            rDstRect.x = dstX - 320; // TODO: Get it from a global setting.
            fBlitSide = true;
        }

        if (fBlitSide) {
            //need to set source rect before each blit so it can be clipped correctly
            rSrcRect.x = srcX;
            rSrcRect.y = srcY;
            rSrcRect.w = iw;
            rSrcRect.h = ih;

            rDstRect.y = dstY;
            rDstRect.w = iw;
            rDstRect.h = ih;

            gfx_cliprect(rSrcRect, rDstRect, clipRect);

            if (clip) {
                auto [edge, threshold] = *clip;
                if (gfx_adjusthiddenrects(rSrcRect, rDstRect, edge, threshold))
                    return;
            }

            sprite.draw(rSrcRect, blitdest, rDstRect);
        }
    }
}

void gfx_setjoystickteamcolor(SDL_Joystick* joystick, short team, float brightness)
{
    if (!joystick || team < 0) {
        // A non-playing user has no team
        return;
    }
    brightness = max(0.f, min(1.f, brightness));
    if (std::optional<RGB> color = gfx_palette.replacementFor(RGB {0x80, 0x00, 0x00}, team, PlayerPalette::normal)) {
        SDL_SetJoystickLED(joystick, static_cast<Uint8>(brightness * color->r), static_cast<Uint8>(brightness * color->g), static_cast<Uint8>(brightness * color->b));
    }
}

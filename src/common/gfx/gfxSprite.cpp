#include "gfxSprite.h"
#include "LoadLog.h"
#include "LoadScreen.h"
#include "Platform.h"

#include "gfx.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cassert>
#include <mutex>
#include <unordered_set>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>

namespace fs = std::filesystem;

namespace {
// A constant transparency made part of the picture: the color key becomes transparent pixels and
// every pixel's alpha is scaled by `alpha`. SDL blits per-pixel alpha with its fast (SIMD) blitters,
// but a surface with an alpha modulation goes through its slowest, generic one: the eyecandy, the
// score panels' shade and the like were most of the drawing time.
SdlSurfacePtr bakeAlpha(SdlSurfacePtr img, Uint8 alpha, std::optional<RGB> colorKey)
{
    SdlSurfacePtr argb(SDL_ConvertSurface(img.get(), SDL_PIXELFORMAT_ARGB8888));
    if (!argb)
        return img;
    const bool keyed = colorKey.has_value();
    const Uint32 key = keyed ? (static_cast<Uint32>(colorKey->r) << 16) | (static_cast<Uint32>(colorKey->g) << 8) | colorKey->b : 0;
    if (!SDL_LockSurface(argb.get()))
        return img;
    for (int y = 0; y < argb->h; y++) {
        Uint32* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(argb->pixels) + static_cast<ptrdiff_t>(y) * argb->pitch);
        for (int x = 0; x < argb->w; x++) {
            const Uint32 p = row[x];
            if (keyed && (p & 0x00ffffffu) == key) {
                row[x] = p & 0x00ffffffu;
                continue;
            }
            const Uint32 a = ((p >> 24) * alpha + 127) / 255;
            row[x] = (p & 0x00ffffffu) | (a << 24);
        }
    }
    SDL_UnlockSurface(argb.get());
    SDL_SetSurfaceColorKey(argb.get(), false, 0);
    SDL_SetSurfaceBlendMode(argb.get(), SDL_BLENDMODE_BLEND);
    return argb;
}

// (No RLE acceleration: SDL's RLE blits walk every row of the sheet up to the part drawn, so the
// small pieces of wide sheets the game draws (glyphs, tiles, animation frames) cost far more than
// with its plain blitters: drawing took about twice as long. The Xbox SDL ignores RLE anyway.)
// Every pixel's alpha full (a surface in the screen's format, 32 bits a pixel)
bool isOpaque(SDL_Surface* surface)
{
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(surface->format);
    if (!details || details->bits_per_pixel != 32 || details->Amask == 0)
        return false;
    if (!SDL_LockSurface(surface))
        return false;
    bool opaque = true;
    for (int y = 0; y < surface->h && opaque; y++) {
        const Uint32* row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
        for (int x = 0; x < surface->w; x++) {
            if ((row[x] & details->Amask) != details->Amask) {
                opaque = false;
                break;
            }
        }
    }
    SDL_UnlockSurface(surface);
    return opaque;
}

SdlSurfacePtr loadImage(
    const fs::path& path,
    std::optional<RGB> color_key = std::nullopt,
    std::optional<Uint8> alpha = std::nullopt,
    bool bake = false)
{
    const std::string path_str = path.generic_string();

    std::cout << "loading sprite ";
    if (color_key) {
        std::cout << "with key";
        if (alpha) {
            std::cout << "+alpha";
        }
        std::cout << " ";
    }
    std::cout << path_str << " ...";

    Uint64 t = loadlog::nowNs();
    auto raw = SdlSurfacePtr(IMG_Load(path_str.c_str()));
    if (raw)
        loadlog::count("image files loaded (IMG_Load)", loadlog::nowNs() - t, static_cast<Uint64>(raw->w) * raw->h * 4);
    if (!raw) {
        throw std::format("Couldn't load {}: {}", path_str, SDL_GetError());
    }

    if (color_key) {
        const Uint32 key = SDL_MapSurfaceRGB(raw.get(), color_key->r, color_key->g, color_key->b);
        if (!SDL_SetSurfaceColorKey(raw.get(), true, key)) {
            throw std::format("Couldn't set color key for {}: {}", path_str, SDL_GetError());
        }
    }

    t = loadlog::nowNs();
    //(32 bits, the loading's rules work on those (an image is made compact after, ImageLoader::load;
    //the Xbox's screen is 16). Loaded in 32 bits already, and no key to turn into transparency: the
    //conversion would only copy it, a second big allocation for an ingame tileset)
    const bool convert = color_key || raw->format != SDL_PIXELFORMAT_ARGB8888;
    auto img = !convert ? std::move(raw) : SdlSurfacePtr(SDL_ConvertSurface(raw.get(), SDL_PIXELFORMAT_ARGB8888));
    if (img && convert)
        loadlog::count("images converted (their color key)", loadlog::nowNs() - t, static_cast<Uint64>(img->w) * img->h * 4);
    t = loadlog::nowNs();
    if (!img) {
        throw std::format("Couldn't convert {} to the display's pixel format: {}", path_str, SDL_GetError());
    }

    const bool baked = alpha && bake && *alpha < 255;
    if (baked)
        img = bakeAlpha(std::move(img), *alpha, color_key);

    //Opaque all over (no key, no alpha asked for, every pixel's alpha full: the backgrounds): copied,
    //not blended. Converted to the screen's format, with its alpha channel, it would blend by
    //default, to the same pixels, but far slower: the game's back layer and the menus' backdrop are
    //a whole screen each, every frame (the Xbox's SDL blends a pixel at a time; a copy is a memcpy)
    if (!alpha && !color_key && !SDL_SurfaceHasColorKey(img.get()) && isOpaque(img.get()))
        SDL_SetSurfaceBlendMode(img.get(), SDL_BLENDMODE_NONE);

    loadlog::count("images' alpha baked, opaque checked", loadlog::nowNs() - t);

    if (alpha && !baked) {
        if (!SDL_SetSurfaceBlendMode(img.get(), SDL_BLENDMODE_BLEND)) {
            throw std::format("Couldn't set blend mode for {}: {}", path_str, SDL_GetError());
        }
        if (!SDL_SetSurfaceAlphaMod(img.get(), *alpha)) {
            throw std::format("Couldn't set alpha modulation for {}: {}", path_str, SDL_GetError());
        }
    }

    std::cout << " done" << std::endl;
    return img;
}

void blitSurface(SDL_Surface* src, const SDL_Rect* srcArea, SDL_Surface* dst, SDL_Rect* dstArea)
{
    if (!SDL_BlitSurface(src, srcArea, dst, dstArea)) {
        fprintf(stderr, "SDL_BlitSurface error: %s\n", SDL_GetError());
    }
}
} // namespace

gfxSprite::gfxSprite(SdlSurfacePtr image, std::optional<int> wrap)
    : m_picture(std::move(image))
    , m_wrap_x(wrap)
{}


/* ---- Groups: sprites freed and loaded again --------------------------------- */

namespace {
// The sprites in groups (the main thread's: a preview's worker loads tilesets, never in a group)
std::unordered_set<gfxSprite*>& groupSprites()
{
    static auto* sprites = new std::unordered_set<gfxSprite*>;    //(never freed: sprites outlive it at exit)
    return *sprites;
}
thread_local SpriteGroup t_loadingGroup = SpriteGroup::None;
thread_local bool t_loadingDeferred = false;
thread_local bool t_compactAll = false;    // (gfxSprite::CompactLoading)
} // namespace

gfxSprite::CompactLoading::CompactLoading()
    : m_previous(t_compactAll)
{
    t_compactAll = true;
}

gfxSprite::CompactLoading::~CompactLoading()
{
    t_compactAll = m_previous;
}

gfxSprite::LoadingGroup::LoadingGroup(SpriteGroup group, bool deferred)
    : m_previous(t_loadingGroup)
    , m_previousDeferred(t_loadingDeferred)
{
    t_loadingGroup = group;
    t_loadingDeferred = deferred;
}

gfxSprite::LoadingGroup::~LoadingGroup()
{
    t_loadingGroup = m_previous;
    t_loadingDeferred = m_previousDeferred;
}

void gfxSprite::track()
{
    if (m_group != SpriteGroup::None && m_source)
        groupSprites().insert(this);
}

void gfxSprite::untrack()
{
    if (m_group != SpriteGroup::None)
        groupSprites().erase(this);
}

gfxSprite::gfxSprite(gfxSprite&& other) noexcept
    : m_picture(std::move(other.m_picture))
    , m_source(std::move(other.m_source))
    , m_group(other.m_group)
    , m_alphaSet(other.m_alphaSet)
    , m_wrap_x(other.m_wrap_x)
    , m_bakedAlpha(other.m_bakedAlpha)
    , m_opaque(other.m_opaque)
{
    other.untrack();
    other.m_group = SpriteGroup::None;
    track();
}

gfxSprite& gfxSprite::operator=(gfxSprite&& other) noexcept
{
    if (this == &other)
        return *this;
    untrack();
    other.untrack();
    m_picture = std::move(other.m_picture);
    m_source = std::move(other.m_source);
    m_group = other.m_group;
    m_alphaSet = other.m_alphaSet;
    m_wrap_x = other.m_wrap_x;
    m_bakedAlpha = other.m_bakedAlpha;
    m_opaque = other.m_opaque;
    other.m_group = SpriteGroup::None;
    track();
    return *this;
}

gfxSprite::~gfxSprite()
{
    untrack();
}

void gfxSprite::setGroup(SpriteGroup group)
{
    untrack();
    m_group = group;
    track();
}

void gfxSprite::keepLoaded()
{
    picture();
    untrack();
    m_source.reset();
    m_group = SpriteGroup::None;
}

void gfxSprite::unloadGroup(SpriteGroup group)
{
    for (gfxSprite* sprite : groupSprites()) {
        if (sprite->m_group == group)
            sprite->m_picture.reset();
    }
}

void gfxSprite::loadGroup(SpriteGroup group)
{
    for (gfxSprite* sprite : groupSprites()) {
        if (sprite->m_group == group)
            sprite->picture();
    }
}

void gfxSprite::prepareGroup(SpriteGroup group)
{
    for (gfxSprite* sprite : groupSprites()) {
        if (sprite->m_group == group && !sprite->m_picture && sprite->m_source)
            sprite->m_source->load(true);    //(into the cache; the picture let go at once)
    }
}

void gfxSprite::reload() const
{
    //(the map preview's worker draws sprites too: one load at a time, and not twice)
    static std::mutex s_reloading;
    const std::lock_guard<std::mutex> lock(s_reloading);
    if (m_picture)
        return;
    const ImageLoader& from = *m_source;
    m_picture = from.load(true);
    SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
    m_opaque = m_picture && SDL_GetSurfaceBlendMode(m_picture.get(), &mode) && mode == SDL_BLENDMODE_NONE;
    if (m_picture && m_alphaSet)
        const_cast<gfxSprite*>(this)->setalpha(*m_alphaSet);
}

gfxSprite gfxSprite::blank(unsigned w, unsigned h, std::optional<int> wrap)
{
    auto surf = SdlSurfacePtr(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888));
    if (!surf)
        throw std::format("Couldn't create blank surface: {}", SDL_GetError());

    if (!SDL_SetSurfaceBlendMode(surf.get(), SDL_BLENDMODE_NONE))
        throw std::format("Couldn't set blend mode for blank surface: {}", SDL_GetError());

    return gfxSprite(std::move(surf), std::move(wrap));
}

gfxSprite gfxSprite::blankScreenFormat(unsigned w, unsigned h, std::optional<int> wrap)
{
    auto surf = SdlSurfacePtr(SDL_CreateSurface(w, h, platform::screenFormat()));
    if (!surf)
        throw std::format("Couldn't create blank surface: {}", SDL_GetError());
    SDL_SetSurfaceBlendMode(surf.get(), SDL_BLENDMODE_NONE);
    return gfxSprite(std::move(surf), std::move(wrap));
}

void gfxSprite::draw(int x, int y) const
{
    blit(nullptr, blitdest, {x + x_shake, y + y_shake});
}

void gfxSprite::draw(int x, int y, const SDL_Rect& srcRect) const
{
    blit(&srcRect, blitdest, {x + x_shake, y + y_shake});
}

void gfxSprite::blit(const SDL_Rect* srcRect, SDL_Surface* dst, Vec2i dstPos) const
{
    assert(picture());

    SDL_Rect dstRect { dstPos.x, dstPos.y, 0, 0 };
    blitSurface(picture(), srcRect, dst, &dstRect);

    if (m_wrap_x) {
        if (dstRect.x + getWidth() >= *m_wrap_x) {
            dstRect = { dstPos.x - *m_wrap_x, dstPos.y, 0, 0 };  // NOTE: SDL2 modifies the dst rect
            blitSurface(picture(), srcRect, dst, &dstRect);
        } else if (dstRect.x < 0) {
            dstRect = { dstPos.x + *m_wrap_x, dstPos.y, 0, 0 };  // NOTE: SDL2 modifies the dst rect
            blitSurface(picture(), srcRect, dst, &dstRect);
        }
    }
}

void gfxSprite::draw(int x, int y, const SDL_Rect& srcRect, ClipEdge clipEdge, int clipTreshold) const
{
    assert(picture());

    const SDL_Rect dstRect {x + x_shake, y + y_shake, srcRect.w, srcRect.h};
    SDL_Rect adjustedSrcRect = srcRect;
    SDL_Rect adjustedDstRect = dstRect;

    if (gfx_adjusthiddenrects(adjustedSrcRect, adjustedDstRect, clipEdge, clipTreshold))
        return;

    blitSurface(picture(), &adjustedSrcRect, blitdest, &adjustedDstRect);

    if (m_wrap_x) {
        if (x + srcRect.w >= *m_wrap_x) {
            adjustedSrcRect = srcRect;
            adjustedDstRect = dstRect;
            adjustedDstRect.x -= *m_wrap_x;

            if (gfx_adjusthiddenrects(adjustedSrcRect, adjustedDstRect, clipEdge, clipTreshold))
                return;

            blitSurface(picture(), &adjustedSrcRect, blitdest, &adjustedDstRect);
        } else if (x < 0) {
            adjustedSrcRect = srcRect;
            adjustedDstRect = dstRect;
            adjustedDstRect.x += *m_wrap_x;

            if (gfx_adjusthiddenrects(adjustedSrcRect, adjustedDstRect, clipEdge, clipTreshold))
                return;

            blitSurface(picture(), &adjustedSrcRect, blitdest, &adjustedDstRect);
        }
    }
}

void gfxSprite::drawStretch(const SDL_Rect& srcRect, SDL_Surface* dst, const SDL_Rect& dstRect) const
{
    assert(picture());

    if (!SDL_BlitSurfaceScaled(picture(), &srcRect, dst, &dstRect, SDL_SCALEMODE_PIXELART)) {
        fprintf(stderr, "SDL_BlitScaled error: %s\n", SDL_GetError());
    }
}

void gfxSprite::setalpha(Uint8 alpha)
{
    m_alphaSet = alpha;
    if (!picture())
        return;

    //(its loading alpha is in its pixels: relative to that, it can only get more transparent)
    if (m_bakedAlpha < 255)
        alpha = static_cast<Uint8>(std::min(255, alpha * 255 / std::max<int>(1, m_bakedAlpha)));

    //(opaque, and not see-through any more: copied again, not blended: the menus' backdrop after the
    //splash screen fades it in)
    if (m_opaque && alpha == 255) {
        SDL_SetSurfaceAlphaMod(m_picture.get(), 255);
        SDL_SetSurfaceBlendMode(m_picture.get(), SDL_BLENDMODE_NONE);
        return;
    }
    if (!SDL_SetSurfaceBlendMode(m_picture.get(), SDL_BLENDMODE_BLEND)) {
        fprintf(stderr, "\n ERROR: couldn't set blend mode on sprite: %s\n", SDL_GetError());
        return;
    }
    if (!SDL_SetSurfaceAlphaMod(m_picture.get(), alpha)) {
        fprintf(stderr, "\n ERROR: couldn't set alpha on sprite: %s\n", SDL_GetError());
        return;
    }
}

void gfxSprite::setWrap(short wrapsize)
{
    m_wrap_x = wrapsize;
}

SdlSurfacePtr ImageLoader::load(bool compact) const
{
    //(compact, in less memory where the platform has it (16 bits on the Xbox, as the original game's
    //images), and kept on its cache drive as it's drawn: the next load reads that, half the bytes and
    //nothing to convert)
    const bool small = compact || m_compact;
    const std::string file = m_path.string();
    loadscreen::step();    //(the loading screen's bar, while it's up)
    std::string key;
    const unsigned long long t0 = loadlog::nowNs();
    if (small) {
        key = file + (m_color_key ? std::format("|k{:02x}{:02x}{:02x}", m_color_key->r, m_color_key->g, m_color_key->b) : "|-") +
              (m_alpha ? std::format("|a{}", *m_alpha) : "|-") + (m_changingAlpha ? "|c" : "|-");
        if (SDL_Surface* cached = platform::loadCompactImage(file, key)) {
            loadlog::count("compact images from the cache drive", loadlog::nowNs() - t0);
            return SdlSurfacePtr(cached);
        }
    }
    const unsigned long long t1 = loadlog::nowNs();
    SdlSurfacePtr picture = loadImage(m_path, m_color_key, m_alpha, !m_changingAlpha);
    const unsigned long long t2 = loadlog::nowNs();
    unsigned long long t3 = t2, t4 = t2;
    if (picture && small) {
        if (SDL_Surface* compacted = platform::compactSurface(picture.get())) {
            picture.reset(compacted);
            t3 = loadlog::nowNs();
            platform::saveCompactImage(file, key, picture.get());
            t4 = loadlog::nowNs();
            loadlog::count("images compacted (16 bits)", t3 - t2);
            loadlog::count("compact images written to the cache drive", t4 - t3);
        }
    }
    //(a slow one by name: where a first load's time goes)
    if (t4 - t0 > 40000000ull) {
        const std::string text = std::format("slow image {} ms: {} (cache look {}, load {}, compact {}, save {})", (t4 - t0) / 1000000,
            file, (t1 - t0) / 1000000, (t2 - t1) / 1000000, (t3 - t2) / 1000000, (t4 - t3) / 1000000);
        loadlog::note(text.c_str());
    }
    return picture;
}

gfxSprite ImageLoader::create() const
{
    gfxSprite sprite;
    sprite.m_wrap_x = m_wrap_x;
    if (m_alpha && !m_changingAlpha && *m_alpha < 255)
        sprite.m_bakedAlpha = *m_alpha;
    if (t_loadingGroup != SpriteGroup::None) {
        sprite.m_source = std::make_shared<const ImageLoader>(*this);
        sprite.m_group = t_loadingGroup;
    }
    //(deferred: loaded when it's first used)
    if (!(t_loadingDeferred && sprite.m_source)) {
        sprite.m_picture = load(t_loadingGroup != SpriteGroup::None || t_compactAll);
        SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
        sprite.m_opaque = sprite.m_picture && SDL_GetSurfaceBlendMode(sprite.m_picture.get(), &mode) && mode == SDL_BLENDMODE_NONE;
    }
    sprite.track();
    return sprite;
}

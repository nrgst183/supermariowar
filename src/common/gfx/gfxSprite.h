#ifndef GFX_SPRITE
#define GFX_SPRITE

#include "gfx/Color.h"
#include "math/Vec2.h"
#include "util/SdlHelpers.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <optional>

enum class ClipEdge : unsigned char { Top, Right, Bottom, Left };

// Sprites loaded in a group (gfxSprite::LoadingGroup) can be freed and are loaded again (from their
// file) when they're next used: the Xbox has 64 MB, and the menus' and the matches' graphics don't
// all fit at once. Shared: a menu's sprite that the matches use too (kept with the game's).
enum class SpriteGroup : unsigned char { None, Menu, Game, World, Shared };

class ImageLoader;

class gfxSprite {
public:
    explicit gfxSprite() = default;
    explicit gfxSprite(SdlSurfacePtr image, std::optional<int> wrap = 640);
    gfxSprite(gfxSprite&& other) noexcept;
    gfxSprite& operator=(gfxSprite&& other) noexcept;
    ~gfxSprite();

    /// The sprites loaded (ImageLoader::create) while one is alive are in `group`; with `deferred`,
    /// their pictures aren't loaded until they're used (or the group is loaded)
    class LoadingGroup {
    public:
        explicit LoadingGroup(SpriteGroup group, bool deferred = false);
        ~LoadingGroup();
        LoadingGroup(const LoadingGroup&) = delete;
        LoadingGroup& operator=(const LoadingGroup&) = delete;

    private:
        SpriteGroup m_previous;
        bool m_previousDeferred;
    };
    /// The images loaded (ImageLoader::create) on this thread while one is alive are made compact
    /// (16 bits on the Xbox): an editor's session, whose images are only drawn from
    class CompactLoading {
    public:
        CompactLoading();
        ~CompactLoading();
        CompactLoading(const CompactLoading&) = delete;
        CompactLoading& operator=(const CompactLoading&) = delete;

    private:
        bool m_previous;
    };
    /// Frees the group's pictures (each is loaded again when it's used)
    static void unloadGroup(SpriteGroup group);
    /// Loads the group's pictures that aren't loaded (a match's, before it starts: not while it runs)
    static void loadGroup(SpriteGroup group);
    /// The group's pictures that aren't loaded: loaded and let go again, one at a time (into the
    /// platform's cache: on the Xbox, a first boot's work done before a match, not as it starts)
    static void prepareGroup(SpriteGroup group);
    /// Moves the sprite to another group (Shared: a menu's sprite the matches use)
    void setGroup(SpriteGroup group);
    /// The picture changed after it was loaded (drawn on): it can't be loaded again, so it's kept
    void keepLoaded();

    static gfxSprite blank(unsigned w, unsigned h, std::optional<int> wrap = 640);
    /// A blank surface in the screen's format: 16 bits on the Xbox (ARGB1555, its alpha bit the
    /// see-through parts), 32 elsewhere. For one drawn on and then drawn to the screen
    static gfxSprite blankScreenFormat(unsigned w, unsigned h, std::optional<int> wrap = 640);

    /// Draw the whole sprite at the given coordinate. Applies camera shaking.
    void draw(int x, int y) const;
    /// Draw part of the sprite at the given coordinate. Applies camera shaking.
    void draw(int x, int y, const SDL_Rect& srcRect) const;
    /// Draw part of the sprite at the given coordinate, clipped along a given direction.
    /// Used mainly for warping and drawing previews. Applies camera shaking.
    /// TODO: Most of the caller sites don't use std::optional. By updating the code there
    /// this function could be merged with the one without the clip parameters.
    void draw(int x, int y, const SDL_Rect& srcRect, ClipEdge clipEdge, int clipTreshold) const;

    /// Draw the whole sprite on a target surface at a target coordinate.
    void draw(SDL_Surface* dst, Vec2i dstPos) const {
        blit(nullptr, dst, dstPos);
    }
    /// Draw the whole sprite on a target surface at a target coordinate.
    void draw(SDL_Surface* dst, const SDL_Rect& dstRect) const {
        blit(nullptr, dst, Vec2i {dstRect.x, dstRect.y});
    }
    /// Draw part of the sprite on a target surface at a target coordinate.
    void draw(const SDL_Rect& srcRect, SDL_Surface* dst, Vec2i dstPos) const {
        blit(&srcRect, dst, dstPos);
    }
    /// Draw part of the sprite on a target surface at a target coordinate.
    void draw(const SDL_Rect& srcRect, SDL_Surface* dst, const SDL_Rect& dstRect) const {
        blit(&srcRect, dst, Vec2i {dstRect.x, dstRect.y});
    }

    /// Draw a part of the sprite scaled to a destination area.
    void drawStretch(const SDL_Rect& srcRect, SDL_Surface* dst, const SDL_Rect& dstRect) const;

    void setalpha(Uint8 alpha);

    int getWidth() const { return picture()->w; }
    int getHeight() const { return picture()->h; }

    SDL_Surface* getSurface() const { return picture(); }

    void setWrap(short wrapsize = 640);
    bool isWrapping() const { return m_wrap_x.has_value(); }

    explicit operator bool() const { return m_picture || m_source; }

private:
    mutable SdlSurfacePtr m_picture;
    std::shared_ptr<const ImageLoader> m_source;    // where it's loaded again from (nullptr: it can't be)
    SpriteGroup m_group = SpriteGroup::None;
    std::optional<Uint8> m_alphaSet;                // the last setalpha(): applied again after a load

    SDL_Surface* picture() const {
        if (!m_picture && m_source)
            reload();
        return m_picture.get();
    }
    void reload() const;
    void track();
    void untrack();
    std::optional<int> m_wrap_x = std::nullopt;
    Uint8 m_bakedAlpha = 255;    // the loading alpha, made part of the pixels (ImageLoader::withAlpha)
    mutable bool m_opaque = false;    // loaded opaque all over: copied when not see-through (setalpha 255)
    friend class ImageLoader;

    void blit(const SDL_Rect* srcRect, SDL_Surface* dst, Vec2i dstPos) const;
};

class ImageLoader {
public:
    explicit ImageLoader(std::filesystem::path path)
        : m_path(std::move(path))
    {}
    ImageLoader& withColorKey(RGB key) {
        m_color_key = key;
        return *this;
    }
    ImageLoader& withoutColorKey() {
        m_color_key = std::nullopt;
        return *this;
    }
    // A constant transparency: it is made part of the picture (drawn much faster);
    // setalpha() is then relative to it and can't make the sprite more opaque
    ImageLoader& withAlpha(Uint8 alpha) {
        m_alpha = alpha;
        return *this;
    }
    ImageLoader& withWrapping(int wrap_x = 640) {
        m_wrap_x = wrap_x;
        return *this;
    }
    // The transparency changes later (setalpha(), any value): kept as the surface's alpha modulation
    ImageLoader& withChangingAlpha() {
        m_changingAlpha = true;
        return *this;
    }
    // Kept in 16 bits a pixel on the Xbox (SDL_XboxCompactSurface; the sprites loaded in a group
    // are too): an image only drawn from, never drawn on or read
    ImageLoader& compact() {
        m_compact = true;
        return *this;
    }
    gfxSprite create() const;

private:
    friend class gfxSprite;
    std::filesystem::path m_path;
    std::optional<RGB> m_color_key = colors::MAGENTA;
    std::optional<Uint8> m_alpha = std::nullopt;
    bool m_changingAlpha = false;
    bool m_compact = false;
    std::optional<int> m_wrap_x = std::nullopt;

    SdlSurfacePtr load(bool compact) const;
};

#endif // GFX_SPRITE

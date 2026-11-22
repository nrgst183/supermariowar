#include "gfxFont.h"

#include "gfx.h"

#include <SDL3_image/SDL_image.h>

#include <format>
#include <string>
#include <unordered_map>
#include <iostream>

namespace {
constexpr int ASCII_FIRST_PRINTABLE = 33;
} // namespace

gfxFont::gfxFont(const std::filesystem::path& path)
{
    const std::string path_str = path.generic_string();

    std::cout << "loading font " << path_str << " ...";
    auto surf = SdlSurfacePtr(IMG_Load(path_str.c_str()));
    if (!surf) {
        throw std::format("Couldn't load {}: {}", path_str, SDL_GetError());
    }

    if (SDL_MUSTLOCK(surf.get()))
        SDL_LockSurface(surf.get());

    std::vector<Uint32> first_row;
    first_row.reserve(surf->w);
    for (int x = 0; x < surf->w; x++)
        first_row.emplace_back(getRawPixel(surf.get(), x, 0));

    if (SDL_MUSTLOCK(surf.get()))
        SDL_UnlockSurface(surf.get());

    m_glyph_areas.reserve(127);

    const Uint32 raw_magenta = SDL_MapSurfaceRGB(surf.get(), 255, 0, 255);
    const int width = static_cast<int>(first_row.size());
    int x = 0;

    // Find first marker
    while (x < width && first_row[x] != raw_magenta)
        x++;

    while (x < width) {
        // Find marker end
        while (x < width && first_row[x] == raw_magenta)
            x++;

        if (width <= x)
            break;

        const int start = x;

        // Grow until next marker
        while (x < width && first_row[x] != raw_magenta)
            x++;

        const int width = x - start;
        m_glyph_areas.emplace_back(start, width);
    }

    if (m_glyph_areas.empty())
        throw std::format("Didn't find any characters on font image {}", path_str);

    m_glyph_areas.shrink_to_fit();

    const Uint32 color_key = getRawPixel(surf.get(), 0, 1);
    if (!SDL_SetSurfaceColorKey(surf.get(), true, color_key))
        throw std::format("Couldn't set color key on font image: {}", SDL_GetError());

    auto surf_opti = SdlSurfacePtr(SDL_ConvertSurface(surf.get(), screen->format));
    if (!surf_opti)
        throw std::format("Couldn't convert {} to the display's pixel format: {}", path_str, SDL_GetError());

    m_sprite = gfxSprite(std::move(surf_opti), std::nullopt);
    std::cout << "done" << std::endl;
}

void gfxFont::drawScrolling(int x, int y, int width, std::string_view text, bool scroll) const
{
    const int overflow = getWidth(text) - width;
    if (overflow <= 0 || !scroll || !blitdest) {
        draw(x, y, text, x, x + width);
        return;
    }

    //Each text's own clock: from when it was first drawn (a text not drawn for a moment starts again)
    struct Clock {
        Uint64 since;
        Uint64 last;
    };
    static std::unordered_map<std::string, Clock> clocks;
    const Uint64 now = SDL_GetTicks();
    auto [it, added] = clocks.try_emplace(std::string(text), Clock {now, now});
    if (!added && now - it->second.last > 250)
        it->second.since = now;
    it->second.last = now;
    if (clocks.size() > 64)
        std::erase_if(clocks, [now](const auto& c) { return now - c.second.last > 1000; });

    //Wait, slide to the end, wait, slide back (faster); 2-pixel steps, as the game's doubled pixels
    constexpr Uint64 WAIT_MS = 1200;
    constexpr Uint64 PX_PER_S = 40;
    const Uint64 there = static_cast<Uint64>(overflow) * 1000 / PX_PER_S;
    const Uint64 back = there / 3;
    const Uint64 t = (now - it->second.since) % (WAIT_MS + there + WAIT_MS + back);
    int offset = 0;
    if (t < WAIT_MS)
        offset = 0;
    else if (t < WAIT_MS + there)
        offset = static_cast<int>((t - WAIT_MS) * overflow / there);
    else if (t < WAIT_MS + there + WAIT_MS)
        offset = overflow;
    else
        offset = overflow - static_cast<int>((t - WAIT_MS - there - WAIT_MS) * overflow / back);
    offset &= ~1;

    //(clipped to the box: no letter half out of it)
    SDL_Rect previous;
    SDL_GetSurfaceClipRect(blitdest, &previous);
    SDL_Rect box {x, 0, width, blitdest->h};
    SDL_Rect clip;
    if (!SDL_GetRectIntersection(&previous, &box, &clip))
        return;
    SDL_SetSurfaceClipRect(blitdest, &clip);
    draw(x - offset, y, text, x, x + width);
    SDL_SetSurfaceClipRect(blitdest, &previous);
}

void gfxFont::draw(int x, int y, std::string_view text, int dst_min_x, int dst_max_x) const
{
    for (const char ch : text) {
        if (dst_max_x < x)
            break;

        if (ch < ASCII_FIRST_PRINTABLE) {
            x += m_glyph_areas.front().w;
            continue;
        }

        const size_t idx = ch - ASCII_FIRST_PRINTABLE;
        if (idx >= m_glyph_areas.size())
            continue;

        const SDL_Rect src {
            m_glyph_areas[idx].x, 1,
            m_glyph_areas[idx].w, m_sprite.getHeight() - 1,
        };
        if (dst_min_x <= x + src.w)
            m_sprite.draw(x, y, src);

        x += m_glyph_areas[idx].w;
    }
}

void gfxFont::setAlpha(Uint8 alpha)
{
    if (!SDL_SetSurfaceBlendMode(m_sprite.getSurface(), SDL_BLENDMODE_BLEND)) {
        fprintf(stderr, "\n ERROR: couldn't set blend mode on font surface: %s\n", SDL_GetError());
        return;
    }
    if (!SDL_SetSurfaceAlphaMod(m_sprite.getSurface(), alpha)) {
        fprintf(stderr, "\n ERROR: couldn't set alpha on font surface: %s\n", SDL_GetError());
        return;
    }
}

int gfxFont::getHeight() const
{
    return m_sprite.getHeight() - 1;
}

int gfxFont::getWidth(std::string_view text) const
{
    int width = 0;
    for (const char ch : text) {
        size_t idx = 0;
        if (ch >= ASCII_FIRST_PRINTABLE)
            idx = ch - ASCII_FIRST_PRINTABLE;
        if (idx < m_glyph_areas.size())
            width += m_glyph_areas[idx].w;
    }
    return width;
}

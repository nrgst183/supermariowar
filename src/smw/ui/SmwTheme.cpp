#include "SmwTheme.h"

#include "gfx.h"
#include "Game.h"
#include "ResourceManager.h"
#include "ui/InputPrompts.h"

#include <algorithm>

extern CResourceManager* rm;

namespace theme {
namespace {

CResourceManager* g_res = nullptr;

constexpr SDL_Color BLACK {0, 0, 0, 255};

struct Palette {
    SDL_Color light, mid, dark, darker;
};

Palette palette(Tone tone)
{
    switch (tone) {
        case Tone::Green: return {{148, 243, 0, 255}, {107, 203, 0, 255}, {57, 154, 0, 255}, {16, 113, 0, 255}};
        case Tone::Gold: return {{255, 240, 144, 255}, {248, 200, 40, 255}, {208, 144, 0, 255}, {144, 88, 0, 255}};
        case Tone::Red: return {{255, 144, 120, 255}, {224, 56, 32, 255}, {168, 24, 16, 255}, {104, 8, 8, 255}};
        case Tone::Blue: return {{144, 200, 255, 255}, {64, 144, 240, 255}, {32, 96, 200, 255}, {16, 48, 136, 255}};
        case Tone::Dark: return {{88, 96, 112, 255}, {52, 58, 72, 255}, {38, 42, 54, 255}, {24, 26, 34, 255}};
        case Tone::Gray:
        default: return {{176, 192, 192, 255}, {144, 160, 160, 255}, {112, 128, 128, 255}, {80, 96, 96, 255}};
    }
}

void fill(int x, int y, int w, int h, SDL_Color c)
{
    prompts::fill(x, y, w, h, c);
}

// Draws a sprite's area tiled over a rectangle
void tile(const gfxSprite& sprite, const SDL_Rect& src, int x, int y, int w, int h)
{
    for (int ty = 0; ty < h; ty += src.h) {
        for (int tx = 0; tx < w; tx += src.w) {
            const SDL_Rect part {src.x, src.y, std::min(src.w, w - tx), std::min(src.h, h - ty)};
            sprite.draw(x + tx, y + ty, part);
        }
    }
}

} // namespace

void init(CResourceManager* resources)
{
    g_res = resources;
}

CResourceManager& res()
{
    return g_res ? *g_res : *rm;
}

gfxFont& font()
{
    return res().menu_font_small;
}

gfxFont& bigFont()
{
    return res().menu_font_large;
}

SDL_Color shade(SDL_Color c, int percent)
{
    auto channel = [percent](Uint8 v) {
        return static_cast<Uint8>(std::clamp(v * percent / 100, 0, 255));
    };
    return {channel(c.r), channel(c.g), channel(c.b), 255};
}

void block(int x, int y, int w, int h, Tone tone)
{
    const Palette p = palette(tone);
    //The outline (the corners cut), then the bands: light at the top, dark at the bottom
    fill(x + 2, y, w - 4, h, BLACK);
    fill(x, y + 2, w, h - 4, BLACK);
    const bool tall = h >= 24;
    const int light = tall ? 4 : 2;
    const int darker = tall ? 4 : 2;
    fill(x + 2, y + 2, w - 4, h - 4, p.mid);
    fill(x + 2, y + (tall ? 4 : 2), w - 4, light, p.light);
    fill(x + 2, y + h - 2 - darker - 2, w - 4, 2, p.dark);
    fill(x + 2, y + h - 2 - darker, w - 4, darker, p.darker);
    //(a shine at the left end)
    fill(x + 2, y + 2, 2, h - 4 - darker - 2, p.light);
}

void inset(int x, int y, int w, int h)
{
    fill(x + 2, y, w - 4, h, BLACK);
    fill(x, y + 2, w, h - 4, BLACK);
    fill(x + 2, y + 2, w - 4, h - 4, {9, 60, 0, 255});
    fill(x + 2, y + h - 4, w - 4, 2, {16, 113, 0, 255});
}

void dialog(int x, int y, int w, int h)
{
    const gfxSprite& sprite = res().spr_dialog;
    if (!sprite) {
        block(x, y, w, h, Tone::Green);
        return;
    }
    //Nine parts of the sprite: the corners, the edges stretched by tiling, the middle
    constexpr int C = 16;
    const int sw = sprite.getWidth();
    const int sh = sprite.getHeight();
    w = std::max(w, 2 * C);
    h = std::max(h, 2 * C);
    sprite.draw(x, y, {0, 0, C, C});
    sprite.draw(x + w - C, y, {sw - C, 0, C, C});
    sprite.draw(x, y + h - C, {0, sh - C, C, C});
    sprite.draw(x + w - C, y + h - C, {sw - C, sh - C, C, C});
    tile(sprite, {C, 0, sw - 2 * C, C}, x + C, y, w - 2 * C, C);
    tile(sprite, {C, sh - C, sw - 2 * C, C}, x + C, y + h - C, w - 2 * C, C);
    tile(sprite, {0, C, C, sh - 2 * C}, x, y + C, C, h - 2 * C);
    tile(sprite, {sw - C, C, C, sh - 2 * C}, x + w - C, y + C, C, h - 2 * C);
    tile(sprite, {C, C, sw - 2 * C, sh - 2 * C}, x + C, y + C, w - 2 * C, h - 2 * C);
}

void pipe(int x, int y, int w, bool green)
{
    const Palette p = palette(green ? Tone::Green : Tone::Gray);
    if (w <= 0)
        return;
    //The body: outlined, a light band, the shade underneath
    fill(x, y, w, 16, BLACK);
    fill(x, y + 2, w, 12, p.mid);
    fill(x, y + 4, w, 2, p.light);
    fill(x, y + 10, w, 2, p.dark);
    fill(x, y + 12, w, 2, p.darker);
}

void pipeLip(int x, int y, bool green)
{
    const Palette p = palette(green ? Tone::Green : Tone::Gray);
    fill(x, y - 2, 8, 20, BLACK);
    fill(x + 2, y, 4, 16, p.mid);
    fill(x + 2, y, 2, 14, p.light);
    fill(x + 4, y + 12, 2, 4, p.darker);
}

void coin(int x, int y)
{
    if (res().spr_scorecoins)
        res().spr_scorecoins.draw(x, y, {0, 0, 16, 16});
}

void crown(int x, int y)
{
    if (res().spr_crown)
        res().spr_crown.draw(x, y, {0, 0, 14, 14});
}

void icon(int x, int y, Icon which)
{
    if (res().spr_storedpowerupsmall)
        res().spr_storedpowerupsmall.draw(x, y, {static_cast<int>(which) * 16, 0, 16, 16});
}

void modeIcon(int x, int y, int index)
{
    if (res().menu_mode_small)
        res().menu_mode_small.draw(x, y, {index * 16, 0, 16, 16});
}

void flag(int x, int y, SDL_Color c)
{
    prompts::art(x, y, {
        "##..",
        "#cc#",
        "#cl#",    //(a light spot)
        "#cc#",
        "##..",
        "#...",
        "#...",
    }, {{'#', BLACK}, {'c', c}, {'l', shade(c, 140)}});
}

void tooltip(int cx, int y, const std::string& text)
{
    const int w = font().getWidth(text) + 14;
    const int h = 20;
    const int x = std::clamp(cx - w / 2, 2, App::screenWidth - w - 2);
    block(x, y - h - 6, w, h, Tone::Dark);
    //(a point under it)
    const int px = std::clamp(cx, x + 8, x + w - 8);
    fill(px - 4, y - 8, 8, 2, BLACK);
    fill(px - 2, y - 6, 4, 2, BLACK);
    fill(px - 2, y - 8, 4, 2, palette(Tone::Dark).darker);
    font().draw(x + 7, y - h - 4, text);
}

void heading(int x, int y, int w, const std::string& text)
{
    block(x, y, w, 18, Tone::Gray);
    font().draw(x + 8, y + 1, text);
}

} // namespace theme

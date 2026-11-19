#include "EditorInputInternal.h"

#include "CoEdit.h"
#include "LevelShell.h"
#include "ui/MI_MenuPanel.h"
#include "WorldShell.h"
#include "ui/InputPrompts.h"
#include "ui/SmwTheme.h"
#include "gfx.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

extern CResourceManager* rm;

// Drawing the controllers' cursors and the overlays (EditorInput.h, EditorInputInternal.h)
namespace editors::input::detail {

// ---- drawing

void fill(int x, int y, int w, int h, SDL_Color c)
{
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(screen, &rect, SDL_MapSurfaceRGB(screen, c.r, c.g, c.b));
}

// A panel around an area
// A cursor: a white glove pointing up and left (Mario Paint's), its cuff in the player's color;
// the fingertip is the hot spot
// A glove's animation: when it last moved, and its button (held, and when it was pressed)
struct Glove {
    float x = -1, y = -1;
    Uint64 movedMs = 0;
    Uint64 pressMs = 0;
    bool down = false;
};
std::map<int, Glove> g_gloves;    // (by pointer: the controllers 0.., the mouse -1, the others online 1000..)

// A cursor: a white glove pointing up and left (Mario Paint's), its cuff in the player's color;
// the fingertip is the hot spot. Still for a moment, it bobs; its button held, the finger bends
// and the hand dips; a press, a little burst at the fingertip.
void drawArrow(float fx, float fy, SDL_Color color, const std::string& label, int id = -2, bool down = false)
{
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const Uint64 now = SDL_GetTicks();
    Glove dummy;
    Glove& g = id == -2 ? dummy : g_gloves[id];
    if (g.x != fx || g.y != fy) {
        g.x = fx;
        g.y = fy;
        g.movedMs = now;
    }
    if (down && !g.down)
        g.pressMs = now;
    g.down = down;
    const bool idle = !down && now - g.movedMs > 700;
    const int bob = idle ? static_cast<int>(((now - g.movedMs) / 350) % 2) * 2 : 0;
    static const std::vector<const char*> GLOVE = {
        "..##.........",
        ".#ww#........",
        ".#ww#........",
        ".#ww#........",
        ".#ww####.....",
        ".#ww#ww####..",
        "##ww#ww#ww##.",
        "#w#wwwwwwwgw#",
        "#ww#wwwwwwww#",
        "#wwwwwwwwwgw#",
        ".#wwwwwwwgw#.",
        "..#wwwwwgg#..",
        "..#ccccccc#..",
        "..#CCCCCCC#..",
        "...#######...",
    };
    const std::vector<prompts::Ink> inks = {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}, {'g', {184, 184, 200, 255}}, {'c', color},
        {'C', theme::shade(color, 70)}};
    if (down) {
        //(pressing: the finger bent down to the spot, the hand lower)
        std::vector<const char*> pressed = {"..##.........", ".#ww#........"};
        pressed.insert(pressed.end(), GLOVE.begin() + 4, GLOVE.end());
        prompts::art(x0 - 5, y0 + 3, pressed, inks, 2);
    } else {
        prompts::art(x0 - 5, y0 - 1 + bob, GLOVE, inks, 2);
    }
    //A press: a burst of short white lines around the fingertip
    if (now - g.pressMs < 180 && g.pressMs > 0) {
        const int r = 6 + static_cast<int>((now - g.pressMs) / 40);
        const SDL_Color black {0, 0, 0, 255};
        const SDL_Color white {255, 255, 255, 255};
        const int dirs[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}};
        for (const auto& d : dirs) {
            const int px = x0 + d[0] * r;
            const int py = y0 + d[1] * r;
            fill(px - 2, py - 2, 4, 4, black);
            fill(px - 1, py - 1, 2, 2, white);
        }
    }
    if (idle || now - g.pressMs < 200)
        s.overlayDirty = true;    //(it moves by itself)
    if (!label.empty()) {
        const int w = rm->menu_font_small.getWidth(label) + 6;
        const int lx = std::min(x0 + 22, SCREEN_W - w);
        const int ly = std::min(y0 + 26, SCREEN_H - 16);
        fill(lx + 2, ly, w - 4, 16, {0, 0, 0, 255});
        fill(lx, ly + 2, w, 12, {0, 0, 0, 255});
        fill(lx + 2, ly + 2, w - 4, 12, color);
        fill(lx + 2, ly + 2, w - 4, 2, theme::shade(color, 140));
        theme::font().draw(lx + 3, ly + 1, label);
    }
}

// ---- the window's look

// 8x8 pixel art at twice the size: the tools' and the pages' icons
using Art = std::vector<const char*>;
const Art ART_BRUSH = {"......#.", ".....#w#", "....#ww#", "...#ww#.", "..#bb#..", ".#bbb#..", "#bbb#...", "###....."};
const Art ART_ERASER = {"....###.", "...#www#", "..#www#.", ".#rrw#..", "#rrr#...", "#rr#....", ".##.....", "........"};
const Art ART_MOVE = {"#.#.#.#.", "........", "#...###.", "....#ww#", "#...#ww#", ".....##.", "#.#.#.#.", "........"};
const Art ART_PICK = {"......##", ".....#ww", "....#ww#", "...#ww#.", "..#bw#..", ".#bb#...", ".##.....", "#......."};
const Art ART_EYE = {"........", "..####..", ".#wwww#.", "#ww##ww#", "#ww##ww#", ".#wwww#.", "..####..", "........"};
const Art ART_SWAP = {"..#.....", ".##.....", "#######.", ".##.....", "..#..#..", ".....##.", "#######.", ".....#.."};
const Art ART_CLOSE = {"........", ".##..##.", "..####..", "...##...", "..####..", ".##..##.", "........", "........"};
const Art ART_GROUND = {"gggggggg", "gGgGgGgg", "bbbbbbbb", "bdbbbdbb", "bbbbbbbb", "bbbdbbbd", "bbbbbbbb", "dbbbbdbb"};
const Art ART_SPRING = {"..####..", ".#gggg#.", "..#..#..", ".#.##.#.", "..#..#..", ".#.##.#.", "..#..#..", ".######."};
const Art ART_TYPES = {"########", "#ww#cccc", "#ww#cccc", "########", "#rr#ssss", "#rr#ssss", "########", "........"};
const Art ART_ZONE = {"..####..", ".#rrrr#.", "#rr..rr#", "#r..rrr#", "#r.rr.r#", "#rrr..r#", ".#rrrr#.", "..####.."};
const Art ART_PIPE = {"########", "#ggGggg#", "########", ".#gGgg#.", ".#gGgg#.", ".#gGgg#.", ".#gGgg#.", ".######."};
const Art ART_PLATFORM = {"........", "........", "########", "#bbdbbd#", "########", "...##...", "...##...", "..####.."};
const Art ART_FIRE = {"...#....", "..#r#...", ".#rr#.#.", ".#ryr##.", "#ryyyr#.", "#ryyyr#.", ".#rrr#..", "..###..."};
const Art ART_FLAG = {"#.......", "#rrrr...", "#rrrrr..", "#rrrr...", "#.......", "#.......", "#.......", "##......"};
const Art ART_LOOK = {"########", "#cccccc#", "#ccwccc#", "#cc.ccc#", "#c...cc#", "#.....c#", "#gggggg#", "########"};
const Art ART_DISK = {"#######.", "#w###w##", "#w#.#ww#", "#wwwwww#", "#cccccc#", "#cwwwwc#", "#cwwwwc#", "########"};

void drawArt(int x, int y, const Art& art, int scale = 2)
{
    prompts::art(x, y, art, {{'#', {0, 0, 0, 255}}, {'w', {255, 255, 255, 255}}, {'b', {168, 96, 40, 255}}, {'d', {112, 56, 16, 255}},
                                {'g', {56, 184, 48, 255}}, {'G', {152, 232, 88, 255}}, {'r', {216, 40, 32, 255}}, {'y', {248, 208, 48, 255}},
                                {'c', {104, 168, 240, 255}}, {'s', {168, 168, 176, 255}}}, scale);
}

// Some 32 pixel drawing, at another size: drawn on the side, then scaled (as pixel art)
template <typename Draw>
void drawScaled(int x, int y, int size, Draw draw)
{
    static SDL_Surface* canvas = nullptr;
    if (!canvas) {
        canvas = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!canvas)
            return;
        SDL_SetSurfaceBlendMode(canvas, SDL_BLENDMODE_BLEND);
    }
    SDL_FillSurfaceRect(canvas, nullptr, SDL_MapSurfaceRGBA(canvas, 0, 0, 0, 0));
    SDL_Surface* const was = screen;
    SDL_Surface* const wasDest = blitdest;
    screen = canvas;
    blitdest = canvas;
    draw(0, 0);
    screen = was;
    blitdest = wasDest;
    const SDL_Rect dst {x, y, size, size};
    SDL_BlitSurfaceScaled(canvas, nullptr, screen, &dst, SDL_SCALEMODE_PIXELART);
}

// ---- the level editor's palettes

struct Entry {
    int id;
    const char* name;
    const char* tip;
};

const Entry BLOCKS[] = {
    {0, "Brick block", "breaks when a big player hits it from below"},
    {1, "? block", "gives a power-up when hit (set what's inside on the Modes page)"},
    {2, "Donut block", "falls a moment after a player stands on it"},
    {3, "Flip block", "spins when hit, and players fall through it while it spins"},
    {4, "Bounce block", "bounces a player that hits it from below"},
    {5, "Note block", "bounces players that land on it"},
    {6, "Throw block", "hold run to pick it up, then throw it"},
    {15, "View block", "a ? block that shows what's inside"},
    {16, "Red throw block", "hold run to pick it up, then throw it"},
    {17, "Red note block", "a note block with a big bounce"},
    {18, "Blue note block", "a note block with a small bounce"},
    {19, "Gray throw block", "hold run to pick it up, then throw it"},
};
const Entry BREAKABLE[] = {
    {20, "Fireball block", "only a fireball breaks it"}, {21, "Feather block", "only a cape spin breaks it"},
    {22, "Shell block", "only a thrown shell breaks it"}, {23, "Bob-omb block", "only a Bob-omb's blast breaks it"},
    {24, "Boomerang block", "only a boomerang breaks it"}, {25, "Hammer block", "only a hammer breaks it"},
    {26, "Kuribo's shoe block", "only a Kuribo's shoe breaks it"}, {27, "P-wings block", "only a player with P-wings breaks it"},
    {28, "Star block", "only a player with a star breaks it"}, {29, "Leaf block", "only a tail swipe breaks it"},
};
const char* COLORS[] = {"Red", "Green", "Yellow", "Blue"};
const Entry ITEMS[] = {
    {0, "Spring", "bounces whoever lands on it, and can be carried"}, {1, "Spike", "hurts whoever touches it"},
    {2, "Kuribo's shoe", "hop in to stomp on anything"}, {3, "Gold spring", "a spring with a much bigger bounce"},
    {4, "Sticky Kuribo's shoe", "a Kuribo's shoe you can't get out of"}, {5, "Throw box", "pick it up and throw it; it breaks when it hits"},
};
const char* TILE_TYPES[] = {"Solid", "Solid on top", "Ice", "Death", "Death on top", "Death on bottom", "Death on left", "Death on right",
    "Ice on top", "Ice, death on bottom", "Ice, death on left", "Ice, death on right", "Super death", "Super death on top",
    "Super death on bottom", "Super death on left", "Super death on right", "Player death"};

// Where a block is in the block sheet (32 pixels a block)
SDL_Rect blockSrc(int id, bool on)
{
    if (id >= 0 && id <= 6)
        return {id * 32, 0, 32, 32};
    if (id >= 15 && id <= 19)
        return {(id - 15) * 32, 32, 32, 32};
    if (id >= 7 && id <= 10)
        return {224 + (id - 7) * 32, on ? 0 : 32, 32, 32};
    if (id >= 11 && id <= 14)
        return {352 + (id - 11) * 32, on ? 0 : 32, 32, 32};
    return {(id - 20) * 32, 64, 32, 32};
}

void drawBlock(int x, int y, int id, bool on)
{
    if (rm->spr_blocks[0])
        rm->spr_blocks[0].draw(x, y, blockSrc(id, on));
}

// ---- laying out and drawing the window: every widget drawn is also clickable

struct Layout {
    SDL_Rect win {};
    int x = 0;      // the page's left
    int w = 0;      // and width
    int y = 0;      // where the next thing goes
};

// While the page is drawn: its visible part (widgets outside it can't be clicked)
SDL_Rect g_clip {0, 0, SCREEN_W, SCREEN_H};
SDL_Rect g_pageView {};    // the scrolling part of the page (the last frame's)

// (widgets scrolled out of the page stay in the list: the d-pad can go to them, and the page scrolls)
void addWidget(const SDL_Rect& r, std::function<void()> act, std::string tip, Zone zone = ZONE_PAGE, bool selected = false, bool pick = false)
{
    Widget w;
    w.r = r;
    w.act = std::move(act);
    w.tip = std::move(tip);
    w.visible = SDL_GetRectIntersection(&r, &g_clip, &w.hit);
    w.zone = zone;
    w.selected = selected;
    w.pick = pick;
    g_widgets.push_back(std::move(w));
}

bool sameRect(const SDL_Rect& a, const SDL_Rect& b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool hovered(const SDL_Rect& r)
{
    return s.hover >= 0 && sameRect(s.hoverRect, r);
}

// ---- the highlight (a controller's d-pad in the window)

int focusIndex()
{
    if (!s.focusValid)
        return -1;
    for (size_t k = 0; k < g_widgets.size(); k++) {
        if (sameRect(g_widgets[k].r, s.focusRect))
            return static_cast<int>(k);
    }
    return -1;
}

// The page scrolled so the highlight shows
void scrollToFocus()
{
    const int k = focusIndex();
    if (k < 0 || g_widgets[static_cast<size_t>(k)].zone != ZONE_PAGE || g_pageView.h <= 0)
        return;
    const SDL_Rect& r = g_widgets[static_cast<size_t>(k)].r;
    int delta = 0;
    if (r.y < g_pageView.y)
        delta = r.y - g_pageView.y - 4;
    else if (r.y + r.h > g_pageView.y + g_pageView.h)
        delta = r.y + r.h - (g_pageView.y + g_pageView.h) + 4;
    const int scroll = std::clamp(s.scroll + delta, 0, s.scrollMax);
    s.focusRect.y -= scroll - s.scroll;
    s.scroll = scroll;
}

void focusOn(int k)
{
    s.focusRect = g_widgets[static_cast<size_t>(k)].r;
    s.focusValid = true;
    scrollToFocus();
    s.hover = k;
    s.hoverRect = s.focusRect;
    s.overlayDirty = true;
}

// Once the window is laid out: the highlight on a widget (the page's selected item, else its first)
void settleFocus()
{
    if (focusIndex() >= 0) {
        s.hover = focusIndex();
        s.hoverRect = s.focusRect;
        return;
    }
    int best = -1;
    for (int pass = 0; pass < 4 && best < 0; pass++) {
        for (size_t k = 0; k < g_widgets.size() && best < 0; k++) {
            const Widget& w = g_widgets[k];
            const bool ok = pass == 0 ? w.zone == ZONE_PAGE && w.selected
                          : pass == 1 ? w.zone == ZONE_PAGE
                          : pass == 2 ? w.zone == ZONE_RAIL && w.selected
                                      : true;
            if (ok)
                best = static_cast<int>(k);
        }
    }
    if (best >= 0)
        focusOn(best);
}

// The highlight to the nearest widget that way
void moveFocus(int dx, int dy)
{
    const int from = focusIndex();
    if (from < 0)
        return;
    const SDL_Rect& a = g_widgets[static_cast<size_t>(from)].r;
    const float ax = a.x + a.w / 2.0f;
    const float ay = a.y + a.h / 2.0f;
    int best = -1;
    float bestScore = 1e9f;
    for (size_t k = 0; k < g_widgets.size(); k++) {
        if (static_cast<int>(k) == from)
            continue;
        const SDL_Rect& b = g_widgets[k].r;
        //(beyond the highlight's edge that way)
        const bool beyond = dx > 0 ? b.x >= a.x + a.w - 2 : dx < 0 ? b.x + b.w <= a.x + 2 : dy > 0 ? b.y >= a.y + a.h - 2 : b.y + b.h <= a.y + 2;
        if (!beyond)
            continue;
        const float bx = b.x + b.w / 2.0f;
        const float by = b.y + b.h / 2.0f;
        const float along = dx != 0 ? (bx - ax) * static_cast<float>(dx) : (by - ay) * static_cast<float>(dy);
        //(how far off the line: rows and columns that overlap count as on it)
        const float across = dx != 0 ? std::max(0.0f, std::max(static_cast<float>(a.y - (b.y + b.h)), static_cast<float>(b.y - (a.y + a.h))))
                                     : std::max(0.0f, std::max(static_cast<float>(a.x - (b.x + b.w)), static_cast<float>(b.x - (a.x + a.w))));
        const float score = along + across * 4.0f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(k);
        }
    }
    if (best >= 0)
        focusOn(best);
}

// Text wrapped to a width (at most maxLines lines: the last one ends in "..." if cut); returns the height used
int drawWrapped(int x, int y, int w, const std::string& text, int maxLines = 99)
{
    gfxFont& f = theme::font();
    const int lineH = f.getHeight();
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t space = text.find(' ', pos);
        if (space == std::string::npos)
            space = text.size();
        const std::string word = text.substr(pos, space - pos);
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.getWidth(trial) > w) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        pos = space + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<size_t>(maxLines));
        std::string& last = lines.back();
        while (!last.empty() && f.getWidth(last + "...") > w)
            last.pop_back();
        last += "...";
    }
    for (size_t k = 0; k < lines.size(); k++)
        f.draw(x, y + static_cast<int>(k) * lineH, lines[k]);
    return static_cast<int>(lines.size()) * lineH;
}

// A palette cell (a 32 pixel picture, framed): gold when selected, green under the pointer
void cell(const SDL_Rect& r, bool selected, const std::function<void(int, int)>& picture, std::function<void()> act, const std::string& tip)
{
    const bool hover = hovered(r);
    theme::block(r.x, r.y, r.w, r.h, selected ? theme::Tone::Gold : hover ? theme::Tone::Green : theme::Tone::Dark);
    picture(r.x + (r.w - 32) / 2, r.y + (r.h - 32) / 2);
    addWidget(r, std::move(act), tip, ZONE_PAGE, selected, true);
}

// A button with a label (and an icon)
void button(const SDL_Rect& r, const std::string& label, bool selected, std::function<void()> act, const std::string& tip,
    const Art* icon = nullptr, const char* key = nullptr)
{
    const bool hover = hovered(r);
    theme::block(r.x, r.y, r.w, r.h, hover ? theme::Tone::Green : selected ? theme::Tone::Gold : theme::Tone::Gray);
    int tx = r.x + 8;
    if (icon) {
        drawArt(r.x + 5, r.y + (r.h - 16) / 2, *icon);
        tx = r.x + 26;
    }
    theme::font().draw(tx, r.y + (r.h - theme::font().getHeight()) / 2, label);
    if (key) {
        const int kw = std::max(16, theme::font().getWidth(key) + 8);
        prompts::drawKey(r.x + r.w - kw - 4, r.y + (r.h - 16) / 2, key);
    }
    addWidget(r, std::move(act), tip, ZONE_PAGE, selected);
}

// A small labelled heading in the page
void section(Layout& l, const char* title)
{
    theme::font().draw(l.x, l.y, title);
    fill(l.x, l.y + theme::font().getHeight() + 1, l.w, 1, theme::shade(theme::scheme().edge.a ? theme::scheme().edge : theme::scheme().rim, 70));
    l.y += theme::font().getHeight() + 5;
}

// A grid of palette cells
template <typename F>
void grid(Layout& l, int count, F each, int wantCols = 0)
{
    const int size = 36;
    const int cols = wantCols > 0 ? wantCols : std::max(1, (l.w + 2) / (size + 2));
    for (int k = 0; k < count; k++) {
        const SDL_Rect r {l.x + (k % cols) * (size + 2), l.y + (k / cols) * (size + 2), size, size};
        each(k, r);
    }
    l.y += ((count + cols - 1) / cols) * (size + 2) + 6;
}

void pageButton(Layout& l, const std::string& label, std::function<void()> act, const std::string& tip, const Art* icon = nullptr,
    const char* key = nullptr, bool selected = false)
{
    button({l.x, l.y, l.w, 24}, label, selected, std::move(act), tip, icon, key);
    l.y += 27;
}

// ---- the pages

struct Page {
    const char* name;
    const Art* art;           // its icon (or a sprite, see drawPageIcon)
    const char* about;        // what it is
};

const Page LEVEL_PAGES[] = {
    {"Terrain", &ART_GROUND, "Ground, walls and scenery. Take tiles from a tileset and paint them on one of 4 layers."},
    {"Blocks", nullptr, "Things players can bump, break, bounce on and flip. Pick one, then click on the map."},
    {"Items", &ART_SPRING, "Springs, spikes, shoes and boxes that start on the map."},
    {"Types", &ART_TYPES, "How tiles behave: solid, solid on top, icy or deadly. Paint them over the tiles."},
    {"Zones", &ART_ZONE, "Mark the places where players can't spawn or items can't appear."},
    {"Warps", &ART_PIPE, "Pipes and doors that take players to another warp of the same color."},
    {"Platforms", &ART_PLATFORM, "Moving platforms. Set their tiles, path and speed."},
    {"Hazards", &ART_FIRE, "Fireballs, rotodiscs, bullet bills and other things that move on their own."},
    {"Modes", &ART_FLAG, "Race goals and flag bases, for the modes that use them."},
    {"Look", &ART_LOOK, "The map's background, eyecandy and music."},
    {"File", &ART_DISK, "Create, open and save maps, or leave the editor."},
};
constexpr int LEVEL_PAGE_COUNT = static_cast<int>(sizeof(LEVEL_PAGES) / sizeof(LEVEL_PAGES[0]));

// The world editor's pages: its commands, by group
std::vector<std::string> worldGroups()
{
    std::vector<std::string> groups;
    for (const Action& a : WORLD_ACTIONS) {
        if (a.group[0])
            groups.push_back(a.group);
    }
    return groups;
}

int pageCount()
{
    return s.editor == Editor::Level ? LEVEL_PAGE_COUNT : static_cast<int>(worldGroups().size());
}

void drawPageIcon(int x, int y, int page, int size = 16)
{
    //(the pixel art at a whole number of times its 8 pixels; the sprites scaled)
    const int scale = std::max(2, size / 8);
    const int pad = (size - 8 * scale) / 2;
    if (s.editor != Editor::Level) {
        static const Art* arts[] = {&ART_BRUSH, &ART_LOOK, &ART_DISK, &ART_CLOSE};
        drawArt(x + pad, y + pad, *arts[std::min(page, 3)], scale);
        return;
    }
    if (LEVEL_PAGES[page].art)
        drawArt(x + pad, y + pad, *LEVEL_PAGES[page].art, scale);
    else if (size == 16)
        theme::icon(x, y, theme::Icon::QuestionBlock);
    else
        drawScaled(x, y, size, [](int ix, int iy) { theme::icon(ix + 8, iy + 8, theme::Icon::QuestionBlock); });
}

void levelPage(Layout& l)
{
    namespace sh = leveleditor::shell;
    const int mode = sh::mode();
    switch (s.category) {
        case 0: {    //Terrain
            section(l, "Selected tiles");
            const SDL_Rect box {l.x, l.y, 72, 72};
            theme::inset(box.x, box.y, box.w, box.h);
            if (!sh::drawTiles(box.x + 4, box.y + 4, 64))
                theme::font().drawCentered(box.x + 36, box.y + 28, "none");
            theme::font().draw(l.x + 82, l.y + 4, mode == 1 ? "Painting" : "Not painting");
            theme::font().draw(l.x + 82, l.y + 20, "on layer " + std::to_string(sh::layer() + 1));
            l.y += 80;
            pageButton(l, "Choose tiles", [] { runKey(SDLK_T); }, "Open the tilesets. Drag across tiles to take several", &ART_GROUND, "T",
                mode == 1);
            pageButton(l, "Animated tiles", [] { runKey(SDLK_A); }, "Moving tiles like water, lava and coins", &ART_LOOK, "A", mode == 8);
            pageButton(l, "Use the tiles", [] { sh::setMode(1); }, "Back to painting the selected tiles", &ART_BRUSH, nullptr, false);
            break;
        }
        case 1: {    //Blocks
            section(l, "Blocks");
            grid(l, 12, [&](int k, const SDL_Rect& r) {
                const Entry& e = BLOCKS[k];
                cell(r, mode == 0 && sh::block() == e.id, [&](int x, int y) { drawBlock(x, y, e.id, true); },
                    [id = e.id] { sh::setBlock(id, true); }, std::string(e.name) + ": " + e.tip);
            });
            section(l, "ON/OFF switches");
            grid(l, 8, [&](int k, const SDL_Rect& r) {
                const int color = k % 4;
                const bool on = k < 4;
                const int id = 7 + color;
                cell(r, mode == 0 && sh::block() == id && sh::switchOn(color) == on, [&](int x, int y) { drawBlock(x, y, id, on); },
                    [id, on] { sh::setBlock(id, on); },
                    std::string(COLORS[color]) + " switch, starts " + (on ? "ON" : "OFF") + ". Hit it to flip every " + COLORS[color] + " switch block");
            }, 4);
            section(l, "Switch blocks");
            grid(l, 8, [&](int k, const SDL_Rect& r) {
                const int color = k % 4;
                const bool on = k < 4;
                const int id = 11 + color;
                cell(r, mode == 0 && sh::block() == id && sh::switchBlockOn() == on, [&](int x, int y) { drawBlock(x, y, id, on); },
                    [id, on] { sh::setBlock(id, on); },
                    std::string(COLORS[color]) + " switch block, starts " + (on ? "solid" : "as an outline") + ". " + COLORS[color] + " switches flip it");
            }, 4);
            section(l, "Breakable only by...");
            grid(l, 10, [&](int k, const SDL_Rect& r) {
                const Entry& e = BREAKABLE[k];
                cell(r, mode == 0 && sh::block() == e.id, [&](int x, int y) { drawBlock(x, y, e.id, true); },
                    [id = e.id] { sh::setBlock(id, true); }, std::string(e.name) + ": " + e.tip);
            });
            break;
        }
        case 2:    //Items
            section(l, "Map items");
            grid(l, 6, [&](int k, const SDL_Rect& r) {
                const Entry& e = ITEMS[k];
                cell(r, mode == 7 && sh::item() == e.id,
                    [&](int x, int y) {
                        if (rm->spr_mapitems[0])
                            rm->spr_mapitems[0].draw(x, y, {e.id * 32, 0, 32, 32});
                    },
                    [id = e.id] { sh::setItem(id); }, std::string(e.name) + ": " + e.tip);
            });
            break;
        case 3:    //Types
            section(l, "Tile types");
            grid(l, 18, [&](int k, const SDL_Rect& r) {
                cell(r, mode == 6 && sh::tileType() == k + 1,
                    [&](int x, int y) {
                        if (rm->spr_transparenttiles)
                            rm->spr_transparenttiles.draw(x, y, {k * 32, 0, 32, 32});
                    },
                    [k] { sh::setTileType(k + 1); }, TILE_TYPES[k]);
            });
            l.y += 4;
            pageButton(l, "Clear all tile types", [] {
                if (!confirmed("clear-types", "This removes every tile type from the map"))
                    return;
                sh::setMode(6);
                runKey(SDLK_DELETE, false, false, true);
            }, "Remove every tile type from the map", &ART_TYPES, "Del");
            break;
        case 4:    //Zones
            pageButton(l, "No player starts", [] { runKey(SDLK_X, false, false, true); }, "Cells where players can't appear at the start or after dying",
                &ART_ZONE, "X", mode == 4);
            pageButton(l, "No items", [] { runKey(SDLK_Z, false, false, true); }, "Cells where items can't appear", &ART_ZONE, "Z", mode == 5);
            l.y += 4;
            l.y += drawWrapped(l.x, l.y, l.w, padUsed() ? "Mark cells on the map with A, clear them with B." :
                                                          "Click or drag on the map to mark cells. Right-click to clear them.") + 6;
            if (mode == 4) {
                //(whose start zone: everyone's, or a team's; then the whole map at once)
                section(l, "Whose zone");
                grid(l, 5, [&](int k, const SDL_Rect& r) {
                    cell(r, sh::zoneTeam() == k,
                        [k](int x, int y) { theme::font().drawCentered(x + 16, y + 8, k == 0 ? "All" : std::to_string(k).c_str()); },
                        [k] { runKey(k == 0 ? SDLK_X : static_cast<SDL_Keycode>(SDLK_1 + k - 1), false, false, true); },
                        k == 0 ? "Everyone's zone" : "Team " + std::to_string(k) + "'s own zone");
                });
                pageButton(l, "Mark every cell", [] { sh::setMode(4); runKey(SDLK_A, false, false, true); }, "Mark the whole map for this zone", &ART_ZONE, "A");
                pageButton(l, "Clear every cell", [] { sh::setMode(4); runKey(SDLK_N, false, false, true); }, "Clear the whole map for this zone", &ART_ZONE, "N");
                pageButton(l, "Invert", [] { sh::setMode(4); runKey(SDLK_I, false, false, true); }, "Swap marked and unmarked cells", &ART_ZONE, "I");
            }
            break;
        case 5:    //Warps
            pageButton(l, "Warps", [] { runKey(SDLK_W); }, "Pick a warp's color and direction, then click its cells", &ART_PIPE, "W", mode == 2);
            break;
        case 6:    //Platforms
            pageButton(l, "Platforms", [] { runKey(SDLK_P); }, "Draw a platform's tiles, then set its path and speed", &ART_PLATFORM, "P");
            break;
        case 7:    //Hazards
            pageButton(l, "Hazards", [] { runKey(SDLK_H); }, "Add, move and set up the hazards", &ART_FIRE, "H");
            break;
        case 8:    //Modes
            pageButton(l, "Race goals, flag bases", [] { runKey(SDLK_J); }, "Place the race goals and flag bases", &ART_FLAG, "J");
            pageButton(l, "? block contents", [] { armBlockContents(); }, "Choose a ? block on the map to set what's inside it",
                nullptr, "K");
            break;
        case 9:    //Look
            pageButton(l, "Backgrounds", [] { runKey(SDLK_B); }, "Choose the map's background from all of them", &ART_LOOK, "B");
            pageButton(l, "Next background", [] { runKey(SDLK_G, false, false, true); }, "Switch to the next background (and its music)", &ART_LOOK, "G");
            pageButton(l, "Eyecandy", [] { runKey(SDLK_E); }, "Clouds, ghosts, leaves, snow, bubbles...", &ART_LOOK, "E");
            pageButton(l, "Music", [] { runKey(SDLK_R, false, false, true); }, "Change which kind of music plays", &ART_LOOK, "R");
            break;
        default: {    //File
            bool file = false;
            for (const Action& a : LEVEL_ACTIONS) {
                if (a.group[0])
                    file = std::string(a.group) == "File" || std::string(a.group) == "Other";
                if (!file)
                    continue;
                const std::string key = keyName(a);
                pageButton(l, a.label, [&a] {
                    //(the actions that throw the map's changes away: chosen twice)
                    const std::string label = a.label;
                    if (label == "Clear all" && !confirmed("clear", "This clears the whole map"))
                        return;
                    if (label == "New map" && !confirmed("new", "This starts a new, empty map"))
                        return;
                    //(N is "no zone here" in the zones mode: from the tiles' mode, it's a new map)
                    if (label == "New map")
                        sh::setMode(1);
                    if ((label == "Previous map" || label == "Next map") && !s.undos.empty() &&
                        !confirmed(label, "Changes you haven't saved will be lost"))
                        return;
                    runKey(a.key, a.shift, a.ctrl);
                }, a.label, nullptr, nullptr);
                //(its key, drawn over the button's right end: for the keyboard)
                if (!padUsed()) {
                    const int kw = std::max(16, theme::font().getWidth(key) + 8);
                    prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, key);
                }
            }
            break;
        }
    }
}

void worldPage(Layout& l)
{
    const std::vector<std::string> groups = worldGroups();
    const std::string group = groups[static_cast<size_t>(std::clamp(s.category, 0, static_cast<int>(groups.size()) - 1))];
    bool in = false;
    for (const Action& a : WORLD_ACTIONS) {
        if (a.group[0])
            in = group == a.group;
        if (!in)
            continue;
        const std::string key = keyName(a);
        pageButton(l, a.label, [&a] { runKey(a.key, a.shift, a.ctrl); }, a.label);
        const int kw = std::max(16, theme::font().getWidth(key) + 8);
        prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, key);
    }
}

// What is selected now, small (the window's title row, the chip)
std::string selectionName()
{
    if (s.editor != Editor::Level)
        return {};
    namespace sh = leveleditor::shell;
    switch (sh::mode()) {
        case 0:
            for (const Entry& e : BLOCKS)
                if (e.id == sh::block())
                    return e.name;
            for (const Entry& e : BREAKABLE)
                if (e.id == sh::block())
                    return e.name;
            if (sh::block() >= 7 && sh::block() <= 10)
                return std::string(COLORS[sh::block() - 7]) + " switch";
            if (sh::block() >= 11 && sh::block() <= 14)
                return std::string(COLORS[sh::block() - 11]) + " switch block";
            return "Blocks";
        case 1: return "Tiles";
        case 2: return "Warps";
        case 3: return "Select and move";
        case 4: return "No player starts";
        case 5: return "No items";
        case 6: return sh::tileType() >= 1 && sh::tileType() <= 18 ? TILE_TYPES[sh::tileType() - 1] : "Tile types";
        case 7: return sh::item() >= 0 && sh::item() < 6 ? ITEMS[sh::item()].name : "Items";
        case 8: return "Animated tiles";
        default: return {};
    }
}

void drawSelection(int x, int y)
{
    namespace sh = leveleditor::shell;
    switch (sh::mode()) {
        case 0: drawBlock(x, y, sh::block(), sh::block() >= 11 && sh::block() <= 14 ? sh::switchBlockOn() : (sh::block() >= 7 && sh::block() <= 10 ? sh::switchOn(sh::block() - 7) : true)); break;
        case 1:
        case 8:
            if (!sh::drawTiles(x, y, 32))
                drawArt(x + 8, y + 8, ART_GROUND);
            break;
        case 2: drawArt(x + 8, y + 8, ART_PIPE); break;
        case 3: drawArt(x + 8, y + 8, ART_MOVE); break;
        case 4:
        case 5: drawArt(x + 8, y + 8, ART_ZONE); break;
        case 6:
            if (rm->spr_transparenttiles)
                rm->spr_transparenttiles.draw(x, y, {(sh::tileType() - 1) * 32, 0, 32, 32});
            break;
        case 7:
            if (rm->spr_mapitems[0])
                rm->spr_mapitems[0].draw(x, y, {sh::item() * 32, 0, 32, 32});
            break;
        default: break;
    }
}

// Something picked before (the recently used), 32 pixels
void drawBrush(const leveleditor::shell::Brush& b, int x, int y)
{
    switch (b.mode) {
        case 0: drawBlock(x, y, b.id, b.on); break;
        case 1: leveleditor::shell::drawBrushTiles(b, x, y, 32); break;
        case 6:
            if (rm->spr_transparenttiles)
                rm->spr_transparenttiles.draw(x, y, {(b.id - 1) * 32, 0, 32, 32});
            break;
        case 7:
            if (rm->spr_mapitems[0])
                rm->spr_mapitems[0].draw(x, y, {b.id * 32, 0, 32, 32});
            break;
        default: break;
    }
}

std::string brushName(const leveleditor::shell::Brush& b)
{
    switch (b.mode) {
        case 0:
            for (const Entry& e : BLOCKS)
                if (e.id == b.id)
                    return e.name;
            for (const Entry& e : BREAKABLE)
                if (e.id == b.id)
                    return e.name;
            if (b.id >= 7 && b.id <= 10)
                return std::string(COLORS[b.id - 7]) + " switch, " + (b.on ? "ON" : "OFF");
            if (b.id >= 11 && b.id <= 14)
                return std::string(COLORS[b.id - 11]) + " switch block, " + (b.on ? "solid" : "an outline");
            return "A block";
        case 1: return b.cols * b.rows > 1 ? std::to_string(b.cols) + " x " + std::to_string(b.rows) + " tiles" : "A tile";
        case 6: return b.id >= 1 && b.id <= 18 ? TILE_TYPES[b.id - 1] : "A tile type";
        case 7: return b.id >= 0 && b.id < 6 ? ITEMS[b.id].name : "A map item";
        default: return {};
    }
}

// A key's short name (for a key cap)
std::string keyCap(SDL_Keycode key, bool shift)
{
    std::string name = key == SDLK_ESCAPE ? "Esc" : key == SDLK_PAGEUP ? "PgUp" : key == SDLK_PAGEDOWN ? "PgDn" : key == SDLK_DELETE ? "Del"
                     : key == SDLK_GRAVE ? "~" : SDL_GetKeyName(key);
    return shift ? "Shift+" + name : name;
}

// The level editor's other screens: what the buttons do there, and its commands
void screenPage(Layout& l)
{
    const leveleditor::shell::Screen& sc = s.screen;
    l.y += drawWrapped(l.x, l.y, l.w, sc.title) + 6;
    for (const leveleditor::shell::Command& c : sc.commands) {
        const std::string cap = keyCap(c.key, c.shift);
        const SDL_Keycode key = c.key;
        const bool shift = c.shift;
        pageButton(l, c.label, [key, shift] { runKey(key, shift); }, c.label + " (" + cap + ")");
        const int kw = std::max(16, theme::font().getWidth(cap) + 8);
        prompts::drawKey(l.x + l.w - kw - 4, l.y - 27 + 4, cap);
    }
    if (!sc.holds.empty())
        l.y += drawWrapped(l.x, l.y + 4, l.w, sc.holds) + 8;
}

void drawWindow()
{
    g_widgets.clear();
    const SDL_Rect win = windowRect();
    theme::panel(win.x - 6, win.y - 6, win.w + 12, win.h + 12);
    const bool level = s.editor == Editor::Level;
    namespace sh = leveleditor::shell;

    //Title: the editor, the map, and the window's own buttons
    theme::strip(win.x + 4, win.y + 4, win.w - 8, 30);
    theme::icon(win.x + 12, win.y + 11, level ? theme::Icon::QuestionBlock : theme::Icon::Star);
    theme::font().draw(win.x + 32, win.y + 12, level ? "Level Editor" : "World Editor");
    if (level) {
        std::string name = sh::mapName();
        const int room = win.w - 150 - theme::font().getWidth("Level Editor");
        while (!name.empty() && theme::font().getWidth(name) > room)
            name.pop_back();
        theme::font().drawRightJustified(win.x + win.w - 62, win.y + 12, name);
    }
    {
        const SDL_Rect swap {win.x + win.w - 56, win.y + 8, 24, 22};
        theme::block(swap.x, swap.y, swap.w, swap.h, hovered(swap) ? theme::Tone::Green : theme::Tone::Gray);
        drawArt(swap.x + 4, swap.y + 3, ART_SWAP);
        addWidget(swap, [] { s.dockLeft = !s.dockLeft; s.overlayDirty = true; s.focusValid = false; }, "Move the window to the other side", ZONE_TITLE);
        const SDL_Rect close {win.x + win.w - 30, win.y + 8, 22, 22};
        theme::block(close.x, close.y, close.w, close.h, hovered(close) ? theme::Tone::Red : theme::Tone::Gray);
        drawArt(close.x + 3, close.y + 3, ART_CLOSE);
        addWidget(close, [] { closeWindow(); }, "Close (Tab)", ZONE_TITLE);
    }

    //(on one of the level editor's other screens: just its commands)
    const bool screenMode = level && !sh::inMapView() && !s.screen.title.empty();

    //Tools: paint, erase, move, pick; the layer; what is shown
    int ty = win.y + 40;
    if (level && !screenMode) {
        static const Art* toolArt[] = {&ART_BRUSH, &ART_ERASER, &ART_MOVE, &ART_PICK};
        static const char* toolTip[] = {"Paint: click or drag to place. Right-click removes",
            "Erase: click or drag to remove", "Move: drag over cells, then drag them to a new spot (M)",
            "Pick: click the map to copy the block or tile there"};
        int x = win.x + 8;
        for (int t = 0; t < TOOL_COUNT; t++) {
            const SDL_Rect r {x, ty, 26, 26};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : currentTool() == t ? theme::Tone::Gold : theme::Tone::Gray);
            drawArt(r.x + 5, r.y + 5, *toolArt[t]);
            addWidget(r, [t] { setTool(t); }, toolTip[t], ZONE_TOOLS, currentTool() == t, true);
            x += 28;
        }
        x += 4;
        //The layers: L1 to L4
        for (int layer = 0; layer < 4; layer++) {
            const SDL_Rect r {x, ty, 25, 26};
            theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : sh::layer() == layer ? theme::Tone::Gold : theme::Tone::Gray);
            theme::font().drawCentered(r.x + 13, r.y + (r.h - theme::font().getHeight()) / 2, "L" + std::to_string(layer + 1));
            addWidget(r, [layer] { sh::setLayer(layer); }, "Layer " + std::to_string(layer + 1) + " of 4: where tiles are painted (Y goes through them)",
                ZONE_TOOLS, sh::layer() == layer);
            x += 26;
        }
        x += 4;
        const SDL_Rect only {x, ty, 26, 26};
        theme::block(only.x, only.y, only.w, only.h, hovered(only) ? theme::Tone::Green : sh::othersHidden() ? theme::Tone::Gold : theme::Tone::Gray);
        drawArt(only.x + 5, only.y + 5, ART_EYE);
        addWidget(only, [] { sh::setOthersHidden(!sh::othersHidden()); }, "Show only the active layer (U)", ZONE_TOOLS);
        x += 28;
        const SDL_Rect blocks {x, ty, 26, 26};
        theme::block(blocks.x, blocks.y, blocks.w, blocks.h, hovered(blocks) ? theme::Tone::Green : sh::blocksShown() ? theme::Tone::Gray : theme::Tone::Gold);
        theme::icon(blocks.x + 5, blocks.y + 5, theme::Icon::QuestionBlock);
        if (!sh::blocksShown())
            fill(blocks.x + 4, blocks.y + 12, 18, 2, {216, 40, 32, 255});
        addWidget(blocks, [] { sh::setBlocksShown(!sh::blocksShown()); }, "Hide blocks to see the tiles under them (V)", ZONE_TOOLS);
        ty += 32;
        //The last things picked (LB / RB on the map)
        if (!s.recents.empty()) {
            const bool controller = prompts::lastDevice() == prompts::Device::Controller;
            int rx = win.x + 8;
            if (controller)
                rx += prompts::drawPad(rx, ty + 7, prompts::Pad::LB) + 4;
            const sh::Brush now = sh::brush();
            for (const sh::Brush& b : s.recents) {
                const SDL_Rect r {rx, ty, 28, 28};
                const bool sel = b == now;
                theme::block(r.x, r.y, r.w, r.h, hovered(r) ? theme::Tone::Green : sel ? theme::Tone::Gold : theme::Tone::Dark);
                drawScaled(r.x + 2, r.y + 2, 24, [&b](int x, int y) { drawBrush(b, x, y); });
                addWidget(r, [b] { s.stepped = b; sh::setBrush(b); if (currentTool() != TOOL_ERASE) setTool(TOOL_PAINT); },
                    "Used recently: " + brushName(b), ZONE_TOOLS, sel, true);
                rx += 30;
            }
            if (controller)
                prompts::drawPad(rx + 2, ty + 7, prompts::Pad::RB);
            ty += 34;
        }
    }

    //The pages: a rail of icons, the page beside it
    const int bodyTop = ty;
    const int footH = 66;
    const int bodyBottom = win.y + win.h - footH;
    const int count = pageCount();
    const int itemH = std::min(36, (bodyBottom - bodyTop) / count);
    const std::vector<std::string> groups = level ? std::vector<std::string>() : worldGroups();
    for (int k = 0; k < count && !screenMode; k++) {
        const SDL_Rect r {win.x + 8, bodyTop + k * itemH, RAIL_W - 6, itemH - 2};
        const bool sel = s.category == k;
        theme::block(r.x, r.y, r.w, r.h, sel ? theme::Tone::Gold : hovered(r) ? theme::Tone::Green : theme::Tone::Dark);
        const std::string label = level ? LEVEL_PAGES[k].name : groups[static_cast<size_t>(k)];
        //Its icon, big (its name: the page's title, and in the footer)
        const int size = std::min(24, r.h - 4);
        drawPageIcon(r.x + (r.w - size) / 2, r.y + (r.h - size) / 2, k, size);
        addWidget(r, [k] { s.category = k; s.scroll = 0; s.hover = -1; s.focusValid = false; s.overlayDirty = true; },
            level ? label + ": " + LEVEL_PAGES[k].about : label, ZONE_RAIL, sel);
    }

    Layout l;
    l.win = win;
    l.x = screenMode ? win.x + 14 : win.x + RAIL_W + 10;
    l.w = win.x + win.w - 16 - l.x;
    l.y = bodyTop + 2;
    const std::string title = screenMode ? std::string("This screen")
                            : level      ? LEVEL_PAGES[std::clamp(s.category, 0, count - 1)].name
                                         : groups[static_cast<size_t>(std::clamp(s.category, 0, count - 1))];
    theme::bigFont().draw(l.x, l.y - 2, title);
    l.y += theme::bigFont().getHeight() + 2;
    //The page below the title scrolls (the wheel, or the right stick over the window)
    const int viewTop = l.y;
    const int viewH = bodyBottom - 4 - viewTop;
    g_clip = SDL_Rect {l.x - 2, viewTop, l.w + 4, viewH};
    g_pageView = g_clip;
    SDL_SetSurfaceClipRect(screen, &g_clip);
    l.y -= s.scroll;
    const int contentTop = l.y;
    if (screenMode) {
        screenPage(l);
    } else {
        if (level)
            l.y += drawWrapped(l.x, l.y, l.w, LEVEL_PAGES[s.category].about) + 8;
        if (level)
            levelPage(l);
        else
            worldPage(l);
    }
    SDL_SetSurfaceClipRect(screen, nullptr);
    g_clip = SDL_Rect {0, 0, SCREEN_W, SCREEN_H};
    s.scrollMax = std::max(0, l.y - contentTop - viewH);
    if (s.scroll > s.scrollMax)
        s.scroll = s.scrollMax;
    if (s.scrollMax > 0) {
        //A scroll bar at the page's right edge
        const int barX = l.x + l.w + 3;
        fill(barX, viewTop, 3, viewH, {0, 0, 0, 255});
        const int thumbH = std::max(16, viewH * viewH / (viewH + s.scrollMax));
        const int thumbY = viewTop + (viewH - thumbH) * s.scroll / s.scrollMax;
        fill(barX, thumbY, 3, thumbH, {255, 255, 255, 255});
    }

    //Footer: what is under the pointer, and how to close
    const int fy = win.y + win.h - footH + 4;
    theme::inset(win.x + 8, fy, win.w - 16, footH - 12);
    std::string tip = level ? "Selected: " + selectionName() : std::string(s.nav ? "Choose a command" : "Click a command");
    if (s.hover >= 0) {
        for (const Widget& w : g_widgets) {
            if (sameRect(w.r, s.hoverRect))
                tip = w.tip;
        }
    }
    drawWrapped(win.x + 14, fy + 4, win.w - 28, tip, 2);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller && s.nav)
        row = {{{prompts::Pad::DPad}, {}, ""}, {{prompts::Pad::A}, {}, "choose"}, {{prompts::Pad::LB, prompts::Pad::RB}, {}, "pages"},
            {{prompts::Pad::B}, {}, "close"}};
    else if (controller)
        row = {{{prompts::Pad::A}, {}, "pick"}, {{prompts::Pad::LB, prompts::Pad::RB}, {}, "pages"}, {{prompts::Pad::Start}, {}, "close"}};
    else
        row = {{{}, {"Tab"}, "close"}, {{}, {"F1"}, "help"}};
    const int pw = prompts::promptsWidth(row, 14);
    prompts::drawPrompts(win.x + win.w - 14 - pw / 2, fy + footH - 33, row);
    if (s.nav) {
        settleFocus();
        //The highlight: a white frame (inside the page's view, for the page's widgets)
        const int k = focusIndex();
        if (k >= 0) {
            const Widget& w = g_widgets[static_cast<size_t>(k)];
            if (w.zone == ZONE_PAGE)
                SDL_SetSurfaceClipRect(screen, &g_pageView);
            const SDL_Rect r = w.r;
            const SDL_Color black {0, 0, 0, 255};
            const SDL_Color white {255, 255, 255, 255};
            fill(r.x - 4, r.y - 4, r.w + 8, 4, black);
            fill(r.x - 4, r.y + r.h, r.w + 8, 4, black);
            fill(r.x - 4, r.y, 4, r.h, black);
            fill(r.x + r.w, r.y, 4, r.h, black);
            fill(r.x - 3, r.y - 3, r.w + 6, 2, white);
            fill(r.x - 3, r.y + r.h + 1, r.w + 6, 2, white);
            fill(r.x - 3, r.y - 1, 2, r.h + 2, white);
            fill(r.x + r.w + 1, r.y - 1, 2, r.h + 2, white);
            SDL_SetSurfaceClipRect(screen, nullptr);
        }
    }
}

// A player's reticle on the cell it points at: corner brackets in its color, and its tag
void reticle(float px, float py, SDL_Color c, const std::string& tag)
{
    const int x = static_cast<int>(px) / TILE * TILE;
    const int y = static_cast<int>(py) / TILE * TILE;
    const SDL_Color black {0, 0, 0, 255};
    const int len = 10;
    //Each corner: black under, the color on top
    auto corner = [&](int cx, int cy, int dx, int dy) {
        const int hx = dx > 0 ? cx : cx - len + 1;
        const int vy = dy > 0 ? cy : cy - len + 1;
        fill(hx - 1, cy - 1 - (dy < 0 ? 2 : 0), len + 2, 5, black);
        fill((dx > 0 ? cx : cx - 2) - 1 - (dx < 0 ? 0 : 0), vy - 1, 5, len + 2, black);
        fill(hx, dy > 0 ? cy : cy - 2, len, 3, c);
        fill(dx > 0 ? cx : cx - 2, vy, 3, len, c);
    };
    corner(x - 2, y - 2, 1, 1);
    corner(x + TILE + 1, y - 2, -1, 1);
    corner(x - 2, y + TILE + 1, 1, -1);
    corner(x + TILE + 1, y + TILE + 1, -1, -1);
    //The center: a small dot
    fill(x + TILE / 2 - 2, y + TILE / 2 - 2, 4, 4, black);
    fill(x + TILE / 2 - 1, y + TILE / 2 - 1, 2, 2, c);
    if (!tag.empty()) {
        //The tag on the top left corner (inside the screen)
        const int w = theme::font().getWidth(tag) + 8;
        const int tx = std::clamp(x - 2, 0, SCREEN_W - w);
        const int ty = y - 20 >= 0 ? y - 20 : y + TILE + 4;
        fill(tx, ty, w, 16, black);
        fill(tx + 1, ty + 1, w - 2, 14, c);
        fill(tx + 1, ty + 1, w - 2, 2, theme::shade(c, 140));
        theme::font().draw(tx + 4, ty + 1, tag);
    }
}

// Cells framed (a rectangle being drawn, the area to paste): a dashed frame in a color, and a tag
void cellFrame(int c0, int r0, int c1, int r1, SDL_Color c, const std::string& tag, float pointerY = -1)
{
    const int x = std::min(c0, c1) * TILE;
    const int y = std::min(r0, r1) * TILE;
    const int w = (std::abs(c1 - c0) + 1) * TILE;
    const int h = (std::abs(r1 - r0) + 1) * TILE;
    const SDL_Color black {0, 0, 0, 255};
    fill(x - 1, y - 1, w + 2, 4, black);
    fill(x - 1, y + h - 3, w + 2, 4, black);
    fill(x - 1, y - 1, 4, h + 2, black);
    fill(x + w - 3, y - 1, 4, h + 2, black);
    //(dashes, marching)
    const int phase = static_cast<int>((SDL_GetTicks() / 80) % 8);
    for (int i = -phase; i < w; i += 8) {
        fill(x + std::max(0, i), y, std::min(4, w - std::max(0, i)), 2, c);
        fill(x + std::max(0, i), y + h - 2, std::min(4, w - std::max(0, i)), 2, c);
    }
    for (int i = -phase; i < h; i += 8) {
        fill(x, y + std::max(0, i), 2, std::min(4, h - std::max(0, i)), c);
        fill(x + w - 2, y + std::max(0, i), 2, std::min(4, h - std::max(0, i)), c);
    }
    s.overlayDirty = true;
    if (!tag.empty()) {
        const int tw = theme::font().getWidth(tag) + 8;
        const int tx = std::clamp(x, 0, SCREEN_W - tw);
        //(below; above when the pointer is in its lower half, so the glove doesn't cover it)
        const bool above = (pointerY >= 0 && pointerY >= static_cast<float>(y + h / 2) && y - 20 >= 0) || y + h + 4 + 16 > SCREEN_H;
        const int ty = above ? std::max(0, y - 20) : y + h + 4;
        fill(tx, ty, tw, 16, black);
        fill(tx + 1, ty + 1, tw - 2, 14, c);
        fill(tx + 1, ty + 1, tw - 2, 2, theme::shade(c, 140));
        theme::font().draw(tx + 4, ty + 1, tag);
    }
}

// What A would put down, faint over the map: the selection in each cell of a rectangle (tiles: their
// pattern repeating, as painting does it); erasing, the cells tinted red
void drawGhost(int c0, int r0, int c1, int r1, bool erase)
{
    namespace sh = leveleditor::shell;
    const int x0 = std::clamp(std::min(c0, c1), 0, sh::MAP_COLS - 1);
    const int y0 = std::clamp(std::min(r0, r1), 0, sh::MAP_ROWS - 1);
    const int x1 = std::clamp(std::max(c0, c1), 0, sh::MAP_COLS - 1);
    const int y1 = std::clamp(std::max(r0, r1), 0, sh::MAP_ROWS - 1);
    const SDL_Rect area {x0 * TILE, y0 * TILE, (x1 - x0 + 1) * TILE, (y1 - y0 + 1) * TILE};
    if (erase) {
        static SDL_Surface* red = nullptr;
        if (!red) {
            red = SDL_CreateSurface(TILE, TILE, SDL_PIXELFORMAT_ARGB8888);
            if (!red)
                return;
            SDL_FillSurfaceRect(red, nullptr, SDL_MapSurfaceRGBA(red, 216, 40, 32, 110));
            SDL_SetSurfaceBlendMode(red, SDL_BLENDMODE_BLEND);
        }
        for (int c = x0; c <= x1; c++) {
            for (int r = y0; r <= y1; r++) {
                SDL_Rect dst {c * TILE, r * TILE, TILE, TILE};
                SDL_BlitSurface(red, nullptr, screen, &dst);
            }
        }
        return;
    }
    const sh::Brush b = sh::brush();
    if (b.mode < 0)
        return;
    static SDL_Surface* canvas = nullptr;
    if (!canvas) {
        canvas = SDL_CreateSurface(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_ARGB8888);
        if (!canvas)
            return;
        SDL_SetSurfaceBlendMode(canvas, SDL_BLENDMODE_BLEND);
        SDL_SetSurfaceAlphaMod(canvas, 150);
    }
    SDL_FillSurfaceRect(canvas, &area, SDL_MapSurfaceRGBA(canvas, 0, 0, 0, 0));
    SDL_Surface* const was = screen;
    SDL_Surface* const wasDest = blitdest;
    screen = canvas;
    blitdest = canvas;
    for (int c = x0; c <= x1; c++) {
        for (int r = y0; r <= y1; r++) {
            if (b.mode == 1)
                sh::drawBrushTile(b, c - x0, r - y0, c * TILE, r * TILE);
            else
                drawBrush(b, c * TILE, r * TILE);
        }
    }
    screen = was;
    blitdest = wasDest;
    SDL_Rect dst = area;
    SDL_BlitSurface(canvas, &area, screen, &dst);
}

// Where each controller's A would paint now (no tool armed, not drawing a rectangle): the selection,
// faint, in the cell (the tiles: all of them, from there)
void drawGhosts()
{
    namespace sh = leveleditor::shell;
    if (s.armed != Armed::None || s.overlay != Overlay::None || !sh::paintsCells())
        return;
    const int tool = currentTool();
    if (tool == TOOL_PICK)
        return;
    for (const Pointer& p : s.pointers) {
        if (!p.shown || p.drag >= 0 || inWindow(p.x, p.y))
            continue;
        const int c = std::clamp(static_cast<int>(p.x) / TILE, 0, sh::MAP_COLS - 1);
        const int r = std::clamp(static_cast<int>(p.y) / TILE, 0, sh::MAP_ROWS - 1);
        if (tool == TOOL_ERASE) {
            drawGhost(c, r, c, r, true);
            continue;
        }
        const sh::Brush b = sh::brush();
        const int w = b.mode == 1 ? b.cols : 1;
        const int h = b.mode == 1 ? b.rows : 1;
        drawGhost(c, r, c + w - 1, r + h - 1, false);
    }
}

// The rectangles being drawn, the area about to be pasted, the fill's cell
void drawCellTools()
{
    namespace sh = leveleditor::shell;
    auto cellOf = [](float px, float py, int* c, int* r) {
        *c = std::clamp(static_cast<int>(px) / TILE, 0, sh::MAP_COLS - 1);
        *r = std::clamp(static_cast<int>(py) / TILE, 0, sh::MAP_ROWS - 1);
    };
    const SDL_Color red {216, 40, 32, 255};
    for (size_t k = 0; k < s.pointers.size(); k++) {
        const Pointer& p = s.pointers[k];
        if (p.drag < 0)
            continue;
        int c = 0, r = 0;
        cellOf(p.x, p.y, &c, &r);
        const bool erase = p.drag == B || currentTool() == TOOL_ERASE;
        if (s.armed != Armed::Copy)
            drawGhost(p.dragCol, p.dragRow, c, r, erase);
        const std::string size = std::to_string(std::abs(c - p.dragCol) + 1) + " x " + std::to_string(std::abs(r - p.dragRow) + 1);
        cellFrame(p.dragCol, p.dragRow, c, r, s.armed == Armed::Copy ? SDL_Color {255, 255, 255, 255} : erase ? red : pointerColor(static_cast<int>(k)),
            s.armed == Armed::Copy ? "Copy " + size : erase ? "Clear " + size : size, p.y);
    }
    if (s.mouseDrag) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(s.mouseDragCol, s.mouseDragRow, c, r, {255, 255, 255, 255}, "Copy", s.cy);
    }
    if (s.mouseRect) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        if (!s.mouseRectErase)
            drawGhost(s.mouseDragCol, s.mouseDragRow, c, r, false);
        const std::string size = std::to_string(std::abs(c - s.mouseDragCol) + 1) + " x " + std::to_string(std::abs(r - s.mouseDragRow) + 1);
        cellFrame(s.mouseDragCol, s.mouseDragRow, c, r, s.mouseRectErase ? red : SDL_Color {255, 255, 255, 255},
            s.mouseRectErase ? "Clear " + size : size, s.cy);
    }
    if (s.armed == Armed::Paste) {
        int w = 1, h = 1;
        sh::clipboard(&w, &h);
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(c, r, std::min(c + w - 1, sh::MAP_COLS - 1), std::min(r + h - 1, sh::MAP_ROWS - 1), {255, 255, 255, 255}, padUsed() ? "Paste here (A)" : "Paste here", s.cy);
    } else if (s.armed == Armed::Fill) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        cellFrame(c, r, c, r, {255, 255, 255, 255}, padUsed() ? "Fill from here (A)" : "Fill from here", s.cy);
    } else if (s.armed == Armed::Properties) {
        int c = 0, r = 0;
        cellOf(s.cx, s.cy, &c, &r);
        const bool block = sh::hasProperties(c, r);
        cellFrame(c, r, c, r, block ? SDL_Color {255, 255, 255, 255} : red, block ? "Open this block" : "Not a ? block", s.cy);
    }
}

// The tools menu, beside the pointer that opened it
void drawTools()
{
    const SDL_Rect r = toolsRect();
    theme::panel(r.x, r.y, r.w, r.h);
    theme::font().draw(r.x + 14, r.y + 14, "Tools");
    const char* LABELS[] = {"Undo", "Redo", "Copy an area", "Paste", "Fill", "Copy selection", "Delete selection",
        leveleditor::shell::moveReplace() ? "Moving: replaces" : "Moving: merges"};
    static const Art* ARTS[] = {nullptr, nullptr, &ART_MOVE, &ART_BRUSH, &ART_SPRING, &ART_MOVE, &ART_CLOSE, &ART_MOVE};
    static const Art ART_UNDO = {"..#.....", ".##.....", "#######.", ".##....#", "..#....#", ".......#", "..#####.", "........"};
    static const Art ART_REDO = {".....#..", ".....##.", ".#######", "#....##.", "#....#..", "#.......", ".#####..", "........"};
    static const Art ART_FILL = {"...#....", "..#w#...", ".#www#..", "#wwwww#.", ".#www#c.", "..#w#.cc", "...#..cc", "......c."};
    for (int k = 0; k < toolsCount(); k++) {
        const SDL_Rect ir = toolsItemRect(k);
        const bool sel = s.toolsIndex == k;
        const bool usable = toolUsable(k);
        theme::block(ir.x, ir.y, ir.w, ir.h, sel ? (usable ? theme::Tone::Gold : theme::Tone::Gray) : theme::Tone::Dark);
        const Art* art = k == T_UNDO ? &ART_UNDO : k == T_REDO ? &ART_REDO : k == T_FILL ? &ART_FILL : ARTS[k];
        drawArt(ir.x + 6, ir.y + (ir.h - 16) / 2, *art);
        std::string label = LABELS[k];
        if (k == T_UNDO && !s.undos.empty())
            label += " (" + std::to_string(s.undos.size()) + ")";
        if (k == T_REDO && !s.redos.empty())
            label += " (" + std::to_string(s.redos.size()) + ")";
        theme::font().draw(ir.x + 30, ir.y + (ir.h - theme::font().getHeight()) / 2, label);
        if (!usable)
            fill(ir.x + 28, ir.y + ir.h / 2, theme::font().getWidth(label) + 4, 2, {0, 0, 0, 255});
    }
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    if (controller)
        row = {{{prompts::Pad::A}, {}, "use"}, {{prompts::Pad::B}, {}, "close"}};
    else
        row = {{{}, {"Ctrl+Z"}, "undo"}, {{}, {"Ctrl+Y"}, "redo"}};
    prompts::drawPrompts(r.x + r.w / 2, r.y + r.h - 26, row, 10);
}

// The whole map, while a left stick is held: the level's (the screen, without the editor's
// panels) or the world, small, with where everyone's glove is
void drawPreview()
{
    const std::vector<coedit::Cursor> remote = coedit::remoteCursors();
    if (s.editor == Editor::World && s.worldPicture) {
        //(the world scaled to fit, over a dark screen)
        fill(0, 0, SCREEN_W, SCREEN_H, {16, 16, 24, 255});
        const int pw = s.worldPicture->w;
        const int ph = s.worldPicture->h;
        const float scale = std::min({1.0f, (SCREEN_W - 40.0f) / static_cast<float>(pw), (SCREEN_H - 90.0f) / static_cast<float>(ph)});
        const int w = static_cast<int>(static_cast<float>(pw) * scale);
        const int h = static_cast<int>(static_cast<float>(ph) * scale);
        const int x = (SCREEN_W - w) / 2;
        const int y = 50 + (SCREEN_H - 90 - h) / 2;
        theme::panel(x - 8, y - 8, w + 16, h + 16);
        const SDL_Rect dst {x, y, w, h};
        SDL_BlitSurfaceScaled(s.worldPicture, nullptr, screen, &dst, SDL_SCALEMODE_LINEAR);
        int ox = 0, oy = 0;
        worldeditor::shell::viewOrigin(&ox, &oy);
        //(the part on the screen now)
        const int vx = x + static_cast<int>(static_cast<float>(ox) * scale);
        const int vy = y + static_cast<int>(static_cast<float>(oy) * scale);
        const int vw = static_cast<int>(SCREEN_W * scale);
        const int vh = static_cast<int>(SCREEN_H * scale);
        const SDL_Color white {255, 255, 255, 255};
        fill(vx, vy, vw, 2, white);
        fill(vx, vy + vh - 2, vw, 2, white);
        fill(vx, vy, 2, vh, white);
        fill(vx + vw - 2, vy, 2, vh, white);
        auto mark = [&](float px, float py, SDL_Color c, const std::string& name, int id) {
            drawArrow(static_cast<float>(x) + (px + static_cast<float>(ox)) * scale, static_cast<float>(y) + (py + static_cast<float>(oy)) * scale, c, name, id);
        };
        for (const coedit::Cursor& c : remote)
            mark(c.x, c.y, c.color, c.name, 1000 + static_cast<int>(&c - remote.data()));
        for (size_t k = 0; k < s.pointers.size(); k++) {
            if (s.pointers[k].shown)
                mark(s.pointers[k].x, s.pointers[k].y, pointerColor(static_cast<int>(k)), "P" + std::to_string(k + 1), static_cast<int>(k));
        }
    } else {
        //(the level: the map is the screen; everyone's reticle and name on it)
        for (const coedit::Cursor& c : remote)
            reticle(c.x, c.y, c.color, c.name);
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                reticle(p.x, p.y, pointerColor(static_cast<int>(k)), coedit::active() && k == 0 ? "You" : "P" + std::to_string(k + 1));
        }
    }
    theme::announce(SCREEN_W / 2, 10, s.editor == Editor::World ? "The whole world" : "The whole map");
}

// The tileset screen: the tile under the cursor, framed, and which tileset it is
void drawTilePicker(const leveleditor::shell::TilePicker& picker)
{
    for (size_t k = 0; k < s.pointers.size(); k++) {
        const Pointer& p = s.pointers[k];
        if (!p.shown)
            continue;
        const int c = static_cast<int>(p.x) / TILE;
        const int r = static_cast<int>(p.y) / TILE;
        if (c + picker.viewCol >= picker.cols || r + picker.viewRow >= picker.rows)
            continue;
        const int x = c * TILE;
        const int y = r * TILE;
        const SDL_Color black {0, 0, 0, 255};
        const SDL_Color gold {248, 200, 24, 255};
        fill(x - 3, y - 3, TILE + 6, 3, black);
        fill(x - 3, y + TILE, TILE + 6, 3, black);
        fill(x - 3, y, 3, TILE, black);
        fill(x + TILE, y, 3, TILE, black);
        fill(x - 2, y - 2, TILE + 4, 3, gold);
        fill(x - 2, y + TILE - 1, TILE + 4, 3, gold);
        fill(x - 2, y, 3, TILE, gold);
        fill(x + TILE - 1, y, 3, TILE, gold);
    }
}

// The level editor's chip when the window is closed: what A puts down (or the tool), the layers, the
// cell; with a controller, the last things picked and what its buttons do now
void drawChip()
{
    namespace sh = leveleditor::shell;
    const int tool = currentTool();
    const sh::Brush brush = sh::brush();
    //(the name: the tool when it isn't painting, else the selection, with its size for tiles)
    std::string name = tool == TOOL_ERASE ? "Eraser" : tool == TOOL_PICK ? "Pick: A copies a cell" : selectionName();
    if (tool == TOOL_PAINT && brush.mode == 1)
        name = brushName(brush);
    const int col = static_cast<int>(s.cx) / TILE;
    const int row = static_cast<int>(s.cy) / TILE;
    const std::string where = std::to_string(col) + "," + std::to_string(row);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const std::vector<prompts::Prompt> row1 = controller ? std::vector<prompts::Prompt> {{{prompts::Pad::Start}, {}, "editor"}}
                                                         : std::vector<prompts::Prompt> {{{}, {"Tab"}, "editor"}};
    //(the buttons: what they do now; with a tool armed, what they do with it)
    bool dragging = false;
    for (const Pointer& p : s.pointers)
        dragging = dragging || p.drag >= 0;
    std::vector<prompts::Prompt> buttons;
    if (s.armed == Armed::Copy)
        buttons = {{{prompts::Pad::A}, {}, "hold and drag over the cells"}, {{prompts::Pad::B}, {}, "stop"}};
    else if (s.armed == Armed::Paste)
        buttons = {{{prompts::Pad::A}, {}, "paste here"}, {{prompts::Pad::B}, {}, "stop pasting"}};
    else if (s.armed == Armed::Fill)
        buttons = {{{prompts::Pad::A}, {}, "fill from this cell"}, {{prompts::Pad::B}, {}, "stop"}};
    else if (s.armed == Armed::Properties)
        buttons = {{{prompts::Pad::A}, {}, "open this ? block"}, {{prompts::Pad::B}, {}, "stop"}};
    else if (dragging)
        buttons = {{{prompts::Pad::LStick}, {}, "size the rectangle"}, {{}, {}, "release to fill it"}};
    else if (sh::paintsCells() && tool != TOOL_PICK)
        buttons = {{{prompts::Pad::A}, {}, tool == TOOL_ERASE ? "erase (hold: area)" : "place (hold: area)"},
            {{prompts::Pad::B}, {}, "erase"}, {{prompts::Pad::X}, {}, "take"}, {{prompts::Pad::Y}, {}, "layer"},
            {{prompts::Pad::RStick}, {}, "tools"}};
    else
        buttons = {{{prompts::Pad::A}, {}, tool == TOOL_PICK ? "take this" : "place"}, {{prompts::Pad::B}, {}, "remove"},
            {{prompts::Pad::X}, {}, "take"}, {{prompts::Pad::Y}, {}, "layer"}, {{prompts::Pad::RStick}, {}, "tools"}};
    const int pw = prompts::promptsWidth(row1, 14);
    const int bw = controller && s.hints ? prompts::promptsWidth(buttons, 10) : 0;
    //(the layers: four boxes, the one painted on lit)
    const int pillW = 22;
    const int layersW = theme::font().getWidth("Layer") + 6 + 4 * (pillW + 3);
    const std::string info = where;
    //(the last things picked, with LB and RB at their ends)
    const bool recents = controller && s.recents.size() >= 2;
    const int recentW = recents ? static_cast<int>(s.recents.size()) * 26 + 2 * prompts::padWidth(prompts::Pad::LB) + 12 : 0;
    const int line2 = layersW + 12 + theme::font().getWidth(info);
    const int w = std::max({46 + std::max(theme::font().getWidth(name), line2) + 16 + pw + 10, bw + 20, recentW + 20});
    const int h = 46 + (recents ? 28 : 0) + (bw > 0 ? 24 : 0);
    //(bottom left; at the top while a pointer is down there)
    bool low = s.cy > SCREEN_H - h - 44 && s.cx < w + 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 44 && p.x < w + 30);
    const int x = 6;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::inset(x + 6, y + 6, 34, 34);
    if (tool == TOOL_ERASE)
        drawArt(x + 15, y + 15, ART_ERASER);
    else if (tool == TOOL_PICK)
        drawArt(x + 15, y + 15, ART_PICK);
    else
        drawSelection(x + 7, y + 7);
    theme::font().draw(x + 46, y + 7, name);
    //(the layers)
    int lx = x + 46;
    theme::font().draw(lx, y + 24, "Layer");
    lx += theme::font().getWidth("Layer") + 6;
    for (int k = 0; k < 4; k++) {
        const bool on = sh::layer() == k;
        theme::block(lx, y + 24, pillW, 16, on ? theme::Tone::Green : theme::Tone::Dark);
        theme::font().drawCentered(lx + pillW / 2, y + 24, std::to_string(k + 1));
        lx += pillW + 3;
    }
    theme::font().draw(lx + 9, y + 24, info);
    prompts::drawPrompts(x + w - pw / 2 - 8, y + 14, row1);
    int by = y + 47;
    if (recents) {
        int rx = x + 10;
        rx += prompts::drawPad(rx, by + 4, prompts::Pad::LB) + 4;
        for (const sh::Brush& b : s.recents) {
            if (b == brush) {
                fill(rx - 2, by - 2, 28, 28, {0, 0, 0, 255});
                fill(rx - 1, by - 1, 26, 26, {248, 200, 24, 255});
            }
            drawScaled(rx, by, 24, [&b](int ix, int iy) { drawBrush(b, ix, iy); });
            rx += 26;
        }
        prompts::drawPad(rx + 2, by + 4, prompts::Pad::RB);
        by += 28;
    }
    if (bw > 0)
        prompts::drawPrompts(x + 10 + bw / 2, by, buttons, 10);
}

// The world editor's chip: the mode, the world, and (with a controller) its buttons
void drawWorldChip()
{
    namespace sh = worldeditor::shell;
    const std::string name = sh::modeName() + (sh::autoPaint() ? "  (auto-paint)" : "");
    const std::string info = sh::worldName() + "   " + std::to_string(static_cast<int>(s.cx) / TILE) + "," +
                             std::to_string(static_cast<int>(s.cy) / TILE);
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const std::vector<prompts::Prompt> row1 = controller ? std::vector<prompts::Prompt> {{{prompts::Pad::Start}, {}, "editor"}}
                                                         : std::vector<prompts::Prompt> {{{}, {"Tab"}, "editor"}};
    const std::vector<prompts::Prompt> buttons = {{{prompts::Pad::A}, {}, "place"}, {{prompts::Pad::B}, {}, "remove"},
        {{prompts::Pad::LT, prompts::Pad::RT}, {}, "shift/ctrl"}, {{prompts::Pad::RStick}, {}, "undo"}, {{prompts::Pad::LStick}, {}, "see all"}};
    const int pw = prompts::promptsWidth(row1, 14);
    const int bw = controller && s.hints ? prompts::promptsWidth(buttons, 10) : 0;
    const int w = std::max(46 + std::max(theme::font().getWidth(name), theme::font().getWidth(info)) + 16 + pw + 10, bw + 20);
    const int h = bw > 0 ? 70 : 46;
    bool low = s.cy > SCREEN_H - h - 44 && s.cx < w + 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 44 && p.x < w + 30);
    const int x = 6;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::inset(x + 6, y + 6, 34, 34);
    theme::icon(x + 15, y + 15, theme::Icon::Star);
    theme::font().draw(x + 46, y + 7, name);
    theme::font().draw(x + 46, y + 24, info);
    prompts::drawPrompts(x + w - pw / 2 - 8, y + 14, row1);
    if (bw > 0)
        prompts::drawPrompts(x + 10 + bw / 2, y + 47, buttons, 10);
}

void drawKeyboard()
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const int px = OSK_PANEL_X;
    const int py = OSK_PANEL_Y;
    //The panel (over the dialog: it shows the dialog's title and text itself)
    menupanel::box(px - 4, py - 4, OSK_PANEL_W + 8, OSK_PANEL_H + 8);
    theme::bigFont().drawCentered(SCREEN_W / 2, py + 10, s.oskTitle ? s.oskTitle : "Type");
    if (s.oskPrompt)
        rm->menu_font_small.drawCentered(SCREEN_W / 2, py + 40, s.oskPrompt);
    //The text so far, in a field, with a blinking cursor
    const SDL_Rect field {px + 20, py + 60, OSK_PANEL_W - 40, 36};
    theme::inset(field.x, field.y, field.w, field.h);
    const std::string text = s.oskTyped && s.oskText ? s.oskText : std::string();
    const int tw = theme::bigFont().getWidth(text);
    const int tx = field.x + 12 + std::min(0, field.w - 36 - tw);
    SDL_Rect clip {field.x + 4, field.y, field.w - 8, field.h};
    SDL_SetSurfaceClipRect(screen, &clip);
    theme::bigFont().draw(tx, field.y + 6, text);
    if ((SDL_GetTicks() / 400) % 2 == 0)
        fill(tx + tw + 2, field.y + 7, 3, field.h - 14, {255, 255, 255, 255});
    SDL_SetSurfaceClipRect(screen, nullptr);
    s.overlayDirty = true;    //(the cursor blinks)

    //The keys: the pipes (light green selected; Done green, Shift and Caps Lock green while on), the blocks
    //(gray; the selected one gold, a ? block's, in a white frame); a key pressed is drawn down for a moment
    const std::vector<OskKey>& keys = oskKeys();
    const bool justPressed = s.oskPressedKey >= 0 && SDL_GetTicks() - s.oskPressedMs < 140;
    //(drawn here: the game's menu graphics are put away while an editor runs)
    struct PipeColors {
        SDL_Color light, mid, dark, darker;
    };
    auto pipe = [](const SDL_Rect& r, const PipeColors& c) {
        const SDL_Color black {0, 0, 0, 255};
        //The body, between its lips: a light band on top, shade underneath
        const int bx = r.x + 8;
        const int bw = r.w - 16;
        fill(bx, r.y + 3, bw, 26, black);
        fill(bx, r.y + 4, bw, 24, c.mid);
        fill(bx, r.y + 6, bw, 4, c.light);
        fill(bx, r.y + 20, bw, 4, c.dark);
        fill(bx, r.y + 24, bw, 4, c.darker);
        //The lips at both ends: a little taller and wider
        for (const int lx : {r.x, r.x + r.w - 12}) {
            fill(lx, r.y, 12, 32, black);
            fill(lx + 1, r.y + 1, 10, 30, c.mid);
            fill(lx + 1, r.y + 1, 3, 30, c.light);
            fill(lx + 8, r.y + 1, 3, 30, c.dark);
            fill(lx + 1, r.y + 27, 10, 4, c.darker);
        }
    };
    static const PipeColors GRAY {{176, 192, 192, 255}, {144, 160, 160, 255}, {112, 128, 128, 255}, {53, 64, 64, 255}};
    static const PipeColors LIGHT_GRAY {{216, 228, 228, 255}, {184, 200, 200, 255}, {144, 160, 160, 255}, {80, 96, 96, 255}};
    static const PipeColors GREEN {{107, 203, 0, 255}, {57, 154, 0, 255}, {40, 128, 0, 255}, {9, 60, 0, 255}};
    static const PipeColors LIGHT_GREEN {{180, 255, 64, 255}, {148, 243, 0, 255}, {107, 203, 0, 255}, {16, 113, 0, 255}};
    for (size_t i = 0; i < keys.size(); i++) {
        const OskKey& k = keys[i];
        const bool selected = (controller && s.oskKey == static_cast<int>(i)) || (!controller && s.hoverKey == static_cast<int>(i));
        const bool down = justPressed && s.oskPressedKey == static_cast<int>(i);
        SDL_Rect r = k.r;
        if (down)
            r.y += 2;
        if (k.kind == OSK_CHAR) {
            theme::block(r.x, r.y, r.w, r.h, selected ? theme::Tone::Gold : down ? theme::Tone::Green : theme::Tone::Gray);
            char ch = k.ch;
            if ((s.oskShift != s.oskCaps) && ch >= 'a' && ch <= 'z')
                ch = static_cast<char>(ch - 32);
            theme::bigFont().drawCentered(r.x + r.w / 2, r.y + (r.h - theme::bigFont().getHeight()) / 2 + 1, std::string(1, ch));
            if (selected) {
                const SDL_Color white {255, 255, 255, 255};
                fill(r.x - 3, r.y - 3, r.w + 6, 2, white);
                fill(r.x - 3, r.y + r.h + 1, r.w + 6, 2, white);
                fill(r.x - 3, r.y - 1, 2, r.h + 2, white);
                fill(r.x + r.w + 1, r.y - 1, 2, r.h + 2, white);
            }
            continue;
        }
        const bool on = (k.kind == OSK_SHIFT && s.oskShift) || (k.kind == OSK_CAPS && s.oskCaps);
        pipe(r, selected ? LIGHT_GREEN : (k.kind == OSK_ENTER || on) ? GREEN : down ? LIGHT_GRAY : GRAY);
        static const char* labels[] = {"", "SHIFT", "CAPS LOCK", "CLEAR", "SPACE", "BACKSPACE", "DONE", "CANCEL"};
        //(between the lips: the small font if the large one doesn't fit)
        const bool fits = rm->menu_font_large.getWidth(labels[k.kind]) <= r.w - 30;
        gfxFont& font = fits ? rm->menu_font_large : rm->menu_font_small;
        font.drawCentered(r.x + r.w / 2, r.y + (32 - font.getHeight()) / 2, labels[k.kind]);
    }
    if (justPressed)
        s.overlayDirty = true;    //(it comes up again)

    //The buttons, for the device in use
    std::vector<prompts::Prompt> row;
    if (controller)
        row = {{{prompts::Pad::A}, {}, "type"}, {{prompts::Pad::B}, {}, "delete"}, {{prompts::Pad::X}, {}, "shift"},
            {{prompts::Pad::Y}, {}, "space"}, {{prompts::Pad::Start}, {}, "done"}, {{prompts::Pad::Back}, {}, "cancel"}};
    else
        row = {{{}, {"Enter"}, "done"}, {{}, {"Esc"}, "cancel"}};
    prompts::drawPrompts(SCREEN_W / 2, py + OSK_PANEL_H - 26, row, 14);
    if (!controller)
        rm->menu_font_small.drawCentered(SCREEN_W / 2, py + OSK_PANEL_H - 44, "Type, or click the keys");
}

// The level editor's other screens: a strip with the screen's name and what the buttons do
void drawScreenStrip()
{
    const leveleditor::shell::Screen& sc = s.screen;
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    std::vector<prompts::Prompt> row;
    const bool exitShown = s.editor == Editor::Level && leveleditor::shell::exitPrompt();
    if (controller) {
        if (!sc.a.empty())
            row.push_back({{prompts::Pad::A}, {}, sc.a});
        if (!sc.b.empty())
            row.push_back({{prompts::Pad::B}, {}, sc.b});
        if (!sc.shoulders.empty())
            row.push_back({{prompts::Pad::LB, prompts::Pad::RB}, {}, sc.shoulders});
        //(the exit prompt: only its own answers)
        if (!exitShown) {
            row.push_back({{prompts::Pad::Start}, {}, "more"});
            row.push_back({{prompts::Pad::Back}, {}, "done"});
        }
    } else if (exitShown) {
        row.push_back({{}, {"Enter"}, "choose"});
        row.push_back({{}, {"Esc"}, "stay"});
    } else {
        row.push_back({{}, {"Tab"}, "commands"});
        row.push_back({{}, {"Esc"}, "done"});
    }
    //(a long row: closer together)
    const int gap = prompts::promptsWidth(row, 12) > SCREEN_W - 40 ? 6 : 12;
    const int pw = prompts::promptsWidth(row, gap);
    const std::string hold = controller ? sc.holds : std::string();
    const int w = std::min(SCREEN_W - 12, std::max({pw + 24, theme::font().getWidth(sc.title) + 24, theme::font().getWidth(hold) + 24}));
    const int h = hold.empty() ? 48 : 66;
    //(at the bottom; at the top while a cursor is down there)
    bool low = s.cy > SCREEN_H - h - 30;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - h - 30);
    const int x = (SCREEN_W - w) / 2;
    const int y = low ? 6 : SCREEN_H - h - 6;
    theme::strip(x, y, w, h);
    theme::font().drawCentered(SCREEN_W / 2, y + 6, sc.title);
    prompts::drawPrompts(SCREEN_W / 2, y + 26, row, gap);
    if (!hold.empty())
        theme::font().drawCentered(SCREEN_W / 2, y + 46, hold);
}

// Where the hint bar is: shown, and its top
bool hintsShown(int* top)
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    const bool shown = controller ? s.hints : SDL_GetTicks() - s.startMs <= 8000;
    //(at the bottom, or at the top while a cursor is down there)
    bool low = s.cy > SCREEN_H - 70;
    for (const Pointer& p : s.pointers)
        low = low || (p.shown && p.y > SCREEN_H - 70);
    if (top)
        *top = low ? 20 : SCREEN_H - 22;
    return shown && s.overlay == Overlay::None;
}

// The controller's buttons (LB hides it); on the keyboard, how to open the actions
void drawHints()
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    int y = 0;
    if (!hintsShown(&y))
        return;
    //(the help has its own table, and any button closes it)
    if (s.editor == Editor::Level && leveleditor::shell::helpShown())
        return;
    std::vector<std::tuple<prompts::Pad, std::string, std::string>> items;
    if (controller)
        items = {{prompts::Pad::A, "", "place"}, {prompts::Pad::B, "", "remove"}, {prompts::Pad::X, "", "shift"},
            {prompts::Pad::Y, "", "ctrl"}, {prompts::Pad::RB, "", "ok"}, {prompts::Pad::Start, "", "editor"},
            {prompts::Pad::Back, "", "back"}, {prompts::Pad::LB, "", "hide bar"}};
    else
        items = {{prompts::Pad::Start, "Tab", "editor"}, {prompts::Pad::Back, "F1", "help"}, {prompts::Pad::B, "Esc", "back"}};
    const int gap = 7;
    int w = 8;
    for (const auto& [pad, key, label] : items)
        w += prompts::promptWidth(pad, key, label) + gap;
    const int x = (SCREEN_W - w) / 2;
    theme::strip(x - 6, y - 6, w + 12, 28);
    int ix = x + 4;
    for (const auto& [pad, key, label] : items)
        ix += prompts::drawPrompt(ix, y, pad, key, label) + gap;
}

// The level editor's help (F1): a table of what the controller's buttons do, or the mouse's and the
// keys' (whichever was used last; the prompts draw that device's)
void drawLevelHelp()
{
    const bool controller = prompts::lastDevice() == prompts::Device::Controller;
    struct Line {
        prompts::Pad pad;
        const char* key;
        std::string text;
    };
    using prompts::Pad;
    //(the shoulders' names as the style draws them: RB, or the original's Black)
    const Line PAD[] = {
        {Pad::A, "", "place (hold and move: a rectangle)"},
        {Pad::B, "", "erase (hold and move: a rectangle)"},
        {Pad::LT, "", "held: A and B paint and erase freely"},
        {Pad::X, "", "pick up what's under the cursor"},
        {Pad::Y, "", "the next layer"},
        {Pad::LB, "", "and " + prompts::padName(Pad::RB) + ": what you used lately"},
        {Pad::LStick, "", "move (pressed: the whole map)"},
        {Pad::RStick, "", "move slowly (pressed: the tools)"},
        {Pad::Start, "", "the editor window: everything else"},
        {Pad::Back, "", "leave the editor"},
    };
    const Line KEYS[] = {
        {Pad::A, "", "place (drag: paint)"},
        {Pad::B, "RMB", "erase"},
        {Pad::A, "Shift", "and drag: fill a rectangle"},
        {Pad::A, "Alt", "and click: pick up a cell"},
        {Pad::A, "Wheel", "what you used lately (Ctrl: the layer)"},
        {Pad::A, "Middle", "button: the tools"},
        {Pad::A, "Ctrl+Z", "undo (Ctrl+Y: redo)"},
        {Pad::A, "Ctrl+C", "copy an area (Ctrl+V: paste it)"},
        {Pad::A, "Tab", "the editor window: every command, with its key"},
        {Pad::A, "Esc", "leave the editor"},
    };
    const Line* lines = controller ? PAD : KEYS;
    const int count = 10;
    const int lineH = 24;
    const int w = 500;
    const int h = 70 + count * lineH + 30;
    const int x = (SCREEN_W - w) / 2;
    const int y = (SCREEN_H - h) / 2;
    theme::panel(x, y, w, h);
    theme::font().drawCentered(SCREEN_W / 2, y + 14, controller ? "Map editor: the controller" : "Map editor: the mouse and keys");
    for (int k = 0; k < count; k++)
        prompts::drawPrompt(x + 30, y + 50 + k * lineH, lines[k].pad, lines[k].key, lines[k].text);
    rm->menu_font_small.drawCentered(SCREEN_W / 2, y + h - 26,
        controller ? "Elsewhere, the bar at the bottom shows the buttons" : "Elsewhere, Tab lists a screen's commands");
}

// Who is editing (online), and the latest notice
void drawSession()
{
    const std::vector<coedit::Person> people = coedit::people();
    if (!people.empty()) {
        const int lineH = 18;
        int w = theme::font().getWidth("Editing together") + 28;
        for (const coedit::Person& p : people)
            w = std::max(w, theme::font().getWidth(p.name + (p.you ? " (you)" : "")) + (p.host ? 64 : 46));
        const int h = lineH * static_cast<int>(people.size()) + 46;
        const int x = 2;
        //(above the hint bar when it is at the bottom)
        int hintsTop = 0;
        const int bottom = hintsShown(&hintsTop) && hintsTop > SCREEN_H / 2 ? hintsTop - 6 : SCREEN_H;
        const int y = bottom - h;
        theme::panel(x, y, w, h);
        theme::font().draw(x + 14, y + 10, "Editing together");
        int py = y + 30;
        for (const coedit::Person& p : people) {
            //A coin in their color
            prompts::art(x + 14, py + 1, {
                ".####.",
                "#llcc#",
                "#lcDc#",
                "#ccDc#",
                "#ccDd#",
                "#cccd#",
                "#ccdd#",
                ".####.",
            }, {{'#', {0, 0, 0, 255}}, {'l', theme::shade(p.color, 150)}, {'c', p.color}, {'d', theme::shade(p.color, 60)},
                {'D', theme::shade(p.color, 75)}}, 2);
            const std::string name = p.name + (p.you ? " (you)" : "");
            theme::font().draw(x + 32, py, name);
            if (p.host)
                theme::crown(x + 36 + theme::font().getWidth(name), py + 1);
            py += lineH;
        }
    }
    const std::string notice = coedit::notice();
    if (!notice.empty()) {
        //An announcement (the card: a 1-up for who came, else a Bob-omb)
        const bool came = notice.find("joined") != std::string::npos;
        theme::announce(SCREEN_W / 2, 36, notice, [came](int ix, int iy) {
            theme::statusIcon(ix + 8, iy + 8, came ? theme::Status::Good : theme::Status::Warn);
        });
    }
}

bool overlayWanted()
{
    if (s.overlay != Overlay::None || coedit::active() || !coedit::notice().empty() || levelMap() || s.mouseUsed || !s.screen.title.empty() ||
        s.preview || toastShown())
        return true;
    if (prompts::lastDevice() == prompts::Device::Controller ? s.hints && !s.pointers.empty() : SDL_GetTicks() - s.startMs < 8000)
        return true;
    for (const Pointer& p : s.pointers) {
        if (p.shown)
            return true;
    }
    return false;
}

void drawOverlay()
{
    if (!rm)
        return;
    const bool mapView = s.editor != Editor::Level || leveleditor::shell::inMapView();
    const bool keyboard = s.overlay == Overlay::Keyboard;
    const std::vector<coedit::Cursor> remote = coedit::remoteCursors();
    //The whole map: nothing else
    if (s.preview && !keyboard) {
        drawPreview();
        if (s.editor == Editor::Level) {
            for (const coedit::Cursor& c : remote)
                drawArrow(c.x, c.y, c.color, std::string(), 1000 + static_cast<int>(&c - remote.data()));
            for (size_t k = 0; k < s.pointers.size(); k++) {
                if (s.pointers[k].shown)
                    drawArrow(s.pointers[k].x, s.pointers[k].y, pointerColor(static_cast<int>(k)), std::string(), static_cast<int>(k));
            }
        }
        return;
    }
    //The tileset screen: the tile under each cursor
    leveleditor::shell::TilePicker picker;
    if (s.editor == Editor::Level && !keyboard && leveleditor::shell::tilePicker(&picker))
        drawTilePicker(picker);
    if (levelMap() && !keyboard) {
        drawGhosts();
        drawCellTools();
    }
    //Where everyone edits: a reticle on each player's cell
    if (mapView && !keyboard) {
        const bool several = s.pointers.size() > 1 || coedit::active();
        for (const coedit::Cursor& c : remote) {
            if (!inWindow(c.x, c.y))
                reticle(c.x, c.y, c.color, c.name);
        }
        if (s.owner < 0 && s.mouseUsed && !inWindow(s.cx, s.cy))
            reticle(s.cx, s.cy, pointerColor(0), several ? (coedit::active() ? "You" : "P1") : std::string());
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown && !inWindow(p.x, p.y))
                reticle(p.x, p.y, pointerColor(static_cast<int>(k)), several ? "P" + std::to_string(k + 1) : std::string());
        }
    }
    if (s.overlay == Overlay::None) {
        if (levelMap())
            drawChip();
        else if (s.editor == Editor::World && worldeditor::shell::inMapView())
            drawWorldChip();
        else if (!s.screen.title.empty())
            drawScreenStrip();
        else
            drawHints();
    }
    drawSession();
    if (s.overlay == Overlay::Window)
        drawWindow();
    else if (keyboard)
        drawKeyboard();
    else if (s.overlay == Overlay::Tools)
        drawTools();
    if (toastShown() && !keyboard)
        theme::announce(SCREEN_W / 2, 36, s.toast);
    //The cursors on top (the controllers' drawn pointers; the others' online)
    for (const coedit::Cursor& c : remote)
        drawArrow(c.x, c.y, c.color, mapView ? std::string() : c.name, 1000 + static_cast<int>(&c - remote.data()));
    if (!keyboard) {
        for (size_t k = 0; k < s.pointers.size(); k++) {
            const Pointer& p = s.pointers[k];
            if (p.shown)
                drawArrow(p.x, p.y, pointerColor(static_cast<int>(k)), std::string(), static_cast<int>(k), p.pad.buttons[A] || p.pad.buttons[B]);
        }
    }
    //The mouse: a glove too, on top
    if (s.mouseUsed && s.mouseX >= 0)
        drawArrow(s.mouseX, s.mouseY, pointerColor(0), std::string(), -1, (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) != 0);
}

// Presents the frame with the overlay; the screen keeps the editor's frame
void present()
{
    if (!screen)
        return;
    if (!s.frame || s.frame->w != screen->w || s.frame->h != screen->h || s.frame->format != screen->format) {
        if (s.frame)
            SDL_DestroySurface(s.frame);
        s.frame = SDL_CreateSurface(screen->w, screen->h, screen->format);
    }
    const bool overlay = overlayWanted();
    if (overlay && s.frame) {
        SDL_BlitSurface(screen, nullptr, s.frame, nullptr);
        drawOverlay();
    }
    if (!g_pendingShot.empty()) {
        gfx_save_screenshot(g_testShotDir + "/" + g_pendingShot + ".png");
        printf("shots: saved %s/%s.png\n", g_testShotDir.c_str(), g_pendingShot.c_str());
        g_pendingShot.clear();
    }
    gfx_flipscreen();
    if (overlay && s.frame)
        SDL_BlitSurface(s.frame, nullptr, screen, nullptr);
    s.lastPresentMs = SDL_GetTicks();
    s.overlayDirty = false;
}

} // namespace editors::input::detail

namespace leveleditor::shell {

void drawHelp()
{
    editors::input::detail::drawLevelHelp();
}

} // namespace leveleditor::shell

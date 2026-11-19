#include "InputPrompts.h"

#include "gfx.h"
#include "GameValues.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

extern CResourceManager* rm;

namespace prompts {
namespace {

Device g_last = Device::Keyboard;
void noteController(SDL_JoystickID id);
constexpr float DEADZONE = 0.22f;

constexpr SDL_Color WHITE {255, 255, 255, 255};
constexpr SDL_Color BLACK {0, 0, 0, 255};

float axis(Sint16 value)
{
    const float v = std::clamp(value / 32767.0f, -1.0f, 1.0f);
    return std::fabs(v) < DEADZONE ? 0.0f : v;
}

void text(int x, int y, const std::string& s)
{
    if (rm)
        rm->menu_font_small.draw(x, y, s);
}

int textWidth(const std::string& s)
{
    return rm ? rm->menu_font_small.getWidth(s) : static_cast<int>(s.size()) * 8;
}

} // namespace

Device lastDevice()
{
    return g_last;
}

void setLastDevice(Device device)
{
    g_last = device;
}

void note(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
            g_last = Device::Keyboard;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (std::fabs(event.motion.xrel) + std::fabs(event.motion.yrel) > 2)
                g_last = Device::Keyboard;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            g_last = Device::Controller;
            noteController(event.gbutton.which);
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            g_last = Device::Controller;
            noteController(event.jbutton.which);
            break;
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            g_last = Device::Controller;
            noteController(event.jhat.which);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(event.gaxis.value) > 16000) {
                g_last = Device::Controller;
                noteController(event.gaxis.which);
            }
            break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            if (std::abs(event.jaxis.value) > 16000) {
                g_last = Device::Controller;
                noteController(event.jaxis.which);
            }
            break;
        default:
            break;
    }
}

// ---- shapes

// ---- the glyph sheet
//
// The shapes and the pixel art below are drawn once each (per color and size)
// into a sprite sheet built at run time, and from then on blitted from it: one
// blit per glyph instead of a fill per row or pixel (thousands a frame on the
// theater's screens).

namespace {

class GlyphSheet {
public:
    struct Glyph {
        SDL_Surface* page = nullptr;
        SDL_Rect rect {};
    };

    ~GlyphSheet() { clear(); }

    const Glyph* find(const std::string& key) const
    {
        const auto it = m_glyphs.find(key);
        return it != m_glyphs.end() ? &it->second : nullptr;
    }

    // A transparent w x h area for a new glyph (nullptr if it doesn't fit a page)
    const Glyph* add(const std::string& key, int w, int h)
    {
        if (w <= 0 || h <= 0 || w > PAGE_W || h > PAGE_H)
            return nullptr;
        if (m_x + w > PAGE_W) {    // next row
            m_x = 0;
            m_y += m_rowHeight;
            m_rowHeight = 0;
        }
        if (m_pages.empty() || m_y + h > PAGE_H) {    // next page
            if (m_pages.size() >= MAX_PAGES)
                clear();    // (only if something keeps making new glyphs)
            SDL_Surface* page = SDL_CreateSurface(PAGE_W, PAGE_H, SDL_PIXELFORMAT_ARGB8888);
            if (!page)
                return nullptr;
            SDL_FillSurfaceRect(page, nullptr, SDL_MapSurfaceRGBA(page, 0, 0, 0, 0));
            SDL_SetSurfaceBlendMode(page, SDL_BLENDMODE_BLEND);
            m_pages.push_back(page);
            m_x = 0;
            m_y = 0;
            m_rowHeight = 0;
        }
        Glyph glyph {m_pages.back(), {m_x, m_y, w, h}};
        m_x += w + 1;
        m_rowHeight = std::max(m_rowHeight, h + 1);
        return &(m_glyphs[key] = glyph);
    }

private:
    static constexpr int PAGE_W = 512;
    static constexpr int PAGE_H = 256;
    static constexpr size_t MAX_PAGES = 8;

    std::unordered_map<std::string, Glyph> m_glyphs;
    std::vector<SDL_Surface*> m_pages;
    int m_x = 0;
    int m_y = 0;
    int m_rowHeight = 0;

    void clear()
    {
        m_glyphs.clear();
        for (SDL_Surface* page : m_pages)
            SDL_DestroySurface(page);
        m_pages.clear();
    }
};

GlyphSheet& glyphSheet()
{
    static GlyphSheet sheet;
    return sheet;
}

void appendColor(std::string& key, SDL_Color c)
{
    key += static_cast<char>(c.r);
    key += static_cast<char>(c.g);
    key += static_cast<char>(c.b);
}

// A solid rectangle inside a glyph's area of the sheet
void plot(const GlyphSheet::Glyph& glyph, int x, int y, int w, int h, SDL_Color c)
{
    const SDL_Rect rect {glyph.rect.x + x, glyph.rect.y + y, w, h};
    SDL_FillSurfaceRect(glyph.page, &rect, SDL_MapSurfaceRGBA(glyph.page, c.r, c.g, c.b, 255));
}

void blitGlyph(const GlyphSheet::Glyph& glyph, int x, int y)
{
    if (!blitdest)
        return;
    const SDL_Rect dst {x, y, glyph.rect.w, glyph.rect.h};
    SDL_BlitSurface(glyph.page, &glyph.rect, blitdest, &dst);
}

} // namespace

void fill(int x, int y, int w, int h, SDL_Color c)
{
    if (!blitdest || w <= 0 || h <= 0)
        return;
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, c.r, c.g, c.b));
}

void frame(int x, int y, int w, int h, SDL_Color border, SDL_Color inside)
{
    fill(x, y, w, h, border);
    fill(x + 2, y + 2, w - 4, h - 4, inside);
}

void disc(int cx, int cy, int r, SDL_Color c)
{
    if (r < 0)
        return;
    std::string key = "disc";
    key += std::to_string(r);
    appendColor(key, c);
    const GlyphSheet::Glyph* glyph = glyphSheet().find(key);
    if (!glyph) {
        glyph = glyphSheet().add(key, 2 * r + 1, 2 * r + 1);
        if (!glyph)
            return;
        for (int dy = -r; dy <= r; dy++) {
            const int half = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)) + 0.35f);
            plot(*glyph, r - half, r + dy, 2 * half + 1, 1, c);
        }
    }
    blitGlyph(*glyph, cx - r, cy - r);
}

void line(int x0, int y0, int x1, int y1, int thickness, SDL_Color c)
{
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int k = 0; k <= steps; k++) {
        const int x = x0 + (steps ? (x1 - x0) * k / steps : 0);
        const int y = y0 + (steps ? (y1 - y0) * k / steps : 0);
        fill(x - thickness / 2, y - thickness / 2, thickness, thickness, c);
    }
}

// ---- pixel art

void art(int x, int y, const std::vector<const char*>& rows, const std::vector<Ink>& inks, int scale)
{
    if (rows.empty() || scale <= 0)
        return;

    //The key: the size, the inks and the picture
    std::string key = "art";
    key += static_cast<char>('0' + scale);
    for (const Ink& ink : inks) {
        key += ink.c;
        appendColor(key, ink.color);
    }
    int columns = 0;
    for (const char* line : rows) {
        key += '|';
        key += line;
        columns = std::max(columns, static_cast<int>(std::char_traits<char>::length(line)));
    }

    const GlyphSheet::Glyph* glyph = glyphSheet().find(key);
    if (!glyph) {
        glyph = glyphSheet().add(key, columns * scale, static_cast<int>(rows.size()) * scale);
        if (!glyph)
            return;
        auto colorOf = [&inks](char c) -> const SDL_Color* {
            for (const Ink& ink : inks) {
                if (ink.c == c)
                    return &ink.color;
            }
            return nullptr;
        };
        for (size_t row = 0; row < rows.size(); row++) {
            const char* line = rows[row];
            //(runs of one color as one rectangle)
            for (int col = 0; line[col];) {
                const SDL_Color* c = colorOf(line[col]);
                int run = 1;
                while (line[col + run] && line[col + run] == line[col])
                    run++;
                if (c)
                    plot(*glyph, col * scale, static_cast<int>(row) * scale, run * scale, scale, *c);
                col += run;
            }
        }
    }
    blitGlyph(*glyph, x, y);
}

namespace {

// 3x5 capitals and digits
struct TinyChar {
    char ch;
    const char* rows[5];
};
const TinyChar TINY[] = {
    {'A', {".#.", "#.#", "###", "#.#", "#.#"}},
    {'B', {"##.", "#.#", "##.", "#.#", "##."}},
    {'C', {".##", "#..", "#..", "#..", ".##"}},
    {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}},
    {'P', {"##.", "#.#", "##.", "#..", "#.."}},
    {'L', {"#..", "#..", "#..", "#..", "###"}},
    {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
    {'S', {".##", "#..", ".#.", "..#", "##."}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
    {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}},
    {'0', {"###", "#.#", "#.#", "#.#", "###"}},
    {'1', {".#.", "##.", ".#.", ".#.", "###"}},
    {'2', {"##.", "..#", ".#.", "#..", "###"}},
    {'4', {"#.#", "#.#", "###", "..#", "..#"}},
    {'5', {"###", "#..", "##.", "..#", "##."}},
    {'8', {"###", "#.#", "###", "#.#", "###"}},
    {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
    {'E', {"###", "#..", "##.", "#..", "###"}},
    {'F', {"###", "#..", "##.", "#..", "#.."}},
    {'G', {".##", "#..", "#.#", "#.#", ".##"}},
    {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
    {'I', {"###", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..#", "..#", "..#", "#.#", ".#."}},
    {'M', {"#.#", "###", "###", "#.#", "#.#"}},
    {'N', {"##.", "#.#", "#.#", "#.#", "#.#"}},
    {'O', {".#.", "#.#", "#.#", "#.#", ".#."}},
    {'Q', {".#.", "#.#", "#.#", "##.", ".##"}},
    {'U', {"#.#", "#.#", "#.#", "#.#", "###"}},
    {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
    {'W', {"#.#", "#.#", "###", "###", "#.#"}},
    {'Z', {"###", "..#", ".#.", "#..", "###"}},
    {'3', {"##.", "..#", ".#.", "..#", "##."}},
    {'6', {".##", "#..", "##.", "#.#", ".#."}},
    {'7', {"###", "..#", ".#.", ".#.", ".#."}},
    {'9', {".#.", "#.#", ".##", "..#", "##."}},
    {'x', {"...", "#.#", ".#.", "#.#", "..."}},
    {'/', {"..#", "..#", ".#.", "#..", "#.."}},
};

const TinyChar* tinyChar(char ch)
{
    for (const TinyChar& t : TINY) {
        if (t.ch == ch)
            return &t;
    }
    return nullptr;
}

} // namespace

int tinyText(int x, int y, const std::string& s, SDL_Color c, int scale)
{
    int cx = x;
    for (char ch : s) {
        if (const TinyChar* t = tinyChar(ch)) {
            std::string key = "tiny";
            key += ch;
            key += static_cast<char>('0' + scale);
            appendColor(key, c);
            const GlyphSheet::Glyph* glyph = glyphSheet().find(key);
            if (!glyph && scale > 0) {
                glyph = glyphSheet().add(key, 3 * scale, 5 * scale);
                if (glyph) {
                    for (int row = 0; row < 5; row++) {
                        for (int col = 0; col < 3; col++) {
                            if (t->rows[row][col] == '#')
                                plot(*glyph, col * scale, row * scale, scale, scale, c);
                        }
                    }
                }
            }
            if (glyph)
                blitGlyph(*glyph, cx, y);
        }
        cx += 4 * scale;
    }
    return std::max(0, cx - x - scale);
}

int tinyWidth(const std::string& s, int scale)
{
    return s.empty() ? 0 : static_cast<int>(s.size()) * 4 * scale - scale;
}

// ---- the controller styles

namespace {

Style g_detected = Style::Xbox;

void noteController(SDL_JoystickID id)
{
    Uint16 vendor = 0, product = 0;
    const char* name = nullptr;
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(id)) {
        vendor = SDL_GetGamepadVendor(pad);
        product = SDL_GetGamepadProduct(pad);
        name = SDL_GetGamepadName(pad);
    } else if (SDL_Joystick* joy = SDL_GetJoystickFromID(id)) {
        vendor = SDL_GetJoystickVendor(joy);
        product = SDL_GetJoystickProduct(joy);
        name = SDL_GetJoystickName(joy);
    } else {
        vendor = SDL_GetJoystickVendorForID(id);
        product = SDL_GetJoystickProductForID(id);
        name = SDL_GetJoystickNameForID(id);
    }
    g_detected = isOriginalXbox(vendor, product, name) ? Style::OriginalXbox : Style::Xbox;
}

} // namespace

bool isOriginalXbox(Uint16 vendor, Uint16 product, const char* name)
{
    struct Id {
        Uint16 vendor, product;
    };
    //Microsoft's (the Duke, the Controller S), and the licensed ones (Mad Catz, Logitech, Nyko, Pelican...)
    static const Id ids[] = {
        {0x045e, 0x0202}, {0x045e, 0x0285}, {0x045e, 0x0287}, {0x045e, 0x0288}, {0x045e, 0x0289},
        {0x0738, 0x4506}, {0x0738, 0x4516}, {0x0738, 0x4520}, {0x0738, 0x4522}, {0x0738, 0x4526}, {0x0738, 0x4536}, {0x0738, 0x4556},
        {0x046d, 0xca84}, {0x046d, 0xca88}, {0x0e6f, 0x0003}, {0x0e6f, 0x0005}, {0x0e6f, 0x0006}, {0x0e6f, 0x0008},
        {0x0e4c, 0x1097}, {0x0e4c, 0x2390}, {0x0e4c, 0x3510}, {0x0c12, 0x8802}, {0x0c12, 0x8809}, {0x0f30, 0x0202},
        {0x0f30, 0x8888}, {0x102c, 0xff0c}, {0x0d2f, 0x0002}, {0x044f, 0x0f07}, {0x0e8f, 0x3008}, {0xffff, 0xffff},
    };
    for (const Id& id : ids) {
        if (id.vendor == vendor && id.product == product && vendor != 0xffff)
            return true;
    }
    if (!name)
        return false;
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* hint : {"original xbox", "xbox controller s", "xbox duke", "duke", "ogx", "xbox classic", "xid"}) {
        if (lower.find(hint) != std::string::npos)
            return true;
    }
    return false;
}

Style style()
{
    if (game_values.buttonprompts == 1)
        return Style::Xbox;
    if (game_values.buttonprompts == 2)
        return Style::OriginalXbox;
    return g_detected;
}

// ---- glyphs

namespace {

constexpr SDL_Color FACE[] = {{56, 184, 48, 255}, {224, 48, 40, 255}, {40, 112, 232, 255}, {248, 192, 16, 255}};
constexpr SDL_Color FACE_LIGHT[] = {{152, 240, 120, 255}, {255, 152, 136, 255}, {136, 184, 255, 255}, {255, 240, 136, 255}};
constexpr SDL_Color FACE_DARK[] = {{16, 104, 16, 255}, {136, 16, 16, 255}, {16, 48, 144, 255}, {168, 104, 0, 255}};
constexpr const char* FACE_LETTER[] = {"A", "B", "X", "Y"};

constexpr SDL_Color PAD_BODY {92, 96, 116, 255};      // the Xbox's gray buttons
constexpr SDL_Color PAD_LIGHT {164, 168, 188, 255};
constexpr SDL_Color SILVER {184, 188, 200, 255};      // the original's triggers and START / BACK
constexpr SDL_Color SILVER_LIGHT {232, 236, 244, 255};
constexpr SDL_Color SILVER_DARK {112, 116, 128, 255};

// A round button (8x8, doubled)
const std::vector<const char*> ROUND = {
    "..####..",
    ".#llbb#.",
    "#llbbbb#",
    "#lbbbbb#",
    "#bbbbbb#",
    "#bbbbdb#",
    ".#bddb#.",
    "..####..",
};

int faceButton(int x, int y, int i, bool draw)
{
    if (!draw)
        return 17;
    if (style() == Style::OriginalXbox) {
        //A colored gem, a white letter
        art(x, y, ROUND, {{'#', BLACK}, {'l', FACE_LIGHT[i]}, {'b', FACE[i]}, {'d', FACE_DARK[i]}});
        tinyText(x + 6, y + 4, FACE_LETTER[i], FACE_DARK[i]);
        tinyText(x + 5, y + 3, FACE_LETTER[i], WHITE);
    } else {
        //A flat button in its color, a white letter
        art(x, y, {
            "..####..",
            ".#bbbb#.",
            "#bbbbbb#",
            "#bbbbbb#",
            "#bbbbbb#",
            "#bbbbbb#",
            ".#dddd#.",
            "..####..",
        }, {{'#', BLACK}, {'b', FACE[i]}, {'d', FACE_DARK[i]}});
        tinyText(x + 6, y + 4, FACE_LETTER[i], FACE_DARK[i]);
        tinyText(x + 5, y + 3, FACE_LETTER[i], WHITE);
    }
    return 17;
}

// LB / RB (Xbox), White / Black (the original)
int bumper(int x, int y, bool right, bool draw)
{
    if (style() == Style::OriginalXbox) {
        if (draw) {
            const SDL_Color body = right ? SDL_Color {24, 24, 28, 255} : SDL_Color {236, 236, 240, 255};
            const SDL_Color light = right ? SDL_Color {88, 88, 100, 255} : WHITE;
            const SDL_Color dark = right ? SDL_Color {8, 8, 8, 255} : SDL_Color {168, 168, 180, 255};
            art(x, y, ROUND, {{'#', right ? SDL_Color {120, 120, 132, 255} : BLACK}, {'l', light}, {'b', body}, {'d', dark}});
        }
        return 17;
    }
    if (draw) {
        art(x, y, {
            "..##########..",
            ".#llllllllll#.",
            "#lbbbbbbbbbbb#",
            "#bbbbbbbbbbbb#",
            "#bbbbbbbbbbbb#",
            "#bbbbbbbbbbbb#",
            "#dddddddddddd#",
            "##############",
        }, {{'#', BLACK}, {'l', PAD_LIGHT}, {'b', PAD_BODY}, {'d', {60, 62, 78, 255}}});
        tinyText(x + 8, y + 4, right ? "RB" : "LB", BLACK);
        tinyText(x + 7, y + 3, right ? "RB" : "LB", WHITE);
    }
    return 29;
}

// LT / RT (Xbox), L / R (the original)
int trigger(int x, int y, bool right, bool draw)
{
    const bool original = style() == Style::OriginalXbox;
    if (draw) {
        art(x, y, {
            "..######..",
            ".#llllll#.",
            "#llbbbbbb#",
            "#lbbbbbbb#",
            "#bbbbbbbb#",
            "#bbbbbbbb#",
            "#bbbbbbbd#",
            ".########.",
        }, {{'#', BLACK}, {'l', original ? SILVER_LIGHT : PAD_LIGHT}, {'b', original ? SILVER : PAD_BODY},
            {'d', original ? SILVER_DARK : SDL_Color {60, 62, 78, 255}}});
        if (original) {
            tinyText(x + 7, y + 3, right ? "R" : "L", {24, 24, 32, 255});
        } else {
            tinyText(x + 4, y + 4, right ? "RT" : "LT", BLACK);
            tinyText(x + 3, y + 3, right ? "RT" : "LT", WHITE);
        }
    }
    return 21;
}

// Start and Back: Menu and View (Xbox), START and BACK (the original)
int startBack(int x, int y, bool start, bool draw)
{
    if (style() == Style::OriginalXbox) {
        const std::string label = start ? "START" : "BACK";
        const int w = tinyWidth(label) + 10;
        if (draw) {
            //A silver pill with its name
            fill(x + 2, y, w - 4, 16, BLACK);
            fill(x, y + 2, w, 12, BLACK);
            fill(x + 2, y + 2, w - 4, 12, SILVER);
            fill(x + 2, y + 2, w - 4, 2, SILVER_LIGHT);
            fill(x + 2, y + 12, w - 4, 2, SILVER_DARK);
            tinyText(x + 5, y + 3, label, {16, 16, 24, 255});
        }
        return w + 1;
    }
    if (draw) {
        art(x, y, {
            "...........",
            ".#########.",
            "#lllllllll#",
            "#bbbbbbbbb#",
            "#bbbbbbbbb#",
            "#bbbbbbbbb#",
            ".#########.",
            "...........",
        }, {{'#', BLACK}, {'l', PAD_LIGHT}, {'b', PAD_BODY}});
        if (start) {
            for (int k = 0; k < 3; k++)
                fill(x + 7, y + 5 + 2 * k, 8, 1, WHITE);
        } else {
            frame(x + 6, y + 4, 6, 6, WHITE, PAD_BODY);
            frame(x + 10, y + 6, 6, 6, WHITE, PAD_BODY);
        }
    }
    return 23;
}

int dpad(int x, int y, Pad pad, bool draw)
{
    if (!draw)
        return 17;
    const bool original = style() == Style::OriginalXbox;
    art(x, y, {
        "..####..",
        "..#ll#..",
        "###bb###",
        "#lbbbbb#",
        "#bbbbbd#",
        "###bd###",
        "..#dd#..",
        "..####..",
    }, {{'#', BLACK}, {'l', original ? SILVER_LIGHT : PAD_LIGHT}, {'b', original ? SILVER : SDL_Color {74, 74, 88, 255}},
        {'d', original ? SILVER_DARK : PAD_BODY}});
    const SDL_Color lit {255, 216, 40, 255};
    if (pad == Pad::Up)
        fill(x + 6, y + 2, 4, 5, lit);
    else if (pad == Pad::Down)
        fill(x + 6, y + 9, 4, 5, lit);
    else if (pad == Pad::Left)
        fill(x + 2, y + 6, 5, 4, lit);
    else if (pad == Pad::Right)
        fill(x + 9, y + 6, 5, 4, lit);
    return 17;
}

int stick(int x, int y, bool right, bool draw)
{
    if (draw) {
        art(x, y, ROUND, {{'#', BLACK}, {'l', SILVER_LIGHT}, {'b', SILVER}, {'d', SILVER_DARK}});
        art(x + 4, y + 4, {".##.", "#..#", "#..#", ".##."}, {{'#', SILVER_DARK}});
        tinyText(x + 6, y + 4, right ? "R" : "L", {24, 24, 32, 255}, 1);
    }
    return 17;
}

int padGlyph(int x, int y, Pad pad, bool draw)
{
    switch (pad) {
        case Pad::A: return faceButton(x, y, 0, draw);
        case Pad::B: return faceButton(x, y, 1, draw);
        case Pad::X: return faceButton(x, y, 2, draw);
        case Pad::Y: return faceButton(x, y, 3, draw);
        case Pad::LB: return bumper(x, y, false, draw);
        case Pad::RB: return bumper(x, y, true, draw);
        case Pad::LT: return trigger(x, y, false, draw);
        case Pad::RT: return trigger(x, y, true, draw);
        case Pad::Start: return startBack(x, y, true, draw);
        case Pad::Back: return startBack(x, y, false, draw);
        case Pad::Up:
        case Pad::Down:
        case Pad::Left:
        case Pad::Right:
        case Pad::DPad: return dpad(x, y, pad, draw);
        case Pad::LStick: return stick(x, y, false, draw);
        case Pad::RStick: return stick(x, y, true, draw);
    }
    return 0;
}

} // namespace

int drawPad(int x, int y, Pad pad)
{
    return padGlyph(x, y, pad, true);
}

int padWidth(Pad pad)
{
    return padGlyph(0, 0, pad, false);
}

// A key cap: the menus' gray pipe colors
int drawKey(int x, int y, const std::string& label)
{
    const int w = std::max(16, textWidth(label) + 8);
    fill(x + 2, y, w - 4, 16, BLACK);
    fill(x, y + 2, w, 12, BLACK);
    fill(x + 2, y + 2, w - 4, 12, {144, 160, 160, 255});
    fill(x + 2, y + 2, w - 4, 2, {176, 192, 192, 255});
    fill(x + 2, y + 12, w - 4, 2, {80, 96, 96, 255});
    text(x + (w - textWidth(label)) / 2, y + 1, label);
    return w + 1;
}

int drawMouse(int x, int y, bool left, bool right)
{
    const SDL_Color red {232, 56, 40, 255};
    art(x, y, {
        ".#######.",
        "#LLL#RRR#",
        "#LLL#RRR#",
        "#########",
        "#wwwwwww#",
        "#wwwwwww#",
        "#wwwwwww#",
        ".#######.",
    }, {{'#', BLACK}, {'L', left ? red : SILVER_LIGHT}, {'R', right ? red : SILVER_LIGHT}, {'w', SILVER_LIGHT}});
    return 19;
}

std::string padName(Pad pad)
{
    const bool original = style() == Style::OriginalXbox;
    switch (pad) {
        case Pad::LB: return original ? "White" : "LB";
        case Pad::RB: return original ? "Black" : "RB";
        case Pad::LT: return original ? "L" : "LT";
        case Pad::RT: return original ? "R" : "RT";
        default: return {};
    }
}

int drawPrompt(int x, int y, Pad pad, const std::string& key, const std::string& label)
{
    int w = 0;
    if (g_last == Device::Controller)
        w = drawPad(x, y, pad);
    else if (key.empty())
        w = drawMouse(x, y, true, false);
    else if (key == "RMB")
        w = drawMouse(x, y, false, true);
    else
        w = drawKey(x, y, key);
    if (!label.empty()) {
        text(x + w + 3, y + 1, label);
        w += 3 + textWidth(label);
    }
    return w;
}

int promptWidth(Pad pad, const std::string& key, const std::string& label)
{
    int w = 0;
    if (g_last == Device::Controller)
        w = padWidth(pad);
    else if (key.empty() || key == "RMB")
        w = 19;
    else
        w = std::max(16, textWidth(key) + 8) + 1;
    return label.empty() ? w : w + 3 + textWidth(label);
}

// ---- the menus' keys

std::string menuKey(MenuKey key)
{
    const SDL_Keycode code = game_values.inputConfiguration[0][0].inputGameControls[1].keys[static_cast<int>(key)];
    if (code == SDLK_UNKNOWN)
        return {};
    if (code >= 323 && code <= 340)    //(the mouse: MOUSE_UP and on)
        return "Mouse";
    std::string name = SDL_GetKeyName(code);
    static const std::pair<const char*, const char*> shorter[] = {{"Return", "Enter"}, {"Escape", "Esc"}, {"Backspace", "Bksp"},
        {"Left Shift", "Shift"}, {"Right Shift", "RShift"}, {"Left Ctrl", "Ctrl"}, {"Right Ctrl", "RCtrl"}, {"Left Alt", "Alt"},
        {"Right Alt", "AltGr"}, {"Keypad Enter", "Enter"}, {"PageUp", "PgUp"}, {"PageDown", "PgDn"}};
    for (const auto& [from, to] : shorter) {
        if (name == from)
            return to;
    }
    return name;
}

int promptsWidth(const std::vector<Prompt>& row, int gap)
{
    int w = 0;
    for (const Prompt& p : row) {
        int item = 0;
        if (g_last == Device::Controller) {
            for (Pad pad : p.pads)
                item += padWidth(pad) + 2;
        } else {
            for (const std::string& key : p.keys) {
                if (!key.empty())
                    item += promptWidth(Pad::A, key, "") + 2;
            }
        }
        if (!p.label.empty())
            item += 1 + textWidth(p.label);
        w += item + gap;
    }
    return std::max(0, w - gap);
}

int drawPrompts(int cx, int y, const std::vector<Prompt>& row, int gap)
{
    const int w = promptsWidth(row, gap);
    int x = cx - w / 2;
    for (const Prompt& p : row) {
        if (g_last == Device::Controller) {
            for (Pad pad : p.pads)
                x += drawPad(x, y, pad) + 2;
        } else {
            for (const std::string& key : p.keys) {
                if (!key.empty())
                    x += drawPrompt(x, y, Pad::A, key, "") + 2;
            }
        }
        if (!p.label.empty()) {
            text(x + 1, y + 1, p.label);
            x += 1 + textWidth(p.label);
        }
        x += gap;
    }
    return w;
}

// ---- the controllers

void Gamepads::open()
{
    close();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int k = 0; ids && k < count; k++) {
        if (SDL_IsGamepad(ids[k])) {
            if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[k]))
                m_gamepads.push_back(pad);
        } else if (SDL_Joystick* joy = SDL_OpenJoystick(ids[k])) {
            m_joysticks.push_back(joy);
        }
    }
    SDL_free(ids);
}

void Gamepads::close()
{
    for (SDL_Gamepad* pad : m_gamepads)
        SDL_CloseGamepad(pad);
    for (SDL_Joystick* joy : m_joysticks)
        SDL_CloseJoystick(joy);
    m_gamepads.clear();
    m_joysticks.clear();
}

std::vector<PadState> Gamepads::read() const
{
    std::vector<PadState> pads;
    for (SDL_Gamepad* pad : m_gamepads) {
        PadState p;
        const std::array<SDL_GamepadButton, BUTTON_COUNT> map = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
            SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        for (int b = 0; b < BUTTON_COUNT; b++)
            p.buttons[static_cast<size_t>(b)] = SDL_GetGamepadButton(pad, map[static_cast<size_t>(b)]);
        p.lx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        p.ly = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        p.rx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        p.ry = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
        p.lt = std::max(0.0f, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
        p.rt = std::max(0.0f, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
        pads.push_back(p);
    }
    //Without a mapping: the usual layout (buttons 0-7, the first hat, axes 0-3)
    for (SDL_Joystick* joy : m_joysticks) {
        PadState p;
        const int buttons = SDL_GetNumJoystickButtons(joy);
        for (int b = 0; b < 8 && b < buttons; b++)
            p.buttons[static_cast<size_t>(b)] = SDL_GetJoystickButton(joy, b);
        if (SDL_GetNumJoystickHats(joy) > 0) {
            const Uint8 hat = SDL_GetJoystickHat(joy, 0);
            p.buttons[UP] = hat & SDL_HAT_UP;
            p.buttons[DOWN] = hat & SDL_HAT_DOWN;
            p.buttons[LEFT] = hat & SDL_HAT_LEFT;
            p.buttons[RIGHT] = hat & SDL_HAT_RIGHT;
        }
        const int axes = SDL_GetNumJoystickAxes(joy);
        if (axes >= 2) {
            p.lx = axis(SDL_GetJoystickAxis(joy, 0));
            p.ly = axis(SDL_GetJoystickAxis(joy, 1));
        }
        if (axes >= 4) {
            p.rx = axis(SDL_GetJoystickAxis(joy, 2));
            p.ry = axis(SDL_GetJoystickAxis(joy, 3));
        }
        pads.push_back(p);
    }
    return pads;
}

PadState Gamepads::merged() const
{
    PadState m;
    auto stick = [](float& out, float v) {
        if (std::fabs(v) > std::fabs(out))
            out = v;
    };
    for (const PadState& p : read()) {
        for (size_t b = 0; b < m.buttons.size(); b++)
            m.buttons[b] = m.buttons[b] || p.buttons[b];
        stick(m.lx, p.lx);
        stick(m.ly, p.ly);
        stick(m.rx, p.rx);
        stick(m.ry, p.ry);
        m.lt = std::max(m.lt, p.lt);
        m.rt = std::max(m.rt, p.rt);
    }
    return m;
}

} // namespace prompts

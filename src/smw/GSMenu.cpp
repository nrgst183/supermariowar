#include "GSMenu.h"
#include "LoadLog.h"

#include "rollback/MatchConfig.h"
#include "rollback/NetGame.h"
#include "rollback/Replay.h"

#include "CmdArgs.h"
#include "Determinism.h"
#include "FileList.h"
#include "Game.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "ui/InputPrompts.h"
#include "ui/MenuPad.h"
#include "ui/SmwTheme.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_TextField.h"
#include "GSGameplay.h"
#include "GameplayStyles.h"
#include "joystick.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "TilesetManager.h"
#include "MatchHistory.h"
#include "Platform.h"
#include "Score.h"
#include "SimulationState.h"
#include "gamemodes/BonusHouse.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "ui/MI_BonusWheel.h"
#include "ui/MI_MapBrowser.h"
#include "ui/MI_Image.h"
#include "ui/MI_MapField.h"
#include "ui/MI_MapFilterScroll.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/MI_TournamentScoreboard.h"
#include "ui/MI_TourStop.h"
#include "ui/MI_World.h"
#include "Version.h"
#include "world.h"
#include "WorldTourStop.h"
#include "world/WorldMapIO.h"

#include "menu/BonusWheelMenu.h"
#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MapFilterEditMenu.h"
#include "menu/MatchHistoryMenu.h"
#include "menu/EditorsMenu.h"
#ifdef SMW_HAS_EDITORS
#include "editors/EditorLauncher.h"
#include "editors/CoEdit.h"
#endif
#include "ui/MI_MatchHistory.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/ModeOptionsMenu.h"
#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"
#include "menu/SystemLinkMenu.h"
#include "menu/OptionsMenu.h"
#include "menu/PlayerControlsMenu.h"
#include "menu/PlayerControlsSelectMenu.h"
#include "menu/TeamSelectMenu.h"
#include "menu/TournamentScoreboardMenu.h"
#include "menu/TourStopMenu.h"
#include "menu/WorldMenu.h"

#include "menu/options/EyeCandyOptionsMenu.h"
#include "menu/options/GameplayOptionsMenu.h"
#include "menu/options/GraphicsOptionsMenu.h"
#include "menu/options/PowerupDropRatesMenu.h"
#include "menu/options/PowerupSettingsMenu.h"
#include "menu/options/ProjectileLimitsMenu.h"
#include "menu/options/ProjectileOptionsMenu.h"
#include "menu/options/SoundOptionsMenu.h"
#include "menu/options/TeamOptionsMenu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <format>
#include <string>
#include <cstdlib> // atoi()
#include <random>
#include <cassert>
#include <sstream>
#include <unordered_set>
#include <vector>

// std::uniue_ptr requires that the special member function have access
// to the complete types.
namespace {
// What the buttons do, in the title bar of the menus (with a controller: with the keyboard and mouse
// the menus look as they always have): choose, change a value, select at the left (as much as fits
// before the title), back at the right when it goes back
void roundBox(int x, int y, int w, int h, SDL_Color c);

// Whether the menu drawn has no footer (a screen the whole of which is its picture): only then are the
// prompts in its title bar (the others have them in their footer)
bool s_hintsInTitle = false;

void drawMenuHints(const UI_Menu& menu)
{
    if (!s_hintsInTitle || prompts::lastDevice() != prompts::Device::Controller)
        return;
    using prompts::Pad;
    using prompts::Prompt;
    using prompts::menuKey;
    const MI_HeaderText* title = menu.Title();
    if (!title)
        return;
    constexpr int GAP = 10;
    constexpr int Y = 2;

    std::vector<Prompt> left;
    const Prompt change {{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"};
    if (menu.ChangingValue()) {
        left = {change, {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "done"}};
    } else {
        left = {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"}};
        left.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select"});
    }
    if (menu.Paged())
        left.push_back({{Pad::LB, Pad::RB}, {"PgUp", "PgDn"}, "page"});
    //(their labels large, read from a couch: the least needed drop out first if the bar is short)
    prompts::setLargeLabels(true);
    struct SmallAgain {
        ~SmallAgain() { prompts::setLargeLabels(false); }
    } smallAgain;
    //(clear of the title: the least needed first out)
    while (!left.empty() && 8 + prompts::promptsWidth(left, GAP) > title->left() - 20)
        left.erase(left.begin());
    //(each group on a dark pill, as the hint bar at the bottom: apart from the title bar's own)
    auto pill = [](int x, int w) {
        //(inside the title bar's rim)
        roundBox(x - 6, 4, w + 12, 24, {0, 0, 0, 255});
        roundBox(x - 4, 6, w + 8, 20, {16, 28, 84, 255});
    };
    if (!left.empty()) {
        const int w = prompts::promptsWidth(left, GAP);
        pill(8, w);
        prompts::drawPrompts(8 + w / 2, Y + 5, left, GAP);
    }

    if (menu.CancelCode() != MENU_CODE_NONE) {
        const std::vector<Prompt> right {{{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
        const int w = prompts::promptsWidth(right, GAP);
        if (App::screenWidth - 8 - w > title->right() + 20) {
            pill(App::screenWidth - 8 - w, w);
            prompts::drawPrompts(App::screenWidth - 8 - w / 2, Y + 5, right, GAP);
        }
    }
}

// Rounded boxes in pixels (radius 3: a row's ends come in 3, 1, 1 pixels near the top and bottom)
int roundInset(int row, int h)
{
    static constexpr int IN[3] = {3, 1, 1};
    if (row < 3)
        return IN[row];
    if (row >= h - 3)
        return IN[h - 1 - row];
    return 0;
}

// A filled rounded box
void roundBox(int x, int y, int w, int h, SDL_Color c)
{
    for (int row = 0; row < h; row++) {
        const int in = roundInset(row, h);
        prompts::fill(x + in, y + row, w - 2 * in, 1, c);
    }
}

// A rounded ring `t` pixels thick (only the ring: what's inside shows through)
void roundRing(int x, int y, int w, int h, int t, SDL_Color c)
{
    for (int row = 0; row < h; row++) {
        const int outer = roundInset(row, h);
        if (row < t || row >= h - t) {
            prompts::fill(x + outer, y + row, w - 2 * outer, 1, c);
            continue;
        }
        const int inner = t + roundInset(row - t, h - 2 * t);
        prompts::fill(x + outer, y + row, inner - outer, 1, c);
        prompts::fill(x + w - inner, y + row, inner - outer, 1, c);
    }
}

SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
    auto m = [t](Uint8 p, Uint8 q) { return static_cast<Uint8>(static_cast<float>(p) + (static_cast<float>(q) - static_cast<float>(p)) * t); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

// The pixels around a shape (`mask`, w x h): how far each one is from it, 1 to `width` (0: in it,
// or farther); the steps alternate 8 and 4 neighbors, so that the corners come out round
std::vector<Uint8> ringAround(const std::vector<Uint8>& mask, int w, int h, int width)
{
    std::vector<Uint8> dist(mask.size(), 0);
    std::vector<Uint8> grown = mask;
    for (Uint8 step = 1; step <= width; step++) {
        const bool diagonal = (step & 1) != 0;
        std::vector<Uint8> next = grown;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (grown[static_cast<size_t>(y) * w + x])
                    continue;
                bool near = false;
                for (int dy = -1; dy <= 1 && !near; dy++) {
                    for (int dx = -1; dx <= 1 && !near; dx++) {
                        if ((dx == 0 && dy == 0) || (!diagonal && dx != 0 && dy != 0))
                            continue;
                        const int nx = x + dx, ny = y + dy;
                        near = nx >= 0 && ny >= 0 && nx < w && ny < h && grown[static_cast<size_t>(ny) * w + nx];
                    }
                }
                if (near) {
                    next[static_cast<size_t>(y) * w + x] = 1;
                    dist[static_cast<size_t>(y) * w + x] = step;
                }
            }
        }
        grown = std::move(next);
    }
    return dist;
}

// While a value is being changed (after A), on the value's part of the control (ValueBox: between
// its arrows), the row's own frame staying as it is: the value is pressed in, recessed into the
// pipe: darker, a shadow along its top and left inside edges, light along the bottom and right
// (the pixels there made darker or lighter, so that it works on any field's color); the arrows
// are drawn again over it all, at its sides. The darkened part: the control drawn alone on a
// scratch surface (kept while the same control is changed: each frame only blits it and shades
// the edges).
void drawChangingFocus(const UI_Control& control, const UI_Control::Box& v, float wave)
{
    SDL_Surface* const screen = blitdest;
    if (!screen || v.w <= 8 || v.h <= 8)
        return;
    const int bpp = SDL_BYTESPERPIXEL(screen->format);
    if (bpp != 2 && bpp != 4)
        return;
    static SDL_Surface* scratch = nullptr;
    if (!scratch || scratch->w != screen->w || scratch->h != screen->h || scratch->format != screen->format) {
        if (scratch)
            SDL_DestroySurface(scratch);
        scratch = SDL_CreateSurface(screen->w, screen->h, screen->format);
        if (!scratch)
            return;
    }
    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(screen->format);
    const Uint32 key = SDL_MapRGB(format, nullptr, 255, 0, 255);    //(nothing draws the see-through magenta)

    auto pixel = [bpp](SDL_Surface* s, int x, int y) -> Uint8* {
        return static_cast<Uint8*>(s->pixels) + static_cast<ptrdiff_t>(y) * s->pitch + x * bpp;
    };
    auto read = [&](SDL_Surface* s, int x, int y) -> Uint32 {
        const Uint8* p = pixel(s, x, y);
        return bpp == 4 ? *reinterpret_cast<const Uint32*>(p) : *reinterpret_cast<const Uint16*>(p);
    };
    auto write = [&](SDL_Surface* s, int x, int y, Uint32 value) {
        Uint8* p = pixel(s, x, y);
        if (bpp == 4)
            *reinterpret_cast<Uint32*>(p) = value;
        else
            *reinterpret_cast<Uint16*>(p) = static_cast<Uint16>(value);
    };

    constexpr int EDGE = 3;    // the shaded edges' width
    const std::array<MI_Image*, 2> arrows = control.ModifyArrows();

    struct FramePixel {
        short x, y;
        signed char shade;    // -100..100: darker (shadow) or lighter (light), in percent
    };
    //The value part's light and frame: made again for another control (or if it moved)
    struct Cache {
        const UI_Control* control = nullptr;
        UI_Control::Box box;
        SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
        SDL_Rect r {};
        SDL_Surface* shadow = nullptr;    // black where the control is, in the value part; see-through elsewhere
        Uint32 drawn = 0;                 // a sum of what the screen showed there (its text changes: made again)
        std::vector<FramePixel> frame;
    };
    static Cache cache;
    static Uint64 lastMs = 0;
    const Uint64 nowMs = SDL_GetTicks();
    const bool stale = nowMs - lastMs > 250;    //(after a pause: another screen, maybe another control at that address)
    lastMs = nowMs;
    const int x0 = std::max(0, static_cast<int>(v.x)), y0 = std::max(0, static_cast<int>(v.y));
    const SDL_Rect area {x0, y0, std::min(screen->w, v.x + v.w) - x0, std::min(screen->h, v.y + v.h) - y0};
    if (area.w <= 8 || area.h <= 8)
        return;
    //(what the control drew there this frame, but its arrows, which move: another value, the text
    //scrolling, is another shape to keep out of the shadow)
    Uint32 drawn = 2166136261u;
    {
        std::array<SDL_Rect, 2> skip {};
        for (size_t i = 0; i < arrows.size(); i++) {
            if (arrows[i]) {
                short ax, ay, aw, ah;
                arrows[i]->GetPositionAndSize(ax, ay, aw, ah);
                skip[i] = {ax, ay, aw, ah};
            }
        }
        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);
        for (int y = area.y; y < area.y + area.h; y++) {
            for (int x = area.x; x < area.x + area.w; x++) {
                bool arrow = false;
                for (const SDL_Rect& s : skip)
                    arrow = arrow || (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h);
                if (!arrow)
                    drawn = (drawn ^ read(screen, x, y)) * 16777619u;
            }
        }
        SDL_UnlockSurface(screen);
    }
    if (stale || cache.control != &control || cache.box.x != v.x || cache.box.y != v.y || cache.box.w != v.w || cache.box.h != v.h
        || cache.format != screen->format || !cache.shadow || cache.drawn != drawn) {
        cache.control = &control;
        cache.box = v;
        cache.format = screen->format;
        cache.drawn = drawn;
        cache.r = area;
        const SDL_Rect& r = cache.r;

        //The control without its arrows, only its value part
        for (MI_Image* arrow : arrows)
            if (arrow)
                arrow->setVisible(false);
        //(twice: as it is, then without its text, which stays out of the shadow: it keeps its colors)
        auto drawAlone = [&](bool text) {
            SDL_FillSurfaceRect(scratch, &r, key);
            SDL_SetSurfaceClipRect(scratch, &r);
            SDL_Surface* const saved = blitdest;
            blitdest = scratch;
            gfxFont::s_hidden = !text;
            const_cast<UI_Control&>(control).Draw();
            gfxFont::s_hidden = false;
            blitdest = saved;
            SDL_SetSurfaceClipRect(scratch, nullptr);
            std::vector<Uint32> pixels(static_cast<size_t>(r.w) * r.h);
            if (SDL_MUSTLOCK(scratch))
                SDL_LockSurface(scratch);
            for (int y = 0; y < r.h; y++)
                for (int x = 0; x < r.w; x++)
                    pixels[static_cast<size_t>(y) * r.w + x] = read(scratch, r.x + x, r.y + y);
            SDL_UnlockSurface(scratch);
            return pixels;
        };
        const std::vector<Uint32> withText = drawAlone(true);
        const std::vector<Uint32> noText = drawAlone(false);
        for (MI_Image* arrow : arrows)
            if (arrow)
                arrow->setVisible(true);

        if (cache.shadow)
            SDL_DestroySurface(cache.shadow);
        cache.shadow = SDL_CreateSurface(r.w, r.h, screen->format);
        if (!cache.shadow)
            return;
        //(only the pipe goes into the shadow: the pixels of the fields' pipe art's colors; what a
        //field draws on it, a slider's track and marks, keeps its colors as the text does)
        static std::unordered_set<Uint32> pipeColors;
        static SDL_PixelFormat pipeFormat = SDL_PIXELFORMAT_UNKNOWN;
        if (pipeFormat != screen->format) {
            pipeFormat = screen->format;
            pipeColors.clear();
            for (const gfxSprite* art : {&rm->spr_selectfield, &rm->spr_selectfielddisabled}) {
                SDL_Surface* surface = art->getSurface();
                if (!surface)
                    continue;
                if (SDL_MUSTLOCK(surface))
                    SDL_LockSurface(surface);
                for (int y = 0; y < surface->h; y++) {
                    for (int x = 0; x < surface->w; x++) {
                        const RGB c = getRgb(surface, x, y);
                        if (!(c.r == 255 && c.g == 0 && c.b == 255))
                            pipeColors.insert(SDL_MapRGB(format, nullptr, c.r, c.g, c.b));
                    }
                }
                SDL_UnlockSurface(surface);
            }
        }
        const Uint32 black = SDL_MapRGB(format, nullptr, 0, 0, 0);
        if (SDL_MUSTLOCK(cache.shadow))
            SDL_LockSurface(cache.shadow);
        for (int y = 0; y < r.h; y++) {
            for (int x = 0; x < r.w; x++) {
                const size_t i = static_cast<size_t>(y) * r.w + x;
                const bool pipe = noText[i] != key && noText[i] == withText[i] && pipeColors.count(noText[i]) != 0;
                write(cache.shadow, x, y, pipe ? black : key);
            }
        }
        SDL_UnlockSurface(cache.shadow);
        SDL_SetSurfaceColorKey(cache.shadow, true, key);
        SDL_SetSurfaceBlendMode(cache.shadow, SDL_BLENDMODE_BLEND);

        //The edges: EDGE pixels just inside the value part, its corners round; shadow on the sides
        //nearest the top and the left, light on the others, strongest at the very edge
        std::vector<Uint8> inner(static_cast<size_t>(r.w) * r.h, 0);
        for (int y = EDGE + 1; y < r.h - EDGE - 1; y++) {
            const int in = roundInset(y - EDGE - 1, r.h - 2 * EDGE - 2);
            for (int x = EDGE + 1 + in; x < r.w - EDGE - 1 - in; x++)
                inner[static_cast<size_t>(y) * r.w + x] = 1;
        }
        const std::vector<Uint8> dist = ringAround(inner, r.w, r.h, EDGE);
        static constexpr signed char SHADOW[EDGE + 1] = {0, -42, -60, -74};
        static constexpr signed char LIGHT[EDGE + 1] = {0, 14, 34, 56};
        cache.frame.clear();
        for (int y = 0; y < r.h; y++) {
            for (int x = 0; x < r.w; x++) {
                const Uint8 d = dist[static_cast<size_t>(y) * r.w + x];
                if (!d)
                    continue;
                const int top = y, bottom = r.h - 1 - y, left = x, right = r.w - 1 - x;
                const bool shaded = std::min(top, left) <= std::min(bottom, right);
                const signed char shade = shaded ? SHADOW[d] : LIGHT[d];
                if (shade != 0)
                    cache.frame.push_back({static_cast<short>(r.x + x), static_cast<short>(r.y + y), shade});
            }
        }
    }

    (void)wave;
    //The value, pressed in: darker
    SDL_SetSurfaceAlphaMod(cache.shadow, 40);
    SDL_Rect to = cache.r;
    SDL_BlitSurface(cache.shadow, nullptr, screen, &to);

    //Its edges: shadow at the top and left, light at the bottom and right
    if (SDL_MUSTLOCK(screen))
        SDL_LockSurface(screen);
    for (const FramePixel& p : cache.frame) {
        Uint8 cr, cg, cb;
        SDL_GetRGB(read(screen, p.x, p.y), format, nullptr, &cr, &cg, &cb);
        const SDL_Color to = p.shade < 0 ? SDL_Color {0, 0, 0, 255} : SDL_Color {255, 255, 255, 255};
        const SDL_Color c = mix({cr, cg, cb, 255}, to, static_cast<float>(p.shade < 0 ? -p.shade : p.shade) / 100.0f);
        write(screen, p.x, p.y, SDL_MapRGB(format, nullptr, c.r, c.g, c.b));
    }
    SDL_UnlockSurface(screen);

    //The arrows over it all, at its sides
    for (MI_Image* arrow : arrows)
        if (arrow)
            arrow->Draw();
}

// The frame around the focused control: a light band between two dark edges (so that it shows on
// the green and gray buttons and on any backdrop), its corners round like the buttons' ends. It
// breathes slowly from white to a pale silver blue; with a controller, a silver pointer bobs at its
// left, at the item the buttons work on. Gold is only for a value being changed (after A: see
// drawChangingFocus), so that the two never look alike.
void drawMenuFocus(const UI_Control& control, bool modifying)
{
    const UI_Control::Box b = control.FocusBox();
    if (b.w <= 0)
        return;
    const Uint64 ms = SDL_GetTicks();
    const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(ms % 1400) * (6.2831853f / 1400.0f));
    const SDL_Color dark {16, 24, 48, 255};
    const SDL_Color silver {208, 216, 232, 255};
    const SDL_Color light = mix({252, 252, 252, 255}, {176, 204, 240, 255}, wave * 0.6f);

    //(2 pixels each, dark, light, dark: no 1-pixel line to flicker on an interlaced TV; 2 more at the
    //sides: a lit pipe's ends reach 8 past its box, the frame goes round them)
    constexpr int LIP = 2;
    roundRing(b.x - 6 - LIP, b.y - 6, b.w + 12 + 2 * LIP, b.h + 12, 6, dark);
    roundRing(b.x - 4 - LIP, b.y - 4, b.w + 8 + 2 * LIP, b.h + 8, 2, light);
    //(its value being changed: that part of it, in gold)
    if (modifying)
        drawChangingFocus(control, control.ValueBox(), wave);

    if (prompts::lastDevice() != prompts::Device::Controller)
        return;
    //(the pointer: a silver arrowhead, a few pixels to and fro)
    const int bob = static_cast<int>(wave * 3.0f + 0.5f);
    const int px = b.x - 6 - LIP - 2 - 12 - bob;
    const int py = b.y + b.h / 2 - 9;
    if (px < 2)
        return;
    //(lit from above: a white edge on top, a darker steel underneath)
    prompts::art(px, py, {
        "dd    ",
        "dhd   ",
        "dhgd  ",
        "dhggd ",
        "dhgggd",
        "dggod ",
        "dgod  ",
        "dod   ",
        "dd    ",
    }, {{'d', dark}, {'g', silver}, {'h', {255, 255, 255, 255}}, {'o', {120, 136, 168, 255}}}, 2);
}
} // namespace

MenuState::MenuState() = default;
MenuState::~MenuState() = default;

MenuState& MenuState::instance()
{
    static MenuState menu;
    return menu;
}

bool MenuState::init()
{
    UI_Menu::drawHints = drawMenuHints;
    UI_Menu::drawTitleBar = [] { theme::statusBar(0, 0, 640, 32); };
    UI_Menu::drawFocus = drawMenuFocus;
    //(text fields: the on-screen keyboard with a controller; on the Xbox, always)
    MI_TextField::useOnScreenKeyboard = [] {
#ifdef SMW_XBOX
        return true;
#else
        return prompts::lastDevice() == prompts::Device::Controller;
#endif
    };
    loadlog::mark("menus: before");
    mMainMenu = std::make_unique<UI_MainMenu>();
#ifdef SMW_HAS_EDITORS
    //(a lobby's editing session talks over the lobby)
    editors::coedit::installLobbyHandler();
#endif

    // Options menu
    mOptionsMenu = std::make_unique<UI_OptionsMenu>();
    mGameplayOptionsMenu = std::make_unique<UI_GameplayOptionsMenu>();
    mTeamOptionsMenu = std::make_unique<UI_TeamOptionsMenu>();
    mPowerupDropRatesMenu = std::make_unique<UI_PowerupDropRatesMenu>();
    mPowerupSettingsMenu = std::make_unique<UI_PowerupSettingsMenu>();
    mProjectileLimitsMenu = std::make_unique<UI_ProjectileLimitsMenu>();
    mProjectileOptionsMenu = std::make_unique<UI_ProjectileOptionsMenu>();
    mGraphicsOptionsMenu = std::make_unique<UI_GraphicsOptionsMenu>();
    mEyeCandyOptionsMenu = std::make_unique<UI_EyeCandyOptionsMenu>();
    mSoundOptionsMenu = std::make_unique<UI_SoundOptionsMenu>();
    loadlog::mark("menus: main and options");

    // Controls menu
    mPlayerControlsSelectMenu = std::make_unique<UI_PlayerControlsSelectMenu>();
    mPlayerControlsMenu = std::make_unique<UI_PlayerControlsMenu>();

    // Gameplay menu
    UI_ModeOptionsMenu::addPanel = [](UI_Menu& menu, const std::vector<const UI_Control*>& rules) {
        AddPanelBehind(menu, rules);
    };
    mModeOptionsMenu = std::make_unique<UI_ModeOptionsMenu>();
    mMatchSelectionMenu = std::make_unique<UI_MatchSelectionMenu>();
    loadlog::mark("menus: controls, mode options, match selection");
    mGameSettingsMenu = std::make_unique<UI_GameSettingsMenu>();
    loadlog::mark("menus: game settings (its map preview)");
    mMapFilterEditMenu = std::make_unique<UI_MapFilterEditMenu>();
    mTourStopMenu = std::make_unique<UI_TourStopMenu>();
    mWorldMenu = std::make_unique<UI_WorldMenu>();
    mTeamSelectMenu = std::make_unique<UI_TeamSelectMenu>();
    mTournamentScoreboardMenu = std::make_unique<UI_TournamentScoreboardMenu>();
    mBonusWheelMenu = std::make_unique<UI_BonusWheelMenu>();
    loadlog::mark("menus: map filters, tour, world, teams, scoreboard, bonus wheel");

    // Online menu
    mOnlineMenu = std::make_unique<UI_OnlineMenu>();
    mOnlineLobbyMenu = std::make_unique<UI_OnlineLobbyMenu>();
    mSystemLinkMenu = std::make_unique<UI_SystemLinkMenu>();
    mMatchHistoryMenu = std::make_unique<UI_MatchHistoryMenu>();
    mEditorsMenu = std::make_unique<UI_EditorsMenu>();

    mCurrentMenu = mMainMenu.get();
    szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
    loadlog::mark("menus: online, history, editors");

    return true;
}

//---------------------------------------------------------------
// RUN THE MENU
//---------------------------------------------------------------

void MenuState::onEnterState()
{
    platform::setCrashContext("The menus");
    //(low memory: the matches' sprites go; the few the menus draw are loaded again when drawn)
    if (platform::lowMemory()) {
        mapPreviewWaitForLoads();
        gfxSprite::unloadGroup(SpriteGroup::Game);
    }
    iUnlockMinigameOptionIndex = 0;

    iTournamentAIStep = 0;
    iTournamentAITimer = 0;

    //Reset the keys each time we switch from menu to game and back
    game_values.playerInput.ResetKeys();
    menupad::resync();

    fNeedMenuMusicReset = false;

    //Back from a replay: the match history again (the replay changed nothing:
    //its match's result doesn't go to a tournament, tour or world)
    if (replay::takeReturnedFromReplay()) {
        mMatchHistoryMenu->miHistory->Refresh();
        mCurrentMenu = mMatchHistoryMenu.get();
        if (game_values.music) {
            if (game_values.matchtype == MatchType::World)
                rm->backgroundmusic[5].play(false, false);
            else if (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament)
                rm->backgroundmusic[3].play(false, false);
            else
                rm->backgroundmusic[2].play(false, false);
        }
        return;
    }

#ifdef SMW_HAS_EDITORS
    //Back from playing the map being edited: the editor again (its match changed nothing)
    if (!mTestPlay.map.empty()) {
        EndTestPlay();
        return;
    }
#endif

    rollback::NetGame& onlineNet = rollback::NetGame::instance();
    if (onlineNet.active() && onlineNet.lobby() && !cmd::g_net.enabled() && !onlineNet.seriesActive()) {
        //Back from an online match: the lobby again (a tournament, tour or world goes on below)
        mOnlineSetup = false;
        mOnlineView = false;
        mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Off);
        mCurrentMenu = mOnlineLobbyMenu.get();
        if (onlineNet.isHost())
            ApplyOnlineMatchSelection();
        mOnlineLobbyMenu->Open();
        mOnlineLobbyMenu->OpenVote();
    } else if (game_values.gamemode->winningteam > -1 && game_values.tournamentwinner == -1 &&
            (((game_values.matchtype == MatchType::SingleGame || game_values.matchtype == MatchType::QuickGame || game_values.matchtype == MatchType::MiniGame || game_values.matchtype == MatchType::Tournament) && game_values.bonuswheel == BonusWheelStyle::EveryGame) || (game_values.matchtype == MatchType::Tour && game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType))) {
        mBonusWheelMenu->miBonusWheel->Reset(false);
        mCurrentMenu = mBonusWheelMenu.get();
    } else if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame) {
        mCurrentMenu = mTournamentScoreboardMenu.get();
    } else if (game_values.matchtype == MatchType::QuickGame) {
        //Reset back to main menu after quick game
        mCurrentMenu = mMainMenu.get();
        mCurrentMenu->ResetMenu();
    }

    if (game_values.matchtype == MatchType::World) {
        if (game_values.gamemode->winningteam > -1 && game_values.tournamentwinner == -1) { //Stage is completed
            mWorldMenu->miWorld->SetControllingTeam(game_values.gamemode->winningteam);
            mWorldMenu->miWorld->SetCurrentStageToCompleted(game_values.gamemode->winningteam);

            mWorldMenu->CloseStageStart();

            //Clear out the stored powerups after a game that had a winner
            if (!game_values.worldskipscoreboard) {
                //Only clear out the stored powerup if we were allowed to use it in the game
                if (game_values.gamemode->HasStoredPowerups()) {
                    for (short iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++)
                        game_values.storedpowerups[iPlayer] = -1;
                }
            }
        }

        UpdateScoreBoard();

        //If we're suposed to skip the scoreboard, then reset back to the world map
        if (game_values.worldskipscoreboard) {
            mCurrentMenu = mWorldMenu.get();
            mCurrentMenu->ResetMenu();

            game_values.worldskipscoreboard = false;
        } else {
            mTournamentScoreboardMenu->miTournamentScoreboard->RefreshWorldScores(game_values.gamemode->winningteam);
        }
    } else if (game_values.matchtype == MatchType::Tour) {
        mTournamentScoreboardMenu->miTournamentScoreboard->RefreshTourScores();

        if (game_values.tourstopcurrent < game_values.tourstops.size())
            mTourStopMenu->miTourStop->Refresh(game_values.tourstopcurrent);
    } else if (game_values.matchtype == MatchType::Tournament) {
        mTournamentScoreboardMenu->miTournamentScoreboard->StopSwirl();
        if (game_values.gamemode->winningteam > -1) {
            mTournamentScoreboardMenu->miTournamentScoreboard->RefreshTournamentScores(game_values.gamemode->winningteam);

            //Set the next controlling team
            switch (game_values.tournamentcontrolstyle) {
            case TournamentControlStyle::GameWinner: { //Winning Team
                game_values.tournamentcontrolteam = game_values.gamemode->winningteam;
                break;
            }

            case TournamentControlStyle::GameLoser: { //Losing Team
                short iNumInPlace = 0;
                short iInPlace[4];
                short iLowestPlace = 0;
                for (short iScore = 0; iScore < g_sim.score_cnt; iScore++) {
                    if (g_sim.score[iScore].place == iLowestPlace) {
                        iInPlace[iNumInPlace++] = iScore;
                    } else if (g_sim.score[iScore].place > iLowestPlace) {
                        iNumInPlace = 1;
                        iInPlace[0] = iScore;
                        iLowestPlace = g_sim.score[iScore].place;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::LeadingTeams: { //Tournament Ahead Teams
                short iNumInPlace = 0;
                short iInPlace[4];
                short iMostWins = 0;
                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (game_values.tournament_scores[iTeam].wins == iMostWins) {
                        iInPlace[iNumInPlace++] = iTeam;
                    } else if (game_values.tournament_scores[iTeam].wins > iMostWins) {
                        iNumInPlace = 1;
                        iInPlace[0] = iTeam;
                        iMostWins = game_values.tournament_scores[iTeam].wins;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::TrailingTeams: { //Tournament Behind Teams
                short iNumInPlace = 0;
                short iInPlace[4] = {0,0,0,0};
                short iLeastWins = 20; //Most possible wins are 10

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (game_values.tournament_scores[iTeam].wins == iLeastWins) {
                        iInPlace[iNumInPlace++] = iTeam;
                    } else if (game_values.tournament_scores[iTeam].wins < iLeastWins) {
                        iNumInPlace = 1;
                        iInPlace[0] = iTeam;
                        iLeastWins = game_values.tournament_scores[iTeam].wins;
                    }
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::Random: { //Random
                game_values.tournamentcontrolteam = RANDOM_INT( g_sim.score_cnt);
                break;
            }

            case TournamentControlStyle::RandomLoser: { //Random Losing Team
                short iNumInPlace = 0;
                short iInPlace[4] = {0, 0, 0, 0};
                short iWinner = 0;

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (g_sim.score[iTeam].place == 0) {
                        iWinner = iTeam;
                        break;
                    }
                }

                for (short iTeam = 0; iTeam < g_sim.score_cnt; iTeam++) {
                    if (iTeam == iWinner)
                        continue;

                    iInPlace[iNumInPlace++] = iTeam;
                }

                game_values.tournamentcontrolteam = iInPlace[RANDOM_INT(iNumInPlace)];
                break;
            }

            case TournamentControlStyle::RoundRobin: { //Round Robin
                game_values.tournamentcontrolteam = game_values.tournamentnextcontrol;

                if (++game_values.tournamentnextcontrol >= g_sim.score_cnt)
                    game_values.tournamentnextcontrol = 0;

                break;
            }

            default: {
                game_values.tournamentcontrolteam = -1;
                break;
            }
            }
        }
    }

    //Reset game mode back to the current game mode in case we came from boss mode
    game_values.gamemode = gamemodes[currentgamemode];

    //Keep track if we entered the menu loop as part of a tournament, if we exit the tournament
    //we need to reset the menu music back to normal
    if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame)
        fNeedMenuMusicReset = true;

    if (game_values.music) {
        if (game_values.tournamentwinner < 0) {
            if (game_values.matchtype == MatchType::SingleGame || game_values.matchtype == MatchType::QuickGame || game_values.matchtype == MatchType::MiniGame)
                rm->backgroundmusic[2].play(false, false);
            else if (game_values.matchtype == MatchType::World)
                rm->backgroundmusic[5].play(false, false);
            else
                rm->backgroundmusic[3].play(false, false);
        }
    }

    if (game_values.matchtype != MatchType::World && game_values.matchtype != MatchType::QuickGame) {
        if (mCurrentMenu == mGameSettingsMenu.get() || mCurrentMenu == mTournamentScoreboardMenu.get())
            mGameSettingsMenu->miMapField->LoadCurrentMap();
    }

    OnlineSeriesAfterMatch();
}

void MenuState::update()
{
#ifdef SMW_HAS_EDITORS
    //(back from playing the map: the editor opens on it again)
    if (mTestPlay.reopenEditor) {
        mTestPlay.reopenEditor = false;
        RunEditor(editors::Editor::Level);
        return;
    }
#endif

    //Reset the keys that were down the last frame
    game_values.playerInput.ClearPressedKeys(1);

    if (!PollMenuEvents())
        return;
    //(every controller, whether or not it's a player's: see MenuPad.h)
    menupad::update(game_values.playerInput);

    //If AI is controlling the tournament menu, select the options
    if (game_values.matchtype == MatchType::Tournament && iTournamentAITimer > 0 && mCurrentMenu == mGameSettingsMenu.get() && mGameSettingsMenu->IsOnStartBtn()) {
        if (--iTournamentAITimer == 0) {
            iTournamentAIStep++;

            if (iTournamentAIStep == 1) {
                mGameSettingsMenu->miMapField->ChooseRandomMap();

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 2) {
                currentgamemode = RANDOM_INT(game_mode_mapmaker);    //(not the Map Maker: it never ends)
                game_values.gamemode = gamemodes[currentgamemode];
                mGameSettingsMenu->GameModeChanged(currentgamemode);
                game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 3) {
                SModeOption * options = game_values.gamemode->GetOptions();

                //Choose a goal from the lower values for a quicker game
                short iRandOption = (RANDOM_INT(6)) + 1;
                game_values.gamemode->goal  = options[iRandOption].iValue;

                mGameSettingsMenu->miGoalField[currentgamemode]->setCurrentValue(gamemodes[currentgamemode]->goal);

                mModeOptionsMenu->SetRandomGameModeSettings(game_values.gamemode->gamemode);

                iTournamentAITimer = 60;
            } else if (iTournamentAIStep == 4) {
                iTournamentAIStep = 0;
                StartGame();
            }
        }
    }

    //Watch for the konami code to unlock the minigames match type

#ifdef _DEBUG
    if (g_fAutoTest)
        GetNextScriptOperation();

    if (g_fRecordTest)
        SetNextScriptOperation();
#endif

    bool fGenerateMapThumbs = false;
    HandleOnlineSeries();
    if (AppState::Menu == game_values.appstate) {
        if (!cmd::g_net.enabled())
            HandleOnlineEvents();
        if (mCurrentMenu == mOnlineLobbyMenu.get()) {
            mOnlineLobbyMenu->Sync();
            UpdateOnlineVote();
        }
        SyncOnlineSettings();

        //--replay: plays it once, at startup
        static bool replayFromArgsStarted = false;
        if (!cmd::g_replay.empty() && !replayFromArgsStarted) {
            replayFromArgsStarted = true;
            std::string error;
            if (replay::startPlayback(cmd::g_replay, error))
                return;
            printf("replay: can't play %s: %s\n", cmd::g_replay.c_str(), error.c_str());
            if (cmd::g_autotest.enabled) {
                Exit();
                return;
            }
        }

#ifdef SMW_HAS_EDITORS
        //A lobby's host asked us into its editor (as it started, or as we arrived)
        editors::Editor invited;
        if (editors::coedit::takeLobbyInvite(invited) && InEditorLobby() && editors::coedit::state() == editors::coedit::State::Off)
            JoinLobbyEditing();

        //Joining an editing session: the editor opens once the host answers
        if (editors::coedit::state() != editors::coedit::State::Off) {
            editors::coedit::pump();
            const editors::coedit::State coeditState = editors::coedit::state();
            if (coeditState == editors::coedit::State::Joined) {
                const bool lobbySession = editors::coedit::overLobby();
                if (!RunEditor(editors::coedit::editor()))
                    return;
                if (lobbySession)
                    BackToLobbyFromEditor();
            } else if (coeditState == editors::coedit::State::Ended && editors::coedit::overLobby()) {
                //(the host ended it before we were in)
                mOnlineLobbyMenu->SetNotice(editors::coedit::statusText());
                editors::coedit::leave("");
            } else if (coeditState == editors::coedit::State::Ended) {
                mEditorsMenu->SetStatus(editors::coedit::statusText());
                editors::coedit::leave("");
            } else if (coeditState == editors::coedit::State::Connecting) {
                mEditorsMenu->SetStatus(editors::coedit::statusText());
            }
        }
#endif

        MenuCodeEnum code = MENU_CODE_NONE;
        //(online, the world map runs only on the frames its lockstep has a tick for: OnlineSeriesWorldTick)
        mSeriesSkipUpdate = rollback::NetGame::instance().seriesActive();
        if (cmd::g_autotest.enabled)
            code = AutoTestMenuUpdate();
        //Online tournaments, tours and worlds: the host leads the menus (MenuOnlineSeries.cpp)
        //(LB and RB: the options screens' pages)
        if (MENU_CODE_NONE == code && menupad::shoulder() != 0)
            code = OptionsPageCode(menupad::shoulder());
        if (MENU_CODE_NONE == code && !OnlineSeriesInput(code)) {
            code = mCurrentMenu->SendInput(&game_values.playerInput);
            //(nothing from the buttons: what the mouse did)
            if (MENU_CODE_NONE == code)
                code = ApplyPointer();
        }
        //(the editors menu's own buttons: what they lead to)
        if (MENU_CODE_EDITORS_ACTION == code)
            code = mEditorsMenu->Act();

        if (!HandleMenuCode(code, fGenerateMapThumbs))
            return;
    }

    //--------------- draw everything ----------------------

    //(a frame behind runs ticks to catch up: only its last draws, the others only update)
    const bool draw = drawsThisTick();

    //Don't draw backdrop for world
    if (!draw)
        ;
    else if (mCurrentMenu != mWorldMenu.get())
        rm->menu_backdrop.draw(0, 0);
    else
        SDL_FillSurfaceRect(screen, nullptr, SDL_MapSurfaceRGB(screen, 0, 0, 0));

    //(online, the world map only runs with the ticks of its lockstep)
    const bool worldWaits = mCurrentMenu == mWorldMenu.get() && rollback::NetGame::instance().seriesActive() &&
        (mSeriesSkipUpdate || game_values.appstate != AppState::Menu);
    if (!worldWaits)
        mCurrentMenu->Update();
    menupanel::beginFrame();
    //(every menu has the footer of prompts: the ones that don't draw their own, the usual one; not
    //the screens the whole of which is their picture: the world map, the map thumbnails, the
    //tournament's scoreboard and bonus wheel, a tour's stop)
    const bool wholeScreen = mCurrentMenu == mWorldMenu.get() || mCurrentMenu == mMapFilterEditMenu.get() ||
        mCurrentMenu == mTournamentScoreboardMenu.get() || mCurrentMenu == mBonusWheelMenu.get() || mCurrentMenu == mTourStopMenu.get();
    s_hintsInTitle = wholeScreen;
    if (draw) {
        mCurrentMenu->Draw();
        if (!menupanel::footerDrawn() && !wholeScreen) {
            if (mCurrentMenu == mMainMenu.get())
                menupanel::footer(menupanel::standardPrompts(false, nullptr), mMainMenu->focusHint());
            else
                menupanel::footer(menupanel::standardPrompts(mCurrentMenu->ChangingValue(), "back"));
        }
        DrawOnlineSeriesNotice();
        DrawDeterminismReport();
    }
    //(the determinism test's result, on the Xbox: a button quits)
    if (determinism::finished()) {
        for (const COutputControl& keys : game_values.playerInput.outputControls)
            if (keys.menu_select.fPressed || keys.menu_cancel.fPressed)
                Exit();
    }

    if (iDisplayError > DISPLAY_ERROR_NONE) {
        if (draw) {
            rm->spr_selectfield.draw(70, 400, {0, 0, 484, 32});
            rm->spr_selectfield.draw(554, 400, {496, 0, 16, 32});

            if (iDisplayError == DISPLAY_ERROR_READ_TOUR_FILE)
                rm->menu_font_large.drawCentered(320, 405, "Error Reading Tour File!");
            if (iDisplayError == DISPLAY_ERROR_READ_WORLD_FILE)
                rm->menu_font_large.drawCentered(320, 405, "Error Reading World File!");
            else if (iDisplayError == DISPLAY_ERROR_MAP_FILTER)
                rm->menu_font_large.drawCentered(320, 405, "No Maps Meet All Filter Conditions!");
        }

        if (--iDisplayErrorTimer == 0)
            iDisplayError = DISPLAY_ERROR_NONE;
    }

    if (game_values.screenfadespeed != 0) {
        game_values.screenfade += game_values.screenfadespeed;

        if (game_values.screenfade <= 0) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 0;
        } else if (game_values.screenfade >= 255) {
            game_values.screenfadespeed = 0;
            game_values.screenfade = 255;
        }
    }

    if (draw && game_values.screenfade > 0) {
        rm->drawShade(static_cast<Uint8>(game_values.screenfade));
    }

    if (draw && cmd::g_autotest.enabled && cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots)
        AutoTestAfterDraw();

    if (game_values.screenfade == 255) {
        rollback::NetGame& net = rollback::NetGame::instance();
        if (AppState::StartGame == game_values.appstate && net.active() && net.seriesActive() && !net.isHost()) {
            //A stage of the world map: the host's config starts it
            return;
        }
        if (AppState::StartGame == game_values.appstate && net.active()) {
            //Online: the host sends the match it set up to the peers; the game
            //switches to it by itself once they accept (see rollback/NetLobby.h)
            std::string error;
            if (!net.startMatch(error)) {
                printf("netplay: can't start this match online: %s\n", error.c_str());
                mOnlineLobbyMenu->SetNotice("Can't start the match: " + error);
                if (net.seriesActive()) {
                    net.endSeries("the match couldn't start: " + error);
                    OnlineSeriesEnded("Can't start the match: " + error);
                }
            }
            game_values.appstate = AppState::Menu;
            game_values.screenfadespeed = -8;
            return;
        }

        if (AppState::StartGame == game_values.appstate) {
            LaunchMatch();
            return;
        } else if (AppState::StartWorld == game_values.appstate) { //Fade to world match type
            game_values.screenfadespeed = -8;

            mCurrentMenu = mWorldMenu.get();
            mCurrentMenu->ResetMenu();

            rm->backgroundmusic[2].stop();
            rm->backgroundmusic[5] = sfxMusic(worldmusiclist->currentMusic(g_worldmap.GetMusicCategory(), g_worldmap.GetWorldName()));
            rm->backgroundmusic[5].play(false, false);
            fNeedMenuMusicReset = true;

            mWorldMenu->miWorld->DisplayTeamControlAnnouncement();

            game_values.appstate = AppState::Menu;
        }
    }

    if (fGenerateMapThumbs) {
        rm->menu_dialog.draw(160, 176, {0, 0, 160, 64});
        rm->menu_dialog.draw(App::screenWidth/2, 176, {352, 0, 160, 64});
        rm->menu_dialog.draw(160, 240, {0, 416, 160, 64});
        rm->menu_dialog.draw(App::screenWidth/2, App::screenHeight/2, {352, 416, 160, 64});
        rm->menu_font_large.drawCentered(App::screenWidth/2, 215, "Refreshing Map Thumbnails");
        rm->menu_font_large.drawCentered(App::screenWidth/2, 245, "Please Wait...");
    }

#ifdef _DEBUG
    if (g_fAutoTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Auto");

    if (g_fRecordTest)
        rm->menu_font_small.drawRightJustified(635, 5, "Recording...");
#endif

    if (fGenerateMapThumbs) {
        fGenerateMapThumbs = false;
        rm->backgroundmusic[2].togglePause();

        //Reload map auto filters from live map files (don't use the cache)
        maplist->ReloadMapAutoFilters();

        //Write out all the map thumbnails for the map browser and filter editor
        std::map<std::string, MapListNode>::iterator itr = maplist->GetIteratorAt(0, false);

        short iMapCount = maplist->count();
        for (short iMap = 0; iMap < iMapCount; iMap++) {
            g_map->loadMap((*itr).second.filename, read_type_preview);
            g_map->saveThumbnail(mapThumbnailPath(itr->second.filename), false);

            itr++;
        }

        rm->backgroundmusic[2].togglePause();
    }
}

// The window's and the keyboard's events (the input devices get them too). False: the game quits.
bool MenuState::PollMenuEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        gfx_eventtoscreen(&event);    //(the mouse in the screen's pixels at any window size)
        prompts::note(event);         //(the button prompts: a controller's buttons or the keys)
        switch (event.type) {
        case SDL_EVENT_QUIT:
            Exit();
            return false;

        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            JoystickManager::instance().handleEvent(event);
            menupad::devicesChanged();
            break;

        //The mouse: the pointer shows while it's used (and hides with a key or a button)
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ShowCursor();
            mPointer.moved = true;
            mPointer.x = static_cast<short>(event.motion.x);
            mPointer.y = static_cast<short>(event.motion.y);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            SDL_ShowCursor();
            if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                mPointer.click = event.button.button == SDL_BUTTON_LEFT ? 1 : 2;
                mPointer.clickX = static_cast<short>(event.button.x);
                mPointer.clickY = static_cast<short>(event.button.y);
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            mPointer.wheel += event.wheel.y > 0 ? 1 : event.wheel.y < 0 ? -1 : 0;
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            SDL_HideCursor();
            break;

        case SDL_EVENT_KEY_DOWN: {
            SDL_HideCursor();
            menupad::noteKey(event.key.key);
            if (event.key.key == SDLK_F1) {
                game_values.showfps = !game_values.showfps;
            }

#ifdef _DEBUG
            if (event.key.key == SDLK_F2) {
                game_values.frameadvance = !game_values.frameadvance;
            }
#endif
            if (event.key.mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
                //ALT + F4 = close window
                if (event.key.key == SDLK_F4) {
                    Exit();
                    return false;
                } //ALT + Enter = fullscreen/windowed toggle
                else if (event.key.key == SDLK_RETURN) {
                    game_values.fullscreen = !game_values.fullscreen;
                    gfx_changefullscreen(game_values.fullscreen);
                    blitdest = screen;

                    continue;
                }
            }

            if (event.key.key == SDLK_INSERT || event.key.key == SDLK_F12) {
                gfx_take_screenshot();
            }
#ifdef _DEBUG
            //Pressing insert in debug mode turns on automated testing
            if (event.key.key == SDLK_F8) {
                g_fAutoTest = !g_fAutoTest;

                if (g_fAutoTest) {
                    mCurrentMenu = mMainMenu.get();
                    mCurrentMenu->ResetMenu();

                    LoadScript("Scripts/StartMenuAutomation.txt");
                }
            }

            if (event.key.key == SDLK_HOME) {
                g_fRecordTest = !g_fRecordTest;

                if (g_fRecordTest) {
                    StartRecordScript();
                } else {
                    SaveScript("Scripts/SavedScript.txt");
                }
            }

            if (event.key.key == SDLK_END) {
                AddEmtpyLineToScript();
            }

#endif
            break;
        }

        default:
            break;
        }

        game_values.playerInput.Update(event, 1);
    }
    return true;
}

// What the mouse did since the last frame, done to the current menu: the code to handle
MenuCodeEnum MenuState::ApplyPointer()
{
    const PendingPointer pointer = mPointer;
    mPointer = {};
    MenuCodeEnum code = MENU_CODE_NONE;
    if (pointer.moved)
        code = mCurrentMenu->PointerMove(pointer.x, pointer.y);
    //(a click on the footer's "back": as B, as a right click)
    if (pointer.click == 1 && menupanel::footerBackAt(pointer.clickX, pointer.clickY))
        code = mCurrentMenu->PointerClick(pointer.clickX, pointer.clickY, true);
    else if (pointer.click)
        code = mCurrentMenu->PointerClick(pointer.clickX, pointer.clickY, pointer.click == 2);
    else if (pointer.wheel)
        code = mCurrentMenu->PointerWheel(pointer.wheel > 0 ? 1 : -1);
    return code;
}

MenuCodeEnum MenuState::OptionsPageCode(int dir) const
{
    struct Page {
        const UI_Menu* menu;
        MenuCodeEnum code;
    };
    //(in the order of the Options menu's buttons)
    const Page pages[] = {{mGameplayOptionsMenu.get(), MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU},
        {mTeamOptionsMenu.get(), MENU_CODE_TO_TEAM_OPTIONS_MENU},
        {mPowerupDropRatesMenu.get(), MENU_CODE_TO_POWERUP_SELECTION_MENU},
        {mPowerupSettingsMenu.get(), MENU_CODE_TO_POWERUP_SETTINGS_MENU},
        {mProjectileOptionsMenu.get(), MENU_CODE_TO_PROJECTILES_OPTIONS_MENU},
        {mProjectileLimitsMenu.get(), MENU_CODE_TO_PROJECTILES_LIMITS_MENU},
        {mGraphicsOptionsMenu.get(), MENU_CODE_TO_GRAPHICS_OPTIONS_MENU},
        {mEyeCandyOptionsMenu.get(), MENU_CODE_TO_EYECANDY_OPTIONS_MENU},
        {mSoundOptionsMenu.get(), MENU_CODE_TO_SOUND_OPTIONS_MENU}};
    constexpr int COUNT = static_cast<int>(sizeof(pages) / sizeof(pages[0]));
    for (int k = 0; k < COUNT; k++) {
        if (pages[k].menu == mCurrentMenu)
            return pages[(k + dir + COUNT) % COUNT].code;
    }
    return MENU_CODE_NONE;
}

// The screen has faded out to start a match: its settings, its map and music, then the gameplay
// The match's pictures of the map's layers (the back ones, the front ones, the animated tiles'
// second copies) and its animated tiles: a match's start, and the editor in the match after an edit
void BuildMatchMapLayers()
{
    g_map->predrawbackground(rm->spr_background, rm->spr_backmap[0]);
    g_map->predrawforeground(rm->spr_frontmap[0]);

    //(the second copy, the animated tiles' other buffer: the same pixels, copied, not drawn again;
    //set up as the drawing sets it: the back layer opaque, the front one blended)
    if (rm->useSecondMapLayers(g_map->hasAnimatedTiles())) {
        if (gfx_copypixels(rm->spr_backmap[0].getSurface(), rm->spr_backmap[1].getSurface()))
            SDL_SetSurfaceBlendMode(rm->spr_backmap[1].getSurface(), SDL_BLENDMODE_NONE);
        else
            g_map->predrawbackground(rm->spr_background, rm->spr_backmap[1]);
        if (gfx_copypixels(rm->spr_frontmap[0].getSurface(), rm->spr_frontmap[1].getSurface())) {
            SDL_SetSurfaceColorKey(rm->spr_frontmap[1].getSurface(), false, 0);
            SDL_SetSurfaceBlendMode(rm->spr_frontmap[1].getSurface(), SDL_BLENDMODE_BLEND);
        } else {
            g_map->predrawforeground(rm->spr_frontmap[1]);
        }
    }
    g_map->fitDrawAreas(rm->spr_frontmap[0]);    //(only where it shows)
    g_map->SetupAnimatedTiles();
}

void MenuState::LaunchMatch()
{
    loadlog::mark("match: the fade out ended");
    g_autoTestLoadStartNs = SDL_GetTicksNS();
    SDL_HideCursor();    //(the menus' pointer: not in the match)
#ifdef SMW_HAS_EDITORS
    editors::freeKeptGraphics();    //(an editor's graphics, kept in the menus: the match's memory)
#endif
    //(64 MB on the Xbox: the menus' memory goes before the match's map, music and sprites load)
    rm->freeMenuGraphicsForMatch(game_values.matchtype == MatchType::World);
    if (game_values.matchtype != MatchType::World)
        mWorldMenu->miWorld->releaseSurfaces();
    if (game_values.matchtype == MatchType::QuickGame)
        mModeOptionsMenu->SetRandomGameModeSettings(game_values.gamemode->gamemode);
    else
        SetGameModeSettingsFromMenu();

    if (game_values.matchtype == MatchType::World && game_values.tourstops[game_values.tourstopcurrent]->iStageType == 1) {
        g_map->loadMap(convertPath("maps/special/two52_special_bonushouse.map"), read_type_full);
        LoadCurrentMapBackground();

        if (game_values.music) {
            rm->backgroundmusic[0] = sfxMusic(worldmusiclist->currentMusic(WorldMusicCategory::Bonus, ""));
            rm->backgroundmusic[0].play(false, false);
        }
    } else {
        std::string sShortMapName = "";

        bool fMiniGameMapFound = false;

        if (game_values.matchtype == MatchType::World) {
            if (game_values.gamemode->gamemode == game_mode_pipe_minigame ||
                    game_values.gamemode->gamemode == game_mode_boss_minigame ||
                    game_values.gamemode->gamemode == game_mode_boxes_minigame) {
                fMiniGameMapFound = maplist->findexact(game_values.tourstops[game_values.tourstopcurrent]->pszMapFile.c_str(), true);

                if (fMiniGameMapFound) {
                    g_map->loadMap(maplist->currentFilename(), read_type_full);
                    sShortMapName = maplist->currentShortmapname();
                }
            }
        }

        if (game_values.gamemode->gamemode == game_mode_pipe_minigame) {
            if (!fMiniGameMapFound) {
                g_map->loadMap(convertPath("maps/special/two52_special_pipe_minigame.map"), read_type_full);
                sShortMapName = "minigamepipe";
            }
        } else if (game_values.gamemode->gamemode == game_mode_boss_minigame) {
            if (!fMiniGameMapFound) {
                Boss bossType = game_values.gamemodesettings.boss.bosstype;
                bossgamemode->SetBossType(bossType);
                switch (bossType) {
                    case Boss::Hammer:
                        g_map->loadMap(convertPath("maps/special/two52_special_hammerboss_minigame.map"), read_type_full);
                        break;
                    case Boss::Bomb:
                        g_map->loadMap(convertPath("maps/special/two52_special_bombboss_minigame.map"), read_type_full);
                        break;
                    case Boss::Fire:
                        g_map->loadMap(convertPath("maps/special/two52_special_fireboss_minigame.map"), read_type_full);
                        break;
                }
                sShortMapName = "minigameboss";
            }
        } else if (game_values.gamemode->gamemode == game_mode_boxes_minigame) {
            if (!fMiniGameMapFound) {
                g_map->loadMap(convertPath("maps/special/two52_special_boxes_minigame.map"), read_type_full);
                sShortMapName = "minigameboxes";
            }
        } else if (game_values.matchtype == MatchType::QuickGame) {
            //Load a random map for the quick game
            std::string szMapName = maplist->randomFilename();
            g_map->loadMap(szMapName, read_type_full);
            sShortMapName = stripPathAndExtension(szMapName);

            printf("  State: GS_START_GAME, Match type: MatchType::QuickGame\n");
#ifdef SMW_HAS_EDITORS
        } else if (!mTestPlay.map.empty()) {
            //The map the editor closed to play (its scratch file)
            g_map->loadMap(mTestPlay.map, read_type_full);
            sShortMapName = stripPathAndExtension(mTestPlay.map);
            printf("testplay: playing %08x (tiles %08x types %08x blocks %08x)\n", g_map->tilesHash(), g_map->tilesHash(1), g_map->tilesHash(2), g_map->tilesHash(3));
#endif
        } else {
            g_map->loadMap(maplist->currentFilename(), read_type_full);
            sShortMapName = maplist->currentShortmapname();
            matchedit::prepareMap();
        }

        loadlog::mark("match: map");
        LoadCurrentMapBackground();
        loadlog::mark("match: background");

        //Allows all players to start the game
        game_values.singleplayermode = -1;

        if (game_values.music) {
            musiclist->setRandomMusic(static_cast<MusicCategory>(g_map->musicCategoryID), sShortMapName.c_str(), g_map->szBackgroundFile);
            rm->backgroundmusic[0] = sfxMusic(musiclist->currentMusic());
            rm->backgroundmusic[0].play(game_values.playnextmusic, false);
        }
        loadlog::mark("match: music");
    }

    game_values.appstate = AppState::Game;
    printf("  GS_GAME\n");

    //The replay of this match (match history): from here on, the
    //match plays from the seed that the replay keeps
    replay::beginLocal(g_map->loadedFile());
    loadlog::mark("match: replay started");

    BuildMatchMapLayers();
    loadlog::mark("match: the map's layers drawn");
    LoadMapObjects(false);
    loadlog::mark("match: animated tiles, map objects");

    GameStateManager::instance().changeStateTo(&GameplayState::instance());
    loadlog::mark("match: gameplay state entered");
}

// What a menu code the current menu (or the autotest, or the online series) returned does.
// Returns false when update() should stop here (a match, a replay or an editor starts, or the game quits).
bool MenuState::HandleMenuCode(MenuCodeEnum code, bool& fGenerateMapThumbs)
{
    if (MENU_CODE_EXIT_APPLICATION == code) {
        Exit();
        return false;
    } else if (MENU_CODE_MAIN_MENU_BACK == code) {
        //B on the main menu: to its Exit; there, B again quits
        if (!mMainMenu->focusExit()) {
            Exit();
            return false;
        }
    } else if (MENU_CODE_TO_LOCAL_LOBBY == code) {
        //The main menu's Start: the lobby (of a local game, until others are let in)
        mLocalSetup.valid = false;
        OpenLocalLobby();
    } else if (MENU_CODE_TO_MAIN_MENU == code) {
        if (mOnlineSetup) {
            ReturnToOnlineLobby();
        } else if (mLocalFromLobby && mCurrentMenu == mMatchSelectionMenu.get()) {
            //(a local game's match type: back to its lobby, as it was)
            OpenLocalLobby();
        } else {
            mGameSettingsMenu->SetMapMakerSession(false);
            mLocalFromLobby = false;
            iDisplayError = DISPLAY_ERROR_NONE;
            iDisplayErrorTimer = 0;
            mSystemLinkMenu->Close();    //(the Xbox's System Link menu goes back here: its search stops)
            mCurrentMenu = mMainMenu.get();
        }
    } else if (MENU_CODE_TO_HISTORY_MENU == code) {
        mMatchHistoryMenu->miHistory->Refresh();
        mCurrentMenu = mMatchHistoryMenu.get();
    } else if (MENU_CODE_HISTORY_EXIT == code) {
        mCurrentMenu = mMainMenu.get();
    } else if (MENU_CODE_HISTORY_PLAY_REPLAY == code) {
        std::string error;
        if (!replay::startPlayback(mMatchHistoryMenu->miHistory->selectedReplay(), error)) {
            mMatchHistoryMenu->miHistory->setMessage("Can't play it: " + error);
        } else {
            rm->backgroundmusic[2].stop();
            return false;
        }
    } else if (MENU_CODE_TO_EDITORS_MENU == code) {
        mGameSettingsMenu->SetMapMakerSession(false);
        mEditorsMenu->Open();
        mCurrentMenu = mEditorsMenu.get();
    } else if (MENU_CODE_EDITORS_EXIT == code) {
        mCurrentMenu = mMainMenu.get();
    } else if (MENU_CODE_RUN_LEVEL_EDITOR == code || MENU_CODE_RUN_WORLD_EDITOR == code || MENU_CODE_HOST_LEVEL_EDITOR == code ||
            MENU_CODE_HOST_WORLD_EDITOR == code) {
#ifdef SMW_HAS_EDITORS
        const bool level = MENU_CODE_RUN_LEVEL_EDITOR == code || MENU_CODE_HOST_LEVEL_EDITOR == code;
        const editors::Editor editor = level ? editors::Editor::Level : editors::Editor::World;
        if (MENU_CODE_HOST_LEVEL_EDITOR == code || MENU_CODE_HOST_WORLD_EDITOR == code) {
            //Editing together: a lobby, as a match's (the others find it in Join a Game); its Start opens
            //the editor for everyone
            OpenEditorLobby(editor == editors::Editor::World);
            return true;
        }
        if (!RunEditor(editor))
            return false;
#endif
    } else if (MENU_CODE_MAPMAKER_LOCAL == code) {
        //Editors > Map Maker, here: a local game's lobby (its players, CPUs and skins), then its map and
        //rules; Back from there, the Editors
        mGameSettingsMenu->SetMapMakerSession(true);
        mLocalSetup.valid = false;
        OpenLocalLobby(LobbyOrigin::Editors);
    } else if (MENU_CODE_MAPMAKER_HOST == code) {
        //(online: a match's lobby, found in Join a Game)
        mGameSettingsMenu->SetMapMakerSession(true);
        std::string error;
        if (!OpenLobby(rollback::LobbyAccess::Lan, LobbyOrigin::Editors, error)) {
            mGameSettingsMenu->SetMapMakerSession(false);
            mEditorsMenu->SetStatus("Can't host: " + error);
        }
    } else if (MENU_CODE_JOIN_EDITOR == code) {
        //(a lobby that edits together is joined as any: from the games list)
        mSystemLinkMenu->Open();
        mCurrentMenu = mSystemLinkMenu.get();
    } else if (MENU_CODE_TO_ONLINE_MENU == code) {
        //"Join by address", from the games list
        mSystemLinkMenu->Close();
        mOnlineMenu->ClearMessage();
        mOnlineMenu->Open(UI_OnlineMenu::Mode::Join);
        mCurrentMenu = mOnlineMenu.get();
    } else if (MENU_CODE_TO_ONLINE_OPTIONS_MENU == code) {
        mOnlineMenu->ClearMessage();
        mOnlineMenu->Open(UI_OnlineMenu::Mode::Settings);
        mCurrentMenu = mOnlineMenu.get();
    } else if (MENU_CODE_ONLINE_HOST == code) {
        HostOnlineGame();
    } else if (MENU_CODE_ONLINE_JOIN == code) {
        JoinOnlineGame();
    } else if (MENU_CODE_TO_SYSTEM_LINK_MENU == code) {
        mSystemLinkMenu->Open();
        mCurrentMenu = mSystemLinkMenu.get();
    } else if (MENU_CODE_SYSTEM_LINK_JOIN == code) {
        JoinSystemLinkGame();
    } else if (MENU_CODE_SYSTEM_LINK_HOST == code) {
        HostSystemLinkGame();
    } else if (MENU_CODE_ONLINE_LOBBY_CHANGED == code) {
        mOnlineLobbyMenu->ApplyChanges();
    } else if (MENU_CODE_ONLINE_OPTIONS == code) {
        mOnlineLobbyMenu->ShowPage(UI_OnlineLobbyMenu::Page::Options);
    } else if (MENU_CODE_ONLINE_OPTIONS_BACK == code) {
        mOnlineLobbyMenu->ShowPage(UI_OnlineLobbyMenu::Page::Team);
    } else if (MENU_CODE_ONLINE_KICK == code) {
        mOnlineLobbyMenu->KickSelected();
    } else if (MENU_CODE_ONLINE_JOIN_MATCH == code) {
        rollback::NetGame::instance().joinMatchInProgress();
    } else if (MENU_CODE_ONLINE_VIEW_SETTINGS == code && InEditorLobby()) {
        JoinLobbyEditing();
    } else if (MENU_CODE_ONLINE_MATCH_SETTINGS == code && InEditorLobby()) {
        StartLobbyEditing();
        if (game_values.appstate == AppState::Quit)
            return false;
    } else if (MENU_CODE_ONLINE_VIEW_SETTINGS == code) {
        OpenOnlineSettingsView();
    } else if (MENU_CODE_ONLINE_SETTINGS_BACK == code) {
        CloseOnlineSettingsView();
    } else if (MENU_CODE_ONLINE_MATCH_SETTINGS == code) {
        OpenOnlineMatchSettings();
    } else if (MENU_CODE_ONLINE_LEAVE == code) {
        const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
        const bool alone = rollback::NetGame::instance().access() == rollback::LobbyAccess::Nobody && !rollback::NetGame::instance().hasOthers();
        if (!lobby || alone || lobby->phase() == rollback::NetLobby::Phase::Joining || lobby->phase() == rollback::NetLobby::Phase::Closed)
            LeaveOnlineLobbyNow();
        else
            mOnlineLobbyMenu->OpenDialog(UI_OnlineLobbyMenu::Dialog::Leave);
    } else if (MENU_CODE_ONLINE_LEAVE_YES == code) {
        rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
        if (mOnlineLobbyMenu->dialog() == UI_OnlineLobbyMenu::Dialog::Kick) {
            const int peer = mOnlineLobbyMenu->kickPeer();
            mOnlineLobbyMenu->CloseDialog();
            if (lobby && peer > 0)
                lobby->kick(peer);
        } else {
            mOnlineLobbyMenu->CloseDialog();
            LeaveOnlineLobbyNow();
        }
    } else if (MENU_CODE_ONLINE_LEAVE_NO == code) {
        mOnlineLobbyMenu->CloseDialog();
    } else if (MENU_CODE_ONLINE_VOTE_REMATCH == code || MENU_CODE_ONLINE_VOTE_NEXT_MAP == code || MENU_CODE_ONLINE_VOTE_CLOSE == code) {
        if (rollback::NetLobby* lobby = rollback::NetGame::instance().lobby())
            lobby->setVote(MENU_CODE_ONLINE_VOTE_REMATCH == code ? rollback::NetLobby::VOTE_REMATCH
                : MENU_CODE_ONLINE_VOTE_NEXT_MAP == code    ? rollback::NetLobby::VOTE_NEXT_MAP
                                                            : rollback::NetLobby::VOTE_NONE);
        if (MENU_CODE_ONLINE_VOTE_CLOSE == code)
            mOnlineLobbyMenu->CloseDialog();
    } else if (MENU_CODE_BACK_TO_MATCH_SELECTION_MENU == code) {
        mCurrentMenu = mMatchSelectionMenu.get();
        iUnlockMinigameOptionIndex = 0;
    } else if (MENU_CODE_TO_MATCH_SELECTION_MENU == code) {
        mMatchSelectionMenu->WorldMapChanged();

        mCurrentMenu = mMatchSelectionMenu.get();
        mCurrentMenu->ResetMenu();
        iUnlockMinigameOptionIndex = 0;
    } else if (mOnlineSetup && MENU_CODE_MATCH_SELECTION_START == code) {
        //The host chose the match type for the lobby
        const MatchType selected = mMatchSelectionMenu->GetSelectedMatchType();
        mOnlineMinigame = selected == MatchType::MiniGame;
        mOnlineSeriesType = rollback::isSeriesMatchType(selected) ? selected : MatchType::SingleGame;
        if (mOnlineMinigame || mOnlineSeriesType != MatchType::SingleGame)
            StartOnlineFromSettings();
        else
            OpenOnlineGameSettings();
    } else if (MENU_CODE_MATCH_SELECTION_START == code || MENU_CODE_QUICK_GAME_START == code) {
    
        if (MENU_CODE_QUICK_GAME_START == code)
            game_values.matchtype = MatchType::QuickGame;
        else
            game_values.matchtype = mMatchSelectionMenu->GetSelectedMatchType();
        if (MENU_CODE_QUICK_GAME_START == code)
            mLocalFromLobby = false;

        if (game_values.matchtype != MatchType::Tournament) {
            game_values.tournamentcontrolteam = -1;
            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
        }

        if (MENU_CODE_QUICK_GAME_START == code) {
            //The main menu's Go!: a random game right away, with the main menu's players on the
            //teams and characters they last had (the team select's, not shown)
            mTeamSelectMenu->ResetTeamSelect();
            EnterGameSetup();
        } else if (mLocalFromLobby) {
            //A local game from the lobby: its players and teams are set, straight to the game setup
            EnterGameSetup();
        } else {
            mTeamSelectMenu->ResetTeamSelect();
            mCurrentMenu = mTeamSelectMenu.get();
            mCurrentMenu->ResetMenu();
        }
    } else if (MENU_CODE_MATCH_SELECTION_MATCH_CHANGED == code) {
        mMatchSelectionMenu->SelectionChanged();
    } else if (MENU_CODE_WORLD_MAP_CHANGED == code) {
        mMatchSelectionMenu->WorldMapChanged();
    } else if (MENU_CODE_TO_OPTIONS_MENU == code) {
        mCurrentMenu = mOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_BACK_TO_OPTIONS_MENU == code) {
        mCurrentMenu = mOptionsMenu.get();
    } else if (MENU_CODE_BACK_TO_GRAPHIC_OPTIONS_MENU == code) {
        mCurrentMenu = mGraphicsOptionsMenu.get();
    } else if (MENU_CODE_TO_CONTROLS_MENU == code) {
        mCurrentMenu = mPlayerControlsSelectMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_BACK_TO_CONTROLS_MENU == code) {
        mCurrentMenu = mPlayerControlsSelectMenu.get();
    } else if (MENU_CODE_TO_PLAYER_1_CONTROLS == code) {
        mPlayerControlsMenu->SetPlayer(0);
        mCurrentMenu = mPlayerControlsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_PLAYER_2_CONTROLS == code) {
        mPlayerControlsMenu->SetPlayer(1);
        mCurrentMenu = mPlayerControlsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_PLAYER_3_CONTROLS == code) {
        mPlayerControlsMenu->SetPlayer(2);
        mCurrentMenu = mPlayerControlsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_PLAYER_4_CONTROLS == code) {
        mPlayerControlsMenu->SetPlayer(3);
        mCurrentMenu = mPlayerControlsMenu.get();
        mCurrentMenu->ResetMenu();
    }
    else if (MENU_CODE_TOGGLE_FULLSCREEN == code) {
            gfx_changefullscreen(game_values.fullscreen);
        blitdest = screen;
    }
    else if (MENU_CODE_TO_GAME_SETUP_MENU == code && rollback::isSeriesMatchType(game_values.matchtype) && rollback::NetGame::instance().active() &&
            rollback::NetGame::instance().isHost()) {
        //Online (--autotest): the host starts a tournament, tour or world for everybody
        StartOnlineSeries();
    }
    else if (MENU_CODE_TO_GAME_SETUP_MENU == code) {
        EnterGameSetup();
    } else if (MENU_CODE_BACK_TO_GAME_SETUP_MENU == code) {
        bool fNeedTeamAnnouncement = false;
        if (game_values.matchtype == MatchType::World) {
            if (game_values.tournamentwinner == -2 || (game_values.tournamentwinner >= 0 && game_values.bonuswheel == BonusWheelStyle::Off)) {
                ResetTournamentBackToMainMenu();
            } else if (game_values.tournamentwinner >= 0) {
                mBonusWheelMenu->miBonusWheel->Reset(true);
                mCurrentMenu = mBonusWheelMenu.get();
            } else {
                mCurrentMenu = mWorldMenu.get();
                mWorldMenu->miWorldStop->Refresh(game_values.tourstopcurrent);

                fNeedTeamAnnouncement = true;
            }
        } else {
            if (game_values.tournamentwinner == -2) { //Tied Tour Result
                ResetTournamentBackToMainMenu();
            } else if (game_values.tournamentwinner >= 0) { //Tournament/Tour Won and Bonus Wheel will be spun
                if (game_values.bonuswheel == BonusWheelStyle::Off || (game_values.matchtype == MatchType::Tour && !game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType)) {
                    ResetTournamentBackToMainMenu();
                } else {
                    mBonusWheelMenu->miBonusWheel->Reset(true);
                    mCurrentMenu = mBonusWheelMenu.get();
                }
            } else { //Next Tour/Tourament Game
                if (game_values.matchtype == MatchType::Tour) {
                    mCurrentMenu = mTourStopMenu.get();
                } else {
                    mCurrentMenu = mGameSettingsMenu.get();
                }
            }
        }

        mCurrentMenu->ResetMenu();

        //Set the controlling team for tournament mode
        if (MatchType::Tournament == game_values.matchtype && game_values.tournamentwinner == -1)
            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, true);

        if (fNeedTeamAnnouncement)
            mWorldMenu->miWorld->DisplayTeamControlAnnouncement();
    } else if (MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS == code) {
        mCurrentMenu = mGameSettingsMenu.get();
    } else if (MENU_CODE_MODE_CHANGED == code) {
        game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
        mGameSettingsMenu->RefreshGameModeButtons();
    } else if (MENU_CODE_BACK_TEAM_SELECT_MENU == code) {
        if (mOnlineSetup) {
            ReturnToOnlineLobby();
        } else if (game_values.matchtype == MatchType::World) {
            mWorldMenu->OpenExitDialog();
        } else if (game_values.matchtype == MatchType::Tour) {
            mTourStopMenu->OpenExitDialog();
        } else if (game_values.matchtype == MatchType::Tournament) {
            mGameSettingsMenu->OpenExitDialog();

            if (iTournamentAITimer > 0)
                SetControllingTeamForSettingsMenu(-1, false);
        } else if (mLocalFromLobby && mGameSettingsMenu->MapMakerSession()) {
            //(the Map Maker's: back to its lobby, as it was)
            OpenLocalLobby(LobbyOrigin::Editors);
        } else if (mLocalFromLobby) {
            //(a local game from the lobby has no team select: its teams are the lobby's)
            mCurrentMenu = mMatchSelectionMenu.get();
        } else {
            mCurrentMenu = mTeamSelectMenu.get();
            mCurrentMenu->ResetMenu();
        }
    } else if (MENU_CODE_WINDOW_SIZE_CHANGED == code) {
        gfx_setwindowsize(game_values.windowsize);
    } else if (MENU_CODE_CURSOR_STYLE_CHANGED == code) {
        gfx_setcursorstyle(game_values.cursorstyle);
    } else if (MENU_CODE_SCREEN_SIZE_CHANGED == code) {
        gfx_setscreenscale(game_values.screensize == 2 ? 0.90f : game_values.screensize == 1 ? 0.95f : 1.0f);
    } else if (MENU_CODE_SOUND_VOLUME_CHANGED == code) {
        game_values.sound = game_values.soundvolume > 0;
        sfx_setsoundvolume(game_values.soundvolume);
        ifSoundOnPlay(rm->sfx_coin);
    } else if (MENU_CODE_MUSIC_VOLUME_CHANGED == code) {
        sfx_setmusicvolume(game_values.musicvolume);

        if (game_values.musicvolume == 0)
            rm->backgroundmusic[2].stop();
        else if (game_values.musicvolume > 0 && !game_values.music)
            rm->backgroundmusic[2].play(false, false);

        game_values.music = game_values.musicvolume > 0;
    } else if (MENU_CODE_START_GAME == code) {
        if (mOnlineSetup)
            StartOnlineFromSettings();
        else
            StartGame();
    } else if (MENU_CODE_EXIT_TOURNAMENT_YES == code || MENU_CODE_EXIT_TOURNAMENT_NO == code) {
        mGameSettingsMenu->CloseExitDialog();

        if (MENU_CODE_EXIT_TOURNAMENT_YES == code)
            ResetTournamentBackToMainMenu();
        else
            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
    } else if (MENU_CODE_EXIT_TOUR_YES == code || MENU_CODE_EXIT_TOUR_NO == code) {
        mTourStopMenu->CloseExitDialog();

        if (MENU_CODE_EXIT_TOUR_YES == code)
            ResetTournamentBackToMainMenu();
    } else if (MENU_CODE_EXIT_WORLD_YES == code || MENU_CODE_EXIT_WORLD_NO == code) {
        mWorldMenu->CloseExitDialog();

        if (MENU_CODE_EXIT_WORLD_YES == code) {
            //Clear out any stored items a player might have
            for (short iPlayer = 0; iPlayer < 4; iPlayer++)
                game_values.storedpowerups[iPlayer] = -1;

            ResetTournamentBackToMainMenu();
            mWorldMenu->miWorldStop->setVisible(false);
        }
    } else if (MENU_CODE_BONUS_DONE == code) {
        if (mBonusWheelMenu->miBonusWheel->GetPowerupSelectionDone()) {
            if (game_values.matchtype == MatchType::World) {
                if (game_values.tournamentwinner == -1)
                    mCurrentMenu = mTournamentScoreboardMenu.get();
                else
                    ResetTournamentBackToMainMenu();
            } else {
                if ((game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament) && game_values.tournamentwinner == -1 &&
                        ((game_values.matchtype != MatchType::Tour && game_values.bonuswheel == BonusWheelStyle::EveryGame) || (game_values.matchtype == MatchType::Tour && game_values.tourstops[game_values.tourstopcurrent - 1]->iBonusType))) {
                    mCurrentMenu = mTournamentScoreboardMenu.get();
                } else if (game_values.matchtype == MatchType::SingleGame) {
                    mCurrentMenu = mGameSettingsMenu.get();
                    mCurrentMenu->ResetMenu();
                    mGameSettingsMenu->miMapField->LoadCurrentMap();
                } else {
                    ResetTournamentBackToMainMenu();
                }
            }
        }
    } else if (MENU_CODE_TO_POWERUP_SELECTION_MENU == code) {
        mCurrentMenu = mPowerupDropRatesMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_POWERUP_SETTINGS_MENU == code) {
        mCurrentMenu = mPowerupSettingsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_GRAPHICS_OPTIONS_MENU == code) {
        mCurrentMenu = mGraphicsOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_EYECANDY_OPTIONS_MENU == code) {
        mCurrentMenu = mEyeCandyOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_SOUND_OPTIONS_MENU == code) {
        mCurrentMenu = mSoundOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU == code) {
        mCurrentMenu = mGameplayOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_TEAM_OPTIONS_MENU == code) {
        mCurrentMenu = mTeamOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_PROJECTILES_OPTIONS_MENU == code) {
        mCurrentMenu = mProjectileOptionsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_PROJECTILES_LIMITS_MENU == code) {
        mCurrentMenu = mProjectileLimitsMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_TO_MODE_SETTINGS_MENU == code) {
        for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
            if (mGameSettingsMenu->miGoalField[iGameMode]->IsVisible()) {
                mCurrentMenu = mModeOptionsMenu->GetOptionsMenu(iGameMode);
                mCurrentMenu->ResetMenu();
                break;
            }
        }
    } else if (MENU_CODE_MENU_GRAPHICS_PACK_CHANGED == code) {
        rm->loadMenuGraphics();

        blitdest = rm->menu_backdrop.getSurface();
        rm->drawShade(App::menuTransparency);
        blitdest = screen;
    } else if (MENU_CODE_WORLD_GRAPHICS_PACK_CHANGED == code) {
        rm->loadWorldGraphics();
    } else if (MENU_CODE_GAME_GRAPHICS_PACK_CHANGED == code) {
        try {
            gfx_loadpalette(convertPath("gfx/packs/palette.png", gamegraphicspacklist->currentPath()));
        } catch (const std::string& err) {
            printf("\nwarning: %s -> falling back to BMP\n", err.c_str());
            gfx_loadpalette(convertPath("gfx/packs/palette.bmp", gamegraphicspacklist->currentPath()));
        }
        rm->loadGameGraphics();
    } else if (MENU_CODE_SOUND_PACK_CHANGED == code) {
        rm->LoadGameSounds();

        if (!game_values.soundcapable) {
            game_values.sound = false;
            game_values.music = false;
            game_values.soundvolume = 0;
            game_values.musicvolume = 0;
        }
    } else if (MENU_CODE_WORLD_STAGE_START == code) {
        mWorldMenu->OpenStageStart();
    } else if (MENU_CODE_WORLD_STAGE_NO_START == code) {
        mWorldMenu->CloseStageStart();
    } else if (MENU_CODE_WORLD_MUSIC_CHANGED == code) {
        worldmusiclist->setCurrent(mSoundOptionsMenu->GetCurrentWorldMusicID());
    } else if (MENU_CODE_TOUR_STOP_CONTINUE == code || MENU_CODE_TOUR_STOP_CONTINUE_FORCED == code) {
        //If this tour stop is forced, we need to load the map first
        if (MENU_CODE_TOUR_STOP_CONTINUE_FORCED == code)
            mWorldMenu->miWorldStop->Refresh(game_values.tourstopcurrent);

        mWorldMenu->miWorld->ClearCloud();

        //Tour bonus house
        if (game_values.matchtype == MatchType::World && game_values.tourstops[game_values.tourstopcurrent]->iStageType == 1) {
            bonushousemode->goal = 0;
            game_values.gamemode = bonushousemode;
        } else {
            short iGameMode = game_values.tourstops[game_values.tourstopcurrent]->iMode;

            if (iGameMode == game_mode_pipe_minigame)
                game_values.gamemode = pipegamemode;
            else if (iGameMode == game_mode_boss_minigame)
                game_values.gamemode = bossgamemode;
            else if (iGameMode == game_mode_boxes_minigame)
                game_values.gamemode = boxesgamemode;
            else
                game_values.gamemode = gamemodes[iGameMode];

            game_values.gamemode->goal = game_values.tourstops[game_values.tourstopcurrent]->iGoal;
        }

        StartGame();
    } else if (MENU_CODE_RESET_STORED_POWERUPS == code) {
        for (short iPlayer = 0; iPlayer < 4; iPlayer++)
            game_values.storedpowerups[iPlayer] = -1;
    } else if (MENU_CODE_MAP_CHANGED == code) {
        if (game_values.matchtype != MatchType::Tour)
            szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
    } else if (MENU_CODE_MAP_FILTER_EXIT == code) {
        maplist->ApplyFilters(game_values.pfFilters);

        //If the filtered map list has at least 1 map in it, then allow exiting the filter menu
        if (maplist->MapInFilteredSet()) {
            mGameSettingsMenu->miMapField->LoadCurrentMap();
            szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();

            mGameSettingsMenu->CloseMapFilters();

            iDisplayError = DISPLAY_ERROR_NONE;
        } else { //otherwise display a message
            iDisplayError = DISPLAY_ERROR_MAP_FILTER;
            iDisplayErrorTimer = 120;
        }
    } else if (MENU_CODE_TO_MAP_FILTERS == code) {
        mGameSettingsMenu->OpenMapFilters();
    } else if (MENU_CODE_TO_MAP_FILTER_EDIT == code) {
        mMapFilterEditMenu->miMapBrowser->Reset(0);

        mCurrentMenu = mMapFilterEditMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_MAP_BROWSER_EXIT == code) {
        mGameSettingsMenu->miMapField->LoadCurrentMap();
        szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();

        mCurrentMenu = mGameSettingsMenu.get();
    } else if (MENU_CODE_TO_MAP_BROWSER_THUMBNAILS == code) {
        mMapFilterEditMenu->miMapBrowser->Reset(1);

        mCurrentMenu = mMapFilterEditMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_SAVE_ALL_MAP_THUMBNAILS == code) {
        (static_cast<UI_OptionsMenu*>(mCurrentMenu))->showThumbnailsPopup();
        mCurrentMenu->ResetMenu();
    } else if (MENU_CODE_GENERATE_THUMBS_RESET_YES == code || MENU_CODE_GENERATE_THUMBS_RESET_NO == code) {
        (static_cast<UI_OptionsMenu*>(mCurrentMenu))->hideThumbnailsPopup();

        if (MENU_CODE_GENERATE_THUMBS_RESET_YES == code) {
            fGenerateMapThumbs = true;
        }
    } else if (MENU_CODE_HEALTH_MODE_START_LIFE_CHANGED == code) {
        mModeOptionsMenu->HealthModeStartLifeChanged();
    } else if (MENU_CODE_HEALTH_MODE_MAX_LIFE_CHANGED == code) {
        mModeOptionsMenu->HealthModeMaxLifeChanged();
    }
    return true;
}

// The game settings menu, entered from team selection: the match type's setup (tour, tournament, world, ...)
void MenuState::EnterGameSetup()
{
    printf("MENU_CODE_TO_GAME_SETUP_MENU\n");
    //Moves teams to the first arrays in the list and counts the number of teams (a local game from
    //the lobby: they're there already)
    g_sim.score_cnt = mLocalFromLobby ? mLocalTeams : mTeamSelectMenu->GetTeamCount();
    iDisplayError = DISPLAY_ERROR_NONE;
    iDisplayErrorTimer = 0;
    bool fErrorReadingTourFile = false;

    if (MatchType::MiniGame == game_values.matchtype) {
        printf(" Match type: Minigame\n");
        const Minigame minigame = mMatchSelectionMenu->GetMinigame();
        switch (minigame) {
            case Minigame::PipeCoin:
                pipegamemode->goal = 50;
                game_values.gamemode = pipegamemode;
                break;
            case Minigame::HammerBoss:
            case Minigame::BombBoss:
            case Minigame::FireBoss:
                game_values.gamemodemenusettings.boss.difficulty = 2;
                game_values.gamemodemenusettings.boss.hitpoints = 5;

                bossgamemode->goal = 5;
                game_values.gamemode = bossgamemode;
                break;
            case Minigame::Boxes:
                boxesgamemode->goal = 10;
                game_values.gamemode = boxesgamemode;
                break;
        }
        switch (minigame) {
            case Minigame::HammerBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Hammer;
                break;
            case Minigame::BombBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Bomb;
                break;
            case Minigame::FireBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Fire;
                break;
            default:
                break;
        }
        StartGame();
    } else if (MatchType::QuickGame == game_values.matchtype) {
        printf(" Match type: Quick game\n");
        short iRandomMode = RANDOM_INT(game_mode_mapmaker);    //(not the Map Maker: it never ends)
        game_values.gamemode = gamemodes[iRandomMode];

        SModeOption * options = game_values.gamemode->GetOptions();

        //Choose a goal from the lower values for a quicker game
        short iRandOption = (RANDOM_INT(6)) + 1;
        game_values.gamemode->goal  = options[iRandOption].iValue;

        game_values.tournamentwinner = -1;

        StartGame();
    } else {
        //Load the tour here if one was selected
        if (game_values.matchtype == MatchType::Tour) {
            printf("  Match type: Tour\n");
            if (!ReadTourFile()) {
                iDisplayError = DISPLAY_ERROR_READ_TOUR_FILE;
                iDisplayErrorTimer = 120;
                fErrorReadingTourFile = true;
            } else {
                mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tourstops.size(), &rm->spr_tour_markers);
            }
        } else if (game_values.matchtype == MatchType::Tournament) {
            printf("  Match type: Tournament\n");
            //Set who is controlling the tournament menu
            if (game_values.tournamentcontrolstyle == TournamentControlStyle::Random || game_values.tournamentcontrolstyle == TournamentControlStyle::RandomLoser) //Random
                game_values.tournamentcontrolteam = RANDOM_INT( g_sim.score_cnt);
            else if (game_values.tournamentcontrolstyle == TournamentControlStyle::RoundRobin) //Round robin
                game_values.tournamentcontrolteam = 0;
            else //The first game of the tournament is controlled by all players
                game_values.tournamentcontrolteam = -1;

            game_values.tournamentnextcontrol = 1;  //if round robin is selected, choose the next team next

            mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tournamentgames, &rm->menu_mode_large);
        } else if (game_values.matchtype == MatchType::World) {
            printf("  Match type: World\n");
            try {
                g_worldmap = loadWorldMap(worldlist->at(game_values.worldindex).string(), TILESIZE);

                mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, 0, &rm->spr_tour_markers);

                g_worldmap.SetInitialPowerups();

                //If a player had a stored powerup from another game, add it to their inventory
                for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
                    if (game_values.storedpowerups[iPlayer] != -1) {
                        short iTeamId = LookupTeamID(iPlayer);
                        if (game_values.worldpowerupcount[iTeamId] < 32)
                            game_values.worldpowerups[iTeamId][game_values.worldpowerupcount[iTeamId]++] = game_values.storedpowerups[iPlayer];

                        game_values.storedpowerups[iPlayer] = -1;
                    }
                }

                mWorldMenu->miWorld->Init();
                mWorldMenu->miWorld->SetControllingTeam(RANDOM_INT( g_sim.score_cnt));
            }
            catch (const std::runtime_error& err) {
                iDisplayError = DISPLAY_ERROR_READ_WORLD_FILE;
                iDisplayErrorTimer = 120;
                fErrorReadingTourFile = true;
            }
        }

        if (!fErrorReadingTourFile) {
            printf("  !fErrorReadingTourFile\n");
            mTournamentScoreboardMenu->ClearEyeCandy();

            //Initialize tournament values
            game_values.tournamentwinner = -1;

            //Setup wins counters for tournament/tour
            for (int k = 0; k < 4; k++) {
                game_values.tournament_scores[k].wins = 0;
                game_values.tournament_scores[k].total = 0;
            }

            if (MatchType::SingleGame == game_values.matchtype || MatchType::Tournament == game_values.matchtype) {
                printf("  MatchType::SingleGame || MatchType::Tournament\n");
                printf("current map: %s\n", szCurrentMapName);
                maplist->findexact(szCurrentMapName, false);
                mGameSettingsMenu->miMapField->LoadCurrentMap();

                game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];

                for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++) {
                    gamemodes[iMode]->goal = mGameSettingsMenu->miGoalField[iMode]->currentValue();
                }

                if (mGameSettingsMenu->GetCurrentGameModeID() == game_mode_owned)
                    mGameSettingsMenu->HideGMSettingsBtn();

                mCurrentMenu = mGameSettingsMenu.get();
                mCurrentMenu->ResetMenu();

                //If it is a tournament, then set the controlling team
                if (MatchType::Tournament == game_values.matchtype)
                    SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, true);
                } else if (MatchType::Tour == game_values.matchtype) {
                    mCurrentMenu = mTourStopMenu.get();
                    mCurrentMenu->ResetMenu();
                } else if (MatchType::World == game_values.matchtype) {
                    game_values.screenfadespeed = 8;
                    game_values.screenfade = 8;
                    game_values.appstate = AppState::StartWorld;

                    //Play enter world sound
                    rm->sfx_enterstage.play();
                }

            //Setup items on next menu
            for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++) {
                mGameSettingsMenu->miGoalField[iGameMode]->hideItem(-1, game_values.matchtype == MatchType::Tournament);
            }

            if (game_values.matchtype == MatchType::World)
                mGameSettingsMenu->SetHeaderText("World Game");
            else if (game_values.matchtype == MatchType::Tour)
                mGameSettingsMenu->SetHeaderText("Tour Game");
            else if (game_values.matchtype == MatchType::Tournament)
                mGameSettingsMenu->SetHeaderText("Tournament Game");
            else
                mGameSettingsMenu->SetHeaderText("Single Game");
        }
    }
}

void MenuState::SetControllingTeamForSettingsMenu(short iControlTeam, bool fDisplayMessage)
{
    mGameSettingsMenu->SetControllingTeam(iControlTeam);

    mModeOptionsMenu->SetControllingTeam(iControlTeam);

    mGameSettingsMenu->miMapFilterScroll->SetControllingTeam(iControlTeam);
    mMapFilterEditMenu->miMapBrowser->SetControllingTeam(iControlTeam);

    if (fDisplayMessage)
        DisplayControllingTeamMessage(iControlTeam);

    //Scan controlling team members and if they are only bots, then let the cpu control the selection
    iTournamentAITimer = 0;
    iTournamentAIStep = 0;
    mGameSettingsMenu->SetAllowExit(false);

    if (iControlTeam >= 0) {
        bool fNeedAI = true;
        for (short iPlayer = 0; iPlayer < game_values.teamcounts[iControlTeam]; iPlayer++) {
            if (game_values.playercontrol[game_values.teamids[iControlTeam][iPlayer]] == 1) {
                fNeedAI = false;
                break;
            }
        }

        if (fNeedAI) {
            iTournamentAITimer = 60;
            mGameSettingsMenu->SetAllowExit(true);
        }
    }
}

void MenuState::DisplayControllingTeamMessage(short iControlTeam)
{
    //Display the team that is in control of selecting the next game
    std::string szMessage;
    if (iControlTeam < 0)
        szMessage = "All Teams Are In Control";
    else if (game_values.teamcounts[iControlTeam] <= 1)
        szMessage = std::format("Player {} Is In Control", game_values.teamids[iControlTeam][0] + 1);
    else
        szMessage = std::format("Team {} Is In Control", iControlTeam + 1);

    mCurrentMenu->AddEyeCandy<EC_Announcement>(&rm->menu_font_large, &rm->spr_announcementicons, szMessage, iControlTeam < 0 ? 4 : game_values.colorids[game_values.teamids[iControlTeam][0]], 120, 100);
}

#ifdef SMW_HAS_EDITORS
// Runs an editor (alone, or in the editing session that is open); false if
// the game quits
bool MenuState::RunEditor(editors::Editor editor)
{
    //The editor runs in the window until it is closed (EditorLauncher.h)
    rm->backgroundmusic[2].stop();
    const bool keepRunning = editors::run(editor);
    const bool together = editors::coedit::state() != editors::coedit::State::Off;
    if (together) {
        editors::coedit::leave(editors::coedit::isHost() ? "the host left" : "left");
        mEditorsMenu->SetStatus("The editing session ended");
    } else {
        mEditorsMenu->SetStatus("");
    }
    if (!keepRunning) {
        Exit();
        return false;
    }
    if (game_values.music)
        rm->backgroundmusic[2].play(false, false);
    //Maps saved in the editor: the map field shows the list again
    mGameSettingsMenu->miMapField->LoadCurrentMap();
    game_values.playerInput.ResetKeys();
    //(a controller's buttons held as the editor closed aren't presses in the menus)
    menupad::resync();
    //"Play this map": its match now, the editor again after it
    if (const std::string play = editors::takeTestPlayRequest(); !play.empty() && editor == editors::Editor::Level)
        StartTestPlay(play);
    return true;
}

// Player 1 alone on the map, in frag mode without a goal: the match lasts until they leave it. What
// that changes of the settings is kept here and put back after it (EndTestPlay), and nothing of it
// is written to the settings file (StartGame)
void MenuState::StartTestPlay(const std::string& file)
{
    TestPlay& t = mTestPlay;
    for (int k = 0; k < 4; k++) {
        t.playercontrol[k] = game_values.playercontrol[k];
        t.teamcounts[k] = game_values.teamcounts[k];
        for (int s = 0; s < 3; s++)
            t.teamids[k][s] = game_values.teamids[k][s];
    }
    t.scoreCount = g_sim.score_cnt;
    t.matchtype = game_values.matchtype;
    t.gamemode = game_values.gamemode;
    CGameMode* frag = gamemodes[game_mode_frag];
    t.fragGoal = frag->goal;

    for (int k = 0; k < 4; k++) {
        game_values.playercontrol[k] = k == 0 ? 1 : 0;
        game_values.teamcounts[k] = k == 0 ? 1 : 0;
        for (int s = 0; s < 3; s++)
            game_values.teamids[k][s] = 0;
    }
    g_sim.score_cnt = 1;
    game_values.matchtype = MatchType::SingleGame;
    game_values.gamemode = frag;
    frag->goal = -1;    //(unlimited)

    t.map = file;
    editors::setTestPlaying(true);
    mCurrentMenu = mEditorsMenu.get();
    printf("editors: playing %s\n", file.c_str());
    StartGame();
}

// Back from the test play: the settings as they were, and the editor opens on the map again
void MenuState::EndTestPlay()
{
    TestPlay& t = mTestPlay;
    for (int k = 0; k < 4; k++) {
        game_values.playercontrol[k] = t.playercontrol[k];
        game_values.teamcounts[k] = t.teamcounts[k];
        for (int s = 0; s < 3; s++)
            game_values.teamids[k][s] = t.teamids[k][s];
    }
    g_sim.score_cnt = t.scoreCount;
    game_values.matchtype = t.matchtype;
    gamemodes[game_mode_frag]->goal = t.fragGoal;
    if (t.gamemode)
        game_values.gamemode = t.gamemode;

    editors::setTestPlaying(false);
    editors::setTestPlayReturn(t.map);
    t.map.clear();
    t.reopenEditor = true;
    mCurrentMenu = mEditorsMenu.get();
}
#endif

// The determinism test's result (Determinism.h), over the menu: the Xbox has no console to print it to
void MenuState::DrawDeterminismReport()
{
    if (!determinism::finished())
        return;
    const std::vector<std::string>& lines = determinism::report();
    const int h = 70 + static_cast<int>(lines.size()) * 22;
    const int y = (App::screenHeight - h) / 2;
    menupanel::box(30, y, 580, h);
    int ly = y + 16;
    for (size_t k = 0; k < lines.size(); k++) {
        const bool verdict = k + 1 == lines.size();
        (verdict || k == 0 ? rm->menu_font_large : rm->menu_font_small).drawCentered(320, ly, lines[k].c_str());
        ly += 22;
    }
    rm->menu_font_small.drawCentered(320, y + h - 26, "Press A or B to quit");
}

void MenuState::Exit()
{
#ifdef __EMSCRIPTEN__
    return;
#endif
    //Online: tell the others that this machine is gone
    rollback::NetGame& net = rollback::NetGame::instance();
    if (net.active() && !cmd::g_net.enabled())
        net.leave();

    game_values.appstate = AppState::Quit;
    game_values.WriteConfig();
}

void MenuState::ResetTournamentBackToMainMenu()
{
    mCurrentMenu = mMainMenu.get();
    mCurrentMenu->ResetMenu();

    if (game_values.matchtype != MatchType::SingleGame && game_values.matchtype != MatchType::QuickGame && game_values.matchtype != MatchType::MiniGame) {
        if (fNeedMenuMusicReset) {
            rm->backgroundmusic[3].stop();
            rm->backgroundmusic[2].play(false, false);
            fNeedMenuMusicReset = false;
        }
    }
}

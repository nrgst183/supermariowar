// THE THEATER: watching a replay (rollback/Replay.h)
//
// Like a film viewer: play and pause, slower and faster, skip back and
// forward, step frame by frame, rewind and fast forward, jump anywhere on the
// timeline (its markers are the score changes and the end of the match), and
// follow one player (their name over them, and the buttons they hold).
//
// Everything works with a controller (any connected one), the keyboard or the
// mouse; the bar's buttons show the controller button or the key that does
// the same (whichever was used last, see ui/InputPrompts.h). The look is the
// game's and SMB3's (ui/SmwTheme.h): the dock is a status bar, the timeline a line in its colors.
//
//                       controller        keyboard      mouse
//   play / pause        A (or Start)      Space         the play button
//   back / forward 5 s  LB / RB           Left / Right  the arrow buttons
//     (paused: 1 frame)  (d-pad < >)
//   rewind / fast fwd   LT / RT (hold)    Shift+Left / Shift+Right (hold)
//   speed               d-pad up / down   Up / Down     the clock button (right click: slower)
//   follow a player     X                 F             the camera button
//   hide the bar        Y                 H             the eye button
//   the controls        Back              F1            the ? block
//   leave               B                 Escape        the pipe
//   jump                                  Home / End    click / drag on the timeline
//
// The bar hides itself while the replay plays; any input shows it again.

#include "GSGameplay.h"

#include "CmdArgs.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplayInternal.h"
#include "GSMenu.h"
#include "MatchHistory.h"
#include "player.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "rollback/Replay.h"
#include "ui/InputPrompts.h"
#include "ui/SmwTheme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using prompts::Pad;

namespace {

constexpr std::array<float, 6> SPEEDS = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
constexpr const char* SPEED_NAMES[] = {"1/4x", "1/2x", "1x", "2x", "4x", "8x"};
constexpr int NORMAL_SPEED = 2;
constexpr int SKIP_TICKS = 313;        // 5 seconds
constexpr int UI_SHOW_FRAMES = 200;    // after the last input

// The layout (screen coordinates): the dock (the game's green dialog) at the
// bottom, the timeline (a line) in it, the buttons under it
constexpr int DOCK_Y = 394;
constexpr int TRACK_X = 88;
constexpr int TRACK_W = 446;
constexpr int TRACK_Y = 420;
constexpr int TRACK_H = 16;
constexpr int BUTTON_Y = 446;
constexpr int BUTTON_H = 26;

constexpr SDL_Color WHITE {255, 255, 255, 255};
constexpr SDL_Color BLACK {0, 0, 0, 255};
constexpr SDL_Color GOLD {248, 200, 40, 255};
constexpr SDL_Color GOLD_LIGHT {255, 240, 144, 255};
constexpr SDL_Color PAPER {236, 236, 244, 255};

// The bar's buttons
enum Control { BACK5, PLAY, FORWARD5, SPEED, FOLLOW, HIDE, HELP, LEAVE, CONTROL_COUNT };

struct ControlInfo {
    Pad pad;
    const char* key;      // the key cap
    const char* label;    // the tooltip and the help
};
const std::array<ControlInfo, CONTROL_COUNT> CONTROLS = {{
    {Pad::LB, "<", "Back 5 seconds (paused: a frame)"},
    {Pad::A, "Spc", "Play / pause"},
    {Pad::RB, ">", "Forward 5 seconds (paused: a frame)"},
    {Pad::DPad, "Up", "Speed (right click: slower)"},
    {Pad::X, "F", "Follow a player"},
    {Pad::Y, "H", "Hide the bar"},
    {Pad::Back, "F1", "The controls"},
    {Pad::B, "Esc", "Leave the replay"},
}};

struct Theater {
    bool paused = false;
    int speed = NORMAL_SPEED;
    float carry = 0;           // fractions of a tick (slow speeds)
    float speedNow = 0;        // the ticks per update of the latest update (0: paused)
    int smoothTick = -1;       // the tick the smooth motion's previous positions lead to (-1: none, after a jump)
    int uiFrames = UI_SHOW_FRAMES;
    bool hidden = false;
    bool help = false;
    int follow = -1;           // the followed slot, -1: nobody
    int holdBack = 0;
    int holdForward = 0;
    bool dragging = false;
    int endFrames = 0;
    float mouseX = -1;
    float mouseY = -1;
    std::array<SDL_Rect, CONTROL_COUNT> rects {};

    prompts::Gamepads pads;
    prompts::PadState pad;
    prompts::PadState lastPad;

    //This frame's commands (keyboard and mouse events, then the controller)
    bool playPause = false;
    int skip = 0;              // -1 back, +1 forward
    int speedChange = 0;
    bool followNext = false;
    bool toggleHide = false;
    bool toggleHelp = false;
    bool leave = false;
};
Theater g_theater;

// Every jump: the smooth motion starts again from the next tick
void seekTo(int tick)
{
    g_theater.smoothTick = -1;
    replay::seek(tick);
}
std::string g_theaterShot;    // --at-role theater: a screenshot of this frame

// The colors of the players' color ids (red, green, yellow, blue)
SDL_Color slotColor(int slot)
{
    static const SDL_Color colors[] = {{232, 56, 40, 255}, {40, 184, 56, 255}, {248, 200, 24, 255}, {48, 112, 232, 255}};
    const short id = slot >= 0 && slot < 4 ? game_values.colorids[slot] : 0;
    return colors[std::clamp<int>(id, 0, 3)];
}

SDL_Color teamColor(int team)
{
    if (team >= 0 && team < 4 && game_values.teamcounts[team] > 0)
        return slotColor(game_values.teamids[team][0]);
    return WHITE;
}

// The theme's tone nearest to a player's color
theme::Tone slotTone(int slot)
{
    const short id = slot >= 0 && slot < 4 ? game_values.colorids[slot] : 0;
    static const theme::Tone tones[] = {theme::Tone::Red, theme::Tone::Green, theme::Tone::Gold, theme::Tone::Blue};
    return tones[std::clamp<int>(id, 0, 3)];
}

int tickAt(float mouseX)
{
    const int total = replay::playbackStatus().total;
    const float f = std::clamp((mouseX - TRACK_X) / static_cast<float>(TRACK_W), 0.0f, 1.0f);
    return static_cast<int>(std::lround(f * total));
}

std::string followName(int slot)
{
    const std::string& name = replay::playerName(slot);
    return name.empty() ? "Player " + std::to_string(slot + 1) : name;
}

// The next slot with a player (after `slot`), or -1 after the last
int nextFollow(int slot)
{
    for (int k = slot + 1; k < 4; k++) {
        if (game_values.playercontrol[k] != 0)
            return k;
    }
    return -1;
}

bool inside(const SDL_Rect& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool barShown()
{
    const Theater& th = g_theater;
    return !th.hidden && (th.paused || th.help || replay::playbackStatus().finished || th.uiFrames > 0);
}

// The width of a button
int buttonWidth(Control c)
{
    return c == SPEED ? 62 : 30;
}

// Lays out the buttons: the play controls on the left, the others on the right
void layout()
{
    Theater& th = g_theater;
    int x = 14;
    for (int c = BACK5; c <= SPEED; c++) {
        const int w = buttonWidth(static_cast<Control>(c));
        th.rects[static_cast<size_t>(c)] = {x, BUTTON_Y, w, BUTTON_H};
        x += w + 2 + prompts::promptWidth(CONTROLS[static_cast<size_t>(c)].pad, CONTROLS[static_cast<size_t>(c)].key, "") + 6;
    }
    x = App::screenWidth - 14;
    for (int c = LEAVE; c >= FOLLOW; c--) {
        const int w = buttonWidth(static_cast<Control>(c));
        x -= prompts::promptWidth(CONTROLS[static_cast<size_t>(c)].pad, CONTROLS[static_cast<size_t>(c)].key, "") + 2 + w;
        th.rects[static_cast<size_t>(c)] = {x, BUTTON_Y, w, BUTTON_H};
        x -= 6;
    }
}

// The icons (8x8, in doubled pixels: 16x16)
const std::vector<prompts::Ink> GOLD_INKS = {{'#', BLACK}, {'g', GOLD}, {'l', GOLD_LIGHT}};

void iconBack(int x, int y)
{
    prompts::art(x - 2, y, {
        "...##...##",
        "..#l#..#l#",
        ".#lg#.#lg#",
        "#lgg##lgg#",
        "#ggg##ggg#",
        ".#gg#.#gg#",
        "..#g#..#g#",
        "...##...##",
    }, GOLD_INKS);
}

void iconForward(int x, int y)
{
    prompts::art(x - 2, y, {
        "##...##...",
        "#l#..#l#..",
        "#lg#.#lg#.",
        "#lgg##lgg#",
        "#ggg##ggg#",
        "#gg#.#gg#.",
        "#g#..#g#..",
        "##...##...",
    }, GOLD_INKS);
}

void iconPlay(int x, int y)
{
    prompts::art(x + 1, y, {
        "##.....",
        "#l##...",
        "#lgg#..",
        "#lggg##",
        "#gggg##",
        "#ggg#..",
        "#g##...",
        "##.....",
    }, GOLD_INKS);
}

void iconPause(int x, int y)
{
    prompts::art(x + 1, y, {
        "###.###",
        "#l#.#l#",
        "#l#.#l#",
        "#g#.#g#",
        "#g#.#g#",
        "#g#.#g#",
        "#g#.#g#",
        "###.###",
    }, GOLD_INKS);
}

void iconCamera(int x, int y, SDL_Color lens)
{
    prompts::art(x, y, {
        ".###....",
        "########",
        "#ww##ww#",
        "#w#ll#w#",
        "#w#lL#w#",
        "#ww##ww#",
        "#wwwwww#",
        "########",
    }, {{'#', BLACK}, {'w', PAPER}, {'l', lens}, {'L', theme::shade(lens, 150)}});
}

void iconEye(int x, int y)
{
    prompts::art(x, y + 1, {
        "..####..",
        ".#wwww#.",
        "#ww##ww#",
        "#w#bB#w#",
        "#ww##ww#",
        ".#wwww#.",
        "..####..",
    }, {{'#', BLACK}, {'w', PAPER}, {'b', {40, 112, 232, 255}}, {'B', WHITE}});
}

void drawIcon(Control c, int x, int y)
{
    const Theater& th = g_theater;
    switch (c) {
        case BACK5: iconBack(x, y); break;
        case FORWARD5: iconForward(x, y); break;
        case PLAY:
            if (th.paused || replay::playbackStatus().finished)
                iconPlay(x, y);
            else
                iconPause(x, y);
            break;
        case SPEED: theme::icon(x, y, theme::Icon::Clock); break;
        case FOLLOW: iconCamera(x, y, th.follow >= 0 ? slotColor(th.follow) : SDL_Color {120, 120, 140, 255}); break;
        case HIDE: iconEye(x, y); break;
        case HELP: theme::icon(x, y, theme::Icon::QuestionBlock); break;
        case LEAVE: theme::modeIcon(x, y, 25); break;    //(a warp pipe: out)
        default: break;
    }
}

// The playhead: a gold knob (20 x 20, in doubled pixels); lighter while it is dragged
void drawPlayhead(int cx, int cy, bool held)
{
    const SDL_Color gold = held ? GOLD_LIGHT : GOLD;
    prompts::art(cx - 10, cy - 10, {
        "...####...",
        ".##llgg##.",
        ".#lllggg#.",
        "#llgggggg#",
        "#lggggggd#",
        "#ggggggdd#",
        "#gggggddd#",
        ".#gggddd#.",
        ".##dddd##.",
        "...####...",
    }, {{'#', BLACK}, {'l', GOLD_LIGHT}, {'g', gold}, {'d', {200, 136, 0, 255}}});
}

void drawButton(Control c, const SDL_Rect& r, bool hover, bool active)
{
    const Theater& th = g_theater;
    //Like the menus' fields: gray, green when selected (here: under the mouse, or switched on)
    theme::block(r.x, r.y, r.w, r.h, hover ? theme::Tone::Green : active ? theme::Tone::Gold : theme::Tone::Gray);
    if (c == SPEED) {
        drawIcon(c, r.x + 5, r.y + 5);
        theme::font().drawCentered(r.x + 40, r.y + 5, SPEED_NAMES[th.speed]);
    } else {
        drawIcon(c, r.x + (r.w - 16) / 2, r.y + 5);
    }
}

void drawHelp()
{
    const int w = 580;
    const int h = 332;
    const int x = (App::screenWidth - w) / 2;
    const int y = 58;
    theme::panel(x, y, w, h);
    //(its title: an announcement, like the match's)
    theme::announce(App::screenWidth / 2, y - 26, "Theater", [](int ix, int iy) { theme::clapperboard(ix, iy); });
    const int padX = x + 272;
    const int keyX = x + 394;
    theme::heading(padX - 6, y + 40, 116, prompts::style() == prompts::Style::OriginalXbox ? "Xbox (orig.)" : "Controller");
    theme::heading(keyX - 6, y + 40, 168, "Keyboard");
    struct Row {
        Control icon;
        const char* what;
        std::vector<Pad> pads;
        std::vector<std::string> keys;
    };
    const Row rows[] = {
        {PLAY, "Play / pause", {Pad::A}, {"Space"}},
        {FORWARD5, "Back / forward 5 s", {Pad::LB, Pad::RB}, {"Left", "Right"}},
        {CONTROL_COUNT, "  paused: one frame", {Pad::Left, Pad::Right}, {"Left", "Right"}},
        {BACK5, "Rewind / fast fwd (hold)", {Pad::LT, Pad::RT}, {"Shift", "Left", "Right"}},
        {SPEED, "Slower / faster", {Pad::Down, Pad::Up}, {"Down", "Up"}},
        {FOLLOW, "Follow a player", {Pad::X}, {"F"}},
        {HIDE, "Hide the bar", {Pad::Y}, {"H"}},
        {HELP, "These controls", {Pad::Back}, {"F1"}},
        {LEAVE, "Leave", {Pad::B}, {"Esc"}},
        {CONTROL_COUNT, "Jump in the replay", {}, {}},
    };
    int ry = y + 64;
    bool odd = false;
    for (const Row& row : rows) {
        if (odd)
            prompts::fill(x + 14, ry - 2, w - 34, 22, theme::scheme().stripe);
        odd = !odd;
        if (row.icon != CONTROL_COUNT)
            drawIcon(row.icon, x + 20, ry + 1);
        else if (row.pads.empty())
            drawPlayhead(x + 28, ry + 9, false);
        theme::font().draw(x + 44, ry + 1, row.what);
        int px = padX;
        for (Pad p : row.pads)
            px += prompts::drawPad(px, ry + 1, p) + 3;
        int kx = keyX;
        for (const std::string& k : row.keys)
            kx += prompts::drawKey(kx, ry + 1, k) + 3;
        if (row.pads.empty()) {
            prompts::drawMouse(padX, ry + 1, true, false);
            theme::font().draw(padX + 22, ry + 2, "click or drag on the meter");
        }
        ry += 22;
    }
    theme::font().drawCentered(App::screenWidth / 2, y + h - 32, "Close: the same button again");
}

void showUi()
{
    g_theater.uiFrames = UI_SHOW_FRAMES;
}

bool padPressed(prompts::Button b)
{
    return g_theater.pad.buttons[b] && !g_theater.lastPad.buttons[b];
}

} // namespace

void GameplayState::resetTheater()
{
    g_theater = Theater();
    if (replay::playing())
        g_theater.pads.open();
}

void GameplayState::theaterEvent(const SDL_Event& event)
{
    Theater& th = g_theater;
    prompts::note(event);
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            showUi();
            const SDL_Keycode key = event.key.key;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            if (key == SDLK_SPACE || key == SDLK_K)
                th.playPause = true;
            else if ((key == SDLK_LEFT || key == SDLK_J) && !shift)
                th.skip = -1;
            else if ((key == SDLK_RIGHT || key == SDLK_L) && !shift)
                th.skip = 1;
            else if (key == SDLK_UP)
                th.speedChange = 1;
            else if (key == SDLK_DOWN)
                th.speedChange = -1;
            else if (key == SDLK_F)
                th.followNext = true;
            else if (key == SDLK_H)
                th.toggleHide = true;
            else if (key == SDLK_F1 || key == SDLK_SLASH)
                th.toggleHelp = true;
            else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE)
                th.leave = true;
            else if (key == SDLK_HOME)
                seekTo(0);
            else if (key == SDLK_END)
                seekTo(replay::playbackStatus().total);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            showUi();
            const float x = event.button.x;
            const float y = event.button.y;
            if (th.help) {
                th.toggleHelp = true;
                break;
            }
            if (!barShown())
                break;
            if (event.button.button == SDL_BUTTON_LEFT && y >= TRACK_Y - 10 && y < TRACK_Y + TRACK_H + 6 && x >= TRACK_X - 8 &&
                    x < TRACK_X + TRACK_W + 8) {
                th.dragging = true;
                seekTo(tickAt(x));
                lastUpdateTicked = false;
                break;
            }
            for (int c = 0; c < CONTROL_COUNT; c++) {
                if (!inside(th.rects[static_cast<size_t>(c)], x, y))
                    continue;
                const bool right = event.button.button == SDL_BUTTON_RIGHT;
                switch (c) {
                    case BACK5: th.skip = -1; break;
                    case PLAY: th.playPause = true; break;
                    case FORWARD5: th.skip = 1; break;
                    case SPEED: th.speedChange = right ? -1 : 1; break;
                    case FOLLOW: th.followNext = true; break;
                    case HIDE: th.toggleHide = true; break;
                    case HELP: th.toggleHelp = true; break;
                    case LEAVE: th.leave = true; break;
                    default: break;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            th.mouseX = event.motion.x;
            th.mouseY = event.motion.y;
            if (th.dragging && (event.motion.state & SDL_BUTTON_LMASK)) {
                seekTo(tickAt(event.motion.x));
                lastUpdateTicked = false;
            }
            showUi();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            th.dragging = false;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
            th.pads.open();
            break;
        default:
            break;
    }
}

void GameplayState::updateReplay()
{
    lastUpdateTicked = false;
    Theater& th = g_theater;
    handleInput();    //(the keyboard and mouse: theaterEvent)
    if (game_values.appstate == AppState::Quit)
        return;
    //(the game's pause and exit dialogs aren't used here)
    game_values.flags.pausegame = false;
    game_values.flags.exitinggame = false;
    if (updateMatchTransition())
        return;

    //The controller
    th.lastPad = th.pad;
    th.pad = th.pads.any() ? th.pads.merged() : prompts::PadState();
    bool controller = false;
    for (int b = 0; b < prompts::BUTTON_COUNT; b++)
        controller = controller || padPressed(static_cast<prompts::Button>(b));
    if (controller || th.pad.lt > 0.3f || th.pad.rt > 0.3f) {
        prompts::setLastDevice(prompts::Device::Controller);
        showUi();
    }
    if (padPressed(prompts::A) || padPressed(prompts::START))
        th.playPause = true;
    if (padPressed(prompts::B))
        th.leave = true;
    if (padPressed(prompts::X))
        th.followNext = true;
    if (padPressed(prompts::Y))
        th.toggleHide = true;
    if (padPressed(prompts::BACK))
        th.toggleHelp = true;
    if (padPressed(prompts::UP))
        th.speedChange = 1;
    if (padPressed(prompts::DOWN))
        th.speedChange = -1;

    //The help: the same button closes it (and B / Escape)
    if (th.help && th.leave) {
        th.leave = false;
        th.toggleHelp = true;
    }
    if (th.toggleHelp)
        th.help = !th.help;
    if (th.leave) {
        leaveReplay();
        return;
    }

    const replay::PlaybackStatus& status = replay::playbackStatus();
    if (th.playPause) {
        if (status.finished) {
            seekTo(0);    //(from the start again)
            th.paused = false;
        } else {
            th.paused = !th.paused;
        }
    }
    if (th.speedChange) {
        th.speed = std::clamp(th.speed + th.speedChange, 0, static_cast<int>(SPEEDS.size()) - 1);
        th.carry = 0;
    }
    if (th.followNext)
        th.follow = nextFollow(th.follow);
    if (th.toggleHide)
        th.hidden = !th.hidden;

    //Back / forward: LB / RB, the d-pad, the arrow keys (held: repeats)
    auto repeat = [](int& held, bool down) {
        if (!down) {
            held = 0;
            return false;
        }
        ++held;
        return held == 1 || (held > 18 && held % 5 == 0);
    };
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    const bool padBack = th.pad.buttons[prompts::LB] || th.pad.buttons[prompts::LEFT];
    const bool padForward = th.pad.buttons[prompts::RB] || th.pad.buttons[prompts::RIGHT];
    const bool back = repeat(th.holdBack, padBack) || th.skip < 0;
    const bool forward = repeat(th.holdForward, padForward) || th.skip > 0;
    if (back || forward) {
        const int amount = th.paused ? 1 : SKIP_TICKS;
        seekTo(g_sim.tick + (forward ? amount : -amount));
        showUi();
    }

    //Rewind (LT, Shift+Left held): backwards, faster the further it's pressed
    const float rewind = std::max(th.pad.lt, shift && keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
    const float fastForward = std::max(th.pad.rt, shift && keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f);
    if (rewind > 0.05f) {
        seekTo(g_sim.tick - static_cast<int>(2 + 6 * rewind));
        lastUpdateTicked = false;
        showUi();
    }

    //Play
    th.speedNow = 0;
    if (!th.paused && !status.finished && rewind <= 0.05f) {
        const float speed = fastForward > 0.05f ? std::max(SPEEDS[static_cast<size_t>(th.speed)], 2.0f + 6.0f * fastForward)
                                                : SPEEDS[static_cast<size_t>(th.speed)];
        th.speedNow = speed;
        th.carry += speed;
        int ticks = static_cast<int>(th.carry);
        th.carry -= static_cast<float>(ticks);
        while (ticks-- > 0) {
            if (!replay::step())
                break;
            lastUpdateTicked = true;
            th.smoothTick = g_sim.tick;
            //A fade to the end of the match: the transition comes first
            if (game_values.screenfade == 255)
                break;
        }
    }

    //The song recorded for this moment (after playing on, or a jump)
    replay::syncMusic();

    //--at-role theater (screenshots): follow, pause, the help, jump near the end
    if (cmd::g_autotest.enabled && cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "theater") {
        static int demo = 0;
        ++demo;
        if (demo == 30) { th.follow = nextFollow(-1); showUi(); }
        if (demo == 140) { th.paused = true; prompts::setLastDevice(prompts::Device::Controller); }
        if (demo == 155) th.help = true;
        if (demo == 165) { th.help = false; prompts::setLastDevice(prompts::Device::Keyboard); }
        if (demo == 180) { seekTo(status.total - 90); th.paused = false; th.speed = 3; showUi(); }
        g_theaterShot = demo == 100 ? "t1_playing_following" : demo == 150 ? "t2_paused_controller" : demo == 160 ? "t4_help" :
                        demo == 170 ? "t5_paused_keyboard" : "";
    }

    //--at-replay-seeks N: a random jump every N frames (the checksums after it must match)
    if (cmd::g_autotest.enabled && cmd::g_autotest.replaySeeks > 0 && !replay::playbackStatus().finished) {
        static uint32_t rng = 12345;
        static int frames = 0;
        static int seeks = 0;
        if (++frames % cmd::g_autotest.replaySeeks == 0 && seeks < 40) {
            rng = rng * 1664525u + 1013904223u;
            const int target = static_cast<int>((rng >> 8) % static_cast<uint32_t>(std::max(1, replay::playbackStatus().total)));
            printf("replay: seek from %d to %d\n", g_sim.tick, target);
            seekTo(target);
            seeks++;
        }
    }

    th.playPause = false;
    th.skip = 0;
    th.speedChange = 0;
    th.followNext = false;
    th.toggleHide = false;
    th.toggleHelp = false;
    if (th.uiFrames > 0 && !th.paused)
        th.uiFrames--;

    //The end: the theater stays (seek back, or leave); --autotest --replay leaves
    if (replay::playbackStatus().finished) {
        th.paused = true;
        if (cmd::g_autotest.enabled && cmd::g_autotest.shotsRole == "theater" && th.endFrames == 5)
            g_theaterShot = "t3_end";
        if (cmd::g_autotest.enabled && ++th.endFrames > 10) {
            leaveReplay();
            return;
        }
    }
    playMusic();
}

// Smooth motion in the theater, at every speed: how far between the previous
// tick and the latest one to draw. At 1/4x a tick comes every 4th update, so
// the way there is the fraction of a tick played since (carry), plus the time
// since the update at this speed; and as at full speed it doesn't depend on
// the frame rate. False: draw the latest tick as it is (paused, just jumped).
bool GameplayState::theaterAlpha(float& alpha) const
{
    const Theater& th = g_theater;
    if (th.smoothTick != g_sim.tick || th.speedNow <= 0)
        return false;
    alpha = std::clamp(th.carry + alpha * std::min(th.speedNow, 1.0f), 0.0f, 1.0f);
    return alpha < 1.0f;
}

void GameplayState::leaveReplay()
{
    CleanUp();
    g_theater.pads.close();
    g_theater = Theater();
    game_values.flags.exitinggame = false;
    game_values.flags.pausegame = false;
    game_values.appstate = AppState::Menu;
    game_values.screenfadespeed = -8;
    GameStateManager::instance().changeStateTo(&MenuState::instance());
}

void GameplayState::drawReplayOverlay()
{
    if (!replay::playing())
        return;
    Theater& th = g_theater;
    const replay::PlaybackStatus& status = replay::playbackStatus();
    const bool finished = status.finished;
    layout();

    //The followed player: their name over them, an arrow in their color
    if (th.follow >= 0) {
        for (const CPlayer* player : g_sim.players) {
            if (player->getGlobalID() != th.follow)
                continue;
            //(with the screen's shake, like the player under it)
            const int cx = player->leftX() + 16 + x_shake;
            const SDL_Color c = slotColor(th.follow);
            const int ay = std::max(22, player->topY() - 14) + y_shake;
            prompts::art(cx - 8, ay, {
                "########",
                "#cccccc#",
                ".#cCcc#.",
                "..#cc#..",
                "...##...",
            }, {{'#', BLACK}, {'c', c}, {'C', theme::shade(c, 150)}});
            theme::font().drawCentered(cx - x_shake, std::max(0, ay - 18) - y_shake, followName(th.follow));
        }
    }

    //The theater stays still when the game's screen shakes (a POW block, a thwomp...)
    struct NoShake {
        short x = x_shake;
        short y = y_shake;
        NoShake() { x_shake = y_shake = 0; }
        ~NoShake() { x_shake = x; y_shake = y; }
    } noShake;

    if (!barShown()) {
        //Just a reminder
        const int w = prompts::promptWidth(Pad::Back, "F1", "Replay") + 14;
        theme::strip(App::screenWidth - w - 4, App::screenHeight - 30, w, 28);
        prompts::drawPrompt(App::screenWidth - w + 3, App::screenHeight - 24, Pad::Back, "F1", "Replay");
        if (th.help)
            drawHelp();
        return;
    }

    //The dock: a panel along the bottom (its edges off the screen)
    theme::panel(-6, DOCK_Y, App::screenWidth + 12, App::screenHeight - DOCK_Y + 20);

    //The timeline: a line, white as far as the replay is; the score changes
    //are flags in the scoring team's color, the end a checkered flag; the
    //playhead a gold knob
    const int total = std::max(1, status.total);
    const int done = TRACK_W * std::clamp(g_sim.tick, 0, total) / total;
    theme::progressLine(TRACK_X - 4, TRACK_Y, TRACK_W + 8, static_cast<float>(std::clamp(g_sim.tick, 0, total)) / static_cast<float>(total));
    int lastFlag = -100;
    int lastKill = -100;
    for (const replay::Marker& m : replay::markers()) {
        const int mx = TRACK_X + TRACK_W * m.tick / total;
        //(one flag in 10 pixels, one kill mark in 8: in some modes the score changes all the time)
        if (m.delta > 0 && mx - lastFlag < 10)
            continue;
        if (m.delta < 0 && mx - lastKill < 8)
            continue;
        if (m.delta == 0) {
            theme::modeIcon(mx - 3, TRACK_Y - 18, 13);
        } else if (m.delta > 0) {
            lastFlag = mx;
            theme::flag(mx - 1, TRACK_Y - 14, teamColor(m.team));
        } else {
            //A kill: an outlined post in the team's color, standing on the line
            lastKill = mx;
            const SDL_Color c = teamColor(m.team);
            prompts::art(mx - 3, TRACK_Y - 8, {
                "###",
                "#l#",
                "#c#",
                "#c#",
                "#c#",
                "###",
            }, {{'#', {0, 0, 0, 255}}, {'c', c}, {'l', theme::shade(c, 150)}});
        }
    }
    drawPlayhead(TRACK_X + done, TRACK_Y + 8, th.dragging);
    //(the times: the status bar's timer)
    const std::string now = history::formatDuration(g_sim.tick);
    theme::timer(TRACK_X - 12 - theme::timerWidth(now), TRACK_Y, now);
    theme::timer(TRACK_X + TRACK_W + 12, TRACK_Y, history::formatDuration(status.total));

    //The buttons, each with its controller button or key
    int hovered = -1;
    for (int c = 0; c < CONTROL_COUNT; c++) {
        const SDL_Rect& r = th.rects[static_cast<size_t>(c)];
        const bool hover = prompts::lastDevice() == prompts::Device::Keyboard && inside(r, th.mouseX, th.mouseY);
        if (hover)
            hovered = c;
        const bool active = (c == FOLLOW && th.follow >= 0) || (c == HELP && th.help);
        drawButton(static_cast<Control>(c), r, hover, active);
        const ControlInfo& info = CONTROLS[static_cast<size_t>(c)];
        prompts::drawPrompt(r.x + r.w + 2, BUTTON_Y + 5, info.pad, info.key, "");
    }

    //(the mouse over the timeline: the time there)
    if (prompts::lastDevice() == prompts::Device::Keyboard && th.mouseY >= TRACK_Y - 10 && th.mouseY < TRACK_Y + TRACK_H + 6 &&
            th.mouseX >= TRACK_X && th.mouseX < TRACK_X + TRACK_W)
        theme::tooltip(static_cast<int>(th.mouseX), TRACK_Y - 6, history::formatDuration(tickAt(th.mouseX)));

    //Paused, or the end: a banner over the dock (the mode, the map, when)
    const bool titled = th.paused || finished;
    if (titled) {
        std::string title = (game_values.gamemode ? game_values.gamemode->GetModeName() : std::string("Replay"));
        if (!replay::mapName().empty())
            title += "  -  " + replay::mapName();
        const std::string when = history::formatTime(replay::startTime());
        const char* state = finished ? "The end" : "Paused";
        const int stateW = theme::font().getWidth(state) + 34;
        const int w = stateW + theme::font().getWidth(title) + theme::font().getWidth(when) + 96;
        const int x = (App::screenWidth - w) / 2;
        const int y = DOCK_Y - 46;
        //(a status bar, SMB3's)
        theme::strip(x, y, w, 46);
        theme::block(x + 12, y + 14, stateW, 18, finished ? theme::Tone::Gold : theme::Tone::Red);
        if (finished)
            theme::modeIcon(x + 15, y + 15, 13);
        else
            prompts::art(x + 17, y + 17, {"##.##", "##.##", "##.##", "##.##", "##.##", "##.##"}, {{'#', WHITE}});
        theme::font().draw(x + 36, y + 14, state);
        const int tx = x + 12 + stateW + 12;
        const short modeIndex = game_values.gamemode ? GetModeIconIndexFromMode(game_values.gamemode->gamemode) : -1;
        if (modeIndex >= 0)
            theme::modeIcon(tx, y + 15, modeIndex);
        theme::font().draw(tx + 22, y + 14, title);
        theme::font().drawRightJustified(x + w - 16, y + 14, when);
    }

    //The followed player's buttons
    if (th.follow >= 0) {
        const COutputControl& held = g_sim.playerKeys[static_cast<size_t>(th.follow)];
        const std::array<const CKeyState*, 6> pressed = {&held.game_left, &held.game_right, &held.game_jump, &held.game_down, &held.game_turbo, &held.game_powerup};
        const char* labels[] = {"<", ">", "J", "v", "R", "P"};
        const int panelW = 6 * 18 + 50 + theme::font().getWidth(followName(th.follow));
        const int panelX = App::screenWidth - panelW - 4;
        const int panelY = DOCK_Y - (titled ? 82 : 32);
        theme::strip(panelX, panelY - 2, panelW, 30);
        //(the player, as on the score panels)
        if (rm->spr_player[th.follow][PGFX_STANDING_R])
            rm->spr_player[th.follow][PGFX_STANDING_R].draw(panelX + 6, panelY - 6, {0, 0, 32, 32});
        theme::font().draw(panelX + 42, panelY + 4, followName(th.follow));
        int x = panelX + panelW - 6 * 18 - 4;
        for (int k = 0; k < 6; k++) {
            theme::block(x, panelY + 4, 16, 18, pressed[static_cast<size_t>(k)]->fDown ? slotTone(th.follow) : theme::Tone::Gray);
            theme::font().drawCentered(x + 8, panelY + 4, labels[k]);
            x += 18;
        }
    }

    //The tooltip of the button under the mouse
    if (hovered >= 0) {
        const SDL_Rect& r = th.rects[static_cast<size_t>(hovered)];
        theme::tooltip(r.x + r.w / 2, BUTTON_Y - 2, CONTROLS[static_cast<size_t>(hovered)].label);
    }
    if (status.diverged) {
        const char* warning = "This replay plays differently in this version of the game";
        const int w = theme::font().getWidth(warning) + 16;
        theme::block((App::screenWidth - w) / 2, 4, w, 22, theme::Tone::Red);
        theme::font().drawCentered(App::screenWidth / 2, 7, warning);
    }
    if (th.help)
        drawHelp();

    if (!g_theaterShot.empty()) {
        gfx_save_screenshot(cmd::g_autotest.shotsDir + "/" + g_theaterShot + ".png");
        g_theaterShot.clear();
    }
}

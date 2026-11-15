// THE THEATER: watching a replay (rollback/Replay.h)
//
// Like a film viewer: play and pause, slower and faster, skip back and
// forward, step frame by frame, rewind and fast forward, jump anywhere on the
// timeline (its markers are the score changes and the end of the match), and
// follow one player (their name over them, and the buttons they hold).
//
// Everything works with a controller (any connected one), the keyboard or the
// mouse; the bar's buttons show the controller button or the key that does
// the same (whichever was used last, see ui/InputPrompts.h):
//
//                       controller        keyboard      mouse
//   play / pause        A (or Start)      Space         the play button
//   back / forward 5 s  LB / RB           Left / Right  the << >> buttons
//     (paused: 1 frame)  (d-pad < >)
//   rewind / fast fwd   LT / RT (hold)    Shift+Left / Shift+Right (hold)
//   speed               d-pad up / down   Up / Down     the speed button (right click: slower)
//   follow a player     X                 F             the follow button
//   hide the bar        Y                 H             the hide button
//   the controls        Back              F1            the ? button
//   leave               B                 Escape        the X button
//   jump                                                click / drag on the timeline
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

// The layout (screen coordinates)
constexpr int BAR_Y = 448;
constexpr int BUTTON_Y = BAR_Y + 4;
constexpr int BUTTON_H = 24;
constexpr int TRACK_X = 70;
constexpr int TRACK_W = 500;
constexpr int TRACK_Y = 428;
constexpr int TRACK_H = 8;

constexpr SDL_Color WHITE {255, 255, 255, 255};
constexpr SDL_Color DARK {16, 16, 32, 255};

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

void box(int x, int y, int w, int h, SDL_Color inside)
{
    prompts::fill(x - 1, y - 1, w + 2, h + 2, {0, 0, 0, 255});
    prompts::fill(x, y, w, h, inside);
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

// The width of a button's icon box
int buttonWidth(Control c)
{
    return c == SPEED ? 36 : 26;
}

// Lays out the buttons: the play controls on the left, the others on the right
void layout()
{
    Theater& th = g_theater;
    int x = 8;
    for (int c = BACK5; c <= SPEED; c++) {
        const int w = buttonWidth(static_cast<Control>(c));
        th.rects[static_cast<size_t>(c)] = {x, BUTTON_Y, w, BUTTON_H};
        x += w + prompts::promptWidth(CONTROLS[static_cast<size_t>(c)].pad, CONTROLS[static_cast<size_t>(c)].key, "") + 8;
    }
    x = App::screenWidth - 6;
    for (int c = LEAVE; c >= FOLLOW; c--) {
        const int w = buttonWidth(static_cast<Control>(c));
        x -= prompts::promptWidth(CONTROLS[static_cast<size_t>(c)].pad, CONTROLS[static_cast<size_t>(c)].key, "") + 2 + w;
        th.rects[static_cast<size_t>(c)] = {x, BUTTON_Y, w, BUTTON_H};
        x -= 8;
    }
}

void drawIcon(Control c, const SDL_Rect& r, bool hover, bool active)
{
    const Theater& th = g_theater;
    prompts::frame(r.x, r.y, r.w, r.h, hover ? SDL_Color {255, 232, 120, 255} : (active ? SDL_Color {120, 220, 96, 255} : SDL_Color {220, 220, 235, 255}),
        hover ? SDL_Color {70, 70, 120, 255} : SDL_Color {36, 36, 72, 255});
    const int cx = r.x + r.w / 2;
    const int cy = r.y + r.h / 2;
    switch (c) {
        case BACK5:
            prompts::triangle(cx - 9, cy - 6, 8, 12, false, WHITE);
            prompts::triangle(cx - 1, cy - 6, 8, 12, false, WHITE);
            break;
        case FORWARD5:
            prompts::triangle(cx - 7, cy - 6, 8, 12, true, WHITE);
            prompts::triangle(cx + 1, cy - 6, 8, 12, true, WHITE);
            break;
        case PLAY:
            if (th.paused || replay::playbackStatus().finished) {
                prompts::triangle(cx - 4, cy - 7, 10, 14, true, WHITE);
            } else {
                prompts::fill(cx - 6, cy - 6, 4, 13, WHITE);
                prompts::fill(cx + 2, cy - 6, 4, 13, WHITE);
            }
            break;
        case SPEED:
            rm->menu_font_small.drawCentered(cx, r.y + 5, SPEED_NAMES[th.speed]);
            break;
        case FOLLOW:
            //A player: head and shoulders
            prompts::disc(cx, cy - 4, 4, active ? slotColor(th.follow) : WHITE);
            prompts::fill(cx - 7, cy + 2, 15, 6, active ? slotColor(th.follow) : WHITE);
            break;
        case HIDE:
            //An eye
            for (int dx = -9; dx <= 9; dx++) {
                const int half = static_cast<int>(5.0f * std::sqrt(1.0f - (dx * dx) / 81.0f));
                prompts::fill(cx + dx, cy - half, 1, 2 * half + 1, WHITE);
            }
            prompts::disc(cx, cy, 3, {36, 36, 72, 255});
            break;
        case HELP:
            rm->menu_font_large.drawCentered(cx, r.y + 2, "?");
            break;
        case LEAVE:
            prompts::line(cx - 6, cy - 6, cx + 6, cy + 6, 3, WHITE);
            prompts::line(cx - 6, cy + 6, cx + 6, cy - 6, 3, WHITE);
            break;
        default:
            break;
    }
}

void drawHelp()
{
    const int w = 560;
    const int h = 300;
    const int x = (App::screenWidth - w) / 2;
    const int y = 70;
    prompts::frame(x - 2, y - 2, w + 4, h + 4, WHITE, {24, 24, 52, 255});
    rm->menu_font_large.drawCentered(App::screenWidth / 2, y + 8, "Theater");
    rm->menu_font_small.draw(x + 250, y + 36, "Controller");
    rm->menu_font_small.draw(x + 380, y + 36, "Keyboard");
    struct Row {
        const char* what;
        std::vector<Pad> pads;
        std::vector<std::string> keys;
    };
    const Row rows[] = {
        {"Play / pause", {Pad::A}, {"Space"}},
        {"Back / forward 5 s", {Pad::LB, Pad::RB}, {"Left", "Right"}},
        {"  paused: one frame", {Pad::Left, Pad::Right}, {"Left", "Right"}},
        {"Rewind / fast fwd (hold)", {Pad::LT, Pad::RT}, {"Shift", "Left", "Right"}},
        {"Slower / faster", {Pad::Down, Pad::Up}, {"Down", "Up"}},
        {"Follow a player", {Pad::X}, {"F"}},
        {"Hide the bar", {Pad::Y}, {"H"}},
        {"These controls", {Pad::Back}, {"F1"}},
        {"Leave", {Pad::B}, {"Esc"}},
        {"Jump in the replay", {}, {}},
    };
    int ry = y + 58;
    for (const Row& row : rows) {
        rm->menu_font_small.draw(x + 16, ry + 1, row.what);
        int px = x + 250;
        for (Pad p : row.pads)
            px += prompts::drawPad(px, ry, p) + 3;
        int kx = x + 380;
        for (const std::string& k : row.keys)
            kx += prompts::drawKey(kx, ry, k) + 3;
        if (row.pads.empty()) {
            prompts::drawMouse(x + 250, ry, true, false);
            rm->menu_font_small.draw(x + 266, ry + 1, "click or drag on the timeline");
        }
        ry += 22;
    }
    rm->menu_font_small.drawCentered(App::screenWidth / 2, y + h - 22, "Close: the same button again");
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
                replay::seek(0);
            else if (key == SDLK_END)
                replay::seek(replay::playbackStatus().total);
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
                replay::seek(tickAt(x));
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
                replay::seek(tickAt(event.motion.x));
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
            replay::seek(0);    //(from the start again)
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
        replay::seek(g_sim.tick + (forward ? amount : -amount));
        showUi();
    }

    //Rewind (LT, Shift+Left held): backwards, faster the further it's pressed
    const float rewind = std::max(th.pad.lt, shift && keys[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f);
    const float fastForward = std::max(th.pad.rt, shift && keys[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f);
    if (rewind > 0.05f) {
        replay::seek(g_sim.tick - static_cast<int>(2 + 6 * rewind));
        lastUpdateTicked = false;
        showUi();
    }

    //Play
    if (!th.paused && !status.finished && rewind <= 0.05f) {
        const float speed = fastForward > 0.05f ? std::max(SPEEDS[static_cast<size_t>(th.speed)], 2.0f + 6.0f * fastForward)
                                                : SPEEDS[static_cast<size_t>(th.speed)];
        th.carry += speed;
        int ticks = static_cast<int>(th.carry);
        th.carry -= static_cast<float>(ticks);
        while (ticks-- > 0) {
            if (!replay::step())
                break;
            lastUpdateTicked = true;
            //A fade to the end of the match: the transition comes first
            if (game_values.screenfade == 255)
                break;
        }
    }

    //--at-role theater (screenshots): follow, pause, the help, jump near the end
    if (cmd::g_autotest.enabled && cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "theater") {
        static int demo = 0;
        ++demo;
        if (demo == 30) { th.follow = nextFollow(-1); showUi(); }
        if (demo == 140) { th.paused = true; prompts::setLastDevice(prompts::Device::Controller); }
        if (demo == 155) th.help = true;
        if (demo == 165) { th.help = false; prompts::setLastDevice(prompts::Device::Keyboard); }
        if (demo == 180) { replay::seek(status.total - 90); th.paused = false; th.speed = 3; showUi(); }
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
            replay::seek(target);
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
            const int cx = player->leftX() + 16;
            const int top = player->topY();
            const SDL_Color c = slotColor(th.follow);
            const int ay = std::max(14, top - 12);
            for (int row = 0; row < 6; row++)
                prompts::fill(cx - 6 + row, ay + row, 12 - 2 * row, 1, c);
            rm->menu_font_small.drawCentered(cx, std::max(0, ay - 16), followName(th.follow));
        }
    }

    if (!barShown()) {
        //Just a reminder
        const int w = prompts::promptWidth(Pad::Back, "F1", "Replay");
        prompts::drawPrompt(App::screenWidth - w - 6, App::screenHeight - 20, Pad::Back, "F1", "Replay");
        if (th.help)
            drawHelp();
        return;
    }

    //A dark band behind the timeline (and the title while paused)
    const bool titled = th.paused || finished;
    const int bandTop = titled ? TRACK_Y - 46 : TRACK_Y - 14;
    prompts::fill(0, bandTop, App::screenWidth, BAR_Y - bandTop, DARK);

    //The bar: the menus' header strip, the buttons with their prompts
    rm->menu_plain_field.draw(0, BAR_Y, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, BAR_Y, {192, 0, 320, 32});
    int hovered = -1;
    for (int c = 0; c < CONTROL_COUNT; c++) {
        const SDL_Rect& r = th.rects[static_cast<size_t>(c)];
        const bool hover = prompts::lastDevice() == prompts::Device::Keyboard && inside(r, th.mouseX, th.mouseY);
        if (hover)
            hovered = c;
        const bool active = (c == FOLLOW && th.follow >= 0) || (c == HELP && th.help);
        drawIcon(static_cast<Control>(c), r, hover, active);
        const ControlInfo& info = CONTROLS[static_cast<size_t>(c)];
        prompts::drawPrompt(r.x + r.w + 2, BUTTON_Y + 4, info.pad, info.key, "");
    }

    //The time
    //(between the two groups of buttons; smaller if it doesn't fit)
    const std::string time = finished ? std::string("End") : history::formatDuration(g_sim.tick) + " / " + history::formatDuration(status.total);
    const SDL_Rect& lastLeft = th.rects[SPEED];
    const int gapFrom = lastLeft.x + lastLeft.w + prompts::promptWidth(CONTROLS[SPEED].pad, CONTROLS[SPEED].key, "") + 4;
    const int gapTo = th.rects[FOLLOW].x - 4;
    if (rm->menu_font_large.getWidth(time) <= gapTo - gapFrom)
        rm->menu_font_large.drawCentered((gapFrom + gapTo) / 2, BAR_Y + 6, time);
    else
        rm->menu_font_small.drawCentered((gapFrom + gapTo) / 2, BAR_Y + 9, time);

    //The timeline: progress, markers, the playhead
    box(TRACK_X, TRACK_Y, TRACK_W, TRACK_H, {32, 32, 48, 255});
    const int total = std::max(1, status.total);
    const int done = TRACK_W * std::clamp(g_sim.tick, 0, total) / total;
    prompts::fill(TRACK_X, TRACK_Y, done, TRACK_H, {72, 184, 56, 255});
    for (const replay::Marker& m : replay::markers()) {
        const int mx = TRACK_X + TRACK_W * m.tick / total;
        if (m.delta == 0)    //the end of the match: a gold flag
            box(mx - 2, TRACK_Y - 9, 5, 7, {248, 216, 32, 255});
        else
            prompts::fill(mx, TRACK_Y - (m.delta > 0 ? 5 : 3), 2, m.delta > 0 ? 4 : 2, teamColor(m.team));
    }
    const int px = TRACK_X + done;
    box(px - 3, TRACK_Y - 5, 7, TRACK_H + 10, WHITE);
    rm->menu_font_small.drawRightJustified(TRACK_X - 6, TRACK_Y - 4, history::formatDuration(g_sim.tick));
    rm->menu_font_small.draw(TRACK_X + TRACK_W + 6, TRACK_Y - 4, history::formatDuration(status.total));
    //(the mouse over the timeline: the time there)
    if (prompts::lastDevice() == prompts::Device::Keyboard && th.mouseY >= TRACK_Y - 10 && th.mouseY < TRACK_Y + TRACK_H + 6 &&
            th.mouseX >= TRACK_X && th.mouseX < TRACK_X + TRACK_W) {
        const std::string at = history::formatDuration(tickAt(th.mouseX));
        const int w = rm->menu_font_small.getWidth(at) + 8;
        const int tx = std::clamp(static_cast<int>(th.mouseX) - w / 2, 0, App::screenWidth - w);
        box(tx, TRACK_Y - 26, w, 16, {48, 48, 96, 255});
        rm->menu_font_small.draw(tx + 4, TRACK_Y - 26, at);
    }

    //The followed player's buttons
    if (th.follow >= 0) {
        const COutputControl& held = g_sim.playerKeys[static_cast<size_t>(th.follow)];
        const std::array<const CKeyState*, 6> pressed = {&held.game_left, &held.game_right, &held.game_jump, &held.game_down, &held.game_turbo, &held.game_powerup};
        const char* labels[] = {"<", ">", "J", "v", "R", "P"};
        const int panelW = 6 * 17 + 8 + rm->menu_font_small.getWidth(followName(th.follow)) + 8;
        const int panelX = App::screenWidth - panelW - 4;
        const int panelY = TRACK_Y - (titled ? 66 : 34);
        box(panelX, panelY, panelW, 20, {24, 24, 52, 255});
        rm->menu_font_small.draw(panelX + 6, panelY + 2, followName(th.follow));
        int x = panelX + panelW - 6 * 17 - 2;
        for (int k = 0; k < 6; k++) {
            box(x, panelY + 2, 14, 16, pressed[static_cast<size_t>(k)]->fDown ? slotColor(th.follow) : SDL_Color {40, 40, 64, 255});
            rm->menu_font_small.drawCentered(x + 7, panelY + 2, labels[k]);
            x += 17;
        }
    }

    //The title while paused; the tooltip of the button under the mouse
    if (titled) {
        std::string title = (game_values.gamemode ? game_values.gamemode->GetModeName() : std::string("Replay"));
        if (!replay::mapName().empty())
            title += "  -  " + replay::mapName();
        title += "    " + history::formatTime(replay::startTime());
        rm->menu_font_small.drawCentered(App::screenWidth / 2, TRACK_Y - 40, title);
        rm->menu_font_small.drawCentered(App::screenWidth / 2, TRACK_Y - 22, finished ? "The end: play to watch again" : "Paused");
    }
    if (hovered >= 0) {
        const char* label = CONTROLS[static_cast<size_t>(hovered)].label;
        const int w = rm->menu_font_small.getWidth(label) + 10;
        const SDL_Rect& r = th.rects[static_cast<size_t>(hovered)];
        const int tx = std::clamp(r.x + r.w / 2 - w / 2, 0, App::screenWidth - w);
        box(tx, BAR_Y - 22, w, 18, {48, 48, 96, 255});
        rm->menu_font_small.draw(tx + 5, BAR_Y - 21, label);
    }
    if (status.diverged)
        rm->menu_font_small.drawCentered(App::screenWidth / 2, 4, "This replay plays differently in this version of the game");
    if (th.help)
        drawHelp();

    if (!g_theaterShot.empty()) {
        gfx_save_screenshot(cmd::g_autotest.shotsDir + "/" + g_theaterShot + ".png");
        g_theaterShot.clear();
    }
}

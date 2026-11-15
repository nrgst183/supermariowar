// THE THEATER: watching a replay (rollback/Replay.h)
//
// Like a film viewer: play and pause, slower and faster, skip back and
// forward, step frame by frame, jump anywhere on the timeline (its markers
// are the score changes and the end of the match), and follow one player
// (their name over them, and the buttons they hold). The bar at the bottom
// hides itself while the replay plays; any input shows it again.
//
// Controls (player 1's game keys, or the mouse):
//   Jump or Start        play / pause
//   Left / Right         skip 5 seconds back / forward (paused: one frame; held: repeats)
//   Down                 the next speed (1/4, 1/2, 1, 2, 4, 8)
//   Run (held)           fast forward (at least 4x)
//   Powerup              follow the next player (then nobody)
//   Escape / cancel      leave (at the end, right away)
//   H                    hide or show the bar
//   click / drag         on the timeline: jump there

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

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

constexpr std::array<float, 6> SPEEDS = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
constexpr const char* SPEED_NAMES[] = {"1/4x", "1/2x", "1x", "2x", "4x", "8x"};
constexpr int NORMAL_SPEED = 2;
constexpr int SKIP_TICKS = 313;        // 5 seconds
constexpr int UI_SHOW_FRAMES = 200;    // after the last input

// The timeline (screen coordinates)
constexpr int BAR_Y = 448;
constexpr int TRACK_X = 80;
constexpr int TRACK_W = 480;
constexpr int TRACK_Y = 431;
constexpr int TRACK_H = 8;

struct Theater {
    bool paused = false;
    int speed = NORMAL_SPEED;
    float carry = 0;           // fractions of a tick (slow speeds)
    int uiFrames = UI_SHOW_FRAMES;
    bool hidden = false;       // H
    int follow = -1;           // the followed slot, -1: nobody
    int holdLeft = 0;
    int holdRight = 0;
    bool dragging = false;
    int endFrames = 0;
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
    return {255, 255, 255, 255};
}

void fill(int x, int y, int w, int h, SDL_Color c)
{
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, c.r, c.g, c.b));
}

void box(int x, int y, int w, int h, SDL_Color inside)
{
    fill(x - 1, y - 1, w + 2, h + 2, {0, 0, 0, 255});
    fill(x, y, w, h, inside);
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

void showUi()
{
    g_theater.uiFrames = UI_SHOW_FRAMES;
}

} // namespace

void GameplayState::resetTheater()
{
    g_theater = Theater();
}

void GameplayState::theaterEvent(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_H && !event.key.repeat)
                g_theater.hidden = !g_theater.hidden;
            showUi();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT && event.button.y >= TRACK_Y - 10 && event.button.y < BAR_Y + 2 &&
                    event.button.x >= TRACK_X - 8 && event.button.x < TRACK_X + TRACK_W + 8) {
                g_theater.dragging = true;
                replay::seek(tickAt(event.button.x));
                lastUpdateTicked = false;
            }
            showUi();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (g_theater.dragging && (event.motion.state & SDL_BUTTON_LMASK)) {
                replay::seek(tickAt(event.motion.x));
                lastUpdateTicked = false;
            }
            showUi();
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            g_theater.dragging = false;
            break;
        default:
            break;
    }
}

void GameplayState::updateReplay()
{
    lastUpdateTicked = false;
    handleInput();
    if (game_values.appstate == AppState::Quit)
        return;

    //Player 1's keys (any device that drives player 1)
    COutputControl& keys = game_values.playerInput.outputControls[0];
    const replay::PlaybackStatus& status = replay::playbackStatus();

    //At the end, leaving needs no confirmation
    if (status.finished && !game_values.flags.exitinggame && keys.game_cancel.fPressed) {
        leaveReplay();
        return;
    }
    //Start is ours (play / pause), not the pause dialog's
    const bool startPressed = keys.game_start.fPressed;
    keys.game_start.fPressed = false;
    if (updateExitPauseDialog(g_sim.countdownState)) {
        GameStateManager::instance().changeStateTo(&MenuState::instance());
        return;
    }
    if (game_values.flags.exitinggame || game_values.flags.pausegame) {
        playMusic();
        return;
    }
    if (updateMatchTransition())
        return;

    Theater& th = g_theater;
    bool input = false;
    if (startPressed || keys.game_jump.fPressed) {
        if (status.finished) {
            replay::seek(0);    //(from the start again)
            th.paused = false;
        } else {
            th.paused = !th.paused;
        }
        input = true;
    }
    if (keys.game_down.fPressed) {
        th.speed = (th.speed + 1) % static_cast<int>(SPEEDS.size());
        th.carry = 0;
        input = true;
    }
    if (keys.game_powerup.fPressed) {
        th.follow = nextFollow(th.follow);
        input = true;
    }

    //Left / right: skip (paused: a frame); held, it repeats
    auto repeat = [](int& held, bool down) {
        if (!down) {
            held = 0;
            return false;
        }
        ++held;
        return held == 1 || (held > 18 && held % 5 == 0);
    };
    const bool back = repeat(th.holdLeft, keys.game_left.fDown);
    const bool forward = repeat(th.holdRight, keys.game_right.fDown);
    if (back || forward) {
        const int amount = th.paused ? 1 : SKIP_TICKS;
        replay::seek(g_sim.tick + (forward ? amount : -amount));
        input = true;
    }

    //--at-role theater (screenshots): follow, pause, jump near the end
    if (cmd::g_autotest.enabled && cmd::g_autotest.kind == cmd::AutoTestArgs::Kind::Shots && cmd::g_autotest.shotsRole == "theater") {
        static int demo = 0;
        ++demo;
        if (demo == 30) { th.follow = nextFollow(-1); input = true; }
        if (demo == 140) { th.paused = true; input = true; }
        if (demo == 160) { replay::seek(status.total - 90); th.paused = false; th.speed = 3; input = true; }
        g_theaterShot = demo == 100 ? "t1_playing_following" : demo == 150 ? "t2_paused" : "";
    }

    //--at-replay-seeks N: a random jump every N frames (the checksums after it must match)
    if (cmd::g_autotest.enabled && cmd::g_autotest.replaySeeks > 0 && !status.finished) {
        static uint32_t rng = 12345;
        static int frames = 0;
        static int seeks = 0;
        if (++frames % cmd::g_autotest.replaySeeks == 0 && seeks < 40) {
            rng = rng * 1664525u + 1013904223u;
            const int target = static_cast<int>((rng >> 8) % static_cast<uint32_t>(std::max(1, status.total)));
            printf("replay: seek from %d to %d\n", g_sim.tick, target);
            replay::seek(target);
            seeks++;
        }
    }

    //Play
    if (!th.paused && !status.finished) {
        const float speed = keys.game_turbo.fDown ? std::max(SPEEDS[static_cast<size_t>(th.speed)], 4.0f) : SPEEDS[static_cast<size_t>(th.speed)];
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

    if (input || keys.game_turbo.fDown)
        showUi();
    else if (th.uiFrames > 0 && !th.paused)
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
    g_theater = Theater();
    game_values.flags.exitinggame = false;
    game_values.flags.pausegame = false;
    game_values.appstate = AppState::Menu;
    game_values.screenfadespeed = -8;
    GameStateManager::instance().changeStateTo(&MenuState::instance());
}

void GameplayState::drawReplayOverlay()
{
    if (!replay::playing() || game_values.flags.exitinggame || game_values.flags.pausegame)
        return;
    const Theater& th = g_theater;
    const replay::PlaybackStatus& status = replay::playbackStatus();
    const bool finished = status.finished;

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
                fill(cx - 6 + row, ay + row, 12 - 2 * row, 1, c);
            rm->menu_font_small.drawCentered(cx, std::max(0, ay - 16), followName(th.follow));
        }
    }

    const bool showBar = !th.hidden && (th.paused || finished || th.uiFrames > 0);
    if (!showBar) {
        //Just a small reminder
        rm->menu_font_small.drawRightJustified(App::screenWidth - 4, App::screenHeight - 16, "Replay");
        return;
    }

    //A dark band behind the timeline (and the title while paused)
    const int bandTop = (th.paused || finished) ? TRACK_Y - 42 : TRACK_Y - 14;
    fill(0, bandTop, App::screenWidth, BAR_Y - bandTop, {16, 16, 32, 255});

    //The bar: the menus' header strip
    rm->menu_plain_field.draw(0, BAR_Y, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, BAR_Y, {192, 0, 320, 32});

    //Play state and speed
    const SDL_Color white {255, 255, 255, 255};
    if (finished) {
        rm->menu_font_large.draw(10, BAR_Y + 6, "End");
    } else if (th.paused) {
        box(14, BAR_Y + 9, 5, 15, white);
        box(23, BAR_Y + 9, 5, 15, white);
        rm->menu_font_small.draw(36, BAR_Y + 10, "Paused");
    } else {
        for (int row = 0; row < 15; row++) {
            const int half = row < 8 ? row : 14 - row;
            fill(14, BAR_Y + 9 + row, half + 1, 1, white);
        }
        rm->menu_font_small.draw(36, BAR_Y + 10, SPEED_NAMES[th.speed]);
    }

    //The time
    const std::string time = history::formatDuration(g_sim.tick) + " / " + history::formatDuration(status.total);
    rm->menu_font_large.drawCentered(App::screenWidth / 2, BAR_Y + 6, time);

    //The followed player's buttons, or what the item button does
    if (th.follow >= 0) {
        const COutputControl& held = g_sim.playerKeys[static_cast<size_t>(th.follow)];
        const std::array<const CKeyState*, 6> pressed = {&held.game_left, &held.game_right, &held.game_jump, &held.game_down, &held.game_turbo, &held.game_powerup};
        const char* labels[] = {"<", ">", "J", "v", "R", "P"};
        int x = App::screenWidth - 10 - 6 * 17;
        rm->menu_font_small.drawRightJustified(x - 6, BAR_Y + 10, followName(th.follow));
        for (int k = 0; k < 6; k++) {
            box(x, BAR_Y + 8, 14, 16, pressed[static_cast<size_t>(k)]->fDown ? slotColor(th.follow) : SDL_Color {40, 40, 64, 255});
            rm->menu_font_small.drawCentered(x + 7, BAR_Y + 10, labels[k]);
            x += 17;
        }
    } else {
        rm->menu_font_small.drawRightJustified(App::screenWidth - 10, BAR_Y + 10, "Item: follow a player");
    }

    //The timeline: progress, markers, the playhead
    box(TRACK_X, TRACK_Y, TRACK_W, TRACK_H, {32, 32, 48, 255});
    const int total = std::max(1, status.total);
    const int done = TRACK_W * std::clamp(g_sim.tick, 0, total) / total;
    fill(TRACK_X, TRACK_Y, done, TRACK_H, {72, 184, 56, 255});
    for (const replay::Marker& m : replay::markers()) {
        const int mx = TRACK_X + TRACK_W * m.tick / total;
        if (m.delta == 0)    //the end of the match: a gold flag
            box(mx - 2, TRACK_Y - 9, 5, 7, {248, 216, 32, 255});
        else
            fill(mx, TRACK_Y - (m.delta > 0 ? 5 : 3), 2, m.delta > 0 ? 4 : 2, teamColor(m.team));
    }
    const int px = TRACK_X + done;
    box(px - 2, TRACK_Y - 4, 5, TRACK_H + 8, white);
    rm->menu_font_small.drawRightJustified(TRACK_X - 8, TRACK_Y - 4, history::formatDuration(g_sim.tick));
    rm->menu_font_small.draw(TRACK_X + TRACK_W + 8, TRACK_Y - 4, history::formatDuration(status.total));

    //The title and the controls, while paused
    if (th.paused || finished) {
        std::string title = (game_values.gamemode ? game_values.gamemode->GetModeName() : std::string("Replay"));
        if (!replay::mapName().empty())
            title += "  -  " + replay::mapName();
        title += "    " + history::formatTime(replay::startTime());
        rm->menu_font_small.drawCentered(App::screenWidth / 2, TRACK_Y - 36, title);
        rm->menu_font_small.drawCentered(App::screenWidth / 2, TRACK_Y - 20,
            finished ? "Jump: watch again    Left: back    Esc: leave"
                     : "Jump: play  <>: frame  Down: speed  Item: follow  Esc: leave");
    }
    if (status.diverged)
        rm->menu_font_small.drawCentered(App::screenWidth / 2, 4, "This replay plays differently in this version of the game");

    if (!g_theaterShot.empty()) {
        gfx_save_screenshot(cmd::g_autotest.shotsDir + "/" + g_theaterShot + ".png");
        g_theaterShot.clear();
    }
}

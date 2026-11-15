#include "NetOverlay.h"

#include "CmdArgs.h"
#include "Game.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "gfx.h"
#include "rollback/NetGame.h"
#include "rollback/Replay.h"
#include "ui/InputPrompts.h"
#include "ui/SmwTheme.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace {

// Short stalls (a late packet) are normal and must not flicker
constexpr uint32_t SHOW_WAIT_AFTER_MS = 150;
// From here on, tell what happens at the timeout
constexpr uint32_t SHOW_HINT_AFTER_MS = 3000;
// "X disconnected - CPU takes over" stays this long
constexpr uint32_t SHOW_DROP_MS = 4000;


std::string waitingNames(const rollback::NetGame& net, uint8_t peers)
{
    std::string names;
    int count = 0;
    for (int p = 0; p < 4; p++) {
        if (!(peers & (1u << p)))
            continue;
        std::string name = net.lobby() ? net.lobby()->peer(p).name : std::string();
        if (name.empty())
            name = "Player " + std::to_string(p + 1);
        if (count++)
            names += ", ";
        names += name;
    }
    return names;
}

// "Waiting for <names>": a panel above the middle, a spinner, how long, and a pipe running down to the
// moment the CPU takes over
void drawWaitingBox(const rollback::NetGame& net, const rollback::NetGame::WaitInfo& wait)
{
    const int w = 300;
    const int h = 150;
    const int x = (App::screenWidth - w) / 2;
    const int y = (App::screenHeight - h) / 2 - 50;    // above the middle: the action is there
    const int cx = App::screenWidth / 2;

    theme::panel(x, y, w, h);
    theme::spinner(x + 44, y + 44, static_cast<int>(wait.waitingMs / 16));
    theme::bigFont().draw(x + 72, y + 22, "Waiting for");

    //The names may not fit: chop them
    const std::string names = waitingNames(net, wait.peers);
    theme::bigFont().drawChopRight(x + 72, y + 48, w - 96, names);

    const std::string seconds = std::format("{:.1f} s", static_cast<double>(wait.waitingMs) / 1000.0);
    theme::font().drawCentered(cx, y + 80, seconds);

    //The time left until the CPU takes over: a pipe, green while there's time
    const uint32_t timeoutMs = static_cast<uint32_t>(std::max(1000, cmd::g_net.timeoutMs));
    const float left = 1.0f - std::min(1.0f, static_cast<float>(wait.waitingMs) / static_cast<float>(timeoutMs));
    const int pipeX = x + 30;
    const int pipeW = w - 60 - 8;
    const int done = static_cast<int>(static_cast<float>(pipeW) * left);
    theme::pipeLip(pipeX - 8, y + 104, true);
    theme::pipe(pipeX, y + 104, done, true);
    theme::pipe(pipeX + done, y + 104, pipeW - done, false);
    theme::pipeLip(pipeX + pipeW, y + 104, false);
    if (wait.waitingMs >= SHOW_HINT_AFTER_MS)
        theme::font().drawCentered(cx, y + 124, std::format("CPU takes over in {} s", (timeoutMs - std::min(timeoutMs, wait.waitingMs) + 999) / 1000));
}

// Rollbacks per second, over the last second or so
float rollbacksPerSecond(const rollback::NetGame& net)
{
    static int64_t lastCount = 0;
    static uint64_t lastMs = 0;
    static float rate = 0;

    const rollback::RollbackSession::Stats* stats = net.matchStats();
    if (!stats)
        return 0;
    const uint64_t now = SDL_GetTicks();
    if (lastMs == 0 || now < lastMs || stats->rollbacks < lastCount) {    //first call, or a new match
        lastMs = now;
        lastCount = stats->rollbacks;
        rate = 0;
    } else if (now - lastMs >= 1000) {
        rate = static_cast<float>(stats->rollbacks - lastCount) * 1000.0f / static_cast<float>(now - lastMs);
        lastMs = now;
        lastCount = stats->rollbacks;
    }
    return rate;
}

// The connection: each other player's name, signal bars and ping, the input delay and the rollbacks,
// in a bar under the score panels (the FPS line owns the bottom)
void drawNetStats(const rollback::NetGame& net)
{
    struct Item {
        std::string name;
        int ping;
    };
    std::vector<Item> items;
    for (int p = 0; p < 4; p++) {
        if (p == net.localPeer() || !net.lobby() || !net.lobby()->peer(p).present)
            continue;
        std::string name = net.lobby()->peer(p).name;
        if (name.size() > 10)
            name.resize(10);
        items.push_back({name, net.peerPingMs(p)});
    }
    const std::string tail = std::format("Delay {}   Rollbacks {:.1f}/s", net.inputDelay(), rollbacksPerSecond(net));

    const gfxFont& font = theme::font();
    int w = 16 + font.getWidth(tail);
    for (const Item& item : items)
        w += font.getWidth(item.name) + 28 + font.getWidth(item.ping >= 0 ? std::format("{} ms", item.ping) : std::string("? ms")) + 16;
    const int x = (App::screenWidth - w) / 2;
    const int y = 40;
    theme::strip(x - 4, y, w + 8, 34);
    int ix = x + 6;
    for (const Item& item : items) {
        font.draw(ix, y + 8, item.name);
        ix += font.getWidth(item.name) + 6;
        theme::signal(ix, y + 9, item.ping);
        ix += 28;
        const std::string ms = item.ping >= 0 ? std::format("{} ms", item.ping) : std::string("? ms");
        font.draw(ix, y + 8, ms);
        ix += font.getWidth(ms) + 16;
    }
    font.draw(ix, y + 8, tail);
}

} // namespace

void drawNetOverlay()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.inMatch())
        return;

    //(still when the screen shakes)
    struct NoShake {
        short x = x_shake;
        short y = y_shake;
        NoShake() { x_shake = y_shake = 0; }
        ~NoShake() { x_shake = x; y_shake = y; }
    } noShake;

    if (game_values.shownetstats)
        drawNetStats(net);

    const rollback::NetGame::DropNotice drop = net.dropNotice();
    if (!drop.text.empty() && drop.ageMs < SHOW_DROP_MS) {
        //A banner: who left, the CPU takes over
        const int w = theme::bigFont().getWidth(drop.text) + 54;
        const int x = (App::screenWidth - w) / 2;
        theme::strip(x, 70, w, 36);
        theme::statusIcon(x + 12, 80, theme::Status::Warn);
        theme::bigFont().draw(x + 36, 76, drop.text);
    }

    const rollback::NetGame::WaitInfo wait = net.waitInfo();
    if (wait.waiting && wait.waitingMs >= SHOW_WAIT_AFTER_MS)
        drawWaitingBox(net, wait);
}

void drawPingMeter(int slot, int x, int y)
{
    if (!game_values.showpingbars || slot < 0 || slot >= 4)
        return;
    int ping = -1;
    if (replay::playing()) {
        ping = replay::pingAt(slot);
        if (ping < 0)
            return;
    } else {
        const rollback::NetGame& net = rollback::NetGame::instance();
        const rollback::NetLobby* lobby = net.lobby();
        if (!net.inMatch() || !lobby)
            return;
        const rollback::LobbySlot& s = lobby->slot(slot);
        if (s.kind != rollback::SlotKind::Human || s.peer < 0 || s.peer == net.localPeer() || !lobby->peer(s.peer).present)
            return;
        ping = net.peerPingMs(s.peer);
    }
    //(with the shake of the score panels: the shapes don't shake by themselves)
    theme::signalSmall(x + x_shake - 4, y + y_shake - 2, ping);
}

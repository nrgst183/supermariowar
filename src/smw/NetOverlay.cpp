#include "NetOverlay.h"

#include "CmdArgs.h"
#include "Game.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

namespace {

// Short stalls (a late packet) are normal and must not flicker
constexpr uint32_t SHOW_WAIT_AFTER_MS = 150;
// From here on, tell what happens at the timeout
constexpr uint32_t SHOW_HINT_AFTER_MS = 3000;
// "X disconnected - CPU takes over" stays this long
constexpr uint32_t SHOW_DROP_MS = 4000;

constexpr int DIALOG_W = 192;
constexpr int DIALOG_H = 128;

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

void drawWaitingBox(const rollback::NetGame& net, const rollback::NetGame::WaitInfo& wait)
{
    const int x = (App::screenWidth - DIALOG_W) / 2;
    const int y = (App::screenHeight - DIALOG_H) / 2 - 40;    // above the middle: the action is there
    const int cx = App::screenWidth / 2;

    rm->spr_dialog.draw(x, y);
    rm->menu_font_large.drawCentered(cx, y + 16, "Waiting for");

    //The names may not fit: chop them
    const std::string names = waitingNames(net, wait.peers);
    rm->menu_font_large.drawChopCentered(cx, y + 46, DIALOG_W - 32, names);

    const std::string seconds = std::format("{:.1f} s", static_cast<double>(wait.waitingMs) / 1000.0);
    rm->menu_font_large.drawCentered(cx, y + 74, seconds);

    if (wait.waitingMs >= SHOW_HINT_AFTER_MS) {
        const int timeout = std::max(1, cmd::g_net.timeoutMs / 1000);
        rm->menu_font_small.drawCentered(cx, y + 95, std::format("CPU takes over after {} s", timeout));
    }
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

void drawNetStats(const rollback::NetGame& net)
{
    std::string line = "Net";
    for (int p = 0; p < 4; p++) {
        if (p == net.localPeer() || !net.lobby() || !net.lobby()->peer(p).present)
            continue;
        const int ping = net.peerPingMs(p);
        std::string name = net.lobby()->peer(p).name;
        if (name.size() > 10)
            name.resize(10);
        line += ping >= 0 ? std::format("  {} {} ms", name, ping) : std::format("  {} ?", name);
    }
    line += std::format("  Delay {}  Rollbacks {:.1f}/s", net.inputDelay(), rollbacksPerSecond(net));

    //Centered under the score panels (the FPS line owns the bottom)
    rm->menu_font_small.drawCentered(App::screenWidth / 2, 42, line);
}

} // namespace

void drawNetOverlay()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.inMatch())
        return;

    if (game_values.shownetstats)
        drawNetStats(net);

    const rollback::NetGame::DropNotice drop = net.dropNotice();
    if (!drop.text.empty() && drop.ageMs < SHOW_DROP_MS)
        rm->menu_font_large.drawCentered(App::screenWidth / 2, 64, drop.text);

    const rollback::NetGame::WaitInfo wait = net.waitInfo();
    if (wait.waiting && wait.waitingMs >= SHOW_WAIT_AFTER_MS)
        drawWaitingBox(net, wait);
}

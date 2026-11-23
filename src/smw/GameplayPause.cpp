#include "GSGameplay.h"

#include "CmdArgs.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "rollback/NetGame.h"
#include "gamemodes/Tag.h"
#include "ui/InputPrompts.h"
#include "ui/MI_MenuPanel.h"

#include <format>
#include <string>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

#include "GSGameplayInternal.h"

void drawExitPauseDialog()
{
    if (game_values.flags.pausegame) {
        menupanel::dialogBox(190, 172, 260, 128);
        rm->menu_font_large.drawCentered(App::screenWidth/2, 194, "Pause");

        short iMode = GetModeIconIndexFromMode(game_values.gamemode->gamemode);

        rm->menu_mode_large.draw(304, 224, {iMode << 5, 0, 32, 32});

        std::string szGoal = game_values.gamemode->GetGoalName() + ": ";

        if (game_values.gamemode->goal == -1)
            szGoal += "Unlimited";
        else
            szGoal += std::to_string(game_values.gamemode->goal);

        rm->menu_font_large.drawCentered(App::screenWidth/2, 264, szGoal.c_str());
    }

    if (game_values.flags.exitinggame) {
        menupanel::dialogBox(170, 172, 300, 128);
        if (rollback::NetGame::instance().inMatch()) {
            //Leaving disconnects us from the others: the match ends for everyone
            const int titleY = static_cast<int>(App::screenHeight*0.46f) - (rm->menu_font_large.getHeight() >> 1);
            rm->menu_font_large.drawCentered(App::screenWidth / 2, titleY - 14, "Leave online");
            rm->menu_font_large.drawCentered(App::screenWidth / 2, titleY + 6, "match?");
        } else {
            rm->menu_font_large.drawCentered(App::screenWidth * 0.5f, App::screenHeight*0.46f - (rm->menu_font_large.getHeight() >> 1), "Quit the match?");
        }

        menupanel::dialogChoice(App::screenWidth / 2, 252, "Yes", "No", game_values.flags.exityes);
    }
}

// ---- The online panel (the pause button online: the match can't wait, so it goes on behind it)

namespace {
bool s_onlinePanel = false;

// The run-ahead settings, in the order left/right go through them (-1: Auto)
constexpr short RUN_AHEAD_SETTINGS[] = {-1, 0, 1, 2, 3};

void changeRunAhead(int direction)
{
    const short* begin = std::begin(RUN_AHEAD_SETTINGS);
    const short* end = std::end(RUN_AHEAD_SETTINGS);
    const short* at = std::find(begin, end, game_values.runahead);
    const long index = std::clamp<long>((at == end ? 0 : at - begin) + direction, 0, (end - begin) - 1);
    game_values.runahead = begin[index];
}

std::string framesText(int frames)
{
    return frames == 1 ? "1 frame" : std::to_string(frames) + " frames";
}
} // namespace

bool onlinePanelOpen()
{
    //(auto-play: open by --at-online-panel, for its screenshots)
    if (cmd::g_autotest.enabled && cmd::g_autotest.onlinePanel && rollback::NetGame::instance().inMatch())
        s_onlinePanel = true;
    return s_onlinePanel;
}

void closeOnlinePanel()
{
    if (s_onlinePanel)
        game_values.WriteConfig();    //(the run-ahead chosen: next time too)
    s_onlinePanel = false;
}

void drawOnlinePanel()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!onlinePanelOpen() || !net.inMatch())
        return;

    //The worst connection: the others' inputs come that late (rollback hides it, the input delay too)
    int ping = -1;
    for (int peer = 0; peer < rollback::net::MAX_PEERS; peer++)
        ping = std::max(ping, net.peerPingMs(peer));
    const int delay = net.inputDelay();
    const int ahead = runAheadSetting(game_values.runahead, delay);
    const int late = std::max(0, delay - ahead);

    const int x = 100, y = 116, w = 440, h = 204;
    menupanel::box(x, y, w, h);
    rm->menu_font_large.drawCentered(320, y + 10, "Online");

    //From the ping: how late the others' moves come (half the round trip; rollback corrects them)
    std::string lag = "Ping -";
    if (ping >= 0) {
        const int othersLate = (ping / 2 + 15) / 16;
        lag = "Ping " + std::to_string(ping) + " ms: the others are " + (othersLate == 0 ? "in time" : framesText(othersLate) + " behind");
    }
    rm->menu_font_small.drawCentered(320, y + 40, lag);

    //The setting, with its arrows
    const std::string setting = game_values.runahead < 0 ? "Auto (" + std::to_string(ahead) + ")" : game_values.runahead == 0 ? "Off" : framesText(game_values.runahead);
    menupanel::row(x + 20, y + 62, w - 40, 36, true);
    rm->menu_font_large.draw(x + 34, y + 70, "Run-Ahead");
    rm->menu_font_large.drawRightJustified(x + w - 34, y + 70, "< " + setting + " >");

    //Ours: the input delay holds them back, the run-ahead wins that back
    const std::string own = "Input delay " + std::to_string(delay) + ": your buttons answer " + (late == 0 ? "at once" : framesText(late) + " late");
    rm->menu_font_small.drawCentered(320, y + 112, own);
    const char* note = ahead > delay ? "Past the input delay it only guesses: more corrections"
        : game_values.runahead < 0 ? "Auto: as much run-ahead as input delay"
        : "Each frame of it costs one more tick of work";
    rm->menu_font_small.drawCentered(320, y + 132, note);

    using prompts::Pad;
    using prompts::menuKey;
    prompts::drawPromptsLarge(320, y + 164, {{{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"},
        {{Pad::Start}, {menuKey(prompts::MENU_CANCEL)}, "close"}}, 14, 400);
}

bool IsPauseAllowed()
{
    //Online, the match can't wait for one player
    return !game_values.flags.noexit && !rollback::NetGame::instance().inMatch();
}

bool IsExitAllowed()
{
    if (!game_values.flags.noexit || g_sim.players.empty())
        return true;

    for (CPlayer* player : g_sim.players) {
        if (game_values.playercontrol[player->getGlobalID()] == 1)
            return false;
    }

    return true;
}

bool updateExitPauseDialog(short iCountDownState) // true on exit
{
    //The world map's force exit timer that used to run here is simulation (see simulateTick)
    if (game_values.screenfade == 0 && iCountDownState <= COUNTDOWN_START_INDEX) {
        for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
            COutputControl * playerKeys = &game_values.playerInput.outputControls[iPlayer];

            //Online: the pause button opens the online panel (and while it's up, it has the keys)
            if (rollback::NetGame::instance().inMatch() && !game_values.gamemode->gameover) {
                if (s_onlinePanel) {
                    if (playerKeys->game_start.fPressed || playerKeys->game_cancel.fPressed || playerKeys->menu_cancel.fPressed) {
                        closeOnlinePanel();
                        ifSoundOnPlay(rm->sfx_pause);
                        game_values.playerInput.ResetKeys();
                        break;
                    }
                    if (playerKeys->menu_left.fPressed)
                        changeRunAhead(-1);
                    else if (playerKeys->menu_right.fPressed)
                        changeRunAhead(1);
                    continue;
                }
                if (playerKeys->game_start.fPressed && !game_values.flags.exitinggame && !game_values.flags.showscoreboard) {
                    s_onlinePanel = true;
                    ifSoundOnPlay(rm->sfx_pause);
                    continue;
                }
            }

            //If the start key is pressed (pause key)
            if (playerKeys->game_start.fPressed && IsPauseAllowed()) {
                if (!game_values.flags.showscoreboard && !game_values.flags.exitinggame) {
                    game_values.flags.pausegame = !game_values.flags.pausegame;

                    if (game_values.flags.pausegame) {
                        //Stop the pwings sound if it is on
                        if (rm->sfx_flyingsound.isPlaying())
                            rm->sfx_flyingsound.stop();
                    }

                    ifSoundOnPlay(rm->sfx_pause);
                }
            }

            if ((playerKeys->game_cancel.fPressed || (playerKeys->game_start.fPressed && game_values.gamemode->gameover)) && IsExitAllowed()) {
                if (game_values.gamemode->gameover) {
                    if (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::Tournament)
                        UpdateScoreBoard();

                    CleanUp();
                    game_values.appstate = AppState::Menu;

                    return true;
                } else {
                    if (!game_values.flags.pausegame && !game_values.flags.exitinggame) {
                        game_values.flags.exitinggame = true;

                        //Reset the keys each time we switch from menu to game and back
                        game_values.playerInput.ResetKeys();
                    }
                }
            }

            //Deal with input to game exit dialog box
            if (game_values.flags.exitinggame) {
                if (playerKeys->menu_left.fPressed)
                    game_values.flags.exityes = true;
                else if (playerKeys->menu_right.fPressed)
                    game_values.flags.exityes = false;

                if (playerKeys->menu_select.fPressed) {
                    if (game_values.flags.exityes) {
                        CleanUp();
                        game_values.flags.exitinggame = false;
                        game_values.flags.exityes = false;
                        game_values.appstate = AppState::Menu;
                        return true;
                    } else {
                        game_values.flags.exitinggame = false;

                        //Reset the keys each time we switch from menu to game and back
                        game_values.playerInput.ResetKeys();
                    }
                }
            }
        }
    }

    return false;
}

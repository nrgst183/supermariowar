#include "MI_MatchHistory.h"

#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include "GameMode.h"
#include "input.h"
#include "ui/InputPrompts.h"
#include "ResourceManager.h"
#include "FileList.h"
#include "uimenu.h"
#include "rollback/Replay.h"

#include <algorithm>
#include <filesystem>

extern CResourceManager* rm;

namespace {
constexpr short LINE_H = 32;

// "1st"
std::string ordinal(int n)
{
    switch (n) {
        case 1: return "1st";
        case 2: return "2nd";
        case 3: return "3rd";
        default: return std::to_string(n) + "th";
    }
}

std::string teamNames(const history::Team& team)
{
    std::string names;
    for (const history::Player& p : team.players)
        names += (names.empty() ? "" : " & ") + p.name;
    return names.empty() ? "?" : names;
}

std::string resultText(const history::Entry& e)
{
    if (e.result == "won" && e.winner >= 0 && e.winner < static_cast<int>(e.teams.size()))
        return teamNames(e.teams[static_cast<size_t>(e.winner)]) + " won";
    if (e.result == "draw")
        return "Draw";
    return "Left early";
}

// Mode icon index in menu_mode_small (as GetModeIconIndexFromMode)
short modeIcon(int mode)
{
    if (mode == game_mode_pipe_minigame)
        return 25;
    if (mode == game_mode_boss_minigame)
        return 26;
    if (mode == game_mode_boxes_minigame)
        return 27;
    return mode >= 0 && mode < GAMEMODE_LAST ? static_cast<short>(mode) : -1;
}
} // namespace

MI_MatchHistory::MI_MatchHistory(short x, short y, short width, short lines)
    : UI_Control(x, y)
    , m_width(width)
    , m_lines(lines)
{}

void MI_MatchHistory::Refresh()
{
    m_entries = history::load();
    m_index = 0;
    m_offset = 0;
    m_message.clear();
}

MenuCodeEnum MI_MatchHistory::Modify(bool modify)
{
    fModifying = modify;
    return MENU_CODE_MODIFY_ACCEPTED;
}

std::string MI_MatchHistory::selectedReplay() const
{
    if (m_index < 0 || m_index >= static_cast<int>(m_entries.size()))
        return {};
    return m_entries[static_cast<size_t>(m_index)].replay;
}

void MI_MatchHistory::move(int delta)
{
    if (m_entries.empty())
        return;
    const int last = static_cast<int>(m_entries.size()) - 1;
    const int index = std::clamp(m_index + delta, 0, last);
    if (index == m_index)
        return;
    m_index = index;
    m_message.clear();
    if (m_index < m_offset)
        m_offset = m_index;
    else if (m_index >= m_offset + m_lines)
        m_offset = m_index - m_lines + 1;
}

MenuCodeEnum MI_MatchHistory::SendInput(CPlayerInput* playerInput)
{
    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        const COutputControl& keys = playerInput->outputControls[iPlayer];
        if (keys.menu_down.fPressed) {
            move(1);
            return MENU_CODE_NONE;
        }
        if (keys.menu_up.fPressed) {
            move(-1);
            return MENU_CODE_NONE;
        }
        //Left and right: a page
        if (keys.menu_right.fPressed) {
            move(m_lines);
            return MENU_CODE_NONE;
        }
        if (keys.menu_left.fPressed) {
            move(-m_lines);
            return MENU_CODE_NONE;
        }
        if (keys.menu_select.fPressed) {
            if (m_entries.empty())
                return MENU_CODE_NONE;
            const std::string file = selectedReplay();
            if (file.empty() || !replay::replayExists(file)) {
                m_message = file.empty() ? "This match has no replay" : "Replay deleted (only the newest 200 are kept)";
                return MENU_CODE_NONE;
            }
            return MENU_CODE_HISTORY_PLAY_REPLAY;
        }
        if (keys.menu_cancel.fPressed)
            return MENU_CODE_HISTORY_EXIT;
    }
    return MENU_CODE_NONE;
}

// A panel, in the menus' colors
void MI_MatchHistory::drawBox(short x, short y, short w, short h) const
{
    menupanel::box(x, y, w, h);
}

void MI_MatchHistory::Draw()
{
    if (!m_visible)
        return;

    // The header bar, as on every menu of the game
    UI_Menu::drawTitleBarBack();
    rm->menu_font_large.drawCentered(320, 5, "Match History");
    if (!m_entries.empty())
        rm->menu_font_small.drawRightJustified(620, 10, std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));

    const short top = 40;
    const short listH = static_cast<short>(m_lines * LINE_H + 20);
    drawBox(m_pos.x, top, m_width, listH);

    if (m_entries.empty()) {
        theme::statusIcon(m_pos.x + m_width / 2 - 8, top + LINE_H * 2, theme::Status::Info);
        rm->menu_font_large.drawCentered(m_pos.x + m_width / 2, top + LINE_H * 3, "No matches yet");
        rm->menu_font_small.drawCentered(m_pos.x + m_width / 2, top + LINE_H * 4, "Every match you play shows up here with its replay");
    }

    const short lineW = m_width - 24;
    for (int line = 0; line < m_lines && m_offset + line < static_cast<int>(m_entries.size()); line++) {
        const history::Entry& e = m_entries[static_cast<size_t>(m_offset + line)];
        const bool selected = m_offset + line == m_index;
        const short x = m_pos.x + 12;
        const short y = static_cast<short>(top + 10 + line * LINE_H);
        menupanel::row(x, y + 1, lineW, LINE_H - 2, selected);

        const short icon = modeIcon(e.modeId);
        if (icon >= 0)
            rm->menu_mode_small.draw(x + 10, y + 8, {static_cast<short>(icon * 16), 0, 16, 16});
        rm->menu_font_small.drawChopRight(x + 34, y + 9, 118, history::formatShortTime(e.time));
        const std::string what = e.mode + (e.map.empty() ? "" : " - " + e.map);
        rm->menu_font_large.drawChopRight(x + 160, y + 5, lineW - 160 - 172, what);
        rm->menu_font_small.drawChopLeft(x + lineW - 12, y + 9, 160, resultText(e));
    }

    drawDetails(static_cast<short>(top + listH + 6));
}

// The skins of the selected match's players (their menu sprites), loaded when it's chosen
void MI_MatchHistory::loadPortraits(const history::Entry& e) const
{
    if (m_portraitsFor == m_index)
        return;
    m_portraitsFor = m_index;
    m_portraits.clear();
    m_portraits.resize(e.teams.size());
    for (size_t t = 0; t < e.teams.size(); t++) {
        for (const history::Player& p : e.teams[t].players) {
            std::filesystem::path path;
            for (size_t k = 0; skinlist && k < skinlist->count(); k++) {
                if (skinlist->at(k).name == p.skin)
                    path = skinlist->at(k).path;
            }
            if (path.empty() && skinlist && skinlist->count() > 0)
                path = skinlist->at(0).path;    //(a skin not here any more: the default one)
            SpriteStrip strip;
            if (!path.empty())
                strip = gfx_loadmenuskin(path, static_cast<short>(p.slot & 3), false);
            m_portraits[t].push_back(std::move(strip));
        }
    }
}

void MI_MatchHistory::drawDetails(short y) const
{
    const short h = static_cast<short>(442 - y);    //(the footer under it)
    if (h < 64)
        return;
    drawBox(m_pos.x, y, m_width, h);
    const short x = m_pos.x + 20;
    const short right = m_pos.x + m_width - 20;
    const short bottom = static_cast<short>(y + h - 10);

    if (m_entries.empty() || m_index >= static_cast<int>(m_entries.size())) {
        menupanel::footer({{{prompts::Pad::B}, {prompts::menuKey(prompts::MENU_CANCEL)}, "back"}});
        return;
    }
    const history::Entry& e = m_entries[static_cast<size_t>(m_index)];

    std::string title = e.mode;
    if (e.goal > 0)
        title += ", goal " + std::to_string(e.goal);
    if (!e.map.empty())
        title += "  -  " + e.map;
    rm->menu_font_large.drawChopRight(x, y + 12, m_width - 40, title);
    std::string info = e.matchType + (e.online ? " (online)" : "") + "    " + history::formatDuration(e.ticks) + "    " + history::formatTime(e.time);
    menupanel::bestText(x, y + 36, m_width - 40, info);

    //The teams, by place, side by side: the place and the score, then the players' portraits (their
    //skins, in the lobby's frames) and their names; the winners' crown over theirs
    std::vector<size_t> order(e.teams.size());
    for (size_t k = 0; k < order.size(); k++)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return e.teams[a].place < e.teams[b].place; });
    loadPortraits(e);
    const int columns = std::max<int>(1, static_cast<int>(order.size()));
    const int columnW = (m_width - 40) / columns;
    for (int c = 0; c < columns && c < static_cast<int>(order.size()); c++) {
        const size_t t = order[static_cast<size_t>(c)];
        const history::Team& team = e.teams[t];
        const short cx = static_cast<short>(x + c * columnW);
        const short ty = static_cast<short>(y + 54);
        const bool won = static_cast<int>(t) == e.winner;
        rm->menu_font_large.draw(cx, ty, ordinal(team.place));
        rm->menu_font_small.draw(cx + 52, ty + 6, std::to_string(team.score) + (team.score == 1 ? " point" : " points"));
        for (size_t k = 0; k < team.players.size() && k < 4; k++) {
            const short px = static_cast<short>(cx + static_cast<short>(k) * 42);
            const short py = static_cast<short>(ty + 26);
            if (px + 38 > cx + columnW - 4)
                break;
            theme::statusBar(px, py, 38, 38);
            const std::vector<SpriteStrip>& strips = m_portraits[t];
            if (k < strips.size() && strips[k][0].getSurface())
                strips[k][0].draw(px + 3, py + 3, {0, 0, 32, 32});
            if (won && k == 0)
                theme::crown(px + 26, py - 8);
            if (team.players.size() == 1)
                rm->menu_font_small.drawChopRight(px + 44, py + 14, columnW - 50, team.players[k].name);
        }
        if (team.players.size() > 1 && bottom > ty + 70)
            rm->menu_font_small.drawChopRight(cx, ty + 68, columnW - 8, team.players[0].name + (team.players.size() > 1 ? " +" + std::to_string(team.players.size() - 1) : ""));
    }

    //What the buttons do: the controller's or the keys, in the footer (as on the other menus)
    using prompts::Pad;
    using prompts::menuKey;
    std::vector<prompts::Prompt> row;
    if (!e.replay.empty())
        row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "watch the replay"});
    row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"});
    menupanel::footer(row);

    if (!m_message.empty())
        rm->menu_font_small.drawRightJustified(right, bottom - 14, m_message);
    else if (e.replay.empty())
        rm->menu_font_small.drawRightJustified(right, bottom - 14, "No replay");
}

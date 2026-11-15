#include "MI_MatchHistory.h"

#include "GameMode.h"
#include "input.h"
#include "ResourceManager.h"
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
            std::error_code ec;
            if (file.empty() || !std::filesystem::is_regular_file(replay::replayDir() + file, ec)) {
                m_message = file.empty() ? "This match has no replay" : "Its replay was deleted (the newest 200 are kept)";
                return MENU_CODE_NONE;
            }
            return MENU_CODE_HISTORY_PLAY_REPLAY;
        }
        if (keys.menu_cancel.fPressed)
            return MENU_CODE_HISTORY_EXIT;
    }
    return MENU_CODE_NONE;
}

// A dialog frame of any width (menu_dialog is 512 x 480)
void MI_MatchHistory::drawBox(short x, short y, short w, short h) const
{
    const short left = w / 2;
    const short right = w - left;
    rm->menu_dialog.draw(x, y, {0, 0, left, h});
    rm->menu_dialog.draw(x + left, y, {512 - right, 0, right, h});
    rm->menu_dialog.draw(x, y + h, {0, 464, left, 16});
    rm->menu_dialog.draw(x + left, y + h, {512 - right, 464, right, 16});
}

void MI_MatchHistory::Draw()
{
    if (!m_visible)
        return;

    const short listH = static_cast<short>(m_lines * LINE_H + 32);
    drawBox(m_pos.x, m_pos.y, m_width, listH);
    rm->menu_font_large.drawCentered(m_pos.x + m_width / 2, m_pos.y + 5, "Match History");

    if (m_entries.empty()) {
        rm->menu_font_large.drawCentered(m_pos.x + m_width / 2, m_pos.y + 32 + LINE_H * 2, "No matches yet");
        rm->menu_font_small.drawCentered(m_pos.x + m_width / 2, m_pos.y + 32 + LINE_H * 3 + 4, "Every match you play is listed here, with its replay");
    } else {
        rm->menu_font_small.drawRightJustified(m_pos.x + m_width - 20, m_pos.y + 10,
            std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));
    }

    const short lineW = m_width - 32;
    const short half = lineW / 2;
    for (int line = 0; line < m_lines && m_offset + line < static_cast<int>(m_entries.size()); line++) {
        const history::Entry& e = m_entries[static_cast<size_t>(m_offset + line)];
        const bool selected = m_offset + line == m_index;
        const short x = m_pos.x + 16;
        const short y = static_cast<short>(m_pos.y + 32 + line * LINE_H);
        rm->spr_selectfield.draw(x, y, {0, static_cast<short>(selected ? 32 : 0), half, 32});
        rm->spr_selectfield.draw(x + half, y, {static_cast<short>(512 - (lineW - half)), static_cast<short>(selected ? 32 : 0), static_cast<short>(lineW - half), 32});

        const short icon = modeIcon(e.modeId);
        if (icon >= 0)
            rm->menu_mode_small.draw(x + 10, y + 8, {static_cast<short>(icon * 16), 0, 16, 16});
        rm->menu_font_small.drawChopRight(x + 34, y + 9, 118, history::formatShortTime(e.time));
        const std::string what = e.mode + (e.map.empty() ? "" : " - " + e.map);
        rm->menu_font_large.drawChopRight(x + 160, y + 5, lineW - 160 - 172, what);
        rm->menu_font_small.drawChopLeft(x + lineW - 12, y + 9, 160, resultText(e));
    }

    drawDetails(static_cast<short>(m_pos.y + listH + 24));
}

void MI_MatchHistory::drawDetails(short y) const
{
    const short h = static_cast<short>(480 - 16 - 16 - y);
    if (h < 64)
        return;
    drawBox(m_pos.x, y, m_width, h);
    const short x = m_pos.x + 20;
    const short right = m_pos.x + m_width - 20;
    const short bottom = static_cast<short>(y + h - 6);

    if (m_entries.empty() || m_index >= static_cast<int>(m_entries.size())) {
        rm->menu_font_small.drawCentered(m_pos.x + m_width / 2, bottom - 14, "Cancel: back");
        return;
    }
    const history::Entry& e = m_entries[static_cast<size_t>(m_index)];

    std::string title = e.mode;
    if (e.goal > 0)
        title += ", goal " + std::to_string(e.goal);
    if (!e.map.empty())
        title += "  -  " + e.map;
    rm->menu_font_large.drawChopRight(x, y + 8, m_width - 40, title);
    std::string info = e.matchType + (e.online ? " (online)" : "") + "    " + history::formatDuration(e.ticks) + "    " + history::formatTime(e.time);
    rm->menu_font_small.drawChopRight(x, y + 28, m_width - 40, info);

    //The teams, by place
    std::vector<size_t> order(e.teams.size());
    for (size_t k = 0; k < order.size(); k++)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return e.teams[a].place < e.teams[b].place; });
    short ty = static_cast<short>(y + 46);
    for (size_t t : order) {
        if (ty + 14 > bottom - 16)
            break;
        const history::Team& team = e.teams[t];
        rm->menu_font_small.draw(x, ty, ordinal(team.place));
        rm->menu_font_small.drawRightJustified(x + 70, ty, std::to_string(team.score));
        std::string players;
        for (const history::Player& p : team.players)
            players += (players.empty() ? "" : ",  ") + p.name + (p.skin.empty() ? "" : " (" + p.skin + ")");
        if (static_cast<int>(t) == e.winner)
            players += "   - winner";
        rm->menu_font_small.drawChopRight(x + 90, ty, m_width - 130, players);
        ty += 15;
    }

    const std::string hint = !m_message.empty() ? m_message
        : e.replay.empty() ? "No replay    Cancel: back"
        : "Select: watch the replay    Cancel: back";
    rm->menu_font_small.drawRightJustified(right, bottom - 14, hint);
}

#include "MI_MatchHistory.h"

#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include "GameMode.h"
#include "input.h"
#include "ui/InputPrompts.h"
#include "ResourceManager.h"
#include "FileList.h"
#include "MapList.h"
#include "map.h"
#include "path.h"
#include "uimenu.h"
#include "rollback/Replay.h"

#include <SDL3_image/SDL_image.h>

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
    if (mode == game_mode_mapmaker)
        return 23;
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
    m_portraitsFor = -1;
    m_thumbnailFor = -1;
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
        rm->menu_font_large.drawScrolling(x + 160, y + 5, lineW - 160 - 172, what, selected);
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

// The selected match's map, as the map browser shows it: its saved thumbnail, or one made (and saved)
// from the map file if there is none yet; none if the map isn't here any more
void MI_MatchHistory::loadThumbnail(const history::Entry& e) const
{
    //(none saved: made from the map file, a read and a drawing of the whole map, slow from a disc;
    //so only once the selection has stayed on the match a moment, not for each one moved past)
    if (!m_thumbnailToMake.empty()) {
        if (m_thumbnailFor != m_index) {
            m_thumbnailToMake.clear();
        } else if (SDL_GetTicks() - m_thumbnailAsked >= 350 && g_map) {
            g_map->loadMap(m_thumbnailToMake, read_type_preview);
            m_thumbnail = g_map->createThumbnailSurface(false);
            IMG_SavePNG(m_thumbnail.getSurface(), mapThumbnailPath(m_thumbnailToMake).c_str());    //(if it can't be saved, it's made again next time)
            m_thumbnailToMake.clear();
            return;
        }
    }
    if (m_thumbnailFor == m_index)
        return;
    m_thumbnailFor = m_index;
    m_thumbnail = gfxSprite();
    if (e.map.empty() || !maplist)
        return;
    const std::string file = maplist->fileNamed(e.map);
    if (file.empty())
        return;
    const std::string cached = mapThumbnailPath(file);
    if (FileExists(cached)) {
        try {
            m_thumbnail = ImageLoader(cached.c_str()).withoutColorKey().create();
            return;
        } catch (...) {
            //(a broken file: made again)
        }
    }
    m_thumbnailToMake = file;
    m_thumbnailAsked = SDL_GetTicks();
}

void MI_MatchHistory::drawDetails(short y) const
{
    const short h = static_cast<short>(442 - y);    //(the footer under it)
    if (h < 64)
        return;
    drawBox(m_pos.x, y, m_width, h);
    const short x = m_pos.x + 20;
    const short right = m_pos.x + m_width - 20;

    if (m_entries.empty() || m_index >= static_cast<int>(m_entries.size())) {
        menupanel::footer({{{prompts::Pad::B}, {prompts::menuKey(prompts::MENU_CANCEL)}, "back"}});
        return;
    }
    const history::Entry& e = m_entries[static_cast<size_t>(m_index)];

    //The map's thumbnail on the left, in the lobby's frame (the map browser's picture of it)
    loadThumbnail(e);
    theme::statusBar(x, y + 10, 166, 126);
    if (m_thumbnail) {
        m_thumbnail.draw(x + 3, y + 13);
    } else if (!m_thumbnailToMake.empty()) {
        //(being made: the map read and drawn, in a moment; the spinner the games list waits with)
        theme::spinner(x + 83, y + 56, static_cast<int>(SDL_GetTicks() / 16));
        rm->menu_font_small.drawCentered(x + 83, y + 92, "Drawing the map...");
    } else {
        rm->menu_font_small.drawCentered(x + 83, y + 69, "No picture");
    }

    //Right of it the heading: the mode's icon and the map, large; under it the rest, small
    const short ax = static_cast<short>(x + 178);
    const short icon = modeIcon(e.modeId);
    short tx = ax;
    if (icon >= 0) {
        rm->menu_mode_small.draw(ax, y + 14, {static_cast<short>(icon * 16), 0, 16, 16});
        tx += 24;
    }
    rm->menu_font_large.drawScrolling(tx, y + 12, right - tx, e.map.empty() ? e.mode : e.map);
    std::string what = e.mode;
    if (e.goal > 0)
        what += " to " + std::to_string(e.goal);
    what += "   " + e.matchType + (e.online ? " (online)" : "");
    rm->menu_font_small.drawScrolling(ax, y + 36, right - ax, what);
    rm->menu_font_small.drawScrolling(ax, y + 50, right - ax, history::formatDuration(e.ticks) + "   " + history::formatTime(e.time));
    prompts::fill(ax, y + 66, right - ax, 2, {42, 154, 240, 255});    //(the frame's light edge: the heading's end)

    //The teams, by place, two to a row: a card each (the winner's lit, with a crown), its first
    //player's portrait (their skin, in the lobby's frame), the place and the score, then the name
    std::vector<size_t> order(e.teams.size());
    for (size_t k = 0; k < order.size(); k++)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return e.teams[a].place < e.teams[b].place; });
    loadPortraits(e);
    const int columnW = (right - ax + 8) / 2;
    constexpr short CARD_H = 44;
    for (size_t c = 0; c < order.size() && c < 4; c++) {
        const size_t t = order[c];
        const history::Team& team = e.teams[t];
        const short cx = static_cast<short>(ax + static_cast<int>(c % 2) * columnW);
        const short cy = static_cast<short>(y + 74 + static_cast<int>(c / 2) * (CARD_H + 4));
        const short cw = static_cast<short>(columnW - 8);
        const bool won = static_cast<int>(t) == e.winner;
        menupanel::row(cx, cy, cw, CARD_H, won);

        const short px = static_cast<short>(cx + 3);
        const short py = static_cast<short>(cy + 3);
        theme::statusBar(px, py, 38, 38);
        const std::vector<SpriteStrip>& strips = m_portraits[t];
        if (!strips.empty() && strips[0][0].getSurface())
            strips[0][0].draw(px + 3, py + 3, {0, 0, 32, 32});
        if (won)
            theme::crown(px + 26, py - 8);

        const short textX = static_cast<short>(px + 46);
        const short textW = static_cast<short>(cx + cw - 8 - textX);
        rm->menu_font_large.draw(textX, cy + 4, ordinal(team.place));
        rm->menu_font_small.drawRightJustified(cx + cw - 8, cy + 9, std::to_string(team.score) + (team.score == 1 ? " point" : " points"));
        std::string name = team.players.empty() ? "?" : team.players[0].name;
        if (team.players.size() > 1)
            name += " +" + std::to_string(team.players.size() - 1);
        rm->menu_font_small.drawScrolling(textX, cy + 27, textW, name);
    }

    //What the buttons do: the controller's or the keys, in the footer (as on the other menus)
    using prompts::Pad;
    using prompts::menuKey;
    std::vector<prompts::Prompt> row;
    if (!e.replay.empty())
        row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "watch the replay"});
    row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"});
    //(why a replay can't play: by the buttons, in the footer)
    menupanel::footer(row, !m_message.empty() ? m_message : e.replay.empty() ? std::string("No replay") : std::string());
}

#include "SettingsPage.h"

#include "ResourceManager.h"
#include "uimenu.h"
#include "ui/MI_Button.h"
#include "ui/MI_MenuPanel.h"

#include <algorithm>
#include <cassert>

namespace {
// The rows' panel goes between the title bar and the footer (its help line and prompts, B: back),
// the rows in the middle of it with as much of the panel around them on every side
constexpr short AREA_TOP = 36;
constexpr short AREA_BOTTOM = 432;
constexpr short PAD = 15;           // (the panel around the rows: clear of the focused row's frame)
constexpr short PAD_X = 30;          // (and of the controller's pointer, at the focused row's left)
constexpr short ROW_H = 32;
constexpr short STEP = 40;          // the original's
constexpr short MIN_STEP = 34;      // (a long list: the rows closer)
} // namespace


MI_SettingsHelp::MI_SettingsHelp(const UI_Menu& menu)
    : UI_Control(0, 450)
    , m_menu(menu)
{}

void MI_SettingsHelp::set(const UI_Control* row, std::string text)
{
    m_lines.emplace_back(row, std::move(text));
}

void MI_SettingsHelp::Draw()
{
    if (!m_visible)
        return;
    const UI_Control* focus = m_menu.currentFocus();
    std::string help;
    for (const auto& [row, text] : m_lines) {
        if (row == focus)
            help = text;
    }
    //(up/down go without saying here: the help line needs the room; A selects a field, left/right
    //change it, A again is done)
    using prompts::Pad;
    using prompts::menuKey;
    if (m_menu.ChangingValue())
        menupanel::footer({{{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"},
                           {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "done"}}, help);
    else
        menupanel::footer({{{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select"},
                           {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}}, help);
}


SettingsPage::SettingsPage(short rows)
    : m_expected(rows)
{
    //(as far apart as the original's, or as fit with the panel around them)
    const short room = AREA_BOTTOM - AREA_TOP - 2 * PAD;
    m_step = rows > 1 ? static_cast<short>(std::clamp((room - ROW_H) / (rows - 1), static_cast<int>(MIN_STEP), static_cast<int>(STEP))) : STEP;
    const short height = (rows - 1) * m_step + ROW_H;
    m_next = AREA_TOP + (AREA_BOTTOM - AREA_TOP - height) / 2;
}

void SettingsPage::add(UI_Control* row, std::string help)
{
    m_rows.push_back({row, std::move(help)});
    m_next += m_step;
}

void SettingsPage::finish(UI_Menu& menu, const std::string& title, MenuCodeEnum backCode)
{
    assert(static_cast<short>(m_rows.size()) == m_expected);

    //Up and down the rows, round from the last to the first; B (or the footer's "back"): back
    const size_t n = m_rows.size();
    for (size_t i = 0; i < n; i++)
        menu.AddControl(m_rows[i].control, m_rows[i == 0 ? n - 1 : i - 1].control, m_rows[i + 1 == n ? 0 : i + 1].control, nullptr, nullptr);

    //The rows on a panel
    std::vector<const UI_Control*> rows;
    for (const Row& row : m_rows)
        rows.push_back(row.control);
    AddPanelBehind(menu, rows, PAD_X, PAD);

    MI_SettingsHelp* help = new MI_SettingsHelp(menu);
    for (const Row& row : m_rows)
        help->set(row.control, row.help);
    menu.AddNonControl(help);

    menu.AddTitleBar(title);
    menu.setInitialFocus(m_rows[0].control);
    menu.SetCancelCode(backCode);
    //(the options screens are pages: LB and RB turn them)
    menu.SetPaged(backCode == MENU_CODE_BACK_TO_OPTIONS_MENU);
}

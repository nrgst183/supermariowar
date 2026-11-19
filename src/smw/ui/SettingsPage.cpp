#include "SettingsPage.h"

#include "ResourceManager.h"
#include "uimenu.h"
#include "ui/MI_Button.h"
#include "ui/MI_MenuPanel.h"

#include <cassert>

namespace {
// The rows go between the title bar and the Back button (over the help line)
constexpr short AREA_TOP = 40;
constexpr short AREA_BOTTOM = 404;
constexpr short ROW_H = 32;
constexpr short STEP = 40;          // the original's
constexpr short TIGHT_STEP = 36;    // (a long list)
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
    menupanel::footer({});
    const UI_Control* focus = m_menu.currentFocus();
    for (const auto& [row, text] : m_lines) {
        if (row == focus) {
            rm->menu_font_small.drawChopRight(24, 458, 592, text);
            return;
        }
    }
}


SettingsPage::SettingsPage(short rows)
    : m_expected(rows)
{
    m_step = (rows - 1) * STEP + ROW_H <= AREA_BOTTOM - AREA_TOP ? STEP : TIGHT_STEP;
    const short height = (rows - 1) * m_step + ROW_H;
    m_next = AREA_TOP + (AREA_BOTTOM - AREA_TOP - height) / 2;
}

void SettingsPage::add(UI_Control* row, std::string help)
{
    m_rows.push_back({row, std::move(help)});
    m_next += m_step;
}

MI_Button* SettingsPage::finish(UI_Menu& menu, const std::string& title, MenuCodeEnum backCode)
{
    assert(static_cast<short>(m_rows.size()) == m_expected);

    MI_Button* back = new MI_Button(&rm->spr_selectfield, 544, 410, "Back", 80, TextAlign::CENTER);
    back->SetCode(backCode);

    //Up and down the rows (and round through Back); right, or B: Back
    const size_t n = m_rows.size();
    for (size_t i = 0; i < n; i++)
        menu.AddControl(m_rows[i].control, i == 0 ? back : m_rows[i - 1].control, i + 1 == n ? back : m_rows[i + 1].control, nullptr, back);
    menu.AddControl(back, m_rows[n - 1].control, m_rows[0].control, m_rows[n - 1].control, nullptr);

    MI_SettingsHelp* help = new MI_SettingsHelp(menu);
    for (const Row& row : m_rows)
        help->set(row.control, row.help);
    help->set(back, backCode == MENU_CODE_TO_MAIN_MENU ? "Back to the main menu" : "Back to the options");
    menu.AddNonControl(help);

    menu.AddTitleBar(title);
    menu.setInitialFocus(m_rows[0].control);
    menu.SetCancelCode(backCode);
    return back;
}

#include "MI_MenuPanel.h"

#include "ResourceManager.h"
#include "ui/SmwTheme.h"

#include <cmath>
#include <utility>

namespace menupanel {
namespace {
// A rounded box, filled
void roundedFill(int x, int y, int w, int h, int r, SDL_Color c)
{
    for (int row = 0; row < h; row++) {
        int inset = 0;
        const int d = row < r ? r - row : (row >= h - r ? row - (h - r - 1) : 0);
        if (d > 0)
            inset = r - static_cast<int>(std::sqrt(static_cast<float>(r * r) - (d - 0.5f) * (d - 0.5f)) + 0.5f);
        prompts::fill(x + inset, y + row, w - 2 * inset, 1, c);
    }
}
} // namespace

void box(int x, int y, int w, int h)
{
    roundedFill(x, y, w, h, 8, {248, 248, 248, 255});
    roundedFill(x + 2, y + 2, w - 4, h - 4, 6, {0, 40, 104, 255});
    roundedFill(x + 3, y + 3, w - 6, h - 6, 5, {0, 95, 177, 255});
}

void row(int x, int y, int w, int h, bool selected, bool dim)
{
    if (selected) {
        roundedFill(x, y, w, h, 6, {248, 248, 248, 255});
        roundedFill(x + 2, y + 2, w - 4, h - 4, 4, dim ? SDL_Color {104, 112, 136, 255} : SDL_Color {32, 144, 232, 255});
    } else {
        roundedFill(x, y, w, h, 6, dim ? SDL_Color {56, 64, 88, 255} : SDL_Color {0, 64, 136, 255});
    }
}
} // namespace menupanel


MI_MenuPanel::MI_MenuPanel(short x, short y, short w, short h, std::string title, std::string note)
    : UI_Control(x, y)
    , m_w(w)
    , m_h(h)
    , m_title(std::move(title))
    , m_note(std::move(note))
{}

void MI_MenuPanel::SetTitle(std::string title, std::string note)
{
    m_title = std::move(title);
    m_note = std::move(note);
}

void MI_MenuPanel::Draw()
{
    if (!m_visible)
        return;
    menupanel::box(m_pos.x, m_pos.y, m_w, m_h);
    if (!m_title.empty())
        rm->menu_font_large.draw(m_pos.x + 20, m_pos.y + 10, m_title.c_str());
    const std::string note = m_noteSource ? m_noteSource() : m_note;
    if (!note.empty())
        rm->menu_font_small.drawRightJustified(m_pos.x + m_w - 20, m_pos.y + 14, note.c_str());
}


MI_PromptStrip::MI_PromptStrip(short x, short y, short w, std::string label, std::function<std::vector<prompts::Prompt>()> row)
    : UI_Control(x, y)
    , m_w(w)
    , m_label(std::move(label))
    , m_row(std::move(row))
{}

void MI_PromptStrip::Draw()
{
    if (!m_visible || !m_row)
        return;
    menupanel::box(m_pos.x, m_pos.y - 2, m_w, 34);
    int left = m_pos.x + 12;
    if (!m_label.empty()) {
        theme::font().draw(left, m_pos.y + 6, m_label);
        left += theme::font().getWidth(m_label) + 14;
    }
    const std::vector<prompts::Prompt> row = m_row();
    const int w = prompts::promptsWidth(row, 14);
    prompts::drawPrompts(left + w / 2, m_pos.y + 7, row);
}

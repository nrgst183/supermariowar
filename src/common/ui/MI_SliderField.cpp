#include "MI_SliderField.h"

#include "gfx.h"
#include "input.h"
#include "ResourceManager.h"
#include "ui/MI_Image.h"

#include <cmath>

namespace {
void fillRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, r, g, b));
}
} // namespace


/**************************************
 * MI_SliderField Class
 **************************************/

MI_SliderField::MI_SliderField(gfxSprite * nspr, gfxSprite * nsprSlider, short x, short y, const char * name, short width, short indent1, short indent2)
    : MI_SelectField<short>(nspr, x, y, name, width, indent1)
{
    m_indent2 = indent2;
    m_sprSlider = nsprSlider;

    SetPosition(x, y);
}

MI_SliderField::~MI_SliderField()
{}

void MI_SliderField::SetPosition(short x, short y)
{
    MI_SelectField<short>::SetPosition(x, y);
    miModifyImageLeft->SetPosition(m_pos.x + m_indent - 26, m_pos.y + 4);
    miModifyImageRight->SetPosition(m_pos.x + m_width - 16, m_pos.y + 4);
}

void MI_SliderField::Update()
{
    MI_SelectField<short>::Update();
    const float target = static_cast<float>(m_index);
    if (m_knob < 0.0f || std::fabs(target - m_knob) < 0.05f)
        m_knob = target;
    else
        m_knob += (target - m_knob) * 0.4f;    //(a few frames: it slides)
}

void MI_SliderField::drawTrack(int x0, int x1, int y) const
{
    const int count = static_cast<int>(m_items.size());
    if (count <= 0 || x1 <= x0)
        return;
    const bool dim = fLocked || fDisable;
    const int span = x1 - x0;
    auto stepX = [&](float i) { return x0 + (count > 1 ? static_cast<int>(i * static_cast<float>(span) / static_cast<float>(count - 1) + 0.5f) : 0); };

    //The ticks along the top, one a value (the ends taller)
    for (int i = 0; i < count; i++) {
        const int tx = stepX(static_cast<float>(i));
        const bool end = i == 0 || i == count - 1;
        fillRect(tx - 1, y - (end ? 11 : 9), 2, end ? 6 : 4, 16, 24, 48);
    }

    //The groove: a dark rim, dark green inside, a shade along its top
    constexpr int GH = 8;
    const int gy = y - GH / 2;
    fillRect(x0 - 2, gy, span + 4, GH, 16, 24, 48);
    fillRect(x0 - 4, gy + 2, span + 8, GH - 4, 16, 24, 48);
    if (dim)
        fillRect(x0 - 2, gy + 2, span + 4, GH - 4, 110, 116, 120);
    else
        fillRect(x0 - 2, gy + 2, span + 4, GH - 4, 40, 104, 32);
    fillRect(x0 - 2, gy + 2, span + 4, 2, dim ? 84 : 24, dim ? 88 : 72, dim ? 92 : 20);

    //The knob: gold (a coin's), taller than the groove, a shade and a grip line
    constexpr int KW = 12;
    constexpr int KH = 18;
    const float at = m_knob < 0.0f ? static_cast<float>(m_index) : m_knob;
    const int kx = stepX(at) - KW / 2;
    const int ky = y - KH / 2;
    fillRect(kx + 2, ky, KW - 4, KH, 16, 24, 48);
    fillRect(kx, ky + 2, KW, KH - 4, 16, 24, 48);
    if (dim) {
        fillRect(kx + 2, ky + 2, KW - 4, KH - 4, 200, 196, 184);
        fillRect(kx + 2, ky + KH - 6, KW - 4, 4, 150, 146, 136);
    } else {
        fillRect(kx + 2, ky + 2, KW - 4, KH - 4, 248, 208, 48);
        fillRect(kx + 2, ky + KH - 6, KW - 4, 4, 200, 136, 16);
    }
    fillRect(kx + KW / 2 - 1, ky + 5, 2, KH - 11, dim ? 150 : 200, dim ? 146 : 136, dim ? 136 : 16);
}

void MI_SliderField::Draw()
{
    if (!m_visible)
        return;

    m_spr->draw(m_pos.x, m_pos.y, {0, (fSelected ? 32 : 0) + m_adjustmentY, m_indent - 16, 32});
    m_spr->draw(m_pos.x + m_indent - 16, m_pos.y, {0, (fSelected ? 96 : 64), 32, 32});
    m_spr->draw(m_pos.x + m_indent + 16, m_pos.y, {528 - m_width + m_indent, (fSelected ? 32 : 0) + m_adjustmentY, m_width - m_indent - 16, 32});

    rm->menu_font_large.drawScrolling(m_pos.x + 16, m_pos.y + 5, m_indent - 8, m_name.c_str(), fSelected);

    if (!m_items.empty()) {
        rm->menu_font_large.drawScrolling(m_pos.x + m_indent2 + 16, m_pos.y + 5, m_width - m_indent2 - 24, currentItem().name.c_str(), fSelected);
    }

    //(the slider between the joint and the value's name)
    drawTrack(m_pos.x + m_indent + 26, m_pos.x + m_indent2 - 14, m_pos.y + 18);

    const bool drawLeft = m_index > 0;
    if (m_wraps || drawLeft)
        miModifyImageLeft->Draw();

    const bool drawRight = (m_index + 1) < m_items.size();
    if (m_wraps || drawRight)
        miModifyImageRight->Draw();
}

MenuCodeEnum MI_SliderField::SendInput(CPlayerInput * playerInput)
{
    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        if (playerInput->outputControls[iPlayer].menu_scrollfast.fPressed) {
            if (m_index == 0)
                while (moveNext());
            else
                while (movePrev());

            return mcItemChangedCode;
        }
    }

    return MI_SelectField<short>::SendInput(playerInput);
}

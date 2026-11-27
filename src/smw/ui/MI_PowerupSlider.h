#pragma once

#include "ui/MI_SliderField.h"

class gfxSprite;


class MI_PowerupSlider : public MI_SliderField {
public:
    MI_PowerupSlider(gfxSprite* nspr, gfxSprite* nsprSlider, gfxSprite* nsprPowerup, short x, short y, short width, short powerupIndex);

    //Draws every frame
    void Draw() override;
    void SetPosition(short x, short y) override;
    //(its pipe starts 38 to the right, after the item's picture; the sprite's 8 empty pixels)
    Box FocusBox() const override {
        return {static_cast<short>(m_pos.x + 46), m_pos.y, static_cast<short>(m_width - 54), 32};
    }
    Box ValueBox() const override {
        return {static_cast<short>(m_pos.x + 46), m_pos.y, static_cast<short>(m_width - 54), 32};
    }

protected:
    void trackRange(int& x0, int& x1) const override;
    void PlaceArrows();    // its own arrows: in it, not where a slider puts them
    gfxSprite* m_sprPowerup = nullptr;
    short m_powerupIndex = 0;
    short m_halfWidth = 0;
};

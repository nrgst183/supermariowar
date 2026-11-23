#pragma once

#include "ui/MI_SelectField.h"


class MI_SliderField : public MI_SelectField<short> {
public:
    MI_SliderField(gfxSprite * nspr, gfxSprite * nsprSlider, short x, short y, const char * name, short width, short indent1, short indent2);
    ~MI_SliderField() override;

    void SetPosition(short x, short y) override;

    //Slides the knob toward the value
    void Update() override;
    //Draws every frame
    void Draw() override;
    //Sends player input to control on every frame
    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;

protected:
    /// The slider, as the lobby's switch: a tick along the top for each value, a dark green groove
    /// under them from x0 to x1, and a gold knob riding on it at the value (sliding there); its
    /// middle at height y
    void drawTrack(int x0, int x1, int y) const;

    gfxSprite* m_sprSlider;
    short m_indent2;
    float m_knob = -1.0f;    // where the knob is drawn, as a value index (it slides to the value)
};

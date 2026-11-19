#pragma once

#include "uicontrol.h"

#include <array>
#include <memory>
#include <string>

class gfxSprite;
class MI_Image;


class MI_PlayerSelect : public UI_Control {
public:
    MI_PlayerSelect(gfxSprite* nspr, short x, short y, std::string name, short width, short indent);

    void SetImagePosition();

    void Update() override;
    void Draw() override;

    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
    MenuCodeEnum Modify(bool modify) override;
    Box FocusBox() const override {
        return {m_pos.x, m_pos.y, iWidth, 64};
    }
    /// A click on a player's mushroom: the next of off, player and CPU for it
    bool PointerPress(short x, short y, MenuCodeEnum& code) override;

private:
    gfxSprite* spr = nullptr;

    std::string szName;
    short iWidth = 0;
    short iIndent = 0;

    short iSelectedPlayer = 0;
    std::array<short, 4> iPlayerPosition;

    std::unique_ptr<MI_Image> miModifyImage;
};

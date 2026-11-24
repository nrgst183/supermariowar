#pragma once

#include "uicontrol.h"

#include <memory>
#include <string>

class gfxSprite;
class SimpleFileList;
class MI_Image;


class MI_AnnouncerField : public UI_Control {
public:
    MI_AnnouncerField(gfxSprite* nspr, short x, short y, std::string name, short width, short indent, SimpleFileList* pList);
    virtual ~MI_AnnouncerField();

    //Called when user selects this control to change it's value
    MenuCodeEnum Modify(bool modify) override;

    //Updates animations or other events every frame
    void Update() override;

    //Draws every frame
    void Draw() override;
    Box FocusBox() const override {
        //(the pipe, as a select field's: its sprite has 8 empty pixels at each end)
        return {static_cast<short>(m_pos.x + 8), m_pos.y, static_cast<short>(iWidth - 16), 32};
    }
    Box ValueBox() const override {
        return {static_cast<short>(m_pos.x + iIndent - 12), m_pos.y, static_cast<short>(iWidth - iIndent + 4), 32};
    }
    std::array<MI_Image*, 2> ModifyArrows() const override {
        return fModifying ? std::array<MI_Image*, 2> {miModifyImageLeft.get(), miModifyImageRight.get()} : std::array<MI_Image*, 2> {};
    }

    //Sends player input to control on every frame
    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;

protected:
    void UpdateName();

    gfxSprite* spr = nullptr;

    std::string szName;
    short iWidth = 0;
    short iIndent = 0;

    std::string szFieldName;

    std::unique_ptr<MI_Image> miModifyImageLeft;
    std::unique_ptr<MI_Image> miModifyImageRight;

    SimpleFileList* list = nullptr;
};

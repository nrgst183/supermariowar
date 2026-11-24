#pragma once

#include "uicontrol.h"

#include <memory>
#include <string>

class gfxSprite;
class MI_Image;


class MI_TextField : public UI_Control {
public:
    MI_TextField(gfxSprite* nspr, short x, short y, const char * name, short width, short indent);

    void SetTitle(std::string name);

    // Gets the values of the currently selected item
    const std::string& GetValue() const {
        return *szOutValue;
    }

    //Called when user selects this control to change it's value
    MenuCodeEnum Modify(bool modify) override;

    void Clear() {
        szOutValue->clear();
        iCursorIndex = 0;
        iNumChars = 0;
    }

    //Updates animations or other events every frame
    void Update() override;

    //Draws every frame
    void Draw() override;
    //The on-screen keyboard, while it's up
    void DrawOverlay() override;

    /// Whether changing a field opens the on-screen keyboard (the game sets it: with a controller,
    /// and always on the Xbox; none: never)
    static bool (*useOnScreenKeyboard)();

    //Sends player input to control on every frame
    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;

    //When the item is changed, this code will be returned from SendInput()
    void SetItemChangedCode(MenuCodeEnum code) {
        mcItemChangedCode = code;
    }
    void SetControlSelectedCode(MenuCodeEnum code) {
        mcControlSelectedCode = code;
    }

    //Set where the data of this control is written to (some member of game_values probably)
    void SetData(std::string& data, short maxchars);

    MenuCodeEnum MouseClick(short iMouseX, short iMouseY) override;
    Box FocusBox() const override {
        //(the pipe: its sprite has 8 empty pixels at each end)
        return {static_cast<short>(m_pos.x + 8), m_pos.y, static_cast<short>(iWidth - 16), 32};
    }
    Box ValueBox() const override {
        return {static_cast<short>(m_pos.x + iIndent - 12), m_pos.y, static_cast<short>(iWidth - iIndent + 4), 32};
    }

    void Refresh() override;

    void SetDisallowedChars(const char * chars);

protected:
    void UpdateCursor();
    //The on-screen keyboard: the key at a row and column, and pressing it
    MenuCodeEnum OskInput(CPlayerInput* playerInput);
    MenuCodeEnum OskPress();
    bool OskAllowed(char c) const;
    MenuCodeEnum TypeChar(char c);
    MenuCodeEnum Backspace();

    bool m_osk = false;          // the keyboard is up
    short m_oskRow = 1;
    short m_oskCol = 0;
    bool m_oskShift = false;

    short iCursorIndex = 0;
    short iNumChars = 0;
    short iMaxChars = 0;

    gfxSprite* spr = nullptr;
    std::string szName;
    std::string szTempValue;
    std::string* szOutValue = nullptr;

    short iWidth = 0;
    short iIndent = 0;

    std::unique_ptr<MI_Image> miModifyCursor;

    MenuCodeEnum mcItemChangedCode = MENU_CODE_NONE;
    MenuCodeEnum mcControlSelectedCode = MENU_CODE_NONE;

    short iAdjustmentY = 0;
    short iStringWidth = 0;
    short iAllowedWidth = 0;

    std::string szDisallowedChars;
};
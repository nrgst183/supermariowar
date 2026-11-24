#pragma once

#include "GameplayStyles.h"
#include "uicontrol.h"
#include "GlobalConstants.h"

#include <array>
#include <memory>

class MI_DialogBox;
class MI_Button;
class MI_Image;
class MI_PowerupSlider;
class MI_Text;
class UI_Menu;
template<typename T> class MI_SelectField;

class MI_PowerupSelection : public UI_Control {
public:
    MI_PowerupSelection(short x, short y, short width, short numlines);

    MenuCodeEnum Modify(bool modify) override;
    bool ChangingValue() const override;    //(its own menu's)
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;

    void Update() override;
    void Draw() override;

    void MoveNext();
    void MovePrev();

private:
    void AdjustDisplayArrows();

    void SetupPowerupFields();

    short iIndex = 0;
    short iOffset = 0;
    short iNumLines = 0;
    short iTopStop = 0;
    short iBottomStop = 0;

    // TODO: Ownership of most members gets passed to this menu
    std::unique_ptr<UI_Menu> mMenu;

    MI_SelectField<PowerupDropRateStyle>* miOverride = nullptr;
    MI_SelectField<short>* miPreset = nullptr;

    std::array<MI_PowerupSlider*, NUM_POWERUPS> miPowerupSlider;

    MI_Button* miRestoreDefaultsButton = nullptr;
    MI_Button* miClearButton = nullptr;

    MI_DialogBox* miDialogImage = nullptr;
    MI_Text* miDialogAreYouText = nullptr;
    MI_Text* miDialogSureText = nullptr;
    MI_Button* miDialogYesButton = nullptr;
    MI_Button* miDialogNoButton = nullptr;

    MI_Image* miUpArrow = nullptr;
    MI_Image* miDownArrow = nullptr;
};

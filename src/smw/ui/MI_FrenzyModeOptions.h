#ifndef UI_FRENZY_MODE_OPTIONS_H
#define UI_FRENZY_MODE_OPTIONS_H

#include "uimenu.h"
#include "uicontrol.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_PowerupSlider.h"

#include <memory>

class MI_Button;

class MI_FrenzyModeOptions : public UI_Control
{
public:

    MI_FrenzyModeOptions(short x, short y, short width, short numlines);
    ~MI_FrenzyModeOptions() override;

    MenuCodeEnum Modify(bool modify) override;
    bool ChangingValue() const override;    //(its own menu's)
    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
    // The mouse: its fields and sliders (its own menu's), the wheel scrolls the sliders
    Box FocusBox() const override;
    bool PointerHoverInside(short x, short y) override;
    bool PointerClickInside(short x, short y, MenuCodeEnum& code) override;
    bool PointerWheelInside(int dir, MenuCodeEnum& code) override;


    void Update() override;
    void Draw() override;

    void MoveNext();
    void MovePrev();

    void SetRandomGameModeSettings();

    MenuCodeEnum MouseClick(short iMouseX, short iMouseY) override;

    void Refresh() override;

private:

    void SetupPowerupFields();
    void AdjustDisplayArrows();

    short iIndex, iOffset;
    short iTopStop, iBottomStop;
    short iNumLines;
    short iWidth;

    std::unique_ptr<UI_Menu> mMenu;
    void syncToFocus();    // the rows shown follow the focus (the mouse moved it)

    MI_SelectField<short> * miQuantityField;
    MI_SelectField<short> * miRateField;
    MI_SelectField<bool>* miStoredShellsField;
    MI_PowerupSlider * miPowerupSlider[NUMFRENZYCARDS];

    MI_Image * miUpArrow;
    MI_Image * miDownArrow;
};

#endif // UI_FRENZY_MODE_OPTIONS_H

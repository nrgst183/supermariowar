#include "BonusWheelMenu.h"

#include "ui/MI_BonusWheel.h"


UI_BonusWheelMenu::UI_BonusWheelMenu()
    : UI_Menu()
{
    miBonusWheel = new MI_BonusWheel(144, 38);

    AddControl(miBonusWheel, nullptr, nullptr, nullptr, nullptr);
    setInitialFocus(miBonusWheel);
    SetCancelCode(MENU_CODE_BONUS_DONE);
};

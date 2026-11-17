#include "PowerupDropRatesMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Image.h"
#include "ui/MI_PowerupSelection.h"
#include "ui/MI_Text.h"

UI_PowerupDropRatesMenu::UI_PowerupDropRatesMenu()
    : UI_Menu()
{
    miPowerupSelection = new MI_PowerupSelection(50, 44, 640, 8);
    miPowerupSelection->SetAutoModify(true);

    AddControl(miPowerupSelection, nullptr, nullptr, nullptr, nullptr);

    AddTitleBar("Item Selection Menu");

    setInitialFocus(miPowerupSelection);
    SetCancelCode(MENU_CODE_BACK_TO_OPTIONS_MENU);
}

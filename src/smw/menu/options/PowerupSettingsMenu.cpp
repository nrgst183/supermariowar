#include "PowerupSettingsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_StoredPowerupResetButton.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_PowerupSettingsMenu::UI_PowerupSettingsMenu()
    : UI_Menu()
{
    SettingsPage page(7);

    miStoredPowerupDelayField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Item Use Speed", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miStoredPowerupDelayField->add("Very Slow", 2);
    miStoredPowerupDelayField->add("Slow", 3);
    miStoredPowerupDelayField->add("Moderate", 4);
    miStoredPowerupDelayField->add("Fast", 5);
    miStoredPowerupDelayField->add("Very Fast", 6);
    miStoredPowerupDelayField->setOutputPtr(&game_values.storedpowerupdelay);
    miStoredPowerupDelayField->setCurrentValue(game_values.storedpowerupdelay);
    page.add(miStoredPowerupDelayField, "How fast a stored item comes out");

    miItemRespawnField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Item Spawn", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miItemRespawnField->add("Off", 0);
    miItemRespawnField->add("5 Seconds", 310);
    miItemRespawnField->add("10 Seconds", 620);
    miItemRespawnField->add("15 Seconds", 930);
    miItemRespawnField->add("20 Seconds", 1240);
    miItemRespawnField->add("25 Seconds", 1550);
    miItemRespawnField->add("30 Seconds", 1860);
    miItemRespawnField->add("35 Seconds", 2170);
    miItemRespawnField->add("40 Seconds", 2480);
    miItemRespawnField->add("45 Seconds", 2790);
    miItemRespawnField->add("50 Seconds", 3100);
    miItemRespawnField->add("55 Seconds", 3410);
    miItemRespawnField->add("60 Seconds", 3720);
    miItemRespawnField->setOutputPtr(&game_values.itemrespawntime);
    miItemRespawnField->setCurrentValue(game_values.itemrespawntime);
    page.add(miItemRespawnField, "How often items appear at item spots");

    miHiddenBlockRespawnField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Hidden Block Hide", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miHiddenBlockRespawnField->add("Off", 0);
    miHiddenBlockRespawnField->add("5 Seconds", 310);
    miHiddenBlockRespawnField->add("10 Seconds", 620);
    miHiddenBlockRespawnField->add("15 Seconds", 930);
    miHiddenBlockRespawnField->add("20 Seconds", 1240);
    miHiddenBlockRespawnField->add("25 Seconds", 1550);
    miHiddenBlockRespawnField->add("30 Seconds", 1860);
    miHiddenBlockRespawnField->add("35 Seconds", 2170);
    miHiddenBlockRespawnField->add("40 Seconds", 2480);
    miHiddenBlockRespawnField->add("45 Seconds", 2790);
    miHiddenBlockRespawnField->add("50 Seconds", 3100);
    miHiddenBlockRespawnField->add("55 Seconds", 3410);
    miHiddenBlockRespawnField->add("60 Seconds", 3720);
    miHiddenBlockRespawnField->setOutputPtr(&game_values.hiddenblockrespawn);
    miHiddenBlockRespawnField->setCurrentValue(game_values.hiddenblockrespawn);
    page.add(miHiddenBlockRespawnField, "Until a hit hidden block hides again");

    miSwapStyleField = new MI_SelectField<MysteryMushroomSwapStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Swap Style", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miSwapStyleField->add("Walk", MysteryMushroomSwapStyle::Walk);
    miSwapStyleField->add("Blink", MysteryMushroomSwapStyle::Blink);
    miSwapStyleField->add("Instant", MysteryMushroomSwapStyle::Instant);
    miSwapStyleField->setOutputPtr(&game_values.swapstyle);
    miSwapStyleField->setCurrentValue(game_values.swapstyle);
    page.add(miSwapStyleField, "How the Mystery Mushroom swaps players");

    miBonusWheelField = new MI_SelectField<BonusWheelStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Bonus Wheel", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miBonusWheelField->add("Off", BonusWheelStyle::Off);
    miBonusWheelField->add("Tournament Win", BonusWheelStyle::TournamentWin);
    miBonusWheelField->add("Every Game", BonusWheelStyle::EveryGame);
    miBonusWheelField->setOutputPtr(&game_values.bonuswheel);
    miBonusWheelField->setCurrentValue(game_values.bonuswheel);
    page.add(miBonusWheelField, "When the bonus wheel spins");

    miKeepPowerupField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Bonus Item", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miKeepPowerupField->add("Until Next Spin", false);
    miKeepPowerupField->add("Keep Always", true);
    miKeepPowerupField->setOutputPtr(&game_values.keeppowerup);
    miKeepPowerupField->setCurrentValue(game_values.keeppowerup ? 1 : 0);
    miKeepPowerupField->setAutoAdvance(true);
    page.add(miKeepPowerupField, "How long the wheel's item is kept");

    miStoredPowerupResetButton = new MI_StoredPowerupResetButton(&rm->spr_selectfield, page.x(), page.rowY(), "Reset Stored Items", SettingsPage::ROW_W);
    miStoredPowerupResetButton->SetCode(MENU_CODE_RESET_STORED_POWERUPS);
    page.add(miStoredPowerupResetButton, "Clear every player's stored item");

    page.finish(*this, "Item Settings", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

#include "ProjectileLimitsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_ProjectileLimitsMenu::UI_ProjectileLimitsMenu()
    : UI_Menu()
{
    SettingsPage page(9);

    miFireballLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Fireball Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miFireballLimitField->add("Unlimited", 0);
    miFireballLimitField->add("2", 2);
    miFireballLimitField->add("5", 5);
    miFireballLimitField->add("8", 8);
    miFireballLimitField->add("10", 10);
    miFireballLimitField->add("12", 12);
    miFireballLimitField->add("15", 15);
    miFireballLimitField->add("20", 20);
    miFireballLimitField->add("25", 25);
    miFireballLimitField->add("30", 30);
    miFireballLimitField->add("40", 40);
    miFireballLimitField->add("50", 50);
    miFireballLimitField->setOutputPtr(&game_values.fireballlimit);
    miFireballLimitField->setCurrentValue(game_values.fireballlimit);
    page.add(miFireballLimitField, "Fireballs per Fire Flower");

    miHammerLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Hammer Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miHammerLimitField->add("Unlimited", 0);
    miHammerLimitField->add("2", 2);
    miHammerLimitField->add("5", 5);
    miHammerLimitField->add("8", 8);
    miHammerLimitField->add("10", 10);
    miHammerLimitField->add("12", 12);
    miHammerLimitField->add("15", 15);
    miHammerLimitField->add("20", 20);
    miHammerLimitField->add("25", 25);
    miHammerLimitField->add("30", 30);
    miHammerLimitField->add("40", 40);
    miHammerLimitField->add("50", 50);
    miHammerLimitField->setOutputPtr(&game_values.hammerlimit);
    miHammerLimitField->setCurrentValue(game_values.hammerlimit);
    page.add(miHammerLimitField, "Hammers per Hammer Suit");

    miBoomerangLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Boomerang Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miBoomerangLimitField->add("Unlimited", 0);
    miBoomerangLimitField->add("2", 2);
    miBoomerangLimitField->add("5", 5);
    miBoomerangLimitField->add("8", 8);
    miBoomerangLimitField->add("10", 10);
    miBoomerangLimitField->add("12", 12);
    miBoomerangLimitField->add("15", 15);
    miBoomerangLimitField->add("20", 20);
    miBoomerangLimitField->add("25", 25);
    miBoomerangLimitField->add("30", 30);
    miBoomerangLimitField->add("40", 40);
    miBoomerangLimitField->add("50", 50);
    miBoomerangLimitField->setOutputPtr(&game_values.boomeranglimit);
    miBoomerangLimitField->setCurrentValue(game_values.boomeranglimit);
    page.add(miBoomerangLimitField, "Throws per Boomerang Flower");

    miFeatherLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Feather Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miFeatherLimitField->add("Unlimited", 0);
    miFeatherLimitField->add("2", 2);
    miFeatherLimitField->add("5", 5);
    miFeatherLimitField->add("8", 8);
    miFeatherLimitField->add("10", 10);
    miFeatherLimitField->add("12", 12);
    miFeatherLimitField->add("15", 15);
    miFeatherLimitField->add("20", 20);
    miFeatherLimitField->add("25", 25);
    miFeatherLimitField->add("30", 30);
    miFeatherLimitField->add("40", 40);
    miFeatherLimitField->add("50", 50);
    miFeatherLimitField->setOutputPtr(&game_values.featherlimit);
    miFeatherLimitField->setCurrentValue(game_values.featherlimit);
    page.add(miFeatherLimitField, "Uses per Feather");

    miLeafLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Leaf Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miLeafLimitField->add("Unlimited", 0);
    miLeafLimitField->add("2", 2);
    miLeafLimitField->add("5", 5);
    miLeafLimitField->add("8", 8);
    miLeafLimitField->add("10", 10);
    miLeafLimitField->add("12", 12);
    miLeafLimitField->add("15", 15);
    miLeafLimitField->add("20", 20);
    miLeafLimitField->add("25", 25);
    miLeafLimitField->add("30", 30);
    miLeafLimitField->add("40", 40);
    miLeafLimitField->add("50", 50);
    miLeafLimitField->setOutputPtr(&game_values.leaflimit);
    miLeafLimitField->setCurrentValue(game_values.leaflimit);
    page.add(miLeafLimitField, "Tail spins per Super Leaf");

    miPwingsLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "P-Wings Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miPwingsLimitField->add("Unlimited", 0);
    miPwingsLimitField->add("2", 2);
    miPwingsLimitField->add("5", 5);
    miPwingsLimitField->add("8", 8);
    miPwingsLimitField->add("10", 10);
    miPwingsLimitField->add("12", 12);
    miPwingsLimitField->add("15", 15);
    miPwingsLimitField->add("20", 20);
    miPwingsLimitField->add("25", 25);
    miPwingsLimitField->add("30", 30);
    miPwingsLimitField->add("40", 40);
    miPwingsLimitField->add("50", 50);
    miPwingsLimitField->setOutputPtr(&game_values.pwingslimit);
    miPwingsLimitField->setCurrentValue(game_values.pwingslimit);
    page.add(miPwingsLimitField, "Uses per P-Wing");

    miTanookiLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Tanooki Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miTanookiLimitField->add("Unlimited", 0);
    miTanookiLimitField->add("2", 2);
    miTanookiLimitField->add("5", 5);
    miTanookiLimitField->add("8", 8);
    miTanookiLimitField->add("10", 10);
    miTanookiLimitField->add("12", 12);
    miTanookiLimitField->add("15", 15);
    miTanookiLimitField->add("20", 20);
    miTanookiLimitField->add("25", 25);
    miTanookiLimitField->add("30", 30);
    miTanookiLimitField->add("40", 40);
    miTanookiLimitField->add("50", 50);
    miTanookiLimitField->setOutputPtr(&game_values.tanookilimit);
    miTanookiLimitField->setCurrentValue(game_values.tanookilimit);
    page.add(miTanookiLimitField, "Statue turns per Tanooki Suit");

    miBombLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Bomb Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miBombLimitField->add("Unlimited", 0);
    miBombLimitField->add("2", 2);
    miBombLimitField->add("5", 5);
    miBombLimitField->add("8", 8);
    miBombLimitField->add("10", 10);
    miBombLimitField->add("12", 12);
    miBombLimitField->add("15", 15);
    miBombLimitField->add("20", 20);
    miBombLimitField->add("25", 25);
    miBombLimitField->add("30", 30);
    miBombLimitField->add("40", 40);
    miBombLimitField->add("50", 50);
    miBombLimitField->setOutputPtr(&game_values.bombslimit);
    miBombLimitField->setCurrentValue(game_values.bombslimit);
    page.add(miBombLimitField, "Bombs per Bob-omb item");

    miWandLimitField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Wand Limit", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miWandLimitField->add("Unlimited", 0);
    miWandLimitField->add("2", 2);
    miWandLimitField->add("5", 5);
    miWandLimitField->add("8", 8);
    miWandLimitField->add("10", 10);
    miWandLimitField->add("12", 12);
    miWandLimitField->add("15", 15);
    miWandLimitField->add("20", 20);
    miWandLimitField->add("25", 25);
    miWandLimitField->add("30", 30);
    miWandLimitField->add("40", 40);
    miWandLimitField->add("50", 50);
    miWandLimitField->setOutputPtr(&game_values.wandlimit);
    miWandLimitField->setCurrentValue(game_values.wandlimit);
    page.add(miWandLimitField, "Shots per Wand");

    page.finish(*this, "Weapon Use Limits", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

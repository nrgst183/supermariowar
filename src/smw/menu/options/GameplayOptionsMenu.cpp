#include "GameplayOptionsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_GameplayOptionsMenu::UI_GameplayOptionsMenu()
    : UI_Menu()
{
    SettingsPage page(10);

    miRespawnField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Respawn Time", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miRespawnField->add("Instant", 0);
    miRespawnField->add("0.5 Seconds", 1);
    miRespawnField->add("1.0 Seconds", 2);
    miRespawnField->add("1.5 Seconds", 3);
    miRespawnField->add("2.0 Seconds", 4);
    miRespawnField->add("2.5 Seconds", 5);
    miRespawnField->add("3.0 Seconds", 6);
    miRespawnField->add("3.5 Seconds", 7);
    miRespawnField->add("4.0 Seconds", 8);
    miRespawnField->add("4.5 Seconds", 9);
    miRespawnField->add("5.0 Seconds", 10);
    miRespawnField->add("5.5 Seconds", 11);
    miRespawnField->add("6.0 Seconds", 12);
    miRespawnField->add("6.5 Seconds", 13);
    miRespawnField->add("7.0 Seconds", 14);
    miRespawnField->add("7.5 Seconds", 15);
    miRespawnField->add("8.0 Seconds", 16);
    miRespawnField->add("8.5 Seconds", 17);
    miRespawnField->add("9.0 Seconds", 18);
    miRespawnField->add("9.5 Seconds", 19);
    miRespawnField->add("10.0 Seconds", 20);
    miRespawnField->setOutputPtr(&game_values.respawn);
    miRespawnField->setCurrentValue(game_values.respawn);
    page.add(miRespawnField, "How long a player waits to come back after dying");

    miShieldStyleField = new MI_SelectField<ShieldStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Shield Style", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miShieldStyleField->add("No Shield", ShieldStyle::NoShield);
    miShieldStyleField->add("Soft", ShieldStyle::Soft);
    miShieldStyleField->add("Soft with Stomp", ShieldStyle::SoftWithStomp);
    miShieldStyleField->add("Hard", ShieldStyle::Hard);
    miShieldStyleField->setOutputPtr(&game_values.shieldstyle);
    miShieldStyleField->setCurrentValue(game_values.shieldstyle);
    page.add(miShieldStyleField, "The shield you get right after respawning");

    miShieldTimeField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Shield Time", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miShieldTimeField->add("0.5 Seconds", 31);
    miShieldTimeField->add("1.0 Seconds", 62);
    miShieldTimeField->add("1.5 Seconds", 93);
    miShieldTimeField->add("2.0 Seconds", 124);
    miShieldTimeField->add("2.5 Seconds", 155);
    miShieldTimeField->add("3.0 Seconds", 186);
    miShieldTimeField->add("3.5 Seconds", 217);
    miShieldTimeField->add("4.0 Seconds", 248);
    miShieldTimeField->add("4.5 Seconds", 279);
    miShieldTimeField->add("5.0 Seconds", 310);
    miShieldTimeField->setOutputPtr(&game_values.shieldtime);
    miShieldTimeField->setCurrentValue(game_values.shieldtime);
    page.add(miShieldTimeField, "How long that shield lasts");

    miBoundsTimeField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Bounds Time", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miBoundsTimeField->add("Infinite", 0);
    miBoundsTimeField->add("1 Second", 1);
    miBoundsTimeField->add("2 Seconds", 2);
    miBoundsTimeField->add("3 Seconds", 3);
    miBoundsTimeField->add("4 Seconds", 4);
    miBoundsTimeField->add("5 Seconds", 5);
    miBoundsTimeField->add("6 Seconds", 6);
    miBoundsTimeField->add("7 Seconds", 7);
    miBoundsTimeField->add("8 Seconds", 8);
    miBoundsTimeField->add("9 Seconds", 9);
    miBoundsTimeField->add("10 Seconds", 10);
    miBoundsTimeField->setOutputPtr(&game_values.outofboundstime);
    miBoundsTimeField->setCurrentValue(game_values.outofboundstime);
    page.add(miBoundsTimeField, "How long a player can stay above the top of the screen");

    miSuicideTimeField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Suicide Time", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miSuicideTimeField->add("Off", 0);
    miSuicideTimeField->add("3 Seconds", 186);
    miSuicideTimeField->add("5 Seconds", 310);
    miSuicideTimeField->add("8 Seconds", 496);
    miSuicideTimeField->add("10 Seconds", 620);
    miSuicideTimeField->add("15 Seconds", 930);
    miSuicideTimeField->add("20 Seconds", 1240);
    miSuicideTimeField->setOutputPtr(&game_values.suicidetime);
    miSuicideTimeField->setCurrentValue(game_values.suicidetime);
    page.add(miSuicideTimeField, "Kills a player stuck in place this long");

    miWarpLockStyleField = new MI_SelectField<WarpLockStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Warp Lock Style", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miWarpLockStyleField->add("Entrance Only", WarpLockStyle::EntranceOnly);
    miWarpLockStyleField->add("Exit Only", WarpLockStyle::ExitOnly);
    miWarpLockStyleField->add("Entrance and Exit", WarpLockStyle::EntranceAndExit);
    miWarpLockStyleField->add("Entire Connection", WarpLockStyle::EntireConnection);
    miWarpLockStyleField->add("All Warps", WarpLockStyle::AllWarps);
    miWarpLockStyleField->setOutputPtr(&game_values.warplockstyle);
    miWarpLockStyleField->setCurrentValue(game_values.warplockstyle);
    page.add(miWarpLockStyleField, "Which pipes lock for a moment after someone uses one");

    miWarpLockTimeField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Warp Lock Time", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miWarpLockTimeField->add("Off", 0);
    miWarpLockTimeField->add("1 Second", 62);
    miWarpLockTimeField->add("2 Seconds", 124);
    miWarpLockTimeField->add("3 Seconds", 186);
    miWarpLockTimeField->add("4 Seconds", 248);
    miWarpLockTimeField->add("5 Seconds", 310);
    miWarpLockTimeField->add("6 Seconds", 372);
    miWarpLockTimeField->add("7 Seconds", 434);
    miWarpLockTimeField->add("8 Seconds", 496);
    miWarpLockTimeField->add("9 Seconds", 558);
    miWarpLockTimeField->add("10 Seconds", 620);
    miWarpLockTimeField->setOutputPtr(&game_values.warplocktime);
    miWarpLockTimeField->setCurrentValue(game_values.warplocktime);
    page.add(miWarpLockTimeField, "How long a used pipe stays locked");

    miBotsField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Bot Difficulty", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miBotsField->add("Very Easy", 0);
    miBotsField->add("Easy", 1);
    miBotsField->add("Moderate", 2);
    miBotsField->add("Hard", 3);
    miBotsField->add("Very Hard", 4);
    miBotsField->setOutputPtr(&game_values.cpudifficulty);
    miBotsField->setCurrentValue(game_values.cpudifficulty);
    page.add(miBotsField, "How well CPU players play");

    miPointSpeedField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Point Speed", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miPointSpeedField->add("Very Slow", 60);
    miPointSpeedField->add("Slow", 40);
    miPointSpeedField->add("Moderate", 20);
    miPointSpeedField->add("Fast", 10);
    miPointSpeedField->add("Very Fast", 5);
    miPointSpeedField->setOutputPtr(&game_values.pointspeed);
    miPointSpeedField->setCurrentValue(game_values.pointspeed);
    page.add(miPointSpeedField, "How fast points rise in modes like Tag and Chicken");

    miSecretsField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Secrets", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miSecretsField->add("Off", false);
    miSecretsField->add("On", true);
    miSecretsField->setOutputPtr(&game_values.secretsenabled);
    miSecretsField->setCurrentValue(game_values.secretsenabled ? 1 : 0);
    page.add(miSecretsField, "Allow hidden unlockables to turn up in matches");

    page.finish(*this, "Gameplay Options", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

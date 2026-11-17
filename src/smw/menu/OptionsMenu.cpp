#include "OptionsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"

UI_OptionsMenu::UI_OptionsMenu()
    : UI_Menu()
{
    miGameplayOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 40, "Gameplay", 400, TextAlign::CENTER);
    miGameplayOptionsMenuButton->SetCode(MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU);

    miTeamOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 80, "Team", 400, TextAlign::CENTER);
    miTeamOptionsMenuButton->SetCode(MENU_CODE_TO_TEAM_OPTIONS_MENU);

    miPowerupOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 120, "Item Selection", 400, TextAlign::CENTER);
    miPowerupOptionsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SELECTION_MENU);

    miPowerupSettingsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 160, "Item Settings", 400, TextAlign::CENTER);
    miPowerupSettingsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SETTINGS_MENU);

    miProjectilesOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 200, "Weapons & Projectiles", 400, TextAlign::CENTER);
    miProjectilesOptionsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_OPTIONS_MENU);

    miProjectilesLimitsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 240, "Weapon Use Limits", 400, TextAlign::CENTER);
    miProjectilesLimitsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_LIMITS_MENU);

    miGraphicsOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 280, "Graphics", 400, TextAlign::CENTER);
    miGraphicsOptionsMenuButton->SetCode(MENU_CODE_TO_GRAPHICS_OPTIONS_MENU);

    miEyeCandyOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 120, 320, "Eye Candy", 400, TextAlign::CENTER);
    miEyeCandyOptionsMenuButton->SetCode(MENU_CODE_TO_EYECANDY_OPTIONS_MENU);

    miSoundOptionsMenuButton = new MI_Button((game_values.soundcapable ? &rm->spr_selectfield : &rm->spr_selectfielddisabled), 120, 360, "Music & Sound", 400, TextAlign::CENTER);

    if (game_values.soundcapable)
        miSoundOptionsMenuButton->SetCode(MENU_CODE_TO_SOUND_OPTIONS_MENU);

    miGenerateMapThumbsButton = new MI_Button(&rm->spr_selectfield, 120, 400, "Refresh Maps", 400, TextAlign::CENTER);
    miGenerateMapThumbsButton->SetCode(MENU_CODE_SAVE_ALL_MAP_THUMBNAILS);

    miOptionsMenuBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miOptionsMenuBackButton->SetCode(MENU_CODE_TO_MAIN_MENU);

    miGenerateThumbsDialogImage = new MI_Image(&rm->spr_dialog, 224, 176, 0, 0, 192, 128, 1, 1, 0);
    miGenerateThumbsDialogAreYouText = new MI_HeaderText("Are You", 320, 195);
    miGenerateThumbsDialogSureText = new MI_HeaderText("Sure?", 320, 220);
    miGenerateThumbsDialogYesButton = new MI_Button(&rm->spr_selectfield, 235, 250, "Yes", 80, TextAlign::CENTER);
    miGenerateThumbsDialogNoButton = new MI_Button(&rm->spr_selectfield, 325, 250, "No", 80, TextAlign::CENTER);

    miGenerateThumbsDialogYesButton->SetCode(MENU_CODE_GENERATE_THUMBS_RESET_YES);
    miGenerateThumbsDialogNoButton->SetCode(MENU_CODE_GENERATE_THUMBS_RESET_NO);

    miGenerateThumbsDialogImage->setVisible(false);
    miGenerateThumbsDialogAreYouText->setVisible(false);
    miGenerateThumbsDialogSureText->setVisible(false);
    miGenerateThumbsDialogYesButton->setVisible(false);
    miGenerateThumbsDialogNoButton->setVisible(false);

    AddControl(miGameplayOptionsMenuButton, miOptionsMenuBackButton, miTeamOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miTeamOptionsMenuButton, miGameplayOptionsMenuButton, miPowerupOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miPowerupOptionsMenuButton, miTeamOptionsMenuButton, miPowerupSettingsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miPowerupSettingsMenuButton, miPowerupOptionsMenuButton, miProjectilesOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miProjectilesOptionsMenuButton, miPowerupSettingsMenuButton, miProjectilesLimitsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miProjectilesLimitsMenuButton, miProjectilesOptionsMenuButton, miGraphicsOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miGraphicsOptionsMenuButton, miProjectilesLimitsMenuButton, miEyeCandyOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miEyeCandyOptionsMenuButton, miGraphicsOptionsMenuButton, miSoundOptionsMenuButton, nullptr, miOptionsMenuBackButton);
    AddControl(miSoundOptionsMenuButton, miEyeCandyOptionsMenuButton, miGenerateMapThumbsButton, nullptr, miOptionsMenuBackButton);
    AddControl(miGenerateMapThumbsButton, miSoundOptionsMenuButton, miOptionsMenuBackButton, nullptr, miOptionsMenuBackButton);

    AddControl(miOptionsMenuBackButton, miGenerateMapThumbsButton, miGameplayOptionsMenuButton, miGenerateMapThumbsButton, nullptr);

    AddTitleBar("Options Menu");

    AddNonControl(miGenerateThumbsDialogImage);
    AddNonControl(miGenerateThumbsDialogAreYouText);
    AddNonControl(miGenerateThumbsDialogSureText);

    AddControl(miGenerateThumbsDialogYesButton, nullptr, nullptr, nullptr, miGenerateThumbsDialogNoButton);
    AddControl(miGenerateThumbsDialogNoButton, nullptr, nullptr, miGenerateThumbsDialogYesButton, nullptr);

    setInitialFocus(miGameplayOptionsMenuButton);
    SetCancelCode(MENU_CODE_TO_MAIN_MENU);
};

void UI_OptionsMenu::showThumbnailsPopup()
{
    miGenerateThumbsDialogImage->setVisible(true);
    miGenerateThumbsDialogAreYouText->setVisible(true);
    miGenerateThumbsDialogSureText->setVisible(true);
    miGenerateThumbsDialogYesButton->setVisible(true);
    miGenerateThumbsDialogNoButton->setVisible(true);

    RememberCurrent();

    setInitialFocus(miGenerateThumbsDialogNoButton);
    SetCancelCode(MENU_CODE_GENERATE_THUMBS_RESET_NO);
}

void UI_OptionsMenu::hideThumbnailsPopup()
{
    miGenerateThumbsDialogImage->setVisible(false);
    miGenerateThumbsDialogAreYouText->setVisible(false);
    miGenerateThumbsDialogSureText->setVisible(false);
    miGenerateThumbsDialogYesButton->setVisible(false);
    miGenerateThumbsDialogNoButton->setVisible(false);

    setInitialFocus(miGameplayOptionsMenuButton);
    SetCancelCode(MENU_CODE_TO_MAIN_MENU);

    RestoreCurrent();
}

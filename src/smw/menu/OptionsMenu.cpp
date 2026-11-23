#include "OptionsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_Text.h"

UI_OptionsMenu::UI_OptionsMenu()
    : UI_Menu()
{
    // The settings in groups, two to a row: the game, the items and weapons, the look and sound
    // (and online: the nickname, run-ahead and port; builds with the network, not the Xbox's)
    constexpr short LEFT_X = 64;
    constexpr short RIGHT_X = 326;
    constexpr short BUTTON_W = 250;
    AddNonControl(new MI_MenuPanel(36, 42, 568, 76, "Game", "The rules of every match"));
    AddNonControl(new MI_MenuPanel(36, 122, 568, 112, "Items and Weapons", "What appears and how it works"));
    AddNonControl(new MI_MenuPanel(36, 238, 568, 112, "Look and Sound"));

    miGameplayOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, 78, "Gameplay", BUTTON_W, TextAlign::CENTER);
    miGameplayOptionsMenuButton->SetCode(MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU);

    miTeamOptionsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, 78, "Team", BUTTON_W, TextAlign::CENTER);
    miTeamOptionsMenuButton->SetCode(MENU_CODE_TO_TEAM_OPTIONS_MENU);

    miPowerupOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, 158, "Item Selection", BUTTON_W, TextAlign::CENTER);
    miPowerupOptionsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SELECTION_MENU);

    miPowerupSettingsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, 158, "Item Settings", BUTTON_W, TextAlign::CENTER);
    miPowerupSettingsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SETTINGS_MENU);

    miProjectilesOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, 194, "Weapons & Projectiles", BUTTON_W, TextAlign::CENTER);
    miProjectilesOptionsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_OPTIONS_MENU);

    miProjectilesLimitsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, 194, "Weapon Use Limits", BUTTON_W, TextAlign::CENTER);
    miProjectilesLimitsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_LIMITS_MENU);

    miGraphicsOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, 274, "Graphics", BUTTON_W, TextAlign::CENTER);
    miGraphicsOptionsMenuButton->SetCode(MENU_CODE_TO_GRAPHICS_OPTIONS_MENU);

    miEyeCandyOptionsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, 274, "Eye Candy", BUTTON_W, TextAlign::CENTER);
    miEyeCandyOptionsMenuButton->SetCode(MENU_CODE_TO_EYECANDY_OPTIONS_MENU);

    miSoundOptionsMenuButton = new MI_Button((game_values.soundcapable ? &rm->spr_selectfield : &rm->spr_selectfielddisabled), LEFT_X, 310, "Music & Sound", BUTTON_W, TextAlign::CENTER);

    if (game_values.soundcapable)
        miSoundOptionsMenuButton->SetCode(MENU_CODE_TO_SOUND_OPTIONS_MENU);

    miGenerateMapThumbsButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, 310, "Refresh Maps", BUTTON_W, TextAlign::CENTER);
    miGenerateMapThumbsButton->SetCode(MENU_CODE_SAVE_ALL_MAP_THUMBNAILS);

#if !defined(NETWORK_DISABLED) && !defined(__EMSCRIPTEN__)
    AddNonControl(new MI_MenuPanel(36, 354, 568, 76, "Online", "Your nickname, port and ping bars"));
    miOnlineOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 320 - BUTTON_W / 2, 390, "Online", BUTTON_W, TextAlign::CENTER);
    miOnlineOptionsMenuButton->SetCode(MENU_CODE_TO_ONLINE_OPTIONS_MENU);
#endif

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

    // Up and down in a column (round from the bottom to the top), left and right in a row; B (or the
    // footer's "back"): back
    UI_Control* const last = miOnlineOptionsMenuButton ? static_cast<UI_Control*>(miOnlineOptionsMenuButton) : nullptr;
    AddControl(miGameplayOptionsMenuButton, last ? last : miSoundOptionsMenuButton, miPowerupOptionsMenuButton, nullptr, miTeamOptionsMenuButton);
    AddControl(miTeamOptionsMenuButton, last ? last : miGenerateMapThumbsButton, miPowerupSettingsMenuButton, miGameplayOptionsMenuButton, nullptr);
    AddControl(miPowerupOptionsMenuButton, miGameplayOptionsMenuButton, miProjectilesOptionsMenuButton, nullptr, miPowerupSettingsMenuButton);
    AddControl(miPowerupSettingsMenuButton, miTeamOptionsMenuButton, miProjectilesLimitsMenuButton, miPowerupOptionsMenuButton, nullptr);
    AddControl(miProjectilesOptionsMenuButton, miPowerupOptionsMenuButton, miGraphicsOptionsMenuButton, nullptr, miProjectilesLimitsMenuButton);
    AddControl(miProjectilesLimitsMenuButton, miPowerupSettingsMenuButton, miEyeCandyOptionsMenuButton, miProjectilesOptionsMenuButton, nullptr);
    AddControl(miGraphicsOptionsMenuButton, miProjectilesOptionsMenuButton, miSoundOptionsMenuButton, nullptr, miEyeCandyOptionsMenuButton);
    AddControl(miEyeCandyOptionsMenuButton, miProjectilesLimitsMenuButton, miGenerateMapThumbsButton, miGraphicsOptionsMenuButton, nullptr);
    AddControl(miSoundOptionsMenuButton, miGraphicsOptionsMenuButton, last ? last : miGameplayOptionsMenuButton, nullptr, miGenerateMapThumbsButton);
    AddControl(miGenerateMapThumbsButton, miEyeCandyOptionsMenuButton, last ? last : miTeamOptionsMenuButton, miSoundOptionsMenuButton, nullptr);
    if (miOnlineOptionsMenuButton)
        AddControl(miOnlineOptionsMenuButton, miSoundOptionsMenuButton, miGameplayOptionsMenuButton, nullptr, nullptr);

    AddTitleBar("Options");

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

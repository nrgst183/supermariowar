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
    // The settings in groups, two to a row: the game (and online: the nickname, run-ahead and port;
    // builds with the network, not the Xbox's), the items and weapons, the look and sound
    //(each panel: its title, its rows 36 apart, as much room under the last as above the first)
    constexpr short LEFT_X = 64;
    constexpr short RIGHT_X = 326;
    constexpr short BUTTON_W = 250;
#if !defined(NETWORK_DISABLED) && !defined(__EMSCRIPTEN__)
    constexpr short GAME_ROWS = 2;
#else
    constexpr short GAME_ROWS = 1;
#endif
    auto panelHeight = [](short rows) { return static_cast<short>(36 + 36 * rows + 12); };
    const short gameY = GAME_ROWS == 2 ? 52 : 70;
    const short itemsY = gameY + panelHeight(GAME_ROWS) + 4;
    const short lookY = itemsY + panelHeight(2) + 4;
    AddNonControl(new MI_MenuPanel(36, gameY, 568, panelHeight(GAME_ROWS), "Game",
        GAME_ROWS == 2 ? "The rules of every match, and online" : "The rules of every match"));
    AddNonControl(new MI_MenuPanel(36, itemsY, 568, panelHeight(2), "Items and Weapons", "What appears and how it works"));
    AddNonControl(new MI_MenuPanel(36, lookY, 568, panelHeight(2), "Look and Sound"));

    miGameplayOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, gameY + 36, "Gameplay", BUTTON_W, TextAlign::CENTER);
    miGameplayOptionsMenuButton->SetCode(MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU);

    miTeamOptionsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, gameY + 36, "Team", BUTTON_W, TextAlign::CENTER);
    miTeamOptionsMenuButton->SetCode(MENU_CODE_TO_TEAM_OPTIONS_MENU);

#if !defined(NETWORK_DISABLED) && !defined(__EMSCRIPTEN__)
    miOnlineOptionsMenuButton = new MI_Button(&rm->spr_selectfield, 320 - BUTTON_W / 2, gameY + 72, "Online", BUTTON_W, TextAlign::CENTER);
    miOnlineOptionsMenuButton->SetCode(MENU_CODE_TO_ONLINE_OPTIONS_MENU);
#endif

    miPowerupOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, itemsY + 36, "Item Selection", BUTTON_W, TextAlign::CENTER);
    miPowerupOptionsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SELECTION_MENU);

    miPowerupSettingsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, itemsY + 36, "Item Settings", BUTTON_W, TextAlign::CENTER);
    miPowerupSettingsMenuButton->SetCode(MENU_CODE_TO_POWERUP_SETTINGS_MENU);

    miProjectilesOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, itemsY + 72, "Weapons & Projectiles", BUTTON_W, TextAlign::CENTER);
    miProjectilesOptionsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_OPTIONS_MENU);

    miProjectilesLimitsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, itemsY + 72, "Weapon Use Limits", BUTTON_W, TextAlign::CENTER);
    miProjectilesLimitsMenuButton->SetCode(MENU_CODE_TO_PROJECTILES_LIMITS_MENU);

    miGraphicsOptionsMenuButton = new MI_Button(&rm->spr_selectfield, LEFT_X, lookY + 36, "Graphics", BUTTON_W, TextAlign::CENTER);
    miGraphicsOptionsMenuButton->SetCode(MENU_CODE_TO_GRAPHICS_OPTIONS_MENU);

    miEyeCandyOptionsMenuButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, lookY + 36, "Eye Candy", BUTTON_W, TextAlign::CENTER);
    miEyeCandyOptionsMenuButton->SetCode(MENU_CODE_TO_EYECANDY_OPTIONS_MENU);

    miSoundOptionsMenuButton = new MI_Button((game_values.soundcapable ? &rm->spr_selectfield : &rm->spr_selectfielddisabled), LEFT_X, lookY + 72, "Music & Sound", BUTTON_W, TextAlign::CENTER);

    if (game_values.soundcapable)
        miSoundOptionsMenuButton->SetCode(MENU_CODE_TO_SOUND_OPTIONS_MENU);

    miGenerateMapThumbsButton = new MI_Button(&rm->spr_selectfield, RIGHT_X, lookY + 72, "Refresh Maps", BUTTON_W, TextAlign::CENTER);
    miGenerateMapThumbsButton->SetCode(MENU_CODE_SAVE_ALL_MAP_THUMBNAILS);

    miGenerateThumbsDialogImage = new MI_DialogBox();
    miGenerateThumbsDialogAreYouText = new MI_HeaderText("Refresh all maps?", 320, 195);
    miGenerateThumbsDialogSureText = new MI_HeaderText("Their pictures are made again", 320, 220);
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
    UI_Control* const online = miOnlineOptionsMenuButton;
    AddControl(miGameplayOptionsMenuButton, miSoundOptionsMenuButton, online ? online : miPowerupOptionsMenuButton, nullptr, miTeamOptionsMenuButton);
    AddControl(miTeamOptionsMenuButton, miGenerateMapThumbsButton, online ? online : miPowerupSettingsMenuButton, miGameplayOptionsMenuButton, nullptr);
    if (online)
        AddControl(online, miGameplayOptionsMenuButton, miPowerupOptionsMenuButton, nullptr, nullptr);
    AddControl(miPowerupOptionsMenuButton, online ? online : miGameplayOptionsMenuButton, miProjectilesOptionsMenuButton, nullptr, miPowerupSettingsMenuButton);
    AddControl(miPowerupSettingsMenuButton, online ? online : miTeamOptionsMenuButton, miProjectilesLimitsMenuButton, miPowerupOptionsMenuButton, nullptr);
    AddControl(miProjectilesOptionsMenuButton, miPowerupOptionsMenuButton, miGraphicsOptionsMenuButton, nullptr, miProjectilesLimitsMenuButton);
    AddControl(miProjectilesLimitsMenuButton, miPowerupSettingsMenuButton, miEyeCandyOptionsMenuButton, miProjectilesOptionsMenuButton, nullptr);
    AddControl(miGraphicsOptionsMenuButton, miProjectilesOptionsMenuButton, miSoundOptionsMenuButton, nullptr, miEyeCandyOptionsMenuButton);
    AddControl(miEyeCandyOptionsMenuButton, miProjectilesLimitsMenuButton, miGenerateMapThumbsButton, miGraphicsOptionsMenuButton, nullptr);
    AddControl(miSoundOptionsMenuButton, miGraphicsOptionsMenuButton, miGameplayOptionsMenuButton, nullptr, miGenerateMapThumbsButton);
    AddControl(miGenerateMapThumbsButton, miEyeCandyOptionsMenuButton, miTeamOptionsMenuButton, miSoundOptionsMenuButton, nullptr);

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

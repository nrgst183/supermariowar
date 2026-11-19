#include "PlayerControlsSelectMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_PlayerControlsSelectMenu::UI_PlayerControlsSelectMenu()
    : UI_Menu()
{
    SettingsPage page(5);

    miPlayer1ControlsButton = new MI_Button(&rm->spr_selectfield, page.x(), page.rowY(), "Player 1", SettingsPage::ROW_W, TextAlign::CENTER);
    miPlayer1ControlsButton->SetCode(MENU_CODE_TO_PLAYER_1_CONTROLS);
    page.add(miPlayer1ControlsButton, "Set up player 1's keys or controller");

    miPlayer2ControlsButton = new MI_Button(&rm->spr_selectfield, page.x(), page.rowY(), "Player 2", SettingsPage::ROW_W, TextAlign::CENTER);
    miPlayer2ControlsButton->SetCode(MENU_CODE_TO_PLAYER_2_CONTROLS);
    page.add(miPlayer2ControlsButton, "Set up player 2's keys or controller");

    miPlayer3ControlsButton = new MI_Button(&rm->spr_selectfield, page.x(), page.rowY(), "Player 3", SettingsPage::ROW_W, TextAlign::CENTER);
    miPlayer3ControlsButton->SetCode(MENU_CODE_TO_PLAYER_3_CONTROLS);
    page.add(miPlayer3ControlsButton, "Set up player 3's keys or controller");

    miPlayer4ControlsButton = new MI_Button(&rm->spr_selectfield, page.x(), page.rowY(), "Player 4", SettingsPage::ROW_W, TextAlign::CENTER);
    miPlayer4ControlsButton->SetCode(MENU_CODE_TO_PLAYER_4_CONTROLS);
    page.add(miPlayer4ControlsButton, "Set up player 4's keys or controller");

    //The controller buttons the theater and the editors show
    miButtonPromptsField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Button Prompts", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miButtonPromptsField->add("Auto", 0);
    miButtonPromptsField->add("Xbox", 1);
    miButtonPromptsField->add("Original Xbox", 2);
    miButtonPromptsField->setOutputPtr(&game_values.buttonprompts);
    miButtonPromptsField->setCurrentValue(game_values.buttonprompts);
    page.add(miButtonPromptsField, "Button icons to show (Auto matches your controller)");

    miPlayerControlsBackButton = page.finish(*this, "Player Controls", MENU_CODE_TO_MAIN_MENU);
}

#include "PlayerControlsSelectMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"


UI_PlayerControlsSelectMenu::UI_PlayerControlsSelectMenu()
    : UI_Menu()
{
    miPlayer1ControlsButton = new MI_Button(&rm->spr_selectfield, 120, 140, "Player 1", 400, TextAlign::CENTER);
    miPlayer1ControlsButton->SetCode(MENU_CODE_TO_PLAYER_1_CONTROLS);

    miPlayer2ControlsButton = new MI_Button(&rm->spr_selectfield, 120, 180, "Player 2", 400, TextAlign::CENTER);
    miPlayer2ControlsButton->SetCode(MENU_CODE_TO_PLAYER_2_CONTROLS);

    miPlayer3ControlsButton = new MI_Button(&rm->spr_selectfield, 120, 220, "Player 3", 400, TextAlign::CENTER);
    miPlayer3ControlsButton->SetCode(MENU_CODE_TO_PLAYER_3_CONTROLS);

    miPlayer4ControlsButton = new MI_Button(&rm->spr_selectfield, 120, 260, "Player 4", 400, TextAlign::CENTER);
    miPlayer4ControlsButton->SetCode(MENU_CODE_TO_PLAYER_4_CONTROLS);

    //The controller buttons the theater and the editors show
    miButtonPromptsField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 320, "Button Prompts", 500, 220);
    miButtonPromptsField->add("Auto", 0);
    miButtonPromptsField->add("Xbox", 1);
    miButtonPromptsField->add("Original Xbox", 2);
    miButtonPromptsField->setOutputPtr(&game_values.buttonprompts);
    miButtonPromptsField->setCurrentValue(game_values.buttonprompts);

    miPlayerControlsBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miPlayerControlsBackButton->SetCode(MENU_CODE_TO_MAIN_MENU);

    miPlayerControlsLeftHeaderBar = new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0);
    miPlayerControlsMenuRightHeaderBar = new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0);
    miPlayerControlsMenuHeaderText = new MI_HeaderText("Player Controls Menu", 320, 5);

    AddControl(miPlayer1ControlsButton, miPlayerControlsBackButton, miPlayer2ControlsButton, nullptr, miPlayerControlsBackButton);
    AddControl(miPlayer2ControlsButton, miPlayer1ControlsButton, miPlayer3ControlsButton, nullptr, miPlayerControlsBackButton);
    AddControl(miPlayer3ControlsButton, miPlayer2ControlsButton, miPlayer4ControlsButton, nullptr, miPlayerControlsBackButton);
    AddControl(miPlayer4ControlsButton, miPlayer3ControlsButton, miButtonPromptsField, nullptr, miPlayerControlsBackButton);
    AddControl(miButtonPromptsField, miPlayer4ControlsButton, miPlayerControlsBackButton, nullptr, miPlayerControlsBackButton);
    AddControl(miPlayerControlsBackButton, miButtonPromptsField, miPlayer1ControlsButton, miButtonPromptsField, nullptr);

    AddNonControl(miPlayerControlsLeftHeaderBar);
    AddNonControl(miPlayerControlsMenuRightHeaderBar);
    AddNonControl(miPlayerControlsMenuHeaderText);

    setInitialFocus(miPlayer1ControlsButton);
    SetCancelCode(MENU_CODE_TO_MAIN_MENU);
}

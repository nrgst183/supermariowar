#include "EditorsMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"

UI_EditorsMenu::UI_EditorsMenu()
    : UI_Menu()
{
    auto* levelButton = new MI_Button(&rm->spr_selectfield, 120, 150, "Map Editor", 400, TextAlign::CENTER);
    levelButton->SetCode(MENU_CODE_RUN_LEVEL_EDITOR);
    auto* worldButton = new MI_Button(&rm->spr_selectfield, 120, 230, "World Editor", 400, TextAlign::CENTER);
    worldButton->SetCode(MENU_CODE_RUN_WORLD_EDITOR);
    auto* backButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    backButton->SetCode(MENU_CODE_EDITORS_EXIT);

    AddControl(levelButton, backButton, worldButton, nullptr, backButton);
    AddControl(worldButton, levelButton, backButton, nullptr, backButton);
    AddControl(backButton, worldButton, levelButton, worldButton, nullptr);
    setInitialFocus(levelButton);
    SetCancelCode(MENU_CODE_EDITORS_EXIT);

    AddNonControl(new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0));
    AddNonControl(new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0));
    AddNonControl(new MI_HeaderText("Editors", 320, 5));
    AddNonControl(new MI_Text("Make and change the maps you play on", 320, 186, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Make and change worlds: the maps between the stages", 320, 266, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Mouse and keyboard, or a controller: Start shows every action", 320, 330, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Back leaves the editor (it asks first)", 320, 350, 0, false, TextAlign::CENTER));
}

#include "EditorsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"
#include "ui/MI_TextField.h"

UI_EditorsMenu::UI_EditorsMenu()
    : UI_Menu()
{
    //Alone
    auto* levelButton = new MI_Button(&rm->spr_selectfield, 120, 64, "Map Editor", 400, TextAlign::CENTER);
    levelButton->SetCode(MENU_CODE_RUN_LEVEL_EDITOR);
    auto* worldButton = new MI_Button(&rm->spr_selectfield, 120, 104, "World Editor", 400, TextAlign::CENTER);
    worldButton->SetCode(MENU_CODE_RUN_WORLD_EDITOR);

    //Together: host one, or join
    auto* hostLevelButton = new MI_Button(&rm->spr_selectfield, 120, 194, "Host Map", 200, TextAlign::CENTER);
    hostLevelButton->SetCode(MENU_CODE_HOST_LEVEL_EDITOR);
    auto* hostWorldButton = new MI_Button(&rm->spr_selectfield, 320, 194, "Host World", 200, TextAlign::CENTER);
    hostWorldButton->SetCode(MENU_CODE_HOST_WORLD_EDITOR);

    //(the online menu's address, without its port: the editors have their own)
    m_address = game_values.onlineaddress.substr(0, game_values.onlineaddress.find(':'));
    miAddressField = new MI_TextField(&rm->spr_selectfield, 120, 234, "Join", 300, 90);
    miAddressField->SetData(m_address, 40);
    auto* joinButton = new MI_Button(&rm->spr_selectfield, 420, 234, "Join", 100, TextAlign::CENTER);
    joinButton->SetCode(MENU_CODE_JOIN_EDITOR);

    auto* backButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    backButton->SetCode(MENU_CODE_EDITORS_EXIT);

    AddControl(levelButton, backButton, worldButton, nullptr, backButton);
    AddControl(worldButton, levelButton, hostLevelButton, nullptr, backButton);
    AddControl(hostLevelButton, worldButton, miAddressField, hostWorldButton, hostWorldButton);
    AddControl(hostWorldButton, worldButton, joinButton, hostLevelButton, hostLevelButton);
    AddControl(miAddressField, hostLevelButton, backButton, joinButton, joinButton);
    AddControl(joinButton, hostWorldButton, backButton, miAddressField, miAddressField);
    AddControl(backButton, joinButton, levelButton, miAddressField, nullptr);
    setInitialFocus(levelButton);
    SetCancelCode(MENU_CODE_EDITORS_EXIT);

    AddNonControl(new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0));
    AddNonControl(new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0));
    AddNonControl(new MI_HeaderText("Editors", 320, 5));
    AddNonControl(new MI_Text("Make and change the maps you play on, and worlds", 320, 140, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Edit together: one hosts, the others join its address", 320, 170, 0, false, TextAlign::CENTER));
    miStatusText = new MI_Text("", 320, 274, 0, false, TextAlign::CENTER);
    AddNonControl(miStatusText);
    AddNonControl(new MI_Text("Mouse and keyboard, or a controller: Start shows every action", 320, 330, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Several controllers: everyone gets a cursor", 320, 350, 0, false, TextAlign::CENTER));
    AddNonControl(new MI_Text("Back leaves the editor (it asks first)", 320, 370, 0, false, TextAlign::CENTER));
}

void UI_EditorsMenu::SetStatus(const std::string& text)
{
    miStatusText->SetText(text);
}

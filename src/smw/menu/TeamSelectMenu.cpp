#include "TeamSelectMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Image.h"
#include "ui/MI_TeamSelect.h"
#include "ui/MI_Text.h"

UI_TeamSelectMenu::UI_TeamSelectMenu()
    : UI_Menu()
{
    miTeamSelect = new MI_TeamSelect(&rm->spr_player_select_background, 112, 96);
    miTeamSelect->SetAutoModify(true);

    AddControl(miTeamSelect, nullptr, nullptr, nullptr, nullptr);

    AddTitleBar("Team and Character Selection");

    setInitialFocus(miTeamSelect);
    SetCancelCode(MENU_CODE_BACK_TO_MATCH_SELECTION_MENU);
}

void UI_TeamSelectMenu::ResetTeamSelect()
{
    miTeamSelect->Reset();
}

short UI_TeamSelectMenu::GetTeamCount() const
{
    return miTeamSelect->OrganizeTeams();
}

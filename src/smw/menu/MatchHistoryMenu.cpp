#include "MatchHistoryMenu.h"

#include "ui/MI_MatchHistory.h"

UI_MatchHistoryMenu::UI_MatchHistoryMenu()
    : UI_Menu()
{
    miHistory = new MI_MatchHistory(16, 16, 608, 7);    //(room under the list for the players' portraits)
    miHistory->SetAutoModify(true);

    AddControl(miHistory, nullptr, nullptr, nullptr, nullptr);
    setInitialFocus(miHistory);
    SetCancelCode(MENU_CODE_HISTORY_EXIT);
}

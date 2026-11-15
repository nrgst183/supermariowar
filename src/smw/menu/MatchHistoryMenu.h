#pragma once

#include "uimenu.h"

class MI_MatchHistory;

// Main menu > History: the matches played, with their replays (MatchHistory.h)
class UI_MatchHistoryMenu : public UI_Menu {
public:
    UI_MatchHistoryMenu();

    MI_MatchHistory* miHistory;
};

#pragma once

#include "uimenu.h"

class MI_Button;
class MI_Image;

/*
    This is the main menu you see at the start of the game.
*/
class UI_MainMenu : public UI_Menu {
public:
    UI_MainMenu();

    /// What the focused button does, for the footer ("Go!" says nothing by itself)
    const char* focusHint() const;

private:
    MI_Image* miSMWTitle;
    MI_Image* miSMWVersion;

    MI_Button* miMainStartButton;
    MI_Button* miQuickGameButton;

    MI_Button* miOnlineButton = nullptr;
    MI_Button* miHistoryButton = nullptr;
    MI_Button* miEditorsButton = nullptr;
    MI_Button* miOptionsButton;
    MI_Button* miControlsButton;

    MI_Button* miExitButton;
};

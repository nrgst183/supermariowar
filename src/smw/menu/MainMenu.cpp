#include "MainMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_PlayerSelect.h"

// The web build (and every NO_NETWORK build) has no online mode
#if defined(__EMSCRIPTEN__) || defined(NETWORK_DISABLED)
#define SMW_NO_ONLINE
#endif

UI_MainMenu::UI_MainMenu()
    : UI_Menu()
{
    miSMWTitle = new MI_Image(&rm->menu_smw, 320 - (static_cast<short>(rm->menu_smw.getWidth()) >> 1), 30, 0, 0, 372, 140, 1, 1, 0);
    miSMWVersion = new MI_Image(&rm->menu_version, 628 - rm->menu_version.getWidth(), 10,
        0, 0, rm->menu_version.getWidth(), rm->menu_version.getHeight(), 1, 1, 0);

    miMainStartButton = new MI_Button(&rm->spr_selectfield, 120, 210, "Start", 310);
    miMainStartButton->SetCode(MENU_CODE_TO_MATCH_SELECTION_MENU);

    miQuickGameButton = new MI_Button(&rm->spr_selectfield, 440, 210, "Go!", 80);
    miQuickGameButton->SetCode(MENU_CODE_QUICK_GAME_START);

    miPlayerSelect = new MI_PlayerSelect(&rm->menu_player_select, 120, 250, "Players", 400, 140);

#ifndef SMW_NO_ONLINE
    // Online (rollback netplay); web builds have no network code
#ifdef SMW_XBOX
    miOnlineButton = new MI_Button(&rm->spr_selectfield, 120, 322, "System Link", 200);
#else
    miOnlineButton = new MI_Button(&rm->spr_selectfield, 120, 322, "Online", 200);
#endif
    miOnlineButton->SetCode(MENU_CODE_TO_ONLINE_MENU);
    miHistoryButton = new MI_Button(&rm->spr_selectfield, 320, 322, "History", 200);
#else
    miHistoryButton = new MI_Button(&rm->spr_selectfield, 120, 322, "History", 400);
#endif
    miHistoryButton->SetCode(MENU_CODE_TO_HISTORY_MENU);

#ifdef __EMSCRIPTEN__
    miOptionsButton = new MI_Button(&rm->spr_selectfield, 120, 362, "Options", 400);
    miControlsButton = new MI_Button(&rm->spr_selectfield, 120, 402, "Controls", 400);
#else
    miOptionsButton = new MI_Button(&rm->spr_selectfield, 120, 362, "Options", 200);
    miControlsButton = new MI_Button(&rm->spr_selectfield, 320, 362, "Controls", 200);
#endif
    miOptionsButton->SetCode(MENU_CODE_TO_OPTIONS_MENU);
    miControlsButton->SetCode(MENU_CODE_TO_CONTROLS_MENU);

#ifdef __EMSCRIPTEN__
    miExitButton = nullptr;
    miEditorsButton = nullptr;
#else
    // The map and world editors (EditorLauncher.h)
    miEditorsButton = new MI_Button(&rm->spr_selectfield, 120, 402, "Editors", 200);
    miEditorsButton->SetCode(MENU_CODE_TO_EDITORS_MENU);
    miExitButton = new MI_Button(&rm->spr_selectfield, 320, 402, "Exit", 200);
    miExitButton->SetCode(MENU_CODE_EXIT_APPLICATION);
#endif

#ifndef __EMSCRIPTEN__
    AddControl(miMainStartButton, miEditorsButton, miPlayerSelect, nullptr, miQuickGameButton);
    AddControl(miQuickGameButton, miExitButton, miPlayerSelect, miMainStartButton, nullptr);
#ifndef SMW_NO_ONLINE
    AddControl(miPlayerSelect, miMainStartButton, miOnlineButton, nullptr, nullptr);
    AddControl(miOnlineButton, miPlayerSelect, miOptionsButton, miHistoryButton, miHistoryButton);
    AddControl(miHistoryButton, miPlayerSelect, miControlsButton, miOnlineButton, miOnlineButton);
    AddControl(miOptionsButton, miOnlineButton, miEditorsButton, miControlsButton, miControlsButton);
    AddControl(miControlsButton, miHistoryButton, miExitButton, miOptionsButton, miOptionsButton);
#else
    AddControl(miPlayerSelect, miMainStartButton, miHistoryButton, nullptr, nullptr);
    AddControl(miHistoryButton, miPlayerSelect, miOptionsButton, nullptr, nullptr);
    AddControl(miOptionsButton, miHistoryButton, miEditorsButton, miControlsButton, miControlsButton);
    AddControl(miControlsButton, miHistoryButton, miExitButton, miOptionsButton, miOptionsButton);
#endif
    AddControl(miEditorsButton, miOptionsButton, miMainStartButton, miExitButton, miExitButton);
    AddControl(miExitButton, miControlsButton, miQuickGameButton, miEditorsButton, miEditorsButton);
#else
    AddControl(miMainStartButton, miControlsButton, miPlayerSelect, nullptr, miQuickGameButton);
    AddControl(miQuickGameButton, miControlsButton, miPlayerSelect, miMainStartButton, nullptr);
    AddControl(miPlayerSelect, miMainStartButton, miHistoryButton, nullptr, nullptr);
    AddControl(miHistoryButton, miPlayerSelect, miOptionsButton, nullptr, nullptr);
    AddControl(miOptionsButton, miHistoryButton, miControlsButton, nullptr, nullptr);
    AddControl(miControlsButton, miOptionsButton, miMainStartButton, nullptr, nullptr);
#endif

    setInitialFocus(miMainStartButton);
#ifndef __EMSCRIPTEN__
    SetCancelCode(MENU_CODE_EXIT_APPLICATION);
#endif

    AddNonControl(miSMWTitle);
    AddNonControl(miSMWVersion);
};

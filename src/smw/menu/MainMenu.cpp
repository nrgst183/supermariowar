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
    // miSMWVersionText = new MI_Text("Beta 2", 630, 45, 0, 2, 2);

    miMainStartButton = new MI_Button(&rm->spr_selectfield, 120, 210, "Start", 310);
    miMainStartButton->SetCode(MENU_CODE_TO_MATCH_SELECTION_MENU);

    miQuickGameButton = new MI_Button(&rm->spr_selectfield, 440, 210, "Go!", 80);
    miQuickGameButton->SetCode(MENU_CODE_QUICK_GAME_START);

    miPlayerSelect = new MI_PlayerSelect(&rm->menu_player_select, 120, 250, "Players", 400, 140);

#ifndef SMW_NO_ONLINE
    // Online (rollback netplay); web builds have no network code
    miOnlineButton = new MI_Button(&rm->spr_selectfield, 120, 322, "Online", 400);
    miOnlineButton->SetCode(MENU_CODE_TO_ONLINE_MENU);
#endif

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
#else
    miExitButton = new MI_Button(&rm->spr_selectfield, 120, 402, "Exit", 640 * 0.625f);
    miExitButton->SetCode(MENU_CODE_EXIT_APPLICATION);
#endif

#ifndef __EMSCRIPTEN__
    AddControl(miMainStartButton, miExitButton, miPlayerSelect, nullptr, miQuickGameButton);
    AddControl(miQuickGameButton, miExitButton, miPlayerSelect, miMainStartButton, nullptr);
#ifndef SMW_NO_ONLINE
    AddControl(miPlayerSelect, miMainStartButton, miOnlineButton, nullptr, nullptr);
    AddControl(miOnlineButton, miPlayerSelect, miOptionsButton, nullptr, nullptr);
    AddControl(miOptionsButton, miOnlineButton, miExitButton, miControlsButton, miControlsButton);
    AddControl(miControlsButton, miOnlineButton, miExitButton, miOptionsButton, miOptionsButton);
#else
    AddControl(miPlayerSelect, miMainStartButton, miOptionsButton, nullptr, nullptr);
    AddControl(miOptionsButton, miPlayerSelect, miExitButton, miControlsButton, miControlsButton);
    AddControl(miControlsButton, miPlayerSelect, miExitButton, miOptionsButton, miOptionsButton);
#endif
    AddControl(miExitButton, miOptionsButton, miMainStartButton, nullptr, nullptr);
#else
    AddControl(miMainStartButton, miControlsButton, miPlayerSelect, nullptr, miQuickGameButton);
    AddControl(miQuickGameButton, miControlsButton, miPlayerSelect, miMainStartButton, nullptr);
    AddControl(miPlayerSelect, miMainStartButton, miOptionsButton, nullptr, nullptr);
    AddControl(miOptionsButton, miPlayerSelect, miControlsButton, nullptr, nullptr);
    AddControl(miControlsButton, miOptionsButton, miMainStartButton, nullptr, nullptr);
#endif

    setInitialFocus(miMainStartButton);
#ifndef __EMSCRIPTEN__
    SetCancelCode(MENU_CODE_EXIT_APPLICATION);
#endif

    AddNonControl(miSMWTitle);
    AddNonControl(miSMWVersion);
    // AddNonControl(miSMWVersionText);
};

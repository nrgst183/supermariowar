#include "MainMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

// The web build (and every NO_NETWORK build) has no online mode
#if defined(__EMSCRIPTEN__) || defined(NETWORK_DISABLED)
#define SMW_NO_ONLINE
#endif

UI_MainMenu::UI_MainMenu()
    : UI_Menu()
{
    // The original's layout (Start and Go!, then a row each), with a row more for the fork's History
    // and Editors. Who plays (players and CPUs) is the lobby's, which Start opens; Go! plays with the
    // lobby's last ones
    miSMWTitle = new MI_Image(&rm->menu_smw, 320 - (static_cast<short>(rm->menu_smw.getWidth()) >> 1), 20, 0, 0, 372, 140, 1, 1, 0);
    miSMWVersion = new MI_Image(&rm->menu_version, 628 - rm->menu_version.getWidth(), 10,
        0, 0, rm->menu_version.getWidth(), rm->menu_version.getHeight(), 1, 1, 0);

    // Start: the lobby (a game on this computer, which it can open to others); Go!: a random game
    // right away
    //(the buttons wider and further apart than the original's: the screen under the title has room)
    constexpr short LEFT = 88;
    constexpr short WIDTH = 464;
    constexpr short GO_W = 112;
    miMainStartButton = new MI_Button(&rm->spr_selectfield, LEFT, 184, "Start", WIDTH - GO_W);
    miMainStartButton->SetCode(MENU_CODE_TO_LOCAL_LOBBY);
    miQuickGameButton = new MI_Button(&rm->spr_selectfield, LEFT + WIDTH - GO_W, 184, "Go!", GO_W);
    miQuickGameButton->SetCode(MENU_CODE_QUICK_GAME_START);

    // A row each from here: a button the full width, or two side by side
    constexpr short ROWS_Y = 232;
    constexpr short ROW_STEP = 48;
    std::vector<std::vector<UI_Control*>> rows {{miMainStartButton, miQuickGameButton}};
    auto wide = [&](const char* name, MenuCodeEnum code) {
        MI_Button* button = new MI_Button(&rm->spr_selectfield, LEFT, ROWS_Y + ROW_STEP * static_cast<short>(rows.size() - 1), name, WIDTH);
        button->SetCode(code);
        rows.push_back({button});
        return button;
    };
    auto pair = [&](const char* left, MenuCodeEnum leftCode, const char* right, MenuCodeEnum rightCode) {
        const short y = ROWS_Y + ROW_STEP * static_cast<short>(rows.size() - 1);
        MI_Button* a = new MI_Button(&rm->spr_selectfield, LEFT, y, left, WIDTH / 2);
        MI_Button* b = new MI_Button(&rm->spr_selectfield, LEFT + WIDTH / 2, y, right, WIDTH / 2);
        a->SetCode(leftCode);
        b->SetCode(rightCode);
        rows.push_back({a, b});
        return std::pair {a, b};
    };

#ifndef SMW_NO_ONLINE
    // The games others host (on the network, or at an address); hosting is Start's: its lobby
    // opens to the others
    miOnlineButton = wide("Join a Game", MENU_CODE_TO_SYSTEM_LINK_MENU);
#endif
    std::tie(miOptionsButton, miControlsButton) = pair("Options", MENU_CODE_TO_OPTIONS_MENU, "Controls", MENU_CODE_TO_CONTROLS_MENU);
#ifdef __EMSCRIPTEN__
    // (no editors, and nothing to exit to)
    miHistoryButton = wide("History", MENU_CODE_TO_HISTORY_MENU);
    miEditorsButton = nullptr;
    miExitButton = nullptr;
#else
    // The matches played (and their replays), and the map and world editors (EditorLauncher.h)
    std::tie(miHistoryButton, miEditorsButton) = pair("History", MENU_CODE_TO_HISTORY_MENU, "Editors", MENU_CODE_TO_EDITORS_MENU);
    miExitButton = wide("Exit", MENU_CODE_EXIT_APPLICATION);
#endif

    // Up and down from row to row (round from the bottom to the top), into the same side of a pair;
    // left and right in a row
    const size_t n = rows.size();
    for (size_t r = 0; r < n; r++) {
        const std::vector<UI_Control*>& row = rows[r];
        const std::vector<UI_Control*>& above = rows[(r + n - 1) % n];
        const std::vector<UI_Control*>& below = rows[(r + 1) % n];
        for (size_t c = 0; c < row.size(); c++) {
            AddControl(row[c], above[std::min(c, above.size() - 1)], below[std::min(c, below.size() - 1)],
                c > 0 ? row[c - 1] : nullptr, c + 1 < row.size() ? row[c + 1] : nullptr);
        }
    }

    setInitialFocus(miMainStartButton);
#ifndef __EMSCRIPTEN__
    SetCancelCode(MENU_CODE_EXIT_APPLICATION);
#endif

    AddNonControl(miSMWTitle);
    AddNonControl(miSMWVersion);
};

const char* UI_MainMenu::focusHint() const
{
    const UI_Control* focus = currentFocus();
    if (focus == miMainStartButton)
        return "Pick players and teams, then play";
    if (focus == miQuickGameButton)
        return "A quick game now: random mode and map";
    if (focus != nullptr && focus == miOnlineButton)
        return "Join a game someone else hosts";
    if (focus == miOptionsButton)
        return "Rules, items, sound and picture";
    if (focus == miControlsButton)
        return "Set each player's buttons";
    if (focus != nullptr && focus == miHistoryButton)
        return "Past matches and their replays";
    if (focus != nullptr && focus == miEditorsButton)
        return "Make maps and worlds";
    return "";
}

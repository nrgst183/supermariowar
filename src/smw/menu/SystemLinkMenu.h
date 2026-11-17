#pragma once

#include "uimenu.h"
#include "uicontrol.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

class MI_Button;
class MI_Text;
class MI_WrappedText;

/*
    The games hosted on the local network (system link; LAN games on a PC), found as the menu
    is up (rollback/LanDiscovery.h): one row each, with how many are in. A joins the chosen one,
    B goes back to the Online menu.
*/
class UI_SystemLinkMenu : public UI_Menu {
public:
    UI_SystemLinkMenu();

    /// Starts looking for games (the list starts empty)
    void Open();
    /// Stops looking (when the menu is left for a lobby)
    void Close();

    void ShowMessage(std::string text);
    void ClearMessage();

    /// The address of the game of the chosen row ("192.168.1.20:7300"), or "" and why it can't be joined
    std::string SelectedAddress(std::string& why) const;

    /// Every frame (while the menu is up): reads the answers and updates the rows
    void RefreshGames();

private:
    static constexpr int ROWS = 7;

    struct Row {
        std::string address;
        bool sameBuild = true;
    };

    std::array<MI_Button*, ROWS> m_rows {};
    std::array<MI_Text*, ROWS> m_info {};
    std::array<Row, ROWS> m_shown {};
    int m_shownCount = 0;
    MI_Button* m_back = nullptr;
    MI_WrappedText* m_message = nullptr;
    bool m_showingStatus = true;  // no message: what the search found shows there
    unsigned m_frames = 0;

    void focus(UI_Control* control);
};

/// Not drawn: calls a function every frame (a menu's controls are updated while it is up)
class MI_EveryFrame : public UI_Control {
public:
    explicit MI_EveryFrame(std::function<void()> fn)
        : UI_Control(0, 0)
        , m_fn(std::move(fn))
    {}
    void Update() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

/// The Xbox: the state of its network, for a menu's message line ("" elsewhere)
std::string systemLinkNetworkStatus();

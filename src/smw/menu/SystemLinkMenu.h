#pragma once

#include "uimenu.h"
#include "uicontrol.h"

#include <functional>
#include <string>
#include <vector>

class MI_Button;
class MI_WrappedText;

/// The list of the games found on the network (a row each: the host, the mode and map, how
/// many are in, the connection). Up and down choose (it scrolls past four games; down from the
/// last one leaves it, to the Host button under it), A joins, B goes back.
class MI_LanGameList : public UI_Control {
public:
    struct Game {
        std::string name;       // the host's
        std::string address;    // "192.168.1.20:7300"
        std::string mode;
        std::string map;
        std::string build;
        int members = 0;        // machines in the lobby
        int maxMembers = 0;
        int players = 0;        // the next match's, CPUs included (0: not told, an older build)
        int pingMs = -1;
        bool inMatch = false;
        bool sameBuild = true;
        int activity = 0;       // lan::ACTIVITY_*: matches, or a map or world editor session
        bool editing = false;   // an editor session the host is in now
        bool editor() const { return activity != 0; }
        // "Map editor", "World editor"
        const char* editorName() const { return activity == 2 ? "World editor" : "Map editor"; }
    };

    MI_LanGameList();

    void Update() override;
    void Draw() override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;

    /// The games found now (the chosen one stays chosen while it's there)
    void SetGames(std::vector<Game> games);
    /// The chosen game (null: none found)
    const Game* Selected() const;
    /// Whether the search is running (else the list says it can't search)
    void SetSearching(bool searching) { m_searching = searching; }
    /// Down from the last game (or with none): the focus goes to what's under the list
    void SetLeaveDown(std::function<void()> leave) { m_leaveDown = std::move(leave); }
    /// What B does (back to the main menu)
    void SetBackCode(MenuCodeEnum code) { m_backCode = code; }

    static constexpr int ROWS = 4;

private:
    std::vector<Game> m_games;
    std::string m_selectedAddress;
    int m_selected = 0;
    int m_scroll = 0;
    int m_tick = 0;
    bool m_searching = true;
    std::function<void()> m_leaveDown;
    MenuCodeEnum m_backCode = MENU_CODE_TO_MAIN_MENU;

    void move(int direction);
    void drawRow(int x, int y, const Game& game, bool selected) const;
    void drawEmpty() const;
};

/*
    System link (LAN games on a PC): the games hosted on the local network, found as the menu is
    up (rollback/LanDiscovery.h), in a list (MI_LanGameList) to join, and under it the button that
    hosts a game here (the others' lists then show it, under this computer's name: on the Xbox
    the console's nickname). Under the list, the chosen game's details or what hosting does.
    The main menu's Join a game opens it, and B goes back there. On the Xbox it has the run-ahead
    setting beside Host; on a PC, Join by address (a game that isn't on this network).
    Errors (and on the Xbox, the state of its network) show at the bottom.
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

    /// The address of the chosen game ("192.168.1.20:7300"), or "" and why it can't be joined
    std::string SelectedAddress(std::string& why) const;

    /// Every frame (while the menu is up): reads the answers and updates the list
    void RefreshGames();

private:
    MI_LanGameList* m_list = nullptr;
    MI_Button* m_host = nullptr;
    UI_Control* m_runAhead = nullptr;   // (the Xbox's; on a PC it's in Options > Online)
    MI_Button* m_joinAddress = nullptr; // (a PC's: a game that isn't on this network)
    MI_WrappedText* m_message = nullptr;
    bool m_moved = false;               // the player moved: a game showing up doesn't take the focus
    bool m_showingStatus = true;  // no message: on the Xbox, the state of its network shows there
    unsigned m_frames = 0;

    void focus(UI_Control* control);
    void drawDetails() const;
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
/// The name this computer goes by in a lobby (and the games it hosts, in the others' lists): on the
/// Xbox the console's nickname (else "Xbox" and its address's last number), elsewhere the Online
/// menu's nickname (empty: the lobby says "Player N")
std::string systemLinkPlayerName();

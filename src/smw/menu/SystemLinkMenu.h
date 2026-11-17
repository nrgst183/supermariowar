#pragma once

#include "uimenu.h"
#include "uicontrol.h"

#include <functional>
#include <string>
#include <vector>

class MI_Button;
class MI_WrappedText;

/// The list of the games found on the network (a row each: the host, the mode and map, how
/// many are in, the connection), and under it the chosen game's details: whether it can be
/// joined, and if not why. Up and down choose (it scrolls past five games), A joins, B goes back.
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

    static constexpr int ROWS = 5;

private:
    std::vector<Game> m_games;
    std::string m_selectedAddress;
    int m_selected = 0;
    int m_scroll = 0;
    int m_tick = 0;
    bool m_searching = true;

    void move(int direction);
    void drawRow(int x, int y, const Game& game, bool selected) const;
    void drawEmpty() const;
    void drawDetails(const Game& game) const;
};

/*
    The games hosted on the local network (system link; LAN games on a PC), found as the menu
    is up (rollback/LanDiscovery.h), in a list (MI_LanGameList). A joins the chosen one, B goes
    back to the Online menu. Errors (and on the Xbox, the state of its network) show at the bottom.
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
    MI_WrappedText* m_message = nullptr;
    bool m_showingStatus = true;  // no message: on the Xbox, the state of its network shows there
    unsigned m_frames = 0;
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

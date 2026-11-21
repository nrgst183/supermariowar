#include "SystemLinkMenu.h"

#include "GameValues.h"
#include "input.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_SelectField.h"
#include "ui/SmwTheme.h"

#include <algorithm>
#include <cstring>

#ifndef NETWORK_DISABLED
#include "rollback/LanDiscovery.h"
#include "rollback/Transport.h"
#endif

#ifdef SMW_XBOX
#include <SDL3/SDL.h>
#endif

namespace {
// The list: the panel, its rows; under it the details panel, and the Host button
constexpr int PANEL_X = 36;
constexpr int PANEL_Y = 42;
constexpr int PANEL_W = 568;
constexpr int PANEL_H = 226;
constexpr int ROW_X = 52;
constexpr int ROW_Y = 76;
constexpr int ROW_W = 536;
constexpr int ROW_H = 42;
constexpr int ROW_STEP = 46;
constexpr int DETAILS_Y = 274;
constexpr int DETAILS_H = 62;
constexpr short HOST_Y = 344;

// Draws with a function (a menu's part that depends on more than one control)
class MI_DrawFn : public UI_Control {
public:
    explicit MI_DrawFn(std::function<void()> fn)
        : UI_Control(0, 0)
        , m_fn(std::move(fn))
    {}
    void Draw() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

// (the main menu's button)
constexpr const char* TITLE = "Join a Game";
#ifdef SMW_XBOX
constexpr const char* WHERE = "consoles and PCs";
#else
constexpr const char* WHERE = "PCs and consoles";
#endif

#ifndef NETWORK_DISABLED
// The search (only while the menu is up: its controls are updated only then)
rollback::lan::Browser s_browser;
#endif

// A text cut to a width, "..." at its end
std::string fit(const gfxFont& font, std::string text, int width)
{
    if (font.getWidth(text) <= width)
        return text;
    while (!text.empty() && font.getWidth(text + "...") > width)
        text.pop_back();
    return text + "...";
}

std::string shortBuild(const std::string& build)
{
    return build.empty() ? "unknown" : build.substr(0, 8);
}

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
} // namespace

std::string systemLinkPlayerName()
{
#ifdef SMW_XBOX
    //The nickname of Options > Online, else the console's (the games share them), else "Xbox" and
    //its address's last number
    if (!trimmed(game_values.onlinename).empty())
        return trimmed(game_values.onlinename);
    char name[17];
    if (SDL_XboxGetNickname(name, sizeof(name)))
        return trimmed(name).empty() ? "Xbox" : trimmed(name);
    char address[32];
    if (SDL_XboxGetNetworkStatus(address, sizeof(address)) == SDL_XBOX_NETWORK_READY) {
        const char* last = std::strrchr(address, '.');
        if (last && last[1])
            return std::string("Xbox ") + (last + 1);
    }
    return "Xbox";
#else
    return trimmed(game_values.onlinename);
#endif
}

std::string systemLinkNetworkStatus()
{
#ifdef SMW_XBOX
    char address[32];
    switch (SDL_XboxGetNetworkStatus(address, sizeof(address))) {
        case SDL_XBOX_NETWORK_READY:
            return std::string("System link: this console is ") + address;
        case SDL_XBOX_NETWORK_NO_CABLE:
            return "No network cable. Connect the consoles, a hub or a router";
        case SDL_XBOX_NETWORK_PENDING:
            return "Getting a network address...";
        case SDL_XBOX_NETWORK_NO_ADDRESS:
            return "No network address. Check the dashboard's network settings";
        default:
            return "The network didn't start";
    }
#else
    return {};
#endif
}


// ---- MI_LanGameList

MI_LanGameList::MI_LanGameList()
    : UI_Control(PANEL_X, PANEL_Y)
{
    SetAutoModify(true);    //(it takes up and down itself: the list scrolls)
}

void MI_LanGameList::Update()
{
    ++m_tick;
}

void MI_LanGameList::SetGames(std::vector<Game> games)
{
    m_games = std::move(games);

    //The chosen game stays chosen (the list's order may change); if it went, the one at its place
    const auto it = std::find_if(m_games.begin(), m_games.end(), [&](const Game& g) { return g.address == m_selectedAddress; });
    if (it != m_games.end())
        m_selected = static_cast<int>(it - m_games.begin());
    m_selected = std::clamp(m_selected, 0, std::max(0, static_cast<int>(m_games.size()) - 1));
    m_selectedAddress = m_games.empty() ? std::string() : m_games[static_cast<size_t>(m_selected)].address;
    m_scroll = std::clamp(m_scroll, std::max(0, m_selected - ROWS + 1), m_selected);
    m_scroll = std::clamp(m_scroll, 0, std::max(0, static_cast<int>(m_games.size()) - ROWS));
}

const MI_LanGameList::Game* MI_LanGameList::Selected() const
{
    return m_games.empty() ? nullptr : &m_games[static_cast<size_t>(m_selected)];
}

void MI_LanGameList::move(int direction)
{
    if (m_games.empty())
        return;
    const int count = static_cast<int>(m_games.size());
    m_selected = std::clamp(m_selected + direction, 0, count - 1);
    m_selectedAddress = m_games[static_cast<size_t>(m_selected)].address;
    if (m_selected < m_scroll)
        m_scroll = m_selected;
    if (m_selected >= m_scroll + ROWS)
        m_scroll = m_selected - ROWS + 1;
}

MenuCodeEnum MI_LanGameList::SendInput(CPlayerInput* playerInput)
{
    for (int p = 0; p < 4; p++) {
        const COutputControl& keys = playerInput->outputControls[p];
        if (keys.menu_cancel.fPressed)
            return m_backCode;
        if (keys.menu_select.fPressed)
            return m_games.empty() ? MENU_CODE_NONE : MENU_CODE_SYSTEM_LINK_JOIN;
        if (keys.menu_up.fPressed) {
            move(-1);
        } else if (keys.menu_down.fPressed) {
            //(down from the last game: the Host button)
            if (m_selected + 1 >= static_cast<int>(m_games.size())) {
                if (m_leaveDown)
                    m_leaveDown();
                return MENU_CODE_NONE;
            }
            move(1);
        }
    }
    return MENU_CODE_NONE;
}

void MI_LanGameList::Draw()
{
    if (!m_visible)
        return;

    //The panel; its note: how many were found (and that there are more than shown)
    std::string note;
    const int count = static_cast<int>(m_games.size());
    if (!m_searching)
        note = "Not searching";
    else if (count == 0)
        note = "Searching...";
    else if (count > ROWS)
        note = std::to_string(count) + " found (" + std::to_string(m_scroll + 1) + "-" + std::to_string(std::min(count, m_scroll + ROWS)) + " shown)";
    else
        note = count == 1 ? "1 found" : std::to_string(count) + " found";
    menupanel::box(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    rm->menu_font_large.draw(PANEL_X + 20, PANEL_Y + 10, "Games on your network");
    rm->menu_font_small.drawRightJustified(PANEL_X + PANEL_W - 20, PANEL_Y + 14, note);

    if (m_games.empty()) {
        drawEmpty();
        return;
    }

    for (int i = 0; i < ROWS && m_scroll + i < count; i++)
        drawRow(ROW_X, ROW_Y + i * ROW_STEP, m_games[static_cast<size_t>(m_scroll + i)], fSelected && m_scroll + i == m_selected);

    //More above or below: a marker at the panel's edge
    if (m_scroll > 0)
        rm->menu_font_small.drawCentered(320, ROW_Y - 9, "- more above -");
    if (m_scroll + ROWS < count)
        rm->menu_font_small.drawCentered(320, ROW_Y + ROWS * ROW_STEP - 4, "- more below -");
}

void MI_LanGameList::drawRow(int x, int y, const Game& game, bool selected) const
{
    const bool full = game.maxMembers > 0 && game.members >= game.maxMembers;
    const bool joinable = game.sameBuild && !full;
    menupanel::row(x, y, ROW_W, ROW_H, selected, !joinable);

    //Left: the host, and under it what they play
    rm->menu_font_large.draw(x + 14, y + 4, fit(rm->menu_font_large, game.name.empty() ? game.address : game.name, 300));
    std::string what;
    if (!game.sameBuild)
        what = "Another version of the game";
    else if (game.editor())
        //(an editor session: what is being made, or that it hasn't started)
        what = std::string(game.editorName()) + (game.editing ? " - editing " + (game.map.empty() ? std::string("now") : game.map) : " - in the lobby");
    else if (!game.mode.empty())
        what = game.mode + (game.map.empty() ? "" : " - " + game.map);
    else
        what = game.address;
    if (game.sameBuild && game.inMatch)
        what = "Match on - " + what;
    //(an editor session: its tag before the host's name, in the menus' highlight color)
    if (game.sameBuild && game.editor()) {
        const std::string tag = game.activity == 2 ? "WORLD EDIT" : "MAP EDIT";
        const int tagW = rm->menu_font_small.getWidth(tag) + 30;
        const int nameW = rm->menu_font_large.getWidth(fit(rm->menu_font_large, game.name.empty() ? game.address : game.name, 300));
        const int tagX = x + 14 + nameW + 10;
        //(green: being edited now; gold: its lobby waits for the host's start)
        theme::block(tagX, y + 4, tagW, 20, game.editing ? theme::Tone::Green : theme::Tone::Gold);
        theme::icon(tagX + 3, y + 6, game.activity == 2 ? theme::Icon::Star : theme::Icon::QuestionBlock);
        rm->menu_font_small.draw(tagX + 22, y + 8, tag.c_str());
    }
    rm->menu_font_small.draw(x + 14, y + 26, fit(rm->menu_font_small, what, 360));

    //Right: the connection, and how many are in
    theme::signal(x + ROW_W - 34, y + 6, game.pingMs);
    const std::string in = std::to_string(game.members) + "/" + std::to_string(game.maxMembers);
    rm->menu_font_large.drawRightJustified(x + ROW_W - 44, y + 4, in);
    std::string under = full ? "full" : game.players > 0 ? std::to_string(game.players) + (game.players == 1 ? " player" : " players") : "in lobby";
    if (!full && game.editor())
        under = std::to_string(game.members) + (game.members == 1 ? " person" : " people");
    rm->menu_font_small.drawRightJustified(x + ROW_W - 44, y + 26, under);
}

void MI_LanGameList::drawEmpty() const
{
    if (!m_searching) {
        rm->menu_font_large.drawCentered(320, 130, "Can't search the network");
        return;
    }
    theme::spinner(320, 116, m_tick);
    rm->menu_font_large.drawCentered(320, 150, "Looking for games...");
    rm->menu_font_small.drawCentered(320, 184, std::string("Games hosted on ") + WHERE + " on this network");
    rm->menu_font_small.drawCentered(320, 202, "appear here on their own within a second or two.");
    rm->menu_font_small.drawCentered(320, 228, "None? Host one with the button below.");
}

// ---- UI_SystemLinkMenu

UI_SystemLinkMenu::UI_SystemLinkMenu()
    : UI_Menu()
{
    AddTitleBar(TITLE);
    SetHints(false);    //(its own prompts, at the bottom)
    const MenuCodeEnum back = MENU_CODE_TO_MAIN_MENU;    //(the main menu's Join a game opens it)

    m_list = new MI_LanGameList();
    m_list->SetBackCode(back);
    m_list->SetLeaveDown([this] {
        m_moved = true;
        focus(m_host);
    });

    //Under the list: hosting, and beside it joining by address (a game that isn't on this network)
    m_host = new MI_Button(&rm->spr_selectfield, 64, HOST_Y, "Host a game", 250, TextAlign::CENTER);
    m_joinAddress = new MI_Button(&rm->spr_selectfield, 326, HOST_Y, "Join by address", 250, TextAlign::CENTER);
    m_joinAddress->SetCode(MENU_CODE_TO_ONLINE_MENU);
    m_host->SetCode(MENU_CODE_SYSTEM_LINK_HOST);

    //What went wrong (or on the Xbox, the state of its network), at the bottom
    m_message = new MI_WrappedText(320, 392, 512, 2);
    m_message->SetBoxed(true);

    MI_PromptStrip* prompts = new MI_PromptStrip(16, 434, 512, "", [this]() {
        using prompts::Pad;
        using prompts::menuKey;
        std::vector<prompts::Prompt> row;
        const prompts::Prompt choose {{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"};
        if (m_currentFocus == m_list && m_list->Selected()) {
            row.push_back(choose);
            row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "join"});
        } else if (m_currentFocus == m_host) {
            if (m_list->Selected())
                row.push_back(choose);
            row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "host"});
        } else if (m_joinAddress && m_currentFocus == m_joinAddress) {
            row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "type an address"});
        } else if (m_runAhead && m_currentFocus == m_runAhead) {
            row.push_back({{Pad::Left, Pad::Right}, {menuKey(prompts::MENU_LEFT), menuKey(prompts::MENU_RIGHT)}, "change"});
        } else {
            row.push_back(choose);
        }
        row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"});
        return row;
    });
    MI_Button* backButton = new MI_Button(&rm->spr_selectfield, 544, 410, "Back", 80, TextAlign::CENTER);
    backButton->SetCode(back);

    //(the list takes up and down itself: down from its last game comes to the Host button)
    AddControl(m_list, nullptr, nullptr, nullptr, nullptr);
    if (m_runAhead) {
        AddControl(m_host, m_list, nullptr, nullptr, m_runAhead);
        AddControl(m_runAhead, m_list, nullptr, m_host, nullptr);
    } else if (m_joinAddress) {
        AddControl(m_host, m_list, nullptr, nullptr, m_joinAddress);
        AddControl(m_joinAddress, m_list, nullptr, m_host, nullptr);
    } else {
        AddControl(m_host, m_list, nullptr, nullptr, nullptr);
    }
    AddNonControl(new MI_DrawFn([this] { drawDetails(); }));
    AddNonControl(backButton);
    AddNonControl(m_message);
    AddNonControl(prompts);
    AddNonControl(new MI_EveryFrame([this] { RefreshGames(); }));

    setInitialFocus(m_host);
    SetCancelCode(back);
}

void UI_SystemLinkMenu::focus(UI_Control* control)
{
    if (m_currentFocus == control)
        return;
    if (m_currentFocus) {
        m_currentFocus->Modify(false);
        m_currentFocus->Select(false);
    }
    m_currentFocus = control;
    fModifyingItem = control ? control->Select(true) : false;
}

void UI_SystemLinkMenu::Open()
{
#ifndef NETWORK_DISABLED
    s_browser.close();
    const bool searching = s_browser.open();
    m_list->SetSearching(searching);
    if (!searching)
        ShowMessage("Can't search the network (no UDP socket)");
    else
        ClearMessage();
#else
    m_list->SetSearching(false);
    ShowMessage("This build has no network support");
#endif
    m_list->SetGames({});
    m_frames = 0;
    m_moved = false;
    ResetMenu();
}

void UI_SystemLinkMenu::Close()
{
#ifndef NETWORK_DISABLED
    s_browser.close();
#endif
}

void UI_SystemLinkMenu::ShowMessage(std::string text)
{
    m_showingStatus = text.empty();
    const bool problem = text.rfind("Can't", 0) == 0 || text.rfind("That game", 0) == 0 || text.find("no network") != std::string::npos;
    m_message->SetText(std::move(text), problem ? MI_WrappedText::Tone::Error : MI_WrappedText::Tone::Warn);
}

void UI_SystemLinkMenu::ClearMessage()
{
    m_showingStatus = true;
    m_message->SetText({});
}

// Under the list: the chosen game (who, where, whether it can be joined), or what hosting does
void UI_SystemLinkMenu::drawDetails() const
{
    const MI_LanGameList::Game* chosen = m_list->Selected();
    if (m_currentFocus != m_list || !chosen) {
        menupanel::box(PANEL_X, DETAILS_Y, PANEL_W, DETAILS_H);
        const std::string name = systemLinkPlayerName();
        rm->menu_font_large.draw(PANEL_X + 20, DETAILS_Y + 9, "Your game");
#ifdef SMW_XBOX
        const std::string where = systemLinkNetworkStatus();
        rm->menu_font_small.drawRightJustified(PANEL_X + PANEL_W - 20, DETAILS_Y + 13, fit(rm->menu_font_small, where.rfind("System link: this console is ", 0) == 0 ? where.substr(29) : std::string(), 250));
#else
        const int port = atoi(game_values.onlineport.c_str());
        rm->menu_font_small.drawRightJustified(PANEL_X + PANEL_W - 20, DETAILS_Y + 13, "Port " + std::to_string(port > 0 ? port : 7300));
#endif
        //(no nickname: the lobby's "Player 1", as the others' lists show it)
        theme::statusIcon(PANEL_X + 20, DETAILS_Y + 36, theme::Status::Info);
        const std::string text = "Others on this network see it as \"" + (name.empty() ? std::string("Player 1") : name) + "\"";
        rm->menu_font_small.draw(PANEL_X + 44, DETAILS_Y + 40, fit(rm->menu_font_small, text, PANEL_W - 64));
        return;
    }
    const MI_LanGameList::Game& game = *chosen;

    //The chosen game: who, where, and whether (and how) it can be joined
    menupanel::box(PANEL_X, DETAILS_Y, PANEL_W, DETAILS_H);
    const std::string name = game.name.empty() ? "A game" : game.name + "'s game";
    rm->menu_font_large.draw(PANEL_X + 20, DETAILS_Y + 9, fit(rm->menu_font_large, name, 300));
    rm->menu_font_small.drawRightJustified(PANEL_X + PANEL_W - 20, DETAILS_Y + 13, game.address);

    const bool full = game.maxMembers > 0 && game.members >= game.maxMembers;
    theme::Status status = theme::Status::Good;
    std::string text;
    if (!game.sameBuild) {
        status = theme::Status::Error;
        text = "Another version (" + shortBuild(game.build) + ", yours is " + shortBuild(GIT_REVISION) + "). Builds must match";
    } else if (full) {
        status = theme::Status::Warn;
        text = "Full. Every spot in the lobby is taken";
    } else if (game.inMatch) {
        status = theme::Status::Info;
        text = "Match in progress. You'll play from the next match";
    } else if (game.editor() && game.editing) {
        text = std::string(game.activity == 2 ? "Editing a world" : "Editing a map") + " together (" + std::to_string(game.members) + " of "
            + std::to_string(game.maxMembers) + "). Press A to join in";
    } else if (game.editor()) {
        status = theme::Status::Info;
        text = std::string(game.editorName()) + " lobby (" + std::to_string(game.members) + " of " + std::to_string(game.maxMembers)
            + "). Press A to join";
    } else {
        text = "In the lobby (" + std::to_string(game.members) + " of " + std::to_string(game.maxMembers) + "). Press A to join";
    }
    theme::statusIcon(PANEL_X + 20, DETAILS_Y + 36, status);
    rm->menu_font_small.draw(PANEL_X + 44, DETAILS_Y + 40, fit(rm->menu_font_small, text, PANEL_W - 64));
}

std::string UI_SystemLinkMenu::SelectedAddress(std::string& why) const
{
    const MI_LanGameList::Game* game = m_list->Selected();
    if (!game) {
        why = "No games yet. Wait for one to show up";
        return {};
    }
    if (!game->sameBuild) {
        why = "That game is a different version. Builds must match";
        return {};
    }
    if (game->maxMembers > 0 && game->members >= game->maxMembers) {
        why = "That game is full";
        return {};
    }
    return game->address;
}

void UI_SystemLinkMenu::RefreshGames()
{
#ifndef NETWORK_DISABLED
    s_browser.update(rollback::monotonicMs());

    if (m_frames++ % 10 == 0) {
        std::vector<MI_LanGameList::Game> games;
        for (const rollback::lan::FoundGame& found : s_browser.games()) {
            MI_LanGameList::Game game;
            game.name = found.info.name;
            game.address = found.address();
            game.mode = found.info.mode;
            game.map = found.info.map;
            game.build = found.info.build;
            game.members = found.info.members;
            game.maxMembers = found.info.maxMembers;
            game.players = found.info.players;
            game.pingMs = found.pingMs;
            game.inMatch = found.info.inMatch;
            game.sameBuild = found.info.build == GIT_REVISION;
            game.activity = found.info.activity;
            game.editing = found.info.editing;
            games.push_back(std::move(game));
        }
        //Sorted (the answers come in any order): the games that can be joined first, by name
        std::stable_sort(games.begin(), games.end(), [](const MI_LanGameList::Game& a, const MI_LanGameList::Game& b) {
            if (a.sameBuild != b.sameBuild)
                return a.sameBuild;
            return a.name < b.name;
        });
        const bool first = !m_list->Selected() && !games.empty();
        m_list->SetGames(std::move(games));
        //The first game found takes the focus, only just after the menu opened (later, a game showing
        //up as A is pressed on Host would join it instead); none left: the Host button
        if (first && !m_moved && m_frames < 120 && m_currentFocus == m_host)
            focus(m_list);
        if (!m_list->Selected() && m_currentFocus == m_list)
            focus(m_host);

        //No message: the Xbox's network (if it isn't ready, a warning)
        if (m_showingStatus) {
            const std::string network = systemLinkNetworkStatus();
            const bool ready = network.rfind("System link:", 0) == 0;
            m_message->SetText(network, ready ? MI_WrappedText::Tone::Info : MI_WrappedText::Tone::Warn);
        }
    }
#endif
}

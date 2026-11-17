#include "SystemLinkMenu.h"

#include "input.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include <algorithm>

#ifndef NETWORK_DISABLED
#include "rollback/LanDiscovery.h"
#include "rollback/Transport.h"
#endif

#ifdef SMW_XBOX
#include <SDL3/SDL.h>
#endif

namespace {
// The list: the panel, its rows, and the details panel under it
constexpr int PANEL_X = 36;
constexpr int PANEL_Y = 42;
constexpr int PANEL_W = 568;
constexpr int PANEL_H = 272;
constexpr int ROW_X = 52;
constexpr int ROW_Y = 76;
constexpr int ROW_W = 536;
constexpr int ROW_H = 42;
constexpr int ROW_STEP = 46;
constexpr int DETAILS_Y = 320;
constexpr int DETAILS_H = 62;

#ifdef SMW_XBOX
constexpr const char* TITLE = "System Link Games";
constexpr const char* WHERE = "consoles and PCs";
#else
constexpr const char* TITLE = "LAN Games";
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
} // namespace

std::string systemLinkNetworkStatus()
{
#ifdef SMW_XBOX
    char address[32];
    switch (SDL_XboxGetNetworkStatus(address, sizeof(address))) {
        case SDL_XBOX_NETWORK_READY:
            return std::string("System link: this console is ") + address;
        case SDL_XBOX_NETWORK_NO_CABLE:
            return "No network cable: connect the consoles (or a hub, or a router) for system link";
        case SDL_XBOX_NETWORK_PENDING:
            return "Getting a network address...";
        case SDL_XBOX_NETWORK_NO_ADDRESS:
            return "The network gave no address: check the dashboard's network settings";
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
    m_selected = (m_selected + direction + count) % count;    //(round the ends)
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
            return MENU_CODE_TO_ONLINE_MENU;
        if (keys.menu_select.fPressed)
            return MENU_CODE_SYSTEM_LINK_JOIN;
        if (keys.menu_up.fPressed)
            move(-1);
        else if (keys.menu_down.fPressed)
            move(1);
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
        drawRow(ROW_X, ROW_Y + i * ROW_STEP, m_games[static_cast<size_t>(m_scroll + i)], m_scroll + i == m_selected);

    //More above or below: a marker at the panel's edge
    if (m_scroll > 0)
        rm->menu_font_small.drawCentered(320, ROW_Y - 9, "- more above -");
    if (m_scroll + ROWS < count)
        rm->menu_font_small.drawCentered(320, ROW_Y + ROWS * ROW_STEP - 4, "- more below -");

    drawDetails(m_games[static_cast<size_t>(m_selected)]);
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
    else if (!game.mode.empty())
        what = game.mode + (game.map.empty() ? "" : " - " + game.map);
    else
        what = game.address;
    if (game.sameBuild && game.inMatch)
        what = "Match on - " + what;
    rm->menu_font_small.draw(x + 14, y + 26, fit(rm->menu_font_small, what, 360));

    //Right: the connection, and how many are in
    theme::signal(x + ROW_W - 34, y + 6, game.pingMs);
    const std::string in = std::to_string(game.members) + "/" + std::to_string(game.maxMembers);
    rm->menu_font_large.drawRightJustified(x + ROW_W - 44, y + 4, in);
    const std::string under = full ? "full" : game.players > 0 ? std::to_string(game.players) + (game.players == 1 ? " player" : " players") : "in lobby";
    rm->menu_font_small.drawRightJustified(x + ROW_W - 44, y + 26, under);
}

void MI_LanGameList::drawEmpty() const
{
    if (!m_searching) {
        rm->menu_font_large.drawCentered(320, 150, "Can't search the network");
        rm->menu_font_small.drawCentered(320, 180, "Join by address from the Online menu instead");
        return;
    }
    theme::spinner(320, 130, m_tick);
    rm->menu_font_large.drawCentered(320, 166, "Looking for games...");
    rm->menu_font_small.drawCentered(320, 200, std::string("Games hosted on the ") + WHERE + " of this network");
    rm->menu_font_small.drawCentered(320, 218, "show up here by themselves, within a second or two.");
#ifdef SMW_XBOX
    rm->menu_font_small.drawCentered(320, 248, "Connect the consoles with a cable, a hub or a router,");
    rm->menu_font_small.drawCentered(320, 266, "or host a game yourself from the System Link menu.");
#else
    rm->menu_font_small.drawCentered(320, 248, "Nothing? Host one from the Online menu, or join");
    rm->menu_font_small.drawCentered(320, 266, "a game over the internet by its address there.");
#endif
}

void MI_LanGameList::drawDetails(const Game& game) const
{
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
        text = "Another version (" + shortBuild(game.build) + ", this is " + shortBuild(GIT_REVISION) + "): both need the same build";
    } else if (full) {
        status = theme::Status::Warn;
        text = "Full: every place in the lobby is taken";
    } else if (game.inMatch) {
        status = theme::Status::Info;
        text = "A match is on: you join the lobby and play from the next match";
    } else {
        text = "In the lobby, " + std::to_string(game.members) + " of " + std::to_string(game.maxMembers) + " in: press A to join";
    }
    theme::statusIcon(PANEL_X + 20, DETAILS_Y + 36, status);
    rm->menu_font_small.draw(PANEL_X + 44, DETAILS_Y + 40, fit(rm->menu_font_small, text, PANEL_W - 64));
}


// ---- UI_SystemLinkMenu

UI_SystemLinkMenu::UI_SystemLinkMenu()
    : UI_Menu()
{
    AddTitleBar(TITLE);

    m_list = new MI_LanGameList();

    //What went wrong (or on the Xbox, the state of its network), at the bottom
    m_message = new MI_WrappedText(320, 396, 512, 2);
    m_message->SetBoxed(true);

    MI_PromptStrip* prompts = new MI_PromptStrip(16, 434, 512, "", [this]() {
        using prompts::Pad;
        using prompts::menuKey;
        std::vector<prompts::Prompt> row;
        if (m_list->Selected()) {
            row.push_back({{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"});
            row.push_back({{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "join"});
        }
        row.push_back({{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"});
        return row;
    });
    //(B goes back: the list has the focus, always)
    MI_Button* back = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    back->SetCode(MENU_CODE_TO_ONLINE_MENU);

    AddControl(m_list, nullptr, nullptr, nullptr, nullptr);
    AddNonControl(back);
    AddNonControl(m_message);
    AddNonControl(prompts);
    AddNonControl(new MI_EveryFrame([this] { RefreshGames(); }));

    setInitialFocus(m_list);
    SetCancelCode(MENU_CODE_TO_ONLINE_MENU);
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

std::string UI_SystemLinkMenu::SelectedAddress(std::string& why) const
{
    const MI_LanGameList::Game* game = m_list->Selected();
    if (!game) {
        why = "No game to join yet: wait for one to show up";
        return {};
    }
    if (!game->sameBuild) {
        why = "That game is another version: both need the same build of the game";
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
            games.push_back(std::move(game));
        }
        //Sorted (the answers come in any order): the games that can be joined first, by name
        std::stable_sort(games.begin(), games.end(), [](const MI_LanGameList::Game& a, const MI_LanGameList::Game& b) {
            if (a.sameBuild != b.sameBuild)
                return a.sameBuild;
            return a.name < b.name;
        });
        m_list->SetGames(std::move(games));

        //No message: the Xbox's network (if it isn't ready, a warning)
        if (m_showingStatus) {
            const std::string network = systemLinkNetworkStatus();
            const bool ready = network.rfind("System link:", 0) == 0;
            m_message->SetText(network, ready ? MI_WrappedText::Tone::Info : MI_WrappedText::Tone::Warn);
        }
    }
#endif
}

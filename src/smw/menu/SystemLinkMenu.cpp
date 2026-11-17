#include "SystemLinkMenu.h"

#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_Text.h"

#include <algorithm>

#ifndef NETWORK_DISABLED
#include "rollback/LanDiscovery.h"
#include "rollback/Transport.h"
#endif

#ifdef SMW_XBOX
#include <SDL3/SDL.h>
#endif

namespace {
constexpr short ROW_X = 64;
constexpr short ROW_Y = 78;
constexpr short ROW_STEP = 36;
constexpr short ROW_W = 512;

#ifndef NETWORK_DISABLED
// The search (only while the menu is up: its controls are updated only then)
rollback::lan::Browser s_browser;

std::string gameInfo(const rollback::lan::FoundGame& game)
{
    if (game.info.build != GIT_REVISION)
        return "other version";
    if (game.info.inMatch)
        return "in a match";
    return std::to_string(game.info.members) + "/" + std::to_string(game.info.maxMembers) + " in";
}
#endif

#ifdef SMW_XBOX
constexpr const char* TITLE = "System Link Games";
#else
constexpr const char* TITLE = "LAN Games";
#endif
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

UI_SystemLinkMenu::UI_SystemLinkMenu()
    : UI_Menu()
{
    AddTitleBar(TITLE);

    AddNonControl(new MI_MenuPanel(36, 42, 568, 300, "Open games", "Hosted on consoles and PCs of your network"));

    for (int i = 0; i < ROWS; i++) {
        const short y = static_cast<short>(ROW_Y + i * ROW_STEP);
        m_rows[i] = new MI_Button(&rm->spr_selectfield, ROW_X, y, "", ROW_W);
        m_rows[i]->SetCode(MENU_CODE_SYSTEM_LINK_JOIN);
        m_rows[i]->setVisible(false);
        m_info[i] = new MI_Text("", ROW_X + ROW_W - 16, static_cast<short>(y + 5), 0, true, TextAlign::RIGHT);
    }

    //What's happening (or what went wrong), under the panel
    m_message = new MI_WrappedText(320, 352, 580, 2);

    MI_PromptStrip* prompts = new MI_PromptStrip(16, 434, 512, "", []() {
        using prompts::Pad;
        using prompts::menuKey;
        return std::vector<prompts::Prompt> {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"},
            {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "join"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
    });
    m_back = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    m_back->SetCode(MENU_CODE_TO_ONLINE_MENU);

    //Up and down go through the rows (the hidden ones are skipped) and the Back button
    for (int i = 0; i < ROWS; i++)
        AddControl(m_rows[i], i > 0 ? m_rows[i - 1] : m_back, i + 1 < ROWS ? m_rows[i + 1] : m_back, nullptr, nullptr);
    AddControl(m_back, m_rows[ROWS - 1], m_rows[0], nullptr, nullptr);
    for (MI_Text* info : m_info)
        AddNonControl(info);
    AddNonControl(m_message);
    AddNonControl(prompts);
    AddNonControl(new MI_EveryFrame([this] { RefreshGames(); }));

    setInitialFocus(m_back);
    SetCancelCode(MENU_CODE_TO_ONLINE_MENU);
}

void UI_SystemLinkMenu::Open()
{
#ifndef NETWORK_DISABLED
    s_browser.close();
    if (!s_browser.open())
        ShowMessage("Can't search the network (no UDP socket)");
    else
        ClearMessage();
#else
    ShowMessage("This build has no network support");
#endif
    m_shownCount = 0;
    m_frames = 0;
    for (int i = 0; i < ROWS; i++) {
        m_rows[i]->setVisible(false);
        m_info[i]->SetText({});
    }
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
    m_message->SetText(std::move(text));
}

void UI_SystemLinkMenu::ClearMessage()
{
    m_showingStatus = true;
    m_message->SetText({});
}

std::string UI_SystemLinkMenu::SelectedAddress(std::string& why) const
{
    for (int i = 0; i < m_shownCount; i++) {
        if (currentFocus() != m_rows[i])
            continue;
        if (!m_shown[i].sameBuild) {
            why = "That game is another version: both need the same build of the game";
            return {};
        }
        return m_shown[i].address;
    }
    why = "Choose a game in the list";
    return {};
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

void UI_SystemLinkMenu::RefreshGames()
{
#ifndef NETWORK_DISABLED
    s_browser.update(rollback::monotonicMs());

    if (m_frames++ % 10 == 0) {
        const auto& games = s_browser.games();
        const int before = m_shownCount;
        m_shownCount = static_cast<int>(std::min<size_t>(games.size(), ROWS));
        for (int i = 0; i < ROWS; i++) {
            const bool shown = i < m_shownCount;
            m_rows[i]->setVisible(shown);
            if (!shown) {
                m_info[i]->SetText({});
                m_shown[i] = {};
                continue;
            }
            const rollback::lan::FoundGame& game = games[static_cast<size_t>(i)];
            m_rows[i]->SetName(game.info.name.empty() ? game.address() : game.info.name);
            m_info[i]->SetText(gameInfo(game));
            m_shown[i] = {game.address(), game.info.build == GIT_REVISION};
        }

        //The chosen row went away: the first game (or Back). The first game found gets the focus.
        bool focusShown = currentFocus() == m_back;
        for (int i = 0; i < m_shownCount; i++)
            focusShown = focusShown || currentFocus() == m_rows[i];
        if (!focusShown || (before == 0 && m_shownCount > 0 && currentFocus() == m_back))
            focus(m_shownCount > 0 ? m_rows[0] : m_back);

        if (m_showingStatus) {
            std::string text;
            if (games.empty())
                text = "Looking for games on the network...";
            else if (games.size() > ROWS)
                text = std::to_string(games.size()) + " games found (the first " + std::to_string(ROWS) + " are listed)";
            else
                text = std::to_string(games.size()) + (games.size() == 1 ? " game found" : " games found");
            const std::string network = systemLinkNetworkStatus();
            if (!network.empty())
                text += " - " + network;
            m_message->SetText(text);
        }
    }
#endif
}

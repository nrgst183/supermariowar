#include "OnlineMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/MI_TextField.h"

#include <algorithm>
#include <cstdlib>

//The games hosted on the local network have a menu of their own (SystemLinkMenu.h: LAN games,
//where hosting is too), beside the address to type. (The Xbox has only that one: system link.)
#ifndef NETWORK_DISABLED
#define SMW_LAN_GAMES
#include "SystemLinkMenu.h"
#endif

namespace {
constexpr short MAX_NAME_CHARS = 17;       // the field takes one character less than this (MAX_NAME_LENGTH is 16)
constexpr short MAX_PORT_CHARS = 6;
constexpr short MAX_ADDRESS_CHARS = 64;
} // namespace

UI_OnlineMenu::UI_OnlineMenu()
    : UI_Menu()
{
    AddTitleBar("Online");

    //Three panels (under their fields): you, hosting, joining
    AddNonControl(new MI_MenuPanel(36, 42, 568, 112, "You", "Your name, and how far ahead you see"));
    //(the host's panel says the address the others on this network join)
    MI_MenuPanel* hostPanel = new MI_MenuPanel(36, 158, 568, 108, "Host a game");
    hostPanel->SetNoteSource([]() -> std::string {
        const std::string address = rollback::NetGame::lanAddress();
        if (address.empty())
            return "The others join this computer's address";
        const int port = atoi(game_values.onlineport.c_str());
        return "Others join " + address + ":" + std::to_string(port > 0 ? port : 7300);
    });
    AddNonControl(hostPanel);
    AddNonControl(new MI_MenuPanel(36, 270, 568, 108, "Join a game", "At the address the host gives you"));

    miNameField = new MI_TextField(&rm->spr_selectfield, 64, 78, "Nickname", 512, 150);
    miNameField->SetData(game_values.onlinename, MAX_NAME_CHARS);

    //Run-ahead: online matches are drawn this many ticks ahead of the real state, so your buttons
    //answer sooner (GameplayTick.cpp). Offline there's no network delay to hide: it isn't used.
    miRunAheadField = new MI_SelectField<short>(&rm->spr_selectfield, 64, 114, "Run-Ahead", 512, 150);
    miRunAheadField->add("Auto", -1);    //(as much as the input delay)
    miRunAheadField->add("Off", 0);
    miRunAheadField->add("1 Frame", 1);
    miRunAheadField->add("2 Frames", 2);
    miRunAheadField->add("3 Frames", 3);
    game_values.runahead = std::clamp<short>(game_values.runahead, -1, 3);
    miRunAheadField->setOutputPtr(&game_values.runahead);
    miRunAheadField->setCurrentValue(game_values.runahead);
    miRunAheadField->setAutoAdvance(true);

    miPortField = new MI_TextField(&rm->spr_selectfield, 64, 194, "Port", 512, 150);
    miPortField->SetData(game_values.onlineport, MAX_PORT_CHARS);
    miPortField->SetDisallowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()-_=+`~[]{}\\|;:'\",<>./?");
    miHostButton = new MI_Button(&rm->spr_selectfield, 170, 230, "Host game", 300, TextAlign::CENTER);
    miHostButton->SetCode(MENU_CODE_ONLINE_HOST);

    miAddressField = new MI_TextField(&rm->spr_selectfield, 64, 306, "Address", 512, 150);
    miAddressField->SetData(game_values.onlineaddress, MAX_ADDRESS_CHARS);
    miAddressField->SetDisallowedChars(" ");
#ifdef SMW_LAN_GAMES
    //Beside it, the games found on the local network
    miJoinButton = new MI_Button(&rm->spr_selectfield, 64, 342, "Join game", 250, TextAlign::CENTER);
    miFindGamesButton = new MI_Button(&rm->spr_selectfield, 326, 342, "LAN games", 250, TextAlign::CENTER);
    miFindGamesButton->SetCode(MENU_CODE_TO_SYSTEM_LINK_MENU);
#else
    miJoinButton = new MI_Button(&rm->spr_selectfield, 170, 342, "Join game", 300, TextAlign::CENTER);
#endif
    miJoinButton->SetCode(MENU_CODE_ONLINE_JOIN);

    //What went wrong (or what's happening), under the panels, in a panel of its own
    miMessage = new MI_WrappedText(320, 390, 512, 3);
    miMessage->SetBoxed(true);

    //What the buttons do here (a controller's, or the keys), and back
    miPrompts = new MI_PromptStrip(16, 434, 512, "", []() {
        using prompts::Pad;
        using prompts::menuKey;
        return std::vector<prompts::Prompt> {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"},
            {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select / type"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
    });
    miBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miBackButton->SetCode(MENU_CODE_TO_MAIN_MENU);

    AddControl(miNameField, miBackButton, miRunAheadField, nullptr, nullptr);
    AddControl(miRunAheadField, miNameField, miPortField, nullptr, nullptr);
    AddControl(miPortField, miRunAheadField, miHostButton, nullptr, nullptr);
#ifdef SMW_LAN_GAMES
    AddControl(miHostButton, miPortField, miAddressField, nullptr, nullptr);
    AddControl(miAddressField, miHostButton, miJoinButton, nullptr, nullptr);
    AddControl(miJoinButton, miAddressField, miBackButton, nullptr, miFindGamesButton);
    AddControl(miFindGamesButton, miAddressField, miBackButton, miJoinButton, nullptr);
    AddControl(miBackButton, miJoinButton, miNameField, nullptr, nullptr);
#else
    AddControl(miHostButton, miPortField, miAddressField, nullptr, nullptr);
    AddControl(miAddressField, miHostButton, miJoinButton, nullptr, nullptr);
    AddControl(miJoinButton, miAddressField, miBackButton, nullptr, nullptr);
    AddControl(miBackButton, miJoinButton, miNameField, nullptr, nullptr);
#endif

    AddNonControl(miMessage);
    AddNonControl(miPrompts);

    setInitialFocus(miHostButton);
    SetCancelCode(MENU_CODE_TO_MAIN_MENU);
}

void UI_OnlineMenu::Open()
{
    miNameField->Refresh();
    miPortField->Refresh();
    miAddressField->Refresh();
    ResetMenu();
}

void UI_OnlineMenu::ShowMessage(std::string text)
{
    //Its icon: a problem, or just news (e.g. "The host closed the lobby")
    using Tone = MI_WrappedText::Tone;
    const bool problem = text.rfind("Can't", 0) == 0 || text.rfind("Could not", 0) == 0 || text.rfind("Lost", 0) == 0
        || text.rfind("The port", 0) == 0 || text.find("removed") != std::string::npos;
    const bool missing = text.rfind("Enter", 0) == 0;
    miMessage->SetText(std::move(text), problem ? Tone::Error : missing ? Tone::Warn : Tone::Info);
}

void UI_OnlineMenu::ClearMessage()
{
    miMessage->SetText({});
}

const std::string& UI_OnlineMenu::nickname() const
{
    return game_values.onlinename;
}

const std::string& UI_OnlineMenu::address() const
{
    return game_values.onlineaddress;
}

int UI_OnlineMenu::port() const
{
    return atoi(game_values.onlineport.c_str());
}

#include "OnlineMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MenuPanel.h"
#include "ui/MI_Text.h"
#include "ui/MI_TextField.h"

#include <cstdlib>


namespace {
constexpr short MAX_NAME_CHARS = 17;       // the field takes one character less than this (MAX_NAME_LENGTH is 16)
constexpr short MAX_PORT_CHARS = 6;
constexpr short MAX_ADDRESS_CHARS = 64;
} // namespace


UI_OnlineMenu::UI_OnlineMenu()
    : UI_Menu()
{
    miLeftHeaderBar = new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0);
    miRightHeaderBar = new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0);
    miHeaderText = new MI_HeaderText("Online", 320, 5);
    AddNonControl(miLeftHeaderBar);
    AddNonControl(miRightHeaderBar);
    AddNonControl(miHeaderText);

    //Three panels (under their fields): you, hosting, joining
    AddNonControl(new MI_MenuPanel(36, 42, 568, 78, "You", "Your name in the lobby and the matches"));
    AddNonControl(new MI_MenuPanel(36, 126, 568, 118, "Host a game", "The others join this computer's address"));
    AddNonControl(new MI_MenuPanel(36, 250, 568, 118, "Join a game", "At the address the host gives you"));

    miNameField = new MI_TextField(&rm->spr_selectfield, 64, 78, "Nickname", 512, 150);
    miNameField->SetData(game_values.onlinename, MAX_NAME_CHARS);

    miPortField = new MI_TextField(&rm->spr_selectfield, 64, 162, "Port", 512, 150);
    miPortField->SetData(game_values.onlineport, MAX_PORT_CHARS);
    miPortField->SetDisallowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()-_=+`~[]{}\\|;:'\",<>./?");
    miHostButton = new MI_Button(&rm->spr_selectfield, 170, 202, "Host game", 300, TextAlign::CENTER);
    miHostButton->SetCode(MENU_CODE_ONLINE_HOST);

    miAddressField = new MI_TextField(&rm->spr_selectfield, 64, 286, "Address", 512, 150);
    miAddressField->SetData(game_values.onlineaddress, MAX_ADDRESS_CHARS);
    miAddressField->SetDisallowedChars(" ");
    miJoinButton = new MI_Button(&rm->spr_selectfield, 170, 326, "Join game", 300, TextAlign::CENTER);
    miJoinButton->SetCode(MENU_CODE_ONLINE_JOIN);

    //What went wrong (or what's happening), under the panels
    miMessage = new MI_WrappedText(320, 376, 580, 3);

    //What the buttons do here (a controller's, or the keys), and back
    miPrompts = new MI_PromptStrip(16, 434, 512, "", []() {
        using prompts::Pad;
        using prompts::menuKey;
        return std::vector<prompts::Prompt> {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"},
            {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select / type"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
    });
    miBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miBackButton->SetCode(MENU_CODE_TO_MAIN_MENU);

    AddControl(miNameField, miBackButton, miPortField, nullptr, nullptr);
    AddControl(miPortField, miNameField, miHostButton, nullptr, nullptr);
    AddControl(miHostButton, miPortField, miAddressField, nullptr, nullptr);
    AddControl(miAddressField, miHostButton, miJoinButton, nullptr, nullptr);
    AddControl(miJoinButton, miAddressField, miBackButton, nullptr, nullptr);
    AddControl(miBackButton, miJoinButton, miNameField, nullptr, nullptr);

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
    miMessage->SetText(std::move(text));
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

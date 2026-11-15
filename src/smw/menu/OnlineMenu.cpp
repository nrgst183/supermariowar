#include "OnlineMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
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
    miNameField = new MI_TextField(&rm->spr_selectfield, 60, 56, "Nickname", 520, 150);
    miNameField->SetData(game_values.onlinename, MAX_NAME_CHARS);

    miHostLabel = new MI_Text("Host a game on this computer", 70, 112, 0, false);

    miPortField = new MI_TextField(&rm->spr_selectfield, 60, 128, "Port", 520, 150);
    miPortField->SetData(game_values.onlineport, MAX_PORT_CHARS);
    miPortField->SetDisallowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()-_=+`~[]{}\\|;:'\",<>./?");

    miHostButton = new MI_Button(&rm->spr_selectfield, 60, 168, "Host game", 520, TextAlign::CENTER);
    miHostButton->SetCode(MENU_CODE_ONLINE_HOST);

    miJoinLabel = new MI_Text("Join a game by address", 70, 232, 0, false);

    miAddressField = new MI_TextField(&rm->spr_selectfield, 60, 248, "Address", 520, 150);
    miAddressField->SetData(game_values.onlineaddress, MAX_ADDRESS_CHARS);
    miAddressField->SetDisallowedChars(" ");

    miJoinButton = new MI_Button(&rm->spr_selectfield, 60, 288, "Join game", 520, TextAlign::CENTER);
    miJoinButton->SetCode(MENU_CODE_ONLINE_JOIN);

    miMessage = new MI_WrappedText(320, 342, 580, 3);

    //What the buttons do here (a controller's, or the keys)
    miPrompts = new MI_PromptRow(282, 440);
    miPrompts->SetPrompts([]() {
        using prompts::Pad;
        using prompts::menuKey;
        return std::vector<prompts::Prompt> {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"},
            {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select / type"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
    });

    miBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miBackButton->SetCode(MENU_CODE_TO_MAIN_MENU);

    miLeftHeaderBar = new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0);
    miRightHeaderBar = new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0);
    miHeaderText = new MI_HeaderText("Online Menu", 320, 5);

    AddControl(miNameField, miBackButton, miPortField, nullptr, nullptr);
    AddControl(miPortField, miNameField, miHostButton, nullptr, nullptr);
    AddControl(miHostButton, miPortField, miAddressField, nullptr, nullptr);
    AddControl(miAddressField, miHostButton, miJoinButton, nullptr, nullptr);
    AddControl(miJoinButton, miAddressField, miBackButton, nullptr, nullptr);
    AddControl(miBackButton, miJoinButton, miNameField, nullptr, nullptr);

    AddNonControl(miLeftHeaderBar);
    AddNonControl(miRightHeaderBar);
    AddNonControl(miHeaderText);
    AddNonControl(miHostLabel);
    AddNonControl(miJoinLabel);
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

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

namespace {
constexpr short MAX_NAME_CHARS = 17;       // the field takes one character less than this (MAX_NAME_LENGTH is 16)
constexpr short MAX_PORT_CHARS = 6;
constexpr short MAX_ADDRESS_CHARS = 64;
} // namespace

UI_OnlineMenu::UI_OnlineMenu()
    : UI_Menu()
{
    SetHints(false);    //(its own prompts, at the bottom)

    // The title bar: the page's name
    AddNonControl(newTitleBarBack());
    miTitle = new MI_HeaderText("Online Options", 320, 5);
    AddNonControl(miTitle);

    // Settings: you, and hosting
    miYouPanel = new MI_MenuPanel(36, 42, 568, 156, "You", "Your nickname, run-ahead and downloads");
    miHostPanel = new MI_MenuPanel(36, 202, 568, 84, "Hosting", "The port friends join on");
    miHostPanel->SetNoteSource([]() -> std::string {
        const std::string address = rollback::NetGame::lanAddress();
        if (address.empty())
            return "The port friends join on";
        const int port = atoi(game_values.onlineport.c_str());
        return "Others join " + address + ":" + std::to_string(port > 0 ? port : 7300);
    });
    AddNonControl(miYouPanel);
    AddNonControl(miHostPanel);

    miNameField = new MI_TextField(&rm->spr_selectfield, 64, 78, "Nickname", 512, 150);
    miNameField->SetData(game_values.onlinename, MAX_NAME_CHARS);

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

    //What is downloaded from the others when this machine lacks it: their skins (else a ? block in their
    //place), the host's map (else this machine can't play on it)
    miDownloadsField = new MI_SelectField<short>(&rm->spr_selectfield, 64, 150, "Downloads", 512, 150);
    miDownloadsField->add("Skins and Maps", 3);
    miDownloadsField->add("Skins Only", 2);
    miDownloadsField->add("Maps Only", 1);
    miDownloadsField->add("Off", 0);
    miDownloadsField->setCurrentValue(downloadsValue());
    miDownloadsField->setOnChanged([](short value) {
        game_values.skindownloads = (value & 2) != 0;
        game_values.mapdownloads = (value & 1) != 0;
    });

    miPortField = new MI_TextField(&rm->spr_selectfield, 64, 238, "Port", 512, 150);
    miPortField->SetData(game_values.onlineport, MAX_PORT_CHARS);
    miPortField->SetDisallowedChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()-_=+`~[]{}\\|;:'\",<>./?");

    // In the matches (and online replays): the others' connection on their score panels, and the
    // connection's numbers (F3 in a match toggles them too)
    miMatchPanel = new MI_MenuPanel(36, 290, 568, 120, "In online matches", "What the screen shows");
    AddNonControl(miMatchPanel);

    miPingBarsField = new MI_SelectField<bool>(&rm->spr_selectfield, 64, 326, "Ping Bars", 512, 150);
    miPingBarsField->add("Off", false);
    miPingBarsField->add("On", true);
    miPingBarsField->setOutputPtr(&game_values.showpingbars);
    miPingBarsField->setCurrentValue(game_values.showpingbars ? 1 : 0);
    miPingBarsField->setAutoAdvance(true);

    miNetStatsField = new MI_SelectField<bool>(&rm->spr_selectfield, 64, 362, "Net Stats", 512, 150);
    miNetStatsField->add("Off", false);
    miNetStatsField->add("On", true);
    miNetStatsField->setOutputPtr(&game_values.shownetstats);
    miNetStatsField->setCurrentValue(game_values.shownetstats ? 1 : 0);
    miNetStatsField->setAutoAdvance(true);


    // Join by address (from the games list: a game that isn't on this network), in the middle of the
    // screen: it's all the screen has
    miJoinPanel = new MI_MenuPanel(36, 150, 568, 120, "Join a game", "Type in the host's address");
    AddNonControl(miJoinPanel);

    miAddressField = new MI_TextField(&rm->spr_selectfield, 64, 186, "Address", 512, 150);
    miAddressField->SetData(game_values.onlineaddress, MAX_ADDRESS_CHARS);
    miAddressField->SetDisallowedChars(" ");
    miJoinButton = new MI_Button(&rm->spr_selectfield, 170, 222, "Join game", 300, TextAlign::CENTER);
    miJoinButton->SetCode(MENU_CODE_ONLINE_JOIN);


    //What went wrong (or what's happening), under the panels, in a panel of its own
    miMessage = new MI_WrappedText(320, 382, 512, 3);
    miMessage->SetBoxed(true);

    //What the buttons do here (a controller's, or the keys), and back
    miPrompts = new MI_PromptStrip(16, 434, 512, "", []() {
        using prompts::Pad;
        using prompts::menuKey;
        return std::vector<prompts::Prompt> {{{Pad::Up, Pad::Down}, {menuKey(prompts::MENU_UP), menuKey(prompts::MENU_DOWN)}, "choose"},
            {{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "select / type"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}};
    });

    //(round from the last to the first; B, or the footer's "back": back)
    AddControl(miNameField, miNetStatsField, miRunAheadField, nullptr, nullptr);
    AddControl(miRunAheadField, miNameField, miDownloadsField, nullptr, nullptr);
    AddControl(miDownloadsField, miRunAheadField, miPortField, nullptr, nullptr);
    AddControl(miPortField, miDownloadsField, miPingBarsField, nullptr, nullptr);
    AddControl(miPingBarsField, miPortField, miNetStatsField, nullptr, nullptr);
    AddControl(miNetStatsField, miPingBarsField, miNameField, nullptr, nullptr);

    AddControl(miAddressField, miJoinButton, miJoinButton, nullptr, nullptr);
    AddControl(miJoinButton, miAddressField, miAddressField, nullptr, nullptr);

    AddNonControl(miMessage);
    AddNonControl(miPrompts);

    ShowMode(Mode::Join);
}

short UI_OnlineMenu::downloadsValue()
{
    return static_cast<short>((game_values.skindownloads ? 2 : 0) | (game_values.mapdownloads ? 1 : 0));
}

void UI_OnlineMenu::ShowMode(Mode mode)
{
    m_mode = mode;
    const bool settings = mode == Mode::Settings;
    miTitle->SetText(settings ? "Online Options" : "Join by Address");
    for (UI_Control* control : std::initializer_list<UI_Control*> {miYouPanel, miHostPanel, miNameField, miRunAheadField, miDownloadsField, miPortField, miMatchPanel,
             miPingBarsField, miNetStatsField})
        control->setVisible(settings);
    for (UI_Control* control : std::initializer_list<UI_Control*> {miJoinPanel, miAddressField, miJoinButton})
        control->setVisible(!settings);
    setInitialFocus(settings ? static_cast<UI_Control*>(miNameField) : static_cast<UI_Control*>(miAddressField));
    SetCancelCode(settings ? MENU_CODE_BACK_TO_OPTIONS_MENU : MENU_CODE_TO_SYSTEM_LINK_MENU);
    //(what went wrong: under the panels, the settings' or the join panel)
    miMessage->SetPosition(320, settings ? 414 : 282);
}

void UI_OnlineMenu::Open(Mode mode)
{
    miNameField->Refresh();
    miPortField->Refresh();
    miAddressField->Refresh();
    miRunAheadField->setCurrentValue(game_values.runahead);
    miDownloadsField->setCurrentValue(downloadsValue());
    miPingBarsField->setCurrentValue(game_values.showpingbars ? 1 : 0);
    miNetStatsField->setCurrentValue(game_values.shownetstats ? 1 : 0);
    ShowMode(mode);
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

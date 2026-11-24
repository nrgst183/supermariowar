#pragma once

#include "uimenu.h"

#include <string>

class MI_Button;
class MI_Image;
class MI_Text;
class MI_TextField;
template <typename T> class MI_SelectField;
class MI_WrappedText;
class MI_PromptStrip;

class MI_HeaderText;
class MI_MenuPanel;

/*
    Two pages of the online (rollback netplay) mode, outside the lobby:
    - Settings (Options > Online): your nickname, run-ahead, and the port you host on
    - Join (the games list's "Join by address"): the address of a game that isn't on this network
    Hosting and joining the games on the network are the games list's (SystemLinkMenu.h); a local
    game's lobby is opened to the others by its Who can join. Errors of joining are shown here.
*/
class UI_OnlineMenu : public UI_Menu {
public:
    enum class Mode { Settings, Join };

    UI_OnlineMenu();

    /// Shows the page, with the remembered nickname, port and address
    void Open(Mode mode);
    Mode mode() const { return m_mode; }

    void ShowMessage(std::string text);
    void ClearMessage();

    /// The nickname, the port to host on (0: the default) and the address to join, as entered
    const std::string& nickname() const;
    const std::string& address() const;
    int port() const;

private:
    void ShowMode(Mode mode);
    // The downloads field's value: skins (2) and maps (1), from the settings
    static short downloadsValue();

    Mode m_mode = Mode::Join;
    MI_HeaderText* miTitle;

    MI_MenuPanel* miYouPanel;
    MI_MenuPanel* miHostPanel;
    MI_TextField* miNameField;
    MI_SelectField<short>* miRunAheadField;
    MI_SelectField<short>* miDownloadsField;
    MI_TextField* miPortField;
    MI_MenuPanel* miMatchPanel;
    MI_SelectField<bool>* miPingBarsField;
    MI_SelectField<bool>* miNetStatsField;

    MI_MenuPanel* miJoinPanel;
    MI_TextField* miAddressField;
    MI_Button* miJoinButton;

    MI_WrappedText* miMessage;
    MI_PromptStrip* miPrompts;
};

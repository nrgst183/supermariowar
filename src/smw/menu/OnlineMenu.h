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

/*
    The entry of the online (rollback netplay) mode: your nickname and run-ahead, hosting a lobby
    on a port and joining one by address (or the LAN games: the System Link menu, which hosts too).
    Not on the Xbox: its main menu opens the System Link menu. Errors of hosting and joining are
    shown at the bottom.
*/
class UI_OnlineMenu : public UI_Menu {
public:
    UI_OnlineMenu();

    /// Shows the remembered nickname, port and address again (when the menu is opened)
    void Open();

    void ShowMessage(std::string text);
    void ClearMessage();

    /// The nickname, the port to host on (0: the default) and the address to join, as entered
    const std::string& nickname() const;
    const std::string& address() const;
    int port() const;

private:
    MI_TextField* miNameField;
    MI_SelectField<short>* miRunAheadField;
    MI_TextField* miPortField;
    MI_Button* miHostButton;
    MI_TextField* miAddressField;
    MI_Button* miJoinButton;
    MI_Button* miFindGamesButton = nullptr;    // the system link / LAN games menu (builds with the network)
    MI_Button* miBackButton;

    MI_WrappedText* miMessage;
    MI_PromptStrip* miPrompts;
};

#pragma once

#include "uimenu.h"

#include <string>

class MI_Button;
class MI_Text;
class MI_TextField;
class UI_Control;

// Main menu > Editors: first the map (level) editor or the world editor, two
// big buttons; then on this computer or online (host, or join an address), and
// Start (editors/EditorLauncher.h, editors/CoEdit.h)
class UI_EditorsMenu : public UI_Menu {
public:
    UI_EditorsMenu();

    // The address to join ("ip" or "ip:port")
    const std::string& joinAddress() const { return m_address; }
    std::string& joinAddressRef() { return m_address; }
    void SetStatus(const std::string& text);

    // MENU_CODE_EDITORS_ACTION: does what the pressed button wants; returns the
    // menu code to go on with (an editor to run, to join, to leave), or none
    MenuCodeEnum Act();
    // Back (Escape, B): the setup goes back to the choice, the choice leaves
    MenuCodeEnum Back();
    // Opening the menu: the choice again
    void Open();

    enum class Action { None, Maps, Worlds, Local, Online, Host, Join, Start };
    void Want(Action action) { m_pending = action; }

private:
    void ShowPage(bool setup);
    void Relink();

    std::string m_address;
    Action m_pending = Action::None;
    bool m_setup = false;     // the second page
    bool m_level = true;      // maps (else worlds)
    bool m_online = false;
    bool m_joining = false;
    short m_panelY = 42;     // the setup panel (it fits what shows, centered)
    short m_panelH = 340;
    short m_whereY = 0;      // its rows
    short m_howY = 0;

    MI_Button* miMaps = nullptr;
    MI_Button* miWorlds = nullptr;
    MI_Button* miLocal = nullptr;
    MI_Button* miOnline = nullptr;
    MI_Button* miHost = nullptr;
    MI_Button* miJoin = nullptr;
    MI_TextField* miAddressField = nullptr;
    MI_Button* miStart = nullptr;
    MI_Button* miBack = nullptr;
    UI_Control* miSetupPanel = nullptr;
    MI_Text* miPickText = nullptr;
    UI_Control* miPickBox = nullptr;
    MI_Text* miMapsText = nullptr;
    MI_Text* miWorldsText = nullptr;
    MI_Text* miStatusText = nullptr;
    MI_Text* miWhereText = nullptr;
    MI_Text* miHowText = nullptr;
};

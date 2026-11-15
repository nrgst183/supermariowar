#pragma once

#include "uimenu.h"

#include <string>

class MI_Text;
class MI_TextField;

// Main menu > Editors: the map (level) editor and the world editor, built in
// (editors/EditorLauncher.h), alone or together (editors/CoEdit.h)
class UI_EditorsMenu : public UI_Menu {
public:
    UI_EditorsMenu();

    // The address to join ("ip" or "ip:port")
    const std::string& joinAddress() const { return m_address; }
    std::string& joinAddressRef() { return m_address; }
    void SetStatus(const std::string& text);

private:
    std::string m_address;
    MI_TextField* miAddressField;
    MI_Text* miStatusText;
};

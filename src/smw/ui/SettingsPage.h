#pragma once

#include "uicontrol.h"
#include "ui/MenuCode.h"

#include <string>
#include <utility>
#include <vector>

class MI_Button;
class UI_Menu;

/// Along the bottom of the screen: what the menu's selected row does (a line for each row that has one)
class MI_SettingsHelp : public UI_Control {
public:
    explicit MI_SettingsHelp(const UI_Menu& menu);

    void set(const UI_Control* row, std::string text);
    void Draw() override;

private:
    const UI_Menu& m_menu;
    std::vector<std::pair<const UI_Control*, std::string>> m_lines;
};

/// The layout of a settings screen (Gameplay, Graphics...): the original's column of rows, in the
/// middle of the screen, the Back button at the bottom right, and along the bottom of the screen what
/// the selected row does.
/// Make each row at x(), rowY() (ROW_W wide, LABEL_W for its name), then add() it; finish() puts it
/// all in the menu and links the rows: up and down, and right to Back.
class SettingsPage {
public:
    static constexpr short ROW_X = 70;
    static constexpr short ROW_W = 500;
    static constexpr short LABEL_W = 220;

    /// `rows`: how many there will be (the column is centered)
    explicit SettingsPage(short rows);

    short x() const { return ROW_X; }
    /// Where the next row goes
    short rowY() const { return m_next; }
    void add(UI_Control* row, std::string help);

    /// Into `menu`: the title bar, the rows, the Back button (`backCode`, which B is too) and the
    /// help line. Returns the Back button.
    MI_Button* finish(UI_Menu& menu, const std::string& title, MenuCodeEnum backCode);

private:
    struct Row {
        UI_Control* control;
        std::string help;
    };

    short m_expected;   // (how many rows)
    short m_step;
    short m_next;
    std::vector<Row> m_rows;
};

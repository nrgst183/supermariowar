#pragma once

#include "uicontrol.h"
#include "uimenu.h"
#include "ui/InputPrompts.h"

#include <functional>
#include <string>
#include <vector>

// The menus' panels (the Editors and Online menus): rounded, a white rim, SMB3's
// status bar blue. Drawn under the buttons in them (add them to the menu first).
namespace menupanel {
void box(int x, int y, int w, int h);
/// A row of a list in a panel: darker than the panel, or (selected) bright with a white rim;
/// `dim`: a row that can't be chosen (greyer)
void row(int x, int y, int w, int h, bool selected, bool dim = false);
// A line of text in the large font if it fits in w (read from a couch), else the small one (cut to w);
// y: where the small font's line would be
void bestText(int x, int y, int w, const std::string& text);
/// Every menu's footer: a status bar strip along the bottom of the screen (y 448 to 480), the button
/// prompts at its right, a label (what the focused thing does, or how things stand) in the room left
/// at its left, after an icon if `icon` is a theme::Status (-1: none)
void footer(const std::vector<prompts::Prompt>& row, const std::string& label = "", int icon = -1);
/// A frame of the menus: whether a footer was drawn in it (the menus without one get the usual one)
void beginFrame();
bool footerDrawn();
/// Whether (x,y) is on the last footer's B prompt (its "back", "leave"...): a click there is a B press
bool footerBackAt(short x, short y);
/// The usual prompts: up/down chooses, A selects (left/right changes, for settings), B goes back
std::vector<prompts::Prompt> standardPrompts(bool change, const char* back = "back");
/// A question's box (every yes/no dialog: the menus', the pause screen's, the editors'): a panel in
/// the middle of the screen, where the original's dialog was (its lines at y 195 and 220, its
/// buttons at 250)
void dialogBox(int x = 150, int y = 172, int w = 340, int h = 128);
/// For a dialog that draws its own two choices (not buttons): SMB3 blocks side by side around cx, at
/// y, the chosen one gold
void dialogChoice(int cx, int y, const char* left, const char* right, bool leftChosen);
}

/// A yes/no dialog's box (menupanel::dialogBox), as a menu's control: shown and hidden with the dialog
class MI_DialogBox : public UI_Control {
public:
    MI_DialogBox()
        : UI_Control(150, 172)
    {}
    void Draw() override;
};

/// A panel, with a title at its top left (and a line of small text at its top right)
class MI_MenuPanel : public UI_Control {
public:
    MI_MenuPanel(short x, short y, short w, short h, std::string title = {}, std::string note = {});

    void SetTitle(std::string title, std::string note = {});
    /// The note asked for every time it's drawn (instead of a fixed one)
    void SetNoteSource(std::function<std::string()> note) { m_noteSource = std::move(note); }
    void Draw() override;

private:
    short m_w;
    short m_h;
    std::string m_title;
    std::string m_note;
    std::function<std::string()> m_noteSource;
};

/// A panel behind controls already made, around them (`padX`, `padY` out; kept between the title
/// bar and the footer), added under everything in the menu
MI_MenuPanel* AddPanelBehind(UI_Menu& menu, std::initializer_list<const UI_Control*> controls, short padX = 18, short padY = 16);
MI_MenuPanel* AddPanelBehind(UI_Menu& menu, const std::vector<const UI_Control*>& controls, short padX = 18, short padY = 16);

/// A menu's button prompts, with a label first: the controller's buttons or the keys, whichever
/// was used last (asked every frame); drawn as the footer along the bottom of the screen
class MI_PromptStrip : public UI_Control {
public:
    MI_PromptStrip(short x, short y, short w, std::string label, std::function<std::vector<prompts::Prompt>()> row);
    void Draw() override;

private:
    short m_w;
    std::string m_label;
    std::function<std::vector<prompts::Prompt>()> m_row;
};

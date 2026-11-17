#pragma once

#include "uicontrol.h"
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
}

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

/// A strip of button prompts in a panel, with a label first: the controller's buttons or the
/// keys, whichever was used last (asked every frame)
class MI_PromptStrip : public UI_Control {
public:
    MI_PromptStrip(short x, short y, short w, std::string label, std::function<std::vector<prompts::Prompt>()> row);
    void Draw() override;

private:
    short m_w;
    std::string m_label;
    std::function<std::vector<prompts::Prompt>()> m_row;
};

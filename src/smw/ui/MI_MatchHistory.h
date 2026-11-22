#pragma once

#include "uicontrol.h"
#include "MatchHistory.h"
#include "gfx.h"

#include <string>
#include <vector>

// The Match History screen's list (MatchHistory.h): the matches played,
// newest first, one per line, and the selected match's details (teams,
// players, final scores) below. Select plays its replay.
class MI_MatchHistory : public UI_Control {
public:
    MI_MatchHistory(short x, short y, short width, short lines);

    // Reads the history file again (the selection goes to the newest match)
    void Refresh() override;
    void Draw() override;
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;
    MenuCodeEnum Modify(bool modify) override;

    // The replay file of the selected match ("" if it has none)
    std::string selectedReplay() const;
    // Shown in the details box until the selection changes (e.g. why a replay can't play)
    void setMessage(const std::string& message) { m_message = message; }

private:
    void move(int delta);
    void drawBox(short x, short y, short w, short h) const;
    void drawDetails(short y) const;

    std::vector<history::Entry> m_entries;
    short m_width;
    short m_lines;
    int m_index = 0;
    int m_offset = 0;
    std::string m_message;
    // The selected match's players' skins (MI_MatchHistory.cpp): which match they're for, per team
    void loadPortraits(const history::Entry& e) const;
    mutable int m_portraitsFor = -1;
    mutable std::vector<std::vector<SpriteStrip>> m_portraits;
};

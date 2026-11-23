#pragma once

#include "ui/MenuCode.h"
#include "math/Vec2.h"

#include <array>

class CPlayerInput;
class UI_Menu;


enum class TextAlign: unsigned char {
    LEFT,
    CENTER,
    RIGHT,
};


class UI_Control
{
public:
    UI_Control(short x, short y);
    UI_Control(const UI_Control&);
    UI_Control& operator=(const UI_Control&);
    virtual ~UI_Control() = default;

    /// Updates animations or other events every frame
    virtual void Update() {}

    /// Draws every frame
    virtual void Draw() {}
    /// The focused control's drawing over the whole menu (after every control): a popup of its own
    virtual void DrawOverlay() {}

    /// Sends player input to control on every frame
    virtual MenuCodeEnum SendInput(CPlayerInput*) {
        return MENU_CODE_NONE;
    }

    /// Called when user selects this control to change it's value
    virtual MenuCodeEnum Modify(bool modify) {
        if (fDisable)
            return MENU_CODE_UNSELECT_ITEM;

        fModifying = modify;
        return MENU_CODE_MODIFY_ACCEPTED;
    }

    virtual MenuCodeEnum MouseClick(short iMouseX, short iMouseY) {
        return MENU_CODE_NONE;
    }

    /// Where the control is on the screen: the menus frame it when it's the focused one, and the mouse
    /// points at it there. Empty (w 0): no single box (a control that shows its own selection)
    struct Box {
        short x = 0;
        short y = 0;
        short w = 0;
        short h = 0;
    };
    virtual Box FocusBox() const {
        return {};
    }
    /// A click at x, y on the control when it does something of its own there (true, and the code
    /// to handle); false: the menu's usual click (as A)
    virtual bool PointerPress(short /*x*/, short /*y*/, MenuCodeEnum& /*code*/) {
        return false;
    }
    bool Contains(short x, short y) const {
        const Box b = FocusBox();
        return b.w > 0 && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
    }

    /// Left/right on the control while it's only focused, when the menu has nothing that way: a value
    /// field takes the next/previous value at once (no select, change, select). Whether it does that,
    /// and doing it (dir -1/1): the code its change returns
    virtual bool CanNudge() const {
        return false;
    }
    virtual MenuCodeEnum Nudge(int /*dir*/) {
        return MENU_CODE_NONE;
    }
    /// A button's code (what pressing it does), MENU_CODE_NONE for the rest
    virtual MenuCodeEnum PressCode() const {
        return MENU_CODE_NONE;
    }

    virtual void Refresh() {}

    virtual void Disable(bool disable) {
        fDisable = disable;
    }

    /// The control can't be changed by this player (the online match settings, when only the host may):
    /// inert like a disabled control, and drawn with the disabled look of the select fields
    void SetLocked(bool locked) {
        fLocked = locked;
        Disable(locked);
    }
    bool IsLocked() const {
        return fLocked;
    }

    bool Select(bool select) {
        fSelected = select;

        if (fSelected && fAutoModify)
            Modify(true);

        return fModifying;
    }

    void SetAutoModify(bool autoModify) {
        fAutoModify = autoModify;
    }
    bool IsAutoModify() const {
        return fAutoModify;
    }

    /// Moves the control (a control with parts of its own moves them along)
    virtual void SetPosition(short x, short y) {
        m_pos = {x, y};
    }
    short x() const { return m_pos.x; }
    short y() const { return m_pos.y; }

    void setNeighbor(MenuNavDirection iNeighbor, UI_Control* uiControl);

    UI_Control* neighbor(MenuNavDirection iNeighbor) const {
        const auto idx = static_cast<std::size_t>(iNeighbor);
        return m_neighbors[idx];
    }

    void setVisible(bool show) {
        m_visible = show;
    }
    bool IsVisible() const {
        return m_visible;
    }

    void setParent(UI_Menu* menu) {
        m_parentMenu = menu;
    }

    bool IsModifying() const {
        return fModifying;
    }

    void SetControllingTeam(short teamid) {
        iControllingTeam = teamid;
    }

protected:
    Vec2s m_pos = Vec2s::zero();

    bool fSelected = false;
    bool fModifying = false;
    bool fAutoModify = false;
    bool fDisable = false;
    bool fLocked = false;
    bool m_visible = true;

    std::array<UI_Control*, 4> m_neighbors;

    UI_Menu* m_parentMenu = nullptr;
    short iControllingTeam = -1;
};

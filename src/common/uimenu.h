#ifndef UIMENU_H
#define UIMENU_H

#include "eyecandy.h"
#include "ui/MenuCode.h"

#include <memory>
#include <string>
#include <vector>

class UI_Control;
class CPlayerInput;
class MI_HeaderText;

class UI_Menu {
public:
    virtual ~UI_Menu() = default;

    void AddControl(UI_Control* control, UI_Control* up, UI_Control* down, UI_Control* left, UI_Control* right);
    void AddNonControl(UI_Control* control);
    /// A non-control drawn under everything else in the menu (a panel behind its controls)
    void AddBackdrop(UI_Control* control);
    /// Moves everything in the menu so far by (dx, dy)
    void ShiftAll(short dx, short dy);
    /// The bar along the top of the screen, with the menu's title (a non-control)
    void AddTitleBar(const std::string& title);

    /// Sets the initially focused element on opening/resetting the menu.
    void setInitialFocus(UI_Control* control);
    /// The initially focused element on opening/resetting the menu.
    UI_Control* initialFocus() const { return m_initialFocus; }
    /// The currently focused element of the menu.
    UI_Control* currentFocus() const { return m_currentFocus; }

    void SetCancelCode(MenuCodeEnum code) {
        cancelCode = code;
    }
    MenuCodeEnum CancelCode() const {
        return cancelCode;
    }
    /// Left/right change the focused control's value (a value field with nothing to its sides but a
    /// shortcut to the menu's Back button, which B is too)
    bool NudgesFocus() const;

    /// What the buttons do, drawn in the title bar of the menus that have one (the game sets it: the
    /// prompts are its; the editors have none); a menu with hints of its own turns them off
    static void (*drawHints)(const UI_Menu& menu);
    // The title bar's look (the game's: SmwTheme); none: the menus' plain field image
    static void (*drawTitleBar)();
    // The bar under a title, in that look: drawn now, or as a control of a menu (the first thing on it)
    static void drawTitleBarBack();
    static UI_Control* newTitleBarBack();
    const MI_HeaderText* Title() const {
        return m_hints ? m_title : nullptr;
    }
    void SetHints(bool hints) {
        m_hints = hints;
    }
    /// One of a set of pages (the options screens): the shoulders turn to the one before and after
    void SetPaged(bool paged) {
        m_paged = paged;
    }
    bool Paged() const {
        return m_paged;
    }

    void ResetMenu();

    MenuCodeEnum SendInput(CPlayerInput* playerInput);
    void Update();
    void Draw();

    template<typename T, typename... Args>
    void AddEyeCandy(Args&&... args) {
        eyeCandy.emplace<T>(std::forward<Args>(args)...);
    }
    void ClearEyeCandy() {
        eyeCandy.clean();
    }

    void RememberCurrent();
    void RestoreCurrent();

    void SetControllingTeam(short teamid) {
        iControllingTeam = teamid;
    }
    void SetAllowExit(bool allowExit) {
        fAllowExitButton = allowExit;
    }

    MenuCodeEnum MouseClick(short iMouseX, short iMouseY);

    /// The mouse in the menus (the game's: GSMenu.cpp): pointing at a control focuses it; a click
    /// presses it (a value field: its next value, or, while it's being changed, its arrows); the right
    /// button backs out; the wheel steps the focused value field. Each returns the code to handle, as
    /// SendInput does.
    MenuCodeEnum PointerMove(short x, short y);
    MenuCodeEnum PointerClick(short x, short y, bool right);
    MenuCodeEnum PointerWheel(int dir);

    /// Draws the frame around the focused control (the game sets it; with none, no frame)
    static void (*drawFocus)(const UI_Control& control, bool modifying);

    bool IsModifying() const {
        return fModifyingItem;
    }

    void Refresh();

protected:
    MenuCodeEnum MoveNextControl(MenuNavDirection iDirection);
    UI_Control* ControlAt(short x, short y) const;
    void Focus(UI_Control* control);

    std::vector<std::unique_ptr<UI_Control>> controls;

    UI_Control* m_initialFocus = nullptr;
    UI_Control* m_currentFocus = nullptr;
    UI_Control* m_savedCurrent = nullptr;

    MenuCodeEnum cancelCode = MENU_CODE_NONE;
    bool fModifyingItem = false;
    const MI_HeaderText* m_title = nullptr;
    bool m_hints = true;
    bool m_paged = false;

    CEyecandyContainer eyeCandy;

    short iControllingTeam = -1;
    bool fAllowExitButton = true;
};

#endif // UIMENU_H

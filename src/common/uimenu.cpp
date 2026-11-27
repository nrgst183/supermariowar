#include "uimenu.h"

#include "GameValues.h" // UI_Menu::SendInput
#include "GSGameplay.h"
#include "uicontrol.h"
#include "ResourceManager.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"

#include <algorithm>
#include <cassert>

namespace {
constexpr MenuCodeEnum navDir2menuCode(MenuNavDirection direction)
{
    switch (direction) {
        case MenuNavDirection::Up: return MenuCodeEnum::MENU_CODE_NEIGHBOR_UP;
        case MenuNavDirection::Down: return MenuCodeEnum::MENU_CODE_NEIGHBOR_DOWN;
        case MenuNavDirection::Left: return MenuCodeEnum::MENU_CODE_NEIGHBOR_LEFT;
        case MenuNavDirection::Right: return MenuCodeEnum::MENU_CODE_NEIGHBOR_RIGHT;
    }
    assert(false);
    return MenuCodeEnum::MENU_CODE_NONE;
}

// A held direction (a key, the d-pad, the stick) goes on through a menu, faster the longer it's held:
// after a moment about 10 steps a second, after a second about 20, after two every frame (a long map
// list). Its repeats are presses for everything in the menu (the lists, the value fields, the map
// field: they act on presses), counted from how long each player's directions have been down
constexpr int REPEAT_DELAY = 20;
int g_held[4][4] = {};

bool repeats(int held)
{
    if (held <= REPEAT_DELAY)
        return false;
    const int t = held - REPEAT_DELAY;
    const int every = t < 60 ? 6 : t < 125 ? 3 : 1;
    return t % every == 0;
}

void countHeld(CPlayerInput& input)
{
    for (int p = 0; p < 4; p++) {
        COutputControl& keys = input.outputControls[p];
        CKeyState* dirs[4] = {&keys.menu_up, &keys.menu_down, &keys.menu_left, &keys.menu_right};
        for (int d = 0; d < 4; d++) {
            g_held[p][d] = dirs[d]->fDown ? g_held[p][d] + 1 : 0;
            if (repeats(g_held[p][d]))
                dirs[d]->fPressed = true;
        }
    }
}
} // namespace

void UI_Menu::AddControl(UI_Control* control, UI_Control* up, UI_Control* down, UI_Control* left, UI_Control* right)
{
    control->setParent(this);
    control->setNeighbor(MenuNavDirection::Up, up);
    control->setNeighbor(MenuNavDirection::Down, down);
    control->setNeighbor(MenuNavDirection::Left, left);
    control->setNeighbor(MenuNavDirection::Right, right);
    controls.emplace_back(control);
}

void (*UI_Menu::drawHints)(const UI_Menu& menu) = nullptr;
void (*UI_Menu::drawTitleBar)() = nullptr;

namespace {
// The bar under the title: the game's look, or the plain field image
class MI_TitleBarBack : public UI_Control {
public:
    MI_TitleBarBack() : UI_Control(0, 0) {}
    void Draw() override
    {
        if (m_visible)
            UI_Menu::drawTitleBarBack();
    }
};
} // namespace

void UI_Menu::drawTitleBarBack()
{
    if (drawTitleBar) {
        drawTitleBar();
        return;
    }
    rm->menu_plain_field.draw(0, 0, {0, 0, 320, 32});
    rm->menu_plain_field.draw(320, 0, {192, 0, 320, 32});
}

UI_Control* UI_Menu::newTitleBarBack()
{
    return new MI_TitleBarBack();
}
void (*UI_Menu::drawFocus)(const UI_Control& control, bool modifying) = nullptr;

void UI_Menu::AddNonControl(UI_Control* control)
{
    control->setParent(this);
    controls.emplace_back(control);
    //(the title bar's: header text at the top of the screen, not a dialog's)
    if (auto* title = dynamic_cast<MI_HeaderText*>(control); title && !m_title && title->top() < 16)
        m_title = title;
}

void UI_Menu::ShiftAll(short dx, short dy)
{
    for (const std::unique_ptr<UI_Control>& control : controls)
        control->SetPosition(static_cast<short>(control->x() + dx), static_cast<short>(control->y() + dy));
}

void UI_Menu::AddBackdrop(UI_Control* control)
{
    control->setParent(this);
    controls.emplace(controls.begin(), control);
}

void UI_Menu::AddTitleBar(const std::string& title)
{
    AddNonControl(new MI_TitleBarBack());
    AddNonControl(new MI_HeaderText(title, 320, 5));
}

void UI_Menu::setInitialFocus(UI_Control* control)
{
    m_initialFocus = control;
    ResetMenu();
}

void UI_Menu::ResetMenu()
{
    if (m_currentFocus) {
        m_currentFocus->Modify(false);
        m_currentFocus->Select(false);
    }

    m_currentFocus = m_initialFocus;

    if (m_currentFocus)
        fModifyingItem = m_currentFocus->Select(true);

    eyeCandy.clean();
}

void UI_Menu::Update()
{
    for (const std::unique_ptr<UI_Control>& control : controls) {
        control->Update();
    }

    eyeCandy.cleanDeadObjects();
    eyeCandy.update();
}

void UI_Menu::Draw()
{
    for (const std::unique_ptr<UI_Control>& control : controls) {
        control->Draw();
    }

    if (drawFocus && m_currentFocus && m_currentFocus->IsVisible())
        drawFocus(*m_currentFocus, fModifyingItem);
    if (m_currentFocus && m_currentFocus->IsVisible())
        m_currentFocus->DrawOverlay();

    if (drawHints && m_title && m_hints)
        drawHints(*this);

    eyeCandy.draw();
}

bool UI_Menu::ChangingValue() const
{
    return fModifyingItem && m_currentFocus && m_currentFocus->ChangingValue();
}

MenuCodeEnum UI_Menu::SendInput(CPlayerInput* playerInput)
{
    countHeld(*playerInput);
    if (fModifyingItem) {
        MenuCodeEnum ret = m_currentFocus->SendInput(playerInput);

        if (MENU_CODE_UNSELECT_ITEM == ret) {
            fModifyingItem = false;

            /*if (current->IsAutoModify())
                    return cancelCode;*/

            return MENU_CODE_NONE;
        }

        return ret;
    }

    for (short iPlayer = 0; iPlayer < 4; iPlayer++) {
        // Only allow the controlling team to control the menu (if there is one)
        if (iControllingTeam != -1) {
            // Pay attention to other player's exit button pushes so we can exit when AI is controlling
            if (game_values.playercontrol[iPlayer] != 1 || (iControllingTeam != LookupTeamID(iPlayer) && (!fAllowExitButton || !playerInput->outputControls[iPlayer].menu_cancel.fPressed)))
                continue;
        }
        // Only let player 1 on the keyboard control the menu unless there is another controlling team
        else if (iPlayer != 0 && game_values.playerInput.inputControls[iPlayer] && game_values.playerInput.inputControls[iPlayer]->iDevice == DEVICE_KEYBOARD_ID) {
            continue;
        }

        if (playerInput->outputControls[iPlayer].menu_up.fPressed) {
            return MoveNextControl(MenuNavDirection::Up);
        }

        if (playerInput->outputControls[iPlayer].menu_down.fPressed) {
            return MoveNextControl(MenuNavDirection::Down);
        }

        if (playerInput->outputControls[iPlayer].menu_left.fPressed) {
            return MoveNextControl(MenuNavDirection::Left);
        }

        if (playerInput->outputControls[iPlayer].menu_right.fPressed) {
            return MoveNextControl(MenuNavDirection::Right);
        }

        if (playerInput->outputControls[iPlayer].menu_select.fPressed) {
            MenuCodeEnum ret = MENU_CODE_NONE;

            if (m_currentFocus) {
                ret = m_currentFocus->Modify(true);

                if (MENU_CODE_MODIFY_ACCEPTED == ret) {
                    fModifyingItem = true;
                    return MENU_CODE_NONE;
                }

                if (MENU_CODE_UNSELECT_ITEM == ret) {
                    fModifyingItem = false;
                    return MENU_CODE_NONE;
                }
            }

            return ret;
        }

        if (playerInput->outputControls[iPlayer].menu_cancel.fPressed) {
            return cancelCode;
        }
    }

    return MENU_CODE_NONE;
}

MenuCodeEnum UI_Menu::MoveNextControl(MenuNavDirection iDirection)
{
    if (!m_currentFocus)
        return MENU_CODE_NONE;

    UI_Control* neighbor = m_currentFocus->neighbor(iDirection);

    //(past the hidden ones, and the disabled ones: an option that can't be changed now, a button that
    //can't be pressed. A round of them all, back where it started: no move)
    for (size_t steps = 0; neighbor && (!neighbor->IsVisible() || neighbor->IsDisabled()); steps++) {
        if (neighbor == m_currentFocus || steps > controls.size()) {
            neighbor = nullptr;
            break;
        }
        neighbor = neighbor->neighbor(iDirection);
    }

    if (neighbor) {
        m_currentFocus->Select(false);
        m_currentFocus = neighbor;
        fModifyingItem = m_currentFocus->Select(true);
        return navDir2menuCode(iDirection);
    }

    return MENU_CODE_NONE;
}

void UI_Menu::RememberCurrent()
{
    m_savedCurrent = m_currentFocus;
}

void UI_Menu::RestoreCurrent()
{
    if (m_currentFocus) {
        m_currentFocus->Modify(false);
        m_currentFocus->Select(false);
    }

    m_currentFocus = m_savedCurrent;

    if (m_currentFocus)
        fModifyingItem = m_currentFocus->Select(true);

    eyeCandy.clean();
}

MenuCodeEnum UI_Menu::MouseClick(short iMouseX, short iMouseY)
{
    // Loop through all controls to see if one was clicked on
    UI_Control* pFound = nullptr;
    MenuCodeEnum code = MENU_CODE_NONE;
    for (const std::unique_ptr<UI_Control>& control : controls) {
        if (control->IsVisible()) {
            code = control->MouseClick(iMouseX, iMouseY);
            if (code != MENU_CODE_NONE) {
                pFound = control.get();
                break;
            }
        }
    }

    if (pFound) {
        // If we clicked the same control we have selected
        if (pFound != m_currentFocus) {
            if (fModifyingItem) {
                m_currentFocus->Modify(false);
                fModifyingItem = false;
            }

            m_currentFocus->Select(false);
            m_currentFocus = pFound;
            fModifyingItem = m_currentFocus->Select(true);

            if (!fModifyingItem) {
                fModifyingItem = m_currentFocus->Modify(true) == MENU_CODE_MODIFY_ACCEPTED;
            }
        } else {
            if (!fModifyingItem) {
                fModifyingItem = m_currentFocus->Modify(true) == MENU_CODE_MODIFY_ACCEPTED;
            }
        }
    } else {
        // If nothing was clicked, then stop modifying the current control
        if (fModifyingItem) {
            m_currentFocus->Modify(false);
            fModifyingItem = false;
        }
    }

    return code;
}

void UI_Menu::Refresh()
{
    for (const std::unique_ptr<UI_Control>& control : controls) {
        control->Refresh();
    }
}

UI_Control* UI_Menu::PointerTarget(short x, short y) const
{
    UI_Control* control = ControlAt(x, y);
    if (!control || !m_currentFocus || control == m_currentFocus)
        return control;
    //(the focus's group: the controls linked with it, either way)
    std::vector<const UI_Control*> group {m_currentFocus};
    for (size_t at = 0; at < group.size(); at++) {
        const UI_Control* c = group[at];
        for (const std::unique_ptr<UI_Control>& other : controls) {
            bool linked = false;
            for (int d = 0; d < 4 && !linked; d++) {
                const auto dir = static_cast<MenuNavDirection>(d);
                linked = c->neighbor(dir) == other.get() || other->neighbor(dir) == c;
            }
            if (linked && std::find(group.begin(), group.end(), other.get()) == group.end())
                group.push_back(other.get());
        }
    }
    return std::find(group.begin(), group.end(), control) != group.end() ? control : nullptr;
}

UI_Control* UI_Menu::ControlAt(short x, short y) const
{
    //(the last drawn is on top)
    for (auto it = controls.rbegin(); it != controls.rend(); ++it) {
        if ((*it)->IsVisible() && (*it)->Contains(x, y))
            return it->get();
    }
    return nullptr;
}

void UI_Menu::Focus(UI_Control* control)
{
    if (control == m_currentFocus)
        return;
    if (m_currentFocus) {
        if (fModifyingItem)
            m_currentFocus->Modify(false);
        m_currentFocus->Select(false);
    }
    m_currentFocus = control;
    fModifyingItem = m_currentFocus->Select(true);
}

// The focused control, being changed, with parts of its own (a list, a grid, a menu inside): the
// pointer moved (what 0), a left click (1), the wheel (2, dir). MENU_CODE_UNSELECT_ITEM: it didn't
// take it (it has no parts, or the pointer isn't on it)
MenuCodeEnum UI_Menu::PointerInside(short x, short y, int what, int dir)
{
    UI_Control* control = m_currentFocus;
    if (!control || !fModifyingItem || (what != 2 && !control->Contains(x, y)))
        return MENU_CODE_UNSELECT_ITEM;
    if (UI_Menu* inner = control->InnerMenu()) {
        const MenuCodeEnum code = what == 0 ? inner->PointerMove(x, y) : what == 1 ? inner->PointerClick(x, y, false) : inner->PointerWheel(dir);
        control->AfterInnerPointer();
        return code;
    }
    MenuCodeEnum code = MENU_CODE_NONE;
    if (what == 0)
        return control->PointerHoverInside(x, y) ? MENU_CODE_NONE : MENU_CODE_UNSELECT_ITEM;
    if (what == 1)
        return control->PointerClickInside(x, y, code) ? code : MENU_CODE_UNSELECT_ITEM;
    return control->PointerWheelInside(dir, code) ? code : MENU_CODE_UNSELECT_ITEM;
}

MenuCodeEnum UI_Menu::PointerMove(short x, short y)
{
    //(a control with parts of its own: the pointer to them)
    if (PointerInside(x, y, 0) != MENU_CODE_UNSELECT_ITEM)
        return MENU_CODE_NONE;
    //(not while a value is being changed: the pointer may cross other controls on the way to its arrows)
    if (fModifyingItem)
        return MENU_CODE_NONE;
    if (UI_Control* control = PointerTarget(x, y)) {
        Focus(control);
        PointerInside(x, y, 0);    //(focused, and at once being changed: the part under the pointer)
    }
    return MENU_CODE_NONE;
}

MenuCodeEnum UI_Menu::PointerClick(short x, short y, bool right)
{
    if (right) {
        //(changing a value: done with it; else back, as B)
        if (fModifyingItem && m_currentFocus) {
            m_currentFocus->Modify(false);
            fModifyingItem = false;
            return MENU_CODE_NONE;
        }
        return cancelCode;
    }

    UI_Control* control = PointerTarget(x, y);
    //(a control with parts of its own, being changed: the click to the part)
    if (control && control == m_currentFocus) {
        const MenuCodeEnum inside = PointerInside(x, y, 1);
        if (inside != MENU_CODE_UNSELECT_ITEM)
            return inside;
    }
    if (fModifyingItem && m_currentFocus) {
        //The control being changed: its own click (a value field's arrows); elsewhere: done changing
        if (control == m_currentFocus) {
            const MenuCodeEnum code = m_currentFocus->MouseClick(x, y);
            if (code != MENU_CODE_CLICKED && code != MENU_CODE_NONE)
                return code;
        }
        m_currentFocus->Modify(false);
        fModifyingItem = false;
        if (!control || control == m_currentFocus)
            return MENU_CODE_NONE;
    }
    if (!control)
        return MENU_CODE_NONE;

    Focus(control);
    if (fModifyingItem) {
        //(at once being changed, as the lists are: the click to the part under it)
        const MenuCodeEnum inside = PointerInside(x, y, 1);
        return inside == MENU_CODE_UNSELECT_ITEM ? MENU_CODE_NONE : inside;
    }
    MenuCodeEnum own = MENU_CODE_NONE;
    if (control->PointerPress(x, y, own))
        return own;
    //A value field: its next value; anything else: as A
    if (control->CanNudge())
        return control->Nudge(1);
    const MenuCodeEnum code = control->Modify(true);
    if (MENU_CODE_MODIFY_ACCEPTED == code) {
        fModifyingItem = true;
        const MenuCodeEnum inside = PointerInside(x, y, 1);
        return inside == MENU_CODE_UNSELECT_ITEM ? MENU_CODE_NONE : inside;
    }
    if (MENU_CODE_UNSELECT_ITEM == code)
        return MENU_CODE_NONE;
    return code;
}

MenuCodeEnum UI_Menu::PointerWheel(int dir)
{
    const MenuCodeEnum inside = PointerInside(0, 0, 2, dir);
    if (inside != MENU_CODE_UNSELECT_ITEM)
        return inside;
    if (!m_currentFocus || !m_currentFocus->CanNudge())
        return MENU_CODE_NONE;
    return m_currentFocus->Nudge(dir);
}

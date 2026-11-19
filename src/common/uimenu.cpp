#include "uimenu.h"

#include "GameValues.h" // UI_Menu::SendInput
#include "GSGameplay.h"
#include "uicontrol.h"
#include "ResourceManager.h"
#include "ui/MI_Image.h"
#include "ui/MI_Text.h"

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
void (*UI_Menu::drawFocus)(const UI_Control& control, bool modifying) = nullptr;

void UI_Menu::AddNonControl(UI_Control* control)
{
    control->setParent(this);
    controls.emplace_back(control);
    //(the title bar's: header text at the top of the screen, not a dialog's)
    if (auto* title = dynamic_cast<MI_HeaderText*>(control); title && !m_title && title->top() < 16)
        m_title = title;
}

void UI_Menu::AddTitleBar(const std::string& title)
{
    AddNonControl(new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0));
    AddNonControl(new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0));
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

    if (drawHints && m_title && m_hints)
        drawHints(*this);

    eyeCandy.draw();
}

MenuCodeEnum UI_Menu::SendInput(CPlayerInput* playerInput)
{
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

bool UI_Menu::NudgesFocus() const
{
    if (!m_currentFocus || !m_currentFocus->CanNudge())
        return false;
    //(a field with a real control beside it keeps left/right for moving, both ways)
    auto shortcutOrNothing = [&](const UI_Control* side) {
        return !side || (cancelCode != MENU_CODE_NONE && side->PressCode() == cancelCode);
    };
    return shortcutOrNothing(m_currentFocus->neighbor(MenuNavDirection::Left)) &&
           shortcutOrNothing(m_currentFocus->neighbor(MenuNavDirection::Right));
}

MenuCodeEnum UI_Menu::MoveNextControl(MenuNavDirection iDirection)
{
    if (!m_currentFocus)
        return MENU_CODE_NONE;

    //Left/right on a value field with nothing beside it: its next/previous value at once (no select,
    //change, select)
    if ((iDirection == MenuNavDirection::Left || iDirection == MenuNavDirection::Right) && NudgesFocus())
        return m_currentFocus->Nudge(iDirection == MenuNavDirection::Left ? -1 : 1);

    UI_Control* neighbor = m_currentFocus->neighbor(iDirection);

    while (neighbor && !neighbor->IsVisible()) {
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

MenuCodeEnum UI_Menu::PointerMove(short x, short y)
{
    //(not while a value is being changed: the pointer may cross other controls on the way to its arrows)
    if (fModifyingItem)
        return MENU_CODE_NONE;
    if (UI_Control* control = ControlAt(x, y))
        Focus(control);
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

    UI_Control* control = ControlAt(x, y);
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
    if (fModifyingItem)
        return MENU_CODE_NONE;
    MenuCodeEnum own = MENU_CODE_NONE;
    if (control->PointerPress(x, y, own))
        return own;
    //A value field: its next value; anything else: as A
    if (control->CanNudge())
        return control->Nudge(1);
    const MenuCodeEnum code = control->Modify(true);
    if (MENU_CODE_MODIFY_ACCEPTED == code) {
        fModifyingItem = true;
        return MENU_CODE_NONE;
    }
    if (MENU_CODE_UNSELECT_ITEM == code)
        return MENU_CODE_NONE;
    return code;
}

MenuCodeEnum UI_Menu::PointerWheel(int dir)
{
    if (!m_currentFocus || !m_currentFocus->CanNudge())
        return MENU_CODE_NONE;
    return m_currentFocus->Nudge(dir);
}

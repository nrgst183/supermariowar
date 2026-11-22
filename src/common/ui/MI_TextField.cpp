#include "MI_TextField.h"

#include "GameValues.h"
#include "gfx.h"
#include "ResourceManager.h"
#include "ui/MI_Image.h"

#include <array>


namespace {
constexpr std::array<short, 10> NUMBER_KEY_MAP {41, 33, 64, 35, 36, 37, 94, 38, 42, 40};

// The on-screen keyboard: four rows of ten characters, then Shift, Space, Del and Done (each over
// some of the ten columns)
constexpr const char* OSK_ROWS[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._:"};
constexpr short OSK_CHAR_ROWS = 4;
constexpr short OSK_ROWS_ALL = 5;
enum OskAction { OSK_SHIFT, OSK_SPACE, OSK_DEL, OSK_DONE };
constexpr short OSK_ACTION_FROM[] = {0, 2, 6, 8};    //(their first column; each to the next's)
constexpr const char* OSK_ACTION_NAME[] = {"Shift", "Space", "Del", "Done"};

OskAction actionAt(short col)
{
    return col < 2 ? OSK_SHIFT : col < 6 ? OSK_SPACE : col < 8 ? OSK_DEL : OSK_DONE;
}

constexpr short KEY_W = 40;
constexpr short KEY_H = 28;
constexpr short KEY_GAP = 4;
constexpr short PANEL_W = 10 * KEY_W + 9 * KEY_GAP + 24;
constexpr short PANEL_H = 44 + OSK_ROWS_ALL * (KEY_H + KEY_GAP) + 26;
constexpr short PANEL_X = (640 - PANEL_W) / 2;
constexpr short PANEL_Y = 480 - PANEL_H - 8;

void fillRect(short x, short y, short w, short h, Uint8 r, Uint8 g, Uint8 b)
{
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, r, g, b));
}
} // namespace

bool (*MI_TextField::useOnScreenKeyboard)() = nullptr;


MI_TextField::MI_TextField(gfxSprite * nspr, short x, short y, const char * name, short width, short indent)
    : UI_Control(x, y)
    , spr(nspr)
    , szName(name)
    , iWidth(width)
    , iIndent(indent)
    , iAdjustmentY(width > 256 ? 0 : 128)
    , iAllowedWidth(iWidth - iIndent - 24)
{
    miModifyCursor = std::make_unique<MI_Image>(nspr, m_pos.x + indent, m_pos.y + 4, 136, 64, 15, 24, 4, 1, 8);
    miModifyCursor->SetBlink(true, 20);
    miModifyCursor->setVisible(false);
}

void MI_TextField::SetTitle(std::string name)
{
    szName = std::move(name);
}

MenuCodeEnum MI_TextField::Modify(bool modify)
{
    if (fDisable)
        return MENU_CODE_UNSELECT_ITEM;

    if (MENU_CODE_NONE != mcControlSelectedCode)
        return mcControlSelectedCode;

    miModifyCursor->setVisible(modify);
    fModifying = modify;
    //(with a controller: the on-screen keyboard, on the first letter)
    m_osk = modify && useOnScreenKeyboard && useOnScreenKeyboard();
    if (m_osk) {
        m_oskRow = 1;
        m_oskCol = 0;
        m_oskShift = false;
    }
    return MENU_CODE_MODIFY_ACCEPTED;
}

bool MI_TextField::OskAllowed(char c) const
{
    return szDisallowedChars.find(c) == std::string::npos;
}

MenuCodeEnum MI_TextField::TypeChar(char c)
{
    if (!szOutValue || iNumChars >= iMaxChars - 1 || !OskAllowed(c))
        return MENU_CODE_NONE;
    szOutValue->insert(iCursorIndex++, 1, c);
    iNumChars++;
    UpdateCursor();
    return mcItemChangedCode;
}

MenuCodeEnum MI_TextField::Backspace()
{
    if (!szOutValue || iCursorIndex <= 0)
        return MENU_CODE_NONE;
    iCursorIndex--;
    iNumChars--;
    szOutValue->erase(iCursorIndex, 1);
    UpdateCursor();
    return mcItemChangedCode;
}

MenuCodeEnum MI_TextField::OskPress()
{
    if (m_oskRow < OSK_CHAR_ROWS) {
        char c = OSK_ROWS[m_oskRow][m_oskCol];
        if (m_oskShift && c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        return TypeChar(c);
    }
    switch (actionAt(m_oskCol)) {
        case OSK_SHIFT: m_oskShift = !m_oskShift; return MENU_CODE_NONE;
        case OSK_SPACE: return TypeChar(' ');
        case OSK_DEL: return Backspace();
        case OSK_DONE:
        default:
            m_osk = false;
            miModifyCursor->setVisible(false);
            fModifying = false;
            return MENU_CODE_UNSELECT_ITEM;
    }
}

// The keyboard's buttons: the d-pad moves over the keys, A presses one, B is done (the text kept)
MenuCodeEnum MI_TextField::OskInput(CPlayerInput* playerInput)
{
    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        const COutputControl& keys = playerInput->outputControls[iPlayer];
        if (keys.menu_cancel.fPressed) {
            m_osk = false;
            miModifyCursor->setVisible(false);
            fModifying = false;
            return MENU_CODE_UNSELECT_ITEM;
        }
        if (keys.menu_select.fPressed)
            return OskPress();
        if (keys.menu_up.fPressed)
            m_oskRow = static_cast<short>((m_oskRow + OSK_ROWS_ALL - 1) % OSK_ROWS_ALL);
        if (keys.menu_down.fPressed)
            m_oskRow = static_cast<short>((m_oskRow + 1) % OSK_ROWS_ALL);
        if (keys.menu_left.fPressed || keys.menu_right.fPressed) {
            const int dir = keys.menu_right.fPressed ? 1 : -1;
            if (m_oskRow < OSK_CHAR_ROWS) {
                m_oskCol = static_cast<short>((m_oskCol + 10 + dir) % 10);
            } else {
                //(the action keys: from one to the next)
                const int action = (static_cast<int>(actionAt(m_oskCol)) + 4 + dir) % 4;
                m_oskCol = OSK_ACTION_FROM[action];
            }
        }
    }
    return MENU_CODE_NONE;
}

void MI_TextField::DrawOverlay()
{
    if (!m_visible || !m_osk || !szOutValue)
        return;
    //The panel: a white rim, the status bar's blue; the text at the top
    fillRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 248, 248, 248);
    fillRect(PANEL_X + 2, PANEL_Y + 2, PANEL_W - 4, PANEL_H - 4, 0, 40, 104);
    fillRect(PANEL_X + 3, PANEL_Y + 3, PANEL_W - 6, PANEL_H - 6, 0, 95, 177);
    fillRect(PANEL_X + 12, PANEL_Y + 10, PANEL_W - 24, 26, 16, 28, 84);
    const std::string shown = *szOutValue + "_";
    rm->menu_font_large.drawChopLeft(PANEL_X + PANEL_W - 18, PANEL_Y + 12, PANEL_W - 36, shown.c_str());

    for (short row = 0; row < OSK_ROWS_ALL; row++) {
        const short y = PANEL_Y + 44 + row * (KEY_H + KEY_GAP);
        if (row < OSK_CHAR_ROWS) {
            for (short col = 0; col < 10; col++) {
                const short x = PANEL_X + 12 + col * (KEY_W + KEY_GAP);
                char c = OSK_ROWS[row][col];
                if (m_oskShift && c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
                const bool chosen = row == m_oskRow && col == m_oskCol;
                const bool allowed = OskAllowed(c);
                if (chosen)
                    fillRect(x - 2, y - 2, KEY_W + 4, KEY_H + 4, 248, 248, 248);
                if (allowed)
                    fillRect(x, y, KEY_W, KEY_H, chosen ? 248 : 0, chosen ? 200 : 64, chosen ? 40 : 136);
                else
                    fillRect(x, y, KEY_W, KEY_H, 56, 64, 88);
                const char text[2] = {c, 0};
                rm->menu_font_large.drawCentered(x + KEY_W / 2, y + 3, text);
            }
        } else {
            for (int action = 0; action < 4; action++) {
                const short from = OSK_ACTION_FROM[action];
                const short to = action < 3 ? OSK_ACTION_FROM[action + 1] : 10;
                const short x = PANEL_X + 12 + from * (KEY_W + KEY_GAP);
                const short w = static_cast<short>((to - from) * (KEY_W + KEY_GAP) - KEY_GAP);
                const bool chosen = row == m_oskRow && actionAt(m_oskCol) == action;
                const bool on = action == OSK_SHIFT && m_oskShift;
                if (chosen)
                    fillRect(x - 2, y - 2, w + 4, KEY_H + 4, 248, 248, 248);
                fillRect(x, y, w, KEY_H, chosen ? 248 : on ? 32 : 0, chosen ? 200 : on ? 144 : 64, chosen ? 40 : on ? 232 : 136);
                rm->menu_font_large.drawCentered(x + w / 2, y + 3, OSK_ACTION_NAME[action]);
            }
        }
    }
    rm->menu_font_small.drawCentered(320, PANEL_Y + PANEL_H - 20, "A types, B is done");
}

MenuCodeEnum MI_TextField::SendInput(CPlayerInput * playerInput)
{
    //(the on-screen keyboard: its keys first; a keyboard's typing works as well)
    if (m_osk) {
        const MenuCodeEnum code = OskInput(playerInput);
        if (code != MENU_CODE_NONE || !m_osk)
            return code;
        const SDL_Keycode key = playerInput->iPressedKey;
        if (key == SDLK_BACKSPACE)
            return Backspace();
        if (key >= 32 && key < 127)
            return TypeChar(static_cast<char>(key));
        return MENU_CODE_NONE;
    }

    const bool* keystate = SDL_GetKeyboardState(nullptr);

    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        // NOTE: copied from UI_Menu::SendInput
        // Only let player 1 on the keyboard control the menu
        if (iPlayer != 0
            && game_values.playerInput.inputControls[iPlayer]
            && game_values.playerInput.inputControls[iPlayer]->iDevice == DEVICE_KEYBOARD_ID) {
            continue;
        }

        if (playerInput->outputControls[iPlayer].menu_select.fPressed || playerInput->outputControls[iPlayer].menu_cancel.fPressed) {
            miModifyCursor->setVisible(false);

            fModifying = false;

            return MENU_CODE_UNSELECT_ITEM;
        }
    }

    if (!szOutValue || iNumChars >= iMaxChars)
        return MENU_CODE_NONE;

    // TODO: check string conversion
    //Watch for characters typed in including delete and backspace
    SDL_Keycode key = playerInput->iPressedKey;
    if ((key >= SDLK_A && key <= SDLK_Z) || key == SDLK_SPACE || (key >= SDLK_0 && key <= SDLK_9) || key == SDLK_EQUALS ||
        key == SDLK_MINUS || key == SDLK_GRAVE || (key >= SDLK_LEFTBRACKET && key <= SDLK_RIGHTBRACKET) ||
        key == SDLK_SEMICOLON || key == SDLK_APOSTROPHE || key == SDLK_COMMA || key == SDLK_PERIOD || key == SDLK_SLASH) {
        if (iNumChars < iMaxChars - 1) {
            //Take care of holding shift to shift the pressed key to another character
            if (keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT]) {
                if (key >= SDLK_A && key <= SDLK_Z) {
                    key -= 32;
                } else if (key >= SDLK_0 && key <= SDLK_9) {
                    key = NUMBER_KEY_MAP[key - 48];
                } else if (key == SDLK_MINUS) {
                    key = SDLK_UNDERSCORE;
                } else if (key == SDLK_EQUALS) {
                    key = SDLK_PLUS;
                } else if (key == SDLK_GRAVE) {
                    key = 126;
                } else if (key >= SDLK_LEFTBRACKET && key <= SDLK_RIGHTBRACKET) {
                    key += 32;
                } else if (key == SDLK_SEMICOLON) {
                    key = SDLK_COLON;
                } else if (key == SDLK_APOSTROPHE) {
                    key = SDLK_DBLAPOSTROPHE;
                } else if (key == SDLK_COMMA) {
                    key = SDLK_LESS;
                } else if (key == SDLK_PERIOD) {
                    key = SDLK_GREATER;
                } else if (key == SDLK_SLASH) {
                    key = SDLK_QUESTION;
                }
            }

            //Check to see if this is an allowed character for this field
            bool fAllowed = true;
            if (szDisallowedChars.find(static_cast<char>(key)) != std::string::npos) {
                fAllowed = false;
            }

            //If it is an allowed character, then add it to the field
            if (fAllowed) {
                szOutValue->insert(iCursorIndex++, 1, static_cast<char>(key));
                iNumChars++;

                UpdateCursor();
                return mcItemChangedCode;
            }
        }
    } else if (key == SDLK_BACKSPACE) {
        if (iCursorIndex > 0) {
            iCursorIndex--;
            iNumChars--;
            szOutValue->erase(iCursorIndex, 1);

            UpdateCursor();
            return mcItemChangedCode;
        }
    } else if (key == SDLK_DELETE) {
        if (iCursorIndex < iNumChars) {
            szOutValue->erase(iCursorIndex, 1);
            iNumChars--;

            UpdateCursor();
            return mcItemChangedCode;
        }
    } else if (key == SDLK_LEFT) {
        if (iCursorIndex > 0) {
            iCursorIndex--;
            UpdateCursor();
        }
    } else if (key == SDLK_RIGHT) {
        if (iCursorIndex < iNumChars) {
            iCursorIndex++;
            UpdateCursor();
        }
    }

    return MENU_CODE_NONE;
}


void MI_TextField::Update()
{
    miModifyCursor->Update();
}

void MI_TextField::Draw()
{
    if (!m_visible)
        return;

    spr->draw(m_pos.x, m_pos.y, {0, (fSelected ? 32 : 0) + iAdjustmentY, iIndent - 16, 32});
    spr->draw(m_pos.x + iIndent - 16, m_pos.y, {0, (fSelected ? 96 : 64), 32, 32});
    spr->draw(m_pos.x + iIndent + 16, m_pos.y, {528 - iWidth + iIndent, (fSelected ? 32 : 0) + iAdjustmentY, iWidth - iIndent - 16, 32});

    rm->menu_font_large.drawScrolling(m_pos.x + 16, m_pos.y + 5, iIndent - 8, szName.c_str(), fSelected);

    if (szOutValue) {
        if (iStringWidth <= iAllowedWidth || !fModifying) {
            rm->menu_font_large.drawChopRight(m_pos.x + iIndent + 8, m_pos.y + 5, iAllowedWidth, szOutValue->c_str());
        } else {
            rm->menu_font_large.drawChopLeft(m_pos.x + iWidth - 16, m_pos.y + 5, iAllowedWidth, szTempValue.c_str());
        }
    }

    miModifyCursor->Draw();
}

void MI_TextField::SetData(std::string& data, short maxchars)
{
    iMaxChars = maxchars;
    szOutValue = &data;
    iCursorIndex = static_cast<short>(szOutValue->size());
    iNumChars = iCursorIndex;

    szTempValue.clear();
    szTempValue.reserve(iMaxChars);

    UpdateCursor();
}

MenuCodeEnum MI_TextField::MouseClick(short iMouseX, short iMouseY)
{
    if (!szOutValue || fDisable)
        return MENU_CODE_NONE;

    //If we are modifying this control, see if we clicked on a next/prev button
    if (fModifying) {
        //Move cursor to index in string where clicked
        short iPixelCount = 0;
        char szChar[2];
        szChar[1] = 0;
        for (short iChar = 0; iChar < iNumChars; iChar++) {
            szChar[0] = (*szOutValue)[iChar];
            iPixelCount += rm->menu_font_large.getWidth(szChar);

            if (iPixelCount >= iMouseX - (m_pos.x + iIndent + 8)) {
                iCursorIndex = iChar;
                UpdateCursor();
                return MENU_CODE_NONE;
            }
        }
    }

    //Otherwise just check to see if we clicked on the whole control
    if (iMouseX >= m_pos.x && iMouseX < m_pos.x + iWidth && iMouseY >= m_pos.y && iMouseY < m_pos.y + 32) {
        iCursorIndex = static_cast<short>(szOutValue->size());
        UpdateCursor();
        return MENU_CODE_CLICKED;
    }

    //Otherwise this control wasn't clicked at all
    return MENU_CODE_NONE;
}

void MI_TextField::Refresh()
{
    //Look at destination string and update control based on that value
    if (!szOutValue)
        return;

    SetData(*szOutValue, iMaxChars);
}

void MI_TextField::UpdateCursor()
{
    if (!szOutValue)
        return;

    szTempValue = *szOutValue;
    szTempValue.resize(iCursorIndex);

    iStringWidth = rm->menu_font_large.getWidth(szTempValue.c_str());
    if (iStringWidth <= iAllowedWidth) {
        miModifyCursor->SetPosition(m_pos.x + iIndent + 10 + iStringWidth, m_pos.y + 4);
    } else {
        miModifyCursor->SetPosition(m_pos.x + iIndent + 10 + iAllowedWidth, m_pos.y + 4);
    }
}

void MI_TextField::SetDisallowedChars(const char * chars)
{
    szDisallowedChars = chars;
}

#include "InputPrompts.h"

#include "gfx.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

extern CResourceManager* rm;

namespace prompts {
namespace {

Device g_last = Device::Keyboard;
constexpr float DEADZONE = 0.22f;

constexpr SDL_Color WHITE {255, 255, 255, 255};
constexpr SDL_Color BLACK {0, 0, 0, 255};
constexpr SDL_Color DARK {44, 44, 60, 255};
constexpr SDL_Color GREY {150, 150, 170, 255};

float axis(Sint16 value)
{
    const float v = std::clamp(value / 32767.0f, -1.0f, 1.0f);
    return std::fabs(v) < DEADZONE ? 0.0f : v;
}

void text(int x, int y, const std::string& s)
{
    if (rm)
        rm->menu_font_small.draw(x, y, s);
}

int textWidth(const std::string& s)
{
    return rm ? rm->menu_font_small.getWidth(s) : static_cast<int>(s.size()) * 8;
}

// A rounded box (one pixel cut from the corners)
void roundBox(int x, int y, int w, int h, SDL_Color border, SDL_Color inside)
{
    fill(x + 1, y, w - 2, h, border);
    fill(x, y + 1, w, h - 2, border);
    fill(x + 2, y + 1, w - 4, h - 2, inside);
    fill(x + 1, y + 2, w - 2, h - 4, inside);
}

} // namespace

Device lastDevice()
{
    return g_last;
}

void setLastDevice(Device device)
{
    g_last = device;
}

void note(const SDL_Event& event)
{
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
            g_last = Device::Keyboard;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (std::fabs(event.motion.xrel) + std::fabs(event.motion.yrel) > 2)
                g_last = Device::Keyboard;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            g_last = Device::Controller;
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(event.gaxis.value) > 16000)
                g_last = Device::Controller;
            break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            if (std::abs(event.jaxis.value) > 16000)
                g_last = Device::Controller;
            break;
        default:
            break;
    }
}

// ---- shapes

void fill(int x, int y, int w, int h, SDL_Color c)
{
    if (!blitdest || w <= 0 || h <= 0)
        return;
    const SDL_Rect rect {x, y, w, h};
    SDL_FillSurfaceRect(blitdest, &rect, SDL_MapSurfaceRGB(blitdest, c.r, c.g, c.b));
}

void frame(int x, int y, int w, int h, SDL_Color border, SDL_Color inside)
{
    fill(x, y, w, h, border);
    fill(x + 2, y + 2, w - 4, h - 4, inside);
}

void disc(int cx, int cy, int r, SDL_Color c)
{
    for (int dy = -r; dy <= r; dy++) {
        const int half = static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)) + 0.35f);
        fill(cx - half, cy + dy, 2 * half + 1, 1, c);
    }
}

void triangle(int x, int y, int w, int h, bool right, SDL_Color c)
{
    for (int row = 0; row < h; row++) {
        const int fromMiddle = std::abs(2 * row - (h - 1));
        const int len = std::max(1, w * (h - fromMiddle) / h);
        fill(right ? x : x + w - len, y + row, len, 1, c);
    }
}

void line(int x0, int y0, int x1, int y1, int thickness, SDL_Color c)
{
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int k = 0; k <= steps; k++) {
        const int x = x0 + (steps ? (x1 - x0) * k / steps : 0);
        const int y = y0 + (steps ? (y1 - y0) * k / steps : 0);
        fill(x - thickness / 2, y - thickness / 2, thickness, thickness, c);
    }
}

// ---- glyphs

int drawPad(int x, int y, Pad pad)
{
    switch (pad) {
        case Pad::A:
        case Pad::B:
        case Pad::X:
        case Pad::Y: {
            static const SDL_Color colors[] = {{56, 176, 64, 255}, {214, 56, 48, 255}, {48, 110, 224, 255}, {232, 184, 32, 255}};
            static const char* letters[] = {"A", "B", "X", "Y"};
            const int i = static_cast<int>(pad) - static_cast<int>(Pad::A);
            disc(x + 8, y + 8, 8, BLACK);
            disc(x + 8, y + 8, 7, colors[i]);
            text(x + 8 - textWidth(letters[i]) / 2, y + 1, letters[i]);
            return 17;
        }
        case Pad::LB:
        case Pad::RB:
        case Pad::LT:
        case Pad::RT: {
            static const char* names[] = {"LB", "RB", "LT", "RT"};
            const int i = static_cast<int>(pad) - static_cast<int>(Pad::LB);
            const bool trigger = pad == Pad::LT || pad == Pad::RT;
            const int w = textWidth(names[i]) + 8;
            roundBox(x, y + (trigger ? 0 : 2), w, trigger ? 16 : 13, BLACK, trigger ? SDL_Color {90, 90, 110, 255} : DARK);
            text(x + 4, y + (trigger ? 1 : 1), names[i]);
            return w + 1;
        }
        case Pad::Start:
        case Pad::Back: {
            roundBox(x, y + 2, 22, 12, BLACK, DARK);
            if (pad == Pad::Start) {
                for (int k = 0; k < 3; k++)
                    fill(x + 6, y + 5 + 3 * k, 10, 1, WHITE);
            } else {
                frame(x + 5, y + 5, 7, 6, WHITE, DARK);
                frame(x + 9, y + 7, 7, 6, WHITE, DARK);
            }
            return 23;
        }
        case Pad::Up:
        case Pad::Down:
        case Pad::Left:
        case Pad::Right:
        case Pad::DPad: {
            //A cross; the direction lit
            fill(x + 5, y, 6, 16, BLACK);
            fill(x, y + 5, 16, 6, BLACK);
            fill(x + 6, y + 1, 4, 14, GREY);
            fill(x + 1, y + 6, 14, 4, GREY);
            const SDL_Color lit {255, 214, 48, 255};
            if (pad == Pad::Up)
                fill(x + 5, y, 6, 7, lit);
            else if (pad == Pad::Down)
                fill(x + 5, y + 9, 6, 7, lit);
            else if (pad == Pad::Left)
                fill(x, y + 5, 7, 6, lit);
            else if (pad == Pad::Right)
                fill(x + 9, y + 5, 7, 6, lit);
            return 17;
        }
        case Pad::LStick:
        case Pad::RStick:
            disc(x + 8, y + 8, 8, BLACK);
            disc(x + 8, y + 8, 7, GREY);
            disc(x + 8, y + 8, 4, DARK);
            text(x + 8 - textWidth(pad == Pad::LStick ? "L" : "R") / 2, y + 1, pad == Pad::LStick ? "L" : "R");
            return 17;
    }
    return 0;
}

int drawKey(int x, int y, const std::string& label)
{
    const int w = std::max(16, textWidth(label) + 8);
    roundBox(x, y, w, 16, {200, 200, 215, 255}, {70, 70, 96, 255});
    fill(x + 2, y + 13, w - 4, 1, {40, 40, 56, 255});
    text(x + (w - textWidth(label)) / 2, y + 1, label);
    return w + 1;
}

int drawMouse(int x, int y, bool left, bool right)
{
    roundBox(x, y, 12, 16, BLACK, {220, 220, 230, 255});
    fill(x + 6, y + 1, 1, 6, BLACK);
    fill(x + 1, y + 7, 10, 1, BLACK);
    if (left)
        fill(x + 2, y + 2, 4, 5, {214, 56, 48, 255});
    if (right)
        fill(x + 7, y + 2, 4, 5, {214, 56, 48, 255});
    return 13;
}

int drawPrompt(int x, int y, Pad pad, const std::string& key, const std::string& label)
{
    int w = 0;
    if (g_last == Device::Controller)
        w = drawPad(x, y, pad);
    else if (key.empty())
        w = drawMouse(x, y, true, false);
    else if (key == "RMB")
        w = drawMouse(x, y, false, true);
    else
        w = drawKey(x, y, key);
    if (!label.empty()) {
        text(x + w + 3, y + 1, label);
        w += 3 + textWidth(label);
    }
    return w;
}

int promptWidth(Pad pad, const std::string& key, const std::string& label)
{
    int w = 0;
    if (g_last == Device::Controller) {
        switch (pad) {
            case Pad::LB: case Pad::RB: case Pad::LT: case Pad::RT: w = textWidth("LB") + 9; break;
            case Pad::Start: case Pad::Back: w = 23; break;
            default: w = 17; break;
        }
    } else if (key.empty() || key == "RMB") {
        w = 13;
    } else {
        w = std::max(16, textWidth(key) + 8) + 1;
    }
    return label.empty() ? w : w + 3 + textWidth(label);
}

// ---- the controllers

void Gamepads::open()
{
    close();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    for (int k = 0; ids && k < count; k++) {
        if (SDL_IsGamepad(ids[k])) {
            if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[k]))
                m_gamepads.push_back(pad);
        } else if (SDL_Joystick* joy = SDL_OpenJoystick(ids[k])) {
            m_joysticks.push_back(joy);
        }
    }
    SDL_free(ids);
}

void Gamepads::close()
{
    for (SDL_Gamepad* pad : m_gamepads)
        SDL_CloseGamepad(pad);
    for (SDL_Joystick* joy : m_joysticks)
        SDL_CloseJoystick(joy);
    m_gamepads.clear();
    m_joysticks.clear();
}

std::vector<PadState> Gamepads::read() const
{
    std::vector<PadState> pads;
    for (SDL_Gamepad* pad : m_gamepads) {
        PadState p;
        const std::array<SDL_GamepadButton, BUTTON_COUNT> map = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
            SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        for (int b = 0; b < BUTTON_COUNT; b++)
            p.buttons[static_cast<size_t>(b)] = SDL_GetGamepadButton(pad, map[static_cast<size_t>(b)]);
        p.lx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        p.ly = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        p.rx = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        p.ry = axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
        p.lt = std::max(0.0f, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
        p.rt = std::max(0.0f, axis(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
        pads.push_back(p);
    }
    //Without a mapping: the usual layout (buttons 0-7, the first hat, axes 0-3)
    for (SDL_Joystick* joy : m_joysticks) {
        PadState p;
        const int buttons = SDL_GetNumJoystickButtons(joy);
        for (int b = 0; b < 8 && b < buttons; b++)
            p.buttons[static_cast<size_t>(b)] = SDL_GetJoystickButton(joy, b);
        if (SDL_GetNumJoystickHats(joy) > 0) {
            const Uint8 hat = SDL_GetJoystickHat(joy, 0);
            p.buttons[UP] = hat & SDL_HAT_UP;
            p.buttons[DOWN] = hat & SDL_HAT_DOWN;
            p.buttons[LEFT] = hat & SDL_HAT_LEFT;
            p.buttons[RIGHT] = hat & SDL_HAT_RIGHT;
        }
        const int axes = SDL_GetNumJoystickAxes(joy);
        if (axes >= 2) {
            p.lx = axis(SDL_GetJoystickAxis(joy, 0));
            p.ly = axis(SDL_GetJoystickAxis(joy, 1));
        }
        if (axes >= 4) {
            p.rx = axis(SDL_GetJoystickAxis(joy, 2));
            p.ry = axis(SDL_GetJoystickAxis(joy, 3));
        }
        pads.push_back(p);
    }
    return pads;
}

PadState Gamepads::merged() const
{
    PadState m;
    auto stick = [](float& out, float v) {
        if (std::fabs(v) > std::fabs(out))
            out = v;
    };
    for (const PadState& p : read()) {
        for (size_t b = 0; b < m.buttons.size(); b++)
            m.buttons[b] = m.buttons[b] || p.buttons[b];
        stick(m.lx, p.lx);
        stick(m.ly, p.ly);
        stick(m.rx, p.rx);
        stick(m.ry, p.ry);
        m.lt = std::max(m.lt, p.lt);
        m.rt = std::max(m.rt, p.rt);
    }
    return m;
}

} // namespace prompts

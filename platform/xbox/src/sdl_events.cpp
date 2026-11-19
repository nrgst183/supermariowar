// Xbox SDL3 compatibility layer: events, controllers (XInput), keyboard/mouse stubs.
//
// The four controller ports are always reported as four joysticks, with the
// instance IDs 1-4 (the game uses ID 0 for the keyboard). A port without a
// controller just has no input, so players keep their port when a controller
// is unplugged and plugged back in.
//
// Joystick layout of an Xbox controller:
//   buttons  0 A, 1 B, 2 X, 3 Start, 4 Back, 5 Y, 6 Black, 7 White,
//            8 left trigger, 9 right trigger, 10 left stick, 11 right stick
//   hat 0    the d-pad
//   axes     0/1 left stick (the d-pad also drives these while the stick is
//            centered, so the default stick bindings work with the d-pad),
//            2/3 right stick

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdlib>
#include <cstring>
#include <deque>

struct SDL_Joystick {
    int port = 0;
    bool open = false;
};

namespace {
constexpr int k_ports = SDL_XBOX_PORT_COUNT;
constexpr int k_buttons = 12;
constexpr int k_axes = 4;
constexpr Uint8 k_analog_press = 40;    // analog face buttons and triggers
constexpr Uint8 k_analog_release = 24;
constexpr Uint16 k_vendor_microsoft = 0x045E;
constexpr Uint16 k_product_xbox_controller = 0x0202;

struct PortState {
    HANDLE handle = nullptr;
    bool connected = false;
    bool buttons[k_buttons] {};
    Uint8 hat = SDL_HAT_CENTERED;
    Sint16 axes[k_axes] {};
    XINPUT_GAMEPAD raw {};
};

SDL_Joystick g_joysticks[k_ports];
PortState g_ports[k_ports];
std::deque<SDL_Event> g_queue;
bool g_events_ready = false;
bool g_joystick_events = true;
bool g_polled_since_empty = false;
bool g_keyboard_state[SDL_SCANCODE_COUNT] {};

const char* const k_port_names[k_ports] = {
    "Xbox Controller (Port 1)",
    "Xbox Controller (Port 2)",
    "Xbox Controller (Port 3)",
    "Xbox Controller (Port 4)",
};

bool validId(SDL_JoystickID id)
{
    return id >= 1 && id <= k_ports;
}

int portOf(SDL_JoystickID id)
{
    return static_cast<int>(id) - 1;
}

Uint64 nowNs()
{
    return sdlx_ticks_ns();
}

void pushJoyButton(int port, int button, bool down)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = down ? SDL_EVENT_JOYSTICK_BUTTON_DOWN : SDL_EVENT_JOYSTICK_BUTTON_UP;
    event.jbutton.timestamp = nowNs();
    event.jbutton.which = static_cast<SDL_JoystickID>(port + 1);
    event.jbutton.button = static_cast<Uint8>(button);
    event.jbutton.down = down;
    sdlx_push_event(event);
}

void pushJoyHat(int port, Uint8 value)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    event.jhat.timestamp = nowNs();
    event.jhat.which = static_cast<SDL_JoystickID>(port + 1);
    event.jhat.hat = 0;
    event.jhat.value = value;
    sdlx_push_event(event);
}

void pushJoyAxis(int port, int axis, Sint16 value)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    event.jaxis.timestamp = nowNs();
    event.jaxis.which = static_cast<SDL_JoystickID>(port + 1);
    event.jaxis.axis = static_cast<Uint8>(axis);
    event.jaxis.value = value;
    sdlx_push_event(event);
}

Sint16 invertAxis(SHORT value)
{
    // XInput: up is positive; SDL: up is negative
    return static_cast<Sint16>(value == -32768 ? 32767 : -value);
}

// Turns the raw controller state into button/hat/axis states, sending events for changes
void updatePort(int port, const XINPUT_GAMEPAD* pad)
{
    PortState& state = g_ports[port];
    XINPUT_GAMEPAD empty;
    memset(&empty, 0, sizeof(empty));
    const XINPUT_GAMEPAD& p = pad ? *pad : empty;

    const auto analog = [&](int index, bool wasDown) {
        const Uint8 value = p.bAnalogButtons[index];
        return wasDown ? value > k_analog_release : value >= k_analog_press;
    };

    bool buttons[k_buttons];
    buttons[0] = analog(XINPUT_GAMEPAD_A, state.buttons[0]);
    buttons[1] = analog(XINPUT_GAMEPAD_B, state.buttons[1]);
    buttons[2] = analog(XINPUT_GAMEPAD_X, state.buttons[2]);
    buttons[3] = (p.wButtons & XINPUT_GAMEPAD_START) != 0;
    buttons[4] = (p.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    buttons[5] = analog(XINPUT_GAMEPAD_Y, state.buttons[5]);
    buttons[6] = analog(XINPUT_GAMEPAD_BLACK, state.buttons[6]);
    buttons[7] = analog(XINPUT_GAMEPAD_WHITE, state.buttons[7]);
    buttons[8] = analog(XINPUT_GAMEPAD_LEFT_TRIGGER, state.buttons[8]);
    buttons[9] = analog(XINPUT_GAMEPAD_RIGHT_TRIGGER, state.buttons[9]);
    buttons[10] = (p.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    buttons[11] = (p.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;

    Uint8 hat = SDL_HAT_CENTERED;
    if (p.wButtons & XINPUT_GAMEPAD_DPAD_UP) hat |= SDL_HAT_UP;
    if (p.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) hat |= SDL_HAT_DOWN;
    if (p.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) hat |= SDL_HAT_LEFT;
    if (p.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) hat |= SDL_HAT_RIGHT;

    Sint16 axes[k_axes];
    axes[0] = p.sThumbLX;
    axes[1] = invertAxis(p.sThumbLY);
    axes[2] = p.sThumbRX;
    axes[3] = invertAxis(p.sThumbRY);

    // The d-pad drives the left stick axes while the stick is centered
    constexpr int k_stick_center = 8000;
    if (hat != SDL_HAT_CENTERED) {
        if (axes[0] > -k_stick_center && axes[0] < k_stick_center) {
            if (hat & SDL_HAT_LEFT) axes[0] = -32768;
            if (hat & SDL_HAT_RIGHT) axes[0] = 32767;
        }
        if (axes[1] > -k_stick_center && axes[1] < k_stick_center) {
            if (hat & SDL_HAT_UP) axes[1] = -32768;
            if (hat & SDL_HAT_DOWN) axes[1] = 32767;
        }
    }

    // Quantize the sticks a bit, so tiny jitters don't flood the event queue
    for (Sint16& axis : axes)
        axis = static_cast<Sint16>(axis & ~0xFF);

    // L3 + R3 (both sticks pressed in) toggles the profiler overlay
    if (buttons[10] && buttons[11] && !(state.buttons[10] && state.buttons[11]))
        g_sdlx_overlay = !g_sdlx_overlay;

    for (int i = 0; i < k_buttons; i++) {
        if (buttons[i] != state.buttons[i]) {
            state.buttons[i] = buttons[i];
            if (g_joystick_events)
                pushJoyButton(port, i, buttons[i]);
        }
    }
    if (hat != state.hat) {
        state.hat = hat;
        if (g_joystick_events)
            pushJoyHat(port, hat);
    }
    for (int i = 0; i < k_axes; i++) {
        if (axes[i] != state.axes[i]) {
            state.axes[i] = axes[i];
            if (g_joystick_events)
                pushJoyAxis(port, i, axes[i]);
        }
    }
    state.raw = p;
}

void pollControllers()
{
    DWORD insertions = 0;
    DWORD removals = 0;
    if (XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &insertions, &removals)) {
        for (int port = 0; port < k_ports; port++) {
            PortState& state = g_ports[port];
            if ((removals & (1u << port)) && state.handle) {
                XInputClose(state.handle);
                state.handle = nullptr;
                state.connected = false;
                updatePort(port, nullptr);  // release everything that was held
                sdlx_log("[pad] Controller removed from port %d\n", port + 1);
            }
            if ((insertions & (1u << port)) && !state.handle) {
                state.handle = XInputOpen(XDEVICE_TYPE_GAMEPAD, port, XDEVICE_NO_SLOT, NULL);
                state.connected = state.handle != nullptr;
                sdlx_log("[pad] Controller inserted in port %d%s\n", port + 1, state.connected ? "" : " (open failed)");
            }
        }
    }

    for (int port = 0; port < k_ports; port++) {
        PortState& state = g_ports[port];
        if (!state.handle)
            continue;
        XINPUT_STATE input;
        if (XInputGetState(state.handle, &input) == ERROR_SUCCESS)
            updatePort(port, &input.Gamepad);
    }
}
} // namespace


void sdlx_events_init()
{
    if (g_events_ready)
        return;
    XInitDevices(0, NULL);
    for (int port = 0; port < k_ports; port++)
        g_joysticks[port].port = port;
    g_events_ready = true;
}

void sdlx_events_quit()
{
    for (PortState& state : g_ports) {
        if (state.handle)
            XInputClose(state.handle);
        state = PortState();
    }
    g_queue.clear();
}

void sdlx_push_event(const SDL_Event& event)
{
    // Keep the newest events if nobody reads the queue for a while
    if (g_queue.size() >= 512)
        g_queue.pop_front();
    g_queue.push_back(event);
}


/* ---- Events -------------------------------------------------------------- */

void SDL_PumpEvents(void)
{
    sdlx_mixer_dispatch_callbacks();
    if (g_events_ready)
        pollControllers();
}

bool SDL_PollEvent(SDL_Event* event)
{
    sdlx_set_fpu_precision();
    sdlx_mixer_dispatch_callbacks();

    // Read the controllers once per drain of the queue: the game calls this
    // in a loop until it returns false, every frame
    if (g_queue.empty() && !g_polled_since_empty) {
        if (g_events_ready)
            pollControllers();
        g_polled_since_empty = true;
    }

    if (g_queue.empty()) {
        g_polled_since_empty = false;
        return false;
    }

    if (event)
        *event = g_queue.front();
    g_queue.pop_front();
    return true;
}

bool SDL_WaitEventTimeout(SDL_Event* event, Sint32 timeoutMS)
{
    const Uint64 start = SDL_GetTicks();
    for (;;) {
        if (SDL_PollEvent(event))
            return true;
        if (timeoutMS >= 0 && SDL_GetTicks() - start >= static_cast<Uint64>(timeoutMS))
            return false;
        Sleep(5);
    }
}

bool SDL_WaitEvent(SDL_Event* event)
{
    return SDL_WaitEventTimeout(event, -1);
}

bool SDL_PushEvent(SDL_Event* event)
{
    if (!event)
        return SDL_SetError("Parameter 'event' is invalid");
    sdlx_push_event(*event);
    return true;
}

bool SDL_AddEventWatch(SDL_EventFilter filter, void* userdata)
{
    (void)filter;
    (void)userdata;
    return true;
}

void SDL_RemoveEventWatch(SDL_EventFilter filter, void* userdata)
{
    (void)filter;
    (void)userdata;
}


/* ---- Keyboard and mouse (none on a retail Xbox) --------------------------- */

const bool* SDL_GetKeyboardState(int* numkeys)
{
    if (numkeys)
        *numkeys = SDL_SCANCODE_COUNT;
    return g_keyboard_state;
}

// (no keyboard: the modifiers are only what SDL_SetModState says, as SDL's own)
static SDL_Keymod g_modState = SDL_KMOD_NONE;

SDL_Keymod SDL_GetModState(void)
{
    return g_modState;
}

void SDL_SetModState(SDL_Keymod modstate)
{
    g_modState = modstate;
}

const char* SDL_GetKeyName(SDL_Keycode key)
{
    static char single[2] = {0, 0};
    switch (key) {
        case SDLK_UNKNOWN: return "";
        case SDLK_RETURN: return "Return";
        case SDLK_ESCAPE: return "Escape";
        case SDLK_BACKSPACE: return "Backspace";
        case SDLK_TAB: return "Tab";
        case SDLK_SPACE: return "Space";
        case SDLK_DELETE: return "Delete";
        case SDLK_UP: return "Up";
        case SDLK_DOWN: return "Down";
        case SDLK_LEFT: return "Left";
        case SDLK_RIGHT: return "Right";
        case SDLK_LCTRL: return "Left Ctrl";
        case SDLK_RCTRL: return "Right Ctrl";
        case SDLK_LSHIFT: return "Left Shift";
        case SDLK_RSHIFT: return "Right Shift";
        case SDLK_LALT: return "Left Alt";
        case SDLK_RALT: return "Right Alt";
        case SDLK_INSERT: return "Insert";
        case SDLK_HOME: return "Home";
        case SDLK_END: return "End";
        case SDLK_PAGEUP: return "PageUp";
        case SDLK_PAGEDOWN: return "PageDown";
        default: break;
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        static char fkey[4];
        const unsigned n = static_cast<unsigned>(key - SDLK_F1) + 1;
        fkey[0] = 'F';
        fkey[1] = static_cast<char>(n >= 10 ? '1' : '0' + n);
        fkey[2] = n >= 10 ? static_cast<char>('0' + n - 10) : '\0';
        fkey[3] = '\0';
        return fkey;
    }
    if (key > 0x20 && key < 0x7F) {
        single[0] = static_cast<char>((key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key);
        return single;
    }
    return "";
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key, SDL_Keymod* modstate)
{
    if (modstate)
        *modstate = SDL_KMOD_NONE;
    if (key & SDLK_SCANCODE_MASK)
        return static_cast<SDL_Scancode>(key & ~SDLK_SCANCODE_MASK);
    return SDL_SCANCODE_UNKNOWN;
}

SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y)
{
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}


/* ---- Joysticks ----------------------------------------------------------- */

void SDL_SetJoystickEventsEnabled(bool enabled)
{
    g_joystick_events = enabled;
}

bool SDL_JoystickEventsEnabled(void)
{
    return g_joystick_events;
}

bool SDL_HasJoystick(void)
{
    return true;
}

SDL_JoystickID* SDL_GetJoysticks(int* count)
{
    auto* ids = static_cast<SDL_JoystickID*>(malloc(sizeof(SDL_JoystickID) * (k_ports + 1)));
    if (!ids) {
        if (count)
            *count = 0;
        SDL_SetError("Out of memory");
        return nullptr;
    }
    for (int port = 0; port < k_ports; port++)
        ids[port] = static_cast<SDL_JoystickID>(port + 1);
    ids[k_ports] = 0;
    if (count)
        *count = k_ports;
    return ids;
}

const char* SDL_GetJoystickNameForID(SDL_JoystickID instance_id)
{
    return validId(instance_id) ? k_port_names[portOf(instance_id)] : nullptr;
}

SDL_GUID SDL_GetJoystickGUIDForID(SDL_JoystickID instance_id)
{
    SDL_GUID guid;
    memset(&guid, 0, sizeof(guid));
    if (!validId(instance_id))
        return guid;
    // Like SDL's USB GUIDs: bus, vendor, product (identical for all ports, as
    // for identical controllers on a PC)
    guid.data[0] = 0x03;
    guid.data[4] = static_cast<Uint8>(k_vendor_microsoft & 0xFF);
    guid.data[5] = static_cast<Uint8>(k_vendor_microsoft >> 8);
    guid.data[8] = static_cast<Uint8>(k_product_xbox_controller & 0xFF);
    guid.data[9] = static_cast<Uint8>(k_product_xbox_controller >> 8);
    guid.data[14] = 'x';
    guid.data[15] = 'b';
    return guid;
}

Uint16 SDL_GetJoystickVendorForID(SDL_JoystickID instance_id)
{
    return validId(instance_id) ? k_vendor_microsoft : 0;
}

Uint16 SDL_GetJoystickProductForID(SDL_JoystickID instance_id)
{
    return validId(instance_id) ? k_product_xbox_controller : 0;
}

SDL_Joystick* SDL_OpenJoystick(SDL_JoystickID instance_id)
{
    if (!validId(instance_id)) {
        SDL_SetError("Invalid joystick instance ID %u", static_cast<unsigned>(instance_id));
        return nullptr;
    }
    SDL_Joystick* joystick = &g_joysticks[portOf(instance_id)];
    joystick->open = true;
    return joystick;
}

SDL_Joystick* SDL_GetJoystickFromID(SDL_JoystickID instance_id)
{
    if (!validId(instance_id))
        return nullptr;
    SDL_Joystick* joystick = &g_joysticks[portOf(instance_id)];
    return joystick->open ? joystick : nullptr;
}

void SDL_CloseJoystick(SDL_Joystick* joystick)
{
    if (joystick)
        joystick->open = false;
}

bool SDL_JoystickConnected(SDL_Joystick* joystick)
{
    return joystick != nullptr;
}

SDL_JoystickID SDL_GetJoystickID(SDL_Joystick* joystick)
{
    return joystick ? static_cast<SDL_JoystickID>(joystick->port + 1) : 0;
}

const char* SDL_GetJoystickName(SDL_Joystick* joystick)
{
    return joystick ? k_port_names[joystick->port] : nullptr;
}

SDL_GUID SDL_GetJoystickGUID(SDL_Joystick* joystick)
{
    return SDL_GetJoystickGUIDForID(SDL_GetJoystickID(joystick));
}

Uint16 SDL_GetJoystickVendor(SDL_Joystick* joystick)
{
    return joystick ? k_vendor_microsoft : 0;
}

Uint16 SDL_GetJoystickProduct(SDL_Joystick* joystick)
{
    return joystick ? k_product_xbox_controller : 0;
}

int SDL_GetNumJoystickAxes(SDL_Joystick* joystick)
{
    return joystick ? k_axes : -1;
}

int SDL_GetNumJoystickHats(SDL_Joystick* joystick)
{
    return joystick ? 1 : -1;
}

int SDL_GetNumJoystickButtons(SDL_Joystick* joystick)
{
    return joystick ? k_buttons : -1;
}

Sint16 SDL_GetJoystickAxis(SDL_Joystick* joystick, int axis)
{
    if (!joystick || axis < 0 || axis >= k_axes)
        return 0;
    return g_ports[joystick->port].axes[axis];
}

Uint8 SDL_GetJoystickHat(SDL_Joystick* joystick, int hat)
{
    if (!joystick || hat != 0)
        return SDL_HAT_CENTERED;
    return g_ports[joystick->port].hat;
}

bool SDL_GetJoystickButton(SDL_Joystick* joystick, int button)
{
    if (!joystick || button < 0 || button >= k_buttons)
        return false;
    return g_ports[joystick->port].buttons[button];
}

bool SDL_SetJoystickLED(SDL_Joystick* joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    (void)joystick;
    (void)red;
    (void)green;
    (void)blue;
    return SDL_SetError("The controller has no LED");
}

bool SDL_RumbleJoystick(SDL_Joystick* joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms)
{
    (void)joystick;
    (void)low_frequency_rumble;
    (void)high_frequency_rumble;
    (void)duration_ms;
    return true;
}


/* ---- Gamepads (the same controllers, seen through the gamepad API) -------- */

bool SDL_IsGamepad(SDL_JoystickID instance_id)
{
    return validId(instance_id);
}

SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID instance_id)
{
    return reinterpret_cast<SDL_Gamepad*>(SDL_OpenJoystick(instance_id));
}

SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID instance_id)
{
    return reinterpret_cast<SDL_Gamepad*>(SDL_GetJoystickFromID(instance_id));
}

void SDL_CloseGamepad(SDL_Gamepad* gamepad)
{
    // Shares the joystick handle; the joystick side keeps it open
    (void)gamepad;
}

SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad* gamepad)
{
    return reinterpret_cast<SDL_Joystick*>(gamepad);
}

const char* SDL_GetGamepadName(SDL_Gamepad* gamepad)
{
    return SDL_GetJoystickName(SDL_GetGamepadJoystick(gamepad));
}

Uint16 SDL_GetGamepadVendor(SDL_Gamepad* gamepad)
{
    return SDL_GetJoystickVendor(SDL_GetGamepadJoystick(gamepad));
}

Uint16 SDL_GetGamepadProduct(SDL_Gamepad* gamepad)
{
    return SDL_GetJoystickProduct(SDL_GetGamepadJoystick(gamepad));
}

bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button)
{
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
    if (!joystick)
        return false;
    const PortState& state = g_ports[joystick->port];
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return state.buttons[0];
        case SDL_GAMEPAD_BUTTON_EAST: return state.buttons[1];
        case SDL_GAMEPAD_BUTTON_WEST: return state.buttons[2];
        case SDL_GAMEPAD_BUTTON_NORTH: return state.buttons[5];
        case SDL_GAMEPAD_BUTTON_START: return state.buttons[3];
        case SDL_GAMEPAD_BUTTON_BACK: return state.buttons[4];
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return state.buttons[6];  // Black
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return state.buttons[7];   // White
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return state.buttons[10];
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return state.buttons[11];
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return (state.hat & SDL_HAT_UP) != 0;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return (state.hat & SDL_HAT_DOWN) != 0;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return (state.hat & SDL_HAT_LEFT) != 0;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return (state.hat & SDL_HAT_RIGHT) != 0;
        default: return false;
    }
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis)
{
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
    if (!joystick)
        return 0;
    const PortState& state = g_ports[joystick->port];
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: return state.raw.sThumbLX;
        case SDL_GAMEPAD_AXIS_LEFTY: return invertAxis(state.raw.sThumbLY);
        case SDL_GAMEPAD_AXIS_RIGHTX: return state.raw.sThumbRX;
        case SDL_GAMEPAD_AXIS_RIGHTY: return invertAxis(state.raw.sThumbRY);
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return static_cast<Sint16>(state.raw.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] * 32767 / 255);
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return static_cast<Sint16>(state.raw.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] * 32767 / 255);
        default: return 0;
    }
}

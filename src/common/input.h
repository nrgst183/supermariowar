#ifndef INPUT_H
#define INPUT_H

#include <SDL3/SDL.h>

#include <stdint.h>

//This set of classes takes SDL input events, sees if a
//player configured button is pressed, and sets the output
//key structure if that button is down/pressed.
//This way you just need to call Update(SDL_Event) once a frame
//and then to use all the player input, just call
//CPlayerInput.outputControls.{key}.{presstype} to get the key state you want

constexpr int NUM_KEYS = 8;

constexpr SDL_JoystickID DEVICE_KEYBOARD_ID = 0;
constexpr SDL_GUID DEVICE_KEYBOARD_GUID = {};

constexpr int JOYSTICK_DEAD_ZONE = 16384;
constexpr int MOUSE_X_DEAD_ZONE = 0;
constexpr int MOUSE_Y_DEAD_ZONE = 5;

constexpr int MOUSE_UP = 323;
constexpr int MOUSE_DOWN = 324;
constexpr int MOUSE_LEFT = 325;
constexpr int MOUSE_RIGHT = 326;
constexpr int MOUSE_BUTTON_START = 327;

constexpr int JOY_STICK_1_UP = 0;
constexpr int JOY_STICK_1_DOWN = 1;
constexpr int JOY_STICK_1_LEFT = 2;
constexpr int JOY_STICK_1_RIGHT = 3;

constexpr int JOY_STICK_2_UP = 4;
constexpr int JOY_STICK_2_DOWN = 5;
constexpr int JOY_STICK_2_LEFT = 6;
constexpr int JOY_STICK_2_RIGHT = 7;

constexpr int JOY_HAT_UP = 8;
constexpr int JOY_HAT_DOWN = 9;
constexpr int JOY_HAT_LEFT = 10;
constexpr int JOY_HAT_RIGHT = 11;
constexpr int JOY_BUTTON_START = 12;

constexpr SDL_Keycode KEY_NONE = SDLK_UNKNOWN;

struct CKeyState {
    bool fDown;
    bool fPressed;
};

struct CInputControl {
    union {
        struct {
            SDL_Keycode menu_up, menu_down, menu_left, menu_right, menu_select, menu_cancel, menu_random, menu_scrollfast;
        };
        struct {
            SDL_Keycode game_left, game_right, game_jump, game_down, game_turbo, game_powerup, game_start, game_cancel;
        };
        SDL_Keycode keys[NUM_KEYS];
    };
};

struct COutputControl {
    union {
        struct {
            CKeyState menu_up, menu_down, menu_left, menu_right, menu_select, menu_cancel, menu_random, menu_scrollfast;
        };
        struct {
            CKeyState game_left, game_right, game_jump, game_down, game_turbo, game_powerup, game_start, game_cancel;
        };
        CKeyState keys[NUM_KEYS];
    };

    void copyFrom(const COutputControl& other) {
        for (uint8_t k = 0; k < NUM_KEYS; k++) {
            keys[k] = other.keys[k];
        }
    }

    bool operator == (const COutputControl& control) const {
        for (uint8_t k = 0; k < NUM_KEYS; k++)
            if (this->keys[k].fDown != control.keys[k].fDown
                || this->keys[k].fPressed != control.keys[k].fPressed)
                    return false;
        return true;
    }

    bool operator != (const COutputControl& control) const {
        return !(*this == control);
    }
};

struct CInputPlayerControl {
    SDL_JoystickID iDevice; // SDL joystick instance ID, DEVICE_KEYBOARD_ID for keyboard
    CInputControl inputGameControls[2]; //0 == game controls, 1 == menu controls
};

class CPlayerInput
{
    public:

        CPlayerInput();

        //Clears key pressed states
        void ClearPressedKeys(short iGameState);
        void ResetKeys();
        //Updates what commands the user is sending
        void Update(SDL_Event event, short iGameState);

        //Points to the input controls in the global inputConfiguration class
        CInputPlayerControl * inputControls[4];

        //Use these structures to see what input has been pressed
        COutputControl outputControls[4];

        SDL_Keycode iPressedKey;
};

#endif // INPUT_H

// SDL3 compatibility layer for the original Xbox (RXDK).
//
// Super Mario War is written against SDL3, SDL3_image and SDL3_mixer. The
// original Xbox has no SDL port, so this header declares the part of the SDL3
// API the game uses, with the same names, types and semantics. The
// implementation (platform/xbox/src) runs on the Xbox system libraries:
// Direct3D 8 for the display, XInput for the controllers, DirectSound for audio
// and the RXDK C runtime for files and time.
//
// This header must stay free of Xbox headers (xtl.h defines min/max, near/far
// and many Win32 names that would clash with the game code).
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDLCALL
#define SDL_DECLSPEC

#define SDL_MAJOR_VERSION 3
#define SDL_MINOR_VERSION 4
#define SDL_MICRO_VERSION 0
#define SDL_VERSIONNUM(major, minor, patch) ((major) * 1000000 + (minor) * 1000 + (patch))
#define SDL_VERSIONNUM_MAJOR(version) ((version) / 1000000)
#define SDL_VERSIONNUM_MINOR(version) (((version) / 1000) % 1000)
#define SDL_VERSIONNUM_MICRO(version) ((version) % 1000)
#define SDL_VERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION)

typedef int8_t Sint8;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
typedef uint64_t Uint64;

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

#define SDL_arraysize(array) (sizeof(array) / sizeof(array[0]))
#define SDL_min(x, y) (((x) < (y)) ? (x) : (y))
#define SDL_max(x, y) (((x) > (y)) ? (x) : (y))

/* ---- Basics ------------------------------------------------------------- */

typedef Uint32 SDL_InitFlags;
#define SDL_INIT_AUDIO    0x00000010u
#define SDL_INIT_VIDEO    0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_GAMEPAD  0x00002000u
#define SDL_INIT_EVENTS   0x00004000u

bool SDL_Init(SDL_InitFlags flags);
void SDL_Quit(void);
int SDL_GetVersion(void);

const char* SDL_GetError(void);
bool SDL_SetError(const char* fmt, ...);
bool SDL_ClearError(void);

void* SDL_malloc(size_t size);
void* SDL_calloc(size_t nmemb, size_t size);
void SDL_free(void* mem);

#define SDL_PROP_APP_METADATA_NAME_STRING       "SDL.app.metadata.name"
#define SDL_PROP_APP_METADATA_VERSION_STRING    "SDL.app.metadata.version"
#define SDL_PROP_APP_METADATA_IDENTIFIER_STRING "SDL.app.metadata.identifier"
#define SDL_PROP_APP_METADATA_CREATOR_STRING    "SDL.app.metadata.creator"
#define SDL_PROP_APP_METADATA_COPYRIGHT_STRING  "SDL.app.metadata.copyright"
#define SDL_PROP_APP_METADATA_URL_STRING        "SDL.app.metadata.url"
#define SDL_PROP_APP_METADATA_TYPE_STRING       "SDL.app.metadata.type"
bool SDL_SetAppMetadataProperty(const char* name, const char* value);

typedef Uint32 SDL_PropertiesID;
SDL_PropertiesID SDL_CreateProperties(void);
void SDL_DestroyProperties(SDL_PropertiesID props);
bool SDL_SetNumberProperty(SDL_PropertiesID props, const char* name, Sint64 value);
Sint64 SDL_GetNumberProperty(SDL_PropertiesID props, const char* name, Sint64 default_value);

/* ---- Time ---------------------------------------------------------------- */

Uint64 SDL_GetTicks(void);
Uint64 SDL_GetTicksNS(void);
void SDL_Delay(Uint32 ms);
void SDL_DelayNS(Uint64 ns);
void SDL_DelayPrecise(Uint64 ns);

/* ---- Filesystem ---------------------------------------------------------- */

const char* SDL_GetBasePath(void);
char* SDL_GetPrefPath(const char* org, const char* app);

/* ---- Rectangles and colors ----------------------------------------------- */

typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_FPoint { float x, y; } SDL_FPoint;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_FRect { float x, y, w, h; } SDL_FRect;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;

/* ---- Pixels -------------------------------------------------------------- */

typedef enum SDL_PixelFormat {
    SDL_PIXELFORMAT_UNKNOWN = 0,
    SDL_PIXELFORMAT_XRGB8888 = 0x16161804u,
    SDL_PIXELFORMAT_ARGB8888 = 0x16362004u,
    SDL_PIXELFORMAT_ABGR8888 = 0x16762004u,
    SDL_PIXELFORMAT_RGBA32 = SDL_PIXELFORMAT_ABGR8888,
    SDL_PIXELFORMAT_BGRA32 = SDL_PIXELFORMAT_ARGB8888
} SDL_PixelFormat;

#define SDL_BITSPERPIXEL(format) ((int)(((Uint32)(format) >> 8) & 0xFF))
#define SDL_BYTESPERPIXEL(format) ((int)(((Uint32)(format) >> 0) & 0xFF))
#define SDL_ALPHA_OPAQUE 255
#define SDL_ALPHA_TRANSPARENT 0

typedef struct SDL_PixelFormatDetails {
    SDL_PixelFormat format;
    Uint8 bits_per_pixel;
    Uint8 bytes_per_pixel;
    Uint8 padding[2];
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint8 Rbits, Gbits, Bbits, Abits;
    Uint8 Rshift, Gshift, Bshift, Ashift;
} SDL_PixelFormatDetails;

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color* colors;
    Uint32 version;
    int refcount;
} SDL_Palette;

const SDL_PixelFormatDetails* SDL_GetPixelFormatDetails(SDL_PixelFormat format);
Uint32 SDL_MapRGB(const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8* r, Uint8* g, Uint8* b);
void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormatDetails* format, const SDL_Palette* palette, Uint8* r, Uint8* g, Uint8* b, Uint8* a);

/* ---- Surfaces ------------------------------------------------------------ */

typedef Uint32 SDL_BlendMode;
#define SDL_BLENDMODE_NONE  0x00000000u
#define SDL_BLENDMODE_BLEND 0x00000001u
#define SDL_BLENDMODE_ADD   0x00000002u
#define SDL_BLENDMODE_MOD   0x00000004u

typedef enum SDL_ScaleMode {
    SDL_SCALEMODE_INVALID = -1,
    SDL_SCALEMODE_NEAREST = 0,
    SDL_SCALEMODE_LINEAR = 1,
    SDL_SCALEMODE_PIXELART = 2
} SDL_ScaleMode;

typedef Uint32 SDL_SurfaceFlags;
#define SDL_SURFACE_PREALLOCATED 0x00000001u
#define SDL_SURFACE_LOCK_NEEDED  0x00000002u
#define SDL_SURFACE_LOCKED       0x00000004u
#define SDL_MUSTLOCK(S) (((S)->flags & SDL_SURFACE_LOCK_NEEDED) == SDL_SURFACE_LOCK_NEEDED)

typedef struct SDL_Surface {
    SDL_SurfaceFlags flags;
    SDL_PixelFormat format;
    int w, h;
    int pitch;
    void* pixels;
    int refcount;
    void* reserved;

    /* Internal state of the Xbox implementation */
    bool has_colorkey;
    Uint32 colorkey;
    Uint8 alpha_mod;
    SDL_BlendMode blend_mode;
    SDL_Rect clip_rect;  /* blits and fills into the surface stay inside it */
} SDL_Surface;

SDL_Surface* SDL_CreateSurface(int width, int height, SDL_PixelFormat format);
SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, SDL_PixelFormat format, void* pixels, int pitch);
void SDL_DestroySurface(SDL_Surface* surface);
SDL_Surface* SDL_ConvertSurface(SDL_Surface* surface, SDL_PixelFormat format);
SDL_Surface* SDL_DuplicateSurface(SDL_Surface* surface);
SDL_Palette* SDL_GetSurfacePalette(SDL_Surface* surface);
bool SDL_LockSurface(SDL_Surface* surface);
void SDL_UnlockSurface(SDL_Surface* surface);
bool SDL_SetSurfaceRLE(SDL_Surface* surface, bool enabled);
bool SDL_SetSurfaceColorKey(SDL_Surface* surface, bool enabled, Uint32 key);
bool SDL_SurfaceHasColorKey(SDL_Surface* surface);
bool SDL_GetSurfaceColorKey(SDL_Surface* surface, Uint32* key);
bool SDL_SetSurfaceAlphaMod(SDL_Surface* surface, Uint8 alpha);
bool SDL_GetSurfaceAlphaMod(SDL_Surface* surface, Uint8* alpha);
bool SDL_SetSurfaceBlendMode(SDL_Surface* surface, SDL_BlendMode blendMode);
bool SDL_GetSurfaceBlendMode(SDL_Surface* surface, SDL_BlendMode* blendMode);
Uint32 SDL_MapSurfaceRGB(SDL_Surface* surface, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapSurfaceRGBA(SDL_Surface* surface, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
bool SDL_SetSurfaceClipRect(SDL_Surface* surface, const SDL_Rect* rect);
bool SDL_GetSurfaceClipRect(SDL_Surface* surface, SDL_Rect* rect);
bool SDL_FillSurfaceRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color);
bool SDL_BlitSurface(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, const SDL_Rect* dstrect);
bool SDL_BlitSurfaceScaled(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, const SDL_Rect* dstrect, SDL_ScaleMode scaleMode);

/* ---- Video --------------------------------------------------------------- */

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef Uint32 SDL_WindowID;
typedef Uint32 SDL_DisplayID;

typedef Uint64 SDL_WindowFlags;
#define SDL_WINDOW_FULLSCREEN 0x0000000000000001ull
#define SDL_WINDOW_OPENGL     0x0000000000000002ull
#define SDL_WINDOW_HIDDEN     0x0000000000000008ull
#define SDL_WINDOW_BORDERLESS 0x0000000000000010ull
#define SDL_WINDOW_RESIZABLE  0x0000000000000020ull

typedef struct SDL_DisplayMode {
    SDL_DisplayID displayID;
    SDL_PixelFormat format;
    int w, h;
    float pixel_density;
    float refresh_rate;
    int refresh_rate_numerator;
    int refresh_rate_denominator;
    void* internal;
} SDL_DisplayMode;

SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
void SDL_DestroyWindow(SDL_Window* window);
SDL_WindowID SDL_GetWindowID(SDL_Window* window);
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* window);
bool SDL_SetWindowTitle(SDL_Window* window, const char* title);
bool SDL_SetWindowFullscreen(SDL_Window* window, bool fullscreen);
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window);
const SDL_DisplayMode* SDL_GetCurrentDisplayMode(SDL_DisplayID displayID);
bool SDL_ShowCursor(void);
bool SDL_HideCursor(void);
bool SDL_CursorVisible(void);

typedef enum SDL_TextureAccess {
    SDL_TEXTUREACCESS_STATIC,
    SDL_TEXTUREACCESS_STREAMING,
    SDL_TEXTUREACCESS_TARGET
} SDL_TextureAccess;

typedef enum SDL_RendererLogicalPresentation {
    SDL_LOGICAL_PRESENTATION_DISABLED,
    SDL_LOGICAL_PRESENTATION_STRETCH,
    SDL_LOGICAL_PRESENTATION_LETTERBOX,
    SDL_LOGICAL_PRESENTATION_OVERSCAN,
    SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
} SDL_RendererLogicalPresentation;

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name);
void SDL_DestroyRenderer(SDL_Renderer* renderer);
const char* SDL_GetRendererName(SDL_Renderer* renderer);
bool SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
bool SDL_SetRenderLogicalPresentation(SDL_Renderer* renderer, int w, int h, SDL_RendererLogicalPresentation mode);
bool SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync);
bool SDL_GetRenderVSync(SDL_Renderer* renderer, int* vsync);
bool SDL_RenderClear(SDL_Renderer* renderer);
bool SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* srcrect, const SDL_FRect* dstrect);
bool SDL_RenderPresent(SDL_Renderer* renderer);
bool SDL_RenderCoordinatesFromWindow(SDL_Renderer* renderer, float window_x, float window_y, float* x, float* y);

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, SDL_PixelFormat format, SDL_TextureAccess access, int w, int h);
void SDL_DestroyTexture(SDL_Texture* texture);
bool SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch);
bool SDL_SetTextureScaleMode(SDL_Texture* texture, SDL_ScaleMode scaleMode);

typedef Uint32 SDL_MessageBoxFlags;
#define SDL_MESSAGEBOX_ERROR       0x00000010u
#define SDL_MESSAGEBOX_WARNING     0x00000020u
#define SDL_MESSAGEBOX_INFORMATION 0x00000040u
bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message, SDL_Window* window);

/* ---- Keyboard ------------------------------------------------------------ */

typedef Uint32 SDL_KeyboardID;
typedef Uint32 SDL_Keycode;
typedef Uint16 SDL_Keymod;

typedef enum SDL_Scancode {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_LCTRL = 224,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_LALT = 226,
    SDL_SCANCODE_RCTRL = 228,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_RALT = 230,
    SDL_SCANCODE_COUNT = 512
} SDL_Scancode;

#define SDLK_SCANCODE_MASK (1u << 30)
#define SDL_SCANCODE_TO_KEYCODE(X) ((SDL_Keycode)(X) | SDLK_SCANCODE_MASK)

#define SDLK_UNKNOWN      0x00000000u
#define SDLK_RETURN       0x0000000du
#define SDLK_ESCAPE       0x0000001bu
#define SDLK_BACKSPACE    0x00000008u
#define SDLK_TAB          0x00000009u
#define SDLK_SPACE        0x00000020u
#define SDLK_EXCLAIM      0x00000021u
#define SDLK_DBLAPOSTROPHE 0x00000022u
#define SDLK_HASH         0x00000023u
#define SDLK_DOLLAR       0x00000024u
#define SDLK_PERCENT      0x00000025u
#define SDLK_AMPERSAND    0x00000026u
#define SDLK_APOSTROPHE   0x00000027u
#define SDLK_LEFTPAREN    0x00000028u
#define SDLK_RIGHTPAREN   0x00000029u
#define SDLK_ASTERISK     0x0000002au
#define SDLK_PLUS         0x0000002bu
#define SDLK_COMMA        0x0000002cu
#define SDLK_MINUS        0x0000002du
#define SDLK_PERIOD       0x0000002eu
#define SDLK_SLASH        0x0000002fu
#define SDLK_0            0x00000030u
#define SDLK_1            0x00000031u
#define SDLK_2            0x00000032u
#define SDLK_3            0x00000033u
#define SDLK_4            0x00000034u
#define SDLK_5            0x00000035u
#define SDLK_6            0x00000036u
#define SDLK_7            0x00000037u
#define SDLK_8            0x00000038u
#define SDLK_9            0x00000039u
#define SDLK_COLON        0x0000003au
#define SDLK_SEMICOLON    0x0000003bu
#define SDLK_LESS         0x0000003cu
#define SDLK_EQUALS       0x0000003du
#define SDLK_GREATER      0x0000003eu
#define SDLK_QUESTION     0x0000003fu
#define SDLK_AT           0x00000040u
#define SDLK_LEFTBRACKET  0x0000005bu
#define SDLK_BACKSLASH    0x0000005cu
#define SDLK_RIGHTBRACKET 0x0000005du
#define SDLK_CARET        0x0000005eu
#define SDLK_UNDERSCORE   0x0000005fu
#define SDLK_GRAVE        0x00000060u
#define SDLK_A            0x00000061u
#define SDLK_B            0x00000062u
#define SDLK_C            0x00000063u
#define SDLK_D            0x00000064u
#define SDLK_E            0x00000065u
#define SDLK_F            0x00000066u
#define SDLK_G            0x00000067u
#define SDLK_H            0x00000068u
#define SDLK_I            0x00000069u
#define SDLK_J            0x0000006au
#define SDLK_K            0x0000006bu
#define SDLK_L            0x0000006cu
#define SDLK_M            0x0000006du
#define SDLK_N            0x0000006eu
#define SDLK_O            0x0000006fu
#define SDLK_P            0x00000070u
#define SDLK_Q            0x00000071u
#define SDLK_R            0x00000072u
#define SDLK_S            0x00000073u
#define SDLK_T            0x00000074u
#define SDLK_U            0x00000075u
#define SDLK_V            0x00000076u
#define SDLK_W            0x00000077u
#define SDLK_X            0x00000078u
#define SDLK_Y            0x00000079u
#define SDLK_Z            0x0000007au
#define SDLK_DELETE       0x0000007fu
#define SDLK_F1           0x4000003au
#define SDLK_F2           0x4000003bu
#define SDLK_F3           0x4000003cu
#define SDLK_F4           0x4000003du
#define SDLK_F5           0x4000003eu
#define SDLK_F6           0x4000003fu
#define SDLK_F7           0x40000040u
#define SDLK_F8           0x40000041u
#define SDLK_F9           0x40000042u
#define SDLK_F10          0x40000043u
#define SDLK_F11          0x40000044u
#define SDLK_F12          0x40000045u
#define SDLK_INSERT       0x40000049u
#define SDLK_HOME         0x4000004au
#define SDLK_PAGEUP       0x4000004bu
#define SDLK_END          0x4000004du
#define SDLK_PAGEDOWN     0x4000004eu
#define SDLK_RIGHT        0x4000004fu
#define SDLK_LEFT         0x40000050u
#define SDLK_DOWN         0x40000051u
#define SDLK_UP           0x40000052u
#define SDLK_LCTRL        0x400000e0u
#define SDLK_LSHIFT       0x400000e1u
#define SDLK_LALT         0x400000e2u
#define SDLK_RCTRL        0x400000e4u
#define SDLK_RSHIFT       0x400000e5u
#define SDLK_RALT         0x400000e6u

#define SDL_KMOD_NONE   0x0000u
#define SDL_KMOD_LSHIFT 0x0001u
#define SDL_KMOD_RSHIFT 0x0002u
#define SDL_KMOD_LCTRL  0x0040u
#define SDL_KMOD_RCTRL  0x0080u
#define SDL_KMOD_LALT   0x0100u
#define SDL_KMOD_RALT   0x0200u
#define SDL_KMOD_CTRL   (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)
#define SDL_KMOD_SHIFT  (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)
#define SDL_KMOD_ALT    (SDL_KMOD_LALT | SDL_KMOD_RALT)

const bool* SDL_GetKeyboardState(int* numkeys);
SDL_Keymod SDL_GetModState(void);
const char* SDL_GetKeyName(SDL_Keycode key);
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key, SDL_Keymod* modstate);

/* ---- Mouse --------------------------------------------------------------- */

typedef Uint32 SDL_MouseID;
typedef Uint32 SDL_MouseButtonFlags;
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5
#define SDL_BUTTON_MASK(X) (1u << ((X) - 1))
#define SDL_BUTTON_LMASK SDL_BUTTON_MASK(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)

typedef enum SDL_MouseWheelDirection {
    SDL_MOUSEWHEEL_NORMAL,
    SDL_MOUSEWHEEL_FLIPPED
} SDL_MouseWheelDirection;

SDL_MouseButtonFlags SDL_GetMouseState(float* x, float* y);

/* ---- Joysticks and gamepads ---------------------------------------------- */

typedef Uint32 SDL_JoystickID;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_GUID { Uint8 data[16]; } SDL_GUID;

#define SDL_HAT_CENTERED  0x00u
#define SDL_HAT_UP        0x01u
#define SDL_HAT_RIGHT     0x02u
#define SDL_HAT_DOWN      0x04u
#define SDL_HAT_LEFT      0x08u
#define SDL_HAT_RIGHTUP   (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP    (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN  (SDL_HAT_LEFT | SDL_HAT_DOWN)

#define SDL_JOYSTICK_AXIS_MAX 32767
#define SDL_JOYSTICK_AXIS_MIN -32768

void SDL_SetJoystickEventsEnabled(bool enabled);
bool SDL_JoystickEventsEnabled(void);
bool SDL_HasJoystick(void);
SDL_JoystickID* SDL_GetJoysticks(int* count);
const char* SDL_GetJoystickNameForID(SDL_JoystickID instance_id);
SDL_GUID SDL_GetJoystickGUIDForID(SDL_JoystickID instance_id);
Uint16 SDL_GetJoystickVendorForID(SDL_JoystickID instance_id);
Uint16 SDL_GetJoystickProductForID(SDL_JoystickID instance_id);
SDL_Joystick* SDL_OpenJoystick(SDL_JoystickID instance_id);
SDL_Joystick* SDL_GetJoystickFromID(SDL_JoystickID instance_id);
void SDL_CloseJoystick(SDL_Joystick* joystick);
bool SDL_JoystickConnected(SDL_Joystick* joystick);
SDL_JoystickID SDL_GetJoystickID(SDL_Joystick* joystick);
const char* SDL_GetJoystickName(SDL_Joystick* joystick);
SDL_GUID SDL_GetJoystickGUID(SDL_Joystick* joystick);
Uint16 SDL_GetJoystickVendor(SDL_Joystick* joystick);
Uint16 SDL_GetJoystickProduct(SDL_Joystick* joystick);
int SDL_GetNumJoystickAxes(SDL_Joystick* joystick);
int SDL_GetNumJoystickHats(SDL_Joystick* joystick);
int SDL_GetNumJoystickButtons(SDL_Joystick* joystick);
Sint16 SDL_GetJoystickAxis(SDL_Joystick* joystick, int axis);
Uint8 SDL_GetJoystickHat(SDL_Joystick* joystick, int hat);
bool SDL_GetJoystickButton(SDL_Joystick* joystick, int button);
bool SDL_SetJoystickLED(SDL_Joystick* joystick, Uint8 red, Uint8 green, Uint8 blue);
bool SDL_RumbleJoystick(SDL_Joystick* joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble, Uint32 duration_ms);

typedef enum SDL_GamepadButton {
    SDL_GAMEPAD_BUTTON_INVALID = -1,
    SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_COUNT = 26
} SDL_GamepadButton;

typedef enum SDL_GamepadAxis {
    SDL_GAMEPAD_AXIS_INVALID = -1,
    SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY,
    SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    SDL_GAMEPAD_AXIS_COUNT
} SDL_GamepadAxis;

bool SDL_IsGamepad(SDL_JoystickID instance_id);
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID instance_id);
SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID instance_id);
void SDL_CloseGamepad(SDL_Gamepad* gamepad);
const char* SDL_GetGamepadName(SDL_Gamepad* gamepad);
Uint16 SDL_GetGamepadVendor(SDL_Gamepad* gamepad);
Uint16 SDL_GetGamepadProduct(SDL_Gamepad* gamepad);
SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad* gamepad);
bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button);
Sint16 SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis);

/* ---- Events -------------------------------------------------------------- */

typedef enum SDL_EventType {
    SDL_EVENT_FIRST = 0,
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_WINDOW_RESIZED = 0x206,
    SDL_EVENT_KEY_DOWN = 0x300,
    SDL_EVENT_KEY_UP,
    SDL_EVENT_TEXT_EDITING,
    SDL_EVENT_TEXT_INPUT,
    SDL_EVENT_MOUSE_MOTION = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN,
    SDL_EVENT_MOUSE_BUTTON_UP,
    SDL_EVENT_MOUSE_WHEEL,
    SDL_EVENT_JOYSTICK_AXIS_MOTION = 0x600,
    SDL_EVENT_JOYSTICK_BALL_MOTION,
    SDL_EVENT_JOYSTICK_HAT_MOTION,
    SDL_EVENT_JOYSTICK_BUTTON_DOWN,
    SDL_EVENT_JOYSTICK_BUTTON_UP,
    SDL_EVENT_JOYSTICK_ADDED,
    SDL_EVENT_JOYSTICK_REMOVED,
    SDL_EVENT_GAMEPAD_AXIS_MOTION = 0x650,
    SDL_EVENT_GAMEPAD_BUTTON_DOWN,
    SDL_EVENT_GAMEPAD_BUTTON_UP,
    SDL_EVENT_GAMEPAD_ADDED,
    SDL_EVENT_GAMEPAD_REMOVED,
    SDL_EVENT_GAMEPAD_REMAPPED,
    SDL_EVENT_FINGER_DOWN = 0x700,
    SDL_EVENT_FINGER_UP,
    SDL_EVENT_FINGER_MOTION,
    SDL_EVENT_USER = 0x8000,
    SDL_EVENT_LAST = 0xFFFF
} SDL_EventType;

typedef struct SDL_CommonEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
} SDL_CommonEvent;

typedef struct SDL_QuitEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
} SDL_QuitEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    Sint32 data1;
    Sint32 data2;
} SDL_WindowEvent;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    SDL_KeyboardID which;
    SDL_Scancode scancode;
    SDL_Keycode key;
    SDL_Keymod mod;
    Uint16 raw;
    bool down;
    bool repeat;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    const char* text;
} SDL_TextInputEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    SDL_MouseID which;
    SDL_MouseButtonFlags state;
    float x, y;
    float xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    SDL_MouseID which;
    Uint8 button;
    bool down;
    Uint8 clicks;
    Uint8 padding;
    float x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    SDL_MouseID which;
    float x, y;
    SDL_MouseWheelDirection direction;
    float mouse_x, mouse_y;
    Sint32 integer_x, integer_y;
} SDL_MouseWheelEvent;

typedef struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyHatEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1, padding2;
} SDL_JoyHatEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    bool down;
    Uint8 padding1, padding2;
} SDL_JoyButtonEvent;

typedef struct SDL_JoyDeviceEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
} SDL_JoyDeviceEvent;

typedef struct SDL_GamepadAxisEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_GamepadAxisEvent;

typedef struct SDL_GamepadButtonEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    bool down;
    Uint8 padding1, padding2;
} SDL_GamepadButtonEvent;

typedef struct SDL_GamepadDeviceEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_JoystickID which;
} SDL_GamepadDeviceEvent;

typedef struct SDL_TouchFingerEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    Uint64 touchID;
    Uint64 fingerID;
    float x, y, dx, dy, pressure;
    SDL_WindowID windowID;
} SDL_TouchFingerEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Uint32 reserved;
    Uint64 timestamp;
    SDL_WindowID windowID;
    Sint32 code;
    void* data1;
    void* data2;
} SDL_UserEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_QuitEvent quit;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyDeviceEvent jdevice;
    SDL_GamepadAxisEvent gaxis;
    SDL_GamepadButtonEvent gbutton;
    SDL_GamepadDeviceEvent gdevice;
    SDL_TouchFingerEvent tfinger;
    SDL_UserEvent user;
    Uint8 padding[128];
} SDL_Event;

typedef bool (SDLCALL *SDL_EventFilter)(void* userdata, SDL_Event* event);

void SDL_PumpEvents(void);
bool SDL_PollEvent(SDL_Event* event);
bool SDL_WaitEvent(SDL_Event* event);
bool SDL_WaitEventTimeout(SDL_Event* event, Sint32 timeoutMS);
bool SDL_PushEvent(SDL_Event* event);
bool SDL_AddEventWatch(SDL_EventFilter filter, void* userdata);
void SDL_RemoveEventWatch(SDL_EventFilter filter, void* userdata);
bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer* renderer, SDL_Event* event);

/* ---- Audio (types used by SDL_mixer) -------------------------------------- */

typedef enum SDL_AudioFormat {
    SDL_AUDIO_UNKNOWN = 0x0000u,
    SDL_AUDIO_U8 = 0x0008u,
    SDL_AUDIO_S8 = 0x8008u,
    SDL_AUDIO_S16LE = 0x8010u,
    SDL_AUDIO_S16 = SDL_AUDIO_S16LE,
    SDL_AUDIO_F32LE = 0x8120u,
    SDL_AUDIO_F32 = SDL_AUDIO_F32LE
} SDL_AudioFormat;

typedef struct SDL_AudioSpec {
    SDL_AudioFormat format;
    int channels;
    int freq;
} SDL_AudioSpec;

typedef Uint32 SDL_AudioDeviceID;
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)

/* ---- Xbox specific -------------------------------------------------------- */

// Number of controller ports (joystick instance IDs 1..4; 0 is the keyboard in
// the game's input code)
#define SDL_XBOX_PORT_COUNT 4

// Leaves the game and goes back to the dashboard. Doesn't return.
void SDL_XboxReturnToDashboard(void);

// Memory for the game's screen surface that the GPU displays directly (no copy
// per frame): w*h 32-bit pixels, *pitch bytes per row. Create the screen
// surface on it with SDL_CreateSurfaceFrom, after creating the renderer, and
// present it with SDL_UpdateTexture(texture, NULL, pixels, pitch) as usual.
// NULL if it isn't available (then use a normal surface).
void* SDL_XboxGetScreenPixels(int w, int h, int* pitch);

// The folder for the game's cache (files it can make again): the utility
// drive Z:, which the system gives the title from the hard disk's three cache
// partitions (X:, Y: and Z: in the dashboard's terms; which one is the
// system's choice, and it can be wiped whenever another title needs it).
// "T:/cache/" if the utility drive can't be mounted. Ends with '/'.
const char* SDL_XboxGetCachePath(void);

// Game saves on the hard disk (U:), as the dashboard's memory screen shows
// them: each one is a folder, named `name` (ASCII; at most 127 characters).
// Opens the save `name` (making it first if `create`), and returns its folder
// (ending with '\\'; free it with SDL_free), or NULL.
char* SDL_XboxOpenSaveGame(const char* name, bool create);
bool SDL_XboxDeleteSaveGame(const char* name);
// Calls `callback` with the name of each of the title's saves
void SDL_XboxEnumerateSaveGames(void (*callback)(void* userdata, const char* name), void* userdata);

// The network (system link): XNet, started once, without its security, so
// that Xboxes and PCs exchange plain UDP. Starting it early gives its DHCP
// time while the menus are up. Returns 1 if it runs (ENet calls it too).
int SDL_XboxStartNetwork(void);

typedef enum SDL_XboxNetworkStatus {
    SDL_XBOX_NETWORK_OFF,         /* not started, or it couldn't start */
    SDL_XBOX_NETWORK_NO_CABLE,    /* no network cable (or nothing at its end) */
    SDL_XBOX_NETWORK_PENDING,     /* getting an address (DHCP) */
    SDL_XBOX_NETWORK_NO_ADDRESS,  /* no address came */
    SDL_XBOX_NETWORK_READY        /* *address: the console's ("192.168.1.20") */
} SDL_XboxNetworkStatus;
// `address` (at least 16 characters) gets the console's address when READY
SDL_XboxNetworkStatus SDL_XboxGetNetworkStatus(char* address, int size);
// The console's name for system link: the nickname last used on it (the nicknames the
// games share, XFindFirstNickname), in ASCII (other characters as '?'). 0 (and "") if it
// has none.
int SDL_XboxGetNickname(char* name, int size);

#ifdef __cplusplus
}
#endif

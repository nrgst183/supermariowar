// Xbox SDL3 compatibility layer: init, errors, properties, time, paths.

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {
char g_error[512] = "";

std::map<SDL_PropertiesID, std::map<std::string, Sint64>> g_properties;
SDL_PropertiesID g_next_properties = 1;

LARGE_INTEGER g_qpc_freq {};
LARGE_INTEGER g_qpc_start {};
bool g_time_ready = false;

void initTime()
{
    if (g_time_ready)
        return;
    QueryPerformanceFrequency(&g_qpc_freq);
    QueryPerformanceCounter(&g_qpc_start);
    if (g_qpc_freq.QuadPart <= 0)
        g_qpc_freq.QuadPart = 1;
    g_time_ready = true;
}

SDL_InitFlags g_initialized = 0;
} // namespace


void sdlx_log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}


void sdlx_set_fpu_precision()
{
    unsigned short cw = 0;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    // Precision control (bits 8-9): 10b = 53 bits
    const unsigned short wanted = static_cast<unsigned short>((cw & ~0x0300u) | 0x0200u);
    if (wanted != cw)
        __asm__ volatile("fldcw %0" : : "m"(wanted));
}


/* ---- Basics ------------------------------------------------------------- */

bool SDL_Init(SDL_InitFlags flags)
{
    initTime();
    if (!(g_initialized & (SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
        && (flags & (SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))) {
        sdlx_events_init();
    }
    g_initialized |= flags;
    return true;
}

void SDL_Quit(void)
{
    sdlx_video_quit();
    sdlx_events_quit();
    g_initialized = 0;
}

int SDL_GetVersion(void)
{
    return SDL_VERSION;
}

const char* SDL_GetError(void)
{
    return g_error;
}

bool SDL_SetError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_error, sizeof(g_error), fmt, args);
    va_end(args);
    return false;
}

bool SDL_ClearError(void)
{
    g_error[0] = '\0';
    return true;
}

void* SDL_malloc(size_t size) { return malloc(size); }
void* SDL_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
void SDL_free(void* mem) { free(mem); }

bool SDL_SetAppMetadataProperty(const char* name, const char* value)
{
    if (name && value && strcmp(name, SDL_PROP_APP_METADATA_NAME_STRING) == 0)
        sdlx_log("[xbox] %s\n", value);
    return true;
}

SDL_PropertiesID SDL_CreateProperties(void)
{
    const SDL_PropertiesID id = g_next_properties++;
    g_properties[id];
    return id;
}

void SDL_DestroyProperties(SDL_PropertiesID props)
{
    g_properties.erase(props);
}

bool SDL_SetNumberProperty(SDL_PropertiesID props, const char* name, Sint64 value)
{
    auto it = g_properties.find(props);
    if (it == g_properties.end() || !name)
        return SDL_SetError("Invalid properties");
    it->second[name] = value;
    return true;
}

Sint64 SDL_GetNumberProperty(SDL_PropertiesID props, const char* name, Sint64 default_value)
{
    auto it = g_properties.find(props);
    if (it == g_properties.end() || !name)
        return default_value;
    auto value = it->second.find(name);
    return value != it->second.end() ? value->second : default_value;
}


/* ---- Time ---------------------------------------------------------------- */

Uint64 sdlx_ticks_ns()
{
    initTime();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const Uint64 ticks = static_cast<Uint64>(now.QuadPart - g_qpc_start.QuadPart);
    const Uint64 freq = static_cast<Uint64>(g_qpc_freq.QuadPart);
    // Split to avoid overflowing 64 bits (ticks * 1e9)
    return (ticks / freq) * 1000000000ull + ((ticks % freq) * 1000000000ull) / freq;
}

Uint64 SDL_GetTicksNS(void)
{
    return sdlx_ticks_ns();
}

Uint64 SDL_GetTicks(void)
{
    return sdlx_ticks_ns() / 1000000ull;
}

void SDL_Delay(Uint32 ms)
{
    Sleep(ms);
}

void SDL_DelayNS(Uint64 ns)
{
    Sleep(static_cast<DWORD>((ns + 999999ull) / 1000000ull));
}

void SDL_DelayPrecise(Uint64 ns)
{
    const Uint64 deadline = sdlx_ticks_ns() + ns;
    // Sleep for the bulk (1 ms granularity), then yield until the deadline
    if (ns > 2000000ull)
        Sleep(static_cast<DWORD>((ns - 1500000ull) / 1000000ull));
    while (sdlx_ticks_ns() < deadline)
        Sleep(0);
}


/* ---- Filesystem ---------------------------------------------------------- */

const char* SDL_GetBasePath(void)
{
    // The title's own folder (the disc or the folder the .xbe was started from)
    return "D:/";
}

char* SDL_GetPrefPath(const char* org, const char* app)
{
    // T: is the title's persistent data folder on the hard disk
    // (E:\TDATA\<title id>), mapped by the XAPI startup
    (void)org;
    (void)app;
    const char path[] = "T:/";
    char* result = static_cast<char*>(malloc(sizeof(path)));
    if (result)
        memcpy(result, path, sizeof(path));
    return result;
}


/* ---- Message boxes ------------------------------------------------------- */

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message, SDL_Window* window)
{
    (void)flags;
    (void)window;
    // There's no message box on the Xbox: log it (visible in xbWatson / the
    // debugger output on a devkit)
    sdlx_log("[%s] %s\n", title ? title : "SDL", message ? message : "");
    return true;
}


/* ---- Xbox ---------------------------------------------------------------- */

void SDL_XboxReturnToDashboard(void)
{
    XLaunchNewImage(NULL, NULL);
    for (;;)
        Sleep(1000);
}

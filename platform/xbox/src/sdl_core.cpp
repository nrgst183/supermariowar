// Xbox SDL3 compatibility layer: init, errors, properties, time, paths.

#include <xtl.h>
#include <winsockx.h>

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


/* ---- Cache drive and game saves ----------------------------------------- */

const char* SDL_XboxGetCachePath(void)
{
    static const char* path = nullptr;
    if (!path) {
        // Maps Z: to the title's cache partition (keeping what it had when it
        // was this title's last time)
        if (XMountUtilityDrive(FALSE)) {
            path = "Z:/";
        } else {
            sdlx_log("[xbox] Couldn't mount the utility drive; the cache goes on T:\n");
            CreateDirectoryA("T:\\cache", NULL);
            path = "T:/cache/";
        }
    }
    return path;
}

namespace {
constexpr char SAVE_ROOT[] = "U:\\";

void toWide(const char* name, WCHAR (&out)[MAX_GAMENAME])
{
    size_t k = 0;
    for (; name[k] && k + 1 < MAX_GAMENAME; k++)
        out[k] = static_cast<unsigned char>(name[k]) < 0x80 ? static_cast<WCHAR>(name[k]) : L'?';
    out[k] = 0;
}
} // namespace

char* SDL_XboxOpenSaveGame(const char* name, bool create)
{
    if (!name || !*name)
        return nullptr;
    WCHAR wname[MAX_GAMENAME];
    toWide(name, wname);
    CHAR dir[MAX_PATH];
    const DWORD result = XCreateSaveGame(SAVE_ROOT, wname, create ? OPEN_ALWAYS : OPEN_EXISTING, 0, dir, MAX_PATH);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS) {
        if (create)
            sdlx_log("[xbox] Couldn't make the save \"%s\" (error %lu)\n", name, static_cast<unsigned long>(result));
        return nullptr;
    }
    const size_t length = strlen(dir);
    char* out = static_cast<char*>(malloc(length + 2));
    if (!out)
        return nullptr;
    memcpy(out, dir, length + 1);
    if (length == 0 || out[length - 1] != '\\') {
        out[length] = '\\';
        out[length + 1] = 0;
    }
    return out;
}

bool SDL_XboxDeleteSaveGame(const char* name)
{
    if (!name || !*name)
        return false;
    WCHAR wname[MAX_GAMENAME];
    toWide(name, wname);
    return XDeleteSaveGame(SAVE_ROOT, wname) == ERROR_SUCCESS;
}

void SDL_XboxEnumerateSaveGames(void (*callback)(void* userdata, const char* name), void* userdata)
{
    if (!callback)
        return;
    XGAME_FIND_DATA data;
    HANDLE find = XFindFirstSaveGame(SAVE_ROOT, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char name[MAX_GAMENAME];
        size_t k = 0;
        for (; data.szSaveGameName[k] && k + 1 < MAX_GAMENAME; k++)
            name[k] = data.szSaveGameName[k] < 0x80 ? static_cast<char>(data.szSaveGameName[k]) : '?';
        name[k] = 0;
        callback(userdata, name);
    } while (XFindNextSaveGame(find, &data));
    XFindClose(find);
}


/* ---- Network --------------------------------------------------------------- */

int SDL_XboxStartNetwork(void)
{
    static int started = -1;
    if (started < 0) {
        // Plain UDP (no XNet security): PCs on the network can play too
        XNetStartupParams params;
        memset(&params, 0, sizeof(params));
        params.cfgSizeOfStruct = sizeof(params);
        params.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
        const INT result = XNetStartup(&params);
        started = result == 0 ? 1 : 0;
        if (!started)
            sdlx_log("[xbox] The network didn't start (XNetStartup: %d)\n", static_cast<int>(result));
    }
    return started;
}

SDL_XboxNetworkStatus SDL_XboxGetNetworkStatus(char* address, int size)
{
    if (address && size > 0)
        address[0] = 0;
    if (!SDL_XboxStartNetwork())
        return SDL_XBOX_NETWORK_OFF;
    if (!(XNetGetEthernetLinkStatus() & XNET_ETHERNET_LINK_ACTIVE))
        return SDL_XBOX_NETWORK_NO_CABLE;

    XNADDR xna;
    memset(&xna, 0, sizeof(xna));
    const DWORD flags = XNetGetTitleXnAddr(&xna);
    if (flags == XNET_GET_XNADDR_PENDING)
        return SDL_XBOX_NETWORK_PENDING;
    if ((flags & XNET_GET_XNADDR_NONE) || !(flags & (XNET_GET_XNADDR_STATIC | XNET_GET_XNADDR_DHCP | XNET_GET_XNADDR_AUTO | XNET_GET_XNADDR_PPPOE)))
        return SDL_XBOX_NETWORK_NO_ADDRESS;

    if (address && size > 0) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&xna.ina.s_addr);
        snprintf(address, static_cast<size_t>(size), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    return SDL_XBOX_NETWORK_READY;
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

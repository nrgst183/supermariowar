// Xbox SDL3 compatibility layer: init, errors, properties, time, paths.

#include <xtl.h>
#include <winsockx.h>

#include "sdl_internal.h"

#include <cstdio>
#include <cstdlib>
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


namespace {
// The log's last lines, for the crash screen (no lock: a crash may come in the middle of a log)
constexpr int k_log_lines = 12;
constexpr int k_log_line_chars = 104;
char g_log_tail[k_log_lines][k_log_line_chars];
volatile LONG g_log_next = 0;

char g_crash_context[128] = "Starting";
Uint64 g_stage_lowest = ~0ull;    // (the lowest free memory since SDL_XboxTakeStageLowest)
} // namespace

void sdlx_log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);

    // A line each (a message's own new lines: spaces)
    if (!buf[0])
        return;
    const LONG slot = (InterlockedIncrement(&g_log_next) - 1) % k_log_lines;
    char* line = g_log_tail[slot];
    int n = 0;
    for (const char* c = buf; *c && n < k_log_line_chars - 1; c++) {
        if (*c == '\n' && !c[1])
            break;
        line[n++] = (*c == '\n' || *c == '\r' || *c == '\t') ? ' ' : *c;
    }
    line[n] = 0;
    sdlx_memory_sample();
}

int sdlx_log_tail(const char** lines, int count)
{
    const LONG next = g_log_next;
    const int have = static_cast<int>(SDL_min(next, static_cast<LONG>(SDL_min(count, k_log_lines))));
    for (int i = 0; i < have; i++)
        lines[i] = g_log_tail[(next - have + i) % k_log_lines];
    return have;
}

void SDL_XboxSetCrashContext(const char* context)
{
    snprintf(g_crash_context, sizeof(g_crash_context), "%s", context ? context : "");
}

const char* sdlx_crash_context()
{
    return g_crash_context;
}

void SDL_XboxLog(const char* line)
{
    sdlx_log("%s\n", line);
}

Uint64 g_sdlx_surface_bytes = 0;
Uint64 g_sdlx_sound_bytes = 0;

namespace {
struct MemoryWatch {
    Uint64 lowest = ~0ull;
    Uint64 surfacesAtLowest = 0;
    Uint64 surfacesPeak = 0;
    char where[128] = "";
    char logLine[104] = "";
};
MemoryWatch g_watch;
} // namespace

void sdlx_memory_sample()
{
    MEMORYSTATUS memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    if (g_sdlx_surface_bytes > g_watch.surfacesPeak)
        g_watch.surfacesPeak = g_sdlx_surface_bytes;
    if (memory.dwAvailPhys < g_stage_lowest)
        g_stage_lowest = memory.dwAvailPhys;
    if (memory.dwAvailPhys < g_watch.lowest) {
        g_watch.lowest = memory.dwAvailPhys;
        g_watch.surfacesAtLowest = g_sdlx_surface_bytes;
        snprintf(g_watch.where, sizeof(g_watch.where), "%s", g_crash_context);
        const LONG next = g_log_next;
        snprintf(g_watch.logLine, sizeof(g_watch.logLine), "%s", next > 0 ? g_log_tail[(next - 1) % k_log_lines] : "");
    }
}

namespace {
// The errors (a test's report): each kind once, with how many times
struct ErrorKind {
    char text[100];
    int count;
};
ErrorKind g_errors[40];
int g_error_kinds = 0;
int g_error_total = 0;
} // namespace

void sdlx_note_error(const char* text)
{
    //(not errors: a question answered no)
    if (strstr(text, "doesn't have a colorkey"))
        return;
    g_error_total++;
    for (int i = 0; i < g_error_kinds; i++) {
        if (strncmp(g_errors[i].text, text, sizeof(g_errors[i].text) - 1) == 0) {
            g_errors[i].count++;
            return;
        }
    }
    if (g_error_kinds < 40) {
        ErrorKind& kind = g_errors[g_error_kinds++];
        snprintf(kind.text, sizeof(kind.text), "%s", text);
        kind.count = 1;
        sdlx_log("[error] %s\n", text);
    }
}

int SDL_XboxErrorReport(char* text, int size)
{
    int n = snprintf(text, static_cast<size_t>(size), "Errors: %d (%d kinds)", g_error_total, g_error_kinds);
    for (int i = 0; i < g_error_kinds && n > 0 && n < size; i++)
        n += snprintf(text + n, static_cast<size_t>(size - n), "\n  %dx %s", g_errors[i].count, g_errors[i].text);
    return n;
}

Uint64 SDL_XboxTakeStageLowest(void)
{
    sdlx_memory_sample();
    const Uint64 lowest = g_stage_lowest;
    g_stage_lowest = ~0ull;
    return lowest;
}

Uint64 sdlx_memory_lowest()
{
    return g_watch.lowest;
}

int SDL_XboxMemoryReport(char* text, int size)
{
    sdlx_memory_sample();
    MEMORYSTATUS memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    const double mb = 1024.0 * 1024.0;
    return snprintf(text, static_cast<size_t>(size),
        "Lowest free: %.1f MB, while: %s\n"
        "(the log then: %s)\n"
        "Images then: %.1f MB (most ever: %.1f MB)\n"
        "Now: %.1f MB free of %.0f; images %.1f MB, sounds %.1f MB",
        static_cast<double>(g_watch.lowest) / mb, g_watch.where, g_watch.logLine,
        static_cast<double>(g_watch.surfacesAtLowest) / mb, static_cast<double>(g_watch.surfacesPeak) / mb,
        static_cast<double>(memory.dwAvailPhys) / mb, static_cast<double>(memory.dwTotalPhys) / mb,
        static_cast<double>(g_sdlx_surface_bytes) / mb, static_cast<double>(g_sdlx_sound_bytes) / mb);
}

Uint64 SDL_XboxImageBytes(void)
{
    return g_sdlx_surface_bytes;
}

Uint64 SDL_XboxFreeMemory(Uint64* total)
{
    MEMORYSTATUS memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    if (total)
        *total = memory.dwTotalPhys;
    return memory.dwAvailPhys;
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


void SDL_XboxPeekFpu(unsigned short* x87, unsigned int* mxcsr)
{
    unsigned short cw = 0;
    unsigned int csr = 0;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    *x87 = cw;
    *mxcsr = csr;
}


void SDL_XboxSettleFpu(unsigned short* x87Found, unsigned int* mxcsrFound)
{
    unsigned short cw = 0;
    unsigned int mxcsr = 0;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    if (x87Found)
        *x87Found = cw;
    if (mxcsrFound)
        *mxcsrFound = mxcsr;
    // x87: all exceptions masked (0x3F), 53-bit precision (bits 8-9: 10b), round to nearest (bits 10-11: 00)
    const unsigned short wantedCw = 0x027F;
    // SSE: all exceptions masked (0x1F80), round to nearest, no flush-to-zero (bit 15) or
    // denormals-are-zero (bit 6); the sticky flags (bits 0-5) cleared
    const unsigned int wantedMxcsr = 0x1F80;
    //(loaded even when they read right: xemu can keep rounding SSE floats toward zero after an x87
    //rounding switch (a double converted to an int) although MXCSR reads round-to-nearest; loading
    //MXCSR again makes it round as it says)
    __asm__ volatile("fldcw %0" : : "m"(wantedCw));
    __asm__ volatile("ldmxcsr %0" : : "m"(wantedMxcsr));
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
    sdlx_note_error(g_error);
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

namespace {
// A folder's files and folders deleted (--xbox-fresh-cache: a first boot's empty cache)
void emptyFolder(const std::string& dir)
{
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((dir + "*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        const std::string name = found.cFileName;
        if (name == "." || name == "..")
            continue;
        const std::string path = dir + name;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            emptyFolder(path + "\\");
            RemoveDirectoryA(path.c_str());
        } else {
            DeleteFileA(path.c_str());
        }
    } while (FindNextFileA(find, &found));
    FindClose(find);
}
} // namespace

namespace {
// One of the hard disk's cache partitions (X, Y, Z: partitions 3, 4, 5) linked to its letter by hand,
// and a folder of ours on it: when the title's own can't be mounted. It is a cache: another title
// given that partition may wipe it, and then things are just made again
const char* linkCachePartition()
{
    static char path[16];
    const char letters[3] = {'X', 'Y', 'Z'};
    for (int k = 0; k < 3; k++) {
        char link[16], device[40], folder[16];
        snprintf(link, sizeof(link), "\\??\\%c:", letters[k]);
        snprintf(device, sizeof(device), "\\Device\\Harddisk0\\Partition%d", 3 + k);
        OBJECT_STRING linkName, deviceName;
        RtlInitAnsiString(&linkName, link);
        RtlInitAnsiString(&deviceName, device);
        IoDeleteSymbolicLink(&linkName);    //(a letter left linked elsewhere)
        if (IoCreateSymbolicLink(&linkName, &deviceName) < 0)
            continue;
        snprintf(folder, sizeof(folder), "%c:\\smw", letters[k]);
        CreateDirectoryA(folder, NULL);
        if (GetFileAttributesA(folder) == INVALID_FILE_ATTRIBUTES)
            continue;
        snprintf(path, sizeof(path), "%c:/smw/", letters[k]);
        return path;
    }
    return nullptr;
}
} // namespace

const char* SDL_XboxGetCachePath(void)
{
    static const char* path = nullptr;
    if (!path) {
        // Maps Z: to the title's cache partition (keeping what it had when it was this title's last
        // time). Never the title's data folder (T:, E:\TDATA): what is unpacked or decoded goes on the
        // cache partitions (X, Y, Z) only
        if (XMountUtilityDrive(g_sdlx_fresh_cache ? TRUE : FALSE)) {
            path = "Z:/";
        } else if ((path = linkCachePartition()) != nullptr) {
            sdlx_log("[xbox] Couldn't mount the utility drive; the cache goes on %s\n", path);
        } else {
            //(none: nothing is kept; images are decoded and thumbnails made each time)
            sdlx_log("[xbox] No cache partition: nothing is cached\n");
            path = "Z:/";
        }
        if (g_sdlx_fresh_cache) {
            std::string dir = path;
            for (char& c : dir)
                c = c == '/' ? '\\' : c;
            emptyFolder(dir);
            sdlx_log("[xbox] The cache emptied (--xbox-fresh-cache)\n");
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

// The network library's own (libxnet, not in its headers): the dashboard's saved network settings,
// a sector of the hard disk's config area, and applying a set of them
extern "C" {
struct XNetConfigParams;
HANDLE WINAPI XNetOpenConfigVolume(void);
BOOL WINAPI XNetLoadConfigSector(HANDLE volume, DWORD sector, void* data, DWORD size);
VOID WINAPI XNetCloseConfigVolume(HANDLE volume);
INT WINAPI XNetConfig(const XNetConfigParams* params, DWORD flags);
}

namespace {
// The settings as the library reads them (XnInit): 0x1EC bytes; the address 0x14, the mask 0x18,
// the gateway 0x1C, the name servers 0x20 and 0x24 (an address 0: DHCP); "XBCP" at 0x1E8 or the
// library drops them all (DHCP)
constexpr DWORD CONFIG_SIZE = 0x1EC;
constexpr DWORD CONFIG_SIGNATURE = 0x58424350;    // "XBCP"
char g_networkSettings[96] = "";

DWORD dwordAt(const BYTE* data, DWORD offset)
{
    DWORD v;
    memcpy(&v, data + offset, sizeof(v));
    return v;
}

std::string addressText(DWORD a)
{
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&a);
    char text[20];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return text;
}

// The dashboard's static address, if the library didn't take it: read again, and given to the
// library as it is (its mark put right). What happened: g_networkSettings
// The config area's network sectors (8 to 15 of partition 0) as they are, to T:\netconfig.bin: what
// the dashboard saved, to see (pulled off over FTP: E:\TDATA\534D0001\netconfig.bin)
BYTE g_configRaw[8 * 512];

void dumpConfigSectors(HANDLE volume)
{
    BYTE* raw = g_configRaw;
    memset(raw, 0, sizeof(raw));
    for (int k = 0; k < 8; k++) {
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(8 + k) * 512;
        IO_STATUS_BLOCK io;
        memset(&io, 0, sizeof(io));
        NtReadFile(volume, NULL, NULL, NULL, &io, raw + k * 512, 512, &at);
    }
    FILE* f = fopen("T:\\netconfig.bin", "wb");
    if (f) {
        fwrite(raw, 1, sizeof(g_configRaw), f);
        fclose(f);
    }
}

void applyDashboardSettings()
{
    const HANDLE volume = XNetOpenConfigVolume();
    if (volume == INVALID_HANDLE_VALUE) {
        snprintf(g_networkSettings, sizeof(g_networkSettings), "the dashboard's settings can't be opened");
        sdlx_log("[xbox] network: %s\n", g_networkSettings);
        return;
    }
    dumpConfigSectors(volume);
    BYTE data[CONFIG_SIZE];
    memset(data, 0, sizeof(data));
    BOOL loaded = XNetLoadConfigSector(volume, 0, data, CONFIG_SIZE);
    XNetCloseConfigVolume(volume);
    //(the library's checks not met (a dashboard that writes the sector a little otherwise): its data as
    //it is, if the sector starts as theirs do)
    if (!loaded && dwordAt(g_configRaw, 0) == 0x79132568) {
        memcpy(data, g_configRaw + 0xC, CONFIG_SIZE);
        loaded = TRUE;
    }
    if (!loaded) {
        snprintf(g_networkSettings, sizeof(g_networkSettings), "no dashboard settings saved");
        sdlx_log("[xbox] network: %s\n", g_networkSettings);
        return;
    }
    DWORD address = dwordAt(data, 0x14);
    //The newer dashboards' settings: a second set, "XBV2" at 0x3C, its flags at 0x40 (0x04: a static
    //address, 0x08: static name servers; the MS dashboard's static setup saves 0x0C), its address, mask,
    //gateway and name servers from 0x48. The library knows only the first set (0x14..., left all 0 by
    //the dashboard): the static address saved in the second, it took for DHCP. Copied to the first, it
    //takes it (the name servers too if they're static)
    const DWORD tag = dwordAt(data, 0x3C), flags = dwordAt(data, 0x40), address2 = dwordAt(data, 0x48);
    const bool tagged = tag == 0x58425632 || tag == 0x32564258;
    if (tagged && (flags & 0x04) && address2 != 0 && address2 != address) {
        memmove(data + 0x14, data + 0x48, 3 * sizeof(DWORD));
        if (flags & 0x08)
            memmove(data + 0x20, data + 0x54, 2 * sizeof(DWORD));
        address = address2;
        sdlx_log("[xbox] network: the static address from the dashboard's newer settings\n");
    }
    const DWORD mask = dwordAt(data, 0x18), gateway = dwordAt(data, 0x1C);
    const DWORD signature = dwordAt(data, 0x1E8);
    sdlx_log("[xbox] network: the dashboard's settings: address %s mask %s gateway %s, mark %08lx\n", addressText(address).c_str(),
        addressText(mask).c_str(), addressText(gateway).c_str(), static_cast<unsigned long>(signature));
    if (address == 0) {
        snprintf(g_networkSettings, sizeof(g_networkSettings), "no static address in the saved settings (mark %08lx)",
            static_cast<unsigned long>(signature));
        return;
    }
    //(the library took them: its address static)
    XNADDR xna;
    memset(&xna, 0, sizeof(xna));
    if (XNetGetTitleXnAddr(&xna) & XNET_GET_XNADDR_STATIC) {
        snprintf(g_networkSettings, sizeof(g_networkSettings), "static %s, from the dashboard", addressText(address).c_str());
        return;
    }
    //(it didn't (a mark it doesn't know, or it couldn't read them as it started): given to it now)
    const DWORD mark = CONFIG_SIGNATURE;
    memcpy(data + 0x1E8, &mark, sizeof(mark));
    const INT result = XNetConfig(reinterpret_cast<const XNetConfigParams*>(data), 0);
    snprintf(g_networkSettings, sizeof(g_networkSettings), result == 0 ? "static %s, applied by the game" : "static %s couldn't be applied",
        addressText(address).c_str());
    sdlx_log("[xbox] network: %s (XNetConfig %d)\n", g_networkSettings, static_cast<int>(result));
}
} // namespace

const char* SDL_XboxGetNetworkSettingsNote(void)
{
    return g_networkSettings;
}

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
        else
            applyDashboardSettings();
    }
    return started;
}

/* ---- Threads ------------------------------------------------------------ */

struct SDL_Thread {
    HANDLE handle;
    SDL_ThreadFunction fn;
    void* data;
    int status;
};

static DWORD WINAPI sdlx_threadMain(LPVOID param)
{
    SDL_Thread* thread = static_cast<SDL_Thread*>(param);
    thread->status = thread->fn(thread->data);
    return 0;
}

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data)
{
    (void)name;
    auto* thread = static_cast<SDL_Thread*>(calloc(1, sizeof(SDL_Thread)));
    if (!thread) {
        SDL_SetError("Out of memory");
        return nullptr;
    }
    thread->fn = fn;
    thread->data = data;
    thread->handle = CreateThread(NULL, 256 * 1024, sdlx_threadMain, thread, 0, NULL);
    if (!thread->handle) {
        free(thread);
        SDL_SetError("CreateThread failed");
        return nullptr;
    }
    return thread;
}

void SDL_WaitThread(SDL_Thread* thread, int* status)
{
    if (!thread)
        return;
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    if (status)
        *status = thread->status;
    free(thread);
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
    return static_cast<SDL_ThreadID>(GetCurrentThreadId());
}

bool SDL_SetCurrentThreadPriority(SDL_ThreadPriority priority)
{
    int value = THREAD_PRIORITY_NORMAL;
    switch (priority) {
        case SDL_THREAD_PRIORITY_LOW: value = THREAD_PRIORITY_LOWEST; break;
        case SDL_THREAD_PRIORITY_NORMAL: value = THREAD_PRIORITY_NORMAL; break;
        case SDL_THREAD_PRIORITY_HIGH: value = THREAD_PRIORITY_HIGHEST; break;
        case SDL_THREAD_PRIORITY_TIME_CRITICAL: value = THREAD_PRIORITY_TIME_CRITICAL; break;
    }
    if (!SetThreadPriority(GetCurrentThread(), value))
        return SDL_SetError("SetThreadPriority failed");
    return true;
}

int SDL_XboxGetNickname(char* name, int size)
{
    if (!name || size <= 0)
        return 0;
    name[0] = 0;

    //The nicknames of every title (not only this one's), the last used first
    WCHAR wide[MAX_NICKNAME + 1];
    memset(wide, 0, sizeof(wide));
    const HANDLE find = XFindFirstNickname(FALSE, wide, MAX_NICKNAME);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    XFindClose(find);

    int length = 0;
    for (int i = 0; i < MAX_NICKNAME && wide[i] && length < size - 1; i++)
        name[length++] = wide[i] >= 32 && wide[i] < 127 ? static_cast<char>(wide[i]) : '?';
    name[length] = 0;
    return length > 0;
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


const char* SDL_XboxGetNetworkAddressKind(void)
{
    if (!SDL_XboxStartNetwork())
        return "";
    XNADDR xna;
    memset(&xna, 0, sizeof(xna));
    const DWORD flags = XNetGetTitleXnAddr(&xna);
    if (flags == XNET_GET_XNADDR_PENDING || (flags & XNET_GET_XNADDR_NONE))
        return "";
    if (flags & XNET_GET_XNADDR_STATIC)
        return "static";
    if (flags & XNET_GET_XNADDR_DHCP)
        return "DHCP";
    if (flags & XNET_GET_XNADDR_AUTO)
        return "automatic";
    if (flags & XNET_GET_XNADDR_PPPOE)
        return "PPPoE";
    return "";
}


/* ---- Message boxes ------------------------------------------------------- */

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message, SDL_Window* window)
{
    (void)window;
    // There's no message box on the Xbox: log it (visible in xbWatson / the
    // debugger output on a devkit). An error is the game's last words (its
    // main's catch): the crash screen, until a button, then the dashboard
    sdlx_log("[%s] %s\n", title ? title : "SDL", message ? message : "");
    if (flags & SDL_MESSAGEBOX_ERROR)
        sdlx_fatal(title ? title : "Error", message ? message : "");
    return true;
}


/* ---- Xbox ---------------------------------------------------------------- */

void sdlx_leave_to_dashboard()
{
    static LONG leaving = 0;
    if (InterlockedExchange(&leaving, 1) == 0) {
        sdlx_log("[xbox] Leaving for the dashboard\n");
        sdlx_audio_shutdown();
        sdlx_video_shutdown();
        if (SDL_XboxStartNetwork())
            XNetCleanup();
        XLaunchNewImage(NULL, NULL);
    }
    for (;;)
        Sleep(1000);
}

void SDL_XboxReturnToDashboard(void)
{
    sdlx_leave_to_dashboard();
}


/* ---- Files -------------------------------------------------------------- */

namespace {
// (the Xbox's file functions want backslashes)
std::string xboxPath(const char* path)
{
    std::string p = path;
    for (char& c : p) {
        if (c == '/')
            c = '\\';
    }
    return p;
}
} // namespace

bool sdlx_make_dir(const char* path)
{
    std::string p = xboxPath(path);
    while (!p.empty() && p.back() == '\\')
        p.pop_back();
    return CreateDirectoryA(p.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

Uint64 sdlx_dir_bytes(const char* path)
{
    std::string pattern = xboxPath(path);
    if (!pattern.empty() && pattern.back() != '\\')
        pattern += '\\';
    pattern += '*';
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    Uint64 total = 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            total += (static_cast<Uint64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return total;
}

/* ---- Load stats, and the disc's file sizes ------------------------------ */

SDL_XboxLoadStats g_sdlx_image_stats {};
SDL_XboxLoadStats g_sdlx_sound_stats {};

void SDL_XboxTakeLoadStats(SDL_XboxLoadStats* images, SDL_XboxLoadStats* sounds)
{
    if (images)
        *images = g_sdlx_image_stats;
    if (sounds)
        *sounds = g_sdlx_sound_stats;
    g_sdlx_image_stats = {};
    g_sdlx_sound_stats = {};
}

namespace {
// The disc's folders listed so far: each file's size, by its lower-case name
CRITICAL_SECTION g_sizesLock;
bool g_sizesLockReady = false;
std::map<std::string, std::map<std::string, long>> g_discSizes;

std::string lower(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}
} // namespace

long SDL_XboxDiscFileSize(const char* path)
{
    if (!path)
        return -1;
    const std::string full = xboxPath(path);
    const size_t slash = full.find_last_of('\\');
    if (slash == std::string::npos)
        return -1;
    const std::string dir = lower(full.substr(0, slash));
    const std::string name = lower(full.substr(slash + 1));

    //(the map preview's worker loads images too)
    if (!g_sizesLockReady) {
        InitializeCriticalSection(&g_sizesLock);
        g_sizesLockReady = true;
    }
    EnterCriticalSection(&g_sizesLock);
    auto folder = g_discSizes.find(dir);
    if (folder == g_discSizes.end()) {
        std::map<std::string, long> sizes;
        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA((full.substr(0, slash) + "\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    sizes[lower(data.cFileName)] = static_cast<long>(data.nFileSizeLow);
            } while (FindNextFileA(find, &data));
            FindClose(find);
        }
        folder = g_discSizes.emplace(dir, std::move(sizes)).first;
    }
    const auto file = folder->second.find(name);
    const long size = file == folder->second.end() ? -1 : file->second;
    LeaveCriticalSection(&g_sizesLock);
    return size;
}

/* ---- Whole files, in big blocks ----------------------------------------- */

void* sdlx_open(const char* path, bool write)
{
    if (!path)
        return nullptr;
    HANDLE file = CreateFileA(xboxPath(path).c_str(), write ? GENERIC_WRITE : GENERIC_READ, write ? 0 : FILE_SHARE_READ, NULL,
        write ? CREATE_ALWAYS : OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    return file == INVALID_HANDLE_VALUE ? nullptr : file;
}

Sint64 sdlx_size(void* file)
{
    if (!file)
        return -1;
    DWORD high = 0;
    const DWORD low = GetFileSize(static_cast<HANDLE>(file), &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        return -1;
    return static_cast<Sint64>((static_cast<Uint64>(high) << 32) | low);
}

bool sdlx_read(void* file, void* dst, Uint64 bytes)
{
    //(in pieces of at most 4 MB: one call per piece)
    Uint8* out = static_cast<Uint8*>(dst);
    while (bytes > 0) {
        const DWORD chunk = static_cast<DWORD>(bytes < (4u << 20) ? bytes : (4u << 20));
        DWORD read = 0;
        if (!file || !ReadFile(static_cast<HANDLE>(file), out, chunk, &read, NULL) || read == 0)
            return false;
        out += read;
        bytes -= read;
    }
    return true;
}

bool sdlx_write(void* file, const void* src, Uint64 bytes)
{
    const Uint8* in = static_cast<const Uint8*>(src);
    while (bytes > 0) {
        const DWORD chunk = static_cast<DWORD>(bytes < (4u << 20) ? bytes : (4u << 20));
        DWORD written = 0;
        if (!file || !WriteFile(static_cast<HANDLE>(file), in, chunk, &written, NULL) || written == 0)
            return false;
        in += written;
        bytes -= written;
    }
    return true;
}

void sdlx_close(void* file)
{
    if (file)
        CloseHandle(static_cast<HANDLE>(file));
}

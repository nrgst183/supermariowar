// The debug overlay (L3 + R3): where each frame's time goes, drawn over the picture in small text.
//
// - The frame rate, the longest frame and the frames shown late, the CPU clock and free memory
// - A chart of the last 240 frames: each a bar, its parts stacked in colors (the game's own parts:
//   simulation, drawing; presenting; the rest), with lines at one and two refreshes
// - A table of the game's sections (also the ones inside those parts) and this layer's (blits,
//   fills, presenting, the overlay itself): the average per frame and the longest, in the last second
// - The game's counters (ticks run, objects...)
// - The last slow frames: when, how long, and what took the time
//
// The game reports its sections with SDL_XboxProfileAdd (src/common/DebugProfile.h).

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern int g_sdlx_back_buffers;

namespace {
constexpr int k_screen_w = 640;
constexpr int k_screen_h = 480;
constexpr int k_sections = 16;
constexpr int k_counters = 8;
constexpr int k_history = 240;
constexpr int k_parts = 6;      // the chart's: up to 4 of the game's, presenting, the rest
constexpr int k_spikes = 5;
constexpr Uint64 k_spike_ns = 20000000ull;    // a frame this long (a refresh is 16.7 ms)

// Colors (X8R8G8B8)
constexpr Uint32 k_white = 0xFFF0F0F0u;
constexpr Uint32 k_gray = 0xFF909090u;
constexpr Uint32 k_dim = 0xFF505050u;
constexpr Uint32 k_green = 0xFF50E050u;
constexpr Uint32 k_yellow = 0xFFF0D040u;
constexpr Uint32 k_red = 0xFFF05040u;
constexpr Uint32 k_part_colors[k_parts] = {0xFF4090F0u, 0xFF50D070u, 0xFFE070E0u, 0xFFE0A040u, 0xFFB0B0B0u, 0xFF404858u};

struct Section {
    const char* name = nullptr;
    bool top = false;
    int part = -1;               // top: its part of the chart
    Uint64 frame_ns = 0;         // this frame
    Uint32 frame_calls = 0;
    Uint64 window_ns = 0;        // this second
    Uint64 window_worst_ns = 0;
    Uint32 window_calls = 0;
    double avg_ms = 0, worst_ms = 0;    // the last second's
    Uint32 calls = 0;
};

struct Counter {
    const char* name = nullptr;
    int value = 0;
};

// The layer's own timings, in cycles (g_sdlx_profile keeps adding up: these are where they were)
struct LayerMark {
    Uint64 blit = 0, fill = 0, copy = 0, swap = 0, mixer = 0;
    Uint32 blits = 0, pixels = 0;
};

struct Spike {
    double at_s = 0;             // since the console started the title
    double frame_ms = 0;
    double part_ms[k_parts] = {};
    const char* inner = nullptr; // the longest section inside a part
    double inner_ms = 0;
    int ticks = 0;
};

struct Overlay {
    Section sections[k_sections];
    Counter counters[k_counters];
    int parts_used = 0;          // the game's top sections, in the order they showed up
    const char* part_names[k_parts] = {};

    // The chart: each frame's parts, in 10 us units
    Uint16 history[k_history][k_parts] = {};
    int history_next = 0;

    // This second
    Uint64 window_start_ns = 0;
    Uint64 window_start_cycles = 0;
    Uint32 frames = 0;
    Uint64 worst_frame_ns = 0;
    Uint32 late = 0;
    LayerMark window_mark;
    LayerMark frame_mark;

    // The last second's, for the text
    double fps = 0, frame_ms = 0, worst_ms = 0, mhz = 733;
    Uint32 late_shown = 0;
    double blit_ms = 0, fill_ms = 0, copy_ms = 0, swap_ms = 0, audio_pct = 0;
    double blit_worst = 0, present_worst = 0;
    Uint32 blits_per_frame = 0, kpixels_per_frame = 0;
    double free_mb = 0, total_mb = 0;
    double overlay_ms = 0;
    Uint64 overlay_cycles = 0;

    Uint64 last_present_ns = 0;
    Uint64 last_present_cycles = 0;   // the last present's own time (copy, swap)
    double cycles_per_ns = 0.733;

    Spike spikes[k_spikes];
    int spike_count = 0;
};
Overlay g;

/* ---- Text ---------------------------------------------------------------- */

// 5x7 font: digits, letters (lower case shows as upper) and a few symbols
const Uint8* glyph(char c)
{
    static const Uint8 k_digits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    };
    static const Uint8 k_letters[26][7] = {
        {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    };
    static const Uint8 k_dot[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
    static const Uint8 k_colon[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0};
    static const Uint8 k_percent[7] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
    static const Uint8 k_slash[7] = {0, 0x01, 0x02, 0x04, 0x08, 0x10, 0};
    static const Uint8 k_dash[7] = {0, 0, 0, 0x1F, 0, 0, 0};
    static const Uint8 k_plus[7] = {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0};
    static const Uint8 k_open[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    static const Uint8 k_close[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
    static const Uint8 k_less[7] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02};
    static const Uint8 k_greater[7] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08};
    static const Uint8 k_equals[7] = {0, 0, 0x1F, 0, 0x1F, 0, 0};
    static const Uint8 k_comma[7] = {0, 0, 0, 0, 0x0C, 0x04, 0x08};
    static const Uint8 k_square[7] = {0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0};
    if (c >= '0' && c <= '9') return k_digits[c - '0'];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return k_letters[c - 'A'];
    switch (c) {
        case '.': return k_dot;
        case ':': return k_colon;
        case '%': return k_percent;
        case '/': return k_slash;
        case '-': return k_dash;
        case '+': return k_plus;
        case '(': return k_open;
        case ')': return k_close;
        case '<': return k_less;
        case '>': return k_greater;
        case '=': return k_equals;
        case ',': return k_comma;
        case '#': return k_square;
        default: return nullptr;
    }
}

constexpr int k_char_w = 6;
constexpr int k_line_h = 9;

struct Canvas {
    Uint8* pixels;
    int pitch;

    Uint32* row(int y) const { return reinterpret_cast<Uint32*>(pixels + static_cast<size_t>(y) * pitch); }

    void put(int x, int y, Uint32 color) const
    {
        if (x >= 0 && x < k_screen_w && y >= 0 && y < k_screen_h)
            row(y)[x] = color;
    }

    void fill(int x, int y, int w, int h, Uint32 color) const
    {
        for (int py = SDL_max(y, 0); py < SDL_min(y + h, k_screen_h); py++) {
            Uint32* r = row(py);
            for (int px = SDL_max(x, 0); px < SDL_min(x + w, k_screen_w); px++)
                r[px] = color;
        }
    }

    // A quarter as bright (the picture shows through, darkened)
    void darken(int x, int y, int w, int h) const
    {
        for (int py = SDL_max(y, 0); py < SDL_min(y + h, k_screen_h); py++) {
            Uint32* r = row(py);
            for (int px = SDL_max(x, 0); px < SDL_min(x + w, k_screen_w); px++)
                r[px] = 0xFF000000u | ((r[px] >> 2) & 0x003F3F3Fu);
        }
    }

    // Returns where the text ends
    int text(int x, int y, Uint32 color, const char* s) const
    {
        for (; *s; s++, x += k_char_w) {
            const Uint8* g = glyph(*s);
            if (!g)
                continue;
            for (int gy = 0; gy < 7; gy++) {
                for (int gx = 0; gx < 5; gx++) {
                    if (g[gy] & (0x10 >> gx))
                        put(x + gx, y + gy, color);
                }
            }
        }
        return x;
    }

    int textf(int x, int y, Uint32 color, const char* fmt, ...) const
    {
        char buffer[96];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        return text(x, y, color, buffer);
    }
};

Uint32 msColor(double ms, double warn, double bad)
{
    return ms >= bad ? k_red : ms >= warn ? k_yellow : k_white;
}

double toMs(Uint64 ns)
{
    return static_cast<double>(ns) / 1e6;
}

Uint64 cyclesToNs(Uint64 cycles)
{
    return g.cycles_per_ns > 0 ? static_cast<Uint64>(static_cast<double>(cycles) / g.cycles_per_ns) : 0;
}

LayerMark layerNow()
{
    LayerMark m;
    m.blit = g_sdlx_profile.blit;
    m.fill = g_sdlx_profile.fill;
    m.copy = g_sdlx_profile.copy;
    m.swap = g_sdlx_profile.swap;
    m.mixer = g_sdlx_profile.mixer;
    m.blits = g_sdlx_profile.blits;
    m.pixels = g_sdlx_profile.blit_pixels;
    return m;
}

// A frame's part, in the chart's units (10 us)
Uint16 toUnits(Uint64 ns)
{
    const Uint64 u = ns / 10000ull;
    return static_cast<Uint16>(u > 65535ull ? 65535ull : u);
}

// The second is over: the averages for the text, the counters start again
void closeWindow(Uint64 now_ns, Uint64 now_cycles)
{
    const Uint64 elapsed_ns = now_ns - g.window_start_ns;
    const double frames = g.frames > 0 ? g.frames : 1;
    g.cycles_per_ns = static_cast<double>(now_cycles - g.window_start_cycles) / static_cast<double>(elapsed_ns);
    g.mhz = g.cycles_per_ns * 1000.0;
    g.fps = g.frames * 1e9 / static_cast<double>(elapsed_ns);
    g.frame_ms = toMs(elapsed_ns) / frames;
    g.worst_ms = toMs(g.worst_frame_ns);
    g.late_shown = g.late;

    const LayerMark now = layerNow();
    const LayerMark& w = g.window_mark;
    g.blit_ms = toMs(cyclesToNs(now.blit - w.blit)) / frames;
    g.fill_ms = toMs(cyclesToNs(now.fill - w.fill)) / frames;
    g.copy_ms = toMs(cyclesToNs(now.copy - w.copy)) / frames;
    g.swap_ms = toMs(cyclesToNs(now.swap - w.swap)) / frames;
    g.audio_pct = static_cast<double>(cyclesToNs(now.mixer - w.mixer)) / static_cast<double>(elapsed_ns) * 100.0;
    g.blits_per_frame = static_cast<Uint32>((now.blits - w.blits) / frames);
    g.kpixels_per_frame = static_cast<Uint32>((now.pixels - w.pixels) / frames / 1000);
    g.overlay_ms = toMs(cyclesToNs(g.overlay_cycles)) / frames;
    g.overlay_cycles = 0;
    g.window_mark = now;

    for (Section& s : g.sections) {
        if (!s.name)
            continue;
        s.avg_ms = toMs(s.window_ns) / frames;
        s.worst_ms = toMs(s.window_worst_ns);
        s.calls = s.window_calls;
        s.window_ns = s.window_worst_ns = 0;
        s.window_calls = 0;
    }

    MEMORYSTATUS memory;
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatus(&memory);
    g.free_mb = static_cast<double>(memory.dwAvailPhys) / (1024.0 * 1024.0);
    g.total_mb = static_cast<double>(memory.dwTotalPhys) / (1024.0 * 1024.0);

    g.frames = 0;
    g.worst_frame_ns = 0;
    g.late = 0;
    g.blit_worst = g.present_worst = 0;
    g.window_start_ns = now_ns;
    g.window_start_cycles = now_cycles;
}
} // namespace


void SDL_XboxProfileAdd(int section, const char* name, bool top, Uint64 ns)
{
    if (section < 0 || section >= k_sections)
        return;
    Section& s = g.sections[section];
    if (!s.name) {
        s.name = name;
        s.top = top;
        if (top && g.parts_used < k_parts - 2) {
            s.part = g.parts_used;
            g.part_names[g.parts_used++] = name;
        }
    }
    s.frame_ns += ns;
    s.frame_calls++;
}

void SDL_XboxProfileCount(int counter, const char* name, int value)
{
    if (counter < 0 || counter >= k_counters)
        return;
    g.counters[counter].name = name;
    g.counters[counter].value = value;
}

bool SDL_XboxProfileOn(void)
{
    return g_sdlx_overlay;
}

// At the start of each present (the game's frame is done): the frame into the chart and the second
void sdlx_overlay_frame()
{
    const Uint64 now_ns = sdlx_ticks_ns();
    const Uint64 now_cycles = sdlx_cycles();
    if (g.window_start_ns == 0) {
        g.window_start_ns = now_ns;
        g.window_start_cycles = now_cycles;
        g.window_mark = g.frame_mark = layerNow();
        g.last_present_ns = now_ns;
        return;
    }

    // This frame: from the last present's start to this one's
    const Uint64 frame_ns = now_ns - g.last_present_ns;
    g.last_present_ns = now_ns;
    g.frames++;
    if (frame_ns > g.worst_frame_ns)
        g.worst_frame_ns = frame_ns;
    if (frame_ns > 25000000ull)    //(two refreshes or more)
        g.late++;

    // Its parts: the game's top sections, the last present (copy, swap: also the wait for the
    // refresh), the rest (the game's work it didn't name, waiting for the next frame's time)
    Uint64 parts[k_parts] = {};
    Uint64 named = 0;
    const char* inner = nullptr;
    Uint64 inner_ns = 0;
    for (Section& s : g.sections) {
        if (!s.name)
            continue;
        if (s.top && s.part >= 0) {
            parts[s.part] += s.frame_ns;
            named += s.frame_ns;
        } else if (!s.top && s.frame_ns > inner_ns) {
            inner = s.name;
            inner_ns = s.frame_ns;
        }
        s.window_ns += s.frame_ns;
        if (s.frame_ns > s.window_worst_ns)
            s.window_worst_ns = s.frame_ns;
        s.window_calls += s.frame_calls;
    }
    const Uint64 present_ns = cyclesToNs(g.last_present_cycles);
    parts[k_parts - 2] = present_ns;
    named += present_ns;
    parts[k_parts - 1] = frame_ns > named ? frame_ns - named : 0;
    for (int p = 0; p < k_parts; p++)
        g.history[g.history_next][p] = toUnits(parts[p]);
    g.history_next = (g.history_next + 1) % k_history;

    // A slow one: kept, with what took the time
    if (frame_ns >= k_spike_ns) {
        if (g.spike_count == k_spikes) {
            memmove(&g.spikes[0], &g.spikes[1], sizeof(Spike) * (k_spikes - 1));
            g.spike_count--;
        }
        Spike& spike = g.spikes[g.spike_count++];
        spike = Spike();
        spike.at_s = static_cast<double>(now_ns) / 1e9;
        spike.frame_ms = toMs(frame_ns);
        for (int p = 0; p < k_parts; p++)
            spike.part_ms[p] = toMs(parts[p]);
        spike.inner = inner;
        spike.inner_ms = toMs(inner_ns);
        spike.ticks = g.counters[0].name ? g.counters[0].value : -1;
    }

    for (Section& s : g.sections) {
        s.frame_ns = 0;
        s.frame_calls = 0;
    }
    g.frame_mark = layerNow();

    if (now_ns - g.window_start_ns >= 1000000000ull) {
        closeWindow(now_ns, now_cycles);
        if (g_sdlx_overlay)
            sdlx_log("[prof] fps %.1f frame %.1f worst %.1f late %u blit %.1f present %.1f\n", g.fps, g.frame_ms, g.worst_ms,
                static_cast<unsigned>(g.late_shown), g.blit_ms, g.copy_ms + g.swap_ms);
    }
}

// After presenting: its own time, the next frame's "present" part
void sdlx_overlay_presented(Uint64 cycles)
{
    g.last_present_cycles = cycles;
}

void sdlx_overlay_draw(Uint8* pixels, int pitch)
{
    if (!g_sdlx_overlay)
        return;
    const Uint64 start = sdlx_cycles();
    const Canvas c {pixels, pitch};

    constexpr int X = 8, Y = 8, W = 336;
    int y = Y + 4;
    const int x = X + 5;

    // How tall the panel is: worked out from what it shows
    int rows = 0;
    for (const Section& s : g.sections)
        rows += s.name ? 1 : 0;
    int counters = 0;
    for (const Counter& k : g.counters)
        counters += k.name ? 1 : 0;
    const int chart_h = 64;
    const int h = 4 + k_line_h * 3 + 4 + chart_h + 4 + k_line_h * 2 + k_line_h * (rows + 6) + 4 +
                  (counters ? k_line_h * ((counters + 2) / 3) + 4 : 0) + k_line_h * (1 + SDL_max(g.spike_count, 1)) + 4;
    c.darken(X, Y, W, h);
    c.fill(X, Y, W, 1, k_dim);
    c.fill(X, Y + h - 1, W, 1, k_dim);
    c.fill(X, Y, 1, h, k_dim);
    c.fill(X + W - 1, Y, 1, h, k_dim);

    // The frame rate
    int tx = c.textf(x, y, g.fps >= 59.0 ? k_green : g.fps >= 50.0 ? k_yellow : k_red, "%.1f FPS", g.fps);
    tx = c.textf(tx + 8, y, k_white, "FRAME %.1f", g.frame_ms);
    tx = c.textf(tx + 8, y, msColor(g.worst_ms, 20.0, 34.0), "WORST %.1f", g.worst_ms);
    c.textf(tx + 8, y, g.late_shown ? k_red : k_gray, "LATE %u/S", static_cast<unsigned>(g.late_shown));
    y += k_line_h;
    tx = c.textf(x, y, k_gray, "CPU %.0f MHZ  BUFFERS %d  AUDIO %.0f%%", g.mhz, g_sdlx_back_buffers + 1, g.audio_pct);
    y += k_line_h;
    c.textf(x, y, g.free_mb < 4.0 ? k_red : k_gray, "RAM FREE %.1f / %.0f MB", g.free_mb, g.total_mb);
    y += k_line_h + 4;

    // The chart: 2 px a ms, 32 ms tall; a bar's parts stacked from the bottom
    const int cx = x, cy = y, cw = k_history;
    c.fill(cx - 1, cy - 1, cw + 2, chart_h + 2, 0xFF101418u);
    for (int i = 0; i < k_history; i++) {
        const Uint16* f = g.history[(g.history_next + i) % k_history];
        int bottom = cy + chart_h;
        int total = 0;
        for (int p = 0; p < k_parts; p++) {
            total += f[p];
            const int top = cy + chart_h - total * 2 / 100;
            const int from = SDL_max(top, cy);
            if (from < bottom)
                c.fill(cx + i, from, 1, bottom - from, k_part_colors[p]);
            bottom = SDL_max(from, cy);
        }
        if (total * 2 / 100 > chart_h)
            c.fill(cx + i, cy, 1, 2, k_red);    //(off the top)
    }
    for (int i = 0; i < cw; i += 2) {
        c.put(cx + i, cy + chart_h - 33, k_yellow);    // 16.7 ms
        c.put(cx + i, cy + chart_h - 64 + 1, k_red);   // 32 ms
    }
    c.text(cx + cw + 4, cy + chart_h - 37, k_yellow, "16");
    c.text(cx + cw + 4, cy, k_red, "32 MS");
    // The legend
    int ly = cy + 14;
    for (int p = 0; p < k_parts; p++) {
        const char* name = p < g.parts_used ? g.part_names[p] : p == k_parts - 2 ? "PRESENT" : p == k_parts - 1 ? "REST" : nullptr;
        if (!name)
            continue;
        c.fill(cx + cw + 4, ly + 1, 5, 5, k_part_colors[p]);
        c.text(cx + cw + 12, ly, k_gray, name);
        ly += k_line_h;
    }
    y += chart_h + 4;

    // The sections: the game's (inside ones indented), then this layer's
    c.text(x, y, k_gray, "SECTION");
    c.text(x + 132, y, k_gray, "AVG MS");
    c.text(x + 180, y, k_gray, "MAX MS");
    c.text(x + 228, y, k_gray, "CALLS/S");
    y += k_line_h;
    auto row = [&](const char* name, bool inner, double avg, double worst, long calls, Uint32 color) {
        c.text(x + (inner ? 8 : 0), y, color, name);
        c.textf(x + 132, y, msColor(avg, 8.0, 14.0), "%6.2f", avg);
        if (worst >= 0)
            c.textf(x + 180, y, msColor(worst, 12.0, 17.0), "%6.2f", worst);
        if (calls >= 0)
            c.textf(x + 228, y, k_gray, "%6ld", calls);
        y += k_line_h;
    };
    for (const Section& s : g.sections) {
        if (s.name && s.top)
            row(s.name, false, s.avg_ms, s.worst_ms, s.calls, k_part_colors[s.part >= 0 ? s.part : 0]);
    }
    for (const Section& s : g.sections) {
        if (s.name && !s.top)
            row(s.name, true, s.avg_ms, s.worst_ms, s.calls, k_white);
    }
    row("BLIT", true, g.blit_ms, -1, -1, k_white);
    c.textf(x + 228, y - k_line_h, k_gray, "%u/F %uK PX", static_cast<unsigned>(g.blits_per_frame), static_cast<unsigned>(g.kpixels_per_frame));
    row("FILL", true, g.fill_ms, -1, -1, k_white);
    row("PRESENT COPY", false, g.copy_ms, -1, -1, k_part_colors[k_parts - 2]);
    row("PRESENT SWAP+WAIT", false, g.swap_ms, -1, -1, k_part_colors[k_parts - 2]);
    row("THIS OVERLAY", false, g.overlay_ms, -1, -1, k_gray);
    y += 4;

    // The game's counters, three a line
    if (counters) {
        int n = 0;
        for (const Counter& k : g.counters) {
            if (!k.name)
                continue;
            c.textf(x + (n % 3) * 100, y, k_white, "%s %d", k.name, k.value);
            if (++n % 3 == 0)
                y += k_line_h;
        }
        if (n % 3)
            y += k_line_h;
        y += 4;
    }

    // The slow frames
    c.textf(x, y, k_gray, "SLOW FRAMES (> %.0f MS):", toMs(k_spike_ns));
    y += k_line_h;
    if (g.spike_count == 0)
        c.text(x + 8, y, k_gray, "NONE YET");
    for (int i = g.spike_count - 1; i >= 0; i--) {
        const Spike& s = g.spikes[i];
        int biggest = 0;
        for (int p = 1; p < k_parts; p++) {
            if (s.part_ms[p] > s.part_ms[biggest])
                biggest = p;
        }
        const char* part = biggest < g.parts_used ? g.part_names[biggest] : biggest == k_parts - 2 ? "PRESENT" : "REST";
        int sx = c.textf(x + 8, y, k_gray, "%.0fS", s.at_s);
        sx = c.textf(SDL_max(sx + 6, x + 50), y, msColor(s.frame_ms, 20.0, 34.0), "%.1f", s.frame_ms);
        sx = c.textf(sx + 6, y, k_part_colors[biggest], "%s %.1f", part, s.part_ms[biggest]);
        if (s.inner && s.inner_ms >= 1.0)
            sx = c.textf(sx + 6, y, k_white, "(%s %.1f)", s.inner, s.inner_ms);
        if (s.ticks >= 0)
            c.textf(sx + 6, y, k_gray, "T%d", s.ticks);
        y += k_line_h;
    }

    g.overlay_cycles += sdlx_cycles() - start;
}

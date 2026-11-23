// Xbox SDL3 compatibility layer: window, renderer and texture on Direct3D 8.
//
// The game renders on the CPU into a 640x480 surface, uploads it into a
// streaming texture and presents it. Here presenting copies the frame into a
// linear D3D texture (two, used in turns, so the CPU doesn't wait for the GPU
// to finish with the last one) and draws it as one full-screen quad. Writing
// into the back buffer from the CPU instead is very slow (on xemu at least:
// ~170 ms per frame).

#include <xtl.h>
#include <xgraphics.h>

#include "sdl_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct SDL_Window {
    int w = 640;
    int h = 480;
    SDL_WindowFlags flags = SDL_WINDOW_FULLSCREEN;
};

struct SDL_Renderer {
    SDL_Window* window = nullptr;
    D3DDevice* device = nullptr;
    int vsync = 1;
    D3DCOLOR clear_color = D3DCOLOR_XRGB(0, 0, 0);
    SDL_Texture* pending = nullptr;  // texture to present
    SDL_FRect dst {0, 0, 0, 0};      // where (w 0: the whole screen)
};

struct SDL_Texture {
    SDL_Renderer* renderer = nullptr;
    int w = 0;
    int h = 0;
    int pitch = 0;
    Uint32* pixels = nullptr;
    // A whole-texture update only remembers the source (the game's screen
    // surface, which lives for the whole game); presenting copies straight
    // from there, saving a full-screen copy per frame
    const void* source = nullptr;
    int source_pitch = 0;
};

SdlxProfile g_sdlx_profile;
bool g_sdlx_overlay = false;
int g_sdlx_back_buffers = 0;    // (1: double buffering, 2: triple; the overlay shows it)

namespace {
constexpr int k_screen_w = 640;
constexpr int k_screen_h = 480;

SDL_Window* g_window = nullptr;
D3DTexture* g_frame_textures[2] = {nullptr, nullptr};
int g_frame_index = 0;

// The game's screen surface, in memory the GPU reads as a texture (see
// SDL_XboxGetScreenPixels): presenting it needs no copy
void* g_screen_pixels = nullptr;
int g_screen_pitch = 0;
D3DTexture g_screen_texture;

struct QuadVertex {
    float x, y, z, rhw;
    float u, v;
};
constexpr DWORD k_quad_fvf = D3DFVF_XYZRHW | D3DFVF_TEX1;
SDL_Renderer* g_renderer = nullptr;
SDL_DisplayMode g_display_mode {};

float displayRefreshRate()
{
    if (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I && !(XGetVideoFlags() & XC_VIDEO_FLAGS_PAL_60Hz))
        return 50.0f;
    return 60.0f;
}


bool createDevice(SDL_Renderer* renderer)
{
    Direct3D* d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d)
        return SDL_SetError("Direct3DCreate8 failed");

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = k_screen_w;
    pp.BackBufferHeight = k_screen_h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    //(triple buffering: two back buffers. The GPU draws a finished frame into the free one at once,
    //without waiting for the refresh that shows the one before, so the game starts the next frame
    //right away: a frame that takes a little long is made up by the one queued, not shown late)
    pp.BackBufferCount = 2;
    pp.Windowed = FALSE;
    pp.EnableAutoDepthStencil = FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    if (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p)
        pp.Flags |= D3DPRESENTFLAG_PROGRESSIVE;
    pp.FullScreen_RefreshRateInHz = static_cast<UINT>(displayRefreshRate());
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    // 2D only: a small push buffer is enough
    Direct3D_SetPushBufferSize(256 * 1024, (256 * 1024) / 16);

    HRESULT hr = Direct3D_CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &renderer->device);
    if (FAILED(hr)) {
        // Retry without progressive scan
        pp.Flags &= ~D3DPRESENTFLAG_PROGRESSIVE;
        hr = Direct3D_CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &renderer->device);
    }
    if (FAILED(hr)) {
        // Retry with one back buffer (double buffering)
        sdlx_log("[xbox] No triple buffering (0x%08x): one back buffer\n", static_cast<unsigned>(hr));
        pp.BackBufferCount = 1;
        hr = Direct3D_CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &renderer->device);
    }
    g_sdlx_back_buffers = static_cast<int>(pp.BackBufferCount);
    if (FAILED(hr))
        return SDL_SetError("Direct3D_CreateDevice failed (0x%08x)", static_cast<unsigned>(hr));

    D3DVIEWPORT8 vp;
    ZeroMemory(&vp, sizeof(vp));
    vp.Width = k_screen_w;
    vp.Height = k_screen_h;
    vp.MaxZ = 1.0f;
    D3DDevice_SetViewport(&vp);

    // Soften the interlaced flicker on SD TVs
    D3DDevice_SetFlickerFilter(1);
    D3DDevice_SetSoftDisplayFilter(FALSE);

    // The frame textures: linear (not swizzled), so the CPU can write them
    // directly, and addressed in texels (0..640, 0..480)
    for (D3DTexture*& texture : g_frame_textures) {
        texture = D3DDevice_CreateTexture2(k_screen_w, k_screen_h, 1, 1, 0, D3DFMT_LIN_X8R8G8B8, D3DRTYPE_TEXTURE);
        if (!texture)
            return SDL_SetError("Couldn't create the frame texture");
    }

    // Fixed pipeline state for drawing a textured, pre-transformed quad
    D3DDevice_SetVertexShader(k_quad_fvf);
    D3DDevice_SetRenderState(D3DRS_ZENABLE, FALSE);
    D3DDevice_SetRenderState(D3DRS_LIGHTING, FALSE);
    D3DDevice_SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    D3DDevice_SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    D3DDevice_SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    D3DDevice_SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    D3DDevice_SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    D3DDevice_SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    D3DDevice_SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    D3DDevice_SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    D3DDevice_SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    D3DDevice_SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    D3DDevice_SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DDevice_SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    D3DDevice_Swap(0);
    return true;
}

SDL_FRect g_dst {0, 0, 0, 0};    // the present's quad (w 0: the whole screen)

void drawQuad(D3DTexture* texture)
{
    // One quad over the screen; -0.5 lines the texels up with the pixels. A smaller one (a TV's
    // overscan, gfx_setscreenscale): the border black, and the picture filtered, not point sampled
    const bool scaled = g_dst.w > 0 && (g_dst.w < k_screen_w || g_dst.h < k_screen_h);
    const float x0 = (scaled ? g_dst.x : 0.0f) - 0.5f, y0 = (scaled ? g_dst.y : 0.0f) - 0.5f;
    const float x1 = (scaled ? g_dst.x + g_dst.w : static_cast<float>(k_screen_w)) - 0.5f;
    const float y1 = (scaled ? g_dst.y + g_dst.h : static_cast<float>(k_screen_h)) - 0.5f;
    if (scaled) {
        D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        D3DDevice_SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        D3DDevice_SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    }
    const float u1 = static_cast<float>(k_screen_w), v1 = static_cast<float>(k_screen_h);
    const QuadVertex quad[4] = {
        {x0, y0, 0.0f, 1.0f, 0.0f, 0.0f},
        {x1, y0, 0.0f, 1.0f, u1, 0.0f},
        {x1, y1, 0.0f, 1.0f, u1, v1},
        {x0, y1, 0.0f, 1.0f, 0.0f, v1},
    };

    D3DDevice_BeginScene();
    D3DDevice_SetTexture(0, reinterpret_cast<D3DBaseTexture*>(texture));
    D3DDevice_DrawVerticesUP(D3DPT_QUADLIST, 4, quad, sizeof(QuadVertex));
    D3DDevice_SetTexture(0, NULL);
    D3DDevice_EndScene();
    if (scaled) {
        D3DDevice_SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        D3DDevice_SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    }
}

// Draws the frame; returns a fence to wait on before the game may draw into
// the frame's memory again (0: none needed)
DWORD presentTexture(const SDL_Texture* texture)
{
    const Uint8* src = texture->source ? static_cast<const Uint8*>(texture->source) : reinterpret_cast<const Uint8*>(texture->pixels);
    const int src_pitch = texture->source ? texture->source_pitch : texture->pitch;

    // The game drew straight into the screen texture: no copy
    if (g_screen_pixels && src == g_screen_pixels && texture->w == k_screen_w && texture->h == k_screen_h) {
        sdlx_overlay_draw(static_cast<Uint8*>(g_screen_pixels), g_screen_pitch);
        // The memory is cached (fast for the CPU's drawing, which also reads it
        // to blend), and the GPU doesn't see the CPU's caches: write them back
        __asm__ volatile("wbinvd" ::: "memory");
        drawQuad(&g_screen_texture);
        return D3DDevice_InsertFence();
    }

    // Otherwise copy the frame into one of the frame textures (used in turns)
    D3DTexture* frame = g_frame_textures[g_frame_index];
    g_frame_index ^= 1;

    // Waits if the GPU still draws from this texture (two frames ago)
    D3DLOCKED_RECT locked;
    locked.pBits = nullptr;
    D3DTexture_LockRect(frame, 0, &locked, NULL, 0);
    if (locked.pBits) {
        auto* dst = static_cast<Uint8*>(locked.pBits);
        const int w = SDL_min(texture->w, k_screen_w);
        const int h = SDL_min(texture->h, k_screen_h);
        if (locked.Pitch == src_pitch && w == k_screen_w) {
            memcpy(dst, src, static_cast<size_t>(src_pitch) * h);
        } else {
            for (int y = 0; y < h; y++)
                memcpy(dst + static_cast<size_t>(y) * locked.Pitch, src + static_cast<size_t>(y) * src_pitch, static_cast<size_t>(w) * 4);
        }
        sdlx_overlay_draw(dst, locked.Pitch);
    }
    D3DTexture_UnlockRect(frame, 0);

    drawQuad(frame);
    return 0;
}
} // namespace


void sdlx_video_quit()
{
    // The display stays up until the title exits; nothing to tear down
}


/* ---- Window -------------------------------------------------------------- */

SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags)
{
    (void)title;
    (void)flags;
    if (g_window)
        return g_window;
    g_window = new SDL_Window();
    g_window->w = w > 0 ? w : k_screen_w;
    g_window->h = h > 0 ? h : k_screen_h;
    return g_window;
}

void SDL_DestroyWindow(SDL_Window* window)
{
    if (window && window == g_window) {
        delete g_window;
        g_window = nullptr;
    }
}

SDL_WindowID SDL_GetWindowID(SDL_Window* window)
{
    return window ? 1 : 0;
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* window)
{
    return window ? window->flags : 0;
}

bool SDL_SetWindowTitle(SDL_Window* window, const char* title)
{
    (void)window;
    (void)title;
    return true;
}

bool SDL_SetWindowFullscreen(SDL_Window* window, bool fullscreen)
{
    (void)fullscreen;
    // Always fullscreen on a TV
    if (window)
        window->flags |= SDL_WINDOW_FULLSCREEN;
    return true;
}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window)
{
    (void)window;
    return 1;
}

const SDL_DisplayMode* SDL_GetCurrentDisplayMode(SDL_DisplayID displayID)
{
    (void)displayID;
    g_display_mode.displayID = 1;
    g_display_mode.format = SDL_PIXELFORMAT_XRGB8888;
    g_display_mode.w = k_screen_w;
    g_display_mode.h = k_screen_h;
    g_display_mode.pixel_density = 1.0f;
    g_display_mode.refresh_rate = displayRefreshRate();
    g_display_mode.refresh_rate_numerator = static_cast<int>(g_display_mode.refresh_rate);
    g_display_mode.refresh_rate_denominator = 1;
    return &g_display_mode;
}

bool SDL_ShowCursor(void) { return true; }
bool SDL_HideCursor(void) { return true; }
bool SDL_CursorVisible(void) { return false; }


/* ---- Renderer ------------------------------------------------------------ */

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name)
{
    (void)name;
    if (!window) {
        SDL_SetError("Parameter 'window' is invalid");
        return nullptr;
    }
    if (g_renderer)
        return g_renderer;

    auto* renderer = new SDL_Renderer();
    renderer->window = window;
    if (!createDevice(renderer)) {
        delete renderer;
        return nullptr;
    }
    g_renderer = renderer;
    return renderer;
}

void SDL_DestroyRenderer(SDL_Renderer* renderer)
{
    // The D3D device lives as long as the title; keep it for a new renderer
    (void)renderer;
}

const char* SDL_GetRendererName(SDL_Renderer* renderer)
{
    (void)renderer;
    return "xbox-d3d8";
}

bool SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void)a;
    if (!renderer)
        return SDL_SetError("Parameter 'renderer' is invalid");
    renderer->clear_color = D3DCOLOR_XRGB(r, g, b);
    return true;
}

bool SDL_SetRenderLogicalPresentation(SDL_Renderer* renderer, int w, int h, SDL_RendererLogicalPresentation mode)
{
    (void)renderer;
    (void)w;
    (void)h;
    (void)mode;
    return true;
}

bool SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync)
{
    if (!renderer)
        return SDL_SetError("Parameter 'renderer' is invalid");
    renderer->vsync = vsync != 0 ? 1 : 0;
    D3DDevice_SetRenderState(D3DRS_PRESENTATIONINTERVAL,
        renderer->vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);
    return true;
}

bool SDL_GetRenderVSync(SDL_Renderer* renderer, int* vsync)
{
    if (!renderer)
        return SDL_SetError("Parameter 'renderer' is invalid");
    if (vsync)
        *vsync = renderer->vsync;
    return true;
}

bool SDL_RenderClear(SDL_Renderer* renderer)
{
    if (!renderer)
        return SDL_SetError("Parameter 'renderer' is invalid");
    renderer->pending = nullptr;
    return true;
}

bool SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* srcrect, const SDL_FRect* dstrect)
{
    (void)srcrect;
    if (!renderer || !texture)
        return SDL_SetError("Invalid renderer or texture");
    renderer->pending = texture;
    renderer->dst = dstrect ? *dstrect : SDL_FRect {0, 0, 0, 0};
    return true;
}

bool SDL_RenderPresent(SDL_Renderer* renderer)
{
    if (!renderer || !renderer->device)
        return SDL_SetError("Parameter 'renderer' is invalid");

    sdlx_overlay_frame();

    const Uint64 copy_start = sdlx_cycles();
    DWORD fence = 0;
    if (renderer->pending) {
        g_dst = renderer->dst;
        fence = presentTexture(renderer->pending);
    } else {
        D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, renderer->clear_color, 1.0f, 0);
    }
    const Uint64 swap_start = sdlx_cycles();
    D3DDevice_Swap(0);
    // The game draws the next frame into the memory the GPU just showed: wait
    // until the GPU has read it (this is also where the wait for the vertical
    // blank ends up)
    if (fence)
        D3DDevice_BlockOnFence(fence);
    const Uint64 swap_end = sdlx_cycles();
    sdlx_overlay_presented(swap_end - copy_start);
    g_sdlx_profile.copy = g_sdlx_profile.copy + (swap_start - copy_start);
    g_sdlx_profile.swap = g_sdlx_profile.swap + (swap_end - swap_start);
    return true;
}

bool SDL_RenderCoordinatesFromWindow(SDL_Renderer* renderer, float window_x, float window_y, float* x, float* y)
{
    (void)renderer;
    if (x) *x = window_x;
    if (y) *y = window_y;
    return true;
}

bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer* renderer, SDL_Event* event)
{
    (void)renderer;
    (void)event;
    return true;
}


/* ---- The screen in GPU-visible memory ----------------------------------- */

void* SDL_XboxGetScreenPixels(int w, int h, int* pitch)
{
    if (w != k_screen_w || h != k_screen_h || !g_renderer)
        return nullptr;

    if (!g_screen_pixels) {
        // Linear textures need a pitch that's a multiple of 64 bytes
        const int row = ((w * 4) + 63) & ~63;
        // Physically contiguous (the GPU has no virtual memory), cached
        void* memory = XPhysicalAlloc(static_cast<SIZE_T>(row) * h, MAXULONG_PTR, 4096, PAGE_READWRITE);
        if (!memory) {
            sdlx_log("[gfx] Couldn't allocate the screen texture; copying frames instead\n");
            return nullptr;
        }
        memset(memory, 0, static_cast<size_t>(row) * h);

        memset(&g_screen_texture, 0, sizeof(g_screen_texture));
        XGSetTextureHeader(w, h, 1, 0, D3DFMT_LIN_X8R8G8B8, D3DPOOL_DEFAULT, &g_screen_texture, 0, row);
        D3DResource_Register(reinterpret_cast<D3DResource*>(&g_screen_texture), memory);

        g_screen_pixels = memory;
        g_screen_pitch = row;
    }

    if (pitch)
        *pitch = g_screen_pitch;
    return g_screen_pixels;
}


/* ---- Texture ------------------------------------------------------------- */

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, SDL_PixelFormat format, SDL_TextureAccess access, int w, int h)
{
    (void)access;
    if (!renderer) {
        SDL_SetError("Parameter 'renderer' is invalid");
        return nullptr;
    }
    if (format != SDL_PIXELFORMAT_ARGB8888 && format != SDL_PIXELFORMAT_XRGB8888) {
        SDL_SetError("Unsupported texture format");
        return nullptr;
    }
    auto* texture = new SDL_Texture();
    texture->renderer = renderer;
    texture->w = w;
    texture->h = h;
    texture->pitch = w * 4;
    texture->pixels = static_cast<Uint32*>(calloc(static_cast<size_t>(w) * h, 4));
    if (!texture->pixels) {
        delete texture;
        SDL_SetError("Out of memory");
        return nullptr;
    }
    return texture;
}

void SDL_DestroyTexture(SDL_Texture* texture)
{
    if (!texture)
        return;
    if (texture->renderer && texture->renderer->pending == texture)
        texture->renderer->pending = nullptr;
    free(texture->pixels);
    delete texture;
}

bool SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
    if (!texture || !pixels)
        return SDL_SetError("Invalid texture or pixels");

    // Whole texture: just remember the source (see SDL_Texture::source)
    if (!rect || (rect->x == 0 && rect->y == 0 && rect->w == texture->w && rect->h == texture->h)) {
        texture->source = pixels;
        texture->source_pitch = pitch;
        return true;
    }
    if (texture->source) {
        // A partial update after a whole one: bring the buffer up to date first
        for (int y = 0; y < texture->h; y++) {
            memcpy(reinterpret_cast<Uint8*>(texture->pixels) + static_cast<size_t>(y) * texture->pitch,
                   static_cast<const Uint8*>(texture->source) + static_cast<size_t>(y) * texture->source_pitch,
                   static_cast<size_t>(texture->w) * 4);
        }
        texture->source = nullptr;
    }

    SDL_Rect area = rect ? *rect : SDL_Rect {0, 0, texture->w, texture->h};
    const int x0 = SDL_max(area.x, 0), y0 = SDL_max(area.y, 0);
    const int x1 = SDL_min(area.x + area.w, texture->w), y1 = SDL_min(area.y + area.h, texture->h);
    if (x1 <= x0 || y1 <= y0)
        return true;

    for (int y = y0; y < y1; y++) {
        memcpy(reinterpret_cast<Uint8*>(texture->pixels) + static_cast<size_t>(y) * texture->pitch + x0 * 4,
               static_cast<const Uint8*>(pixels) + static_cast<size_t>(y - area.y) * pitch + (x0 - area.x) * 4,
               static_cast<size_t>(x1 - x0) * 4);
    }
    return true;
}

bool SDL_SetTextureScaleMode(SDL_Texture* texture, SDL_ScaleMode scaleMode)
{
    (void)texture;
    (void)scaleMode;
    return true;
}

SDL_Window* SDL_GetKeyboardFocus(void)
{
    return g_window;
}

bool SDL_CaptureMouse(bool)
{
    return true;
}

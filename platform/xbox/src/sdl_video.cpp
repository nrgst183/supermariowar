// Xbox SDL3 compatibility layer: window, renderer and texture on Direct3D 8.
//
// The game renders on the CPU into a 640x480 surface, uploads it into a
// streaming texture and presents it. Here the texture is a plain memory
// buffer, and presenting copies it into the D3D back buffer and swaps.

#include <xtl.h>

#include "sdl_internal.h"

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
};

struct SDL_Texture {
    SDL_Renderer* renderer = nullptr;
    int w = 0;
    int h = 0;
    int pitch = 0;
    Uint32* pixels = nullptr;
};

namespace {
constexpr int k_screen_w = 640;
constexpr int k_screen_h = 480;

SDL_Window* g_window = nullptr;
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
    pp.BackBufferCount = 1;
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

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    D3DDevice_Swap(0);
    return true;
}

void copyToBackBuffer(const SDL_Texture* texture)
{
    D3DSurface* backbuffer = D3DDevice_GetBackBuffer2(0);
    if (!backbuffer)
        return;

    D3DLOCKED_RECT locked;
    D3DSurface_LockRect(backbuffer, &locked, NULL, 0);
    if (locked.pBits) {
        const int w = SDL_min(texture->w, k_screen_w);
        const int h = SDL_min(texture->h, k_screen_h);
        for (int y = 0; y < h; y++) {
            memcpy(static_cast<Uint8*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch,
                   reinterpret_cast<const Uint8*>(texture->pixels) + static_cast<size_t>(y) * texture->pitch,
                   static_cast<size_t>(w) * 4);
        }
    }
    D3DSurface_UnlockRect(backbuffer);
    D3DSurface_Release(backbuffer);
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
    (void)dstrect;
    if (!renderer || !texture)
        return SDL_SetError("Invalid renderer or texture");
    renderer->pending = texture;
    return true;
}

bool SDL_RenderPresent(SDL_Renderer* renderer)
{
    if (!renderer || !renderer->device)
        return SDL_SetError("Parameter 'renderer' is invalid");

    if (renderer->pending) {
        copyToBackBuffer(renderer->pending);
    } else {
        D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, renderer->clear_color, 1.0f, 0);
    }
    D3DDevice_Swap(0);
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

#include "gfxSDL.h"

#include "gfx.h"
#include "CmdArgs.h"
#include "path.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>


constexpr int GFX_SCREEN_W = 640;
constexpr int GFX_SCREEN_H = 480;

template<typename... Args>
[[noreturn]] void throw_error(std::format_string<Args...> fmt, Args&&... args) {
    throw std::runtime_error(std::format(fmt, std::forward<Args>(args)...));
}

namespace {
void initSdl()
{
    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
        throw_error("SDL error: {}", SDL_GetError());

    const auto sdl_version = SDL_GetVersion();
    printf("[init] SDL %d.%d.%d loaded.\n",
        SDL_VERSIONNUM_MAJOR(sdl_version),
        SDL_VERSIONNUM_MINOR(sdl_version),
        SDL_VERSIONNUM_MICRO(sdl_version));

    const auto sdlimg_version = IMG_Version();
    printf("[gfx] SDL_image %d.%d.%d loaded.\n",
        SDL_VERSIONNUM_MAJOR(sdlimg_version),
        SDL_VERSIONNUM_MINOR(sdlimg_version),
        SDL_VERSIONNUM_MICRO(sdlimg_version));
}

void quitSdl()
{
    SDL_Quit();
}

SDL_Window* createWindow(bool fullscreen)
{
    Uint32 window_flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen)
        window_flags |= SDL_WINDOW_FULLSCREEN;
#ifndef SMW_XBOX
    if (cmd::g_autotest.enabled && cmd::g_autotest.minimized)
        window_flags |= SDL_WINDOW_MINIMIZED;    //(--at-minimized: out of the way)
#endif

    SDL_Window* window = SDL_CreateWindow("smw",
        GFX_SCREEN_W, GFX_SCREEN_H,
        window_flags);
    if (!window)
        throw_error("Couldn't create window: {}", SDL_GetError());

    return window;
}

const char* findPreferredRendererIndex() {
#ifdef SDL2_FORCE_GLES
    return "opengles";
#endif
    return nullptr;
}

SDL_Renderer* createRenderer(SDL_Window* window)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(window, findPreferredRendererIndex());
    if (!renderer)
        throw_error("Couldn't create renderer: {}", SDL_GetError());

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_SetRenderLogicalPresentation(renderer, GFX_SCREEN_W, GFX_SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    printf("[gfx] Renderer: %s\n", SDL_GetRendererName(renderer));
    return renderer;
}

SDL_Surface* createScreenSurface()
{
#ifdef SMW_XBOX
    // On the Xbox, draw straight into memory the GPU displays: no copy of the
    // frame when presenting. 16 bits a pixel, as the original game's screen
    // (and its images, SDL_XboxCompactSurface): half the memory and the bytes
    int pitch = 0;
    if (void* pixels = SDL_XboxGetScreenPixels(GFX_SCREEN_W, GFX_SCREEN_H, &pitch)) {
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(GFX_SCREEN_W, GFX_SCREEN_H, SDL_PIXELFORMAT_ARGB1555, pixels, pitch))
            return surface;
    }
    constexpr SDL_PixelFormat screenFormat = SDL_PIXELFORMAT_ARGB1555;
#else
    constexpr SDL_PixelFormat screenFormat = SDL_PIXELFORMAT_ARGB8888;
#endif
    SDL_Surface* surface = SDL_CreateSurface(GFX_SCREEN_W, GFX_SCREEN_H, screenFormat);
    if (!surface)
        throw_error("Couldn't create video buffer: {}", SDL_GetError());

    return surface;
}

SDL_Texture* createScreenTexture(SDL_Renderer* renderer, SDL_PixelFormat format)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer,
        format,
        SDL_TEXTUREACCESS_STREAMING,
        GFX_SCREEN_W, GFX_SCREEN_H);
    if (!texture)
        throw_error("Couldn't create video texture: {}", SDL_GetError());

    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
        throw_error("Couldn't set up texture scaling mode: %s\n", SDL_GetError());

    //The finished frame: copied to the window, not blended over it (its alpha channel was blending
    //every pixel, a whole screen's worth a frame: with a software renderer the most costly thing
    //the game did; the frame is opaque, so it looks the same)
#ifndef SMW_XBOX    //(the Xbox draws in the memory it displays: no texture to present)
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
#endif

    return texture;
}
} // namespace


bool GraphicsSDL::init(bool fullscreen)
{
    initSdl();
    sdl_window = createWindow(fullscreen);
    sdl_renderer = createRenderer(sdl_window);
    sdl_screen_surface = createScreenSurface();
    sdl_screen_texture = createScreenTexture(sdl_renderer, sdl_screen_surface->format);

    printf("[gfx] Game window initialized (%dx%d, %dbpp)\n",
        sdl_screen_surface->w,
        sdl_screen_surface->h,
        SDL_BITSPERPIXEL(sdl_screen_surface->format));

    screen = sdl_screen_surface;
    return true;
}

GraphicsSDL::~GraphicsSDL()
{
#ifndef SMW_XBOX
    if (m_cursor)
        SDL_DestroyCursor(m_cursor);
#endif
    SDL_DestroyTexture(sdl_screen_texture);
    SDL_DestroySurface(sdl_screen_surface);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    quitSdl();
}

void GraphicsSDL::showErrorBox(const char* message) const
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, sdl_window);
}

void GraphicsSDL::setTitle(const char* title) const
{
    SDL_SetWindowTitle(sdl_window, title);
}

SDL_FRect GraphicsSDL::pictureRect() const
{
    const float w = GFX_SCREEN_W * m_screenScale;
    const float h = GFX_SCREEN_H * m_screenScale;
    return {(GFX_SCREEN_W - w) / 2.0f, (GFX_SCREEN_H - h) / 2.0f, w, h};
}

void GraphicsSDL::setScreenScale(float scale)
{
    m_screenScale = std::clamp(scale, 0.5f, 1.0f);
    //(smaller: smoothed, not every few pixels dropped)
    if (sdl_screen_texture)
        SDL_SetTextureScaleMode(sdl_screen_texture, m_screenScale < 1.0f ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void GraphicsSDL::flipScreen() const
{
    SDL_UpdateTexture(sdl_screen_texture, nullptr, sdl_screen_surface->pixels, sdl_screen_surface->pitch);
    SDL_RenderClear(sdl_renderer);
    const SDL_FRect dst = pictureRect();
    SDL_RenderTexture(sdl_renderer, sdl_screen_texture, nullptr, m_screenScale < 1.0f ? &dst : nullptr);
    SDL_RenderPresent(sdl_renderer);
}

void GraphicsSDL::flipSurface(SDL_Surface* surface) const
{
    SDL_UpdateTexture(sdl_screen_texture, nullptr, surface->pixels, surface->pitch);
    SDL_RenderClear(sdl_renderer);
    const SDL_FRect dst = pictureRect();
    SDL_RenderTexture(sdl_renderer, sdl_screen_texture, nullptr, m_screenScale < 1.0f ? &dst : nullptr);
    SDL_RenderPresent(sdl_renderer);
}

bool GraphicsSDL::setVSync(bool enabled) const
{
    if (!SDL_SetRenderVSync(sdl_renderer, enabled ? 1 : 0)) {
        if (enabled)
            fprintf(stderr, "[gfx] Couldn't enable VSync: %s\n", SDL_GetError());
        return false;
    }

    int vsync = 0;
    return SDL_GetRenderVSync(sdl_renderer, &vsync) && (vsync != 0) == enabled;
}

float GraphicsSDL::refreshRate() const
{
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sdl_window));
    return mode ? mode->refresh_rate : 0.0f;
}

bool GraphicsSDL::isFullScreen() const
{
    return sdl_window && (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN);
}

void GraphicsSDL::changeFullScreen(bool fullscreen)
{
    if (!SDL_SetWindowFullscreen(sdl_window, fullscreen)) {
        fprintf(stderr, "[gfx] Couldn't toggle fullscreen mode: %s\n", SDL_GetError());
        return;
    }
#ifndef SMW_XBOX
    if (!fullscreen) {
        SDL_SyncWindow(sdl_window);
        applyWindowSize();    //(back in a window: the size the options ask for)
    }
#endif
}

void GraphicsSDL::setWindowSize(int mode)
{
    m_windowSize = std::clamp(mode, 0, 4);
    applyWindowSize();
}

void GraphicsSDL::applyWindowSize()
{
#ifdef SMW_XBOX
    //(the Xbox: no window, no mouse)
}

void GraphicsSDL::setCursorStyle(int)
{
}
#else
    if (!sdl_window || m_windowSize == 0 || isFullScreen())
        return;
    SDL_Rect usable {0, 0, 0, 0};
    const SDL_DisplayID display = SDL_GetDisplayForWindow(sdl_window);
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable) || usable.w <= 0 || usable.h <= 0)
        return;
    //(the window's borders and title bar take some of the usable area)
    const int fits = std::max(1, std::min((usable.w - 16) / GFX_SCREEN_W, (usable.h - 48) / GFX_SCREEN_H));
    const int times = m_windowSize == 4 ? fits : std::min(m_windowSize, fits);
    SDL_SetWindowSize(sdl_window, GFX_SCREEN_W * times, GFX_SCREEN_H * times);
    SDL_SetWindowPosition(sdl_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

namespace {
// The arrow of the high-contrast pointer: X its outline (black), . inside it (white); its tip at 0,0
constexpr const char* CURSOR_ARROW[] = {
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X.........X",
    "X......XXXXX",
    "X...X..X",
    "X..XX..X",
    "X.X  X..X",
    "XX   X..X",
    "X     X..X",
    "      X..X",
    "       XX",
};
constexpr int CURSOR_ARROW_W = 12;
constexpr int CURSOR_ARROW_H = static_cast<int>(sizeof(CURSOR_ARROW) / sizeof(CURSOR_ARROW[0]));

char cursorArrowAt(int x, int y)
{
    if (y < 0 || y >= CURSOR_ARROW_H || x < 0)
        return ' ';
    const char* row = CURSOR_ARROW[y];
    return x < static_cast<int>(strlen(row)) ? row[x] : ' ';
}
}

void GraphicsSDL::setCursorStyle(int style)
{
    style = std::clamp(style, 0, 3);
    SDL_Cursor* made = nullptr;
    if (style > 0) {
        //(1: about the system arrow's size, 2 and 3: bigger, for a TV or a big screen)
        const int scale = style + 1;
        //With a white rim around the black outline: it stands out on dark and light alike
        const int w = (CURSOR_ARROW_W + 2) * scale;
        const int h = (CURSOR_ARROW_H + 2) * scale;
        if (SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_FillSurfaceRect(surface, nullptr, 0);
            const Uint32 black = SDL_MapSurfaceRGBA(surface, 0, 0, 0, 255);
            const Uint32 white = SDL_MapSurfaceRGBA(surface, 255, 255, 255, 255);
            for (int y = -1; y <= CURSOR_ARROW_H; y++) {
                for (int x = -1; x <= CURSOR_ARROW_W; x++) {
                    const char c = cursorArrowAt(x, y);
                    bool draw = c != ' ';
                    for (int dy = -1; dy <= 1 && !draw; dy++)
                        for (int dx = -1; dx <= 1 && !draw; dx++)
                            draw = cursorArrowAt(x + dx, y + dy) == 'X';    //(the rim)
                    if (draw) {
                        const SDL_Rect r {(x + 1) * scale, (y + 1) * scale, scale, scale};
                        SDL_FillSurfaceRect(surface, &r, c == 'X' ? black : white);
                    }
                }
            }
            made = SDL_CreateColorCursor(surface, scale, scale);
            SDL_DestroySurface(surface);
        }
    }
    SDL_SetCursor(made ? made : SDL_GetDefaultCursor());
    if (m_cursor)
        SDL_DestroyCursor(m_cursor);
    m_cursor = made;
}
#endif

bool GraphicsSDL::saveScreenshot(const std::string& path) const
{
    if (!IMG_SavePNG(sdl_screen_surface, path.c_str())) {  // SDL3: true on success
        fprintf(stderr, "[gfx] Couldn't write the screenshot to file: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// The screen as it is now, a PNG in the settings folder's "screenshots" (written there on any
// machine: the data folder can be read-only, the Xbox's is the disc); its path, "" if it failed
std::string GraphicsSDL::takeScreenshot() const
{
    using std::chrono::system_clock;

    // NOTE: %F and %T don't work on Windows
    const std::time_t now = system_clock::to_time_t(system_clock::now());
    std::ostringstream name;
    name << std::put_time(std::localtime(&now), "%Y-%m-%d_%H%M%S");
    const std::string dir = GetHomeDirectory() + "screenshots/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    //(two in the same second: _2, _3...)
    std::string path = dir + name.str() + ".png";
    for (int k = 2; std::filesystem::exists(path, ec) && k < 100; k++)
        path = dir + name.str() + "_" + std::to_string(k) + ".png";

    if (!IMG_SavePNG(sdl_screen_surface, path.c_str())) {  // SDL3: true on success
        fprintf(stderr, "[gfx] Couldn't write the screenshot to file: %s\n", SDL_GetError());
        return {};
    }

    printf("[gfx] Screenshot saved to file: %s\n", path.c_str());
    return path;
}

void GraphicsSDL::eventToScreen(SDL_Event& event) const
{
    if (!sdl_renderer)
        return;
    SDL_ConvertEventToRenderCoordinates(sdl_renderer, &event);
    //(a smaller picture: from the screen's place to the picture's)
    if (m_screenScale >= 1.0f)
        return;
    const SDL_FRect r = pictureRect();
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.x = (event.motion.x - r.x) / m_screenScale;
        event.motion.y = (event.motion.y - r.y) / m_screenScale;
        event.motion.xrel /= m_screenScale;
        event.motion.yrel /= m_screenScale;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        event.button.x = (event.button.x - r.x) / m_screenScale;
        event.button.y = (event.button.y - r.y) / m_screenScale;
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        event.wheel.mouse_x = (event.wheel.mouse_x - r.x) / m_screenScale;
        event.wheel.mouse_y = (event.wheel.mouse_y - r.y) / m_screenScale;
    }
}

void GraphicsSDL::windowToScreen(float& x, float& y) const
{
    float rx = x;
    float ry = y;
    if (sdl_renderer && SDL_RenderCoordinatesFromWindow(sdl_renderer, x, y, &rx, &ry)) {
        const SDL_FRect r = pictureRect();
        x = (rx - r.x) / m_screenScale;
        y = (ry - r.y) / m_screenScale;
    }
}

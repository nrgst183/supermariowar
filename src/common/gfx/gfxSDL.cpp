#include "gfxSDL.h"

#include "gfx.h"
#include "path.h"

#include <SDL3_image/SDL_image.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>


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
    // frame when presenting
    int pitch = 0;
    if (void* pixels = SDL_XboxGetScreenPixels(GFX_SCREEN_W, GFX_SCREEN_H, &pitch)) {
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(GFX_SCREEN_W, GFX_SCREEN_H, SDL_PIXELFORMAT_ARGB8888, pixels, pitch))
            return surface;
    }
#endif
    SDL_Surface* surface = SDL_CreateSurface(GFX_SCREEN_W, GFX_SCREEN_H, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
        throw_error("Couldn't create video buffer: {}", SDL_GetError());

    return surface;
}

SDL_Texture* createScreenTexture(SDL_Renderer* renderer)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GFX_SCREEN_W, GFX_SCREEN_H);
    if (!texture)
        throw_error("Couldn't create video texture: {}", SDL_GetError());

    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
        throw_error("Couldn't set up texture scaling mode: %s\n", SDL_GetError());

    return texture;
}
} // namespace


bool GraphicsSDL::init(bool fullscreen)
{
    initSdl();
    sdl_window = createWindow(fullscreen);
    sdl_renderer = createRenderer(sdl_window);
    sdl_screen_surface = createScreenSurface();
    sdl_screen_texture = createScreenTexture(sdl_renderer);

    printf("[gfx] Game window initialized (%dx%d, %dbpp)\n",
        sdl_screen_surface->w,
        sdl_screen_surface->h,
        SDL_BITSPERPIXEL(sdl_screen_surface->format));

    screen = sdl_screen_surface;
    return true;
}

GraphicsSDL::~GraphicsSDL()
{
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

void GraphicsSDL::flipScreen() const
{
    SDL_UpdateTexture(sdl_screen_texture, nullptr, sdl_screen_surface->pixels, sdl_screen_surface->pitch);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderTexture(sdl_renderer, sdl_screen_texture, nullptr, nullptr);
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

void GraphicsSDL::changeFullScreen(bool fullscreen) const
{
    if (!SDL_SetWindowFullscreen(sdl_window, fullscreen)) {
        fprintf(stderr, "[gfx] Couldn't toggle fullscreen mode: %s\n", SDL_GetError());
        return;
    }
}

bool GraphicsSDL::saveScreenshot(const std::string& path) const
{
    if (!IMG_SavePNG(sdl_screen_surface, path.c_str())) {  // SDL3: true on success
        fprintf(stderr, "[gfx] Couldn't write the screenshot to file: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void GraphicsSDL::takeScreenshot() const
{
    using std::chrono::system_clock;

    // NOTE: %F and %T don't work on Windows
    constexpr const char* path_format = "screenshots/%Y-%m-%d_%H%M%S.png";

    const std::time_t now = system_clock::to_time_t(system_clock::now());
    std::ostringstream path_ss;
    path_ss << std::put_time(std::localtime(&now), path_format);
    const std::string path = convertPath(path_ss.str());

    if (!IMG_SavePNG(sdl_screen_surface, path.c_str())) {  // SDL3: true on success
        fprintf(stderr, "[gfx] Couldn't write the screenshot to file: %s\n", SDL_GetError());
        return;
    }

    printf("[gfx] Screenshot saved to file: %s\n", path.c_str());
}

void GraphicsSDL::eventToScreen(SDL_Event& event) const
{
    if (sdl_renderer)
        SDL_ConvertEventToRenderCoordinates(sdl_renderer, &event);
}

void GraphicsSDL::windowToScreen(float& x, float& y) const
{
    float rx = x;
    float ry = y;
    if (sdl_renderer && SDL_RenderCoordinatesFromWindow(sdl_renderer, x, y, &rx, &ry)) {
        x = rx;
        y = ry;
    }
}

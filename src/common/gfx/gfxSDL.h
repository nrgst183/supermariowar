#pragma once

#include <SDL3/SDL.h>

#include <string>


class GraphicsSDL {
public:
    ~GraphicsSDL();

    bool init(bool fullscreen);

    void flipScreen() const;
    void changeFullScreen(bool fullcreen) const;
    bool isFullScreen() const;
    void setTitle(const char*) const;
    void showErrorBox(const char*) const;
    void takeScreenshot() const;
    bool saveScreenshot(const std::string& path) const;
    bool setVSync(bool enabled) const;
    float refreshRate() const;
    // The mouse and touch coordinates of an event (window pixels) in the
    // screen's 640x480 (the window can be any size, or fullscreen with borders)
    void eventToScreen(SDL_Event& event) const;
    void setScreenScale(float scale);
    void windowToScreen(float& x, float& y) const;

private:
    float m_screenScale = 1.0f;    // the picture's share of the screen (setScreenScale)
    // Where the picture goes in the 640x480 of the screen
    SDL_FRect pictureRect() const;
    // surface -> texture -> renderer -> window
    SDL_Window* sdl_window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    SDL_Surface* sdl_screen_surface = nullptr;
    SDL_Texture* sdl_screen_texture = nullptr;
};

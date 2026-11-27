#pragma once

#include <SDL3/SDL.h>

#include <string>


class GraphicsSDL {
public:
    ~GraphicsSDL();

    bool init(bool fullscreen);

    void flipScreen() const;
    /// Shows `surface` (the screen's size and format) instead of the screen
    void flipSurface(SDL_Surface* surface) const;
    void changeFullScreen(bool fullcreen);
    bool isFullScreen() const;
    void setTitle(const char*) const;
    void showErrorBox(const char*) const;
    std::string takeScreenshot() const;
    bool saveScreenshot(const std::string& path) const;
    bool setVSync(bool enabled) const;
    float refreshRate() const;
    // The mouse and touch coordinates of an event (window pixels) in the
    // screen's 640x480 (the window can be any size, or fullscreen with borders)
    void eventToScreen(SDL_Event& event) const;
    void setScreenScale(float scale);
    void windowToScreen(float& x, float& y) const;
    /// The window's size when not full screen: 0 as the player left it, 1..3 that many times
    /// 640x480, 4 the largest whole number of times that fits the screen
    void setWindowSize(int mode);
    /// The mouse pointer: 0 the system's, 1..3 a high-contrast arrow (white, outlined in black), bigger
    /// with each step
    void setCursorStyle(int style);

private:
    void applyWindowSize();
    int m_windowSize = 0;
#ifndef SMW_XBOX
    SDL_Cursor* m_cursor = nullptr;    // (setCursorStyle's; null: the system's)
#endif
    float m_screenScale = 1.0f;    // the picture's share of the screen (setScreenScale)
    // Where the picture goes in the 640x480 of the screen
    SDL_FRect pictureRect() const;
    // surface -> texture -> renderer -> window
    SDL_Window* sdl_window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    SDL_Surface* sdl_screen_surface = nullptr;
    SDL_Texture* sdl_screen_texture = nullptr;
};

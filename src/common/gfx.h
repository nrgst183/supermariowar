/*----------------------------------------------+
| gfx							Florian Hufsky	|
|												|
| start:		14.12.2003						|
| last changes:	22.12.2003						|
+----------------------------------------------*/
/*--------------------------------------------------------------+
| gfx															|
|																|
| a stripped down version of tfree/free  for a pixelate article	|
|																|
| gfx is a mini graphics library containing						|
|  + initialisation of SDL										|
|  + a class for simple Sprites									|
|  + a class for fonts											|
|																|
| have a lot of fun!											|
|		(C) 2003-2008 Florian Hufsky <florian.hufsky@gmail.com>	|
+--------------------------------------------------------------*/

#ifndef GFX_H
#define GFX_H

#include "GlobalConstants.h"
#include "gfx/Color.h"
#include "gfx/gfxSprite.h"
#include "gfx/gfxFont.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

using SpriteStrip = std::array<gfxSprite, PGFX_LAST>;


bool gfx_init(int w, int h, bool fullscreen);
void gfx_changefullscreen(bool fullscreen);
bool gfx_isfullscreen();
// Mouse coordinates: from the window's pixels to the screen's (640x480, at any window size)
void gfx_eventtoscreen(SDL_Event* event);
SDL_MouseButtonFlags gfx_mousestate(float* x, float* y);
void gfx_flipscreen();
// How much of the screen the picture fills: 1, or less for a TV that crops its edges (overscan)
void gfx_setscreenscale(float scale);
/// Turns VSync on or off; returns true if the renderer now does what was asked
bool gfx_setvsync(bool enabled);
/// The refresh rate of the window's display in Hz, 0 if unknown
float gfx_refreshrate();
void gfx_settitle(const char*);
void gfx_show_catched_error(const std::string&);
void gfx_take_screenshot();
bool gfx_save_screenshot(const std::string& path);

void gfx_close();
bool gfx_loadpalette(const std::filesystem::path& palette_path);

void gfx_cliprect(SDL_Rect& srcRect, SDL_Rect& dstRect, const SDL_Rect& clipRect);
// Darkens the whole surface as a black picture drawn over it with this alpha would (a
// screen fade; much faster than blending a picture); false if its format isn't 32 bit
bool gfx_darken(SDL_Surface* surface, Uint8 alpha);

/// Clips a source and destination area pair, so that the destination area doesn't go past
/// a certain threshold in a given direction.
/// For example, if the clip edge is `ClipEdge::Right`, then `dstRect` is cut at `x = threshold`,
/// and the width of `srcRect` shrinks accordingly.
/// Returns true if the destination area is fully hidden.
[[nodiscard]] bool gfx_adjusthiddenrects(SDL_Rect& srcRect, SDL_Rect& dstRect, ClipEdge edge, int threshold);

void gfx_drawpreview(gfxSprite& sprite,
    short dstX, short dstY,
    short srcX, short srcY, short iw, short ih,
    const SDL_Rect& clipRect,
    bool wrap,
    std::optional<std::pair<ClipEdge, int>> clip = std::nullopt);

SpriteStrip gfx_loadfullskin(const std::filesystem::path& path, short colorScheme);
SpriteStrip gfx_loadmenuskin(const std::filesystem::path& path, short colorScheme, bool fLoadBothDirections);

void gfx_setjoystickteamcolor(SDL_Joystick * joystick, short team, float brightness);

Uint32 getRawPixel(SDL_Surface* surf, int x, int y);
RGB getRgb(SDL_Surface* surf, int x, int y);

// Defined separately by each executable (smw, the editors, the tests)
extern SDL_Surface* screen;
extern SDL_Surface* blitdest;  // the destination surface for all drawing
extern short x_shake;
extern short y_shake;

#endif // GFX_H

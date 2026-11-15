#pragma once

#include <SDL3/SDL.h>

#include <string>

class CResourceManager;
class gfxFont;

// THE MARIO WAR LOOK for the theater and the editors' overlays
//
// The game's own art where it has some (its green dialog with the rivets and
// the shadow, the score coin, the crown, the ? block, the mode icons), and
// boxes and pipes drawn in its doubled pixels and its menus' colors (gray
// pipes, green when selected) where it doesn't. Everything is drawn on
// blitdest.
//
// The game's resources: the editors swap rm for theirs, so the theme keeps
// the game's (init() at startup).
namespace theme {

void init(CResourceManager* resources);
CResourceManager& res();
gfxFont& font();       // the menus' small font
gfxFont& bigFont();    // and the large one

// Raised boxes, like the menus' pipe fields
enum class Tone { Gray, Green, Gold, Red, Blue, Dark };
void block(int x, int y, int w, int h, Tone tone);
// A box sunk into a dialog
void inset(int x, int y, int w, int h);
// The game's green dialog (the rivets in its corners, a shadow at the right and bottom)
void dialog(int x, int y, int w, int h);
// A pipe lying down, 16 high, with its lips at both ends
void pipe(int x, int y, int w, bool green);
void pipeLip(int x, int y, bool green);    // 8 wide, 20 high (y - 2 to y + 18)

// The game's icons (16x16, the crown 14x14)
void coin(int x, int y);
void crown(int x, int y);
enum class Icon { Star = 6, Clock = 7, QuestionBlock = 27 };    // the small power-ups
void icon(int x, int y, Icon which);
void modeIcon(int x, int y, int index);    // menu_mode_small (13: the checkered flag, 25: a pipe)
// A pennant on a pole (8x14), in a color
void flag(int x, int y, SDL_Color c);

// A tooltip over a point (its bottom at y)
void tooltip(int cx, int y, const std::string& text);
// A heading: the text on a gray pipe strip
void heading(int x, int y, int w, const std::string& text);

SDL_Color shade(SDL_Color c, int percent);    // lighter (> 100) or darker (< 100)

} // namespace theme

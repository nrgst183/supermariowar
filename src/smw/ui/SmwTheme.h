#pragma once

#include <SDL3/SDL.h>

#include <functional>
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

// The overlays' colors (the "Overlay Colors" option): a dark body for the
// text, a riveted rim around it
struct Scheme {
    SDL_Color body, stripe, well;          // the inside, every other row, sunk boxes
    SDL_Color rim, rimLight, rimDark;      // the frame
    SDL_Color rivet;                       // the rivets (and the timeline's dot)
    bool sprite;                           // the game's own dialog (Pipe)
    bool thinRim;                          // SMB3's status bar: a thin white line, a light inner edge, no rivets
    SDL_Color edge;                        // (that inner edge)
    bool edgeOutside = false;              // thin rim: the light edge right inside the white line, then black (Night)
};
const Scheme& scheme();
// A panel in those colors, laid out like dialog(): the frame 6 pixels in from the
// top left, a shadow in the last 6 at the right and the bottom
void panel(int x, int y, int w, int h);
// A small framed bar in those colors (no rivets, no shadow)
void strip(int x, int y, int w, int h);
// The menus' boxes (Super Mario All-Stars' SMB3 status bar, always in its colors): black, a white
// line, black, a light blue edge, the blue body; corners round. `shadow`: a solid one at the right
// and the bottom (`shadow` pixels), outside the box, as SMB3's dialogs have
void statusFrame(int x, int y, int w, int h, int shadow = 0);
// Its slim version for the screen's bars (the title, the help line): black, the white line, the body
// (4 pixels of rim: room for a line of the large font in 32)
void statusBar(int x, int y, int w, int h);
// A pipe lying down, 16 high, with its lips at both ends
void pipe(int x, int y, int w, bool green);

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

// SMB3's status bar (Super Mario All-Stars): a framed bar in the overlay colors, and the item cards
// beside it (a framed square). An announcement is both: the bar with the text (the menus' large font,
// 44 high) centered on cx, and a card with the icon (drawn at x, y, up to 32 x 32) at its right.
// Returns the total width.
void itemCard(int x, int y, int size);
int announce(int cx, int y, const std::string& text, const std::function<void(int x, int y)>& icon = {});
// A progress line in the status bar's colors (16 high, the line in its middle): white as far as
// `fraction`, the rest sunk in the overlay's colors
void progressLine(int x, int y, int w, float fraction);
// SMB3's P-meter (drawn after it, not copied): six arrows on a black strip that light up as `fraction`
// goes, then the P, which flashes (with `tick`) when it's full. `scale` 2: 140 x 22. Returns the width
int pmeter(int x, int y, float fraction, int tick, int scale = 2);
int pmeterWidth(int scale = 2);
// The end of a match: a status bar panel with the headline ("GAME OVER", "TIME UP!") over the result,
// and a card at its right with `icon` (the crown of the winner) if any; centered on cx
void endBanner(int cx, int y, const std::string& headline, const std::string& result, const std::function<void(int x, int y)>& icon = {});
// The status bar's timer: a clock and the time (16 high); returns the width
int timer(int x, int y, const std::string& time);
int timerWidth(const std::string& time);
// A coin spinning (32 x 32; color: 0 red, 1 green, 2 gold, 3 blue)
void spinningCoin(int x, int y, int tick, int color = 2);
// A movie clapperboard, open (32 x 32): the theater help's item
void clapperboard(int x, int y);

// A connection's quality: four bars (22 x 16) lit by the ping (gray: not known yet)
void signal(int x, int y, int pingMs);
// The same, small (15 x 12): for the score panels
void signalSmall(int x, int y, int pingMs);
// A status (16 x 16), with the game's power-ups: a ? block (info), a 1-up (fine), a Bob-omb (a warning),
// a poison mushroom (a problem)
enum class Status { Info, Good, Warn, Error };
void statusIcon(int x, int y, Status status);
// Something going on: eight dots round a circle (about 34 across), the bright one going round
void spinner(int cx, int cy, int tick);

} // namespace theme

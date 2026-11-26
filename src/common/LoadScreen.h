#pragma once

// THE LOADING SCREEN'S PROGRESS
//
// While the game loads (the splash screen's "Loading..."), each image and sound loaded is a step: the
// screen is drawn again with a bar and the kind of thing loading, at most every few hundredths of a second, so
// a slow machine (the Xbox's first boot) shows it moving. A fast one (a PC: about 0.2 s) shows no bar:
// it only comes up once loading has taken a quarter of a second, and "Ready" only after it did. The bar's whole is the count of the last
// time's steps (kept in loadsteps.txt in the settings folder), an estimate the first time.
namespace loadscreen {

// From here, steps show: `drawScreen` draws the screen under the bar (the splash's backdrop)
void begin(void (*drawScreen)());
// What's loading now ("Loading graphics", "Loading sounds"...): shown under the bar
void phase(const char* what);
// A thing loaded (an image, a sound): the bar moves on. Only the main thread's, and only after begin()
void step();
// Done: the bar full, and its count kept for the next time
void end();

} // namespace loadscreen

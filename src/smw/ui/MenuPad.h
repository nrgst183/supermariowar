#pragma once

class CPlayerInput;

// CONTROLLERS IN THE MENUS
//
// Any connected controller works the menus, whether or not a player's controls are set to it: a
// controller that is no player's is player 1's in the menus (so a controller alone gets to the
// Controls menu, and everywhere else). On every controller: the d-pad as well as the stick, Start
// as A and Back (View) as B. A player's own controller keeps its controls (the menus' buttons in
// the Controls menu); the d-pad, Start and Back come on top of them.
//
// The shoulders (LB, RB; Page Up and Page Down on the keyboard) are the menus' to use: the options
// screens turn their pages with them.
namespace menupad {

/// The controllers' buttons into the players' menu keys (after the frame's events: the keys'
/// pressed and down states, as a key's)
void update(CPlayerInput& input);
/// A controller was plugged in or out (opened again on the next update)
void devicesChanged();
/// The buttons held now are old news (the keys were reset: a button that set a key, or ended a
/// match, isn't pressed again on the next update)
void resync();
/// A key was pressed (Page Up and Page Down: the shoulders)
void noteKey(int key);

/// The shoulders this frame: -1 LB (or Page Up), 1 RB (or Page Down), 0 neither
int shoulder();

} // namespace menupad

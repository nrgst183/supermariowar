#pragma once

// Drawing for online matches (rollback/NetGame.h): the "Waiting for ..." box
// and the network info line. Pure drawing from NetGame's data: it never
// touches the simulation. Does nothing outside an online match.
void drawNetOverlay();

// The signal bars on a player's score panel (at x, y: the top left of its picture): an online match's
// other players, and an online replay's recorded pings (the "Ping Bars" option)
void drawPingMeter(int slot, int x, int y);

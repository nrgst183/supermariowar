#pragma once

// Drawing for online matches (rollback/NetGame.h): the "Waiting for ..." box
// and the network info line. Pure drawing from NetGame's data: it never
// touches the simulation. Does nothing outside an online match.
void drawNetOverlay();

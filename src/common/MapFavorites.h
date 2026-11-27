#pragma once

#include <string>
#include <vector>

// The maps starred as favorites (the game settings' Favorite button, X in the map thumbnails), by
// their short names (the map list's, as the map field shows them; any case). Kept in favorites.txt
// in the home folder, a name a line, written when it changes.
namespace mapfavorites {

bool isFavorite(const std::string& mapName);
// Starred if it wasn't, unstarred if it was; true: it's a favorite now
bool toggle(const std::string& mapName);
std::size_t count();

} // namespace mapfavorites

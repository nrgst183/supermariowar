#pragma once

#include <string>
#include <vector>

class WorldMap;

/// Serializes a world map to a file. Returns false when the file could not be written.
bool saveWorldMap(const WorldMap& world, const std::string& path);
// The world files saved (saveWorldMap), in order: the editors' launcher reads them
extern std::vector<std::string> g_savedWorldFiles;

/// Deserializes a world map from a file. Throws std::runtime_error when the file cannot be opened or is invalid.
WorldMap loadWorldMap(const std::string& path, short tilesize);

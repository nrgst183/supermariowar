#include "MapFavorites.h"

#include "path.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>

namespace mapfavorites {
namespace {
std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}

std::string filePath()
{
    return GetHomeDirectory() + "favorites.txt";
}

// The favorites, lowercase (read from the file the first time they're asked for)
std::set<std::string>& favorites()
{
    static std::set<std::string> names;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in(filePath());
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && line[0] != '#')
                names.insert(lower(line));
        }
    }
    return names;
}

void save()
{
    FILE* fp = fopen(filePath().c_str(), "w");
    if (!fp)
        return;
    fprintf(fp, "#Favorite maps: a map's name a line (starred in the game settings or the map thumbnails)\n");
    for (const std::string& name : favorites())
        fprintf(fp, "%s\n", name.c_str());
    fclose(fp);
}
} // namespace

bool isFavorite(const std::string& mapName)
{
    return favorites().count(lower(mapName)) > 0;
}

bool toggle(const std::string& mapName)
{
    if (mapName.empty())
        return false;
    std::set<std::string>& names = favorites();
    const std::string key = lower(mapName);
    const bool now = names.erase(key) == 0;
    if (now)
        names.insert(key);
    save();
    return now;
}

std::size_t count()
{
    return favorites().size();
}

} // namespace mapfavorites

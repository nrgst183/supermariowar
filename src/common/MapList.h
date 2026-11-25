#ifndef MAPLIST_H
#define MAPLIST_H

#include "path.h"

#include <map>
#include <string>
#include <vector>

class MapListNode {
public:
    MapListNode(std::string fullName);

    MapListNode(const MapListNode&) = delete;
    MapListNode(MapListNode&&) = default;
    MapListNode& operator=(const MapListNode&) = delete;
    MapListNode& operator=(MapListNode&&) = default;

    std::vector<bool> pfFilters;
    std::string filename;

    short iIndex = 0;
    short iFilteredIndex = 0;

    bool fInCurrentFilterSet = true;
    bool fReadFromCache = false;
    bool fValid = true;
};

//announcerlist and musiclist are still a screwed up (a vector accessed like a vector and a list), but way better than before
class MapList {
public:
    MapList(bool fWorldEditor);

        //Adds maps in all world map directories to the map list so they can be edited in the map editor
        void addWorldMaps();

        void add(const char * name);
        bool find(const std::string& name);
        bool findexact(const char * name, bool fWorld);
    bool startswith(char letter);
    bool startswith(const std::string& match);

    /// A map of the list is in this file
    bool hasFile(const std::string& filename) const {
        for (const auto& [name, node] : maps) {
            if (node.filename == filename)
                return true;
        }
        return false;
    }
    /// The file of the map (the battle maps', then the worlds') whose name is `name` as
    /// stripPathAndExtension gives it (the match history's); "" if there is none
    std::string fileNamed(const std::string& name) const {
        for (const auto* list : {&maps, &worldmaps}) {
            for (const auto& [key, node] : *list) {
                if (stripPathAndExtension(node.filename) == name)
                    return node.filename;
            }
        }
        return {};
    }
    const std::string& currentFilename() const {
        return (*outercurrent).second.filename;
    }
    /// The current map is now in this file (saved elsewhere: see writablePath)
    void setCurrentFilename(const std::string& filename) {
        (*outercurrent).second.filename = filename;
    }
    /// The file of the battle map whose short name (currentShortmapname's) is `name`, any case, without
    /// changing the current map; "" if there is none
    std::string fileOfShortName(const std::string& name) const {
        auto lower = [](std::string s) {
            for (char& c : s)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return s;
        };
        const std::string wanted = lower(name);
        for (const auto& [key, node] : maps) {
            if (lower(key) == wanted)
                return node.filename;
        }
        return {};
    }
    const std::string& currentShortmapname() const {
        return (*outercurrent).first;
    }

        void prev(bool fUseFilters);
        void next(bool fUseFilters);
        void random(bool fUseFilters);

    std::string randomFilename() const;

    /// Sets whether the current map is valid and can be loaded
    void setValid(bool fValid) {
        (*current).second.fValid = fValid;
    }
    /// The current map is valid and can be loaded
    bool isValid() const {
        return (*current).second.fValid;
    }

    bool isEmpty() const {
        return maps.empty();
    }
    size_t filteredCount() const {
        return iFilteredMapCount;
    }
    size_t count() const {
        return maps.size();
    }

    std::multimap<std::string, MapListNode>::iterator GetCurrent() {
        return current;
    }
    void SetCurrent(std::multimap<std::string, MapListNode>::iterator itr) {
        outercurrent = current = itr;
    }

        void WriteFilters();
        void ReadFilters();

    bool GetFilter(size_t iFilter) const {
        return (*current).second.pfFilters[iFilter];
    }
    void ToggleFilter(size_t iFilter) {
        (*current).second.pfFilters[iFilter] = !(*current).second.pfFilters[iFilter];
    }

        bool FindFilteredMap();
        void ApplyFilters(const std::vector<bool>& pfFilters);
        bool MapInFilteredSet();

        std::multimap<std::string, MapListNode>::iterator GetIteratorAt(unsigned short iIndex, bool fUseFilters);

    void SaveCurrent() {
        savedcurrent = current;
    }
    void ResumeCurrent() {
        current = savedcurrent;
    }

        void ReloadMapAutoFilters();
        // One map's auto filters read again (it was saved over: the Map Maker), the summary cache written
        void refreshAutoFilters(const std::string& file);
        void WriteMapSummaryCache();

    private:

    std::multimap<std::string, MapListNode> maps;
    std::multimap<std::string, MapListNode> worldmaps;

    decltype(maps)::iterator current;
    decltype(maps)::iterator savedcurrent;
    decltype(maps)::iterator outercurrent;

    std::vector<decltype(maps)::iterator> mlnFilteredMaps;
    std::vector<decltype(maps)::iterator> mlnMaps;

    size_t iFilteredMapCount;
};

// Defined in global.cpp
extern MapList* maplist;

#endif // MAPLIST_H

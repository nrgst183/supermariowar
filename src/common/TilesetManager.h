#pragma once

#include "gfx.h"
#include "TileTypes.h"

#include <array>
#include <filesystem>
#include <vector>


enum class DrawSize {
    Ingame,
    Preview,
    Thumbnail,
};


class CTileset {
public:
    // The map format supports tilesets with up to 128x128 tiles
    static constexpr int MAX_TILES_PER_AXIS = 128;
    static constexpr int MAX_TILES = MAX_TILES_PER_AXIS * MAX_TILES_PER_AXIS;

    explicit CTileset(std::filesystem::path dir);
    // Reads the tile types and the size. Each size of the images loads when it
    // is first drawn (the menus' previews only need the medium one).
    void ensureLoaded();
    // Frees the images (they load again on the next use); the tile types stay
    void unload();
    bool isLoaded() const { return m_loaded; }
    // The memory of the images that are loaded
    size_t loadedBytes() const;
    bool isModified() const { return m_modified; }

    void saveTileset() const;
    // How many times tile types were saved (this run): what's read from the files may have changed
    static unsigned savedCount() { return s_savedCount; }

    TileType tileType(size_t iTileCol, size_t iTileRow) const;
    void setTileType(size_t iTileCol, size_t iTileRow, TileType type);
    TileType incrementTileType(size_t iTileCol, size_t iTileRow);
    TileType decrementTileType(size_t iTileCol, size_t iTileRow);

    void draw(DrawSize drawsize, const SDL_Rect& srcRect, SDL_Surface* dstSurface, const SDL_Rect& dstRect) const;

    const std::string& name() const { return m_name; }
    const std::filesystem::path& directory() const { return m_tileset_dir; }
    short height() const { return m_height; }
    short width() const { return m_width; }

    const gfxSprite& sprite(DrawSize size) const;

private:
    static inline unsigned s_savedCount = 0;
    std::string m_name;
    std::filesystem::path m_tileset_dir;
    std::vector<TileType> m_tiletypes;

    mutable gfxSprite m_sprite_large;
    mutable gfxSprite m_sprite_medium;
    mutable gfxSprite m_sprite_small;

    short m_width = 0;
    short m_height = 0;
    bool m_loaded = false;
    bool m_modified = false;
};


class CTilesetManager {
public:
    explicit CTilesetManager(const std::filesystem::path& gfxPack);

    size_t indexFromName(const std::string& name) const;
    CTileset* tileset(size_t index);
    // The tileset's name and folder, without loading its images
    const CTileset* tilesetInfo(size_t index) const {
        return index < m_tilesets.size() ? &m_tilesets[index] : nullptr;
    }
    size_t count() const { return m_tilesets.size(); }

    void Draw(SDL_Surface* dstSurface, size_t iTilesetID, DrawSize iTileSize,
        short iSrcTileCol, short iSrcTileRow,
        short iDstTileCol, short iDstTileRow);
    void saveTilesets() const;

    // A map is about to load: the tilesets it uses from now on stay loaded
    // (on the Xbox, older ones can be freed to stay in the memory budget)
    void beginMap();
    // Frees every tileset's images (not their tile types; an edited one's are kept): they're loaded
    // again when drawn. A match's start on the Xbox: the menus' previews' don't stay in its 64 MB
    void unloadImages();

    size_t classicTilesetIndex() const {
        return m_classicTilesetIndex;
    }
    CTileset* classicTileset() {
        return tileset(classicTilesetIndex());
    }

    static const SDL_Rect& rect(DrawSize size, size_t col, size_t row);
    static const SDL_Rect& rect(DrawSize size, size_t idx);

private:
    std::vector<CTileset> m_tilesets;
    size_t m_classicTilesetIndex = SIZE_MAX;

#ifdef SMW_XBOX
    // The Xbox has 64 MB of RAM: tileset images (~4 MB in game, ~1 MB for a
    // menu preview) stay loaded up to this much, the least recently used ones
    // going first. The tilesets of the current map always stay (else drawing
    // it would load them again and again): this is only what's kept of others.
    static constexpr size_t MAX_LOADED_BYTES = 4u << 20;
    std::vector<size_t> m_recently_used;  // most recent last
    std::vector<unsigned> m_last_map;     // per tileset: the map it was last used in
    unsigned m_map = 0;
    void trackUse(size_t index);
#endif

    static const std::array<SDL_Rect, CTileset::MAX_TILES> s_rects_ingame;
    static const std::array<SDL_Rect, CTileset::MAX_TILES> s_rects_preview;
    static const std::array<SDL_Rect, CTileset::MAX_TILES> s_rects_thumb;
};

// Defined in global.cpp
extern CTilesetManager* g_tilesetmanager;

// The worker of the menus' background preview loads
// (MI_MapPreview::LoadCurrentMapInBackground). The main thread waits for it before it touches what
// the worker uses (the tilesets, the map, the background: the guards in CTilesetManager,
// CMap::loadMap and LoadCurrentMapBackground). Cheap when nothing loads.
// (The preview, ui/MI_MapPreview.cpp, registers its worker and how to wait for it.)
bool mapPreviewOnWorker();
void mapPreviewWaitForLoads();
// The main thread loaded a map: a load running finishes, and one now out of date goes (its memory)
void mapPreviewSettle();
// Counts the main thread's loads of g_map: a background load started before one isn't shown
unsigned mainMapGeneration();
void bumpMainMapGeneration();
void setMapPreviewWorker(bool onThisThread);
void setMapPreviewWaiter(void (*wait)());
void setMapPreviewRunning(bool running);   // (set by the main thread before the worker starts)
// The worker's time loading tileset images since the last call (the debug overlay's PV TILES)
uint64_t takePreviewTilesetNs();

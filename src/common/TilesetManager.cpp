#include "TilesetManager.h"

#include "FileIO.h"
#include "path.h"
#include "util/ContainerHelpers.h"
#include "util/DirIterator.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <format>
#include <optional>
#include <unordered_set>

#if defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;


namespace {
constexpr std::array<SDL_Rect, CTileset::MAX_TILES> generateTilesetRects(int tilesize)
{
    std::array<SDL_Rect, CTileset::MAX_TILES> rects {};
    for (int row = 0; row < CTileset::MAX_TILES_PER_AXIS; row++) {
        for (int col = 0; col < CTileset::MAX_TILES_PER_AXIS; col++) {
            rects[row * CTileset::MAX_TILES_PER_AXIS + col] = SDL_Rect {
                col * tilesize,
                row * tilesize,
                tilesize,
                tilesize,
            };
        }
    }
    return rects;
}

std::vector<TileType> readTileTypeFile(const fs::path& path)
{
    printf("Reading %s\n", path.generic_string().c_str());
    //Detect if the tiletype file already exists, if not create it
    if (!fs::exists(path)) {
        printf("Not found, will use blank tiles\n");
        return {};
    }

    BinaryFile tsf(path, "rb");
    if (!tsf.is_open()) {
        printf("ERROR: couldn't open tileset file: %s\n", path.generic_string().c_str());
        return {};
    }

    int tiletype_count = tsf.read_i32();
    if (tiletype_count <= 0 || tiletype_count > CTileset::MAX_TILES)
        return {};

    std::vector<TileType> tiletypes;
    tiletypes.reserve(tiletype_count);
    for (int i = 0; i < tiletype_count; i++)
        tiletypes.emplace_back(static_cast<TileType>(tsf.read_i32()));

    return tiletypes;
}

// The size of a PNG image from its header (without decoding it)
bool readPngSize(const fs::path& path, int& width, int& height)
{
    FILE* file = fopen(path.generic_string().c_str(), "rb");
    if (!file)
        return false;
    unsigned char header[24];
    const bool read = fread(header, 1, sizeof(header), file) == sizeof(header);
    fclose(file);
    if (!read || memcmp(header, "\x89PNG\r\n\x1a\n", 8) != 0 || memcmp(header + 12, "IHDR", 4) != 0)
        return false;
    auto be32 = [&](int at) {
        return static_cast<int>((header[at] << 24) | (header[at + 1] << 16) | (header[at + 2] << 8) | header[at + 3]);
    };
    width = be32(16);
    height = be32(20);
    return width > 0 && height > 0;
}

// `larger`: the image of the size above (loads it only if this one is missing)
template <typename Larger>
gfxSprite loadImageOrDownscale(const std::filesystem::path& path, Larger larger)
{
    try {
        return ImageLoader(path).compact().create();
    } catch (const std::string& err) {
        printf("\nwarning: %s -> falling back to downscaled image\n", err.c_str());

        const gfxSprite& largeRes = larger();
        auto image = gfxSprite::blank(largeRes.getWidth() / 2, largeRes.getHeight() / 2, std::nullopt);
        if (!SDL_BlitSurfaceScaled(largeRes.getSurface(), nullptr, image.getSurface(), nullptr, SDL_SCALEMODE_PIXELART)) {
            fprintf(stderr, "SDL_BlitSurface error: %s\n", SDL_GetError());
        }

        return image;
    }
}
} // namespace


/*********************************
*  CTileset
*********************************/
CTileset::CTileset(std::filesystem::path dir)
    : m_name(dir.filename().string())
    , m_tileset_dir(std::move(dir))
{}


void CTileset::ensureLoaded()
{
    if (m_loaded)
        return;
    m_loaded = true;

    m_tiletypes = readTileTypeFile(m_tileset_dir / "tileset.tls");

    int width = 0, height = 0;
    if (!readPngSize(m_tileset_dir / "large.png", width, height)) {
        const gfxSprite& large = sprite(DrawSize::Ingame);
        width = large.getWidth();
        height = large.getHeight();
    }

    m_width = std::min(width / TILESIZE, MAX_TILES_PER_AXIS);
    m_height = std::min(height / TILESIZE, MAX_TILES_PER_AXIS);
}


size_t CTileset::loadedBytes() const
{
    size_t bytes = 0;
    for (const gfxSprite* image : {&m_sprite_large, &m_sprite_medium, &m_sprite_small}) {
        if (const SDL_Surface* surface = image->getSurface())
            bytes += static_cast<size_t>(surface->pitch) * surface->h;
    }
    return bytes;
}


void CTileset::unload()
{
    m_sprite_large = gfxSprite();
    m_sprite_medium = gfxSprite();
    m_sprite_small = gfxSprite();
}


const gfxSprite& CTileset::sprite(DrawSize size) const
{
    assert(m_loaded); // call ensureLoaded first!
    switch (size) {
        case DrawSize::Preview:
            if (!m_sprite_medium)
                m_sprite_medium = loadImageOrDownscale(m_tileset_dir / "medium.png", [this]() -> const gfxSprite& { return sprite(DrawSize::Ingame); });
            return m_sprite_medium;
        case DrawSize::Thumbnail:
            if (!m_sprite_small)
                m_sprite_small = loadImageOrDownscale(m_tileset_dir / "small.png", [this]() -> const gfxSprite& { return sprite(DrawSize::Preview); });
            return m_sprite_small;
        case DrawSize::Ingame:
            break;
    }
    if (!m_sprite_large)
        m_sprite_large = ImageLoader(m_tileset_dir / "large.png").compact().create();
    return m_sprite_large;
}


TileType CTileset::tileType(size_t tileCol, size_t tileRow) const
{
    const size_t idx = tileCol + tileRow * m_width;
    return (idx < m_tiletypes.size())
        ? m_tiletypes[idx]
        : TileType::NonSolid;
}


void CTileset::setTileType(size_t tileCol, size_t tileRow, TileType type)
{
    const size_t idx = tileCol + tileRow * m_width;
    if (m_tiletypes.size() <= idx) {
        m_tiletypes.resize(idx + 1);
    }
    m_tiletypes[idx] = type;
    m_modified = true;
}


TileType CTileset::incrementTileType(size_t tileCol, size_t tileRow)
{
    TileType new_type = ::NextTileType(tileType(tileCol, tileRow));
    setTileType(tileCol, tileRow, new_type);
    return new_type;
}


TileType CTileset::decrementTileType(size_t tileCol, size_t tileRow)
{
    TileType new_type = ::PrevTileType(tileType(tileCol, tileRow));
    setTileType(tileCol, tileRow, new_type);
    return new_type;
}


void CTileset::draw(DrawSize drawsize, const SDL_Rect& srcRect, SDL_Surface* dstSurface, const SDL_Rect& dstRect) const
{
    sprite(drawsize).draw(srcRect, dstSurface, dstRect);
}


void CTileset::saveTileset() const
{
    assert(isLoaded());
    if (!isLoaded())
        return;

    const fs::path tileset_path = m_tileset_dir / "tileset.tls";
    s_savedCount++;
    BinaryFile tsf(tileset_path, "wb");
    if (!tsf.is_open()) {
        printf("ERROR: couldn't open tileset file to save tile types: %s\n", tileset_path.generic_string().c_str());
        return;
    }

    tsf.write_i32(m_tiletypes.size());

    for (TileType tiletype : m_tiletypes)
        tsf.write_i32(static_cast<int>(tiletype));

#if defined(__APPLE__)
    chmod(tileset_path.string().c_str(), S_IRWXU | S_IRWXG | S_IROTH);
#endif
}


/*********************************
*  CTilesetManager
*********************************/

const std::array<SDL_Rect, CTileset::MAX_TILES> CTilesetManager::s_rects_ingame = generateTilesetRects(TILESIZE);
const std::array<SDL_Rect, CTileset::MAX_TILES> CTilesetManager::s_rects_preview = generateTilesetRects(PREVIEWTILESIZE);
const std::array<SDL_Rect, CTileset::MAX_TILES> CTilesetManager::s_rects_thumb = generateTilesetRects(THUMBTILESIZE);

CTilesetManager::CTilesetManager(const fs::path& gfxPack)
{
    std::unordered_set<std::string> found_tileset_names;

    SubdirsIterator dir(convertPath("gfx/packs/tilesets", gfxPack) + '/');
    while (auto path = dir.next()) {
        CTileset tileset(*path);
        found_tileset_names.insert(tileset.name());
        m_tilesets.emplace_back(std::move(tileset));
    }

    //Add tilesets from the Classic pack to fill the gaps
    if (gfxPack.filename() != "Classic") {
        dir = SubdirsIterator(convertPath("gfx/packs/Classic/tilesets/"));
        while (auto path = dir.next()) {
            if (!found_tileset_names.contains(path->filename().string())) {
                CTileset tileset(*path);
                m_tilesets.emplace_back(std::move(tileset));
            }
        }
    }

    utils::sort(m_tilesets, [](const CTileset& a, const CTileset& b) {
        return a.name() < b.name();
    });

    const auto it = utils::find_if(m_tilesets, [](const CTileset& tileset){ return tileset.name() == "Classic"; });
    if (it != m_tilesets.cend()) {
        m_classicTilesetIndex = std::distance(m_tilesets.cbegin(), it);
    }
}


size_t CTilesetManager::indexFromName(const std::string& name) const
{
    for (size_t i = 0; i < m_tilesets.size(); i++) {
        if (m_tilesets[i].name() == name)
            return i;
    }

    return TILESETUNKNOWN;
}


void CTilesetManager::Draw(
    SDL_Surface* dstSurface, size_t iTilesetID, DrawSize drawsize,
    short iSrcTileCol, short iSrcTileRow,
    short iDstTileCol, short iDstTileRow)
{
    CTileset* tileset_ptr = tileset(iTilesetID);
    if (!tileset_ptr)
        return;

    const SDL_Rect& src_rect = rect(drawsize, iSrcTileCol, iSrcTileRow);
    const SDL_Rect& dst_rect = rect(drawsize, iDstTileCol, iDstTileRow);

    tileset_ptr->draw(drawsize, src_rect, dstSurface, dst_rect);
}


// ---- The worker of the menus' background map preview loads (see TilesetManager.h)

namespace {
std::atomic<bool> s_previewRunning {false};
std::atomic<SDL_ThreadID> s_previewWorker {0};
void (*s_previewWait)() = nullptr;
unsigned s_mainMapGeneration = 0;
} // namespace

bool mapPreviewOnWorker()
{
    const SDL_ThreadID worker = s_previewWorker;
    return worker != 0 && worker == SDL_GetCurrentThreadID();
}

void mapPreviewWaitForLoads()
{
    //(cheap when nothing loads: the tilesets call it for every tile drawn)
    if (s_previewRunning && s_previewWait && !mapPreviewOnWorker())
        s_previewWait();
}

void mapPreviewSettle()
{
    if (s_previewWait && !mapPreviewOnWorker())
        s_previewWait();
}

unsigned mainMapGeneration()
{
    return s_mainMapGeneration;
}

void bumpMainMapGeneration()
{
    ++s_mainMapGeneration;
}

void setMapPreviewWorker(bool onThisThread)
{
    s_previewWorker = onThisThread ? SDL_GetCurrentThreadID() : 0;
}

void setMapPreviewWaiter(void (*wait)())
{
    s_previewWait = wait;
}

void setMapPreviewRunning(bool running)
{
    s_previewRunning = running;
}

CTileset* CTilesetManager::tileset(size_t index)
{
    //(the main thread: a preview loading in the background uses the tilesets; it finishes first)
    mapPreviewWaitForLoads();
    if (index < m_tilesets.size()) {
#ifdef SMW_XBOX
        trackUse(index);
#endif
        m_tilesets[index].ensureLoaded();
        return &m_tilesets[index];
    }
    return nullptr;
}


void CTilesetManager::unloadImages()
{
    mapPreviewWaitForLoads();
    for (CTileset& tileset : m_tilesets) {
        if (!tileset.isModified())
            tileset.unload();
    }
}

void CTilesetManager::beginMap()
{
#ifdef SMW_XBOX
    m_map++;
#endif
}


#ifdef SMW_XBOX
void CTilesetManager::trackUse(size_t index)
{
    if (m_last_map.size() != m_tilesets.size())
        m_last_map.assign(m_tilesets.size(), m_map);
    m_last_map[index] = m_map;

    if (!m_recently_used.empty() && m_recently_used.back() == index)
        return;

    std::erase(m_recently_used, index);
    m_recently_used.push_back(index);

    // Over the budget: unload the least recently used tilesets that the
    // current map doesn't use. The callers only keep a tileset pointer for the
    // call at hand, so this is safe here.
    size_t total = 0;
    for (size_t i : m_recently_used)
        total += m_tilesets[i].loadedBytes();
    for (auto it = m_recently_used.begin(); total > MAX_LOADED_BYTES && it != m_recently_used.end();) {
        const size_t i = *it;
        if (m_last_map[i] == m_map || m_tilesets[i].isModified()) {
            ++it;
            continue;
        }
        total -= m_tilesets[i].loadedBytes();
        m_tilesets[i].unload();
        it = m_recently_used.erase(it);
    }
}
#endif


const SDL_Rect& CTilesetManager::rect(DrawSize size, size_t col, size_t row)
{
    const size_t rect_idx = row * CTileset::MAX_TILES_PER_AXIS + col;
    return rect(size, rect_idx);
}


const SDL_Rect& CTilesetManager::rect(DrawSize size, size_t idx)
{
    switch (size) {
        case DrawSize::Ingame: return s_rects_ingame.at(idx);
        case DrawSize::Preview: return s_rects_preview.at(idx);
        case DrawSize::Thumbnail: return s_rects_thumb.at(idx);
    }
    return s_rects_ingame.at(idx);
}


void CTilesetManager::saveTilesets() const
{
    for (const CTileset& tileset : m_tilesets) {
        if (tileset.isLoaded() && tileset.isModified()) {
            tileset.saveTileset();
        }
    }
}

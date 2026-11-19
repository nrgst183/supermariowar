// The world editor, built into the game (EditorLauncher.h). Its sources are
// the standalone editor's (src/worldeditor), compiled here as one unit inside
// the namespace worldeditor:: so that its names don't clash with the game's or the
// other editor's. SMW_EMBEDDED_EDITOR leaves out what the game has itself (the
// window, the screen surfaces, the stand-ins for gameplay code).
#include <algorithm>
#include <new>    // (placement new: a session starts with new menus)

#define SMW_EMBEDDED_EDITOR
#include "CoEdit.h"
#include "Version.h"
#include "WorldShell.h"
#include "../../worldeditor/WorldEditorState.h"

namespace worldeditor {
#include "../../worldeditor/WorldEditorDraw.cpp"
#include "../../worldeditor/WorldEditorEdit.cpp"
#include "../../worldeditor/WorldEditorFile.cpp"
#include "../../worldeditor/WorldEditorGlobals.cpp"
#include "../../worldeditor/WorldEditorStage.cpp"
#include "../../worldeditor/WorldEditorStageMode.cpp"
#include "../../worldeditor/WorldEditorTerrain.cpp"
#include "../../worldeditor/WorldEditorTileModes.cpp"
#include "../../worldeditor/WorldEditorVehiclesWarps.cpp"
#include "../../worldeditor/main.cpp"

// ---- editing together (CoEdit.h): the world as keyed values

namespace {
using editors::coedit::Bytes;
using editors::coedit::Reader;
using editors::coedit::Writer;

enum Section : uint32_t { SECTION_META = 1, SECTION_STAGES, SECTION_VEHICLES, SECTION_WARPS };

class CoeditDocument : public editors::coedit::Document {
public:
    void capture(std::unordered_map<uint32_t, Bytes>& values) override
    {
        const short w = g_worldmap.getWidth();
        const short h = g_worldmap.getHeight();
        for (short col = 0; col < w; col++) {
            for (short row = 0; row < h; row++) {
                const WorldMapTile& t = g_worldmap.getTile(col, row);
                Writer out;
                out.i16(t.iType);
                out.i16(t.iBackgroundWater);
                out.i16(t.iBackgroundSprite);
                out.i16(t.iForegroundSprite);
                out.i16(t.iConnectionType);
                for (bool c : t.fConnection)
                    out.u8(c ? 1 : 0);
                out.i16(t.iWarp);
                out.i16(t.iCompleted);
                out.u8(t.fAnimated ? 1 : 0);
                out.i16(t.iVehicleBoundary);
                values[editors::coedit::cellKey(col, row)] = std::move(out.out);
            }
        }

        Writer meta;
        meta.i16(w);
        meta.i16(h);
        meta.i32(static_cast<int32_t>(g_worldmap.GetMusicCategory()));
        meta.i16(g_worldmap.getNumInitialBonuses());
        for (short k = 0; k < g_worldmap.getNumInitialBonuses(); k++)
            meta.i16(g_worldmap.getInitialBonus(k));
        values[SECTION_META] = std::move(meta.out);

        Writer stages;
        stages.u32(static_cast<uint32_t>(game_values.tourstops.size()));
        for (const TourStop* ts : game_values.tourstops)
            stages.str(WriteTourStopLine(*ts, true));
        values[SECTION_STAGES] = std::move(stages.out);

        Writer vehicles;
        vehicles.u32(static_cast<uint32_t>(vehiclelist.size()));
        for (const std::unique_ptr<WorldVehicle>& v : vehiclelist) {
            vehicles.i16(v->getDrawSprite());
            vehicles.i16(v->getActionId());
            vehicles.i16(v->getCurrentTile().x);
            vehicles.i16(v->getCurrentTile().y);
            vehicles.i16(v->getMinMoves());
            vehicles.i16(v->getMaxMoves());
            vehicles.u8(v->isSpritePacing() ? 1 : 0);
            vehicles.i16(v->getDrawDirection());
            vehicles.i16(v->getBoundary());
        }
        values[SECTION_VEHICLES] = std::move(vehicles.out);

        Writer warps;
        warps.u32(static_cast<uint32_t>(warplist.size()));
        for (const std::unique_ptr<WorldWarp>& warp : warplist) {
            warps.i16(warp->id);
            warps.i16(warp->posA.x);
            warps.i16(warp->posA.y);
            warps.i16(warp->posB.x);
            warps.i16(warp->posB.y);
        }
        values[SECTION_WARPS] = std::move(warps.out);
    }

    void apply(uint32_t key, const Bytes& value) override
    {
        if (value.empty())
            return;
        Reader r(value);
        m_redraw = true;
        if (key & editors::coedit::CELL) {
            const short col = static_cast<short>(editors::coedit::cellX(key));
            const short row = static_cast<short>(editors::coedit::cellY(key));
            if (col >= g_worldmap.getWidth() || row >= g_worldmap.getHeight())
                return;
            WorldMapTile t = g_worldmap.getTile(col, row);
            t.iType = r.i16();
            t.iBackgroundWater = r.i16();
            t.iBackgroundSprite = r.i16();
            t.iForegroundSprite = r.i16();
            t.iConnectionType = r.i16();
            for (bool& c : t.fConnection)
                c = r.u8() != 0;
            t.iWarp = r.i16();
            t.iCompleted = r.i16();
            t.fAnimated = r.u8() != 0;
            t.iVehicleBoundary = r.i16();
            if (r.ok)
                g_worldmap.getTile(col, row) = t;
            return;
        }

        switch (key) {
            case SECTION_META: {
                const short w = r.i16();
                const short h = r.i16();
                const int32_t music = r.i32();
                std::vector<short> bonuses(static_cast<size_t>(std::clamp<int>(r.i16(), 0, 32)));
                for (short& b : bonuses)
                    b = r.i16();
                if (!r.ok || w < 1 || h < 1 || w > 4000 || h > 4000)
                    return;
                if (w != g_worldmap.getWidth() || h != g_worldmap.getHeight()) {
                    g_worldmap.Resize(w, h);
                    m_resized = true;
                }
                while (g_worldmap.GetMusicCategory() != static_cast<WorldMusicCategory>(music) && m_musicTries++ < 64)
                    g_worldmap.nextMusicCategory();
                m_musicTries = 0;
                while (g_worldmap.getNumInitialBonuses() > 0)
                    g_worldmap.removeInitialBonus(0);
                for (short b : bonuses)
                    g_worldmap.addInitialBonus(b);
                break;
            }
            case SECTION_STAGES: {
                const uint32_t n = std::min<uint32_t>(r.u32(), 1000);
                std::vector<std::string> lines;
                for (uint32_t k = 0; k < n && r.ok; k++)
                    lines.push_back(r.str());
                if (!r.ok)
                    return;
                for (TourStop* ts : game_values.tourstops)
                    delete ts;
                game_values.tourstops.clear();
                const Version version {1, 8, 0, 4};
                for (const std::string& line : lines) {
                    //(exactly as written: parsed and written again, it must be the same)
                    std::vector<char> buffer(line.begin(), line.end());
                    buffer.push_back('\0');
                    TourStop* ts = new TourStop();
                    *ts = ParseTourStopLine(buffer.data(), version, true);
                    game_values.tourstops.push_back(ts);
                }
                g_worldmap.setNumStages(static_cast<short>(game_values.tourstops.size()));
                break;
            }
            case SECTION_VEHICLES: {
                const uint32_t n = std::min<uint32_t>(r.u32(), 4096);
                std::vector<std::unique_ptr<WorldVehicle>> list;
                for (uint32_t k = 0; k < n && r.ok; k++) {
                    auto v = std::make_unique<WorldVehicle>();
                    v->setDrawSprite(r.i16());
                    v->setActionId(r.i16());
                    const short col = r.i16();
                    const short row = r.i16();
                    v->setCurrentTile(col, row);
                    v->setMinMoves(r.i16());
                    v->setMaxMoves(r.i16());
                    v->setSpritePacing(r.u8() != 0);
                    v->setDrawDirection(r.i16());
                    v->setBoundary(r.i16());
                    list.push_back(std::move(v));
                }
                if (r.ok)
                    vehiclelist = std::move(list);
                break;
            }
            case SECTION_WARPS: {
                const uint32_t n = std::min<uint32_t>(r.u32(), 4096);
                std::vector<std::unique_ptr<WorldWarp>> list;
                for (uint32_t k = 0; k < n && r.ok; k++) {
                    const short id = r.i16();
                    const short ax = r.i16();
                    const short ay = r.i16();
                    const short bx = r.i16();
                    const short by = r.i16();
                    list.push_back(std::make_unique<WorldWarp>(id, Vec2s {ax, ay}, Vec2s {bx, by}));
                }
                if (r.ok)
                    warplist = std::move(list);
                break;
            }
            default:
                break;
        }
    }

    void applied() override
    {
        if (m_resized)
            setupworldview();    //(redraws too)
        else if (m_redraw)
            updateworldsurface();
        m_resized = false;
        m_redraw = false;
    }

    std::string title() override
    {
        return "world " + worldlist->at(game_values.worldindex).stem().string();
    }

private:
    bool m_redraw = false;
    bool m_resized = false;
    int m_musicTries = 0;
};
} // namespace

std::unique_ptr<editors::coedit::Document> makeCoeditDocument()
{
    return std::make_unique<CoeditDocument>();
}
// ---- the overlay's view of the editor (WorldShell.h)

namespace shell {

bool inMapView()
{
    return state == EDITOR_EDIT;
}

std::string modeName()
{
    std::string name = edit_mode >= 0 && edit_mode < 10 ? szEditModes[edit_mode] : "";
    const std::string suffix = " Mode";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        name.resize(name.size() - suffix.size());
    return name;
}

bool autoPaint()
{
    return fAutoPaint;
}

std::string worldName()
{
    return worldlist ? worldlist->currentPath().stem().string() : std::string();
}

void viewOrigin(int* x, int* y)
{
    *x = draw_offset_col * TILESIZE - draw_offset_x;
    *y = draw_offset_row * TILESIZE - draw_offset_y;
}

SDL_Surface* drawWhole()
{
    const int w = g_worldmap.getWidth() * TILESIZE;
    const int h = g_worldmap.getHeight() * TILESIZE;
    if (w <= 0 || h <= 0 || !screen)
        return nullptr;
    SDL_Surface* surface = SDL_CreateSurface(w, h, screen->format);
    if (!surface)
        return nullptr;
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, 0, 0, 0));
    g_worldmap.DrawMapToSurface(surface);
    return surface;
}

} // namespace shell

} // namespace worldeditor

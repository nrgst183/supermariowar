#include "MI_MapPreview.h"

#include "DebugProfile.h"
#include "GameValues.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "objecthazard.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "TilesetManager.h"
#include "objects/IO_FlameCannon.h"
#include "objects/moving/MO_BulletBill.h"
#include "objects/moving/MO_PirhanaPlant.h"
#include "objects/overmap/WO_OrbitHazard.h"
#include "objects/overmap/WO_StraightPathHazard.h"

#include <atomic>
#include <cstring>
#include <memory>

namespace {
// ---- Loading a preview on a worker thread
//
// The worker loads the map into a map of its own (not g_map, which the menu's preview draws and
// updates) and draws the preview's layers into surfaces of its own; the background image too, if
// it's a new one. The main thread, in the preview's Update, makes them the preview's and the map
// g_map (and the hazards from it). Meanwhile the main thread doesn't touch the tilesets (the guard
// in CTilesetManager waits for the worker) and the worker nothing the main thread changes.
struct PreviewJob {
    MI_MapPreview* owner = nullptr;
    std::string path;
    std::string name;
    std::unique_ptr<CMap> map;
    gfxSprite background;
    gfxSprite blocks;
    gfxSprite foreground;
    std::string backgroundPath;
    gfxSprite backgroundImage;      // decoded by the worker: a background not loaded yet
    unsigned generation = 0;        // of g_map when it started (loaded since: not shown)
    SDL_Thread* thread = nullptr;
    std::atomic<bool> done {false};
    // A newer choice came while it loaded: the worker stops at its next step, and it isn't shown
    // (the newer one loads instead, not after it)
    std::atomic<bool> stale {false};
    int frames = 0;    // (the main thread's) the menu frames it has run for

    ~PreviewJob()
    {
        if (thread)
            SDL_WaitThread(thread, nullptr);
    }
};

std::unique_ptr<PreviewJob> s_job;

// (the main thread, via mapPreviewWaitForLoads/Settle: a running job finishes; it's applied in
// Update, unless the main thread loaded the map since: then it goes now)
void waitForPreviewJob()
{
    if (!s_job)
        return;
    if (s_job->thread) {
        SDL_WaitThread(s_job->thread, nullptr);
        s_job->thread = nullptr;
    }
    if (s_job->generation != mainMapGeneration())
        s_job.reset();
}

int SDLCALL runPreviewJob(void* data)
{
    PreviewJob& job = *static_cast<PreviewJob*>(data);
    setMapPreviewWorker(true);
    //(below the game: it gets the time the menu doesn't use, without delaying its frames. On the
    //Xbox's one core that's the frame's wait for the GPU, which sleeps (sdl_video.cpp); a frame that
    //never waits starves it, and the menu finishes it then, ApplyBackgroundLoad)
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);

    //(its steps' times: the debug overlay's PV counters)
    const uint64_t start = SDL_GetTicksNS();
    takePreviewTilesetNs();
    uint64_t mapDone = start, backgroundDone = start;
    //(each step only if no newer choice came meanwhile: scrolling fast, the one settled on starts
    //sooner)
    [&job, &mapDone, &backgroundDone] {
        job.map->loadMap(job.path, read_type_preview);
        mapDone = SDL_GetTicksNS();
        if (job.stale)
            return;
        job.backgroundPath = mapBackgroundPath(*job.map);
        const gfxSprite* background = loadedMapBackground(job.backgroundPath);
        if (!background) {
            job.backgroundImage = ImageLoader(job.backgroundPath).withoutColorKey().compact().create();
            background = &job.backgroundImage;
        }
        backgroundDone = SDL_GetTicksNS();
        if (job.stale)
            return;
        job.map->preDrawPreviewBackground(*background, job.background, false);
        if (job.stale)
            return;
        job.map->preDrawPreviewBlocks(job.blocks, false);
        job.map->preDrawPreviewMapItems(job.background, false);
        if (job.stale)
            return;
        job.map->preDrawPreviewForeground(job.foreground, false);
        job.map->preDrawPreviewWarps(game_values.toplayer ? job.foreground : job.background, false);
    }();
    const uint64_t end = SDL_GetTicksNS();
    const auto ms = [](uint64_t ns) { return static_cast<int>(ns / 1000000); };
    const int tilesMs = ms(takePreviewTilesetNs());
    if (!job.stale) {    //(a load cut short: not the time a preview takes)
        prof::count(prof::Counter::PreviewTotal, ms(end - start));
        prof::count(prof::Counter::PreviewMap, ms(mapDone - start));
        prof::count(prof::Counter::PreviewBg, ms(backgroundDone - mapDone));
        prof::count(prof::Counter::PreviewDraw, ms(end - backgroundDone));
        prof::count(prof::Counter::PreviewTiles, tilesMs);
    }

    setMapPreviewWorker(false);
    job.done = true;
    setMapPreviewRunning(false);
    return 0;
}
} // namespace


MI_MapPreview::MI_MapPreview(gfxSprite * nspr, short x, short y, short width, short indent)
    : UI_Control(x, y)
    , spr(nspr)
    , iWidth(width)
    , iIndent(indent)
    , iSlideListOut(0)
{
    surfaceMapBackground = gfxSprite::blankScreenFormat(App::screenWidth/2, App::screenHeight/2);
    surfaceMapBlockLayer = gfxSprite::blankScreenFormat(App::screenWidth/2, App::screenHeight/2);
    surfaceMapForeground = gfxSprite::blankScreenFormat(App::screenWidth/2, App::screenHeight/2);
    LoadCurrentMap();

    rectDst.x = x + 16;
    rectDst.y = y + 44;
    rectDst.w = App::screenWidth/2;
    rectDst.h = App::screenHeight/2;

    szMapName.clear();
}

void MI_MapPreview::Update()
{
    ApplyBackgroundLoad();

    //Update hazards
    g_sim.noncolcontainer.update();

    g_sim.objectcontainer[1].update();
    g_sim.objectcontainer[1].cleanDeadObjects();

    g_map->updatePlatforms();
}

void MI_MapPreview::Draw()
{
    if (!m_visible)
        return;

    short iMapBoxX = m_pos.x + (iWidth >> 1) - 176 - iSlideListOut;

    //Draw the background for the map preview: its frame (the map, opaque, covers the rest of it: its
    //background is copied, not blended)
    if (rectDst.y == m_pos.y + 44) {
        rm->menu_dialog.draw(iMapBoxX, m_pos.y + 30, {0, 0, 336, 14});
        rm->menu_dialog.draw(iMapBoxX, m_pos.y + 44, {0, 14, 16, 240});
    } else {
        rm->menu_dialog.draw(iMapBoxX, m_pos.y + 30, {0, 0, 336, 254});
    }
    rm->menu_dialog.draw(iMapBoxX + 336, m_pos.y + 30, {496, 0, 16, 254});
    rm->menu_dialog.draw(iMapBoxX, m_pos.y + 284, {0, 464, 336, 16});
    rm->menu_dialog.draw(iMapBoxX + 336, m_pos.y + 284, {496, 464, 16, 16});

    rectDst.x = iMapBoxX + 16;

    DrawContent();
}

void MI_MapPreview::DrawContent()
{
    //Nothing moving over the map, or between its layers: the three layers in one
    bool still = !g_map->hasPlatformsToDraw();
    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
        CObject* o = obj.get();
        if (dynamic_cast<OMO_OrbitHazard*>(o) || dynamic_cast<OMO_StraightPathHazard*>(o) || dynamic_cast<IO_FlameCannon*>(o)
            || dynamic_cast<MO_BulletBill*>(o) || dynamic_cast<MO_PirhanaPlant*>(o)) {
            still = false;
            break;
        }
    }
    if (still) {
        if (m_layersStale || m_layersTop != game_values.toplayer || !m_layers.getSurface()) {
            if (!m_layers.getSurface())
                m_layers = gfxSprite::blankScreenFormat(App::screenWidth / 2, App::screenHeight / 2);
            SDL_Surface* layers = m_layers.getSurface();
            surfaceMapBackground.draw(layers, Vec2i {0, 0});
            surfaceMapBlockLayer.draw(layers, Vec2i {0, 0});
            if (game_values.toplayer)
                surfaceMapForeground.draw(layers, Vec2i {0, 0});
            m_layersStale = false;
            m_layersTop = game_values.toplayer;
        }
        m_layers.draw(blitdest, rectDst);
        return;
    }

    surfaceMapBackground.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 0);

    surfaceMapBlockLayer.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 1);

    //Draw map hazards
    for (const std::unique_ptr<CObject>& obj : g_sim.objectcontainer[1].list()) {
        if (auto* hazard = dynamic_cast<OMO_OrbitHazard*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<OMO_StraightPathHazard*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<IO_FlameCannon*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<MO_BulletBill*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        } else if (auto* hazard = dynamic_cast<MO_PirhanaPlant*>(obj.get())) {
            hazard->draw(rectDst.x, rectDst.y);
        }
    }

    g_map->drawPlatforms(rectDst.x, rectDst.y, 2);

    if (game_values.toplayer)
        surfaceMapForeground.draw(blitdest, rectDst);

    g_map->drawPlatforms(rectDst.x, rectDst.y, 3);
    g_map->drawPlatforms(rectDst.x, rectDst.y, 4);
}

void MI_MapPreview::LoadCurrentMap()
{
    szMapName = maplist->currentShortmapname();

    LoadMap(maplist->currentFilename());
}

MI_MapPreview::~MI_MapPreview()
{
    if (s_job && s_job->owner == this)
        s_job.reset();  //(waits for the worker)
}

void MI_MapPreview::LoadCurrentMapInBackground()
{
    szMapName = maplist->currentShortmapname();
    const std::string path = maplist->currentFilename();
    if (s_job) {
        //(one at a time: this one when it's done, unless another comes before. The one loading is
        //out of date: it stops at its next step, and isn't shown)
        //(back to the one loading, still whole: it shows, nothing after it)
        if (s_job->owner == this && s_job->path == path && !s_job->stale) {
            m_pendingPath.clear();
            m_pendingName.clear();
            return;
        }
        if (s_job->owner == this && s_job->path != path)
            s_job->stale = true;
        m_pendingPath = path;
        m_pendingName = szMapName;
        return;
    }
    StartBackgroundLoad(path, szMapName);
}

void MI_MapPreview::StartBackgroundLoad(const std::string& path, const std::string& name)
{
    auto job = std::make_unique<PreviewJob>();
    job->owner = this;
    job->path = path;
    job->name = name;
    job->map = std::make_unique<CMap>();
    job->background = gfxSprite::blankScreenFormat(App::screenWidth / 2, App::screenHeight / 2);
    job->blocks = gfxSprite::blankScreenFormat(App::screenWidth / 2, App::screenHeight / 2);
    job->foreground = gfxSprite::blankScreenFormat(App::screenWidth / 2, App::screenHeight / 2);
    job->generation = mainMapGeneration();
    setMapPreviewWaiter(waitForPreviewJob);
    s_job = std::move(job);
    setMapPreviewRunning(true);
    s_job->thread = SDL_CreateThread(runPreviewJob, "map preview", s_job.get());
    if (!s_job->thread) {
        setMapPreviewRunning(false);
        //(no thread: here and now, as before)
        s_job.reset();
        szMapName = name;
        LoadMap(path);
    }
}

// A background load of this preview is done: it shows (unless the main thread loaded a map since:
// then it went already), then the newest choice asked for meanwhile loads
void MI_MapPreview::ApplyBackgroundLoad()
{
    //(a worker the menu's frames leave no time to, below their priority: finished now, the menu
    //waiting for it, rather than a preview that never comes. About a second; on a PC it has a core)
    constexpr int k_starvedFrames = 60;
    if (s_job && s_job->owner == this && !s_job->done && ++s_job->frames >= k_starvedFrames)
        waitForPreviewJob();

    if (s_job && s_job->owner == this && s_job->done) {
        waitForPreviewJob();    //(gone if out of date)
        //(a newer choice came while it loaded: not shown (it may have stopped part way), the newer
        //one loads now)
        if (s_job && s_job->stale)
            s_job.reset();
        if (s_job) {
            std::unique_ptr<PreviewJob> job = std::move(s_job);
            std::swap(surfaceMapBackground, job->background);
            std::swap(surfaceMapBlockLayer, job->blocks);
            std::swap(surfaceMapForeground, job->foreground);
            m_layersStale = true;
            if (job->backgroundImage.getSurface())
                setLoadedMapBackground(job->backgroundPath, std::move(job->backgroundImage));
            //The map: the worker's is g_map now (the old one goes with the job)
            CMap* loaded = job->map.release();
            job->map.reset(g_map);
            g_map = loaded;
            LoadMapHazards(true);
        }
    }

    //The newest choice, asked for while one loaded
    if (!s_job && !m_pendingPath.empty()) {
        const std::string path = std::move(m_pendingPath);
        const std::string name = std::move(m_pendingName);
        m_pendingPath.clear();
        m_pendingName.clear();
        StartBackgroundLoad(path, name);
    }
}

void MI_MapPreview::LoadMap(const std::string& szMapPath)
{
    //(this one now: a choice waiting to load in the background is older; one loading goes too)
    m_pendingPath.clear();
    m_pendingName.clear();
    g_map->loadMap(szMapPath, read_type_preview);

    LoadCurrentMapBackground();

    g_map->preDrawPreviewBackground(rm->spr_background, surfaceMapBackground, false);

    g_map->preDrawPreviewBlocks(surfaceMapBlockLayer, false);

    g_map->preDrawPreviewMapItems(surfaceMapBackground, false);

    g_map->preDrawPreviewForeground(surfaceMapForeground, false);

    g_map->preDrawPreviewWarps(game_values.toplayer ? surfaceMapForeground : surfaceMapBackground, false);
    m_layersStale = true;

    LoadMapHazards(true);
}

bool MI_MapPreview::SetMap(const char * paramSzMapName, bool fWorld)
{
    bool fFound = maplist->findexact(paramSzMapName, fWorld);
    LoadCurrentMap();

    return fFound;
}

void MI_MapPreview::SetSpecialMap(const char * mapName, const char * szMapPath)
{
    szMapName = mapName;

    //(a path below the data directory)
    LoadMap(convertPath(szMapPath));
}

void MI_MapPreview::SetDimensions(short width, short indent)
{
    iWidth = width;
    iIndent = indent;
}

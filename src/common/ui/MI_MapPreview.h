#ifndef UI_MAP_PREVIEW
#define UI_MAP_PREVIEW

#include "gfx/gfxSprite.h"
#include "uicontrol.h"

#include <SDL3/SDL.h>

#include <string>


class MI_MapPreview : public UI_Control
{
public:

    MI_MapPreview(gfxSprite* nspr, short x, short y, short width, short indent);
    ~MI_MapPreview() override;

    //Updates animations or other events every frame
    void Update() override;

    //Draws every frame
    void Draw() override;

    void LoadCurrentMap();
    void LoadMap(const std::string& szMapPath);
    /// The current map of `maplist`, loaded on a worker thread (the menu keeps its frame rate: on
    /// the Xbox a map, its background and tilesets take a few frames): its name shows at once, the
    /// preview when it's ready (Update). A request while one runs waits for it (the last one wins).
    void LoadCurrentMapInBackground();

    bool SetMap(const char * szMapName, bool fWorld);
    void SetSpecialMap(const char * szMapName, const char * szMapPath);

    const char * GetMapName() { return szMapName.c_str(); }

    virtual void SetDimensions(short iWidth, short iIndent);

protected:

    //The map itself (its layers, platforms and hazards) at rectDst, without the frame
    void DrawContent();

    void ApplyBackgroundLoad();
    void StartBackgroundLoad(const std::string& path, const std::string& name);

    gfxSprite * spr;
    std::string m_pendingPath;      // a background load asked for while one ran
    std::string m_pendingName;

    gfxSprite surfaceMapBackground;
    gfxSprite surfaceMapBlockLayer;
    gfxSprite surfaceMapForeground;
    SDL_Rect rectDst;

    short iWidth, iIndent;
    std::string szMapName;

    short iSlideListOut;
};

#endif // UI_MAP_PREVIEW

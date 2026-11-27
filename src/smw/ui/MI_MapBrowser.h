#pragma once

#include "uicontrol.h"
#include "gfx/gfxSprite.h"

#include <SDL3/SDL.h>

#include <array>
#include <map>
#include <string>
#include <vector>

class MapListNode;


class MI_MapBrowser : public UI_Control {
public:
    MI_MapBrowser();

    void Update() override;
    void Draw() override;

    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
    MenuCodeEnum Modify(bool modify) override;

    // 0: tagging the maps of the filter chosen; 1: choosing a map (of those the filters leave);
    // FAVORITES: choosing one of the favorite maps
    static constexpr short FAVORITES = 2;
    void Reset(short type);

    // The mouse: the thumbnails; one pointed at is chosen, clicked as A; the wheel turns the pages
    Box FocusBox() const override;
    //(the chosen map has its own frame; the menu's, round all of the grid, would show only as a line
    //across the title)
    bool DrawsFocusFrame() const override { return false; }
    bool PointerHoverInside(short x, short y) override;
    bool PointerClickInside(short x, short y, MenuCodeEnum& code) override;
    bool PointerWheelInside(int dir, MenuCodeEnum& code) override;

private:
    MenuCodeEnum activate();              // A on the chosen map
    int cellAt(short x, short y) const;   // the thumbnail under the pointer, 0..8 (-1: none)
    void choose(int index);               // a map of all of them (its page loaded)
    void LoadPage(short page, bool fUseFilters);
    std::map<std::string, MapListNode>::iterator mapAt(short index, bool fUseFilters);

    short iPage = 0;
    short iSelectedCol = 0;
    short iSelectedRow = 0;
    short iSelectedIndex = 0;

    std::array<gfxSprite, 9> mapSurfaces;
    MapListNode* mapListNodes[9];
    std::string mapNames[9];
    std::map<std::string, MapListNode>::iterator mapListItr[9];

    short iFilterTagAnimationTimer = 0;
    short iFilterTagAnimationFrame = 0;

    short iType = 0;
    std::vector<std::map<std::string, MapListNode>::iterator> m_favorites;    // (FAVORITES) the maps, as listed
    short iMapCount = 0;

    SDL_Rect srcRectBackground;
    SDL_Rect dstRectBackground;
};

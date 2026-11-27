#include "MI_MapBrowser.h"

#include "Game.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "map.h"
#include "MapFavorites.h"
#include "MapList.h"
#include "path.h"
#include "ResourceManager.h"
#include "uimenu.h"
#include "ui/InputPrompts.h"
#include "ui/MI_MenuPanel.h"
#include "ui/SmwTheme.h"

#include <SDL3_image/SDL_image.h>

#include <string>
#include <algorithm>

namespace {
// The grid's rows: between the title bar and the footer, as on the other menus
constexpr short TOP = 42;
constexpr short ROW_H = 134;
} // namespace

MI_MapBrowser::MI_MapBrowser()
    : UI_Control(0, 0)
{
    srcRectBackground.x = 0;
    srcRectBackground.y = 0;
    srcRectBackground.w = App::screenWidth;
    srcRectBackground.h = App::screenHeight;

    dstRectBackground.x = 0;
    dstRectBackground.y = 0;
    dstRectBackground.w = 160;
    dstRectBackground.h = 120;
}

void MI_MapBrowser::Update()
{
    if (++iFilterTagAnimationTimer > 8) {
        iFilterTagAnimationTimer = 0;

        iFilterTagAnimationFrame += 24;
        if (iFilterTagAnimationFrame > 24)
            iFilterTagAnimationFrame = 0;
    }
}

void MI_MapBrowser::Draw()
{
    if (!m_visible)
        return;

    SDL_Rect rSrc = {0, 0, 160, 120};
    SDL_Rect rDst = {0, 0, 160, 120};

    //(no maps to show (no favorites yet): only the title, the message and the footer)
    for (short iRow = 0; iRow < 3 && iMapCount > 0; iRow++) {
        rDst.y = iRow * ROW_H + TOP;

        for (short iCol = 0; iCol < 3; iCol++) {
            if (iSelectedCol != iCol || iSelectedRow != iRow) {
                if (iRow * 3 + iCol + iPage * 9 >= iMapCount)
                    break;

                rDst.x = iCol * 200 + 40;

                mapSurfaces[iRow * 3 + iCol].draw(rSrc, blitdest, rDst);

                if (iType == 0) {
                    if (mapListNodes[iRow * 3 + iCol]->pfFilters[game_values.selectedmapfilter])
                        rm->menu_map_filter.draw(rDst.x, rDst.y, {iFilterTagAnimationFrame, 24, 24, 24});
                } else if (mapfavorites::isFavorite(mapNames[iRow * 3 + iCol])) {
                    theme::icon(rDst.x + 160 - 20, rDst.y + 4, theme::Icon::Star);
                }

                rm->menu_font_small.drawChopRight(rDst.x, rDst.y + 122, 165, mapNames[iRow * 3 + iCol].c_str());
            }
        }
    }

    //Draw the selected map
    rDst.y = iSelectedRow * ROW_H + TOP;
    rDst.x = iSelectedCol * 200 + 40;

    if (iMapCount > 0) {
        rm->menu_dialog.draw(rDst.x - 16, rDst.y - 16, {0, 0, 176, 148});
        rm->menu_dialog.draw(rDst.x + 160, rDst.y - 16, {496, 0, 16, 148});
        rm->menu_dialog.draw(rDst.x - 16, rDst.y + 132, {0, 464, 176, 16});
        rm->menu_dialog.draw(rDst.x + 160, rDst.y + 132, {496, 464, 16, 16});

        mapSurfaces[iSelectedRow * 3 + iSelectedCol].draw(rSrc, blitdest, rDst);

        if (iType == 0) {
            if (mapListNodes[iSelectedRow * 3 + iSelectedCol]->pfFilters[game_values.selectedmapfilter])
                rm->menu_map_filter.draw(rDst.x, rDst.y, {iFilterTagAnimationFrame, 24, 24, 24});
        } else if (mapfavorites::isFavorite(mapNames[iSelectedRow * 3 + iSelectedCol])) {
            theme::icon(rDst.x + 160 - 20, rDst.y + 4, theme::Icon::Star);
        }

        rm->menu_font_small.drawScrolling(rDst.x, rDst.y + 122, 165, mapNames[iSelectedRow * 3 + iSelectedCol].c_str());
    }

    //The title bar, as every menu's: what picking a map does here, and the page (over the top row's
    //frame, which comes up to it)
    UI_Menu::drawTitleBarBack();
    rm->menu_font_large.drawCentered(320, 5, iType == 0 ? "Maps in this Filter" : iType == FAVORITES ? "Favorite Maps" : "Choose a Map");
    if (iMapCount <= 0) {
        //(no favorites yet: how to star one)
        rm->menu_font_large.drawCentered(320, 200, "No favorite maps yet");
        rm->menu_font_small.drawCentered(320, 236, "Star a map with Favorite in the game settings,");
        rm->menu_font_small.drawCentered(320, 254, "or in Thumbs on the map you're on");
    }
    if (iMapCount > 0) {
        const int pages = (iMapCount + 8) / 9;
        rm->menu_font_small.drawRightJustified(620, 10, "Page " + std::to_string(iPage + 1) + " / " + std::to_string(pages));
    }

    //What the buttons do, in the footer (drawn last: the selected map's frame comes down to it)
    using prompts::Pad;
    using prompts::menuKey;
    if (iType == 0) {
        menupanel::footer({{{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "add / remove"}, {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}},
            "Tag the maps that go in the filter");
    } else {
        //(X: the map there starred or not, as the game settings' Favorite button)
        menupanel::footer({{{Pad::A}, {menuKey(prompts::MENU_SELECT)}, "choose"}, {{Pad::X}, {menuKey(prompts::MENU_RANDOM)}, "favorite"},
                           {{Pad::B}, {menuKey(prompts::MENU_CANCEL)}, "back"}});
    }
}

MenuCodeEnum MI_MapBrowser::Modify(bool modify)
{
    fModifying = modify;
    return MENU_CODE_MODIFY_ACCEPTED;
}

MenuCodeEnum MI_MapBrowser::SendInput(CPlayerInput * playerInput)
{
    for (int iPlayer = 0; iPlayer < 4; iPlayer++) {
        //Only allow the controlling team to control the menu (if there is one)
        if (iControllingTeam != -1) {
            if (iControllingTeam != LookupTeamID(iPlayer) || game_values.playercontrol[iPlayer] != 1)
                continue;
        }

        if (playerInput->outputControls[iPlayer].menu_down.fPressed) {
            short iSkipRows = 1;
            if (playerInput->outputControls[iPlayer].menu_scrollfast.fDown)
                iSkipRows = 3;

            iSelectedIndex = (iSelectedRow + iSkipRows) * 3 + iSelectedCol + iPage * 9;

            if (iSelectedIndex >= iMapCount)
                iSelectedIndex = iMapCount - 1;

            iSelectedRow = (iSelectedIndex / 3) % 3;
            iSelectedCol = iSelectedIndex % 3;

            short iOldPage = iPage;
            iPage = iSelectedIndex / 9;

            if (iOldPage != iPage)
                LoadPage(iPage, iType == 1);

            return MENU_CODE_NONE;
        }

        if (playerInput->outputControls[iPlayer].menu_up.fPressed) {
            short iSkipRows = 1;
            if (playerInput->outputControls[iPlayer].menu_scrollfast.fDown)
                iSkipRows = 3;

            iSelectedIndex = (iSelectedRow - iSkipRows) * 3 + iSelectedCol + iPage * 9;

            if (iSelectedIndex < 0)
                iSelectedIndex = 0;

            iSelectedRow = (iSelectedIndex / 3) % 3;
            iSelectedCol = iSelectedIndex % 3;

            short iOldPage = iPage;
            iPage = iSelectedIndex / 9;

            if (iOldPage != iPage)
                LoadPage(iPage, iType == 1);

            return MENU_CODE_NONE;
        }

        if (playerInput->outputControls[iPlayer].menu_left.fPressed) {
            short iNextIndex = iSelectedRow * 3 + iSelectedCol - 1 + iPage * 9;

            if (iNextIndex < 0)
                return MENU_CODE_NONE;

            iSelectedIndex = iNextIndex;

            if (--iSelectedCol < 0) {
                iSelectedCol = 2;

                if (--iSelectedRow < 0) {
                    iSelectedRow = 2;
                    LoadPage(--iPage, iType == 1);
                }
            }

            return MENU_CODE_NONE;
        }

        if (playerInput->outputControls[iPlayer].menu_right.fPressed) {
            short iNextIndex = iSelectedRow * 3 + iSelectedCol + 1 + iPage * 9;

            if (iNextIndex >= iMapCount)
                return MENU_CODE_NONE;

            iSelectedIndex = iNextIndex;

            if (++iSelectedCol > 2) {
                iSelectedCol = 0;

                if (++iSelectedRow > 2) {
                    iSelectedRow = 0;
                    LoadPage(++iPage, iType == 1);
                }
            }

            return MENU_CODE_NONE;
        }

        if (playerInput->outputControls[iPlayer].menu_select.fPressed)
            return activate();

        if (playerInput->outputControls[iPlayer].menu_random.fPressed && iType != 0 && iMapCount > 0) {
            mapfavorites::toggle(mapNames[iSelectedRow * 3 + iSelectedCol]);
            return MENU_CODE_NONE;
        }

        if (playerInput->outputControls[iPlayer].menu_cancel.fPressed) {
            return MENU_CODE_MAP_BROWSER_EXIT;
        }
    }

    return MENU_CODE_NONE;
}

MenuCodeEnum MI_MapBrowser::activate()
{
    if (iMapCount <= 0)
        return MENU_CODE_NONE;
    if (iType == 0) {
        mapListNodes[iSelectedRow * 3 + iSelectedCol]->pfFilters[game_values.selectedmapfilter] = !mapListNodes[iSelectedRow * 3 + iSelectedCol]->pfFilters[game_values.selectedmapfilter];
        game_values.fNeedWriteFilters = true;
        return MENU_CODE_NONE;
    }
    maplist->SetCurrent(mapListItr[iSelectedRow * 3 + iSelectedCol]);
    return MENU_CODE_MAP_BROWSER_EXIT;
}

UI_Control::Box MI_MapBrowser::FocusBox() const
{
    return {0, static_cast<short>(TOP - 16), 640, static_cast<short>(ROW_H * 3 + 16)};
}

int MI_MapBrowser::cellAt(short x, short y) const
{
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            const int cx = col * 200 + 40, cy = row * ROW_H + TOP;
            if (x >= cx && x < cx + 160 && y >= cy && y < cy + 134 && iPage * 9 + row * 3 + col < iMapCount)
                return row * 3 + col;
        }
    }
    return -1;
}

void MI_MapBrowser::choose(int index)
{
    if (iMapCount <= 0)
        return;
    index = std::clamp(index, 0, iMapCount - 1);
    iSelectedIndex = static_cast<short>(index);
    iSelectedRow = static_cast<short>((index / 3) % 3);
    iSelectedCol = static_cast<short>(index % 3);
    const short page = static_cast<short>(index / 9);
    if (page != iPage) {
        iPage = page;
        LoadPage(iPage, iType == 1);
    }
}

bool MI_MapBrowser::PointerHoverInside(short x, short y)
{
    const int cell = cellAt(x, y);
    if (cell >= 0)
        choose(iPage * 9 + cell);
    return true;
}

bool MI_MapBrowser::PointerClickInside(short x, short y, MenuCodeEnum& code)
{
    const int cell = cellAt(x, y);
    if (cell < 0)
        return true;
    choose(iPage * 9 + cell);
    code = activate();
    return true;
}

bool MI_MapBrowser::PointerWheelInside(int dir, MenuCodeEnum& code)
{
    code = MENU_CODE_NONE;
    choose(iSelectedIndex + dir * 9);
    return true;
}

void MI_MapBrowser::Reset(short type)
{
    iType = type;

    if (iType == 0) {
        iSelectedIndex = maplist->GetCurrent()->second.iIndex;

        iFilterTagAnimationTimer = 0;
        iFilterTagAnimationFrame = 72;

        iMapCount = maplist->count();
    } else if (iType == FAVORITES) {
        //(the favorites, in the map list's order (as Thumbs), the one we're on chosen if it's one)
        m_favorites.clear();
        iSelectedIndex = 0;
        const short all = static_cast<short>(maplist->count());
        for (short i = 0; i < all; i++) {
            auto itr = maplist->GetIteratorAt(i, false);
            if (!mapfavorites::isFavorite(itr->first))
                continue;
            if (itr == maplist->GetCurrent())
                iSelectedIndex = static_cast<short>(m_favorites.size());
            m_favorites.push_back(itr);
        }
        iMapCount = static_cast<short>(m_favorites.size());
    } else {
        iSelectedIndex = maplist->GetCurrent()->second.iFilteredIndex;
        iMapCount = maplist->filteredCount();
    }

    iSelectedRow = (iSelectedIndex / 3) % 3;
    iSelectedCol = iSelectedIndex % 3;
    iPage = iSelectedIndex / 9;

    LoadPage(iPage, iType == 1);
}

void MI_MapBrowser::LoadPage(short page, bool fUseFilters)
{
    for (short iMap = 0; iMap < 9; iMap++) {
        short iIndex = iMap + page * 9;

        if (iIndex >= iMapCount)
            return;

        std::map<std::string, MapListNode>::iterator itr = mapAt(iIndex, fUseFilters);

        //See if we already have a thumbnail saved for this map
        const std::string sConvertedPath = mapThumbnailPath((*itr).second.filename);

        bool fLoaded = false;
        if (FileExists(sConvertedPath)) {
            try {
                mapSurfaces[iMap] = ImageLoader(sConvertedPath.c_str()).withoutColorKey().create();
                fLoaded = true;
            } catch (...) {
                //A broken cache file: make the thumbnail again
            }
        }

        if (!fLoaded) {
            g_map->loadMap((*itr).second.filename, read_type_preview);
            gfxSprite thumbnail = g_map->createThumbnailSurface(false);
            IMG_SavePNG(thumbnail.getSurface(), sConvertedPath.c_str());  //(if it can't be saved, it is just made again next time)
            mapSurfaces[iMap] = std::move(thumbnail);
        }
        mapListNodes[iMap] = &(*itr).second;
        mapNames[iMap] = (*itr).first.c_str();
        mapListItr[iMap] = itr;
    }
}

// The map at `index` of those this browser shows: the favorites' list, or the map list's
std::map<std::string, MapListNode>::iterator MI_MapBrowser::mapAt(short index, bool fUseFilters)
{
    if (iType == FAVORITES)
        return m_favorites[static_cast<size_t>(index)];
    return maplist->GetIteratorAt(index, fUseFilters);
}

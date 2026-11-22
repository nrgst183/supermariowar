#include "LevelEditorState.h"

namespace {
short iPage;
std::array<gfxSprite, 16> sBackgrounds;
SDL_Rect rSrc = {0, 0, 160, 120};
SDL_Rect rDst[16];

bool editor_backgrounds_initialized = false;
void init_editor_backgrounds()
{
    if (editor_backgrounds_initialized)
        return;

    iPage = backgroundlist->currentIndex() / 16;

    for (short iRectY = 0; iRectY < 4; iRectY++) {
        for (short iRectX = 0; iRectX < 4; iRectX++) {
            rDst[iRectY * 4 + iRectX].x = iRectX * 160;
            rDst[iRectY * 4 + iRectX].y = iRectY * 120;
            rDst[iRectY * 4 + iRectX].w = 160;
            rDst[iRectY * 4 + iRectX].h = 120;
        }
    }

    for (short iSurface = 0; iSurface < 16; iSurface++)
        sBackgrounds[iSurface] = gfxSprite::blank(160, 120);

    LoadBackgroundPage(sBackgrounds, iPage);

    editor_backgrounds_initialized = true;
}
} // namespace

int editor_backgrounds()
{
    init_editor_backgrounds();

        //handle messages
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                for (short iSurface = 0; iSurface < 16; iSurface++)
                    sBackgrounds[iSurface] = gfxSprite();

                editor_backgrounds_initialized = false;
                return EDITOR_EDIT;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    for (short iSurface = 0; iSurface < 16; iSurface++)
                        sBackgrounds[iSurface] = gfxSprite();

                    editor_backgrounds_initialized = false;
                    return EDITOR_EDIT;
                } else if (event.key.key == SDLK_PAGEDOWN || event.key.key == SDLK_DOWN) {
                    if ((iPage + 1) * 16 < static_cast<int>(backgroundlist->count())) {
                            iPage++;
                            LoadBackgroundPage(sBackgrounds, iPage);
                        }
                } else if (event.key.key == SDLK_PAGEUP || event.key.key == SDLK_UP) {
                    if ((iPage - 1) * 16 >= 0) {
                            iPage--;
                            LoadBackgroundPage(sBackgrounds, iPage);
                        }
                    }

                break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    for (short iBackground = 0; iBackground < 16; iBackground++) {
                        if (iPage * 16 + iBackground >= static_cast<int>(backgroundlist->count()))
                                break;

                            if (event.button.x >= rDst[iBackground].x && event.button.x < rDst[iBackground].x + rDst[iBackground].w &&
                                event.button.y >= rDst[iBackground].y && event.button.y < rDst[iBackground].y + rDst[iBackground].h)
                            {
                                backgroundlist->setCurrentIndex(iPage * 16 + iBackground);

                                rm->spr_background = ImageLoader(backgroundlist->currentPath()).withoutColorKey().create();
                                g_map->szBackgroundFile = backgroundlist->currentPath().filename().string();

                                if (event.button.button == SDL_BUTTON_LEFT) {
                                    //Set music to background default
                                    for (short iCategory = 0; iCategory < MAXMUSICCATEGORY; iCategory++) {
                                        if (!strncmp(g_szMusicCategoryNames[iCategory], g_map->szBackgroundFile.c_str(), strlen(g_szMusicCategoryNames[iCategory]))) {
                                            g_map->musicCategoryID = iCategory;
                                            break;
                                        }
                                    }
                                }

                                for (short iSurface = 0; iSurface < 16; iSurface++)
                                    sBackgrounds[iSurface] = gfxSprite();

                                editor_backgrounds_initialized = false;
                                return EDITOR_EDIT;
                            }
                        }
                    }
                break;

                case SDL_EVENT_MOUSE_MOTION:
                    bound_mouse_motion_coords();
                break;

                default:
                    break;
            }
        }

        SDL_Rect rect = {0, 0, 640, 480};
        SDL_FillSurfaceRect(screen, &rect, 0x0);

        for (short iBackground = 0; iBackground < 16; iBackground++) {
            if (iPage * 16 + iBackground >= static_cast<int>(backgroundlist->count()))
                break;

            sBackgrounds[iBackground].draw(rSrc, screen, rDst[iBackground]);
        }

        if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight() * 2, "[Page Up] next page, [Page Down] previous page");
        if (OLD_HUD) rm->menu_font_small.draw(0,480-rm->menu_font_small.getHeight(), "[LMB] choose background with music category, [RMB] choose just background");

        int iID = mouse_x / 160 + mouse_y / 120 * 4 + iPage * 16;

        if (iID < static_cast<int>(backgroundlist->count()))
            if (OLD_HUD) rm->menu_font_small.draw(0, 0, backgroundlist->at(iID).string());

        DrawMessage();
        return EDITOR_BACKGROUNDS;
}

//short view_tileset_repeat_direction = -1;
//short view_tileset_repeat_timer = 0;

void LoadBackgroundPage(std::array<gfxSprite, 16>& sBackgrounds, short iPage)
{
    SDL_Rect srcRectBackground = {0, 0, 640, 480};
    SDL_Rect dstRectBackground = {0, 0, 160, 120};

    for (short iIndex = 0; iIndex < 16; iIndex++) {
        if (iPage * 16 + iIndex >= static_cast<int>(backgroundlist->count()))
            break;

        const std::filesystem::path& path = backgroundlist->at(iPage * 16 + iIndex);
        if (path.empty())
            return;

        gfxSprite temp = ImageLoader(path).withoutColorKey().create();

        SDL_FillSurfaceRect(sBackgrounds[iIndex].getSurface(), nullptr, 0x0);

        //(640x480, or an animated one: 640x480 frames under each other, the first one shown here)
        if (temp.getWidth() != 640 || temp.getHeight() < 480 || temp.getHeight() % 480 != 0) {
            printf("WARNING: Background %s is %dx%d but must be 640x480 (or 480 per frame). Skipping.\n",
                path.generic_string().c_str(), temp.getWidth(), temp.getHeight());
            continue;
        }

        temp.drawStretch(srcRectBackground, sBackgrounds[iIndex].getSurface(), dstRectBackground);
    }
}

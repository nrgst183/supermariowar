#include "GraphicsOptionsMenu.h"

#include "FileList.h"
#include "GameValues.h"
#include "ResourceManager.h"

#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_PacksField.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

// The builds with the Online Options menu (OptionsMenu.cpp) have the online matches' rows there;
// the others have them here, at the bottom
#if !defined(NETWORK_DISABLED) && !defined(__EMSCRIPTEN__)
#define SMW_ONLINE_OPTIONS
#endif

UI_GraphicsOptionsMenu::UI_GraphicsOptionsMenu()
    : UI_Menu()
{
    //The screen, the style, and (without the Online Options menu) the online rows
    short rows = 9;
#ifdef SMW_XBOX
    rows--;
#endif
#ifndef SMW_ONLINE_OPTIONS
    rows += 2;
#endif
    SettingsPage page(rows);

#ifdef SMW_XBOX
    //The Xbox is always full screen: no Display field
    miFullscreenField = nullptr;
#else
    miFullscreenField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Display", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miFullscreenField->add("Windowed", false);
    miFullscreenField->add("Fullscreen", true);
    miFullscreenField->setOutputPtr(&game_values.fullscreen);
    miFullscreenField->setCurrentValue(game_values.fullscreen ? 1 : 0);
    miFullscreenField->setAutoAdvance(true);
    miFullscreenField->setItemChangedCode(MENU_CODE_TOGGLE_FULLSCREEN);
    page.add(miFullscreenField, "Play in a window or full screen");
#endif

    //The game always ticks at 62.5 Hz; this only sets how often it is drawn
    game_values.framelimiter = static_cast<short>(GetFrameRateMode(game_values.framelimiter));
    miFrameRateField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Frame Rate", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miFrameRateField->add("VSync", static_cast<short>(FrameRateMode::VSync));
    miFrameRateField->add("Uncapped", static_cast<short>(FrameRateMode::Uncapped));
    miFrameRateField->add("62.5 FPS", static_cast<short>(FrameRateMode::TickRate));
    miFrameRateField->setOutputPtr(&game_values.framelimiter);
    miFrameRateField->setCurrentValue(game_values.framelimiter);
    miFrameRateField->setAutoAdvance(true);
    page.add(miFrameRateField, "Screen redraws per second (game logic runs at 62.5)");

    miSmoothMotionField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Smooth Motion", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miSmoothMotionField->add("Off", false);
    miSmoothMotionField->add("On", true);
    miSmoothMotionField->setOutputPtr(&game_values.smoothmotion);
    miSmoothMotionField->setCurrentValue(game_values.smoothmotion ? 1 : 0);
    miSmoothMotionField->setAutoAdvance(true);
    page.add(miSmoothMotionField, "Smooths motion on screens faster than 62.5 FPS");

    miMenuGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, page.x(), page.rowY(), "Menu Graphics", SettingsPage::ROW_W, SettingsPage::LABEL_W, menugraphicspacklist, MENU_CODE_MENU_GRAPHICS_PACK_CHANGED);
    page.add(miMenuGraphicsPackField, "The look of the menus");
    miWorldGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, page.x(), page.rowY(), "World Graphics", SettingsPage::ROW_W, SettingsPage::LABEL_W, worldgraphicspacklist, MENU_CODE_WORLD_GRAPHICS_PACK_CHANGED);
    page.add(miWorldGraphicsPackField, "The look of the world maps");
    miGameGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, page.x(), page.rowY(), "Game Graphics", SettingsPage::ROW_W, SettingsPage::LABEL_W, gamegraphicspacklist, MENU_CODE_GAME_GRAPHICS_PACK_CHANGED);
    page.add(miGameGraphicsPackField, "The look of the matches");

    miTopLayerField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Draw Top Layer", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miTopLayerField->add("Background", false);
    miTopLayerField->add("Foreground", true);
    miTopLayerField->setOutputPtr(&game_values.toplayer);
    miTopLayerField->setCurrentValue(game_values.toplayer ? 1 : 0);
    miTopLayerField->setAutoAdvance(true);
    page.add(miTopLayerField, "Draw the map's top layer in front of or behind players");

    //A TV crops the picture's edges (overscan): the picture smaller, so that nothing is lost
    miScreenSizeField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Picture Size", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miScreenSizeField->add("Full", 0);
    miScreenSizeField->add("95% (TV)", 1);
    miScreenSizeField->add("90% (TV)", 2);
    miScreenSizeField->setOutputPtr(&game_values.screensize);
    miScreenSizeField->setCurrentValue(game_values.screensize);
    miScreenSizeField->setItemChangedCode(MENU_CODE_SCREEN_SIZE_CHANGED);
    page.add(miScreenSizeField, "Smaller if your TV cuts off the edges of the picture");

    //The colors of the theater's and the editors' panels
    miOverlayColorsField = new MI_SelectField<short>(&rm->spr_selectfield, page.x(), page.rowY(), "Overlay Colors", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miOverlayColorsField->add("Night", 0);
    miOverlayColorsField->add("Pipe", 1);
    miOverlayColorsField->add("Castle", 2);
    miOverlayColorsField->add("SMB3", 3);
    miOverlayColorsField->setOutputPtr(&game_values.overlaycolors);
    miOverlayColorsField->setCurrentValue(game_values.overlaycolors);
    page.add(miOverlayColorsField, "Panel colors in the theater and the editors");

#ifdef SMW_ONLINE_OPTIONS
    miPingBarsField = nullptr;
    miNetStatsField = nullptr;
#else
    //Online (and in online replays): the other players' connection on their score panels
    miPingBarsField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Ping Bars", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miPingBarsField->add("Off", false);
    miPingBarsField->add("On", true);
    miPingBarsField->setOutputPtr(&game_values.showpingbars);
    miPingBarsField->setCurrentValue(game_values.showpingbars ? 1 : 0);
    page.add(miPingBarsField, "Shows online players' connection on their score panel");

    //Online matches only (F3 in a match toggles it too)
    miNetStatsField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Network Stats", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miNetStatsField->add("Off", false);
    miNetStatsField->add("On", true);
    miNetStatsField->setOutputPtr(&game_values.shownetstats);
    miNetStatsField->setCurrentValue(game_values.shownetstats ? 1 : 0);
    miNetStatsField->setAutoAdvance(true);
    page.add(miNetStatsField, "Shows connection stats in online matches (or press F3)");
#endif

    page.finish(*this, "Graphics Options", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

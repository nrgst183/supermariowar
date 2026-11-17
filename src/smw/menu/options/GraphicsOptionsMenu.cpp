#include "GraphicsOptionsMenu.h"

#include "FileList.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include <algorithm>

#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_PacksField.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"

UI_GraphicsOptionsMenu::UI_GraphicsOptionsMenu()
    : UI_Menu()
{
    //Online (and in online replays): the other players' connection on their score panels
    miPingBarsField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 40, "Ping Bars", 500, 220);
    miPingBarsField->add("Off", false);
    miPingBarsField->add("On", true);
    miPingBarsField->setOutputPtr(&game_values.showpingbars);
    miPingBarsField->setCurrentValue(game_values.showpingbars ? 1 : 0);

    //The colors of the theater's and the editors' panels
    miOverlayColorsField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 76, "Overlay Colors", 500, 220);
    miOverlayColorsField->add("Night", 0);
    miOverlayColorsField->add("Pipe", 1);
    miOverlayColorsField->add("Castle", 2);
    miOverlayColorsField->add("SMB3", 3);
    miOverlayColorsField->setOutputPtr(&game_values.overlaycolors);
    miOverlayColorsField->setCurrentValue(game_values.overlaycolors);

    miTopLayerField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 112, "Draw Top Layer", 500, 220);
    miTopLayerField->add("Background", false);
    miTopLayerField->add("Foreground", true);
    miTopLayerField->setOutputPtr(&game_values.toplayer);
    miTopLayerField->setCurrentValue(game_values.toplayer ? 1 : 0);
    miTopLayerField->setAutoAdvance(true);

    //The game always ticks at 62.5 Hz; this only sets how often it is drawn
    game_values.framelimiter = static_cast<short>(GetFrameRateMode(game_values.framelimiter));
    miFrameRateField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 148, "Frame Rate", 500, 220);
    miFrameRateField->add("VSync", static_cast<short>(FrameRateMode::VSync));
    miFrameRateField->add("Uncapped", static_cast<short>(FrameRateMode::Uncapped));
    miFrameRateField->add("62.5 FPS", static_cast<short>(FrameRateMode::TickRate));
    miFrameRateField->setOutputPtr(&game_values.framelimiter);
    miFrameRateField->setCurrentValue(game_values.framelimiter);
    miFrameRateField->setAutoAdvance(true);

    miSmoothMotionField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 184, "Smooth Motion", 500, 220);
    miSmoothMotionField->add("Off", false);
    miSmoothMotionField->add("On", true);
    miSmoothMotionField->setOutputPtr(&game_values.smoothmotion);
    miSmoothMotionField->setCurrentValue(game_values.smoothmotion ? 1 : 0);
    miSmoothMotionField->setAutoAdvance(true);

    //Run-ahead: the match drawn a few ticks ahead, so the buttons answer sooner (GameplayTick.cpp)
    miRunAheadField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 220, "Run-Ahead", 500, 220);
    miRunAheadField->add("Off", 0);
    miRunAheadField->add("1 Frame", 1);
    miRunAheadField->add("2 Frames", 2);
    miRunAheadField->add("3 Frames", 3);
    game_values.runahead = std::clamp<short>(game_values.runahead, 0, 3);
    miRunAheadField->setOutputPtr(&game_values.runahead);
    miRunAheadField->setCurrentValue(game_values.runahead);
    miRunAheadField->setAutoAdvance(true);

    miFullscreenField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 256, "Screen Size", 500, 220);
    miFullscreenField->add("Windowed", false);
    miFullscreenField->add("Fullscreen", true);
    miFullscreenField->setOutputPtr(&game_values.fullscreen);
    miFullscreenField->setCurrentValue(game_values.fullscreen ? 1 : 0);
    miFullscreenField->setAutoAdvance(true);
    miFullscreenField->setItemChangedCode(MENU_CODE_TOGGLE_FULLSCREEN);

    miMenuGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 292, "Menu Graphics", 500, 220, menugraphicspacklist, MENU_CODE_MENU_GRAPHICS_PACK_CHANGED);
    miWorldGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 328, "World Graphics", 500, 220, worldgraphicspacklist, MENU_CODE_WORLD_GRAPHICS_PACK_CHANGED);
    miGameGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 364, "Game Graphics", 500, 220, gamegraphicspacklist, MENU_CODE_GAME_GRAPHICS_PACK_CHANGED);

    //Online matches only (F3 in a match toggles it too)
    miNetStatsField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 400, "Network Stats", 500, 220);
    miNetStatsField->add("Off", false);
    miNetStatsField->add("On", true);
    miNetStatsField->setOutputPtr(&game_values.shownetstats);
    miNetStatsField->setCurrentValue(game_values.shownetstats ? 1 : 0);
    miNetStatsField->setAutoAdvance(true);

    miGraphicsOptionsMenuBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miGraphicsOptionsMenuBackButton->SetCode(MENU_CODE_BACK_TO_OPTIONS_MENU);

    AddControl(miPingBarsField, miGraphicsOptionsMenuBackButton, miOverlayColorsField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miOverlayColorsField, miPingBarsField, miTopLayerField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miTopLayerField, miOverlayColorsField, miFrameRateField, nullptr, miGraphicsOptionsMenuBackButton);

    AddControl(miFrameRateField, miTopLayerField, miSmoothMotionField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miSmoothMotionField, miFrameRateField, miRunAheadField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miRunAheadField, miSmoothMotionField, miFullscreenField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miFullscreenField, miRunAheadField, miMenuGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miMenuGraphicsPackField, miFullscreenField, miWorldGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);

    AddControl(miWorldGraphicsPackField, miMenuGraphicsPackField, miGameGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miGameGraphicsPackField, miWorldGraphicsPackField, miNetStatsField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miNetStatsField, miGameGraphicsPackField, miGraphicsOptionsMenuBackButton, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miGraphicsOptionsMenuBackButton, miNetStatsField, miPingBarsField, miNetStatsField, nullptr);

    AddTitleBar("Graphics Options Menu");

    setInitialFocus(miPingBarsField);
    SetCancelCode(MENU_CODE_BACK_TO_OPTIONS_MENU);
};

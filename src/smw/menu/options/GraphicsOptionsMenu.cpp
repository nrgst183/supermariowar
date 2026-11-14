#include "GraphicsOptionsMenu.h"

#include "FileList.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_PacksField.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"


UI_GraphicsOptionsMenu::UI_GraphicsOptionsMenu()
    : UI_Menu()
{
    miTopLayerField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 120, "Draw Top Layer", 500, 220);
    miTopLayerField->add("Background", false);
    miTopLayerField->add("Foreground", true);
    miTopLayerField->setOutputPtr(&game_values.toplayer);
    miTopLayerField->setCurrentValue(game_values.toplayer ? 1 : 0);
    miTopLayerField->setAutoAdvance(true);

    //The game always ticks at 62.5 Hz; this only sets how often it is drawn
    game_values.framelimiter = static_cast<short>(GetFrameRateMode(game_values.framelimiter));
    miFrameRateField = new MI_SelectField<short>(&rm->spr_selectfield, 70, 160, "Frame Rate", 500, 220);
    miFrameRateField->add("VSync", static_cast<short>(FrameRateMode::VSync));
    miFrameRateField->add("Uncapped", static_cast<short>(FrameRateMode::Uncapped));
    miFrameRateField->add("62.5 FPS", static_cast<short>(FrameRateMode::TickRate));
    miFrameRateField->setOutputPtr(&game_values.framelimiter);
    miFrameRateField->setCurrentValue(game_values.framelimiter);
    miFrameRateField->setAutoAdvance(true);

    miSmoothMotionField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 200, "Smooth Motion", 500, 220);
    miSmoothMotionField->add("Off", false);
    miSmoothMotionField->add("On", true);
    miSmoothMotionField->setOutputPtr(&game_values.smoothmotion);
    miSmoothMotionField->setCurrentValue(game_values.smoothmotion ? 1 : 0);
    miSmoothMotionField->setAutoAdvance(true);

    miFullscreenField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 240, "Screen Size", 500, 220);
    miFullscreenField->add("Windowed", false);
    miFullscreenField->add("Fullscreen", true);
    miFullscreenField->setOutputPtr(&game_values.fullscreen);
    miFullscreenField->setCurrentValue(game_values.fullscreen ? 1 : 0);
    miFullscreenField->setAutoAdvance(true);
    miFullscreenField->setItemChangedCode(MENU_CODE_TOGGLE_FULLSCREEN);

    miMenuGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 280, "Menu Graphics", 500, 220, menugraphicspacklist, MENU_CODE_MENU_GRAPHICS_PACK_CHANGED);
    miWorldGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 320, "World Graphics", 500, 220, worldgraphicspacklist, MENU_CODE_WORLD_GRAPHICS_PACK_CHANGED);
    miGameGraphicsPackField = new MI_PacksField(&rm->spr_selectfield, 70, 360, "Game Graphics", 500, 220, gamegraphicspacklist, MENU_CODE_GAME_GRAPHICS_PACK_CHANGED);

    //Online matches only (F3 in a match toggles it too)
    miNetStatsField = new MI_SelectField<bool>(&rm->spr_selectfield, 70, 400, "Network Stats", 500, 220);
    miNetStatsField->add("Off", false);
    miNetStatsField->add("On", true);
    miNetStatsField->setOutputPtr(&game_values.shownetstats);
    miNetStatsField->setCurrentValue(game_values.shownetstats ? 1 : 0);
    miNetStatsField->setAutoAdvance(true);

    miGraphicsOptionsMenuBackButton = new MI_Button(&rm->spr_selectfield, 544, 432, "Back", 80, TextAlign::CENTER);
    miGraphicsOptionsMenuBackButton->SetCode(MENU_CODE_BACK_TO_OPTIONS_MENU);

    miGraphicsOptionsMenuLeftHeaderBar = new MI_Image(&rm->menu_plain_field, 0, 0, 0, 0, 320, 32, 1, 1, 0);
    miGraphicsOptionsMenuRightHeaderBar = new MI_Image(&rm->menu_plain_field, 320, 0, 192, 0, 320, 32, 1, 1, 0);
    miGraphicsOptionsMenuHeaderText = new MI_HeaderText("Graphics Options Menu", 320, 5);

    AddControl(miTopLayerField, miGraphicsOptionsMenuBackButton, miFrameRateField, nullptr, miGraphicsOptionsMenuBackButton);

    AddControl(miFrameRateField, miTopLayerField, miSmoothMotionField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miSmoothMotionField, miFrameRateField, miFullscreenField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miFullscreenField, miSmoothMotionField, miMenuGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miMenuGraphicsPackField, miFullscreenField, miWorldGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);

    AddControl(miWorldGraphicsPackField, miMenuGraphicsPackField, miGameGraphicsPackField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miGameGraphicsPackField, miWorldGraphicsPackField, miNetStatsField, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miNetStatsField, miGameGraphicsPackField, miGraphicsOptionsMenuBackButton, nullptr, miGraphicsOptionsMenuBackButton);
    AddControl(miGraphicsOptionsMenuBackButton, miNetStatsField, miTopLayerField, miNetStatsField, nullptr);

    AddNonControl(miGraphicsOptionsMenuLeftHeaderBar);
    AddNonControl(miGraphicsOptionsMenuRightHeaderBar);
    AddNonControl(miGraphicsOptionsMenuHeaderText);

    setInitialFocus(miTopLayerField);
    SetCancelCode(MENU_CODE_BACK_TO_OPTIONS_MENU);
};

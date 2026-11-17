#pragma once

#include "uimenu.h"

class MI_Button;
class MI_Image;
class MI_PacksField;
class MI_Text;
template<typename T> class MI_SelectField;

/*
    You can switch between windowed and fullscreen mode,
    as well as change the graphic packs here.
*/
class UI_GraphicsOptionsMenu : public UI_Menu {
public:
    UI_GraphicsOptionsMenu();

private:
    MI_SelectField<bool>* miPingBarsField;
    MI_SelectField<short>* miOverlayColorsField;
    MI_SelectField<bool>* miTopLayerField;
    MI_SelectField<short>* miFrameRateField;
    MI_SelectField<bool>* miSmoothMotionField;
    MI_SelectField<bool>* miFullscreenField;

    MI_PacksField* miMenuGraphicsPackField;
    MI_PacksField* miWorldGraphicsPackField;
    MI_PacksField* miGameGraphicsPackField;
    MI_SelectField<bool>* miNetStatsField;
    MI_Button* miGraphicsOptionsMenuBackButton;
};

#pragma once

#include "MI_MapPreview.h"

#include <memory>
#include <string>

class MI_Image;


class MI_MapField: public MI_MapPreview {
public:
    MI_MapField(gfxSprite* nspr, short x, short y, std::string name, short width, short indent, bool showtags);

    //Called when user selects this control to change it's value
    MenuCodeEnum Modify(bool modify) override;

    //Updates animations or other events every frame
    void Update() override;

    //Draws every frame
    void Draw() override;
    std::array<MI_Image*, 2> ModifyArrows() const override {
        return fModifying ? std::array<MI_Image*, 2> {miModifyImageLeft.get(), miModifyImageRight.get()} : std::array<MI_Image*, 2> {};
    }

    //Sends player input to control on every frame
    MenuCodeEnum SendInput(CPlayerInput * playerInput) override;
    Box FocusBox() const override {
        //(the field's pipe, not the preview under it: its sprite has 8 empty pixels at each end)
        return {static_cast<short>(m_pos.x + 8), m_pos.y, static_cast<short>(iWidth - 16), 32};
    }
    Box ValueBox() const override {
        return {static_cast<short>(m_pos.x + iIndent - 12), m_pos.y, static_cast<short>(iWidth - iIndent + 4), 32};
    }
    bool CanNudge() const override { return !fDisable; }
    MenuCodeEnum Nudge(int dir) override;
    MenuCodeEnum MouseClick(short iMouseX, short iMouseY)  override;

    MenuCodeEnum ChooseRandomMap();

    bool MovePrev(bool fScrollFast);
    bool MoveNext(bool fScrollFast);

    // Loads the current map's preview now (also one that was waiting)
    void LoadCurrentMap();
    void LoadInBackground();

    void SetDimensions(short iWidth, short iIndent) override;

protected:
    bool Move(bool fNext, bool fScrollFast);

    // The current map changed: its name shows at once, and its preview loads
    // now, or while scrolling through the maps, once the scrolling stops
    // (loading a map takes a while, so each step would stall the menu)
    void RequestMapLoad();
    static constexpr short MAP_LOAD_SETTLE_FRAMES = 8;
    short iRecentLoadTimer = 0;   // frames since the last change (counting down)
    short iPendingLoadTimer = 0;  // frames until the waiting load (0: none)

    std::string szName;

    std::unique_ptr<MI_Image> miModifyImageLeft;
    std::unique_ptr<MI_Image> miModifyImageRight;

    short iSlideListOutGoal;

    std::string sSearchString;
    short iSearchStringTimer;

    bool fShowtags;
};

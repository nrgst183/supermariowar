#pragma once

#include "uicontrol.h"

#include <array>
#include <memory>
#include <vector>

constexpr int NUMBONUSITEMSONWHEEL = 10;

class MI_Button;
class MI_Image;


class MI_BonusWheel : public UI_Control {
public:
    // The mouse: the wheel and its Continue (a click: as A, it stops the wheel, then goes on)
    Box FocusBox() const override;
    MI_BonusWheel(short x, short y);
    virtual ~MI_BonusWheel();

    MenuCodeEnum Modify(bool modify) override;
    void Update() override;
    void Draw() override;

    bool GetPowerupSelectionDone() const { return fPowerupSelectionDone; }
    void Reset(bool fTournament);

    // Online (rollback/NetSeries.h): the host decides when the wheel stops.
    // A remote wheel doesn't stop by itself, only by ApplyStop().
    void SetRemote(bool remote) { fRemote = remote; }
    short GetWinningTeam() const { return iWinningTeam; }
    bool IsStopped() const { return fPressedSelect; }
    // The wheel's state when it was stopped (IsStopped())
    void GetStop(float& angle, float& speed, float& winddown, short& powerup) const;
    // Stops the wheel as another one was stopped: it lands on the same powerup
    void ApplyStop(float angle, float speed, float winddown, short powerup);
    // A stopped wheel winds down at once (its powerup is given)
    void Finish();

private:
    short iState = 0;
    short iDisplayPowerupIndex = 0;
    short iDisplayPowerupTimer = 0;

    std::array<std::unique_ptr<MI_Image>, NUMBONUSITEMSONWHEEL> miBonusImages;
    std::vector<std::unique_ptr<MI_Image>> miPlayerImages;

    std::unique_ptr<MI_Button> miContinueButton;

    short iChosenPowerups[NUMBONUSITEMSONWHEEL];

    short iPressSelectTimer = 0;
    bool fPressedSelect = false;
    bool fPowerupSelectionDone = false;

    short iSelectorAnimation = 0;
    short iSelectorAnimationCounter = 0;

    float dSelectionSpeed = 0.f;
    float dSelectionAngle = 0.f;
    short iSelectedPowerup = 0;
    short iNextSelectionSoundIndex = 0;
    float dSelectionSector[NUMBONUSITEMSONWHEEL + 1];

    float dSelectionWinddownSpeed = 0.f;

    float dSelectionSpeedGoal = 0.f;
    short iSelectionSpeedTimer = 0;

    short iNumPlayers = 0;
    short iWinningTeam = 0;

    bool fCpuControlled = false;

    bool fRemote = false;
    float dStopAngle = 0.f;
    float dStopSpeed = 0.f;
};

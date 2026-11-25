#pragma once

#include "GameModeType.h"

#include "uimenu.h"
#include "GameplayStyles.h"
#include "MatchTypes.h"

class MI_Button;
class MI_Image;
class MI_FrenzyModeOptions;
class MI_PowerupSlider;
class MI_SliderField;
class MI_Text;
template<typename T> class MI_SelectField;

class UI_ModeOptionsMenu : public UI_Menu {
public:
    UI_ModeOptionsMenu();

    /// Puts a panel behind each mode's rules (the game sets it, before making the menu; the world
    /// editor has none)
    static void (*addPanel)(UI_Menu& menu, const std::vector<const UI_Control*>& rules);

    void SetRandomGameModeSettings(short iMode);
    void SetControllingTeam(short iControlTeam);

    UI_Menu* GetOptionsMenu(short iMode) {
        return &mModeSettingsMenu[iMode];
    }
    UI_Menu* GetBossOptionsMenu() {
        return &mBossSettingsMenu;
    }

    void HealthModeStartLifeChanged();
    void HealthModeMaxLifeChanged();

    void Refresh();

private:
    // Game Mode Settings Menu
    UI_Menu mModeSettingsMenu[GAMEMODE_LAST];

    UI_Menu mBossSettingsMenu;

    // Classic
    MI_SelectField<DeathStyle>* miClassicModeStyleField;
    MI_SelectField<ScoringStyle>* miClassicModeScoringField;

    // Frag
    MI_SelectField<DeathStyle>* miFragModeStyleField;
    MI_SelectField<ScoringStyle>* miFragModeScoringField;

    // Time Limit
    MI_SelectField<DeathStyle>* miTimeLimitModeStyleField;
    MI_SelectField<ScoringStyle>* miTimeLimitModeScoringField;
    MI_SliderField* miTimeLimitModePercentExtraTime;

    // Jail
    MI_SelectField<JailStyle>* miJailModeStyleField;
    MI_SelectField<bool>* miJailModeTagFreeField;
    MI_SelectField<short>* miJailModeTimeFreeField;
    MI_SliderField* miJailModeJailKeyField;

    // Coins
    MI_SelectField<bool>* miCoinModePenaltyField;
    MI_SelectField<short>* miCoinModeQuantityField;
    MI_SliderField* miCoinModePercentExtraCoin;

    // Yoshi's Eggs
    MI_PowerupSlider* miEggModeEggQuantityField[4];
    MI_PowerupSlider* miEggModeYoshiQuantityField[4];
    MI_SelectField<short>* miEggModeExplosionTimeField;

    // Capture The Flag
    MI_SliderField* miFlagModeSpeedField;
    MI_SelectField<bool>* miFlagModeTouchReturnField;
    MI_SelectField<bool>* miFlagModePointMoveField;
    MI_SelectField<short>* miFlagModeAutoReturnField;
    MI_SelectField<bool>* miFlagModeHomeScoreField;
    MI_SelectField<bool>* miFlagModeCenterFlagField;

    // Chicken
    MI_SelectField<bool>* miChickenModeShowTargetField;
    MI_SelectField<bool>* miChickenModeGlideField;

    // Tag
    MI_SelectField<bool>* miTagModeTagOnTouchField;

    // Star
    MI_SelectField<short>* miStarModeTimeField;
    MI_SelectField<StarStyle>* miStarModeShineField;
    MI_SliderField* miStarModePercentExtraTime;

    // Domination
    MI_SelectField<short>* miDominationModeQuantityField;
    MI_SelectField<bool>* miDominationModeLoseOnDeathField;
    MI_SelectField<bool>* miDominationModeRelocateOnDeathField;
    MI_SelectField<bool>* miDominationModeStealOnDeathField;
    MI_SelectField<short>* miDominationModeRelocateFrequencyField;

    MI_Text* miDominationModeDeathText;

    // King of the Hill
    MI_SelectField<short>* miKingOfTheHillModeSizeField;
    MI_SelectField<short>* miKingOfTheHillModeRelocateFrequencyField;
    MI_SelectField<short>* miKingOfTheHillModeMultiplierField;

    // Race
    MI_SelectField<short>* miRaceModeQuantityField;
    MI_SelectField<short>* miRaceModeSpeedField;
    MI_SelectField<short>* miRaceModePenaltyField;

    // Stomp
    MI_SelectField<short>* miStompModeRateField;
    MI_PowerupSlider* miStompModeEnemySlider[NUMSTOMPENEMIES];

    // Frenzy
    MI_FrenzyModeOptions* miFrenzyModeOptions;

    // Survival
    MI_PowerupSlider* miSurvivalModeEnemySlider[NUMSURVIVALENEMIES];
    MI_SelectField<short>* miSurvivalModeDensityField;
    MI_SelectField<short>* miSurvivalModeSpeedField;
    MI_SelectField<bool>* miSurvivalModeShieldField;

    // Greed
    MI_SelectField<short>* miGreedModeCoinLife;
    MI_SelectField<bool>* miGreedModeOwnCoins;
    MI_SelectField<short>* miGreedModeMultiplier;
    MI_SliderField* miGreedModePercentExtraCoin;

    // Health
    MI_SelectField<short>* miHealthModeStartLife;
    MI_SelectField<short>* miHealthModeMaxLife;
    MI_SliderField* miHealthModePercentExtraLife;

    // Card Collection
    MI_SelectField<short>* miCollectionModeQuantityField;
    MI_SelectField<short>* miCollectionModeRateField;
    MI_SelectField<short>* miCollectionModeBankTimeField;
    MI_SelectField<short>* miCollectionModeCardLifeField;

    // Phanto Chase
    MI_SelectField<short>* miChaseModeSpeedField;
    MI_PowerupSlider* miChaseModeQuantitySlider[3];

    // Shyguy Tag
    MI_SelectField<bool>* miShyGuyTagModeTagOnSuicideField;
    MI_SelectField<short>* miShyGuyTagModeTagOnStompField;
    MI_SelectField<short>* miShyGuyTagModeFreeTimeField;

    MI_SelectField<short>* miMapMakerStartField;
    MI_SelectField<short>* miMapMakerBuildersField;
    MI_SelectField<short>* miMapMakerMapField;

    // Boss
    MI_SelectField<Boss>* miBossModeTypeField;
    MI_SelectField<short>* miBossModeDifficultyField;
    MI_SelectField<short>* miBossModeHitPointsField;
};

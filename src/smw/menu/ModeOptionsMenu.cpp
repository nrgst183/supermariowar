#include "ModeOptionsMenu.h"

#include "GameMode.h"
#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_FrenzyModeOptions.h"
#include "ui/MI_Image.h"
#include "ui/MI_PowerupSlider.h"
#include "ui/MI_Text.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
// The values `from` to `to` (every `step`), labelled with their number; a random setting (a quick
// game) picks one from `goodFrom` to `goodTo`
template <typename Field>
void addNumbers(Field* field, short from, short to, short step, short goodFrom, short goodTo)
{
    for (short value = from; value <= to; value += step)
        field->add(std::to_string(value), value, value >= goodFrom && value <= goodTo);
}

// The bar at the top of the screen, with the menu's title
void addTitleBar(UI_Menu& menu, const char* title)
{
    menu.AddNonControl(UI_Menu::newTitleBarBack());   //(the theme's bar, as on the other menus)
    menu.AddNonControl(new MI_HeaderText(title, 320, 5));
}

// What every mode's menu has: the title bar and the fields in a column (the d-pad goes down it, round
// from the last to the first); B (or the footer's "back") goes back
void finishModeMenu(UI_Menu& menu, const char* title, std::initializer_list<UI_Control*> fields)
{
    const std::vector<UI_Control*> column(fields);
    const size_t n = column.size();
    for (size_t i = 0; i < n; i++)
        menu.AddControl(column[i], column[i == 0 ? n - 1 : i - 1], column[i + 1 == n ? 0 : i + 1], nullptr, nullptr);

    //(the rules in the middle of the screen, between the title bar and the footer: a short page's
    //aren't left low on it, where the original put them; everything on it moves with them)
    int top = 480, bottom = 0;
    for (const UI_Control* field : column) {
        const UI_Control::Box b = field->FocusBox();
        if (b.w <= 0)
            continue;
        top = std::min<int>(top, b.y);
        bottom = std::max<int>(bottom, b.y + b.h);
    }
    if (bottom > top) {
        const int dy = (36 + 444) / 2 - (top + bottom) / 2;
        if (dy < -8 || dy > 8)
            menu.ShiftAll(0, static_cast<short>(dy));
    }
    //(the rules on a panel, as the options screens)
    if (UI_ModeOptionsMenu::addPanel)
        UI_ModeOptionsMenu::addPanel(menu, std::vector<const UI_Control*>(column.begin(), column.end()));

    addTitleBar(menu, title);

    menu.setInitialFocus(column[0]);
    menu.SetCancelCode(MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS);
}
} // namespace

void (*UI_ModeOptionsMenu::addPanel)(UI_Menu& menu, const std::vector<const UI_Control*>& rules) = nullptr;

UI_ModeOptionsMenu::UI_ModeOptionsMenu()
    : UI_Menu()
{
    //***********************
    // Classic Mode Settings
    //***********************

    miClassicModeStyleField = new MI_SelectField<DeathStyle>(&rm->spr_selectfield, 120, 200, "On Kill", 400, 180);
    miClassicModeStyleField->add("Respawn", DeathStyle::Respawn);
    miClassicModeStyleField->add("Shield", DeathStyle::Shield);
    miClassicModeStyleField->setOutputPtr(&game_values.gamemodemenusettings.classic.style);
    miClassicModeStyleField->setCurrentValue(game_values.gamemodemenusettings.classic.style);

    miClassicModeScoringField = new MI_SelectField<ScoringStyle>(&rm->spr_selectfield, 120, 240, "Scoring", 400, 180);
    miClassicModeScoringField->add("All Kills", ScoringStyle::AllKills);
    miClassicModeScoringField->add("Push Kills Only", ScoringStyle::PushOnly, false);
    miClassicModeScoringField->setOutputPtr(&game_values.gamemodemenusettings.classic.scoring);
    miClassicModeScoringField->setCurrentValue(game_values.gamemodemenusettings.classic.scoring);

    finishModeMenu(mModeSettingsMenu[0], "Classic Rules", {miClassicModeStyleField, miClassicModeScoringField});

    //***********************
    // Frag Mode Settings
    //***********************

    miFragModeStyleField = new MI_SelectField<DeathStyle>(&rm->spr_selectfield, 120, 200, "On Kill", 400, 180);
    miFragModeStyleField->add("Respawn", DeathStyle::Respawn);
    miFragModeStyleField->add("Shield", DeathStyle::Shield);
    miFragModeStyleField->setOutputPtr(&game_values.gamemodemenusettings.frag.style);
    miFragModeStyleField->setCurrentValue(game_values.gamemodemenusettings.frag.style);

    miFragModeScoringField = new MI_SelectField<ScoringStyle>(&rm->spr_selectfield, 120, 240, "Scoring", 400, 180);
    miFragModeScoringField->add("All Kills", ScoringStyle::AllKills);
    miFragModeScoringField->add("Push Kills Only", ScoringStyle::PushOnly, false);
    miFragModeScoringField->setOutputPtr(&game_values.gamemodemenusettings.frag.scoring);
    miFragModeScoringField->setCurrentValue(game_values.gamemodemenusettings.frag.scoring);

    finishModeMenu(mModeSettingsMenu[1], "Frag Rules", {miFragModeStyleField, miFragModeScoringField});

    //***********************
    // Time Limit Mode Settings
    //***********************

    miTimeLimitModeStyleField = new MI_SelectField<DeathStyle>(&rm->spr_selectfield, 120, 180, "On Kill", 400, 150);
    miTimeLimitModeStyleField->add("Respawn", DeathStyle::Respawn);
    miTimeLimitModeStyleField->add("Shield", DeathStyle::Shield);
    miTimeLimitModeStyleField->setOutputPtr(&game_values.gamemodemenusettings.time.style);
    miTimeLimitModeStyleField->setCurrentValue(game_values.gamemodemenusettings.time.style);

    miTimeLimitModeScoringField = new MI_SelectField<ScoringStyle>(&rm->spr_selectfield, 120, 220, "Scoring", 400, 150);
    miTimeLimitModeScoringField->add("All Kills", ScoringStyle::AllKills);
    miTimeLimitModeScoringField->add("Push Kills Only", ScoringStyle::PushOnly, false);
    miTimeLimitModeScoringField->setOutputPtr(&game_values.gamemodemenusettings.time.scoring);
    miTimeLimitModeScoringField->setCurrentValue(game_values.gamemodemenusettings.time.scoring);

    miTimeLimitModePercentExtraTime = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 260, "Extra Time", 400, 150, 384);
    addNumbers(miTimeLimitModePercentExtraTime, 0, 100, 5, 10, 50);
    miTimeLimitModePercentExtraTime->setOutputPtr(&game_values.gamemodemenusettings.time.percentextratime);
    miTimeLimitModePercentExtraTime->setCurrentValue(game_values.gamemodemenusettings.time.percentextratime);
    miTimeLimitModePercentExtraTime->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[2], "Time Rules", {miTimeLimitModeStyleField, miTimeLimitModeScoringField, miTimeLimitModePercentExtraTime});

    //***********************
    // Jail Mode Settings
    //***********************

    miJailModeStyleField = new MI_SelectField<JailStyle>(&rm->spr_selectfield, 120, 160, "Style", 400, 150);
    miJailModeStyleField->add("Classic", JailStyle::Classic);
    miJailModeStyleField->add("Owned", JailStyle::Owned);
    miJailModeStyleField->add("Free For All", JailStyle::FreeForAll);
    miJailModeStyleField->setOutputPtr(&game_values.gamemodemenusettings.jail.style);
    miJailModeStyleField->setCurrentValue(game_values.gamemodemenusettings.jail.style);

    miJailModeTimeFreeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 200, "Free Timer", 400, 150);
    miJailModeTimeFreeField->add("None", 1);
    miJailModeTimeFreeField->add("5 Seconds", 310, false);
    miJailModeTimeFreeField->add("10 Seconds", 620);
    miJailModeTimeFreeField->add("15 Seconds", 930);
    miJailModeTimeFreeField->add("20 Seconds", 1240);
    miJailModeTimeFreeField->add("25 Seconds", 1550);
    miJailModeTimeFreeField->add("30 Seconds", 1860);
    miJailModeTimeFreeField->add("35 Seconds", 2170);
    miJailModeTimeFreeField->add("40 Seconds", 2480);
    miJailModeTimeFreeField->add("45 Seconds", 2790, false);
    miJailModeTimeFreeField->add("50 Seconds", 3100, false);
    miJailModeTimeFreeField->add("55 Seconds", 3410, false);
    miJailModeTimeFreeField->add("60 Seconds", 3720, false);
    miJailModeTimeFreeField->setOutputPtr(&game_values.gamemodemenusettings.jail.timetofree);
    miJailModeTimeFreeField->setCurrentValue(game_values.gamemodemenusettings.jail.timetofree);

    miJailModeTagFreeField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 240, "Tag Free", 400, 150);
    miJailModeTagFreeField->add("Off", false);
    miJailModeTagFreeField->add("On", true);
    miJailModeTagFreeField->setOutputPtr(&game_values.gamemodemenusettings.jail.tagfree);
    miJailModeTagFreeField->setCurrentValue(game_values.gamemodemenusettings.jail.tagfree ? 1 : 0);
    miJailModeTagFreeField->setAutoAdvance(true);

    miJailModeJailKeyField = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 280, "Jail Key", 400, 150, 384);
    addNumbers(miJailModeJailKeyField, 0, 100, 5, 10, 50);
    miJailModeJailKeyField->setOutputPtr(&game_values.gamemodemenusettings.jail.percentkey);
    miJailModeJailKeyField->setCurrentValue(game_values.gamemodemenusettings.jail.percentkey);
    miJailModeJailKeyField->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[3], "Jail Rules", {miJailModeStyleField, miJailModeTimeFreeField, miJailModeTagFreeField, miJailModeJailKeyField});

    //***********************
    // Coins Mode Settings
    //***********************

    miCoinModePenaltyField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 180, "Penalty", 400, 150);
    miCoinModePenaltyField->add("Off", false);
    miCoinModePenaltyField->add("On", true);
    miCoinModePenaltyField->setOutputPtr(&game_values.gamemodemenusettings.coins.penalty);
    miCoinModePenaltyField->setCurrentValue(game_values.gamemodemenusettings.coins.penalty ? 1 : 0);
    miCoinModePenaltyField->setAutoAdvance(true);

    miCoinModeQuantityField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Quantity", 400, 150);
    addNumbers(miCoinModeQuantityField, 1, 10, 1, 1, 10);
    miCoinModeQuantityField->setOutputPtr(&game_values.gamemodemenusettings.coins.quantity);
    miCoinModeQuantityField->setCurrentValue(game_values.gamemodemenusettings.coins.quantity);

    miCoinModePercentExtraCoin = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 260, "Extra Coins", 400, 150, 384);
    addNumbers(miCoinModePercentExtraCoin, 0, 100, 5, 10, 50);
    miCoinModePercentExtraCoin->setOutputPtr(&game_values.gamemodemenusettings.coins.percentextracoin);
    miCoinModePercentExtraCoin->setCurrentValue(game_values.gamemodemenusettings.coins.percentextracoin);
    miCoinModePercentExtraCoin->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[4], "Coin Collection Rules", {miCoinModePenaltyField, miCoinModeQuantityField, miCoinModePercentExtraCoin});

    //***********************
    // Stomp Mode Settings
    //***********************

    miStompModeRateField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 40, "Rate", 400, 180);
    miStompModeRateField->add("Very Slow", 150, false);
    miStompModeRateField->add("Slow", 120);
    miStompModeRateField->add("Moderate", 90);
    miStompModeRateField->add("Fast", 60);
    miStompModeRateField->add("Very Fast", 30, false);
    miStompModeRateField->add("Extremely Fast", 15, false);
    miStompModeRateField->add("Insanely Fast", 5, false);
    miStompModeRateField->setOutputPtr(&game_values.gamemodemenusettings.stomp.rate);
    miStompModeRateField->setCurrentValue(game_values.gamemodemenusettings.stomp.rate);

    for (short iEnemy = 0; iEnemy < NUMSTOMPENEMIES; iEnemy++) {
        miStompModeEnemySlider[iEnemy] = new MI_PowerupSlider(&rm->spr_selectfield, &rm->menu_slider_bar, &rm->menu_stomp, 120, 80 + 40 * iEnemy, 400, iEnemy);
        miStompModeEnemySlider[iEnemy]->add("", 0);
        miStompModeEnemySlider[iEnemy]->add("", 1);
        miStompModeEnemySlider[iEnemy]->add("", 2);
        miStompModeEnemySlider[iEnemy]->add("", 3);
        miStompModeEnemySlider[iEnemy]->add("", 4);
        miStompModeEnemySlider[iEnemy]->add("", 5);
        miStompModeEnemySlider[iEnemy]->add("", 6);
        miStompModeEnemySlider[iEnemy]->add("", 7);
        miStompModeEnemySlider[iEnemy]->add("", 8);
        miStompModeEnemySlider[iEnemy]->add("", 9);
        miStompModeEnemySlider[iEnemy]->add("", 10);
        miStompModeEnemySlider[iEnemy]->allowWrap(false);
        miStompModeEnemySlider[iEnemy]->setOutputPtr(&game_values.gamemodemenusettings.stomp.enemyweight[iEnemy]);
        miStompModeEnemySlider[iEnemy]->setCurrentValue(game_values.gamemodemenusettings.stomp.enemyweight[iEnemy]);
    }

    finishModeMenu(mModeSettingsMenu[5], "Stomp Rules", {
        miStompModeRateField, miStompModeEnemySlider[0], miStompModeEnemySlider[1],
        miStompModeEnemySlider[2], miStompModeEnemySlider[3], miStompModeEnemySlider[4],
        miStompModeEnemySlider[5], miStompModeEnemySlider[6], miStompModeEnemySlider[7],
        miStompModeEnemySlider[8]
    });

    //***********************
    // Yoshi's Eggs Mode Settings
    //***********************

    for (short iEggField = 0; iEggField < 4; iEggField++) {
        miEggModeEggQuantityField[iEggField] = new MI_PowerupSlider(&rm->spr_selectfield, &rm->menu_slider_bar, &rm->menu_egg, 170, 60 + 40 * iEggField, 300, iEggField);
        miEggModeEggQuantityField[iEggField]->add("0", 0, iEggField == 0 ? false : true);
        miEggModeEggQuantityField[iEggField]->add("1", 1, iEggField >= 2 ? false : true);
        miEggModeEggQuantityField[iEggField]->add("2", 2, iEggField >= 2 ? false : true);
        miEggModeEggQuantityField[iEggField]->add("3", 3, iEggField >= 1 ? false : true);
        miEggModeEggQuantityField[iEggField]->add("4", 4);
        miEggModeEggQuantityField[iEggField]->setOutputPtr(&game_values.gamemodemenusettings.egg.eggs[iEggField]);
        miEggModeEggQuantityField[iEggField]->setCurrentValue(game_values.gamemodemenusettings.egg.eggs[iEggField]);
        miEggModeEggQuantityField[iEggField]->allowWrap(false);
    }

    for (short iYoshiField = 0; iYoshiField < 4; iYoshiField++) {
        miEggModeYoshiQuantityField[iYoshiField] = new MI_PowerupSlider(&rm->spr_selectfield, &rm->menu_slider_bar, &rm->menu_egg, 170, 220 + 40 * iYoshiField, 300, iYoshiField + 4);
        miEggModeYoshiQuantityField[iYoshiField]->add("0", 0, iYoshiField == 0 ? false : true);
        miEggModeYoshiQuantityField[iYoshiField]->add("1", 1, iYoshiField >= 2 ? false : true);
        miEggModeYoshiQuantityField[iYoshiField]->add("2", 2, iYoshiField >= 1 ? false : true);
        miEggModeYoshiQuantityField[iYoshiField]->add("3", 3);
        miEggModeYoshiQuantityField[iYoshiField]->add("4", 4);
        miEggModeYoshiQuantityField[iYoshiField]->setOutputPtr(&game_values.gamemodemenusettings.egg.yoshis[iYoshiField]);
        miEggModeYoshiQuantityField[iYoshiField]->setCurrentValue(game_values.gamemodemenusettings.egg.yoshis[iYoshiField]);
        miEggModeYoshiQuantityField[iYoshiField]->allowWrap(false);
    }

    miEggModeExplosionTimeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 380, "Explosion Timer", 400, 220);
    miEggModeExplosionTimeField->add("Off", 0);
    miEggModeExplosionTimeField->add("3 Seconds", 3, false);
    miEggModeExplosionTimeField->add("5 Seconds", 5);
    miEggModeExplosionTimeField->add("8 Seconds", 8);
    miEggModeExplosionTimeField->add("10 Seconds", 10);
    miEggModeExplosionTimeField->add("15 Seconds", 15);
    miEggModeExplosionTimeField->add("20 Seconds", 20);
    miEggModeExplosionTimeField->setOutputPtr(&game_values.gamemodemenusettings.egg.explode);
    miEggModeExplosionTimeField->setCurrentValue(game_values.gamemodemenusettings.egg.explode);

    finishModeMenu(mModeSettingsMenu[6], "Yoshi's Eggs Rules", {
        miEggModeEggQuantityField[0], miEggModeEggQuantityField[1], miEggModeEggQuantityField[2],
        miEggModeEggQuantityField[3], miEggModeYoshiQuantityField[0], miEggModeYoshiQuantityField[1],
        miEggModeYoshiQuantityField[2], miEggModeYoshiQuantityField[3], miEggModeExplosionTimeField
    });

    //***********************
    // Flag Mode Settings
    //***********************

    miFlagModeSpeedField = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 120, "Speed", 400, 180, 380);
    miFlagModeSpeedField->add("-", -1);
    miFlagModeSpeedField->add("0", 0);
    miFlagModeSpeedField->add("1", 1);
    miFlagModeSpeedField->add("2", 2);
    miFlagModeSpeedField->add("3", 3);
    miFlagModeSpeedField->add("4", 4);
    miFlagModeSpeedField->add("5", 5);
    miFlagModeSpeedField->add("6", 6, false);
    miFlagModeSpeedField->add("7", 7, false);
    miFlagModeSpeedField->add("8", 8, false);
    miFlagModeSpeedField->setOutputPtr(&game_values.gamemodemenusettings.flag.speed);
    miFlagModeSpeedField->setCurrentValue(game_values.gamemodemenusettings.flag.speed);
    miFlagModeSpeedField->allowWrap(false);

    miFlagModeTouchReturnField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 160, "Touch Return", 400, 180);
    miFlagModeTouchReturnField->add("Off", false);
    miFlagModeTouchReturnField->add("On", true);
    miFlagModeTouchReturnField->setOutputPtr(&game_values.gamemodemenusettings.flag.touchreturn);
    miFlagModeTouchReturnField->setCurrentValue(game_values.gamemodemenusettings.flag.touchreturn ? 1 : 0);
    miFlagModeTouchReturnField->setAutoAdvance(true);

    miFlagModePointMoveField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 200, "Point Move", 400, 180);
    miFlagModePointMoveField->add("Off", false);
    miFlagModePointMoveField->add("On", true);
    miFlagModePointMoveField->setOutputPtr(&game_values.gamemodemenusettings.flag.pointmove);
    miFlagModePointMoveField->setCurrentValue(game_values.gamemodemenusettings.flag.pointmove ? 1 : 0);
    miFlagModePointMoveField->setAutoAdvance(true);

    miFlagModeAutoReturnField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 240, "Auto Return", 400, 180);
    miFlagModeAutoReturnField->add("None", 0);
    miFlagModeAutoReturnField->add("5 Seconds", 310, false);
    miFlagModeAutoReturnField->add("10 Seconds", 620);
    miFlagModeAutoReturnField->add("15 Seconds", 930, false);
    miFlagModeAutoReturnField->add("20 Seconds", 1240);
    miFlagModeAutoReturnField->add("25 Seconds", 1550, false);
    miFlagModeAutoReturnField->add("30 Seconds", 1860, false);
    miFlagModeAutoReturnField->add("35 Seconds", 2170, false);
    miFlagModeAutoReturnField->add("40 Seconds", 2480, false);
    miFlagModeAutoReturnField->add("45 Seconds", 2790, false);
    miFlagModeAutoReturnField->add("50 Seconds", 3100, false);
    miFlagModeAutoReturnField->add("55 Seconds", 3410, false);
    miFlagModeAutoReturnField->add("60 Seconds", 3720, false);
    miFlagModeAutoReturnField->setOutputPtr(&game_values.gamemodemenusettings.flag.autoreturn);
    miFlagModeAutoReturnField->setCurrentValue(game_values.gamemodemenusettings.flag.autoreturn);

    miFlagModeHomeScoreField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 280, "Need Home", 400, 180);
    miFlagModeHomeScoreField->add("Off", false);
    miFlagModeHomeScoreField->add("On", true);
    miFlagModeHomeScoreField->setOutputPtr(&game_values.gamemodemenusettings.flag.homescore);
    miFlagModeHomeScoreField->setCurrentValue(game_values.gamemodemenusettings.flag.homescore ? 1 : 0);
    miFlagModeHomeScoreField->setAutoAdvance(true);

    miFlagModeCenterFlagField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 320, "Center Flag", 400, 180);
    miFlagModeCenterFlagField->add("Off", false);
    miFlagModeCenterFlagField->add("On", true);
    miFlagModeCenterFlagField->setOutputPtr(&game_values.gamemodemenusettings.flag.centerflag);
    miFlagModeCenterFlagField->setCurrentValue(game_values.gamemodemenusettings.flag.centerflag ? 1 : 0);
    miFlagModeCenterFlagField->setAutoAdvance(true);

    finishModeMenu(mModeSettingsMenu[7], "Capture The Flag Rules", {
        miFlagModeSpeedField, miFlagModeTouchReturnField, miFlagModePointMoveField,
        miFlagModeAutoReturnField, miFlagModeHomeScoreField, miFlagModeCenterFlagField
    });

    //***********************
    // Chicken Mode Settings
    //***********************

    miChickenModeShowTargetField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 200, "Show Target", 400, 180);
    miChickenModeShowTargetField->add("Off", false);
    miChickenModeShowTargetField->add("On", true);
    miChickenModeShowTargetField->setOutputPtr(&game_values.gamemodemenusettings.chicken.usetarget);
    miChickenModeShowTargetField->setCurrentValue(game_values.gamemodemenusettings.chicken.usetarget ? 1 : 0);
    miChickenModeShowTargetField->setAutoAdvance(true);

    miChickenModeGlideField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 240, "Chicken Glide", 400, 180);
    miChickenModeGlideField->add("Off", false);
    miChickenModeGlideField->add("On", true);
    miChickenModeGlideField->setOutputPtr(&game_values.gamemodemenusettings.chicken.glide);
    miChickenModeGlideField->setCurrentValue(game_values.gamemodemenusettings.chicken.glide ? 1 : 0);
    miChickenModeGlideField->setAutoAdvance(true);

    finishModeMenu(mModeSettingsMenu[8], "Chicken Rules", {miChickenModeShowTargetField, miChickenModeGlideField});

    //***********************
    // Tag Mode Settings
    //***********************

    miTagModeTagOnTouchField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 220, "Touch Tag", 400, 180);
    miTagModeTagOnTouchField->add("Off", false);
    miTagModeTagOnTouchField->add("On", true);
    miTagModeTagOnTouchField->setOutputPtr(&game_values.gamemodemenusettings.tag.tagontouch);
    miTagModeTagOnTouchField->setCurrentValue(game_values.gamemodemenusettings.tag.tagontouch ? 1 : 0);
    miTagModeTagOnTouchField->setAutoAdvance(true);

    finishModeMenu(mModeSettingsMenu[9], "Tag Rules", {miTagModeTagOnTouchField});

    //***********************
    // Star Mode Settings
    //***********************

    miStarModeTimeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 180, "Time", 400, 150);
    miStarModeTimeField->add("5 Seconds", 5, false);
    miStarModeTimeField->add("10 Seconds", 10, false);
    miStarModeTimeField->add("15 Seconds", 15);
    miStarModeTimeField->add("20 Seconds", 20);
    miStarModeTimeField->add("25 Seconds", 25);
    miStarModeTimeField->add("30 Seconds", 30);
    miStarModeTimeField->add("35 Seconds", 35);
    miStarModeTimeField->add("40 Seconds", 40);
    miStarModeTimeField->add("45 Seconds", 45, false);
    miStarModeTimeField->add("50 Seconds", 50, false);
    miStarModeTimeField->add("55 Seconds", 55, false);
    miStarModeTimeField->add("60 Seconds", 60, false);
    miStarModeTimeField->setOutputPtr(&game_values.gamemodemenusettings.star.time);
    miStarModeTimeField->setCurrentValue(game_values.gamemodemenusettings.star.time);

    miStarModeShineField = new MI_SelectField<StarStyle>(&rm->spr_selectfield, 120, 220, "Star Type", 400, 150);
    miStarModeShineField->add("Ztar", StarStyle::Ztar);
    miStarModeShineField->add("Shine", StarStyle::Shine);
    miStarModeShineField->add("Multi Star", StarStyle::Multi);
    miStarModeShineField->add("Random", StarStyle::Random);
    miStarModeShineField->setOutputPtr(&game_values.gamemodemenusettings.star.shine);
    miStarModeShineField->setCurrentValue(game_values.gamemodemenusettings.star.shine);

    miStarModePercentExtraTime = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 260, "Extra Time", 400, 150, 384);
    addNumbers(miStarModePercentExtraTime, 0, 100, 5, 10, 50);
    miStarModePercentExtraTime->setOutputPtr(&game_values.gamemodemenusettings.star.percentextratime);
    miStarModePercentExtraTime->setCurrentValue(game_values.gamemodemenusettings.star.percentextratime);
    miStarModePercentExtraTime->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[10], "Star Rules", {miStarModeTimeField, miStarModeShineField, miStarModePercentExtraTime});

    //***********************
    // Domination Mode Settings
    //***********************

    miDominationModeQuantityField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 120, "Quantity", 400, 180);
    miDominationModeQuantityField->add("1 Base", 1, false);
    miDominationModeQuantityField->add("2 Bases", 2, false);
    miDominationModeQuantityField->add("3 Bases", 3);
    miDominationModeQuantityField->add("4 Bases", 4);
    miDominationModeQuantityField->add("5 Bases", 5);
    miDominationModeQuantityField->add("6 Bases", 6, false);
    miDominationModeQuantityField->add("7 Bases", 7, false);
    miDominationModeQuantityField->add("8 Bases", 8, false);
    miDominationModeQuantityField->add("9 Bases", 9, false);
    miDominationModeQuantityField->add("10 Bases", 10, false);
    miDominationModeQuantityField->add("# Players - 1", 11, false);
    miDominationModeQuantityField->add("# Players", 12, false);
    miDominationModeQuantityField->add("# Players + 1", 13);
    miDominationModeQuantityField->add("# Players + 2", 14);
    miDominationModeQuantityField->add("# Players + 3", 15);
    miDominationModeQuantityField->add("# Players + 4", 16);
    miDominationModeQuantityField->add("# Players + 5", 17);
    miDominationModeQuantityField->add("# Players + 6", 18, false);
    miDominationModeQuantityField->add("2x Players - 3", 19, false);
    miDominationModeQuantityField->add("2x Players - 2", 20, false);
    miDominationModeQuantityField->add("2x Players - 1", 21);
    miDominationModeQuantityField->add("2x Players", 22);
    miDominationModeQuantityField->add("2x Players + 1", 23, false);
    miDominationModeQuantityField->add("2x Players + 2", 24, false);
    miDominationModeQuantityField->setOutputPtr(&game_values.gamemodemenusettings.domination.quantity);
    miDominationModeQuantityField->setCurrentValue(game_values.gamemodemenusettings.domination.quantity);

    miDominationModeRelocateFrequencyField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 160, "Relocate", 400, 180);
    miDominationModeRelocateFrequencyField->add("Never", 0);
    miDominationModeRelocateFrequencyField->add("5 Seconds", 310, false);
    miDominationModeRelocateFrequencyField->add("10 Seconds", 620);
    miDominationModeRelocateFrequencyField->add("15 Seconds", 930);
    miDominationModeRelocateFrequencyField->add("20 Seconds", 1240);
    miDominationModeRelocateFrequencyField->add("30 Seconds", 1860);
    miDominationModeRelocateFrequencyField->add("45 Seconds", 2790);
    miDominationModeRelocateFrequencyField->add("1 Minute", 3720);
    miDominationModeRelocateFrequencyField->add("1.5 Minutes", 5580);
    miDominationModeRelocateFrequencyField->add("2 Minutes", 7440);
    miDominationModeRelocateFrequencyField->add("2.5 Minutes", 9300);
    miDominationModeRelocateFrequencyField->add("3 Minutes", 11160);
    miDominationModeRelocateFrequencyField->setOutputPtr(&game_values.gamemodemenusettings.domination.relocationfrequency);
    miDominationModeRelocateFrequencyField->setCurrentValue(game_values.gamemodemenusettings.domination.relocationfrequency);

    miDominationModeDeathText = new MI_Text("On Death", 120, 210, 0, true, TextAlign::LEFT);

    miDominationModeLoseOnDeathField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 240, "Lose Bases", 400, 180);
    miDominationModeLoseOnDeathField->add("Off", false);
    miDominationModeLoseOnDeathField->add("On", true);
    miDominationModeLoseOnDeathField->setOutputPtr(&game_values.gamemodemenusettings.domination.loseondeath);
    miDominationModeLoseOnDeathField->setCurrentValue(game_values.gamemodemenusettings.domination.loseondeath ? 1 : 0);
    miDominationModeLoseOnDeathField->setAutoAdvance(true);

    miDominationModeRelocateOnDeathField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 280, "Move Bases", 400, 180);
    miDominationModeRelocateOnDeathField->add("Off", false);
    miDominationModeRelocateOnDeathField->add("On", true);
    miDominationModeRelocateOnDeathField->setOutputPtr(&game_values.gamemodemenusettings.domination.relocateondeath);
    miDominationModeRelocateOnDeathField->setCurrentValue(game_values.gamemodemenusettings.domination.relocateondeath ? 1 : 0);
    miDominationModeRelocateOnDeathField->setAutoAdvance(true);

    miDominationModeStealOnDeathField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 320, "Steal Bases", 400, 180);
    miDominationModeStealOnDeathField->add("Off", false);
    miDominationModeStealOnDeathField->add("On", true);
    miDominationModeStealOnDeathField->setOutputPtr(&game_values.gamemodemenusettings.domination.stealondeath);
    miDominationModeStealOnDeathField->setCurrentValue(game_values.gamemodemenusettings.domination.stealondeath ? 1 : 0);
    miDominationModeStealOnDeathField->setAutoAdvance(true);

    finishModeMenu(mModeSettingsMenu[11], "Domination Rules", {miDominationModeQuantityField, miDominationModeRelocateFrequencyField,
        miDominationModeLoseOnDeathField, miDominationModeRelocateOnDeathField, miDominationModeStealOnDeathField});
    mModeSettingsMenu[11].AddNonControl(miDominationModeDeathText);

    //***********************
    // King of the Hill Mode Settings
    //***********************

    miKingOfTheHillModeSizeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 180, "Size", 400, 180);
    miKingOfTheHillModeSizeField->add("2 x 2", 2);
    miKingOfTheHillModeSizeField->add("3 x 3", 3);
    miKingOfTheHillModeSizeField->add("4 x 4", 4);
    miKingOfTheHillModeSizeField->add("5 x 5", 5);
    miKingOfTheHillModeSizeField->add("6 x 6", 6);
    miKingOfTheHillModeSizeField->add("7 x 7", 7);
    miKingOfTheHillModeSizeField->setOutputPtr(&game_values.gamemodemenusettings.kingofthehill.areasize);
    miKingOfTheHillModeSizeField->setCurrentValue(game_values.gamemodemenusettings.kingofthehill.areasize);

    miKingOfTheHillModeRelocateFrequencyField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Relocate", 400, 180);
    miKingOfTheHillModeRelocateFrequencyField->add("Never", 0);
    miKingOfTheHillModeRelocateFrequencyField->add("5 Seconds", 310, false);
    miKingOfTheHillModeRelocateFrequencyField->add("10 Seconds", 620);
    miKingOfTheHillModeRelocateFrequencyField->add("15 Seconds", 930);
    miKingOfTheHillModeRelocateFrequencyField->add("20 Seconds", 1240);
    miKingOfTheHillModeRelocateFrequencyField->add("30 Seconds", 1860);
    miKingOfTheHillModeRelocateFrequencyField->add("45 Seconds", 2790);
    miKingOfTheHillModeRelocateFrequencyField->add("1 Minute", 3720);
    miKingOfTheHillModeRelocateFrequencyField->add("1.5 Minutes", 5580);
    miKingOfTheHillModeRelocateFrequencyField->add("2 Minutes", 7440);
    miKingOfTheHillModeRelocateFrequencyField->add("2.5 Minutes", 9300);
    miKingOfTheHillModeRelocateFrequencyField->add("3 Minutes", 11160);
    miKingOfTheHillModeRelocateFrequencyField->setOutputPtr(&game_values.gamemodemenusettings.kingofthehill.relocationfrequency);
    miKingOfTheHillModeRelocateFrequencyField->setCurrentValue(game_values.gamemodemenusettings.kingofthehill.relocationfrequency);

    miKingOfTheHillModeMultiplierField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 260, "Max Multiplier", 400, 180);
    miKingOfTheHillModeMultiplierField->add("None", 1);
    miKingOfTheHillModeMultiplierField->add("2", 2);
    miKingOfTheHillModeMultiplierField->add("3", 3);
    miKingOfTheHillModeMultiplierField->add("4", 4);
    miKingOfTheHillModeMultiplierField->add("5", 5);
    miKingOfTheHillModeMultiplierField->setOutputPtr(&game_values.gamemodemenusettings.kingofthehill.maxmultiplier);
    miKingOfTheHillModeMultiplierField->setCurrentValue(game_values.gamemodemenusettings.kingofthehill.maxmultiplier);

    finishModeMenu(mModeSettingsMenu[12], "King of the Hill Rules", {
        miKingOfTheHillModeSizeField, miKingOfTheHillModeRelocateFrequencyField,
        miKingOfTheHillModeMultiplierField
    });

    //***********************
    // Race Mode Settings
    //***********************

    miRaceModeQuantityField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 180, "Quantity", 400, 180);
    miRaceModeQuantityField->add("2", 2, false);
    miRaceModeQuantityField->add("3", 3);
    miRaceModeQuantityField->add("4", 4);
    miRaceModeQuantityField->add("5", 5);
    miRaceModeQuantityField->add("6", 6);
    miRaceModeQuantityField->add("7", 7);
    miRaceModeQuantityField->add("8", MAXRACEGOALS);
    miRaceModeQuantityField->setOutputPtr(&game_values.gamemodemenusettings.race.quantity);
    miRaceModeQuantityField->setCurrentValue(game_values.gamemodemenusettings.race.quantity);

    miRaceModeSpeedField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Speed", 400, 180);
    miRaceModeSpeedField->add("Stationary", 0);
    miRaceModeSpeedField->add("Very Slow", 2);
    miRaceModeSpeedField->add("Slow", 3);
    miRaceModeSpeedField->add("Moderate", 4);
    miRaceModeSpeedField->add("Fast", 6);
    miRaceModeSpeedField->add("Very Fast", 8, false);
    miRaceModeSpeedField->add("Extremely Fast", 15, false);
    miRaceModeSpeedField->add("Insanely Fast", 30, false);
    miRaceModeSpeedField->setOutputPtr(&game_values.gamemodemenusettings.race.speed);
    miRaceModeSpeedField->setCurrentValue(game_values.gamemodemenusettings.race.speed);

    miRaceModePenaltyField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 260, "Penalty", 400, 180);
    miRaceModePenaltyField->add("None", 0);
    miRaceModePenaltyField->add("One Goal", 1);
    miRaceModePenaltyField->add("All Goals", 2);
    miRaceModePenaltyField->setOutputPtr(&game_values.gamemodemenusettings.race.penalty);
    miRaceModePenaltyField->setCurrentValue(game_values.gamemodemenusettings.race.penalty);

    finishModeMenu(mModeSettingsMenu[13], "Race Rules", {miRaceModeQuantityField, miRaceModeSpeedField, miRaceModePenaltyField});

    //***********************
    // Frenzy Mode Settings
    //***********************

    miFrenzyModeOptions = new MI_FrenzyModeOptions(50, 44, 640, 7);
    miFrenzyModeOptions->SetAutoModify(true);

    //(its own Back button: a single control)
    mModeSettingsMenu[15].AddControl(miFrenzyModeOptions, nullptr, nullptr, nullptr, nullptr);
    addTitleBar(mModeSettingsMenu[15], "Frenzy Rules");
    mModeSettingsMenu[15].setInitialFocus(miFrenzyModeOptions);
    mModeSettingsMenu[15].SetCancelCode(MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS);

    //***********************
    // Survival Mode Settings
    //***********************

    for (short iEnemy = 0; iEnemy < NUMSURVIVALENEMIES; iEnemy++) {
        miSurvivalModeEnemySlider[iEnemy] = new MI_PowerupSlider(&rm->spr_selectfield, &rm->menu_slider_bar, &rm->menu_survival, 120, 120 + 40 * iEnemy, 400, iEnemy);
        miSurvivalModeEnemySlider[iEnemy]->add("", 0);
        miSurvivalModeEnemySlider[iEnemy]->add("", 1);
        miSurvivalModeEnemySlider[iEnemy]->add("", 2);
        miSurvivalModeEnemySlider[iEnemy]->add("", 3);
        miSurvivalModeEnemySlider[iEnemy]->add("", 4);
        miSurvivalModeEnemySlider[iEnemy]->add("", 5);
        miSurvivalModeEnemySlider[iEnemy]->add("", 6);
        miSurvivalModeEnemySlider[iEnemy]->add("", 7);
        miSurvivalModeEnemySlider[iEnemy]->add("", 8);
        miSurvivalModeEnemySlider[iEnemy]->add("", 9);
        miSurvivalModeEnemySlider[iEnemy]->add("", 10);
        miSurvivalModeEnemySlider[iEnemy]->allowWrap(false);
        miSurvivalModeEnemySlider[iEnemy]->setOutputPtr(&game_values.gamemodemenusettings.survival.enemyweight[iEnemy]);
        miSurvivalModeEnemySlider[iEnemy]->setCurrentValue(game_values.gamemodemenusettings.survival.enemyweight[iEnemy]);
    }

    miSurvivalModeDensityField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 240, "Density", 400, 180);
    miSurvivalModeDensityField->add("Very Low", 40, false);
    miSurvivalModeDensityField->add("Low", 30);
    miSurvivalModeDensityField->add("Medium", 20);
    miSurvivalModeDensityField->add("High", 15);
    miSurvivalModeDensityField->add("Very High", 10, false);
    miSurvivalModeDensityField->add("Extremely High", 6);
    miSurvivalModeDensityField->add("Insanely High", 2, false);
    miSurvivalModeDensityField->setOutputPtr(&game_values.gamemodemenusettings.survival.density);
    miSurvivalModeDensityField->setCurrentValue(game_values.gamemodemenusettings.survival.density);

    miSurvivalModeSpeedField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 280, "Speed", 400, 180);
    miSurvivalModeSpeedField->add("Very Slow", 2, false);
    miSurvivalModeSpeedField->add("Slow", 3);
    miSurvivalModeSpeedField->add("Moderate", 4);
    miSurvivalModeSpeedField->add("Fast", 6);
    miSurvivalModeSpeedField->add("Very Fast", 8, false);
    miSurvivalModeSpeedField->add("Extremely Fast", 15, false);
    miSurvivalModeSpeedField->add("Insanely Fast", 30, false);
    miSurvivalModeSpeedField->setOutputPtr(&game_values.gamemodemenusettings.survival.speed);
    miSurvivalModeSpeedField->setCurrentValue(game_values.gamemodemenusettings.survival.speed);

    miSurvivalModeShieldField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 320, "Shield", 400, 180);
    miSurvivalModeShieldField->add("Off", false);
    miSurvivalModeShieldField->add("On", true);
    miSurvivalModeShieldField->setOutputPtr(&game_values.gamemodemenusettings.survival.shield);
    miSurvivalModeShieldField->setCurrentValue(game_values.gamemodemenusettings.survival.shield ? 1 : 0);
    miSurvivalModeShieldField->setAutoAdvance(true);

    finishModeMenu(mModeSettingsMenu[16], "Survival Rules", {miSurvivalModeEnemySlider[0], miSurvivalModeEnemySlider[1], miSurvivalModeEnemySlider[2], miSurvivalModeDensityField, miSurvivalModeSpeedField, miSurvivalModeShieldField});

    //***********************
    // Greed Mode Settings
    //***********************
    miGreedModeCoinLife = new MI_SelectField<short>(&rm->spr_selectfield, 120, 160, "Coin Life", 400, 150);
    miGreedModeCoinLife->add("1 Second", 62, false);
    miGreedModeCoinLife->add("2 Seconds", 124, false);
    miGreedModeCoinLife->add("3 Seconds", 186);
    miGreedModeCoinLife->add("4 Seconds", 248);
    miGreedModeCoinLife->add("5 Seconds", 310);
    miGreedModeCoinLife->add("6 Seconds", 372);
    miGreedModeCoinLife->add("7 Seconds", 434);
    miGreedModeCoinLife->add("8 Seconds", 496);
    miGreedModeCoinLife->add("9 Seconds", 558);
    miGreedModeCoinLife->add("10 Seconds", 620);
    miGreedModeCoinLife->add("12 Seconds", 744);
    miGreedModeCoinLife->add("15 Seconds", 930);
    miGreedModeCoinLife->add("18 Seconds", 1116);
    miGreedModeCoinLife->add("20 Seconds", 1240);
    miGreedModeCoinLife->add("25 Seconds", 1550);
    miGreedModeCoinLife->add("30 Seconds", 1860);
    miGreedModeCoinLife->setOutputPtr(&game_values.gamemodemenusettings.greed.coinlife);
    miGreedModeCoinLife->setCurrentValue(game_values.gamemodemenusettings.greed.coinlife);

    miGreedModeOwnCoins = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 200, "Own Coins", 400, 150);
    miGreedModeOwnCoins->add("Yes", true);
    miGreedModeOwnCoins->add("No", false);
    miGreedModeOwnCoins->setOutputPtr(&game_values.gamemodemenusettings.greed.owncoins);
    miGreedModeOwnCoins->setCurrentValue(game_values.gamemodemenusettings.greed.owncoins);
    miGreedModeOwnCoins->setAutoAdvance(true);

    miGreedModeMultiplier = new MI_SelectField<short>(&rm->spr_selectfield, 120, 240, "Multipler", 400, 150);
    miGreedModeMultiplier->add("0.5", 1, false);
    miGreedModeMultiplier->add("1", 2);
    miGreedModeMultiplier->add("1.5", 3);
    miGreedModeMultiplier->add("2", 4);
    miGreedModeMultiplier->add("2.5", 5, false);
    miGreedModeMultiplier->add("3", 6, false);
    miGreedModeMultiplier->setOutputPtr(&game_values.gamemodemenusettings.greed.multiplier);
    miGreedModeMultiplier->setCurrentValue(game_values.gamemodemenusettings.greed.multiplier);

    miGreedModePercentExtraCoin = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 280, "Extra Coins", 400, 150, 384);
    addNumbers(miGreedModePercentExtraCoin, 0, 100, 5, 10, 50);
    miGreedModePercentExtraCoin->setOutputPtr(&game_values.gamemodemenusettings.greed.percentextracoin);
    miGreedModePercentExtraCoin->setCurrentValue(game_values.gamemodemenusettings.greed.percentextracoin);
    miGreedModePercentExtraCoin->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[17], "Greed Rules", {
        miGreedModeCoinLife, miGreedModeOwnCoins, miGreedModeMultiplier, miGreedModePercentExtraCoin
    });

    //***********************
    // Health Mode Settings
    //***********************
    miHealthModeStartLife = new MI_SelectField<short>(&rm->spr_selectfield, 120, 180, "Start Life", 400, 150);
    addNumbers(miHealthModeStartLife, 2, 10, 1, 4, 6);
    miHealthModeStartLife->setOutputPtr(&game_values.gamemodemenusettings.health.startlife);
    miHealthModeStartLife->setCurrentValue(game_values.gamemodemenusettings.health.startlife);
    miHealthModeStartLife->allowWrap(false);
    miHealthModeStartLife->setItemChangedCode(MENU_CODE_HEALTH_MODE_START_LIFE_CHANGED);

    miHealthModeMaxLife = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Max Life", 400, 150);
    addNumbers(miHealthModeMaxLife, 2, 10, 1, 6, 10);
    miHealthModeMaxLife->setOutputPtr(&game_values.gamemodemenusettings.health.maxlife);
    miHealthModeMaxLife->setCurrentValue(game_values.gamemodemenusettings.health.maxlife);
    miHealthModeMaxLife->allowWrap(false);
    miHealthModeMaxLife->setItemChangedCode(MENU_CODE_HEALTH_MODE_MAX_LIFE_CHANGED);

    miHealthModePercentExtraLife = new MI_SliderField(&rm->spr_selectfield, &rm->menu_slider_bar, 120, 260, "Extra Life", 400, 150, 384);
    addNumbers(miHealthModePercentExtraLife, 0, 100, 5, 10, 50);
    miHealthModePercentExtraLife->setOutputPtr(&game_values.gamemodemenusettings.health.percentextralife);
    miHealthModePercentExtraLife->setCurrentValue(game_values.gamemodemenusettings.health.percentextralife);
    miHealthModePercentExtraLife->allowWrap(false);

    finishModeMenu(mModeSettingsMenu[18], "Health Rules", {miHealthModeStartLife, miHealthModeMaxLife, miHealthModePercentExtraLife});

    //***********************
    // Card Collection Mode Settings
    //***********************

    miCollectionModeQuantityField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 160, "Limit", 400, 180);
    miCollectionModeQuantityField->add("1 Card", 1);
    miCollectionModeQuantityField->add("2 Cards", 2);
    miCollectionModeQuantityField->add("3 Cards", 3);
    miCollectionModeQuantityField->add("4 Cards", 4, false);
    miCollectionModeQuantityField->add("5 Cards", 5, false);
    miCollectionModeQuantityField->add("# Players - 1", 6);
    miCollectionModeQuantityField->add("# Players", 7);
    miCollectionModeQuantityField->add("# Players + 1", 8, false);
    miCollectionModeQuantityField->add("# Players + 2", 9, false);
    miCollectionModeQuantityField->add("# Players + 3", 10, false);
    miCollectionModeQuantityField->setOutputPtr(&game_values.gamemodemenusettings.collection.quantity);
    miCollectionModeQuantityField->setCurrentValue(game_values.gamemodemenusettings.collection.quantity);

    miCollectionModeRateField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 200, "Rate", 400, 180);
    miCollectionModeRateField->add("Instant", 0);
    miCollectionModeRateField->add("1 Second", 62);
    miCollectionModeRateField->add("2 Seconds", 124);
    miCollectionModeRateField->add("3 Seconds", 186);
    miCollectionModeRateField->add("5 Seconds", 310);
    miCollectionModeRateField->add("10 Seconds", 620, false);
    miCollectionModeRateField->add("15 Seconds", 930, false);
    miCollectionModeRateField->add("20 Seconds", 1240, false);
    miCollectionModeRateField->add("25 Seconds", 1550, false);
    miCollectionModeRateField->add("30 Seconds", 1860, false);
    miCollectionModeRateField->setOutputPtr(&game_values.gamemodemenusettings.collection.rate);
    miCollectionModeRateField->setCurrentValue(game_values.gamemodemenusettings.collection.rate);

    miCollectionModeBankTimeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 240, "Bank Time", 400, 180);
    miCollectionModeBankTimeField->add("Instant", 0, false);
    miCollectionModeBankTimeField->add("1 Second", 62, false);
    miCollectionModeBankTimeField->add("2 Seconds", 124);
    miCollectionModeBankTimeField->add("3 Seconds", 186);
    miCollectionModeBankTimeField->add("4 Seconds", 248);
    miCollectionModeBankTimeField->add("5 Seconds", 310);
    miCollectionModeBankTimeField->add("6 Seconds", 372, false);
    miCollectionModeBankTimeField->add("7 Seconds", 434, false);
    miCollectionModeBankTimeField->add("8 Seconds", 496, false);
    miCollectionModeBankTimeField->add("9 Seconds", 558, false);
    miCollectionModeBankTimeField->add("10 Seconds", 620, false);
    miCollectionModeBankTimeField->setOutputPtr(&game_values.gamemodemenusettings.collection.banktime);
    miCollectionModeBankTimeField->setCurrentValue(game_values.gamemodemenusettings.collection.banktime);

    miCollectionModeCardLifeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 280, "Card Life", 400, 180);
    miCollectionModeCardLifeField->add("1 Second", 62, false);
    miCollectionModeCardLifeField->add("2 Seconds", 124, false);
    miCollectionModeCardLifeField->add("3 Seconds", 186);
    miCollectionModeCardLifeField->add("4 Seconds", 248);
    miCollectionModeCardLifeField->add("5 Seconds", 310);
    miCollectionModeCardLifeField->add("6 Seconds", 372);
    miCollectionModeCardLifeField->add("7 Seconds", 434);
    miCollectionModeCardLifeField->add("8 Seconds", 496);
    miCollectionModeCardLifeField->add("9 Seconds", 558);
    miCollectionModeCardLifeField->add("10 Seconds", 620);
    miCollectionModeCardLifeField->add("12 Seconds", 744);
    miCollectionModeCardLifeField->add("15 Seconds", 930);
    miCollectionModeCardLifeField->add("18 Seconds", 1116);
    miCollectionModeCardLifeField->add("20 Seconds", 1240);
    miCollectionModeCardLifeField->add("25 Seconds", 1550);
    miCollectionModeCardLifeField->add("30 Seconds", 1860);
    miCollectionModeCardLifeField->setOutputPtr(&game_values.gamemodemenusettings.collection.cardlife);
    miCollectionModeCardLifeField->setCurrentValue(game_values.gamemodemenusettings.collection.cardlife);

    finishModeMenu(mModeSettingsMenu[19], "Card Collection Rules", {miCollectionModeQuantityField, miCollectionModeRateField, miCollectionModeBankTimeField, miCollectionModeCardLifeField});

    //***********************
    // Chase Mode Settings
    //***********************

    miChaseModeSpeedField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 160, "Speed", 400, 180);
    miChaseModeSpeedField->add("Very Slow", 3, false);
    miChaseModeSpeedField->add("Slow", 4);
    miChaseModeSpeedField->add("Moderate", 5);
    miChaseModeSpeedField->add("Fast", 6);
    miChaseModeSpeedField->add("Very Fast", 7);
    miChaseModeSpeedField->add("Extremely Fast", 8, false);
    miChaseModeSpeedField->add("Insanely Fast", 10, false);
    miChaseModeSpeedField->setOutputPtr(&game_values.gamemodemenusettings.chase.phantospeed);
    miChaseModeSpeedField->setCurrentValue(game_values.gamemodemenusettings.chase.phantospeed);

    for (short iPhanto = 0; iPhanto < 3; iPhanto++) {
        miChaseModeQuantitySlider[iPhanto] = new MI_PowerupSlider(&rm->spr_selectfield, &rm->menu_slider_bar, &rm->spr_phanto, 120, 200 + 40 * iPhanto, 400, iPhanto);
        miChaseModeQuantitySlider[iPhanto]->add("", 0, iPhanto == 0 ? false : true);
        miChaseModeQuantitySlider[iPhanto]->add("", 1);
        miChaseModeQuantitySlider[iPhanto]->add("", 2);
        miChaseModeQuantitySlider[iPhanto]->add("", 3, false);
        miChaseModeQuantitySlider[iPhanto]->add("", 4, false);
        miChaseModeQuantitySlider[iPhanto]->add("", 5, false);
        miChaseModeQuantitySlider[iPhanto]->allowWrap(false);
        miChaseModeQuantitySlider[iPhanto]->setOutputPtr(&game_values.gamemodemenusettings.chase.phantoquantity[iPhanto]);
        miChaseModeQuantitySlider[iPhanto]->setCurrentValue(game_values.gamemodemenusettings.chase.phantoquantity[iPhanto]);
    }

    finishModeMenu(mModeSettingsMenu[20], "Phanto Rules", {
        miChaseModeSpeedField, miChaseModeQuantitySlider[0], miChaseModeQuantitySlider[1],
        miChaseModeQuantitySlider[2]
    });

    //***********************
    // Shyguy Tag Mode Settings
    //***********************

    miShyGuyTagModeTagOnSuicideField = new MI_SelectField<bool>(&rm->spr_selectfield, 120, 180, "Suicide Tag", 400, 180);
    miShyGuyTagModeTagOnSuicideField->add("Off", false);
    miShyGuyTagModeTagOnSuicideField->add("On", true);
    miShyGuyTagModeTagOnSuicideField->setOutputPtr(&game_values.gamemodemenusettings.shyguytag.tagonsuicide);
    miShyGuyTagModeTagOnSuicideField->setCurrentValue(game_values.gamemodemenusettings.shyguytag.tagonsuicide ? 1 : 0);
    miShyGuyTagModeTagOnSuicideField->setAutoAdvance(true);

    miShyGuyTagModeTagOnStompField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Tag Transfer", 400, 180);
    miShyGuyTagModeTagOnStompField->add("Touch Only", 0);
    miShyGuyTagModeTagOnStompField->add("Kills Only", 1);
    miShyGuyTagModeTagOnStompField->add("Touch and Kills", 2);
    miShyGuyTagModeTagOnStompField->setOutputPtr(&game_values.gamemodemenusettings.shyguytag.tagtransfer);
    miShyGuyTagModeTagOnStompField->setCurrentValue(game_values.gamemodemenusettings.shyguytag.tagtransfer);

    miShyGuyTagModeFreeTimeField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 260, "Free Time", 400, 180);
    miShyGuyTagModeFreeTimeField->add("Instant", 0);
    miShyGuyTagModeFreeTimeField->add("1 Second", 1);
    miShyGuyTagModeFreeTimeField->add("2 Seconds", 2);
    miShyGuyTagModeFreeTimeField->add("3 Seconds", 3);
    miShyGuyTagModeFreeTimeField->add("4 Seconds", 4);
    miShyGuyTagModeFreeTimeField->add("5 Seconds", 5);
    miShyGuyTagModeFreeTimeField->add("6 Seconds", 6);
    miShyGuyTagModeFreeTimeField->add("7 Seconds", 7);
    miShyGuyTagModeFreeTimeField->add("8 Seconds", 8);
    miShyGuyTagModeFreeTimeField->add("9 Seconds", 9);
    miShyGuyTagModeFreeTimeField->add("10 Seconds", 10);
    miShyGuyTagModeFreeTimeField->setOutputPtr(&game_values.gamemodemenusettings.shyguytag.freetime);
    miShyGuyTagModeFreeTimeField->setCurrentValue(game_values.gamemodemenusettings.shyguytag.freetime);

    finishModeMenu(mModeSettingsMenu[21], "Shyguy Tag Rules", {miShyGuyTagModeTagOnSuicideField, miShyGuyTagModeTagOnStompField, miShyGuyTagModeFreeTimeField});

    //***********************
    // Boss Mode Settings
    //***********************

    miBossModeTypeField = new MI_SelectField<Boss>(&rm->spr_selectfield, 120, 180, "Type", 400, 180);
    miBossModeTypeField->add("Hammer", Boss::Hammer);
    miBossModeTypeField->add("Bomb", Boss::Bomb);
    miBossModeTypeField->add("Fire", Boss::Fire);
    miBossModeTypeField->setOutputPtr(&game_values.gamemodemenusettings.boss.bosstype);
    miBossModeTypeField->setCurrentValue(game_values.gamemodemenusettings.boss.bosstype);

    miBossModeDifficultyField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 220, "Difficulty", 400, 180);
    miBossModeDifficultyField->add("Very Easy", 0, false);
    miBossModeDifficultyField->add("Easy", 1);
    miBossModeDifficultyField->add("Moderate", 2);
    miBossModeDifficultyField->add("Hard", 3);
    miBossModeDifficultyField->add("Very Hard", 4, false);
    miBossModeDifficultyField->setOutputPtr(&game_values.gamemodemenusettings.boss.difficulty);
    miBossModeDifficultyField->setCurrentValue(game_values.gamemodemenusettings.boss.difficulty);

    miBossModeHitPointsField = new MI_SelectField<short>(&rm->spr_selectfield, 120, 260, "Health", 400, 180);
    addNumbers(miBossModeHitPointsField, 1, 20, 1, 3, 8);
    miBossModeHitPointsField->setOutputPtr(&game_values.gamemodemenusettings.boss.hitpoints);
    miBossModeHitPointsField->setCurrentValue(game_values.gamemodemenusettings.boss.hitpoints);

    finishModeMenu(mBossSettingsMenu, "Boss Minigame Rules", {miBossModeTypeField, miBossModeDifficultyField, miBossModeHitPointsField});

}

void UI_ModeOptionsMenu::SetControllingTeam(short iControlTeam)
{
    for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++)
        mModeSettingsMenu[iMode].SetControllingTeam(iControlTeam);
}

void UI_ModeOptionsMenu::SetRandomGameModeSettings(short iMode)
{
    if (iMode == game_mode_classic) { // classic
        game_values.gamemodesettings.classic.style = miClassicModeStyleField->randomValue();
        game_values.gamemodesettings.classic.scoring = miClassicModeScoringField->randomValue();
    } else if (iMode == game_mode_frag) { // frag
        game_values.gamemodesettings.frag.style = miFragModeStyleField->randomValue();
        game_values.gamemodesettings.frag.scoring = miFragModeScoringField->randomValue();
    } else if (iMode == game_mode_timelimit) { // time
        game_values.gamemodesettings.time.style = miTimeLimitModeStyleField->randomValue();
        game_values.gamemodesettings.time.scoring = miTimeLimitModeScoringField->randomValue();
        game_values.gamemodesettings.time.percentextratime = miTimeLimitModePercentExtraTime->currentValue();
    } else if (iMode == game_mode_jail) { // jail
        game_values.gamemodesettings.jail.style = miJailModeStyleField->randomValue();
        game_values.gamemodesettings.jail.timetofree = miJailModeTimeFreeField->randomValue();
        game_values.gamemodesettings.jail.tagfree = miJailModeTagFreeField->randomValue();
        game_values.gamemodesettings.jail.percentkey = miJailModeJailKeyField->currentValue();
    } else if (iMode == game_mode_coins) { // coins
        game_values.gamemodesettings.coins.penalty = miCoinModePenaltyField->randomValue();
        game_values.gamemodesettings.coins.quantity = miCoinModeQuantityField->randomValue();
        game_values.gamemodesettings.coins.percentextracoin = miCoinModePercentExtraCoin->currentValue();
    } else if (iMode == game_mode_stomp) { // stomp
        game_values.gamemodesettings.stomp.rate = miStompModeRateField->randomValue();

        for (short iEnemy = 0; iEnemy < NUMSTOMPENEMIES; iEnemy++)
            game_values.gamemodesettings.stomp.enemyweight[iEnemy] = miStompModeEnemySlider[iEnemy]->currentValue();
    } else if (iMode == game_mode_eggs) { // egg
        for (int iEgg = 0; iEgg < 4; iEgg++)
            game_values.gamemodesettings.egg.eggs[iEgg] = miEggModeEggQuantityField[iEgg]->currentValue();

        for (int iYoshi = 0; iYoshi < 4; iYoshi++)
            game_values.gamemodesettings.egg.yoshis[iYoshi] = miEggModeYoshiQuantityField[iYoshi]->currentValue();

        game_values.gamemodesettings.egg.explode = miEggModeExplosionTimeField->randomValue();
    } else if (iMode == game_mode_ctf) { // capture the flag
        game_values.gamemodesettings.flag.speed = miFlagModeSpeedField->currentValue();
        game_values.gamemodesettings.flag.touchreturn = miFlagModeTouchReturnField->randomValue();
        game_values.gamemodesettings.flag.pointmove = miFlagModePointMoveField->randomValue();
        game_values.gamemodesettings.flag.autoreturn = miFlagModeAutoReturnField->randomValue();
        game_values.gamemodesettings.flag.homescore = miFlagModeHomeScoreField->randomValue();
        game_values.gamemodesettings.flag.centerflag = miFlagModeCenterFlagField->randomValue();
    } else if (iMode == game_mode_chicken) { // chicken
        game_values.gamemodesettings.chicken.usetarget = miChickenModeShowTargetField->randomValue();
        game_values.gamemodesettings.chicken.glide = miChickenModeGlideField->randomValue();
    } else if (iMode == game_mode_tag) { // tag
        game_values.gamemodesettings.tag.tagontouch = miTagModeTagOnTouchField->randomValue();
    } else if (iMode == game_mode_star) { // star
        game_values.gamemodesettings.star.time = miStarModeTimeField->randomValue();
        game_values.gamemodesettings.star.shine = miStarModeShineField->randomValue();
        game_values.gamemodesettings.star.percentextratime = miStarModePercentExtraTime->currentValue();
    } else if (iMode == game_mode_domination) { // domination
        game_values.gamemodesettings.domination.quantity = miDominationModeQuantityField->randomValue();
        game_values.gamemodesettings.domination.loseondeath = miDominationModeLoseOnDeathField->randomValue();
        game_values.gamemodesettings.domination.relocateondeath = miDominationModeRelocateOnDeathField->randomValue();
        game_values.gamemodesettings.domination.stealondeath = miDominationModeStealOnDeathField->randomValue();
        game_values.gamemodesettings.domination.relocationfrequency = miDominationModeRelocateFrequencyField->randomValue();
    } else if (iMode == game_mode_koth) { // king of the hill
        game_values.gamemodesettings.kingofthehill.areasize = miKingOfTheHillModeSizeField->randomValue();
        game_values.gamemodesettings.kingofthehill.relocationfrequency = miKingOfTheHillModeRelocateFrequencyField->randomValue();
        game_values.gamemodesettings.kingofthehill.maxmultiplier = miKingOfTheHillModeMultiplierField->randomValue();
    } else if (iMode == game_mode_race) { // race
        game_values.gamemodesettings.race.quantity = miRaceModeQuantityField->randomValue();
        game_values.gamemodesettings.race.speed = miRaceModeSpeedField->randomValue();
        game_values.gamemodesettings.race.penalty = miRaceModePenaltyField->randomValue();
    } else if (iMode == game_mode_frenzy) { // frenzy
        miFrenzyModeOptions->SetRandomGameModeSettings();
    } else if (iMode == game_mode_survival) { // survival
        game_values.gamemodesettings.survival.density = miSurvivalModeDensityField->randomValue();
        game_values.gamemodesettings.survival.speed = miSurvivalModeSpeedField->randomValue();
        game_values.gamemodesettings.survival.shield = miSurvivalModeShieldField->randomValue();

        for (short iEnemy = 0; iEnemy < NUMSURVIVALENEMIES; iEnemy++)
            game_values.gamemodesettings.survival.enemyweight[iEnemy] = miSurvivalModeEnemySlider[iEnemy]->currentValue();
    } else if (iMode == game_mode_greed) { // greed
        game_values.gamemodesettings.greed.coinlife = miGreedModeCoinLife->randomValue();
        game_values.gamemodesettings.greed.owncoins = miGreedModeOwnCoins->randomValue();
        game_values.gamemodesettings.greed.multiplier = miGreedModeMultiplier->randomValue();
        game_values.gamemodesettings.greed.percentextracoin = miGreedModePercentExtraCoin->currentValue();
    } else if (iMode == game_mode_health) { // health
        game_values.gamemodesettings.health.startlife = miHealthModeStartLife->randomValue();
        game_values.gamemodesettings.health.maxlife = miHealthModeMaxLife->randomValue();
        game_values.gamemodesettings.health.percentextralife = miHealthModePercentExtraLife->currentValue();
    } else if (iMode == game_mode_collection) { // card collection
        game_values.gamemodesettings.collection.quantity = miCollectionModeQuantityField->randomValue();
        game_values.gamemodesettings.collection.rate = miCollectionModeRateField->randomValue();
        game_values.gamemodesettings.collection.banktime = miCollectionModeBankTimeField->randomValue();
        game_values.gamemodesettings.collection.cardlife = miCollectionModeCardLifeField->randomValue();
    } else if (iMode == game_mode_chase) { // chase (phanto)
        game_values.gamemodesettings.chase.phantospeed = miChaseModeSpeedField->randomValue();

        for (short iPhanto = 0; iPhanto < 3; iPhanto++)
            game_values.gamemodesettings.chase.phantoquantity[iPhanto] = miChaseModeQuantitySlider[iPhanto]->currentValue();
    } else if (iMode == game_mode_shyguytag) { // shyguy tag
        game_values.gamemodesettings.shyguytag.tagonsuicide = miShyGuyTagModeTagOnSuicideField->randomValue();
        game_values.gamemodesettings.shyguytag.tagtransfer = miShyGuyTagModeTagOnStompField->randomValue();
        game_values.gamemodesettings.shyguytag.freetime = miShyGuyTagModeFreeTimeField->randomValue();
    } else if (iMode == game_mode_boss_minigame) { // boss
        game_values.gamemodesettings.boss.bosstype = miBossModeTypeField->randomValue();
        game_values.gamemodesettings.boss.difficulty = miBossModeDifficultyField->randomValue();
        game_values.gamemodesettings.boss.hitpoints = miBossModeHitPointsField->randomValue();
    }
}

void UI_ModeOptionsMenu::HealthModeStartLifeChanged()
{
    short iMaxLife = miHealthModeMaxLife->currentValue();
    if (miHealthModeStartLife->currentValue() > iMaxLife) {
        miHealthModeStartLife->setCurrentValue(iMaxLife);
    }
}

void UI_ModeOptionsMenu::HealthModeMaxLifeChanged()
{
    short iStartLife = miHealthModeStartLife->currentValue();
    if (miHealthModeMaxLife->currentValue() < iStartLife) {
        miHealthModeMaxLife->setCurrentValue(iStartLife);
    }
}

void UI_ModeOptionsMenu::Refresh()
{
    for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++) {
        mModeSettingsMenu[iMode].Refresh();
    }

    mBossSettingsMenu.Refresh();
}

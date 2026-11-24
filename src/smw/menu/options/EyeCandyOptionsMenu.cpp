#include "EyeCandyOptionsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_EyeCandyOptionsMenu::UI_EyeCandyOptionsMenu()
    : UI_Menu()
{
    SettingsPage page(8);

    miSpawnStyleField = new MI_SelectField<SpawnStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Spawn Style", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miSpawnStyleField->add("Instant", SpawnStyle::Instant);
    miSpawnStyleField->add("Door", SpawnStyle::Door);
    miSpawnStyleField->add("Swirl", SpawnStyle::Swirl);
    miSpawnStyleField->setOutputPtr(&game_values.spawnstyle);
    miSpawnStyleField->setCurrentValue(game_values.spawnstyle);
    page.add(miSpawnStyleField, "How players appear when they spawn");

    miAwardStyleField = new MI_SelectField<AwardStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Award Style", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miAwardStyleField->add("None", AwardStyle::None);
    miAwardStyleField->add("Fireworks", AwardStyle::Fireworks);
    miAwardStyleField->add("Spiral", AwardStyle::Swirl);
    miAwardStyleField->add("Ring", AwardStyle::Halo);
    miAwardStyleField->add("Souls", AwardStyle::Souls);
    miAwardStyleField->add("Text", AwardStyle::Text);
    miAwardStyleField->setOutputPtr(&game_values.awardstyle);
    miAwardStyleField->setCurrentValue(game_values.awardstyle);
    page.add(miAwardStyleField, "Effect for a player on a kill streak");

    miWinningCrownField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Leader Crown", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miWinningCrownField->add("Off", false);
    miWinningCrownField->add("On", true);
    miWinningCrownField->setOutputPtr(&game_values.showwinningcrown);
    miWinningCrownField->setCurrentValue(game_values.showwinningcrown ? 1 : 0);
    page.add(miWinningCrownField, "Crowns the player in the lead");

    miDeadTeamNoticeField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Dead Team Notice", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miDeadTeamNoticeField->add("Off", false);
    miDeadTeamNoticeField->add("On", true);
    miDeadTeamNoticeField->setOutputPtr(&game_values.deadteamnotice);
    miDeadTeamNoticeField->setCurrentValue(game_values.deadteamnotice ? 1 : 0);
    page.add(miDeadTeamNoticeField, "Tell everyone when a team is out");

    miStartCountDownField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Start Countdown", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miStartCountDownField->add("Off", false);
    miStartCountDownField->add("On", true);
    miStartCountDownField->setOutputPtr(&game_values.startgamecountdown);
    miStartCountDownField->setCurrentValue(game_values.startgamecountdown ? 1 : 0);
    page.add(miStartCountDownField, "Count down 3, 2, 1 before a match");

    miStartModeDisplayField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Show Mode", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miStartModeDisplayField->add("Off", false);
    miStartModeDisplayField->add("On", true);
    miStartModeDisplayField->setOutputPtr(&game_values.startmodedisplay);
    miStartModeDisplayField->setCurrentValue(game_values.startmodedisplay ? 1 : 0);
    page.add(miStartModeDisplayField, "Show the mode's name at the start");

    miScoreStyleField = new MI_SelectField<ScoreboardStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Score Location", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miScoreStyleField->add("Top", ScoreboardStyle::Top);
    miScoreStyleField->add("Bottom", ScoreboardStyle::Bottom);
    miScoreStyleField->add("Corners", ScoreboardStyle::Corners);
    miScoreStyleField->setOutputPtr(&game_values.scoreboardstyle);
    miScoreStyleField->setCurrentValue(game_values.scoreboardstyle);
    page.add(miScoreStyleField, "Where scores appear on screen");

    miCrunchField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Screen Crunch", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miCrunchField->add("Off", false);
    miCrunchField->add("On", true);
    miCrunchField->setOutputPtr(&game_values.screencrunch);
    miCrunchField->setCurrentValue(game_values.screencrunch ? 1 : 0);
    page.add(miCrunchField, "Shake the screen when a player dies");

    page.finish(*this, "Eye Candy Options", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

#include "TeamOptionsMenu.h"

#include "GameValues.h"
#include "ResourceManager.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"
#include "ui/SettingsPage.h"

UI_TeamOptionsMenu::UI_TeamOptionsMenu()
    : UI_Menu()
{
    SettingsPage page(4);

    miTeamKillsField = new MI_SelectField<TeamCollisionStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Player Collision", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miTeamKillsField->add("Off", TeamCollisionStyle::Off);
    miTeamKillsField->add("Assist", TeamCollisionStyle::Assist);
    miTeamKillsField->add("On", TeamCollisionStyle::On);
    miTeamKillsField->setOutputPtr(&game_values.teamcollision);
    miTeamKillsField->setCurrentValue(game_values.teamcollision);
    page.add(miTeamKillsField, "Teammates pass through, bounce or hurt");

    miTeamColorsField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "Colors", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miTeamColorsField->add("Individual", false);
    miTeamColorsField->add("Team", true);
    miTeamColorsField->setOutputPtr(&game_values.teamcolors);
    miTeamColorsField->setCurrentValue(game_values.teamcolors ? 1 : 0);
    page.add(miTeamColorsField, "Team colors or each player's own");

    miCpuSkinsField = new MI_SelectField<bool>(&rm->spr_selectfield, page.x(), page.rowY(), "CPU Skins", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miCpuSkinsField->add("Their Slot's", false);
    miCpuSkinsField->add("Random", true);
    miCpuSkinsField->setOutputPtr(&game_values.randomcpuskins);
    miCpuSkinsField->setCurrentValue(game_values.randomcpuskins ? 1 : 0);
    page.add(miCpuSkinsField, "Random skins for CPUs, or their slot's");

    miTournamentControlField = new MI_SelectField<TournamentControlStyle>(&rm->spr_selectfield, page.x(), page.rowY(), "Tournament Control", SettingsPage::ROW_W, SettingsPage::LABEL_W);
    miTournamentControlField->add("All", TournamentControlStyle::All);
    miTournamentControlField->add("Game Winner", TournamentControlStyle::GameWinner);
    miTournamentControlField->add("Game Loser", TournamentControlStyle::GameLoser);
    miTournamentControlField->add("Leading Teams", TournamentControlStyle::LeadingTeams);
    miTournamentControlField->add("Trailing Teams", TournamentControlStyle::TrailingTeams);
    miTournamentControlField->add("Random", TournamentControlStyle::Random);
    miTournamentControlField->add("Random Loser", TournamentControlStyle::RandomLoser);
    miTournamentControlField->add("Round Robin", TournamentControlStyle::RoundRobin);
    miTournamentControlField->setOutputPtr(&game_values.tournamentcontrolstyle);
    miTournamentControlField->setCurrentValue(game_values.tournamentcontrolstyle);
    page.add(miTournamentControlField, "Who picks the next game of a tournament");

    page.finish(*this, "Team Options", MENU_CODE_BACK_TO_OPTIONS_MENU);
};

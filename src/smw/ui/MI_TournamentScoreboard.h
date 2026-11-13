#pragma once

#include "GlobalConstants.h"
#include "uicontrol.h"

#include <array>
#include <memory>
#include <vector>

class gfxSprite;
class MI_Image;
class MI_ScoreText;


class MI_TournamentScoreboard : public UI_Control {
public:
    MI_TournamentScoreboard(gfxSprite* spr_background, short x, short y);
    virtual ~MI_TournamentScoreboard();

    void Update() override;
    void Draw() override;

    void CreateScoreboard(short numTeams, short numGames, gfxSprite* spr_icons);
    void RefreshWorldScores(short gameWinner);
    void RefreshTourScores();
    void RefreshTournamentScores(short gameWinner);
    void DetermineScoreboardWinners();
    void StopSwirl();

private:
    void FreeScoreboard();
    short GetYFromPlace(short iPlace) const {
        return m_pos.y + iPlace * 69 + (4 - iNumTeams) * 35;
    }
    float GetIconSpacing() const {
        return static_cast<float>(372 - iNumGames * 32) / static_cast<float>(iNumGames + 1);
    }

    bool fCreated = false;

    std::vector<std::unique_ptr<MI_Image>> miTeamImages;
    std::vector<std::vector<std::unique_ptr<MI_Image>>> miIconImages;
    std::vector<std::vector<std::unique_ptr<MI_Image>>> miPlayerImages;

    short iNumTeams = 0;
    short iNumGames = 0;

    short iTournamentWinner = 0;
    short iGameWinner = 0;

    short iSwirlIconTeam = 0;
    short iSwirlIconGame = 0;

    short iFireworksCounter = 0;
    short iWinnerTextCounter = 0;
    short iExplosionCounter = 0;

    gfxSprite* sprBackground = nullptr;
    gfxSprite* sprIcons = nullptr;

    short iTeamIDs[4][3];
    short iTeamCounts[4];

    std::array<std::unique_ptr<MI_ScoreText>, 4> tourScores;
    std::array<std::unique_ptr<MI_ScoreText>, 10> tourPoints;
    std::unique_ptr<MI_Image> miTourPointBar;
    std::array<std::unique_ptr<MI_Image>, 10> tourBonus;

    std::array<std::array<std::unique_ptr<MI_Image>, MAX_WORLD_BONUSES_AWARDED>, 4> worldBonus;
    std::unique_ptr<MI_Image> worldScoreModifier;
    std::array<std::unique_ptr<MI_Image>, 4> worldPlace;
    std::unique_ptr<MI_ScoreText> worldScore;
    std::array<std::unique_ptr<MI_Image>, 4> worldPointsBackground;
};

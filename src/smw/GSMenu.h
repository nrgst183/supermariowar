#ifndef GAMESTATE_MENU_H
#define GAMESTATE_MENU_H

#include "GameState.h"
#include "ui/MenuCode.h"

#include <memory>
#include <string>
#include <vector>

class UI_Menu;

class UI_BonusWheelMenu;
class UI_GameSettingsMenu;
class UI_MainMenu;
class UI_MapFilterEditMenu;
class UI_MatchSelectionMenu;
class UI_ModeOptionsMenu;
class UI_OnlineLobbyMenu;
class UI_OnlineMenu;
class UI_OptionsMenu;
class UI_PlayerControlsMenu;
class UI_PlayerControlsSelectMenu;
class UI_TeamSelectMenu;
class UI_TournamentScoreboardMenu;
class UI_TourStopMenu;
class UI_WorldMenu;

class UI_EyeCandyOptionsMenu;
class UI_GameplayOptionsMenu;
class UI_GraphicsOptionsMenu;
class UI_PowerupDropRatesMenu;
class UI_PowerupSettingsMenu;
class UI_ProjectileLimitsMenu;
class UI_ProjectileOptionsMenu;
class UI_SoundOptionsMenu;
class UI_TeamOptionsMenu;



#ifdef _DEBUG
class ScriptOperation
{
	public:

    ScriptOperation() {
        iTimesExecuted = 0;
    }

		short iController; //0 - 3 == specific, 4 == all, 5 == single random
		bool fInput[8];
		short iIterations;  //number of times to execute this operation

		short iTimesExecuted;

	private:

};
#endif

enum DisplayError {
	DISPLAY_ERROR_NONE,
	DISPLAY_ERROR_READ_TOUR_FILE,
	DISPLAY_ERROR_READ_WORLD_FILE,
	DISPLAY_ERROR_MAP_FILTER
};

class MenuState : public GameState
{
	public:
        bool init() override;
        void update() override;

        static MenuState& instance();

#ifdef _DEBUG

		void LoadScript(const char * szScriptFile);
		void ResetScript();
		void GetNextScriptOperation();

		std::vector<ScriptOperation*> operations;
		std::vector<ScriptOperation*>::iterator current;
		short iScriptState;
		bool fScriptRunPreGameOptions;

		void StartRecordScript();
		void SaveScript(const char * szScriptFile);
		void SetNextScriptOperation();
		void AddEmtpyLineToScript();
#endif

	private:
		MenuState();
		~MenuState();

		void onEnterState() override;

		void CreateMenu();
		bool ReadTourFile();
		void StartGame();
		void Exit();
		MenuCodeEnum AutoTestMenuUpdate();
		void ResetTournamentBackToMainMenu();

		// Online (rollback netplay) menus
		void HostOnlineGame();
		void JoinOnlineGame();
		void EnterOnlineLobby();
		void LeaveOnlineLobby(const std::string& message);
		void LeaveOnlineLobbyNow();
		void HandleOnlineEvents();
		void OpenOnlineMatchSettings();
		void OpenOnlineGameSettings();
		void ReturnToOnlineLobby();
		void ApplyOnlineMatchSelection();
		void StartOnlineMatch();
		MenuCodeEnum ShotsMenuUpdate();
		void AutoTestAfterDraw();

		void SetControllingTeamForSettingsMenu(short iControlTeam, bool fDisplayMessage);
		void DisplayControllingTeamMessage(short iControlTeam);


		UI_Menu* mCurrentMenu = nullptr;
		std::unique_ptr<UI_MainMenu> mMainMenu;

		// Options menu
		std::unique_ptr<UI_OptionsMenu> mOptionsMenu;
		std::unique_ptr<UI_GameplayOptionsMenu> mGameplayOptionsMenu;
		std::unique_ptr<UI_TeamOptionsMenu> mTeamOptionsMenu;
		std::unique_ptr<UI_PowerupDropRatesMenu> mPowerupDropRatesMenu;
		std::unique_ptr<UI_PowerupSettingsMenu> mPowerupSettingsMenu;
		std::unique_ptr<UI_ProjectileLimitsMenu> mProjectileLimitsMenu;
		std::unique_ptr<UI_ProjectileOptionsMenu> mProjectileOptionsMenu;
		std::unique_ptr<UI_GraphicsOptionsMenu> mGraphicsOptionsMenu;
		std::unique_ptr<UI_EyeCandyOptionsMenu> mEyeCandyOptionsMenu;
		std::unique_ptr<UI_SoundOptionsMenu> mSoundOptionsMenu;

		// Controls menu
		std::unique_ptr<UI_PlayerControlsSelectMenu> mPlayerControlsSelectMenu;
		std::unique_ptr<UI_PlayerControlsMenu> mPlayerControlsMenu;

		// Gameplay menus
		std::unique_ptr<UI_ModeOptionsMenu> mModeOptionsMenu;
		std::unique_ptr<UI_MatchSelectionMenu> mMatchSelectionMenu;
		std::unique_ptr<UI_GameSettingsMenu> mGameSettingsMenu;
		std::unique_ptr<UI_MapFilterEditMenu> mMapFilterEditMenu;
		std::unique_ptr<UI_TourStopMenu> mTourStopMenu;
		std::unique_ptr<UI_WorldMenu> mWorldMenu;
		std::unique_ptr<UI_TeamSelectMenu> mTeamSelectMenu;
		std::unique_ptr<UI_TournamentScoreboardMenu> mTournamentScoreboardMenu;
		std::unique_ptr<UI_BonusWheelMenu> mBonusWheelMenu;

		// Online menus
		std::unique_ptr<UI_OnlineMenu> mOnlineMenu;
		std::unique_ptr<UI_OnlineLobbyMenu> mOnlineLobbyMenu;
		bool mOnlineSetup = false;      //The host is choosing the match from the lobby (the match menus lead back to it)
		bool mOnlineMinigame = false;   //The host's choice: a minigame instead of a single game

		DisplayError iDisplayError = DISPLAY_ERROR_NONE;
		short iDisplayErrorTimer = 0;
		bool fNeedMenuMusicReset = false;

		const char * szCurrentMapName = nullptr;

		short iUnlockMinigameOptionIndex = 0;

		short iTournamentAIStep = 0;
		short iTournamentAITimer = 0;

		friend class GameplayState;
};

#endif // GAMESTATE_MENU_H

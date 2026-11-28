#ifndef GAMESTATE_MENU_H
#define GAMESTATE_MENU_H

#include "GameState.h"
#ifdef SMW_HAS_EDITORS
#include "editors/EditorLauncher.h"
#endif
#include "MatchTypes.h"
#include "rollback/NetSeries.h"

namespace rollback {
enum class LobbyAccess : uint8_t;
}
#include "ui/MenuCode.h"

#include <memory>
#include <string>
#include <vector>

class CGameMode;
class UI_Menu;

class UI_BonusWheelMenu;
class UI_GameSettingsMenu;
class UI_MainMenu;
class UI_MapFilterEditMenu;
class UI_MatchSelectionMenu;
class UI_ModeOptionsMenu;
class UI_OnlineLobbyMenu;
class UI_MatchHistoryMenu;
class UI_EditorsMenu;
class UI_OnlineMenu;
class UI_SystemLinkMenu;
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
        #ifdef SMW_HAS_EDITORS
        bool RunEditor(editors::Editor editor);
        // Playing the map the level editor closed for (EditorLauncher.h): its match, then the
        // editor again with the settings it changed put back
        void StartTestPlay(const std::string& file);
        void EndTestPlay();
        struct TestPlay {
            std::string map;            // the map file it plays ("": none runs)
            bool reopenEditor = false;  // back from it: the editor opens in the next update
            short playercontrol[4] = {};
            short teamids[4][3] = {};
            short teamcounts[4] = {};
            short scoreCount = 0;
            MatchType matchtype = MatchType::SingleGame;
            CGameMode* gamemode = nullptr;
            short fragGoal = 0;
        };
        TestPlay mTestPlay;
#endif
        void Exit();
        void DrawDeterminismReport();
        bool PollMenuEvents();
        // The mouse in the menus (UI_Menu::Pointer...): what it did since the last frame, done to the
        // current menu with the frame's input
        struct PendingPointer {
            bool moved = false;
            short x = 0;
            short y = 0;
            int click = 0;      // 1 left, 2 right
            short clickX = 0;
            short clickY = 0;
            int wheel = 0;
        };
        PendingPointer mPointer;
        MenuCodeEnum ApplyPointer();
        /// The options screen before (-1) or after (1) the current one (LB and RB turn the pages), or
        /// MENU_CODE_NONE when the current menu isn't one of them
        MenuCodeEnum OptionsPageCode(int dir) const;
        bool HandleMenuCode(MenuCodeEnum code, bool& fGenerateMapThumbs);
        void EnterGameSetup();
        void LaunchMatch();
        MenuCodeEnum AutoTestMenuUpdate();
        void ResetTournamentBackToMainMenu();

        // Online (rollback netplay) menus
        void HostOnlineGame();
        void JoinOnlineGame();
        void JoinSystemLinkGame();
        void HostSystemLinkGame();
        bool HostLobby(int port, rollback::LobbyAccess access, std::string& error);
        void EnterOnlineLobby();
        void LeaveOnlineLobby(const std::string& message);
        void LeaveOnlineLobbyNow();
        void HandleOnlineEvents();
        // Where the lobby was entered from (leaving it goes back there): the main menu's Start (a local
        // game), the Online menu, the System Link (LAN games) menu, or the Editors menu (a lobby that
        // edits a map or a world together)
        enum class LobbyOrigin { MainMenu, OnlineMenu, SystemLink, Editors };
        LobbyOrigin mLobbyOrigin = LobbyOrigin::OnlineMenu;

        // A local game set up in the lobby (who can join: nobody) is played offline: the lobby closes
        // for its menus and matches, and opens again as it was when they're backed out of
        struct LocalSetup {
            bool valid = false;
            int localPlayers = 1;
            int cpus = 0;
            int team[4] = {};
            int color[4] = {};
        };
        LocalSetup mLocalSetup;
        bool mLocalFromLobby = false;   // the match menus (and matches) of a local game from the lobby
        short mLocalTeams = 0;
        void OpenLocalLobby(LobbyOrigin origin = LobbyOrigin::MainMenu);
        // The players on this machine follow the controllers plugged in (when their number changes)
        void FollowControllers();
        // Watching a replay together (NetGame's THEATER): the host picks it in the match history; a
        // joiner's starts once the host's replay is here (true: it did, the theater is up)
        bool mOnlineTheaterPick = false;
        bool StartWatchingTheater();
        bool OpenLobby(rollback::LobbyAccess access, LobbyOrigin origin, std::string& error);
        void StartLocalFromLobby();
        // A lobby that edits together (CoEdit.h): Editors > Online > Host opens it; its Start (the host)
        // opens the editor for everyone, and a joiner's button goes (back) into it
        void OpenEditorLobby(bool world);
        bool InEditorLobby() const;
        void StartLobbyEditing();
        void JoinLobbyEditing();
        void BackToLobbyFromEditor();
        void ResetOnlineMenus();
        void LeaveLobbyQuietly();
        void OpenOnlineMatchSettings();
        void OpenOnlineGameSettings();
        void ReturnToOnlineLobby();
        void ApplyOnlineMatchSelection();
        void StartOnlineMatch();
        // After an online match (NetLobby's REMATCH VOTE): the host starts what everyone voted for
        void UpdateOnlineVote();
        void StartOnlineFromSettings();
        void OpenOnlineSettingsView();
        void CloseOnlineSettingsView();
        void SyncOnlineSettings();
        void ApplyOnlineSettingRequests();
        void SyncOnlineSettingsView();
        MenuCodeEnum ShotsMenuUpdate();
        void AutoTestAfterDraw();

        // Online tournament, tour and world (MenuOnlineSeries.cpp, rollback/NetSeries.h)
        void StartOnlineSeries();
        bool SetupOnlineSeries(const rollback::SeriesBeginMsg& begin, std::string& error);
        void HandleOnlineSeries();
        bool OnlineSeriesInput(MenuCodeEnum& code);
        void OnlineSeriesWorldTick(MenuCodeEnum& code);
        void OnlineSeriesShowScreen(const rollback::SeriesScreenMsg& screen);
        void OnlineSeriesEnterScreen(rollback::SeriesScreen screen);
        void OnlineSeriesEnded(const std::string& reason);
        void OnlineSeriesAfterMatch();
        void DrawOnlineSeriesNotice();
        rollback::SeriesScreen OnlineSeriesHostScreen() const;
        bool OnlineSeriesLocalInWheelTeam() const;
        MenuCodeEnum AutoTestSeriesUpdate();

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
        std::unique_ptr<UI_SystemLinkMenu> mSystemLinkMenu;
        std::unique_ptr<UI_MatchHistoryMenu> mMatchHistoryMenu;
        std::unique_ptr<UI_EditorsMenu> mEditorsMenu;
        bool mOnlineView = false;       //A player who is not the host looks at (or, if the host allows it, edits) the match settings
        short mOnlineShownMode = -1;    //What the settings screen shows of the lobby's mode, goal and map (to tell the player's edits from the lobby's changes)
        short mOnlineShownGoal = -1;
        std::string mOnlineShownMap;
        bool mOnlineMapHere = false;
        int mOnlineViewPending = 0;     //Frames during which the lobby's state doesn't override the player's edit
        uint32_t mOnlineMenuSettingsRevision = 0;    //The host's rules and options last put into our menus
        uint32_t mOnlineMenuSettingsCheck = 0;
        bool mOnlineHostOnSettings = false;     //The host on its settings screens, as last followed
        bool mOnlineStayInLobby = false;        //We went back to the lobby ourselves (the host on its settings)
        std::string mOnlineShownHostMap;        //The host's map (its name) the settings view last showed
        std::string mOnlineAppliedKey;  //What the host's menus last published to the lobby
        bool mOnlineSetup = false;      //The host is choosing the match from the lobby (the match menus lead back to it)
        bool mOnlineMinigame = false;   //The host's choice: a minigame instead of a single game
        MatchType mOnlineSeriesType = MatchType::SingleGame;    //The host's choice: a tournament, tour or world (else SingleGame)

        // The online series' screens (MenuOnlineSeries.cpp)
        rollback::SeriesScreen mSeriesScreen = rollback::SeriesScreen::None;
        uint16_t mSeriesStep = 0;
        bool mSeriesWheelStopSent = false;
        bool mSeriesWheelPressSent = false;
        bool mSeriesSkipUpdate = false;     //The world map waits for its next lockstep tick
        uint8_t mWorldPrevKeys[4] = {};

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

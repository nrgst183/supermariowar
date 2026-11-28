#pragma once

#include "uimenu.h"

#include <string>

class MI_DialogBox;
class MI_Button;
class MI_Image;
class MI_LobbyButton;
class MI_LobbyScene;
class MI_LockTag;
class MI_MenuPanel;
class MI_VoteChoice;
class MI_OnlineTeamSelect;
class MI_SwitchField;
class MI_Text;
template<typename T> class MI_SelectField;

/*
    The online lobby, built like the offline flow into the match: its home is the Team and Character
    Selection screen of the original game (each player picks the skin and team of its own character and
    gets ready; the others are shown locked), with the Continue button (the host: on to the match type and
    game settings screens, the others: a look at the settings) and an Options button for the page of the
    secondary settings (local players, CPU players, input delay, who may change the match settings, kick).

    It shows the state of rollback::NetGame::instance().lobby() and sends the player's changes to it (see
    rollback/NetLobby.h). The match settings screen is the game's own (UI_GameSettingsMenu, see
    MenuOnline.cpp).
*/
class UI_OnlineLobbyMenu : public UI_Menu {
public:
    enum class Dialog { None, Leave, Kick, Vote };
    enum class Page { Team, Options };

    UI_OnlineLobbyMenu();

    /// The lobby was opened (or the player is back from a match or the match settings)
    void Open(bool fresh = true);
    /// Updates the screen from the state of the lobby; call every frame while shown, before SendInput()
    void Sync();
    /// As a menu's; up from the buttons into the team box (ready, as A left it): the CPUs' skins
    MenuCodeEnum SendInput(CPlayerInput* playerInput) override;

    /// After the player edited a field of the options page: sends the change to the lobby
    void ApplyChanges();
    /// The host selected "Kick": asks which player (the one chosen in the options page) to kick
    void KickSelected();
    /// A message for the status line, until the next match starts or the lobby closes
    void SetNotice(std::string text);

    void ShowPage(Page page);
    Page page() const { return m_page; }

    void OpenDialog(Dialog dialog);
    void CloseDialog();
    Dialog dialog() const { return m_dialog; }
    int kickPeer() const { return m_kickPeer; }

    /// After a match: what to play next (NetLobby's REMATCH VOTE), if there is anything to vote on
    void OpenVote();
    /// The host: when everyone voted, the choice (VOTE_REMATCH, VOTE_NEXT_MAP) once its countdown ran out
    uint8_t VoteDecision();

    /// The text of the status line right now (for tests and screenshots)
    const std::string& statusLine() const { return m_statusLine; }

private:
    void ApplyLayout(bool host, bool joining);
    void Focus(UI_Control* control);
    void RebuildDelayItems(int autoDelay);
    void RebuildKickItems();

    MI_LobbyScene* miScene = nullptr;
    MI_OnlineTeamSelect* miTeam = nullptr;
    MI_LobbyButton* miOptionsButton = nullptr;
    MI_LobbyButton* miContinueButton = nullptr;

    // The options page
    MI_SwitchField* miWhoCanJoin = nullptr;     // host: nobody (a local game), the network, anyone
    int m_pendingAccess = 0;
    UI_Control* FirstOption();
    bool LocalGame() const;    // where the options page starts: who can join (the host), else my players
    MI_SelectField<short>* miLocalPlayers = nullptr;
    MI_SelectField<short>* miCpuPlayers = nullptr;
    MI_SelectField<short>* miInputDelay = nullptr;
    MI_SelectField<short>* miEveryone = nullptr;
    MI_SelectField<short>* miKickWho = nullptr;
    MI_LobbyButton* miKickButton = nullptr;
    MI_LobbyButton* miTheaterButton = nullptr;    // the host: watch a replay together
    MI_LockTag* miHostTag[4] {};

    MI_DialogBox* miDialogImage = nullptr;
    MI_Text* miDialogLine1 = nullptr;
    MI_Text* miDialogLine2 = nullptr;
    MI_Button* miDialogYes = nullptr;
    MI_Button* miDialogNo = nullptr;

    // The "Play again?" panel (Dialog::Vote): a panel as the options pages', its two choices as fields
    MI_MenuPanel* miVotePanel = nullptr;
    MI_VoteChoice* miVoteRematch = nullptr;
    MI_VoteChoice* miVoteNext = nullptr;
    MI_Text* miVoteLine = nullptr;      // who it waits for, or what starts
    MI_Text* miVoteHint = nullptr;
    std::string m_voteNote;      // the panel's note: how many voted
    void ShowVote(bool show);

    Dialog m_dialog = Dialog::None;
    Page m_page = Page::Team;
    int m_kickPeer = -1;
    int m_voteCountdown = -1;    // the host: frames until the voted match starts (-1: not everyone voted)
    void SyncVote();
    std::string m_voteStatus;    // who voted for what (the status line while voting)
    std::string m_notice;
    std::string m_statusLine;

    int m_layoutHost = -1;       // what ApplyLayout() last did (-1: nothing yet)
    int m_layoutJoining = -1;
    int m_delayAuto = -1;        // the N of "Auto (N)" in the input delay field
    std::string m_kickSignature; // who the kick field lists
    // frames during which the state of the lobby doesn't override an edited field
    int m_pendingPlayers = 0;
    int m_pendingCpus = 0;
    int m_pendingDelay = 0;
    int m_pendingEveryone = 0;
};

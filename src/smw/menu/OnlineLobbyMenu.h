#pragma once

#include "uimenu.h"

#include <string>

class MI_Button;
class MI_Image;
class MI_LobbyButton;
class MI_LobbyCard;
class MI_LobbyField;
class MI_LobbyMap;
class MI_LobbyScene;
class MI_Text;
template<typename T> class MI_SelectField;

/*
    The online lobby, built from the pieces of the original menus: the map the host has selected with a
    banner of its mode and goal, the four players of the next match (character, team, ping, ready), the
    secondary settings (local players, input delay), a status bar and the ready/start/leave buttons.
    It shows the state of rollback::NetGame::instance().lobby() and sends the player's changes to it
    (see rollback/NetLobby.h).

    It is built only from the art and widgets of the original menus (see MI_LobbyWidgets.h).
*/
class UI_OnlineLobbyMenu : public UI_Menu {
public:
    enum class Dialog { None, Leave, Kick };

    UI_OnlineLobbyMenu();

    /// The lobby was opened (or the player is back from a match or the match settings)
    void Open();
    /// Updates the screen from the state of the lobby; call every frame while shown, before SendInput()
    void Sync();

    /// After the player edited a field: sends the change to the lobby
    void ApplyChanges();
    /// The host pressed "select" on a slot: adds or removes a CPU player, or asks to kick a player
    void SlotAction();
    void ToggleReady();
    /// Moves the focus to a player card
    void FocusSlot(int slot);
    /// A message for the status line, until the next match starts or the lobby closes
    void SetNotice(std::string text);

    void OpenDialog(Dialog dialog);
    void CloseDialog();
    Dialog dialog() const { return m_dialog; }
    int kickPeer() const { return m_kickPeer; }

private:
    void ApplyLayout(bool host, bool joining);
    void Focus(UI_Control* control);
    void RebuildDelayItems(int autoDelay);
    MI_Button* LeaveButton() const;

    static constexpr int SLOTS = 4;

    MI_LobbyScene* miScene = nullptr;
    MI_LobbyMap* miMap = nullptr;
    MI_LobbyCard* miCard[SLOTS] {};
    MI_LobbyField* miMode = nullptr;
    MI_LobbyField* miGoal = nullptr;
    MI_LobbyField* miMapName = nullptr;
    MI_SelectField<short>* miLocalPlayers = nullptr;
    MI_SelectField<short>* miInputDelay = nullptr;
    MI_Button* miReadyButton = nullptr;      // joiners
    MI_Button* miLeaveJoiner = nullptr;
    MI_Button* miSettingsButton = nullptr;   // the host
    MI_Button* miStartButton = nullptr;
    MI_LobbyButton* miStartDisabled = nullptr;
    MI_Button* miLeaveButton = nullptr;

    MI_Image* miDialogImage = nullptr;
    MI_Text* miDialogLine1 = nullptr;
    MI_Text* miDialogLine2 = nullptr;
    MI_Button* miDialogYes = nullptr;
    MI_Button* miDialogNo = nullptr;

    Dialog m_dialog = Dialog::None;
    int m_kickPeer = -1;
    std::string m_notice;

    int m_layoutHost = -1;       // what ApplyLayout() last did (-1: nothing yet)
    int m_layoutJoining = -1;
    int m_delayAuto = -1;        // the N of "Auto (N)" in the input delay field
    std::string m_shownMap;      // the map the preview shows ("": none yet)
    bool m_previewValid = false; // the preview shows m_shownMap
    // frames during which the state of the lobby doesn't override an edited field
    int m_pendingPlayers = 0;
    int m_pendingDelay = 0;
};

#pragma once

#include "uimenu.h"

#include <string>

class MI_DialogBox;
class MI_Button;
class MI_Image;
class MI_LobbyMap;
class MI_LockTag;
class MI_ImageSelectField;
class MI_MapField;
class MI_MapFilterScroll;
class MI_Text;
template<typename T> class MI_SelectField;


class UI_GameSettingsMenu : public UI_Menu {
public:
    UI_GameSettingsMenu();

    void OpenMapFilters();
    void CloseMapFilters();
    void OpenExitDialog();
    void CloseExitDialog();

    void SetHeaderText(const char*);
    void SetStartButtonText(const char*);
    void HideGMSettingsBtn();
    void GameModeChanged(short gmID);
    void RefreshGameModeButtons();
    short GetCurrentGameModeID();
    const char* GetCurrentMapName();
    bool IsOnStartBtn();

    /// The online lobby's view of these settings, for the players who are not the host: the settings the
    /// host has chosen, not editable (Locked: with the disabled look and a "Host" marker) or, when the host
    /// lets everyone change them, editable (Editable). The Start button becomes "Back".
    enum class OnlineView { Off, Locked, Editable };
    void SetOnlineView(OnlineView view);
    OnlineView onlineView() const { return m_onlineView; }
    /// While the map isn't here (yet): covers its picture with a text, and the progress of the download
    void SetMapOverlay(bool show, std::string text = {}, float fraction = -1.0f, std::string label = {});
    MenuCodeEnum CancelCodeNow() const;

    MI_MapFilterScroll* miMapFilterScroll = nullptr;
    MI_MapField* miMapField = nullptr;
    MI_SelectField<short>* miGoalField[22];

private:
    MI_ImageSelectField* miModeField = nullptr;
    MI_Button* miModeSettingsButton = nullptr;
    MI_Button* miSettingsStartButton = nullptr;
    MI_Button* miMapFiltersButton = nullptr;
    MI_Button* miMapThumbnailsButton = nullptr;

    MI_Image* miMapFiltersOnImage = nullptr;

    UI_Control* miGameSettingsLeftHeaderBar = nullptr;    //(the title bar)
    MI_Text* miGameSettingsMenuHeaderText = nullptr;

    MI_LockTag* miHostNote = nullptr;
    MI_LockTag* miHostTag[3] {};
    MI_LobbyMap* miMapOverlay = nullptr;
    OnlineView m_onlineView = OnlineView::Off;

    MI_DialogBox* miGameSettingsExitDialogImage = nullptr;
    MI_Text* miGameSettingsExitDialogExitText = nullptr;
    MI_Text* miGameSettingsExitDialogTournamentText = nullptr;
    MI_Button* miGameSettingsExitDialogYesButton = nullptr;
    MI_Button* miGameSettingsExitDialogNoButton = nullptr;
};

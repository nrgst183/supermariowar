#include "OnlineLobbyMenu.h"

#include "ResourceManager.h"
#include "rollback/NetGame.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_Text.h"

#include <cctype>
#include <format>


namespace {
constexpr int PENDING_FRAMES = 60;    // a change reaches the host and comes back in LOBBY_STATE

rollback::NetLobby* lobbyOrNull()
{
    return rollback::NetGame::instance().lobby();
}

std::string capitalized(std::string text)
{
    if (!text.empty())
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

constexpr short FIELD_X = 70;
constexpr short FIELD_W = 500;
constexpr short FIELD_INDENT = 200;
} // namespace


UI_OnlineLobbyMenu::UI_OnlineLobbyMenu()
    : UI_Menu()
{
    miScene = new MI_LobbyScene();

    // The team and character selection, with the buttons where the original has "Continue"
    miTeam = new MI_OnlineTeamSelect(112, 96);
    miTeam->SetAutoModify(true);
    miOptionsButton = new MI_LobbyButton(&rm->spr_selectfield, 108, 318, "Options", 200, false);
    miOptionsButton->SetCode(MENU_CODE_ONLINE_OPTIONS);
    miContinueButton = new MI_LobbyButton(&rm->spr_selectfield, 332, 318, "Continue", 200, true);
    miContinueButton->SetCode(MENU_CODE_ONLINE_MATCH_SETTINGS);

    // The options page: the settings that are rarely touched
    miLocalPlayers = new MI_SelectField<short>(&rm->spr_selectfield, FIELD_X, 92, "My players", FIELD_W, FIELD_INDENT);
    miLocalPlayers->add("1", 1);
    miLocalPlayers->add("2", 2);
    miLocalPlayers->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);

    miCpuPlayers = new MI_SelectField<short>(&rm->spr_selectfield, FIELD_X, 132, "CPU players", FIELD_W, FIELD_INDENT);
    for (short n = 0; n <= 3; n++)
        miCpuPlayers->add(std::to_string(n), n);
    miCpuPlayers->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);

    miInputDelay = new MI_SelectField<short>(&rm->spr_selectfield, FIELD_X, 172, "Input delay", FIELD_W, FIELD_INDENT);
    miInputDelay->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);
    RebuildDelayItems(2);

    miEveryone = new MI_SelectField<short>(&rm->spr_selectfield, FIELD_X, 212, "Match settings", FIELD_W, FIELD_INDENT);
    miEveryone->add("Host only", 0);
    miEveryone->add("Everyone can change", 1);
    miEveryone->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);

    miKickWho = new MI_SelectField<short>(&rm->spr_selectfield, FIELD_X, 252, "Kick player", FIELD_W, FIELD_INDENT);
    miKickWho->add("-", -1);

    miKickButton = new MI_LobbyButton(&rm->spr_selectfield, FIELD_X, 300, "Kick", 200, false);
    miKickButton->SetCode(MENU_CODE_ONLINE_KICK);
    miOptionsDone = new MI_LobbyButton(&rm->spr_selectfield, static_cast<short>(FIELD_X + FIELD_W - 200), 300, "Back", 200, true);
    miOptionsDone->SetCode(MENU_CODE_ONLINE_OPTIONS_BACK);

    // What only the host can change says so
    const short tagY[4] = {140, 180, 220, 260};
    for (int i = 0; i < 4; i++)
        miHostTag[i] = new MI_LockTag(FIELD_X + FIELD_W - 16, tagY[i], "Host");

    // Confirmation dialog (leave the lobby, kick a player)
    miDialogImage = new MI_Image(&rm->spr_dialog, 224, 176, 0, 0, 192, 128, 1, 1, 0);
    miDialogLine1 = new MI_HeaderText("Leave", 320, 195);
    miDialogLine2 = new MI_HeaderText("Lobby?", 320, 220);
    miDialogYes = new MI_Button(&rm->spr_selectfield, 235, 250, "Yes", 80, TextAlign::CENTER);
    miDialogYes->SetCode(MENU_CODE_ONLINE_LEAVE_YES);
    miDialogNo = new MI_Button(&rm->spr_selectfield, 325, 250, "No", 80, TextAlign::CENTER);
    miDialogNo->SetCode(MENU_CODE_ONLINE_LEAVE_NO);

    AddNonControl(miScene);

    // Team page: the selection, and below it the buttons (down from the selection once ready)
    AddControl(miTeam, nullptr, miContinueButton, nullptr, nullptr);
    AddControl(miOptionsButton, miTeam, nullptr, nullptr, miContinueButton);
    AddControl(miContinueButton, miTeam, nullptr, miOptionsButton, nullptr);
    miTeam->SetLeaveDown([this]() { Focus(miContinueButton); });

    // Options page
    AddControl(miLocalPlayers, nullptr, miCpuPlayers, nullptr, nullptr);
    AddControl(miCpuPlayers, miLocalPlayers, miInputDelay, nullptr, nullptr);
    AddControl(miInputDelay, miCpuPlayers, miEveryone, nullptr, nullptr);
    AddControl(miEveryone, miInputDelay, miKickWho, nullptr, nullptr);
    AddControl(miKickWho, miEveryone, miKickButton, nullptr, nullptr);
    AddControl(miKickButton, miKickWho, nullptr, nullptr, miOptionsDone);
    AddControl(miOptionsDone, miKickWho, nullptr, miKickButton, nullptr);
    for (MI_LockTag* tag : miHostTag)
        AddNonControl(tag);

    AddNonControl(miDialogImage);
    AddNonControl(miDialogLine1);
    AddNonControl(miDialogLine2);
    AddControl(miDialogYes, nullptr, nullptr, nullptr, miDialogNo);
    AddControl(miDialogNo, nullptr, nullptr, miDialogYes, nullptr);

    miDialogImage->setVisible(false);
    miDialogLine1->setVisible(false);
    miDialogLine2->setVisible(false);
    miDialogYes->setVisible(false);
    miDialogNo->setVisible(false);

    setInitialFocus(miTeam);
    SetCancelCode(MENU_CODE_ONLINE_LEAVE);
    ShowPage(Page::Team);
}

void UI_OnlineLobbyMenu::RebuildDelayItems(int autoDelay)
{
    m_delayAuto = autoDelay;
    miInputDelay->clear();
    miInputDelay->add(std::format("Auto ({})", autoDelay), -1);
    for (short delay = 1; delay <= 6; delay++)
        miInputDelay->add(std::to_string(delay), delay);
    miInputDelay->setCurrentIndex(0);
}

// The kick field lists the players of the other computers
void UI_OnlineLobbyMenu::RebuildKickItems()
{
    const rollback::NetLobby* lobby = lobbyOrNull();
    std::string signature;
    if (lobby)
        for (int p = 1; p < 4; p++)
            if (lobby->peer(p).present)
                signature += std::format("{}:{};", p, lobby->peer(p).name);
    if (signature == m_kickSignature)
        return;
    m_kickSignature = signature;

    miKickWho->clear();
    miKickWho->add("-", -1);
    if (lobby)
        for (int p = 1; p < 4; p++)
            if (lobby->peer(p).present)
                miKickWho->add(std::string(lobby->peer(p).name), static_cast<short>(p));
    miKickWho->setCurrentIndex(0);
}

void UI_OnlineLobbyMenu::Focus(UI_Control* control)
{
    if (m_currentFocus == control)
        return;
    if (m_currentFocus) {
        m_currentFocus->Modify(false);
        m_currentFocus->Select(false);
    }
    m_currentFocus = control;
    fModifyingItem = m_currentFocus ? m_currentFocus->Select(true) : false;
}

void UI_OnlineLobbyMenu::ShowPage(Page page)
{
    m_page = page;
    const bool team = page == Page::Team;
    const rollback::NetLobby* lobby = lobbyOrNull();
    const bool joining = !lobby || lobby->phase() == rollback::NetLobby::Phase::Joining;

    miTeam->setVisible(team && !joining);
    miOptionsButton->setVisible(team && !joining);
    miContinueButton->setVisible(team && !joining);
    for (UI_Control* control : std::initializer_list<UI_Control*> {miLocalPlayers, miCpuPlayers, miInputDelay, miEveryone, miKickWho, miKickButton, miOptionsDone})
        control->setVisible(!team && !joining);
    for (MI_LockTag* tag : miHostTag)
        tag->setVisible(!team && !joining);
    miScene->SetTitle(team ? "Team and Character Selection" : "Online Options");

    SetCancelCode(team ? MENU_CODE_ONLINE_LEAVE : MENU_CODE_ONLINE_OPTIONS_BACK);
    if (m_dialog == Dialog::None)
        Focus(team ? static_cast<UI_Control*>(miTeam) : miLocalPlayers);
    m_layoutHost = m_layoutJoining = -1;
}

void UI_OnlineLobbyMenu::Open(bool fresh)
{
    m_notice.clear();
    m_layoutHost = -1;
    m_layoutJoining = -1;
    m_pendingPlayers = m_pendingCpus = m_pendingDelay = m_pendingEveryone = 0;
    m_kickSignature = "?";

    if (m_dialog != Dialog::None)
        CloseDialog();

    if (fresh)
        miTeam->Reset();
    ShowPage(Page::Team);
    Sync();
}

void UI_OnlineLobbyMenu::SetNotice(std::string text)
{
    m_notice = std::move(text);
}

// What there is on screen: joining shows only the status; the host's button goes to the match type and
// game settings, the others' to a look at them; the options only the host can change look disabled
void UI_OnlineLobbyMenu::ApplyLayout(bool host, bool joining)
{
    if (m_layoutHost == static_cast<int>(host) && m_layoutJoining == static_cast<int>(joining))
        return;
    m_layoutHost = host;
    m_layoutJoining = joining;

    miScene->SetJoining(joining);
    const bool team = m_page == Page::Team;
    miTeam->setVisible(team && !joining);
    miOptionsButton->setVisible(team && !joining);
    miContinueButton->setVisible(team && !joining);
    for (UI_Control* control : std::initializer_list<UI_Control*> {miLocalPlayers, miCpuPlayers, miInputDelay, miEveryone, miKickWho, miKickButton, miOptionsDone})
        control->setVisible(!team && !joining);
    for (MI_LockTag* tag : miHostTag)
        tag->setVisible(!team && !joining && !host);

    miContinueButton->SetName(host ? "Continue" : "Settings");
    miContinueButton->SetCode(host ? MENU_CODE_ONLINE_MATCH_SETTINGS : MENU_CODE_ONLINE_VIEW_SETTINGS);

    for (UI_Control* control : std::initializer_list<UI_Control*> {miCpuPlayers, miInputDelay, miEveryone, miKickWho, miKickButton})
        control->SetLocked(!host);

    // While joining the leave dialog is the only thing to do: the cancel button leaves right away
    if (joining) {
        Focus(nullptr);
        SetCancelCode(MENU_CODE_ONLINE_LEAVE);
    }
}

void UI_OnlineLobbyMenu::Sync()
{
    rollback::NetLobby* lobby = lobbyOrNull();
    if (!lobby)
        return;

    using Phase = rollback::NetLobby::Phase;
    const Phase phase = lobby->phase();
    const bool host = lobby->isHost();
    const bool joining = phase == Phase::Joining;
    const int local = lobby->localPeer();
    const bool inLobby = phase == Phase::Lobby;

    ApplyLayout(host, joining);
    if (!joining && !m_currentFocus && m_dialog == Dialog::None)
        Focus(m_page == Page::Team ? static_cast<UI_Control*>(miTeam) : miLocalPlayers);

    // The options page
    if (local >= 0) {
        if (m_pendingPlayers > 0)
            --m_pendingPlayers;
        if (m_pendingPlayers == 0 && !miLocalPlayers->IsModifying())
            miLocalPlayers->setCurrentValue(static_cast<short>(lobby->peer(local).localPlayers));
        miLocalPlayers->Disable(!inLobby);
    }

    int cpus = 0;
    for (int s = 0; s < 4; s++)
        cpus += lobby->slot(s).kind == rollback::SlotKind::Cpu;
    if (m_pendingCpus > 0)
        --m_pendingCpus;
    if (m_pendingCpus == 0 && !miCpuPlayers->IsModifying())
        miCpuPlayers->setCurrentValue(static_cast<short>(cpus));

    const int delay = lobby->inputDelay();
    const bool automatic = lobby->inputDelayIsAuto();
    if (m_pendingDelay > 0)
        --m_pendingDelay;
    if (m_pendingDelay == 0 && !miInputDelay->IsModifying()) {
        if (automatic && delay != m_delayAuto)
            RebuildDelayItems(delay);
        miInputDelay->setCurrentValue(static_cast<short>(automatic ? -1 : delay));
    }

    if (m_pendingEveryone > 0)
        --m_pendingEveryone;
    if (m_pendingEveryone == 0 && !miEveryone->IsModifying())
        miEveryone->setCurrentValue(lobby->settingsEveryone() ? 1 : 0);

    RebuildKickItems();
    miKickButton->Disable(!host || miKickWho->currentValue() < 0);

    // That the match settings are the host's, or everyone's, in the header of the team page
    miScene->SetSettingsNote(m_page == Page::Team && !joining ? (lobby->settingsEveryone() ? "Match settings: everyone can change" : "Match settings: host only") : std::string());

    // The status line: what the lobby says, else what is going on with the players. The hint under
    // the buttons says what the player can do.
    std::string why;
    const bool canStart = host && inLobby && lobby->canStart(why);
    using Tone = MI_LobbyScene::Tone;
    std::string status = lobby->statusText();
    Tone tone = Tone::Info;
    if (status.empty()) {
        status = m_notice;
        tone = Tone::Warn;
    }
    if (contains(status, "abort") || contains(status, "ailed") || contains(status, "lost") || contains(status, "losed") || contains(status, "an't")
        || contains(status, "removed") || contains(status, "efused"))
        tone = Tone::Error;
    if (status.empty() && joining)
        status = "Connecting to the host...";
    if (status.empty() && phase == Phase::Starting)
        status = "Starting the match...";
    if (status.empty() && inLobby && local >= 0) {
        if (host) {
            status = canStart ? "Everyone is ready: select Continue" : capitalized(why);
            tone = canStart ? Tone::Good : Tone::Warn;
        } else {
            const bool ready = lobby->peer(local).ready;
            status = ready ? "Waiting for the host to start the match" : "Pick your character and team, then get ready";
            tone = ready ? Tone::Good : Tone::Info;
        }
    }
    m_statusLine = status;

    std::string hint;
    if (m_dialog == Dialog::None && m_page == Page::Team && !joining) {
        if (m_currentFocus == miTeam)
            hint = miTeam->hint();
        else if (m_currentFocus == miContinueButton)
            hint = host ? "Choose the match type, game mode and map" : "See what the host has chosen";
        else if (m_currentFocus == miOptionsButton)
            hint = host ? "Players, CPUs, input delay, who changes the settings" : "Your players on this computer";
    } else if (m_dialog == Dialog::None && !joining) {
        if (!host && (m_currentFocus == miCpuPlayers || m_currentFocus == miInputDelay || m_currentFocus == miEveryone || m_currentFocus == miKickWho))
            hint = "Only the host can change this";
        else if (m_currentFocus == miLocalPlayers)
            hint = "Players on this computer (the second uses the second controls)";
        else if (m_currentFocus == miInputDelay)
            hint = "How many frames inputs are held back, to hide the lag";
        else if (m_currentFocus == miEveryone)
            hint = "Whether the others can also change the mode, goal and map";
    }
    miScene->SetStatus(status, tone, hint);
}

void UI_OnlineLobbyMenu::ApplyChanges()
{
    rollback::NetLobby* lobby = lobbyOrNull();
    if (!lobby || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return;

    const bool host = lobby->isHost();
    const int local = lobby->localPeer();

    if (local >= 0 && miLocalPlayers->currentValue() != lobby->peer(local).localPlayers) {
        lobby->setLocalPlayers(miLocalPlayers->currentValue());
        m_pendingPlayers = PENDING_FRAMES;
    }

    if (host) {
        int cpus = 0;
        for (int s = 0; s < 4; s++)
            cpus += lobby->slot(s).kind == rollback::SlotKind::Cpu;
        if (miCpuPlayers->currentValue() != cpus) {
            lobby->setCpuCount(miCpuPlayers->currentValue());
            m_pendingCpus = PENDING_FRAMES;
        }

        const short wanted = miInputDelay->currentValue();
        const short current = static_cast<short>(lobby->inputDelayIsAuto() ? -1 : lobby->inputDelay());
        if (wanted != current) {
            lobby->setInputDelay(wanted);
            m_pendingDelay = PENDING_FRAMES;
        }

        const bool everyone = miEveryone->currentValue() != 0;
        if (everyone != lobby->settingsEveryone()) {
            lobby->setSettingsEveryone(everyone);
            m_pendingEveryone = PENDING_FRAMES;
        }
    }
}

void UI_OnlineLobbyMenu::KickSelected()
{
    const rollback::NetLobby* lobby = lobbyOrNull();
    if (!lobby || !lobby->isHost() || miKickWho->currentValue() <= 0)
        return;
    m_kickPeer = miKickWho->currentValue();
    OpenDialog(Dialog::Kick);
}

void UI_OnlineLobbyMenu::OpenDialog(Dialog dialog)
{
    const rollback::NetLobby* lobby = lobbyOrNull();
    m_dialog = dialog;

    if (dialog == Dialog::Kick) {
        miDialogLine1->SetText("Kick");
        miDialogLine2->SetText("Player?");
    } else {
        miDialogLine1->SetText(lobby && lobby->isHost() ? "Close" : "Leave");
        miDialogLine2->SetText("Lobby?");
    }

    miDialogImage->setVisible(true);
    miDialogLine1->setVisible(true);
    miDialogLine2->setVisible(true);
    miDialogYes->setVisible(true);
    miDialogNo->setVisible(true);

    RememberCurrent();

    setInitialFocus(miDialogNo);
    SetCancelCode(MENU_CODE_ONLINE_LEAVE_NO);
    ResetMenu();
}

void UI_OnlineLobbyMenu::CloseDialog()
{
    if (m_dialog == Dialog::None)
        return;
    m_dialog = Dialog::None;
    m_kickPeer = -1;

    miDialogImage->setVisible(false);
    miDialogLine1->setVisible(false);
    miDialogLine2->setVisible(false);
    miDialogYes->setVisible(false);
    miDialogNo->setVisible(false);

    setInitialFocus(m_page == Page::Team ? static_cast<UI_Control*>(miTeam) : miLocalPlayers);
    SetCancelCode(m_page == Page::Team ? MENU_CODE_ONLINE_LEAVE : MENU_CODE_ONLINE_OPTIONS_BACK);

    RestoreCurrent();
}

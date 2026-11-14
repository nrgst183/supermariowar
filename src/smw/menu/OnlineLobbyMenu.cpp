#include "OnlineLobbyMenu.h"

#include "CmdArgs.h"
#include "MapList.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_Text.h"

#include <cctype>
#include <format>


namespace {
constexpr int PENDING_FRAMES = 60;    // a joiner's change reaches the host and comes back in LOBBY_STATE

rollback::NetLobby* lobbyOrNull()
{
    return rollback::NetGame::instance().lobby();
}
} // namespace


namespace {
// The layout of the lobby when nothing asks for another one (--at-lobby-style)
constexpr LobbyStyle DEFAULT_STYLE = LobbyStyle::Stage;
} // namespace


UI_OnlineLobbyMenu::UI_OnlineLobbyMenu()
    : UI_Menu()
{
    const int wanted = cmd::g_autotest.lobbyStyle;
    m_style = wanted >= 0 && wanted < LOBBY_STYLE_COUNT ? static_cast<LobbyStyle>(wanted) : DEFAULT_STYLE;
    const LobbyLayout layout = lobbyLayout(m_style);

    miScene = new MI_LobbyScene(m_style);
    miMap = new MI_LobbyMap(layout.mapFrame, layout.map, m_style);

    // The four players of the next match
    for (int i = 0; i < SLOTS; i++)
        miCard[i] = new MI_LobbyCard(layout.card[i], i, m_style);

    // The settings that are rarely touched
    miLocalPlayers = new MI_LobbyChip(layout.chip[0], "Players", MI_LobbyChip::Icon::Players);
    miLocalPlayers->add("1", 1);
    miLocalPlayers->add("2", 2);
    miLocalPlayers->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);

    miInputDelay = new MI_LobbyChip(layout.chip[1], "Delay", MI_LobbyChip::Icon::Delay);
    miInputDelay->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);
    RebuildDelayItems(2);

    // The buttons: the host has start, match settings and leave in three equal columns, the others
    // ready and leave in two
    const short y = layout.buttonY;
    const short w = layout.buttonW;
    miStartButton = new MI_LobbyButton(&rm->spr_selectfield, layout.buttonX[0], y, "Start match", w, true);
    miStartButton->SetCode(MENU_CODE_ONLINE_START);
    //Shown instead of the start button while the match can't start (it can't be selected)
    miStartDisabled = new MI_LobbyButton(&rm->spr_selectfield, layout.buttonX[0], y, "Start match", w, false);
    miStartDisabled->SetDimmed(true);
    miSettingsButton = new MI_LobbyButton(&rm->spr_selectfield, layout.buttonX[1], y, "Match settings", w, false);
    miSettingsButton->SetCode(MENU_CODE_ONLINE_MATCH_SETTINGS);
    miLeaveButton = new MI_LobbyButton(&rm->spr_selectfield, layout.buttonX[2], y, "Leave", w, false);
    miLeaveButton->SetCode(MENU_CODE_ONLINE_LEAVE);
    miReadyButton = new MI_LobbyButton(&rm->spr_selectfield, layout.buttonX[0], y, "Ready", layout.joinerW, true);
    miReadyButton->SetCode(MENU_CODE_ONLINE_READY);
    miLeaveJoiner = new MI_LobbyButton(&rm->spr_selectfield, static_cast<short>(layout.buttonX[0] + layout.joinerW + 12), y, "Leave", layout.joinerW, false);
    miLeaveJoiner->SetCode(MENU_CODE_ONLINE_LEAVE);

    // Confirmation dialog (leave the lobby, kick a player)
    miDialogImage = new MI_Image(&rm->spr_dialog, 224, 176, 0, 0, 192, 128, 1, 1, 0);
    miDialogLine1 = new MI_HeaderText("Leave", 320, 195);
    miDialogLine2 = new MI_HeaderText("Lobby?", 320, 220);
    miDialogYes = new MI_Button(&rm->spr_selectfield, 235, 250, "Yes", 80, TextAlign::CENTER);
    miDialogYes->SetCode(MENU_CODE_ONLINE_LEAVE_YES);
    miDialogNo = new MI_Button(&rm->spr_selectfield, 325, 250, "No", 80, TextAlign::CENTER);
    miDialogNo->SetCode(MENU_CODE_ONLINE_LEAVE_NO);

    AddNonControl(miScene);
    AddNonControl(miMap);
    AddNonControl(miStartDisabled);

    // Navigation. The Stage layout has the cards side by side, the Cards layout in a column; below them
    // the settings, then the buttons: ready (joiners) or start (the host, when it can start) | match
    // settings | leave. Hidden controls are skipped by following their neighbors, so the "down" chain
    // runs ready, start, settings. Leave is reached from everywhere by the cancel button.
    const bool stage = m_style == LobbyStyle::Stage;
    for (int i = 0; i < SLOTS; i++) {
        UI_Control* up = i == 0 ? nullptr : miCard[i - 1];
        UI_Control* down = i == SLOTS - 1 ? static_cast<UI_Control*>(miLocalPlayers) : miCard[i + 1];
        UI_Control* left = nullptr;
        UI_Control* right = nullptr;
        if (stage) {
            up = nullptr;
            down = i < 2 ? static_cast<UI_Control*>(miLocalPlayers) : miInputDelay;
            left = i == 0 ? nullptr : miCard[i - 1];
            right = i == SLOTS - 1 ? nullptr : miCard[i + 1];
        }
        AddControl(miCard[i], up, down, left, right);
    }
    UI_Control* chipUp0 = stage ? static_cast<UI_Control*>(miCard[1]) : miCard[SLOTS - 1];
    UI_Control* chipUp1 = stage ? static_cast<UI_Control*>(miCard[2]) : miCard[SLOTS - 1];
    AddControl(miLocalPlayers, chipUp0, miReadyButton, nullptr, miInputDelay);
    AddControl(miInputDelay, chipUp1, miLeaveJoiner, miLocalPlayers, nullptr);
    AddControl(miReadyButton, miLocalPlayers, miStartButton, nullptr, miLeaveJoiner);
    AddControl(miLeaveJoiner, miInputDelay, miLeaveButton, miReadyButton, nullptr);
    AddControl(miStartButton, miLocalPlayers, miSettingsButton, miReadyButton, miSettingsButton);
    AddControl(miSettingsButton, miLocalPlayers, nullptr, miStartButton, miLeaveButton);
    AddControl(miLeaveButton, miInputDelay, nullptr, miSettingsButton, nullptr);

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

    setInitialFocus(miLeaveJoiner);
    SetCancelCode(MENU_CODE_ONLINE_LEAVE);
}

MI_Button* UI_OnlineLobbyMenu::LeaveButton() const
{
    return m_layoutHost == 1 ? miLeaveButton : miLeaveJoiner;
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

void UI_OnlineLobbyMenu::Open()
{
    m_notice.clear();
    m_layoutHost = -1;
    m_layoutJoining = -1;
    m_previewValid = false;
    m_shownMap.clear();
    m_pendingPlayers = m_pendingDelay = 0;

    if (m_dialog != Dialog::None)
        CloseDialog();

    const rollback::NetLobby* lobby = lobbyOrNull();
    const bool host = lobby && lobby->isHost();
    const bool joining = !lobby || lobby->phase() == rollback::NetLobby::Phase::Joining;
    ApplyLayout(host, joining);

    setInitialFocus(joining ? static_cast<UI_Control*>(LeaveButton()) : host ? static_cast<UI_Control*>(miSettingsButton) : miReadyButton);
    Sync();
}

void UI_OnlineLobbyMenu::SetNotice(std::string text)
{
    m_notice = std::move(text);
}

// Which controls there are: joining shows only the status; the host has the match settings, start
// and the input delay; the others have the ready button and see the input delay
void UI_OnlineLobbyMenu::ApplyLayout(bool host, bool joining)
{
    if (m_layoutHost == static_cast<int>(host) && m_layoutJoining == static_cast<int>(joining))
        return;
    m_layoutHost = host;
    m_layoutJoining = joining;

    miScene->SetJoining(joining);
    miMap->setVisible(!joining);
    for (int i = 0; i < SLOTS; i++)
        miCard[i]->setVisible(!joining);
    miLocalPlayers->setVisible(!joining);
    miInputDelay->setVisible(!joining);
    miReadyButton->setVisible(!joining && !host);
    miLeaveJoiner->setVisible(!host);
    miSettingsButton->setVisible(!joining && host);
    miStartButton->setVisible(!joining && host);
    miStartDisabled->setVisible(!joining && host);
    miLeaveButton->setVisible(host);

    // While joining there is only the leave button: it takes the width of the whole row
    // The focused control may be gone now
    if (m_currentFocus && !m_currentFocus->IsVisible() && m_dialog == Dialog::None)
        Focus(joining ? static_cast<UI_Control*>(LeaveButton()) : host ? static_cast<UI_Control*>(miSettingsButton) : miReadyButton);
}

namespace {
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
} // namespace

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

    // Local players
    if (local >= 0) {
        const rollback::LobbyPeer& self = lobby->peer(local);
        if (m_pendingPlayers > 0)
            --m_pendingPlayers;
        if (m_pendingPlayers == 0 && !miLocalPlayers->IsModifying())
            miLocalPlayers->setCurrentValue(static_cast<short>(self.localPlayers));
        miLocalPlayers->Disable(!inLobby);
        miReadyButton->SetName(self.ready ? "Cancel ready" : "Ready");
        miReadyButton->Disable(!inLobby);
    }

    // Input delay
    const int delay = lobby->inputDelay();
    const bool automatic = lobby->inputDelayIsAuto();
    miInputDelay->Disable(!inLobby || !host);
    if (m_pendingDelay > 0)
        --m_pendingDelay;
    if (m_pendingDelay == 0 && !miInputDelay->IsModifying()) {
        if (automatic && delay != m_delayAuto)
            RebuildDelayItems(delay);
        miInputDelay->setCurrentValue(static_cast<short>(automatic ? -1 : delay));
    }
    miSettingsButton->Disable(!inLobby);

    // The map: its preview, or a text while it is not here (with the progress of the transfer)
    const std::string& mapName = lobby->mapName();
    const bool minigame = mapName == "(minigame)";
    const rollback::MapTransferInfo& transfer = lobby->mapTransfer();
    const bool transferring = transfer.active && transfer.bytesTotal > 0;
    if (!joining && (!m_previewValid || mapName != m_shownMap)) {
        m_shownMap = mapName;
        m_previewValid = !mapName.empty() && !minigame && maplist->findexact(mapName.c_str(), false);
        if (m_previewValid)
            miMap->LoadCurrentMap();
    }
    miMap->SetAvailable(m_previewValid && !joining);
    if (mapName.empty())
        miMap->SetText("No map selected yet");
    else if (minigame)
        miMap->SetText(lobby->modeName().empty() ? "Minigame" : lobby->modeName());
    else if (!m_previewValid)
        miMap->SetText(transferring ? mapName : mapName + "\nnot here yet");
    else
        miMap->SetText({});
    if (transferring && !joining) {
        const float fraction = static_cast<float>(transfer.bytesDone) / static_cast<float>(transfer.bytesTotal);
        miMap->SetProgress(fraction, std::format("{} {}%", transfer.sending ? "Uploading" : "Downloading", static_cast<int>(fraction * 100.0f)));
    } else {
        miMap->SetProgress(-1.0f, {});
    }

    // Start
    std::string why;
    const bool canStart = host && inLobby && lobby->canStart(why);
    miStartButton->setVisible(!joining && canStart);
    miStartDisabled->setVisible(!joining && host && !canStart);
    if (!canStart && m_currentFocus == miStartButton && m_dialog == Dialog::None)
        Focus(miSettingsButton);

    // The status line: what the lobby says, else what is going on with the players; its color says how
    // good or bad it is. The hint under it says what the focused control does.
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
    else if (contains(status, "ownload") || contains(status, "pload"))
        tone = Tone::Info;
    if (status.empty() && joining)
        status = "Connecting to the host...";
    if (status.empty() && phase == Phase::Starting)
        status = "Starting the match...";
    if (status.empty() && inLobby && local >= 0) {
        if (host) {
            status = canStart ? "Everyone is ready: start the match" : capitalized(why);
            tone = canStart ? Tone::Good : Tone::Warn;
        } else {
            const bool ready = lobby->peer(local).ready;
            status = ready ? "Waiting for the host to start the match" : "Select Ready when you are set";
            tone = ready ? Tone::Good : Tone::Info;
        }
    }

    std::string hint;
    if (m_dialog == Dialog::None) {
        if (const MI_LobbyCard* card = dynamic_cast<const MI_LobbyCard*>(m_currentFocus))
            hint = card->hint();
        else if (m_currentFocus == miInputDelay && !host)
            hint = "Only the host can change the input delay";
        else if (m_currentFocus == miInputDelay)
            hint = "How many frames inputs are held back, to hide the lag";
        else if (m_currentFocus == miLocalPlayers)
            hint = "Players on this computer: a second one plays with the second controls";
        else if (m_currentFocus == miSettingsButton)
            hint = "Choose the game mode, goal and map";
    }
    miScene->SetStatus(status, tone, capitalized(hint));
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
        const short wanted = miInputDelay->currentValue();
        const short current = static_cast<short>(lobby->inputDelayIsAuto() ? -1 : lobby->inputDelay());
        if (wanted != current) {
            lobby->setInputDelay(wanted);
            m_pendingDelay = PENDING_FRAMES;
        }
    }
}

void UI_OnlineLobbyMenu::SlotAction()
{
    rollback::NetLobby* lobby = lobbyOrNull();
    const MI_LobbyCard* control = dynamic_cast<const MI_LobbyCard*>(m_currentFocus);
    if (!lobby || !control || !lobby->isHost())
        return;

    fModifyingItem = false;
    const int index = control->slot();
    const rollback::LobbySlot& slot = lobby->slot(index);
    switch (slot.kind) {
        case rollback::SlotKind::Empty:
            lobby->addCpu();
            break;
        case rollback::SlotKind::Cpu:
            lobby->removeCpu(index);
            break;
        case rollback::SlotKind::Human:
            if (slot.peer != lobby->localPeer()) {
                m_kickPeer = slot.peer;
                OpenDialog(Dialog::Kick);
            }
            break;
    }
}

void UI_OnlineLobbyMenu::ToggleReady()
{
    rollback::NetLobby* lobby = lobbyOrNull();
    if (!lobby || lobby->localPeer() < 0 || lobby->phase() != rollback::NetLobby::Phase::Lobby)
        return;
    lobby->setReady(!lobby->peer(lobby->localPeer()).ready);
}

void UI_OnlineLobbyMenu::FocusSlot(int slot)
{
    if (slot >= 0 && slot < SLOTS && m_dialog == Dialog::None)
        Focus(miCard[slot]);
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
    m_dialog = Dialog::None;
    m_kickPeer = -1;

    miDialogImage->setVisible(false);
    miDialogLine1->setVisible(false);
    miDialogLine2->setVisible(false);
    miDialogYes->setVisible(false);
    miDialogNo->setVisible(false);

    setInitialFocus(LeaveButton());
    SetCancelCode(MENU_CODE_ONLINE_LEAVE);

    RestoreCurrent();
}

#include "OnlineLobbyMenu.h"

#include "MapList.h"
#include "ResourceManager.h"
#include "rollback/NetGame.h"
#include "ui/MI_Button.h"
#include "ui/MI_Image.h"
#include "ui/MI_LobbyWidgets.h"
#include "ui/MI_MapPreview.h"
#include "ui/MI_SelectField.h"
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


UI_OnlineLobbyMenu::UI_OnlineLobbyMenu()
    : UI_Menu()
{
    // The map the host has selected, framed like in the match menus (the frame's top is at y = 4)
    miMapPreview = new MI_MapPreview(&rm->spr_selectfield, 8, -26, 352, 0);
    miMapBox = new MI_MapBox(8, 4);
    miCaption = new MI_WrappedText(184, 280, 352, 1);

    // The four players of the next match
    for (int i = 0; i < SLOTS; i++)
        miCard[i] = new MI_LobbyCard(368, static_cast<short>(8 + i * 66), 264, 62, i);

    // The settings that are rarely touched
    miLocalPlayers = new MI_SelectField<short>(&rm->spr_selectfield, 20, 316, "Local players", 300, 200);
    miLocalPlayers->add("1", 1);
    miLocalPlayers->add("2", 2);
    miLocalPlayers->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);

    miInputDelay = new MI_SelectField<short>(&rm->spr_selectfield, 330, 316, "Input delay", 290, 150);
    miInputDelay->setItemChangedCode(MENU_CODE_ONLINE_LOBBY_CHANGED);
    RebuildDelayItems(2);

    miHint = new MI_WrappedText(320, 360, 600, 1);
    miStatus = new MI_WrappedText(320, 392, 600, 1);

    // The buttons
    miReadyButton = new MI_Button(&rm->spr_selectfield, 20, 432, "Not ready", 504, TextAlign::CENTER);
    miReadyButton->SetCode(MENU_CODE_ONLINE_READY);
    miStartButton = new MI_Button(&rm->spr_selectfield, 20, 432, "Start match", 270, TextAlign::CENTER);
    miStartButton->SetCode(MENU_CODE_ONLINE_START);
    //Shown instead of the start button while the match can't start (it can't be selected)
    miStartDisabled = new MI_Button(&rm->spr_selectfielddisabled, 20, 432, "Start match", 270, TextAlign::CENTER);
    miSettingsButton = new MI_Button(&rm->spr_selectfield, 300, 432, "Match settings", 224, TextAlign::CENTER);
    miSettingsButton->SetCode(MENU_CODE_ONLINE_MATCH_SETTINGS);
    miLeaveButton = new MI_Button(&rm->spr_selectfield, 534, 432, "Leave", 86, TextAlign::CENTER);
    miLeaveButton->SetCode(MENU_CODE_ONLINE_LEAVE);

    // Confirmation dialog (leave the lobby, kick a player)
    miDialogImage = new MI_Image(&rm->spr_dialog, 224, 176, 0, 0, 192, 128, 1, 1, 0);
    miDialogLine1 = new MI_HeaderText("Leave", 320, 195);
    miDialogLine2 = new MI_HeaderText("Lobby?", 320, 220);
    miDialogYes = new MI_Button(&rm->spr_selectfield, 235, 250, "Yes", 80, TextAlign::CENTER);
    miDialogYes->SetCode(MENU_CODE_ONLINE_LEAVE_YES);
    miDialogNo = new MI_Button(&rm->spr_selectfield, 325, 250, "No", 80, TextAlign::CENTER);
    miDialogNo->SetCode(MENU_CODE_ONLINE_LEAVE_NO);

    AddNonControl(miMapPreview);
    AddNonControl(miMapBox);
    AddNonControl(miCaption);
    AddNonControl(miStartDisabled);
    AddNonControl(miHint);
    AddNonControl(miStatus);

    // Navigation: the cards, the settings row, then the buttons (ready or start | settings | leave);
    // leave is reached from everywhere by the cancel button
    for (int i = 0; i < SLOTS; i++)
        AddControl(miCard[i], i == 0 ? nullptr : miCard[i - 1], i == SLOTS - 1 ? static_cast<UI_Control*>(miLocalPlayers) : miCard[i + 1], nullptr, nullptr);
    AddControl(miLocalPlayers, miCard[SLOTS - 1], miReadyButton, nullptr, miInputDelay);
    AddControl(miInputDelay, miCard[SLOTS - 1], miSettingsButton, miLocalPlayers, nullptr);
    AddControl(miReadyButton, miLocalPlayers, miStartButton, nullptr, miStartButton);
    AddControl(miStartButton, miLocalPlayers, miSettingsButton, miReadyButton, miSettingsButton);
    AddControl(miSettingsButton, miInputDelay, nullptr, miStartButton, miLeaveButton);
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

    setInitialFocus(miLeaveButton);
    SetCancelCode(MENU_CODE_ONLINE_LEAVE);
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

    setInitialFocus(joining ? static_cast<UI_Control*>(miLeaveButton) : host ? static_cast<UI_Control*>(miSettingsButton) : miReadyButton);
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

    miMapPreview->setVisible(!joining);
    miCaption->setVisible(!joining);
    for (int i = 0; i < SLOTS; i++)
        miCard[i]->setVisible(!joining);
    miLocalPlayers->setVisible(!joining);
    miInputDelay->setVisible(!joining);
    miReadyButton->setVisible(!joining && !host);
    miSettingsButton->setVisible(!joining && host);
    miStartButton->setVisible(!joining && host);
    miStartDisabled->setVisible(!joining && host);
    miMapBox->setVisible(true);

    // The focused control may be gone now
    if (m_currentFocus && !m_currentFocus->IsVisible() && m_dialog == Dialog::None)
        Focus(joining ? static_cast<UI_Control*>(miLeaveButton) : host ? static_cast<UI_Control*>(miSettingsButton) : miReadyButton);
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

    // Local players
    if (local >= 0) {
        const rollback::LobbyPeer& self = lobby->peer(local);
        if (m_pendingPlayers > 0)
            --m_pendingPlayers;
        if (m_pendingPlayers == 0 && !miLocalPlayers->IsModifying())
            miLocalPlayers->setCurrentValue(static_cast<short>(self.localPlayers));
        miLocalPlayers->Disable(!inLobby);
        miReadyButton->SetName(self.ready ? "Ready - select to cancel" : "Ready");
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

    // The map: its preview and the caption "Mode - Map"
    const std::string& mapName = lobby->mapName();
    const rollback::MapTransferInfo& transfer = lobby->mapTransfer();
    const bool transferring = transfer.active && transfer.bytesTotal > 0;
    if (!joining && (!m_previewValid || mapName != m_shownMap)) {
        m_shownMap = mapName;
        m_previewValid = !mapName.empty() && maplist->findexact(mapName.c_str(), false);
        if (m_previewValid)
            miMapPreview->LoadCurrentMap();
    }
    if (joining)
        miMapBox->SetText("Connecting...");
    else if (mapName.empty())
        miMapBox->SetText("No map selected yet");
    else if (!m_previewValid)
        miMapBox->SetText(transferring ? mapName : mapName + "\nis being transferred...");
    else
        miMapBox->SetText({});
    if (transferring && !joining) {
        const float fraction = static_cast<float>(transfer.bytesDone) / static_cast<float>(transfer.bytesTotal);
        miMapBox->SetProgress(fraction, std::format("{} map {}%", transfer.sending ? "Uploading" : "Downloading", static_cast<int>(fraction * 100.0f)));
    } else {
        miMapBox->SetProgress(-1.0f, {});
    }
    std::string caption = lobby->modeName().empty() ? "" : lobby->modeName();
    if (!mapName.empty())
        caption += (caption.empty() ? "" : " - ") + mapName;
    miCaption->SetText(caption);

    // Start
    std::string why;
    const bool canStart = host && inLobby && lobby->canStart(why);
    miStartButton->setVisible(!joining && canStart);
    miStartDisabled->setVisible(!joining && host && !canStart);
    if (!canStart && m_currentFocus == miStartButton && m_dialog == Dialog::None)
        Focus(miSettingsButton);

    // Status
    std::string status = lobby->statusText();
    if (status.empty())
        status = m_notice;
    if (status.empty() && joining)
        status = "Connecting to the host...";
    if (status.empty() && phase == Phase::Starting)
        status = "Starting the match...";
    miStatus->SetText(status);

    // The hint line: what the focused control does, or why the host can't start
    std::string hint;
    if (m_dialog == Dialog::None) {
        if (const MI_LobbyCard* card = dynamic_cast<const MI_LobbyCard*>(m_currentFocus))
            hint = card->hint();
        else if (m_currentFocus == miInputDelay && !host)
            hint = "Only the host can change the input delay";
        else if (m_currentFocus == miLocalPlayers)
            hint = "Players on this computer: a second one plays with the second controls";
    }
    if (hint.empty() && !host && inLobby && m_dialog == Dialog::None && local >= 0)
        hint = lobby->peer(local).ready ? "Waiting for the host to start the match" : "Select Ready when you are set";
    if (hint.empty() && host && inLobby && !canStart && !why.empty())
        hint = why;
    if (!hint.empty())
        hint[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(hint[0])));
    miHint->SetText(hint);
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

    setInitialFocus(miLeaveButton);
    SetCancelCode(MENU_CODE_ONLINE_LEAVE);

    RestoreCurrent();
}

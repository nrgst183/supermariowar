// The online (rollback netplay) part of MenuState: the Online menu, the lobby and the way from the
// lobby to the usual match menus and back. The backend is rollback/NetGame.h and NetLobby.h.
#include "GSMenu.h"

#include "rollback/NetGame.h"

#include "GameValues.h"
#include "MapList.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "ui/MI_MapField.h"
#include "ui/MI_SelectField.h"

#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"
#include "menu/SystemLinkMenu.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

namespace {

std::string trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

} // namespace

// Opens a lobby on the port (0: the default), open to `access`, under this computer's name; false
// and why if it can't
bool MenuState::HostLobby(int port, rollback::LobbyAccess access, std::string& error)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (port < 0 || port > 65535) {
        error = "The port must be a number from 1 to 65535";
        return false;
    }
    if (!net.host(static_cast<uint16_t>(port), error, access)) {
        error = "Can't host a game: " + error;
        return false;
    }

    if (trimmed(game_values.onlineport).empty())
        game_values.onlineport = "7300";
    game_values.onlinename = trimmed(game_values.onlinename);
    net.lobby()->setName(systemLinkPlayerName());
    game_values.WriteConfig();
    return true;
}

void MenuState::HostOnlineGame()
{
    mOnlineMenu->ClearMessage();
    std::string error;
    if (!HostLobby(mOnlineMenu->port(), rollback::LobbyAccess::Internet, error)) {
        mOnlineMenu->ShowMessage(error);
        return;
    }
    mLobbyOrigin = LobbyOrigin::OnlineMenu;
    EnterOnlineLobby();
}

// "Host a game" in the System Link (LAN games) menu: found there by the others on the network
void MenuState::HostSystemLinkGame()
{
    mSystemLinkMenu->ClearMessage();
    std::string error;
    if (!HostLobby(atoi(game_values.onlineport.c_str()), rollback::LobbyAccess::Lan, error)) {
        mSystemLinkMenu->ShowMessage(error);
        return;
    }
    mSystemLinkMenu->Close();
    mLobbyOrigin = LobbyOrigin::SystemLink;
    EnterOnlineLobby();
}

void MenuState::JoinOnlineGame()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    mOnlineMenu->ClearMessage();

    game_values.onlineaddress = trimmed(game_values.onlineaddress);
    if (game_values.onlineaddress.empty()) {
        mOnlineMenu->ShowMessage("Enter the address of the host, like 192.168.1.20:7300");
        return;
    }

    std::string error;
    if (!net.join(game_values.onlineaddress, error)) {
        mOnlineMenu->ShowMessage("Can't join: " + error);
        return;
    }

    game_values.onlinename = trimmed(game_values.onlinename);
    net.lobby()->setName(systemLinkPlayerName());
    game_values.WriteConfig();
    mLobbyOrigin = LobbyOrigin::OnlineMenu;
    EnterOnlineLobby();
}

// The game chosen in the System Link menu (the games found on the local network)
void MenuState::JoinSystemLinkGame()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    mSystemLinkMenu->ClearMessage();

    std::string why;
    const std::string address = mSystemLinkMenu->SelectedAddress(why);
    if (address.empty()) {
        mSystemLinkMenu->ShowMessage(why);
        return;
    }

    std::string error;
    if (!net.join(address, error)) {
        mSystemLinkMenu->ShowMessage("Can't join: " + error);
        return;
    }
    mSystemLinkMenu->Close();

    //(the Online menu's address field shows it next time)
    game_values.onlineaddress = address;
    game_values.onlinename = trimmed(game_values.onlinename);
    net.lobby()->setName(systemLinkPlayerName());
    game_values.WriteConfig();
    mLobbyOrigin = LobbyOrigin::SystemLink;
    EnterOnlineLobby();
}

void MenuState::EnterOnlineLobby()
{
    mOnlineSetup = false;
    mOnlineMinigame = false;
    mOnlineSeriesType = MatchType::SingleGame;

    if (rollback::NetGame::instance().isHost())
        ApplyOnlineMatchSelection();

    mCurrentMenu = mOnlineLobbyMenu.get();
    mOnlineLobbyMenu->Open();
}

// The online state of the match menus goes (back to a local game's)
void MenuState::ResetOnlineMenus()
{
    mOnlineSetup = false;
    mOnlineView = false;
    mMatchSelectionMenu->SetOnlineMode(false, false);
    mGameSettingsMenu->SetStartButtonText("Start");
    mGameSettingsMenu->SetHeaderText("Single Game Menu");
    mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Off);
    mOnlineLobbyMenu->CloseDialog();
}

// The lobby is over (we left it, it was closed or we couldn't join): the menu it was entered from
// (the main menu, the Online menu, or the System Link one), with the reason
void MenuState::LeaveOnlineLobby(const std::string& message)
{
    ResetOnlineMenus();

    if (mLobbyOrigin == LobbyOrigin::MainMenu) {
        mLocalSetup.valid = false;
        mCurrentMenu = mMainMenu.get();
        return;
    }
    if (mLobbyOrigin == LobbyOrigin::SystemLink) {
        mSystemLinkMenu->Open();
        if (!message.empty())
            mSystemLinkMenu->ShowMessage(message);
        mCurrentMenu = mSystemLinkMenu.get();
        return;
    }
    mOnlineMenu->Open();
    mOnlineMenu->ShowMessage(message);
    mCurrentMenu = mOnlineMenu.get();
}

// The main menu's Start: the lobby of a local game (who can join: nobody; no network). Its players
// are the main menu's (players and CPUs), or as they were (backing out of a local game's menus).
void MenuState::OpenLocalLobby()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    std::string error;
    if (!net.host(0, error, rollback::LobbyAccess::Nobody)) {
        //(can't be: there's no network to fail; the menus as they were, then)
        printf("lobby: %s\n", error.c_str());
        mMatchSelectionMenu->WorldMapChanged();
        mCurrentMenu = mMatchSelectionMenu.get();
        mCurrentMenu->ResetMenu();
        return;
    }
    rollback::NetLobby* lobby = net.lobby();
    lobby->setName(systemLinkPlayerName());

    LocalSetup setup = mLocalSetup;
    if (!setup.valid) {
        setup = {};
        setup.localPlayers = 0;
        for (int k = 0; k < 4; k++) {
            setup.localPlayers += game_values.playercontrol[k] == 1;
            setup.cpus += game_values.playercontrol[k] == 2;
        }
        setup.localPlayers = std::clamp(setup.localPlayers, 1, rollback::MAX_LOCAL_PLAYERS);
        setup.cpus = std::clamp(setup.cpus, 0, 4 - setup.localPlayers);
    }
    lobby->setLocalPlayers(setup.localPlayers);
    lobby->setCpuCount(setup.cpus);
    if (setup.valid) {
        for (int k = 0; k < 4; k++) {
            if (lobby->slot(k).kind == rollback::SlotKind::Empty)
                continue;
            lobby->setSlotTeam(k, setup.team[k]);
            lobby->setSlotColor(k, setup.color[k]);
        }
    }

    mLocalFromLobby = false;
    mLobbyOrigin = LobbyOrigin::MainMenu;
    EnterOnlineLobby();
}

// Continue in a local game's lobby: its players, teams and colors go to the local match menus
// (the team select's work is done), and the lobby closes while they're up
void MenuState::StartLocalFromLobby()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    rollback::NetLobby* lobby = net.lobby();
    if (!lobby)
        return;
    std::string why;
    if (!lobby->canStart(why)) {
        if (!why.empty())
            why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
        mOnlineLobbyMenu->SetNotice(why);
        return;
    }

    //What to open again (backing out), and the players of the matches
    LocalSetup setup;
    setup.valid = true;
    setup.localPlayers = lobby->peer(lobby->localPeer()).localPlayers;
    for (int team = 0; team < 4; team++)
        game_values.teamcounts[team] = 0;
    for (int k = 0; k < 4; k++) {
        const rollback::LobbySlot& slot = lobby->slot(k);
        setup.cpus += slot.kind == rollback::SlotKind::Cpu;
        setup.team[k] = slot.team;
        setup.color[k] = slot.color;
        game_values.playercontrol[k] = slot.kind == rollback::SlotKind::Human ? 1 : slot.kind == rollback::SlotKind::Cpu ? 2 : 0;
        game_values.colorids[k] = static_cast<short>(game_values.teamcolors ? slot.team : slot.color);
        if (slot.kind == rollback::SlotKind::Human) {
            //(the lobby's local players are 0..n-1, in the slots 0..n-1: their skins are theirs)
            game_values.randomskin[k] = false;
            if (slot.localIndex != k)
                game_values.skinids[k] = game_values.skinids[slot.localIndex];
        }
    }
    short teams = 0;
    for (int team = 0; team < 4; team++) {
        short members = 0;
        for (int k = 0; k < 4; k++) {
            const rollback::LobbySlot& slot = lobby->slot(k);
            if (slot.kind != rollback::SlotKind::Empty && slot.team == team && members < 3)
                game_values.teamids[teams][members++] = static_cast<short>(k);
        }
        if (members > 0)
            game_values.teamcounts[teams++] = members;
    }
    mLocalSetup = setup;
    mLocalTeams = teams;

    //The lobby closes (a match with it open would be an online one); the local match menus
    LeaveLobbyQuietly();
    ResetOnlineMenus();
    mLocalFromLobby = true;
    mMatchSelectionMenu->WorldMapChanged();
    mCurrentMenu = mMatchSelectionMenu.get();
    mCurrentMenu->ResetMenu();
}

// The player leaves on purpose
void MenuState::LeaveOnlineLobbyNow()
{
    LeaveLobbyQuietly();
    LeaveOnlineLobby({});
}

// Closes (or leaves) the lobby; its events aren't news
void MenuState::LeaveLobbyQuietly()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    net.leave();
    if (rollback::NetLobby* lobby = net.lobby()) {
        rollback::LobbyEvent event;
        while (lobby->pollEvent(event)) {
        }
    }
}

void MenuState::HandleOnlineEvents()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    rollback::NetLobby* lobby = net.lobby();
    if (!lobby)
        return;

    using Type = rollback::LobbyEvent::Type;
    rollback::LobbyEvent event;
    while (lobby->pollEvent(event)) {
        switch (event.type) {
            case Type::Closed: {
                std::string text = event.text.empty() ? lobby->statusText() : event.text;
                if (text.empty())
                    text = "The lobby was closed";
                LeaveOnlineLobby(text);
                break;
            }

            case Type::MatchAborted:
                mOnlineLobbyMenu->SetNotice(event.text.empty() ? "The match was aborted" : event.text);
                break;

            case Type::ReturnedToLobby:
                if (mCurrentMenu != mOnlineLobbyMenu.get() && !mOnlineSetup) {
                    mOnlineView = false;
                    mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Off);
                    mCurrentMenu = mOnlineLobbyMenu.get();
                    if (net.isHost())
                        ApplyOnlineMatchSelection();
                    mOnlineLobbyMenu->Open();
                }
                break;

            default:
                break;
        }
    }
}

// "Settings": the host picks the match type in the match type menu, then (single game) the mode, goal
// and map in the game menu; both lead back to the lobby. A tournament's games are chosen as it goes.
void MenuState::OpenOnlineMatchSettings()
{
    //A local game (who can join: nobody): its matches are played offline
    if (rollback::NetGame::instance().access() == rollback::LobbyAccess::Nobody && rollback::NetGame::instance().isHost()) {
        StartLocalFromLobby();
        return;
    }
    mOnlineSetup = true;
    mMatchSelectionMenu->SetOnlineMode(true, mOnlineMinigame);
    if (mOnlineSeriesType != MatchType::SingleGame)
        mMatchSelectionMenu->SetSelectedMatchType(mOnlineSeriesType);
    mCurrentMenu = mMatchSelectionMenu.get();
    mCurrentMenu->ResetMenu();
}

void MenuState::OpenOnlineGameSettings()
{
    game_values.matchtype = MatchType::SingleGame;
    game_values.tournamentcontrolteam = -1;
    SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);

    maplist->findexact(szCurrentMapName, false);
    mGameSettingsMenu->miMapField->LoadCurrentMap();

    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
    if (mGameSettingsMenu->GetCurrentGameModeID() == game_mode_owned)
        mGameSettingsMenu->HideGMSettingsBtn();

    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++)
        mGameSettingsMenu->miGoalField[iGameMode]->hideItem(-1, false);

    mGameSettingsMenu->SetHeaderText("Online Match Menu");
    mGameSettingsMenu->SetStartButtonText("Start");
    mOnlineAppliedKey.clear();

    mCurrentMenu = mGameSettingsMenu.get();
    mCurrentMenu->ResetMenu();
}

void MenuState::ReturnToOnlineLobby()
{
    ApplyOnlineMatchSelection();

    mOnlineSetup = false;
    mMatchSelectionMenu->SetOnlineMode(false, false);
    mGameSettingsMenu->SetStartButtonText("Start");
    mGameSettingsMenu->SetHeaderText("Single Game Menu");

    mCurrentMenu = mOnlineLobbyMenu.get();
    mOnlineLobbyMenu->Open(false);
}

// "Start" of the match settings screens: the match starts if the lobby is ready for it; if not, back to
// the lobby, where the status line says what is missing
void MenuState::StartOnlineFromSettings()
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    std::string why;
    if (!lobby || !lobby->canStart(why)) {
        ReturnToOnlineLobby();
        if (!why.empty()) {
            why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
            mOnlineLobbyMenu->SetNotice(why);
        }
        return;
    }
    mOnlineSetup = false;
    StartOnlineMatch();
}

// A player who is not the host opens the settings the host has chosen: the game's own settings screen,
// with its fields disabled and marked as the host's (or live, when the host lets everyone change them)
void MenuState::OpenOnlineSettingsView()
{
    mOnlineView = true;
    mOnlineShownMode = -1;
    mOnlineShownGoal = -1;
    mOnlineShownMap.clear();
    mOnlineMapHere = false;
    mOnlineViewPending = 0;

    game_values.matchtype = MatchType::SingleGame;
    game_values.tournamentcontrolteam = -1;
    SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++)
        mGameSettingsMenu->miGoalField[iGameMode]->hideItem(-1, false);

    mGameSettingsMenu->SetHeaderText("Online Match Menu");
    mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Locked);

    mCurrentMenu = mGameSettingsMenu.get();
    mCurrentMenu->ResetMenu();
    SyncOnlineSettingsView();
}

void MenuState::CloseOnlineSettingsView()
{
    mOnlineView = false;
    mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Off);
    mGameSettingsMenu->SetHeaderText("Single Game Menu");
    mCurrentMenu = mOnlineLobbyMenu.get();
    mOnlineLobbyMenu->Open(false);
}

// Every frame: the host passes on what its menus have chosen, and applies what the others asked for
// (when it lets them); the others see the host's choice, and send theirs
void MenuState::SyncOnlineSettings()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    rollback::NetLobby* lobby = net.lobby();
    if (!lobby || !net.active())
        return;

    if (net.isHost()) {
        ApplyOnlineSettingRequests();

        // The lobby shows the mode, goal and map the host picks as it picks them
        if (mOnlineSetup && mCurrentMenu == mGameSettingsMenu.get() && mOnlineSeriesType == MatchType::SingleGame && !mOnlineMinigame) {
            const short mode = mGameSettingsMenu->GetCurrentGameModeID();
            const std::string key = std::to_string(mode) + "/" + std::to_string(mGameSettingsMenu->miGoalField[mode]->currentValue()) + "/" + mGameSettingsMenu->GetCurrentMapName();
            if (key != mOnlineAppliedKey) {
                mOnlineAppliedKey = key;
                ApplyOnlineMatchSelection();
            }
        }
        return;
    }

    if (mOnlineView && mCurrentMenu == mGameSettingsMenu.get())
        SyncOnlineSettingsView();
}

// The host: the changes the others asked for, which the lobby has let through (see NetLobby::requestSetting)
void MenuState::ApplyOnlineSettingRequests()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    rollback::NetGame::SettingRequest request;
    bool changed = false;
    while (net.popSettingRequest(request)) {
        //(a tournament, tour, world or minigame has no mode, goal and map to change)
        if (mOnlineSeriesType != MatchType::SingleGame || mOnlineMinigame)
            continue;
        switch (request.what) {
            case rollback::NetLobby::SETTING_MODE:
                if (request.value >= 0 && request.value < GAMEMODE_LAST) {
                    mGameSettingsMenu->GameModeChanged(static_cast<short>(request.value));
                    game_values.gamemode = gamemodes[request.value];
                    changed = true;
                }
                break;
            case rollback::NetLobby::SETTING_GOAL: {
                const short mode = mGameSettingsMenu->GetCurrentGameModeID();
                if (mGameSettingsMenu->miGoalField[mode]->setCurrentValue(static_cast<short>(request.value)))
                    changed = true;
                break;
            }
            case rollback::NetLobby::SETTING_MAP:
                if (maplist->findexact(request.text.c_str(), false)) {
                    mGameSettingsMenu->miMapField->LoadCurrentMap();
                    szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
                    changed = true;
                }
                break;
            default:
                break;
        }
    }
    if (changed)
        ApplyOnlineMatchSelection();
}

// The settings screen of a player who is not the host
void MenuState::SyncOnlineSettingsView()
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return;
    using View = UI_GameSettingsMenu::OnlineView;
    UI_GameSettingsMenu& menu = *mGameSettingsMenu;

    const int modeId = lobby->modeId();
    const std::string& mapName = lobby->mapName();
    const bool single = modeId >= 0 && modeId < GAMEMODE_LAST && mapName != "(minigame)";
    const bool editable = single && lobby->settingsEveryone() && lobby->phase() == rollback::NetLobby::Phase::Lobby;
    const View wanted = editable ? View::Editable : View::Locked;
    if (menu.onlineView() != wanted)
        menu.SetOnlineView(wanted);

    if (!single) {
        // A minigame, tournament, tour or world: its screens come up when the host starts it
        menu.SetMapOverlay(true, lobby->modeName().empty() ? "Match type set by host" : lobby->modeName(), -1.0f, {});
        return;
    }

    // What the player has edited since the last frame goes to the host
    if (editable) {
        const short mode = menu.GetCurrentGameModeID();
        const short goal = menu.miGoalField[mode]->currentValue();
        const std::string map = menu.GetCurrentMapName();
        if (mOnlineShownMode >= 0 && mode != mOnlineShownMode) {
            lobby->requestSetting(rollback::NetLobby::SETTING_MODE, mode, {});
            mOnlineShownMode = mode;
            mOnlineShownGoal = menu.miGoalField[mode]->currentValue();
            mOnlineViewPending = 60;
        } else if (mOnlineShownGoal >= 0 && goal != mOnlineShownGoal) {
            lobby->requestSetting(rollback::NetLobby::SETTING_GOAL, goal, {});
            mOnlineShownGoal = goal;
            mOnlineViewPending = 60;
        } else if (mOnlineMapHere && map != mOnlineShownMap) {
            lobby->requestSetting(rollback::NetLobby::SETTING_MAP, 0, map);
            mOnlineShownMap = map;
            mOnlineViewPending = 60;
        }
    }

    if (mOnlineViewPending > 0) {
        --mOnlineViewPending;
    } else {
        // The host's changes
        if (mOnlineShownMode != modeId) {
            menu.GameModeChanged(static_cast<short>(modeId));
            game_values.gamemode = gamemodes[modeId];
            mOnlineShownMode = static_cast<short>(modeId);
            mOnlineShownGoal = -1;
        }
        if (mOnlineShownGoal != lobby->goal()) {
            menu.miGoalField[modeId]->setCurrentValue(static_cast<short>(lobby->goal()));
            mOnlineShownGoal = menu.miGoalField[modeId]->currentValue();
        }
        // (looking for the map selects it in the map list, so only when it changed, or isn't here yet)
        if (mOnlineShownMap != mapName || !mOnlineMapHere) {
            mOnlineMapHere = !mapName.empty() && maplist->findexact(mapName.c_str(), false);
            if (mOnlineMapHere) {
                menu.miMapField->LoadCurrentMap();
                mOnlineShownMap = menu.GetCurrentMapName();
            } else {
                mOnlineShownMap = mapName;
            }
        }
    }

    // The map is not here (yet): its name, and how far the download is
    const rollback::MapTransferInfo& transfer = lobby->mapTransfer();
    if (mOnlineMapHere) {
        menu.SetMapOverlay(false);
    } else if (transfer.active && transfer.bytesTotal > 0) {
        const float fraction = static_cast<float>(transfer.bytesDone) / static_cast<float>(transfer.bytesTotal);
        menu.SetMapOverlay(true, mapName, fraction, std::format("{} {}%", transfer.sending ? "Uploading" : "Downloading", static_cast<int>(fraction * 100.0f)));
    } else {
        menu.SetMapOverlay(true, mapName.empty() ? "No map selected yet" : mapName + "\nnot here yet", -1.0f, {});
    }
}

// Makes the match menus' choice what NetGame::startMatch() reads (see NetLobby.h): the match type, game
// mode and goal, the map and (minigames) the boss
void MenuState::ApplyOnlineMatchSelection()
{
    game_values.tournamentcontrolteam = -1;

    if (mOnlineSeriesType != MatchType::SingleGame) {
        //Tournament, tour or world: what the match type menu selected (wins, tour, world)
        game_values.matchtype = mOnlineSeriesType;
        return;
    }

    if (mOnlineMinigame) {
        game_values.matchtype = MatchType::MiniGame;
        const Minigame minigame = mMatchSelectionMenu->GetMinigame();
        switch (minigame) {
            case Minigame::PipeCoin:
                pipegamemode->goal = 50;
                game_values.gamemode = pipegamemode;
                break;
            case Minigame::HammerBoss:
            case Minigame::BombBoss:
            case Minigame::FireBoss:
                game_values.gamemodemenusettings.boss.difficulty = 2;
                game_values.gamemodemenusettings.boss.hitpoints = 5;

                bossgamemode->goal = 5;
                game_values.gamemode = bossgamemode;
                break;
            case Minigame::Boxes:
                boxesgamemode->goal = 10;
                game_values.gamemode = boxesgamemode;
                break;
        }
        switch (minigame) {
            case Minigame::HammerBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Hammer;
                break;
            case Minigame::BombBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Bomb;
                break;
            case Minigame::FireBoss:
                game_values.gamemodemenusettings.boss.bosstype = Boss::Fire;
                break;
            default:
                break;
        }
        return;
    }

    game_values.matchtype = MatchType::SingleGame;
    maplist->findexact(szCurrentMapName, false);
    mGameSettingsMenu->miMapField->LoadCurrentMap();

    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
    for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++)
        gamemodes[iMode]->goal = mGameSettingsMenu->miGoalField[iMode]->currentValue();
}

void MenuState::StartOnlineMatch()
{
    rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (!lobby)
        return;

    std::string why;
    if (!lobby->canStart(why)) {
        mOnlineLobbyMenu->SetNotice(why);
        return;
    }

    //Like any match: the fade to black, then NetGame::startMatch() (see the end of update()).
    //A tournament, tour or world starts with its first screen (MenuOnlineSeries.cpp).
    ApplyOnlineMatchSelection();
    if (mOnlineSeriesType != MatchType::SingleGame)
        StartOnlineSeries();
    else
        StartGame();
}

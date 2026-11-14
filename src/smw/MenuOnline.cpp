// The online (rollback netplay) part of MenuState: the Online menu, the lobby and the way from the
// lobby to the usual match menus and back. The backend is rollback/NetGame.h and NetLobby.h.
#include "GSMenu.h"

#include "rollback/MatchConfig.h"
#include "rollback/NetGame.h"

#include "GameMode.h"
#include "GameValues.h"
#include "MapList.h"
#include "ResourceManager.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "ui/MI_MapField.h"
#include "ui/MI_SelectField.h"

#include "menu/GameSettingsMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"

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


void MenuState::HostOnlineGame()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    mOnlineMenu->ClearMessage();

    const std::string portText = trimmed(game_values.onlineport);
    const int port = mOnlineMenu->port();
    if (port < 0 || port > 65535) {
        mOnlineMenu->ShowMessage("The port must be a number from 1 to 65535");
        return;
    }

    std::string error;
    if (!net.host(static_cast<uint16_t>(port), error)) {
        mOnlineMenu->ShowMessage("Can't host a game: " + error);
        return;
    }

    game_values.onlinename = trimmed(game_values.onlinename);
    if (portText.empty())
        game_values.onlineport = "7300";
    net.lobby()->setName(game_values.onlinename);
    game_values.WriteConfig();
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
    net.lobby()->setName(game_values.onlinename);
    game_values.WriteConfig();
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

// The lobby is over (we left it, it was closed or we couldn't join): the Online menu, with the reason
void MenuState::LeaveOnlineLobby(const std::string& message)
{
    mOnlineSetup = false;
    mOnlineView = false;
    mMatchSelectionMenu->SetOnlineMode(false, false);
    mGameSettingsMenu->SetStartButtonText("Start");
    mGameSettingsMenu->SetHeaderText("Single Game Menu");
    mGameSettingsMenu->SetOnlineView(UI_GameSettingsMenu::OnlineView::Off);
    mOnlineLobbyMenu->CloseDialog();

    mOnlineMenu->Open();
    mOnlineMenu->ShowMessage(message);
    mCurrentMenu = mOnlineMenu.get();
}

// The player leaves on purpose
void MenuState::LeaveOnlineLobbyNow()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    net.leave();

    //Leaving closes the lobby: its events aren't news
    if (rollback::NetLobby* lobby = net.lobby()) {
        rollback::LobbyEvent event;
        while (lobby->pollEvent(event)) {
        }
    }
    LeaveOnlineLobby({});
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

// The online tournament, tour and world (rollback/NetSeries.h): MenuState's part. The host plays
// through the usual menus; the other peers show the screen the host shows (SERIES_SCREEN) and only
// act where a player of theirs has a say: the bonus wheel of their team, their turns on the world map
// (a lockstep of the world map's input, relayed by the host). The matches are ordinary online matches.
#include "GSMenu.h"

#include "rollback/NetGame.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "GameMode.h"
#include "GameValues.h"
#include "GSGameplay.h"
#include "MapList.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "ui/MI_BonusWheel.h"
#include "ui/MI_MapField.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_TournamentScoreboard.h"
#include "ui/MI_TourStop.h"
#include "ui/MI_World.h"
#include "world.h"
#include "WorldTourStop.h"
#include "world/WorldMapIO.h"

#include "menu/BonusWheelMenu.h"
#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"
#include "menu/TeamSelectMenu.h"
#include "menu/TournamentScoreboardMenu.h"
#include "menu/TourStopMenu.h"
#include "menu/WorldMenu.h"

#include <filesystem>
#include <random>
#include <string>

using rollback::SeriesScreen;

namespace {

// Every peer sets up the bonus wheel after seeding the gameplay generator with this
uint32_t wheelSeed(uint32_t seriesSeed, uint16_t step)
{
    return seriesSeed ^ (0x85ebca6bu * (static_cast<uint32_t>(step) + 1u));
}

// The held menu keys of a player: bit k = keys[k] (cancel is left out: it would open menus on one peer only)
uint8_t menuKeyBits(const COutputControl& keys)
{
    uint8_t bits = 0;
    for (int k = 0; k < 8; k++) {
        if (keys.keys[k].fDown || keys.keys[k].fPressed)
            bits |= static_cast<uint8_t>(1u << k);
    }
    const uint8_t cancel = 1u << 5;    //menu_cancel
    return static_cast<uint8_t>(bits & ~cancel);
}

// A tour or world file by its file name (the host sends the name, every peer has its own copy)
int findListFile(const SimpleFileList& list, const std::string& name)
{
    for (size_t i = 0; i < list.count(); i++) {
        if (list.at(i).filename().string() == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

// ---- starting

// Host: the lobby (or --autotest) starts the tournament, tour or world that the match menus selected
void MenuState::StartOnlineSeries()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    rollback::SeriesBeginMsg begin;
    std::string error;

    if (game_values.matchtype == MatchType::Tour || game_values.matchtype == MatchType::World) {
        const bool tour = game_values.matchtype == MatchType::Tour;
        const SimpleFileList& list = tour ? static_cast<const SimpleFileList&>(*tourlist) : static_cast<const SimpleFileList&>(*worldlist);
        const short index = tour ? game_values.tourindex : game_values.worldindex;
        if (index < 0 || static_cast<size_t>(index) >= list.count()) {
            mOnlineLobbyMenu->SetNotice(tour ? "No tour is selected" : "No world is selected");
            return;
        }
        begin.file = list.at(index).filename().string();
        begin.fileHash = rollback::fileHash(list.at(index).string());
    }

    if (!net.beginSeries(begin, error)) {
        printf("netplay: can't start the series: %s\n", error.c_str());
        mOnlineLobbyMenu->SetNotice("Can't start: " + error);
        return;
    }
    if (!SetupOnlineSeries(begin, error)) {
        printf("netplay: can't start the series: %s\n", error.c_str());
        net.endSeries(error);
        OnlineSeriesEnded("Can't start: " + error);
    }
}

// Every peer: the series from the host's SERIES_BEGIN, set up the same way everywhere (as the
// offline match menus do it, from MENU_CODE_TO_GAME_SETUP_MENU on)
bool MenuState::SetupOnlineSeries(const rollback::SeriesBeginMsg& begin, std::string& error)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    const MatchType type = static_cast<MatchType>(begin.matchType);
    if (!rollback::isSeriesMatchType(type)) {
        error = "unknown match type";
        return false;
    }

    //The tour or world file: our own copy must be the host's
    std::string path;
    if (type == MatchType::Tour || type == MatchType::World) {
        const bool tour = type == MatchType::Tour;
        const SimpleFileList& list = tour ? static_cast<const SimpleFileList&>(*tourlist) : static_cast<const SimpleFileList&>(*worldlist);
        const int index = findListFile(list, begin.file);
        if (index < 0) {
            error = "the " + std::string(tour ? "tour " : "world ") + begin.file + " is missing here";
            return false;
        }
        path = list.at(static_cast<size_t>(index)).string();
        if (rollback::fileHash(path) != begin.fileHash) {
            error = "the " + std::string(tour ? "tour " : "world ") + begin.file + " is different here";
            return false;
        }
        if (tour)
            game_values.tourindex = static_cast<short>(index);
        else
            game_values.worldindex = static_cast<short>(index);
    }

    //The players, teams and settings
    game_values.matchtype = type;
    game_values.tournamentgames = begin.tournamentGames;
    for (int k = 0; k < 4; k++) {
        game_values.playercontrol[k] = begin.control[static_cast<size_t>(k)];
        game_values.colorids[k] = begin.color[static_cast<size_t>(k)];
        game_values.storedpowerups[k] = -1;
        game_values.gamepowerups[k] = -1;
    }
    g_sim.score_cnt = begin.teamCount;
    for (int t = 0; t < 4; t++) {
        game_values.teamcounts[t] = begin.teamCounts[t];
        for (int m = 0; m < 3; m++)
            game_values.teamids[t][m] = begin.teamIds[t][m];
    }
    if (!rollback::applySimulationSettings(begin.settings)) {
        error = "the settings don't match this game version";
        return false;
    }
    for (int k = 0; k < 4; k++) {
        if (game_values.playercontrol[k] > 0)
            rollback::loadMatchSkin(k, game_values.colorids[k]);
    }

    //The series state, the same everywhere
    RandomNumberGenerator::gameplay().seed(begin.seed);
    game_values.tournamentwinner = -1;
    game_values.tournamentcontrolteam = -1;
    game_values.tournamentnextcontrol = 1;
    game_values.singleplayermode = -1;
    game_values.worldskipscoreboard = false;
    game_values.worldpointsbonus = -1;
    for (int t = 0; t < 4; t++) {
        game_values.tournament_scores[t] = TournamentScores();
        game_values.worldpowerupcount[t] = 0;
        for (short& item : game_values.worldpowerups[t])
            item = 0;
    }
    if (type != MatchType::Tour)
        ResetTourStops();

    iDisplayError = DISPLAY_ERROR_NONE;
    mOnlineSetup = false;
    mTournamentScoreboardMenu->ClearEyeCandy();
    for (short iGameMode = 0; iGameMode < GAMEMODE_LAST; iGameMode++)
        mGameSettingsMenu->miGoalField[iGameMode]->hideItem(-1, type == MatchType::Tournament);
    mGameSettingsMenu->SetStartButtonText("Start");

    if (type == MatchType::Tour) {
        if (!ReadTourFile()) {
            error = "the tour " + begin.file + " can't be read";
            return false;
        }
        mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tourstops.size(), &rm->spr_tour_markers);
        mGameSettingsMenu->SetHeaderText("Tour Game Menu");
        mCurrentMenu = mTourStopMenu.get();
        mCurrentMenu->ResetMenu();
    } else if (type == MatchType::Tournament) {
        mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, game_values.tournamentgames, &rm->menu_mode_large);
        mGameSettingsMenu->SetHeaderText("Tournament Game Menu");
        if (net.isHost()) {
            //The host chooses every game, as the controlling team would
            maplist->findexact(szCurrentMapName, false);
            mGameSettingsMenu->miMapField->LoadCurrentMap();
            game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
            for (short iMode = 0; iMode < GAMEMODE_LAST; iMode++)
                gamemodes[iMode]->goal = mGameSettingsMenu->miGoalField[iMode]->currentValue();
            mCurrentMenu = mGameSettingsMenu.get();
            mCurrentMenu->ResetMenu();
            SetControllingTeamForSettingsMenu(-1, false);
        } else {
            mCurrentMenu = mTournamentScoreboardMenu.get();
            mCurrentMenu->ResetMenu();
        }
    } else {
        try {
            g_worldmap = loadWorldMap(path, TILESIZE);
        } catch (const std::runtime_error& err) {
            error = "the world " + begin.file + " can't be read";
            return false;
        }
        mTournamentScoreboardMenu->miTournamentScoreboard->CreateScoreboard(g_sim.score_cnt, 0, &rm->spr_tour_markers);
        g_worldmap.SetInitialPowerups();
        mWorldMenu->miWorld->Init();
        mWorldMenu->miWorld->SetControllingTeam(RANDOM_INT(g_sim.score_cnt));
        mGameSettingsMenu->SetHeaderText("World Game Menu");

        mCurrentMenu = mWorldMenu.get();
        mCurrentMenu->ResetMenu();
        if (game_values.music) {
            rm->backgroundmusic[2].stop();
            rm->backgroundmusic[5] = sfxMusic(worldmusiclist->currentMusic(g_worldmap.GetMusicCategory(), g_worldmap.GetWorldName()));
            rm->backgroundmusic[5].play(false, false);
        }
        mWorldMenu->miWorld->DisplayTeamControlAnnouncement();
    }
    if (type != MatchType::World && game_values.music) {
        rm->backgroundmusic[2].stop();
        rm->backgroundmusic[3].play(false, false);
    }
    fNeedMenuMusicReset = true;

    mSeriesScreen = SeriesScreen::None;
    mSeriesStep = 0;
    mSeriesWheelStopSent = false;
    mSeriesWheelPressSent = false;
    mSeriesSkipUpdate = false;
    net.seriesSetUp(begin);
    return true;
}

// ---- every frame

// The screen of the host's current menu (None: the series is over, the host left its menus)
SeriesScreen MenuState::OnlineSeriesHostScreen() const
{
    const UI_Menu* menu = mCurrentMenu;
    if (menu == mTournamentScoreboardMenu.get())
        return SeriesScreen::Scoreboard;
    if (menu == mBonusWheelMenu.get())
        return SeriesScreen::BonusWheel;
    if (menu == mTourStopMenu.get())
        return SeriesScreen::TourStop;
    if (menu == mWorldMenu.get())
        return SeriesScreen::World;
    if (menu == mMainMenu.get() || menu == mOnlineLobbyMenu.get() || menu == mOnlineMenu.get() || menu == mMatchSelectionMenu.get() ||
            menu == mTeamSelectMenu.get())
        return SeriesScreen::None;
    return SeriesScreen::Choosing;
}

bool MenuState::OnlineSeriesLocalInWheelTeam() const
{
    rollback::NetGame& net = rollback::NetGame::instance();
    return net.seriesSlots().peerInTeam(net.localPeer(), mBonusWheelMenu->miBonusWheel->GetWinningTeam());
}

void MenuState::HandleOnlineSeries()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    using Type = rollback::NetGame::SeriesEvent::Type;
    rollback::NetGame::SeriesEvent event;
    while (net.pollSeriesEvent(event)) {
        switch (event.type) {
            case Type::Begin: {
                std::string error;
                if (!SetupOnlineSeries(event.begin, error)) {
                    //We can't play it: leave, the others go on without us
                    printf("netplay: can't play the series: %s\n", error.c_str());
                    net.leave();
                    if (!cmd::g_net.enabled())
                        LeaveOnlineLobby("Can't play: " + error);
                }
                break;
            }
            case Type::Screen:
                OnlineSeriesShowScreen(event.screen);
                break;
            case Type::WheelPress: {
                MI_BonusWheel* wheel = mBonusWheelMenu->miBonusWheel;
                if (mCurrentMenu == mBonusWheelMenu.get() && event.step == mSeriesStep && !wheel->IsStopped() &&
                        net.seriesSlots().peerInTeam(event.peer, wheel->GetWinningTeam())) {
                    printf("netplay: series: peer %d stops the wheel\n", event.peer);
                    wheel->Modify(true);
                }
                break;
            }
            case Type::WheelStop:
                if (mCurrentMenu == mBonusWheelMenu.get() && event.stop.step == mSeriesStep)
                    mBonusWheelMenu->miBonusWheel->ApplyStop(event.stop.angle, event.stop.speed, event.stop.winddown, event.stop.powerup);
                break;
            case Type::End:
                OnlineSeriesEnded(event.text.empty() ? "" : "The series ended: " + event.text);
                break;
            default:
                break;
        }
    }

    if (!net.seriesActive() || !net.isHost() || game_values.appstate != AppState::Menu)
        return;

    //Host: the others show what we show
    const SeriesScreen screen = OnlineSeriesHostScreen();
    if (screen == SeriesScreen::None) {
        net.endSeries("");
        OnlineSeriesEnded("");
        return;
    }
    net.seriesSyncPeers();
    if (screen != mSeriesScreen) {
        mSeriesStep++;
        mSeriesScreen = screen;
        OnlineSeriesEnterScreen(screen);
        rollback::SeriesScreenMsg msg;
        msg.screen = screen;
        msg.step = mSeriesStep;
        msg.humanMask = net.seriesSlots().humanMask();
        net.sendSeriesScreen(msg);
        if (screen == SeriesScreen::World)
            net.worldBegin(mSeriesStep, msg.humanMask);
        printf("netplay: series: screen %d: %s\n", mSeriesStep, rollback::seriesScreenName(screen));
    }

    MI_BonusWheel* wheel = mBonusWheelMenu->miBonusWheel;
    if (screen == SeriesScreen::BonusWheel && wheel->IsStopped() && !mSeriesWheelStopSent) {
        rollback::WheelStopMsg stop;
        stop.step = mSeriesStep;
        short powerup = 0;
        wheel->GetStop(stop.angle, stop.speed, stop.winddown, powerup);
        stop.powerup = powerup;
        net.sendWheelStop(stop);
        mSeriesWheelStopSent = true;
        printf("netplay: series: the wheel stopped on powerup %d\n", powerup);
    }
}

// What every peer does when a screen starts (the host when its menu changed, the others when told)
void MenuState::OnlineSeriesEnterScreen(SeriesScreen screen)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    //Players that are gone are CPU players on the screens too
    const rollback::SeriesSlots& slots = net.seriesSlots();
    for (int s = 0; s < 4; s++) {
        if (game_values.playercontrol[s] == 1 && slots.control[static_cast<size_t>(s)] != 1)
            game_values.playercontrol[s] = 2;
    }

    if (screen == SeriesScreen::BonusWheel) {
        //The same wheel everywhere
        RandomNumberGenerator::gameplay().seed(wheelSeed(net.seriesSeed(), mSeriesStep));
        mBonusWheelMenu->miBonusWheel->Reset(game_values.tournamentwinner >= 0);
        mBonusWheelMenu->miBonusWheel->SetRemote(!net.isHost());
        mSeriesWheelStopSent = false;
        mSeriesWheelPressSent = false;
    } else if (screen == SeriesScreen::World) {
        for (uint8_t& keys : mWorldPrevKeys)
            keys = 0;
    }
}

// Not the host: show the host's screen
void MenuState::OnlineSeriesShowScreen(const rollback::SeriesScreenMsg& msg)
{
    //The host's wheel is done: so is ours
    if (mCurrentMenu == mBonusWheelMenu.get())
        mBonusWheelMenu->miBonusWheel->Finish();
    mSeriesStep = msg.step;
    mSeriesScreen = msg.screen;
    for (int s = 0; s < 4; s++) {
        if (game_values.playercontrol[s] == 1 && !(msg.humanMask & (1u << s)))
            game_values.playercontrol[s] = 2;
    }

    switch (msg.screen) {
        case SeriesScreen::Choosing:
        case SeriesScreen::Scoreboard:
            if (mCurrentMenu != mTournamentScoreboardMenu.get()) {
                mCurrentMenu = mTournamentScoreboardMenu.get();
                mCurrentMenu->ResetMenu();
            }
            break;
        case SeriesScreen::BonusWheel:
            mCurrentMenu = mBonusWheelMenu.get();
            mCurrentMenu->ResetMenu();
            break;
        case SeriesScreen::TourStop:
            if (game_values.tourstopcurrent < game_values.tourstops.size())
                mTourStopMenu->miTourStop->Refresh(game_values.tourstopcurrent);
            mCurrentMenu = mTourStopMenu.get();
            mCurrentMenu->ResetMenu();
            break;
        case SeriesScreen::World:
            if (mCurrentMenu != mWorldMenu.get()) {
                mCurrentMenu = mWorldMenu.get();
                mWorldMenu->miWorldStop->Refresh(game_values.tourstopcurrent);
                mCurrentMenu->ResetMenu();
                mWorldMenu->miWorld->DisplayTeamControlAnnouncement();
            }
            break;
        default:
            break;
    }
    OnlineSeriesEnterScreen(msg.screen);
}

// The menu input in a series: the world map's lockstep, the bonus wheel of our team, nothing else
// on the other peers (they follow the host). True if it replaced the menu's own input.
bool MenuState::OnlineSeriesInput(MenuCodeEnum& code)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.seriesActive())
        return false;

    if (mCurrentMenu == mWorldMenu.get()) {
        OnlineSeriesWorldTick(code);
        return true;
    }

    if (mCurrentMenu == mBonusWheelMenu.get()) {
        MI_BonusWheel* wheel = mBonusWheelMenu->miBonusWheel;
        if (!wheel->IsStopped()) {
            //Only the winners stop the wheel
            if (!OnlineSeriesLocalInWheelTeam())
                return true;
            if (net.isHost())
                return false;
            const bool pressed = game_values.playerInput.outputControls[0].menu_select.fPressed || game_values.playerInput.outputControls[1].menu_select.fPressed;
            if (pressed && !mSeriesWheelPressSent) {
                net.sendWheelPress(mSeriesStep);
                mSeriesWheelPressSent = true;
            }
            return true;
        }
    }
    return !net.isHost();
}

// One frame of the world map online: our keys go into the lockstep; the map runs when a tick is ready
void MenuState::OnlineSeriesWorldTick(MenuCodeEnum& code)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    mSeriesSkipUpdate = true;
    //(not while a stage is starting: the map waits for the match)
    if (!net.worldActive() || !net.lobby() || net.lobby()->phase() == rollback::NetLobby::Phase::Starting ||
            net.lobby()->phase() == rollback::NetLobby::Phase::InMatch)
        return;

    //Our players use the first input devices, in slot order
    std::array<uint8_t, rollback::SERIES_SLOTS> keys {};
    const uint8_t mine = net.seriesSlots().slotsOf(net.localPeer());
    int device = 0;
    for (int s = 0; s < rollback::SERIES_SLOTS; s++) {
        if (mine & (1u << s))
            keys[static_cast<size_t>(s)] = menuKeyBits(game_values.playerInput.outputControls[std::min(device++, 3)]);
    }
    rollback::WorldTickMsg tick;
    if (!net.worldStep(keys, tick))
        return;
    mSeriesSkipUpdate = false;

    //Players that are gone are CPU players from this tick on
    for (int s = 0; s < 4; s++) {
        if (game_values.playercontrol[s] == 1 && !(tick.humanMask & (1u << s)))
            game_values.playercontrol[s] = 2;
    }

    //The tick's keys replace the local input (and every input device counts as a controller, so
    //that no peer's local device settings decide who may use the menu)
    static CInputPlayerControl controller = [] {
        CInputPlayerControl c {};
        c.iDevice = DEVICE_KEYBOARD_ID + 1;
        return c;
    }();
    CPlayerInput& input = game_values.playerInput;
    COutputControl saved[4];
    CInputPlayerControl* savedDevices[4];
    for (int s = 0; s < 4; s++) {
        saved[s].copyFrom(input.outputControls[s]);
        savedDevices[s] = input.inputControls[s];
        input.inputControls[s] = &controller;
        const uint8_t now = tick.keys[static_cast<size_t>(s)];
        const uint8_t before = mWorldPrevKeys[s];
        for (int k = 0; k < NUM_KEYS; k++) {
            const bool down = k < 8 && (now & (1u << k));
            input.outputControls[s].keys[k].fDown = down;
            input.outputControls[s].keys[k].fPressed = down && !(before & (1u << k));
        }
        mWorldPrevKeys[s] = now;
    }

    code = mCurrentMenu->SendInput(&input);

    for (int s = 0; s < 4; s++) {
        input.outputControls[s].copyFrom(saved[s]);
        input.inputControls[s] = savedDevices[s];
    }
}

// ---- after a match, at the end

// MenuState::onEnterState after a match of the series: its results are applied (the same on every peer)
void MenuState::OnlineSeriesAfterMatch()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.seriesActive())
        return;
    //The host chooses the games (whoever would control the menus offline)
    game_values.tournamentcontrolteam = -1;
    //The world map may have loaded the menu skin of a player (it lacks the frames the scoreboard and
    //the bonus wheel draw; whether it was loaded depends on what this peer loaded before): the full ones
    for (int k = 0; k < 4; k++) {
        if (game_values.playercontrol[k] > 0)
            rollback::loadMatchSkin(k, game_values.colorids[k]);
    }
    mBonusWheelMenu->miBonusWheel->SetRemote(!net.isHost());
    mSeriesScreen = SeriesScreen::None;    //the host tells its screen again
    mSeriesSkipUpdate = false;
    net.seriesResultsApplied();
}

void MenuState::OnlineSeriesEnded(const std::string& reason)
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (mCurrentMenu == mBonusWheelMenu.get())
        mBonusWheelMenu->miBonusWheel->Finish();
    net.printSeriesFinal();
    mSeriesScreen = SeriesScreen::None;
    mSeriesSkipUpdate = false;
    mBonusWheelMenu->miBonusWheel->SetRemote(false);
    if (fNeedMenuMusicReset && game_values.music) {
        rm->backgroundmusic[3].stop();
        rm->backgroundmusic[5].stop();
        rm->backgroundmusic[2].play(false, false);
    }
    fNeedMenuMusicReset = false;
    if (!net.isHost())
        game_values.matchtype = MatchType::SingleGame;
    if (game_values.appstate == AppState::StartGame) {
        //(a stage was about to start)
        game_values.appstate = AppState::Menu;
        game_values.screenfadespeed = -8;
    }

    if (!cmd::g_net.enabled() && net.active()) {
        mCurrentMenu = mOnlineLobbyMenu.get();
        if (net.isHost())
            ApplyOnlineMatchSelection();
        mOnlineLobbyMenu->Open();
        if (!reason.empty())
            mOnlineLobbyMenu->SetNotice(reason);
    } else if (cmd::g_net.enabled()) {
        mCurrentMenu = mMainMenu.get();
        mCurrentMenu->ResetMenu();
    }
}

// The other peers: what they wait for
void MenuState::DrawOnlineSeriesNotice()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    if (!net.seriesActive() || net.isHost())
        return;
    const char* text = nullptr;
    switch (mSeriesScreen) {
        case SeriesScreen::None:
        case SeriesScreen::Choosing: text = "The host chooses the next game..."; break;
        case SeriesScreen::Scoreboard:
        case SeriesScreen::TourStop: text = "Waiting for the host..."; break;
        case SeriesScreen::BonusWheel:
            if (!mBonusWheelMenu->miBonusWheel->IsStopped() && !OnlineSeriesLocalInWheelTeam())
                text = "The winners stop the wheel";
            break;
        default: break;
    }
    if (text)
        rm->menu_font_small.drawCentered(App::screenWidth / 2, App::screenHeight - 18, text);
}

// ---- --autotest: the screens of a series, deterministically (offline tournaments too)
MenuCodeEnum MenuState::AutoTestSeriesUpdate()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    static std::mt19937 rng(777 + static_cast<unsigned>(cmd::g_autotest.seed));
    static const UI_Menu* lastMenu = nullptr;
    static int t = 0;
    if (mCurrentMenu != lastMenu) {
        lastMenu = mCurrentMenu;
        t = 0;
    }
    ++t;

    const bool host = !net.seriesActive() || net.isHost();
    COutputControl& keys = game_values.playerInput.outputControls[0];

    if (mCurrentMenu == mWorldMenu.get()) {
        //Random keys, as offline (the world map ignores those of players whose turn it isn't)
        //Our team's item popup: closed again after a while (it blocks the map)
        static int popupFrames = 0;
        short ourTeam = 0;
        for (int s = 0; s < 4; s++) {
            if (!net.seriesActive() || (net.seriesSlots().slotsOf(net.localPeer()) & (1u << s))) {
                ourTeam = LookupTeamID(static_cast<short>(s));
                break;
            }
        }
        //(once: the press reaches the map a few frames later, a second one would open it again)
        static int closeCooldown = 0;
        popupFrames = mWorldMenu->miWorld->IsItemPopupOpen(ourTeam) ? popupFrames + 1 : 0;
        if (closeCooldown > 0)
            closeCooldown--;
        if (popupFrames > 40 && closeCooldown == 0) {
            keys.menu_random.fPressed = true;
            closeCooldown = 90;
            return MENU_CODE_NONE;
        }
        if (closeCooldown > 0)
            return MENU_CODE_NONE;
        if (t % 4 == 0) {
            //(and sometimes the random key: the team's items, to use them)
            switch (rng() % 13) {
                case 0: case 1: keys.menu_up.fPressed = true; break;
                case 2: case 3: keys.menu_down.fPressed = true; break;
                case 4: case 5: keys.menu_left.fPressed = true; break;
                case 6: case 7: keys.menu_right.fPressed = true; break;
                case 8: keys.menu_random.fPressed = true; break;
                default: keys.menu_select.fPressed = true; break;
            }
        }
        if (host && t > 12000 && net.seriesActive()) {
            printf("autotest: stuck on the world map (%u ticks run, %zu pending; %s; stage dialog %d; control %d%d%d%d), ending the series\n",
                net.worldTicksRun(), net.worldPending(), mWorldMenu->miWorld->DebugState().c_str(), mWorldMenu->miWorldStop->IsVisible() ? 1 : 0,
                game_values.playercontrol[0], game_values.playercontrol[1], game_values.playercontrol[2], game_values.playercontrol[3]);
            net.endSeries("autotest: stuck");
            OnlineSeriesEnded("");
        }
        return MENU_CODE_NONE;
    }

    if (mCurrentMenu == mBonusWheelMenu.get()) {
        MI_BonusWheel* wheel = mBonusWheelMenu->miBonusWheel;
        if (!wheel->IsStopped()) {
            if (t >= 90 && t % 30 == 0 && (!net.seriesActive() || OnlineSeriesLocalInWheelTeam()))
                keys.menu_select.fPressed = true;
        } else if (host && wheel->GetPowerupSelectionDone() && t % 40 == 0) {
            keys.menu_select.fPressed = true;
        }
        return MENU_CODE_NONE;
    }

    if (!host)
        return MENU_CODE_NONE;

    if (mCurrentMenu == mTournamentScoreboardMenu.get() || mCurrentMenu == mTourStopMenu.get()) {
        if (t == 40)
            keys.menu_select.fPressed = true;
        return MENU_CODE_NONE;
    }

    if (mCurrentMenu == mGameSettingsMenu.get() && t == 30) {
        //The next game of a tournament: mode by the match count, a small goal, the next maps
        const short mode = static_cast<short>((cmd::g_autotest.mode >= 0 ? cmd::g_autotest.mode : cmd::g_autotest.start + g_autoTestMatchesDone) % GAMEMODE_LAST);
        mGameSettingsMenu->GameModeChanged(mode);
        game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
        currentgamemode = mode;
        SModeOption* options = game_values.gamemode->GetOptions();
        game_values.gamemode->goal = options[1].iValue;
        mGameSettingsMenu->miGoalField[mode]->setCurrentValue(game_values.gamemode->goal);
        if (!cmd::g_autotest.map.empty()) {
            maplist->findexact(cmd::g_autotest.map.c_str(), false);
        } else {
            for (int k = 0; k < 5; k++)
                maplist->next(false);
        }
        mGameSettingsMenu->miMapField->LoadCurrentMap();
        szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
        printf("autotest: match %d, mode %d, map %s, %d players\n", g_autoTestMatchesDone + 1, mode, maplist->currentShortmapname().c_str(), g_sim.score_cnt);
        return MENU_CODE_START_GAME;
    }
    return MENU_CODE_NONE;
}

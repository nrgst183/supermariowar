#include "GSMenu.h"

#include "rollback/NetGame.h"

#include "CmdArgs.h"
#include "Determinism.h"
#include "FileList.h"
#include "GameValues.h"
#include "GSGameplay.h"
#include "menu/EditorsMenu.h"
#ifdef SMW_HAS_EDITORS
#include "editors/CoEdit.h"
#endif
#ifdef SMW_HAS_EDITORS
#include "editors/EditorInput.h"
#include "editors/EditorLauncher.h"
#endif
#include "MapList.h"
#include "SimulationState.h"
#include "gamemodes/MiniPipe.h"
#include "ui/InputPrompts.h"
#include "ui/MenuPad.h"
#include "ui/MI_MapField.h"
#include "ui/MI_SelectField.h"

#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MatchHistoryMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/ModeOptionsMenu.h"
#include "menu/TeamSelectMenu.h"
#include "menu/PlayerControlsMenu.h"
#include "menu/PlayerControlsSelectMenu.h"

#include "menu/OnlineLobbyMenu.h"
#include "menu/OnlineMenu.h"

#include "LoadLog.h"
#include "Platform.h"
#include "map.h"

#include <SDL3_image/SDL_image.h>

#include <format>
#include <string>
#include <cstdlib> // atoi()
#include <random>
#include <cassert>
#include <sstream>
#include <ctime>
#include <deque>
#include <set>

// main.cpp: --at-perf
namespace perfstats {
void reset();
void report(const std::string& map, int mode);
}

namespace {
// --at-maps: a map at random from the list (the busiest), seeded by the time: each run differs
void pickListedMap(const std::string& mapsFile)
{
    static std::vector<std::string> names;
    static std::mt19937 pick(static_cast<uint32_t>(std::time(nullptr)) ^ static_cast<uint32_t>(SDL_GetTicksNS()));
    if (names.empty()) {
        if (FILE* file = fopen(mapsFile.c_str(), "rb")) {
            std::string text;
            char buf[512];
            size_t n = 0;
            while ((n = fread(buf, 1, sizeof(buf), file)) > 0)
                text.append(buf, n);
            fclose(file);
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                if (!line.empty())
                    names.push_back(line);
            }
        }
        printf("autotest: %d maps from %s\n", static_cast<int>(names.size()), mapsFile.c_str());
    }
    if (!names.empty())
        maplist->findexact(names[pick() % names.size()].c_str(), false);
}
} // namespace

// Opt-in --autotest harness (see cmd::AutoTestArgs): drives the menus without human input.
// Returns a menu code to use instead of the current menu's own input, or MENU_CODE_NONE.
MenuCodeEnum MenuState::AutoTestMenuUpdate()
{
    using Kind = cmd::AutoTestArgs::Kind;
    const cmd::AutoTestArgs& at = cmd::g_autotest;

    static std::mt19937 rng(12345 + cmd::g_autotest.start);    // not the game RNG, so gameplay RNG order is untouched
    static int menuFrames = 0;
    static int phase = 0;              // 0 idle in main menu, 1 waiting for settings menu, 2 match running
    static int timer = 0;
    static int scenario = 0;
    static int lastMatchesDone = 0;
    static int startedWith = -1;    // the matches done when this one was started (it's done when that grows)

    if (game_values.appstate != AppState::Menu)
        return MENU_CODE_NONE;

    ++menuFrames;

    if (at.kind == Kind::Shots)
        return ShotsMenuUpdate();

    //--at-rank-maps: the busiest maps (the memory test's list), then quit (here: everything is loaded)
    if (!at.rankMapsFile.empty()) {
        std::vector<std::pair<int, std::string>> ranked;
        for (size_t k = 0; k < maplist->count(); k++) {
            maplist->next(false);
            try {
                g_map->loadMap(maplist->currentFilename(), read_type_full);
            } catch (...) {
                continue;
            }
            ranked.emplace_back(g_map->busyScore(), maplist->currentShortmapname());
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (FILE* out = fopen(at.rankMapsFile.c_str(), "wb")) {
            for (size_t k = 0; k < ranked.size() && k < 80; k++)
                fprintf(out, "%s\n", ranked[k].second.c_str());
            fclose(out);
        }
        for (size_t k = 0; k < ranked.size() && k < 15; k++)
            printf("rank-maps: %4d %s\n", ranked[k].first, ranked[k].second.c_str());
        return MENU_CODE_EXIT_APPLICATION;
    }


    //--at-match everything: one kind after another, each until it's done (the memory test)
    if (at.everything) {
        struct Stage {
            Kind kind;
            int matches;
            int frames;
            const char* name;
            int speed = 10;    // ticks per drawn frame, the menus' and the matches' (1: normal speed, as played)
        };
        static const Stage stages[] = {
            {Kind::History, 4, 900, "the history and the theater"},
            {Kind::Editors, 4, 900, "the editors"},
            {Kind::Single, 4, 1800, "matches at normal speed (the frame rate as played)", 1},
            {Kind::Single, GAMEMODE_LAST, 700, "every mode, the busiest maps"},
            {Kind::Screens, 22, 0, "every screen"},
            {Kind::Minigame, 5, 900, "minigames"},
            {Kind::Tournament, 2, 700, "a tournament"},
            {Kind::Tour, 4, 900, "a tour"},
            {Kind::World, 4, 900, "a world"},
            {Kind::Single, 6, 700, "more matches, the busiest maps"},
            {Kind::Menus, 0, 2400, "the menus"},
        };
        constexpr int STAGES = static_cast<int>(sizeof(stages) / sizeof(stages[0]));
        static int stage = -1;
        static int stageStart = 0;
        //(a stage's matches done: its end, wherever the menus are: a tournament, a tour or a world
        //goes on to its next game by itself, and never back to the main menu)
        const bool stageDone = stage < 0 ||
            (at.kind == Kind::Menus ? menuFrames - stageStart >= at.frames : g_autoTestMatchesDone >= at.matches);
        if (stageDone) {
            if (stage >= 0) {
                const platform::StageStats st = platform::takeStageStats();
                g_autoTestStageReport += std::format("{}. {}: {} done", stage + 1, stages[stage].name,
                    stages[stage].kind == Kind::Menus ? at.frames : g_autoTestMatchesDone);
                if (st.known) {
                    g_autoTestStageReport += std::format(", lowest {:.1f} MB free, {:.0f} fps (worst {:.0f} ms, {} of {} frames slow)",
                        st.lowestFreeMb, st.averageMs > 0 ? 1000.0 / st.averageMs : 0.0, st.worstMs, st.slowFrames, st.frames);
                }
                g_autoTestStageReport += "\n";
                if (g_autoTestSlowestLoadMs > 0) {
                    g_autoTestStageReport += std::format("   slowest load {:.1f} s ({})", g_autoTestSlowestLoadMs / 1000.0, g_autoTestSlowestLoadMap);
                    if (st.known)
                        g_autoTestStageReport += std::format("; images: {} decoded, {} from the cache", st.decoded, st.cached);
                    g_autoTestStageReport += "\n";
                }
                g_autoTestSlowestLoadMs = 0;
            }
            else {
                platform::takeStageStats();    //(the stages' count from the first)
            }
            if (++stage >= STAGES)
                return MENU_CODE_EXIT_APPLICATION;
            const Stage& next = stages[stage];
            cmd::g_autotest.kind = next.kind;
            cmd::g_autotest.matches = next.matches;
            cmd::g_autotest.frames = next.frames;
            cmd::g_autotest.speed = next.speed;
            g_autoTestMatchesDone = 0;
            phase = 0;
            timer = 0;
            stageStart = menuFrames;
            if (!at.mapsFile.empty())
                pickListedMap(at.mapsFile);
            mCurrentMenu = mMainMenu.get();
            mCurrentMenu->ResetMenu();
            const std::string label = std::format("Memory test {}/{}: {}", stage + 1, STAGES, next.name);
            printf("autotest: %s\n", label.c_str());
            loadlog::mark(label.c_str());
            platform::setMemoryHud(true, label);
        }
    }

    auto pressRandomKey = [&](bool allowCancel) {
        COutputControl& keys = game_values.playerInput.outputControls[0];
        switch (rng() % (allowCancel ? 7 : 6)) {
            case 0: keys.menu_up.fPressed = true; break;
            case 1: keys.menu_down.fPressed = true; break;
            case 2: keys.menu_left.fPressed = true; break;
            case 3: keys.menu_right.fPressed = true; break;
            case 4:
            case 5: keys.menu_select.fPressed = true; break;
            default: keys.menu_cancel.fPressed = true; break;
        }
    };

    //The everything test's own stages: every screen, the history and theater, the editors
    if (at.kind == Kind::Screens) {
        static const MenuCodeEnum screens[] = {
            MENU_CODE_TO_OPTIONS_MENU, MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU, MENU_CODE_TO_TEAM_OPTIONS_MENU,
            MENU_CODE_TO_POWERUP_SELECTION_MENU, MENU_CODE_TO_POWERUP_SETTINGS_MENU, MENU_CODE_TO_PROJECTILES_OPTIONS_MENU,
            MENU_CODE_TO_PROJECTILES_LIMITS_MENU, MENU_CODE_TO_GRAPHICS_OPTIONS_MENU, MENU_CODE_TO_EYECANDY_OPTIONS_MENU,
            MENU_CODE_TO_SOUND_OPTIONS_MENU, MENU_CODE_TO_ONLINE_OPTIONS_MENU, MENU_CODE_TO_SCREEN_SETTINGS,
            MENU_CODE_TO_GAME_SETUP_MENU, MENU_CODE_TO_TEAM_SELECT_MENU, MENU_CODE_TO_MODE_SETTINGS_MENU,
            MENU_CODE_TO_MAP_FILTERS, MENU_CODE_TO_MAP_BROWSER_THUMBNAILS, MENU_CODE_TO_MATCH_SELECTION_MENU,
            MENU_CODE_TO_ONLINE_MENU, MENU_CODE_TO_SYSTEM_LINK_MENU, MENU_CODE_TO_HISTORY_MENU, MENU_CODE_TO_EDITORS_MENU,
        };
        constexpr int SCREENS = static_cast<int>(sizeof(screens) / sizeof(screens[0]));
        static int frames = 0;
        ++frames;
        if (frames == 1) {
            printf("autotest: screen %d\n", g_autoTestMatchesDone);
            return screens[g_autoTestMatchesDone % SCREENS];
        }
        //(moved around in: up and down, not into anything)
        if (frames < 70 && frames % 9 == 0) {
            COutputControl& keys = game_values.playerInput.outputControls[0];
            (rng() % 2 ? keys.menu_down : keys.menu_up).fPressed = true;
        }
        if (frames >= 80) {
            frames = 0;
            ++g_autoTestMatchesDone;
            mCurrentMenu = mMainMenu.get();
            mCurrentMenu->ResetMenu();
        }
        return MENU_CODE_NONE;
    }
    if (at.kind == Kind::History) {
        //(the theater counts a replay done when it leaves it; one that won't play counts too)
        static int frames = 0;
        static int asked = -1;
        if (asked >= 0 && asked == g_autoTestMatchesDone && game_values.appstate == AppState::Menu && frames > 40) {
            ++g_autoTestMatchesDone;
            printf("autotest: a replay didn't play\n");
        }
        if (asked >= 0 && asked != g_autoTestMatchesDone) {
            asked = -1;
            frames = 0;
        }
        ++frames;
        if (frames == 1)
            return MENU_CODE_TO_HISTORY_MENU;
        if (frames < 40 && frames % 6 == 0 && rng() % 2)
            game_values.playerInput.outputControls[0].menu_down.fPressed = true;
        if (frames == 40) {
            asked = g_autoTestMatchesDone;
            return MENU_CODE_HISTORY_PLAY_REPLAY;
        }
        return MENU_CODE_NONE;
    }
#ifdef SMW_HAS_EDITORS
    if (at.kind == Kind::Editors) {
        //An editor session of random editing (its test driver), closed after the stage's frames; the
        //editor runs in the call that opens it, so the frame after is its end
        static bool driverSet = false;
        if (!driverSet) {
            driverSet = true;
            editors::input::setTestDriver([](editors::Editor, int frame, int pointer) {
                editors::input::TestInput t;
                static std::mt19937 keys(4242);
                if (pointer > 0)
                    return t;
                if (frame > cmd::g_autotest.frames) {
                    editors::requestClose();
                    return t;
                }
                if (frame % 3 != 0)
                    return t;
                switch (keys() % 14) {
                    case 0: t.a = true; break;
                    case 1: t.x = true; break;
                    case 2: t.y = true; break;
                    case 3: t.lb = true; break;
                    case 4: t.rb = true; break;
                    case 5: t.up = true; break;
                    case 6: t.down = true; break;
                    case 7: t.left = true; break;
                    case 8: t.right = true; break;
                    case 9: t.lt = true; break;
                    case 10: t.rt = true; break;
                    default:
                        t.lx = static_cast<float>(static_cast<int>(keys() % 201) - 100) / 100.0f;
                        t.ly = static_cast<float>(static_cast<int>(keys() % 201) - 100) / 100.0f;
                        break;
                }
                return t;
            }, "", 1);
        }
        static int frames = 0;
        ++frames;
        if (frames == 1)
            return g_autoTestMatchesDone % 2 ? MENU_CODE_RUN_WORLD_EDITOR : MENU_CODE_RUN_LEVEL_EDITOR;
        frames = 0;
        ++g_autoTestMatchesDone;
        mCurrentMenu = mMainMenu.get();
        mCurrentMenu->ResetMenu();
        return MENU_CODE_NONE;
    }
#endif

    if (at.kind == Kind::Menus) {
        if (!at.everything && menuFrames >= at.frames)
            return MENU_CODE_EXIT_APPLICATION;

        //(not the controls' screens: a binding waits for a real button, and the run with it)
        if (mCurrentMenu == mPlayerControlsMenu.get() || mCurrentMenu == mPlayerControlsSelectMenu.get()) {
            mCurrentMenu = mMainMenu.get();
            mCurrentMenu->ResetMenu();
            return MENU_CODE_NONE;
        }

        if (rng() % 3 == 0)
            pressRandomKey(mCurrentMenu != mMainMenu.get());
        return MENU_CODE_NONE;
    }

    //Online (--net-*): the host picks the matches; the joiners are ready for them
    rollback::NetGame& net = rollback::NetGame::instance();
    static bool sawSeries = false;
    if (net.active() || net.finished()) {
        //(a series that ended runs its last world map ticks first)
        if (net.finished() && !net.seriesActive())
            return net.canQuit() ? MENU_CODE_EXIT_APPLICATION : MENU_CODE_NONE;
        if (net.seriesActive()) {
            //A tournament, tour or world: its screens (the host ends it after --at-matches matches)
            sawSeries = true;
            if (net.isHost() && g_autoTestMatchesDone >= at.matches) {
                net.endSeries("autotest: " + std::to_string(at.matches) + " matches played");
                OnlineSeriesEnded("");
                return MENU_CODE_NONE;
            }
            return AutoTestSeriesUpdate();
        }
        if (!net.isHost()) {
            if (net.lobby()->phase() == rollback::NetLobby::Phase::Lobby && !net.lobby()->peer(net.localPeer()).ready)
                net.lobby()->setReady(true);
            return MENU_CODE_NONE;
        }
        if (g_autoTestMatchesDone >= at.matches || !net.lastMatchError().empty() || sawSeries) {
            //Done, or a peer is gone (disconnect tests): close the lobby
            if (net.matchDrained())
                net.endSession(net.lastMatchError().empty() ? "autotest done" : "autotest: a match was aborted");
            return MENU_CODE_NONE;
        }
        if (phase == 0 && !net.readyForMatch())
            return MENU_CODE_NONE;
    }

    if (g_autoTestMatchesDone >= at.matches && !at.everything) {
        //The determinism test: its result; the Xbox shows it until a button is pressed (no console)
        if (determinism::active()) {
            determinism::finish();
#ifdef SMW_XBOX
            return MENU_CODE_NONE;
#endif
        }
        return MENU_CODE_EXIT_APPLICATION;
    }

    if (lastMatchesDone != g_autoTestMatchesDone) {
        lastMatchesDone = g_autoTestMatchesDone;
        timer = 0;
    }

    const bool oneShot = at.kind == Kind::Single || at.kind == Kind::Minigame;

    switch (phase) {
        case 0: {
            if (mCurrentMenu != mMainMenu.get()) {
                mCurrentMenu = mMainMenu.get();
                mCurrentMenu->ResetMenu();
            }

            if (++timer < 20)
                return MENU_CODE_NONE;
            timer = 0;

            const int idx = at.start + scenario++;
            const int players = at.players > 0 ? at.players : 2 + idx % 3;
            //CPU players (2), or "humans" (1) driven by GameplayState's random input
            for (int k = 0; k < 4; k++)
                game_values.playercontrol[k] = k < players ? (k < at.humans ? 1 : 2) : 0;
            if (net.active()) {
                //Online the lobby has the players: the peers', then CPU players
                int humans = 0;
                for (int k = 0; k < 4; k++)
                    humans += net.lobby()->slot(k).kind == rollback::SlotKind::Human;
                net.lobby()->setCpuCount(players - humans);
            }

            switch (at.kind) {
                case Kind::Minigame: {
                    static const Minigame minigames[] = {Minigame::PipeCoin, Minigame::HammerBoss, Minigame::BombBoss, Minigame::FireBoss, Minigame::Boxes};
                    game_values.matchtype = MatchType::MiniGame;
                    mMatchSelectionMenu->SetMinigame(minigames[(at.mode >= 0 ? at.mode : idx) % 5]);
                    break;
                }
                case Kind::Tour:
                    game_values.matchtype = MatchType::Tour;
                    game_values.tourindex = static_cast<short>(idx % tourlist->count());
                    //--at-map: a tour by its file name
                    for (size_t i = 0; !at.map.empty() && i < tourlist->count(); i++) {
                        if (tourlist->at(i).stem().string() == at.map)
                            game_values.tourindex = static_cast<short>(i);
                    }
                    break;
                case Kind::World:
                    game_values.matchtype = MatchType::World;
                    game_values.worldindex = static_cast<short>(idx % worldlist->count());
                    for (size_t i = 0; !at.map.empty() && i < worldlist->count(); i++) {
                        if (worldlist->at(i).stem().string() == at.map)
                            game_values.worldindex = static_cast<short>(i);
                    }
                    break;
                case Kind::Tournament:
                    game_values.matchtype = MatchType::Tournament;
                    game_values.tournamentgames = 2;
                    break;
                default:
                    game_values.matchtype = MatchType::SingleGame;
                    break;
            }

            mTeamSelectMenu->ResetTeamSelect();
            game_values.tournamentcontrolteam = -1;
            SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);

            phase = at.kind == Kind::Single ? 1 : 2;
            startedWith = g_autoTestMatchesDone;
            return MENU_CODE_TO_GAME_SETUP_MENU;
        }

        case 1: {
            //Single game: pick mode, goal and map in the game settings menu, then start
            const int idx = at.start + scenario - 1;
            const bool suite = determinism::active();
            const short mode = static_cast<short>((suite ? determinism::modeOf(idx) : at.mode >= 0 ? at.mode : idx) % GAMEMODE_LAST);

            mGameSettingsMenu->GameModeChanged(mode);
            game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
            currentgamemode = mode;

            //Choose a small goal so the match can end by itself
            SModeOption* options = game_values.gamemode->GetOptions();
            game_values.gamemode->goal = options[1].iValue;
            mGameSettingsMenu->miGoalField[mode]->setCurrentValue(game_values.gamemode->goal);

            //Select the requested map, or advance the map list to cycle through all maps
            if (suite) {
                maplist->findexact(determinism::mapOf(idx).c_str(), false);
            } else if (!at.mapsFile.empty()) {
                pickListedMap(at.mapsFile);
            } else if (!at.map.empty()) {
                maplist->findexact(at.map.c_str(), false);
            } else {
                for (int k = 0; k < 5; k++)
                    maplist->next(false);
            }
            mGameSettingsMenu->miMapField->LoadCurrentMap();

            printf("autotest: match %d, mode %d, map %s, %d players\n", scenario, mode, maplist->currentShortmapname().c_str(), g_sim.score_cnt);
            perfstats::reset();
            if (suite)
                determinism::matchStarting(idx);

            phase = 2;
            timer = 0;
            startedWith = g_autoTestMatchesDone;
            return MENU_CODE_START_GAME;
        }

        default: {
            if (oneShot) {
                //Back in the menu after the match (not before it: the fade into it is still the
                //menus', and with --at-speed it takes many of their ticks)
                if (g_autoTestMatchesDone == startedWith && ++timer < 6000)
                    return MENU_CODE_NONE;
                phase = 0;
                timer = 0;
                return MENU_CODE_NONE;
            }

            //Tour / world: press random menu keys until a stage starts or the main menu is reached
            if (mCurrentMenu == mMainMenu.get()) {
                phase = 0;
                timer = 0;
                return MENU_CODE_NONE;
            }

            //Tournament: its screens, deterministically
            if (at.kind == Kind::Tournament)
                return AutoTestSeriesUpdate();

            if (++timer > 6000) {
                //Stuck (e.g. unreadable tour/world file): go back to the main menu
                mCurrentMenu = mMainMenu.get();
                mCurrentMenu->ResetMenu();
                return MENU_CODE_NONE;
            }

            if (timer % 4 == 0)
                pressRandomKey(false);
            return MENU_CODE_NONE;
        }
    }
}

//---------------------------------------------------------------
// --autotest --at-match shots: screenshots of the online menus
//---------------------------------------------------------------
// A scripted walk through the main menu, the Online menu and the lobby, saving PNGs of the screen
// (see scripts/menu-screenshots.sh, which runs a host and a joiner at once). Every step waits for
// what it needs (the other peer, the match...) and times out, so a broken run still ends.

namespace {
std::string g_pendingShot;
}

// --at-role pad: a controller played live from a file (<shots dir>/pad.txt), a command a line, read
// as lines are added to it: what a person with a controller would do, to try the menus and the
// editors with nothing else (no pointer put anywhere, no keys). In the menus, players 1 and 2's:
// the d-pad, A, B and X; in the editors, every button and both sticks (EditorInput.h's TestInput).
//   a, down*5          a press (two frames, then two let go), five of them
//   a:30               held for 30 frames
//   +a, -a             held down from now / let go (held through what follows: A and the d-pad)
//   stick l|r X Y N    a stick pushed to X, Y (-1..1) for N frames
//   wait N             N frames of nothing
//   shot NAME          a screenshot (NAME.png in the shots dir)
//   quit               the game quits (from the menus)
//   2:COMMAND          the second controller's (in the editors: someone else on this computer)
// Each command printed as it starts ("pad: <line>"), and "pad: idle" once there are none left.
namespace padscript {
struct Command {
    std::string what;
    int frames = 0;    // the press (or stick, or wait) lasts this long
    int gap = 0;       // then this long let go
    float x = 0, y = 0;
};

// A controller's commands, played in turn
struct Controller {
    std::deque<Command> queue;
    Command current;
    int left = 0;    // frames of `current` still to come (press, then gap)
    bool idle = true;
    std::set<std::string> held;
};

struct State {
    std::string path;
    long offset = 0;
    std::string partial;
    Controller controllers[2];
};
State g;

void start(const std::string& dir)
{
    g = State {};
    g.path = dir + "/pad.txt";
    if (FILE* f = fopen(g.path.c_str(), "rb")) {
        //(what's in it already is from before: only what's added counts)
        fseek(f, 0, SEEK_END);
        g.offset = ftell(f);
        fclose(f);
    }
}

void parse(std::string line)
{
    //(the second controller's)
    Controller& ctl = g.controllers[line.rfind("2:", 0) == 0 ? 1 : 0];
    if (&ctl == &g.controllers[1])
        line.erase(0, 2);
    std::istringstream in(line);
    std::string word;
    if (!(in >> word) || word[0] == '#')
        return;
    Command c;
    if (word == "wait") {
        in >> c.frames;
        c.what = "wait";
    } else if (word == "shot") {
        std::string name;
        in >> name;
        c.what = "shot " + name;
        c.frames = 1;
    } else if (word == "quit") {
        c.what = word;
        c.frames = 1;
    } else if (word == "stick") {
        std::string which;
        in >> which >> c.x >> c.y >> c.frames;
        c.what = "stick " + which;
        c.gap = 2;
    } else if (word[0] == '+' || word[0] == '-') {
        c.what = word;
        c.frames = 1;
    } else {
        int times = 1;
        c.frames = 2;
        if (const size_t star = word.find('*'); star != std::string::npos) {
            times = std::max(1, atoi(word.c_str() + star + 1));
            word.resize(star);
        }
        if (const size_t colon = word.find(':'); colon != std::string::npos) {
            c.frames = std::max(1, atoi(word.c_str() + colon + 1));
            word.resize(colon);
        }
        c.what = word;
        c.gap = 2;
        for (int k = 0; k < times; k++)
            ctl.queue.push_back(c);
        return;
    }
    ctl.queue.push_back(c);
}

void poll()
{
    FILE* f = fopen(g.path.c_str(), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    const long end = ftell(f);
    if (end > g.offset) {
        std::string text(static_cast<size_t>(end - g.offset), '\0');
        fseek(f, g.offset, SEEK_SET);
        text.resize(fread(text.data(), 1, text.size(), f));
        g.offset += static_cast<long>(text.size());
        g.partial += text;
        size_t eol;
        while ((eol = g.partial.find('\n')) != std::string::npos) {
            std::string line = g.partial.substr(0, eol);
            g.partial.erase(0, eol + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            parse(line);
        }
    } else if (end < g.offset) {
        g.offset = end;    //(the file started over)
    }
    fclose(f);
}

// One frame: what's pressed (the buttons' names, and the sticks), and a screenshot or quit
struct Frame {
    std::set<std::string> down;
    bool pressedNow = false;    // the first frame of a command (the menus want presses)
    float lx = 0, ly = 0, rx = 0, ry = 0;
    std::string shot;
    bool quit = false;
};

Frame step(int which = 0)
{
    if (which == 0)
        poll();
    Controller& g = padscript::g.controllers[which];
    const char* tag = which == 1 ? "2:" : "";
    Frame out;
    if (g.left <= 0) {
        if (g.queue.empty()) {
            if (!g.idle) {
                printf("pad: %sidle\n", tag);
                fflush(stdout);
            }
            g.idle = true;
            out.down = g.held;
            return out;
        }
        g.idle = false;
        g.current = g.queue.front();
        g.queue.pop_front();
        g.left = g.current.frames + g.current.gap;
        printf("pad: %s%s\n", tag, g.current.what.c_str());
        fflush(stdout);
        out.pressedNow = true;
    }
    const Command& c = g.current;
    const bool pressing = g.left > c.gap;
    g.left--;
    if (c.what[0] == '+' && out.pressedNow)
        g.held.insert(c.what.substr(1));
    else if (c.what[0] == '-' && out.pressedNow)
        g.held.erase(c.what.substr(1));
    out.down = g.held;
    if (c.what.rfind("shot ", 0) == 0) {
        if (out.pressedNow)
            out.shot = c.what.substr(5);
    } else if (c.what == "quit") {
        out.quit = true;
    } else if (c.what.rfind("stick ", 0) == 0) {
        if (pressing) {
            (c.what == "stick r" ? out.rx : out.lx) = c.x;
            (c.what == "stick r" ? out.ry : out.ly) = c.y;
        }
    } else if (c.what != "wait" && c.what[0] != '+' && c.what[0] != '-' && pressing) {
        out.down.insert(c.what);
    }
    return out;
}

#ifdef SMW_HAS_EDITORS
editors::input::TestInput editorInput(editors::Editor, int, int pointer)
{
    editors::input::TestInput t;
    if (pointer > 1)
        return t;
    const Frame f = step(pointer);
    auto on = [&](const char* name) { return f.down.count(name) > 0; };
    t.a = on("a"), t.b = on("b"), t.x = on("x"), t.y = on("y");
    t.lb = on("lb"), t.rb = on("rb"), t.back = on("back"), t.start = on("start");
    t.up = on("up"), t.down = on("down"), t.left = on("left"), t.right = on("right");
    t.lt = on("lt"), t.rt = on("rt"), t.ls = on("ls"), t.rs = on("rs");
    t.lx = f.lx, t.ly = f.ly, t.rx = f.rx, t.ry = f.ry;
    t.shot = f.shot;
    return t;
}
#endif
} // namespace padscript

void MenuState::AutoTestAfterDraw()
{
    if (g_pendingShot.empty())
        return;

    const std::string path = cmd::g_autotest.shotsDir + "/" + g_pendingShot + ".png";
    g_pendingShot.clear();
    if (IMG_SavePNG(screen, path.c_str()))
        printf("shots: saved %s\n", path.c_str());
    else
        printf("shots: couldn't save %s: %s\n", path.c_str(), SDL_GetError());
}

MenuCodeEnum MenuState::ShotsMenuUpdate()
{
    const cmd::AutoTestArgs& at = cmd::g_autotest;
    rollback::NetGame& net = rollback::NetGame::instance();
    using Phase = rollback::NetLobby::Phase;

    static int menuFrames = 0;
    static int step = 0;
    static int t = 0;
    static bool shotTaken = false;
    ++menuFrames;
    ++t;
    //(--at-pad 1: the controller's prompts throughout, whatever the window's mouse does)
    if (at.pad)
        prompts::setLastDevice(prompts::Device::Controller);

    auto next = [&]() {
        ++step;
        t = 0;
        shotTaken = false;
    };
    auto shot = [&](const char* name) {
        printf("shots: %s\n", name);
        g_pendingShot = name;
    };
    auto exitSoon = [&]() {
        //Leave the lobby tidily, then quit
        if (net.active())
            net.leave();
        return MENU_CODE_EXIT_APPLICATION;
    };
    //A step that gives up after a while: takes a screenshot of what is on the screen and quits
    auto timedOut = [&](int limit, const char* what) {
        if (t < limit)
            return false;
        printf("shots: timed out waiting for %s (step %d)\n", what, step);
        shot("zz_timeout");
        step = 1000;
        t = 0;
        return true;
    };

    if (step == 1000)
        return t > 3 ? exitSoon() : MENU_CODE_NONE;
    if (menuFrames >= at.frames)
        return exitSoon();

    const std::string nick = at.shotsName.empty() ? (at.shotsRole == "join" ? "Luigi" : "Mario") : at.shotsName;
    rollback::NetLobby* lobby = net.lobby();

    //--at-role pad: a controller played live from <shots dir>/pad.txt (padscript, above)
    if (at.shotsRole == "pad") {
        if (step == 0) {
            padscript::start(at.shotsDir);
#ifdef SMW_HAS_EDITORS
            editors::input::setTestDriver(padscript::editorInput, at.shotsDir, 2);
#endif
            printf("pad: reading %s/pad.txt\n", at.shotsDir.c_str());
            fflush(stdout);
            step = 1;
        }
        prompts::setLastDevice(prompts::Device::Controller);
        const padscript::Frame f = padscript::step();
        if (f.quit)
            return exitSoon();
        if (!f.shot.empty())
            shot(f.shot.c_str());
        //(players 1 and 2: the second's commands start with "2:"; X is the menus' random)
        auto press = [](const padscript::Frame& frame, COutputControl& keys) {
            if (!frame.pressedNow)
                return;
            keys.menu_up.fPressed = frame.down.count("up") > 0;
            keys.menu_down.fPressed = frame.down.count("down") > 0;
            keys.menu_left.fPressed = frame.down.count("left") > 0;
            keys.menu_right.fPressed = frame.down.count("right") > 0;
            keys.menu_select.fPressed = frame.down.count("a") > 0;
            keys.menu_cancel.fPressed = frame.down.count("b") > 0;
            keys.menu_random.fPressed = frame.down.count("x") > 0;
        };
        press(f, game_values.playerInput.outputControls[0]);
        press(padscript::step(1), game_values.playerInput.outputControls[1]);
        return MENU_CODE_NONE;
    }

    //The main menu, then the Online menu: the same start for every role
    if (step == 0) {
        if (t == 25)
            shot("01_main_menu");
        if (t == 28)
            next();
        return MENU_CODE_NONE;
    }
#ifdef SMW_HAS_EDITORS
    //--at-role editors: the editors, played with a scripted controller (EditorInput.h), then a match
    if (at.shotsRole == "editors") {
        using editors::input::TestInput;
        //A button held for two frames from `from`
        auto tap = [](int f, int from) { return f >= from && f < from + 2; };
        auto levelScript = [tap](editors::Editor, int f, int pointer) {
            TestInput t;
            //Players 2 and 3 (local editing together): their reticles on the map
            if (pointer > 0) {
                if (f >= 5) {
                    t.px = pointer == 1 ? 420 : 520;
                    t.py = pointer == 1 ? 120 : 300;
                }
                return t;
            }
            if (f == 10) t.shot = "e1_level_start";
            if (f >= 20 && f < 45) { t.lx = 0.8f; t.ly = 0.5f; }
            t.a = (f >= 50 && f < 75);              //paint a stroke
            if (f >= 55 && f < 75) t.lx = 1.0f;
            if (f == 52) t.shot = "e2a_level_painting";    //(the glove pressing)
            if (f == 80) t.shot = "e2_level_painted";
            //The editor window (it opens at the left: the pointer is at the right), with the d-pad
            t.start = tap(f, 90);
            if (f == 100) t.shot = "e3_level_window";
            t.right = tap(f, 104);
            t.down = tap(f, 108) || tap(f, 111) || tap(f, 114) || tap(f, 117) || tap(f, 120) || tap(f, 123);    //(the page scrolls)
            if (f == 127) t.shot = "e3a_level_window_hover";
            t.a = t.a || tap(f, 130);                            //(takes it, back to the map)
            t.start = t.start || tap(f, 138);
            t.lb = tap(f, 142);                                  //(Terrain)
            if (f == 148) t.shot = "e3b_level_terrain";
            t.rb = tap(f, 152) || tap(f, 156) || tap(f, 160);    //(Types)
            if (f == 166) t.shot = "e3c_level_types";
            //The pointer into the window: it clicks there
            if (f >= 170 && f < 184) { t.px = 264; t.py = 22; }  //(to the other side)
            t.a = t.a || tap(f, 172);
            if (f == 178) t.shot = "e3d_level_window_right";
            t.start = t.start || tap(f, 182);
            //A block on the map, the next one (RB), then the chip
            if (f >= 188 && f < 202) { t.px = 176; t.py = 336; }
            t.a = t.a || tap(f, 192);
            t.rb = t.rb || tap(f, 198);
            if (f == 204) t.shot = "e3e_level_chip";
            //"Save as...": Shift (LT) and S
            t.lt = f >= 208 && f < 220;
            if (f == 212) t.key = SDLK_S;
            if (f == 245) t.shot = "e5_level_keyboard";
            t.down = t.down || tap(f, 250);
            t.right = t.right || tap(f, 255) || tap(f, 259);
            t.a = t.a || tap(f, 265) || tap(f, 269);
            if (f == 280) t.shot = "e6_level_typed";
            t.back = tap(f, 285);
            if (f == 305) t.shot = "e7_level_back_in_editor";
            t.back = t.back || tap(f, 315) || tap(f, 321);    //(the first only leaves block mode)
            if (f == 330) t.shot = "e8_level_exit_prompt";
            if (f >= 335) { t.px = 282; t.py = 266; }
            t.a = t.a || tap(f, 340);
            //(still here: try again)
            if (f > 370 && f % 40 == 0) t.back = true;
            if (f > 370 && f % 40 == 20) t.a = true;
            return t;
        };
        auto worldScript = [tap](editors::Editor, int f, int) {
            TestInput t;
            if (f == 10) t.shot = "w1_world_start";
            if (f >= 20 && f < 50) t.ry = 1.0f;     //scrolls the world (arrow keys)
            if (f >= 20 && f < 35) t.rx = 1.0f;
            if (f == 55) t.shot = "w2_world_scrolled";
            t.start = tap(f, 60) || tap(f, 74);    //(the window, open and closed)
            if (f == 70) t.shot = "w3_world_actions";
            //"Edit stages" (E), then a click on stage 1
            if (f == 80) t.key = SDLK_E;
            if (f >= 140 && f < 150) { t.px = 16; t.py = 16; }
            t.b = tap(f, 150);    //(right click: edit the stage)
            if (f == 170) t.shot = "w5_world_stage_menu";
            t.back = tap(f, 180) || tap(f, 195) || tap(f, 210);
            if (f == 225) t.shot = "w4_world_exit_prompt";
            if (f >= 228) { t.px = 282; t.py = 266; }
            t.a = t.a || tap(f, 232);
            if (f > 260 && f % 40 == 0) t.back = true;
            if (f > 260 && f % 40 == 20) t.a = true;
            return t;
        };
        switch (step) {
            case 1:
                if (t == 1)
                    return MENU_CODE_TO_EDITORS_MENU;
                if (t == 15)
                    shot("e0_editors_menu");
                //Maps: on this computer or online (joining an address), then back
                if (t == 18) {
                    mEditorsMenu->Want(UI_EditorsMenu::Action::Maps);
                    return MENU_CODE_EDITORS_ACTION;
                }
                if (t == 26)
                    shot("e0a_editors_maps");
                if (t == 28) {
                    mEditorsMenu->Want(UI_EditorsMenu::Action::Online);
                    return MENU_CODE_EDITORS_ACTION;
                }
                if (t == 30) {
                    mEditorsMenu->Want(UI_EditorsMenu::Action::Join);
                    return MENU_CODE_EDITORS_ACTION;
                }
                if (t == 38)
                    shot("e0b_editors_online");
                if (t == 40)
                    return MENU_CODE_EDITORS_ACTION;    //(back)
                if (t == 44)
                    next();
                return MENU_CODE_NONE;
            case 2:
                if (t == 1) {
                    editors::input::setTestDriver(levelScript, at.shotsDir, 3);
                    return MENU_CODE_RUN_LEVEL_EDITOR;
                }
                if (t == 6)
                    shot("e9_menu_after_level_editor");
                if (t == 8)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    editors::input::setTestDriver(worldScript, at.shotsDir);
                    return MENU_CODE_RUN_WORLD_EDITOR;
                }
                if (t == 6) {
                    editors::input::setTestDriver(nullptr, {});
                    shot("w9_menu_after_world_editor");
                }
                if (t == 8) {
                    //Both again: a second session starts over
                    static int rounds = 0;
                    if (++rounds < 2) {
                        step = 2;
                        t = 0;
                        return MENU_CODE_NONE;
                    }
                    next();
                }
                return MENU_CODE_NONE;
            case 4:
                //The game still plays (its graphics and settings came back)
                if (t == 1)
                    return MENU_CODE_EDITORS_EXIT;
                if (t == 5)
                    shot("g0_main_menu_after_editors");
                if (t == 8) {
                    //Two CPU players, a single game on the current map
                    for (int k = 0; k < 4; k++)
                        game_values.playercontrol[k] = k < 2 ? 2 : 0;
                    game_values.matchtype = MatchType::SingleGame;
                    mTeamSelectMenu->ResetTeamSelect();
                    game_values.tournamentcontrolteam = -1;
                    SetControllingTeamForSettingsMenu(game_values.tournamentcontrolteam, false);
                    return MENU_CODE_TO_GAME_SETUP_MENU;
                }
                if (t == 10) {
                    mGameSettingsMenu->GameModeChanged(0);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = 0;
                    mGameSettingsMenu->miMapField->LoadCurrentMap();
                    next();
                    return MENU_CODE_START_GAME;
                }
                return MENU_CODE_NONE;
            case 5:
                //(back from the match: it ran in the gameplay state)
                if (g_autoTestMatchesDone >= 1 && t > 3) {
                    shot("g9_menu_after_match");
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 60, "the match after the editors");
                return MENU_CODE_NONE;
        }
    }
#endif

#ifdef SMW_HAS_EDITORS
    //--at-role edtools: the editors' controller tools (rectangles, undo, copy, paste, fill, the
    //last things picked, the whole map, the tileset screen)
    if (at.shotsRole == "edtools") {
        using editors::input::TestInput;
        auto tap = [](int f, int from) { return f >= from && f < from + 2; };
        auto levelScript = [tap](editors::Editor, int f, int) {
            TestInput t;
            //A rectangle of the ground's tile (taken with X): A held from one cell to another
            if (f < 5) { t.px = 16; t.py = 464; }
            t.x = f >= 2 && f < 4;
            if (f >= 5 && f < 26) { t.px = 80; t.py = 240; }
            t.a = f >= 20 && f < 45;
            if (f >= 26 && f < 45) { t.px = 240; t.py = 304; }
            if (f == 38) t.shot = "x1_rect_drag";
            if (f == 50) t.shot = "x2_rect_filled";
            //The tools: undo (the menu stays), then redo
            t.rs = tap(f, 55);
            if (f == 62) t.shot = "x3_tools";
            t.a = t.a || tap(f, 66);
            if (f == 72) t.shot = "x4_undone";
            t.down = tap(f, 76);
            t.a = t.a || tap(f, 80);
            t.b = tap(f, 86);
            //Copy an area
            t.rs = t.rs || tap(f, 92);
            t.down = t.down || tap(f, 96) || tap(f, 100);
            t.a = t.a || tap(f, 104);
            if (f >= 108 && f < 118) { t.px = 80; t.py = 240; }
            t.a = t.a || (f >= 112 && f < 128);
            if (f >= 118 && f < 128) { t.px = 176; t.py = 272; }
            if (f == 124) t.shot = "x5_copy_drag";
            //Paste it
            t.rs = t.rs || tap(f, 134);
            t.down = t.down || tap(f, 138) || tap(f, 142) || tap(f, 146);
            t.a = t.a || tap(f, 150);
            if (f >= 154 && f < 176) { t.px = 400; t.py = 96; }
            if (f == 160) t.shot = "x6_paste_preview";
            t.a = t.a || tap(f, 164);
            t.b = t.b || tap(f, 170);
            if (f == 178) t.shot = "x7_pasted";
            //The ground's tile taken (X), then the rectangle's tiles filled with it
            if (f >= 180 && f < 188) { t.px = 16; t.py = 464; }
            t.x = t.x || tap(f, 183);
            t.rs = t.rs || tap(f, 188);
            t.down = t.down || tap(f, 192) || tap(f, 196) || tap(f, 200) || tap(f, 204);
            t.a = t.a || tap(f, 208);
            if (f >= 212 && f < 230) { t.px = 112; t.py = 272; }
            if (f == 216) t.shot = "x8_fill_ready";
            t.a = t.a || tap(f, 220);
            if (f == 228) t.shot = "x9_filled";
            //The last things picked: LB and RB go through them
            t.rb = t.rb || tap(f, 234);
            t.lb = tap(f, 240);
            if (f >= 236 && f < 250) { t.px = 560; t.py = 160; }
            if (f == 246) t.shot = "x10_recents";
            //The whole map (the left stick held), with the window open
            t.start = tap(f, 252);
            t.ls = f >= 262 && f < 276;
            if (f == 270) t.shot = "x11_whole_map";
            if (f == 280) t.shot = "x12_window";
            t.start = t.start || tap(f, 286);
            //The tileset screen: the d-pad tile by tile, the tileset scrolls at the edge
            if (f == 292) t.key = SDLK_T;
            if (f >= 298 && f < 302) { t.px = 48; t.py = 48; }
            t.down = t.down || tap(f, 304) || tap(f, 308);
            if (f == 314) t.shot = "x13_tile_picker";
            for (int k = 0; k < 24; k++)
                t.right = t.right || tap(f, 318 + k * 4);
            if (f == 418) t.shot = "x14_tile_picker_scrolled";
            t.a = t.a || tap(f, 422);
            //Out
            t.back = tap(f, 440) || tap(f, 450) || tap(f, 460);
            if (f >= 470) { t.px = 282; t.py = 266; }
            t.a = t.a || tap(f, 474);
            if (f > 490 && f % 40 == 0) t.back = true;
            if (f > 490 && f % 40 == 20) t.a = true;
            return t;
        };
        auto worldScript = [tap](editors::Editor, int f, int) {
            TestInput t;
            if (f >= 5 && f < 10) { t.px = 300; t.py = 200; }
            t.ls = f >= 12 && f < 30;
            if (f == 22) t.shot = "x15_whole_world";
            t.back = tap(f, 40) || tap(f, 55) || tap(f, 70);
            if (f >= 80) { t.px = 282; t.py = 266; }
            t.a = tap(f, 84);
            if (f > 100 && f % 40 == 0) t.back = true;
            if (f > 100 && f % 40 == 20) t.a = true;
            return t;
        };
        switch (step) {
            case 1:
                if (t == 1) {
                    editors::input::setTestDriver(levelScript, at.shotsDir);
                    return MENU_CODE_RUN_LEVEL_EDITOR;
                }
                if (t == 8)
                    next();
                return MENU_CODE_NONE;
            case 2:
                if (t == 1) {
                    editors::input::setTestDriver(worldScript, at.shotsDir);
                    return MENU_CODE_RUN_WORLD_EDITOR;
                }
                if (t == 8) {
                    editors::input::setTestDriver(nullptr, {});
                    step = 1000;
                    t = 0;
                }
                return MENU_CODE_NONE;
        }
    }

    //--at-role testplay: "Play this map" with a controller only: a tile painted in the air, the tools
    //(Start) and their first entry (A); the match (its screenshot, GameplayState::updateAutoTest) ends at
    //--at-frames; the editor opens again on the map, the tile still there; then out
    if (at.shotsRole == "testplay") {
        using editors::input::TestInput;
        static int sessions = 0;
        auto tap = [](int f, int from) { return f >= from && f < from + 2; };
        auto script = [tap](editors::Editor, int f, int) {
            if (f == 0)
                ++sessions;
            TestInput t;
            if (sessions == 1) {
                //The ground's tile taken (X), then painted in the air (A)
                if (f < 6) { t.px = 16; t.py = 464; }
                t.x = f >= 2 && f < 4;
                if (f >= 6 && f < 30) { t.px = 304; t.py = 208; }
                t.a = f >= 12 && f < 18;
                if (f == 24) t.shot = "p0_painted";
                //The window (Start), its play button (in the title strip, left of the window's own)
                t.start = tap(f, 30);
                if (f >= 36 && f < 60) { t.px = 560; t.py = 26; }
                if (f == 42) t.shot = "p1_tools";
                t.a = t.a || tap(f, 48);
            } else {
                if (f == 20) t.shot = "p3_back_in_editor";
                t.back = tap(f, 40) || tap(f, 55) || tap(f, 70);
                if (f >= 80) { t.px = 282; t.py = 266; }
                t.a = tap(f, 84);
                if (f > 100 && f % 40 == 0) t.back = true;
                if (f > 100 && f % 40 == 20) t.a = true;
            }
            return t;
        };
        if (step == 1) {
            if (t == 1) {
                editors::input::setTestDriver(script, at.shotsDir);
                return MENU_CODE_RUN_LEVEL_EDITOR;
            }
            //(the match and the editor again run inside here's updates' gaps: done once the second closed)
            if (sessions >= 2 && t > 20) {
                editors::input::setTestDriver(nullptr, {});
                printf("shots: test play done, %d editor sessions\n", sessions);
                step = 1000;
                t = 0;
            }
            return MENU_CODE_NONE;
        }
    }

    //--at-role edopen: the editor opened as a player does, with the menus' buttons (main menu: down to
    //Editors, A; Maps, A; Start, A); a screenshot from inside it, then out
    if (at.shotsRole == "edopen" && step == 1) {
        COutputControl& keys = game_values.playerInput.outputControls[0];
        if (t == 2) {
            using editors::input::TestInput;
            editors::input::setTestDriver([](editors::Editor, int f, int) {
                TestInput in;
                if (f == 20) in.shot = "o4_in_the_editor";
                if (f > 30 && f % 40 == 0) in.back = true;
                if (f > 30 && f % 40 == 20) { in.px = 282; in.py = 266; in.a = true; }
                return in;
            }, at.shotsDir);
        }
        //(down four rows: Players, Join a Game, Options, History; right: Editors)
        if (t == 10 || t == 14 || t == 18 || t == 22)
            keys.menu_down.fPressed = true;
        if (t == 26)
            keys.menu_right.fPressed = true;
        if (t == 30)
            shot("o1_main_on_editors");
        if (t == 34)
            keys.menu_select.fPressed = true;
        if (t == 50)
            shot("o2_editors_menu");
        if (t == 54)
            keys.menu_select.fPressed = true;
        if (t == 70)
            shot("o3_editors_setup");
        if (t == 74)
            keys.menu_select.fPressed = true;
        if (t == 200) {
            printf("shots: still in the menus 125 frames after Start (the editor didn't open)\n");
            shot("o9_still_in_menus");
            editors::input::setTestDriver(nullptr, {});
            step = 1000;
            t = 0;
        }
        return MENU_CODE_NONE;
    }

    //--at-role edtwice: the level editor opened twice in a row (its graphics kept between: the log
    //has one "loading graphics")
    if (at.shotsRole == "edtwice" && (step == 1 || step == 2)) {
        using editors::input::TestInput;
        auto leave = [](editors::Editor, int f, int) {
            TestInput in;
            if (f == 10) in.shot = step == 1 ? "w1_first" : "w2_second";
            if (f > 20 && f % 40 == 0) in.back = true;
            if (f > 20 && f % 40 == 20) { in.px = 282; in.py = 266; in.a = true; }
            return in;
        };
        if (t == 1) {
            editors::input::setTestDriver(leave, at.shotsDir);
            return MENU_CODE_RUN_LEVEL_EDITOR;
        }
        if (t == 8) {
            if (step == 2) {
                editors::input::setTestDriver(nullptr, {});
                step = 1000;
                t = 0;
            } else {
                next();
            }
        }
        return MENU_CODE_NONE;
    }

    //--at-role edux: the level editor's screens with a controller: a ? block's contents, the zones page,
    //B on the tileset screen, and the exit prompt (B stays; left and A leave)
    if (at.shotsRole == "edux") {
        using editors::input::TestInput;
        auto tap = [](int f, int from) { return f >= from && f < from + 2; };
        auto levelScript = [tap](editors::Editor, int f, int) {
            TestInput t;
            if (f >= 5 && f < 10) { t.px = 496; t.py = 80; }    //(the map's ? block)
            if (f == 12) t.key = SDLK_K;
            if (f == 25) t.shot = "u1_block_contents";
            t.back = tap(f, 30);
            if (f == 40) t.shot = "u2_back_on_map";
            if (f == 41) t.key = SDLK_F1;
            if (f == 43) t.shot = "u2b_help";
            t.a = tap(f, 44);
            if (f == 47) t.key = SDLK_X;
            t.start = tap(f, 50);
            t.rb = tap(f, 54) || tap(f, 57) || tap(f, 60);    //(blocks, items, types, zones)
            if (f == 64) t.shot = "u3_zones_page";
            t.b = tap(f, 68);
            if (f == 75) t.key = SDLK_T;
            if (f == 85) t.shot = "u4_tile_picker";
            t.b = t.b || tap(f, 90);
            if (f == 100) t.shot = "u5_after_picker_b";
            //Move mode: its commands in the tools
            if (f == 102) t.key = SDLK_M;
            t.rs = tap(f, 106);
            if (f == 112) t.shot = "u5b_move_tools";
            t.b = t.b || tap(f, 116);
            t.back = t.back || tap(f, 150);
            if (f == 160) t.shot = "u6_exit_prompt";
            t.b = t.b || tap(f, 165);
            if (f == 175) t.shot = "u7_stayed";
            t.back = t.back || tap(f, 180);
            t.left = tap(f, 190);
            if (f == 196) t.shot = "u8_yes_chosen";
            t.a = t.a || tap(f, 200);
            if (f > 230 && f % 40 == 0) t.back = true;
            if (f > 230 && f % 40 == 20) t.a = true;
            return t;
        };
        if (step == 1) {
            if (t == 1) {
                editors::input::setTestDriver(levelScript, at.shotsDir);
                return MENU_CODE_RUN_LEVEL_EDITOR;
            }
            if (t == 8) {
                editors::input::setTestDriver(nullptr, {});
                step = 1000;
                t = 0;
            }
            return MENU_CODE_NONE;
        }
    }

    //--at-role edmouse: the level editor with the mouse (SDL's events, as a real one sends): Alt and a
    //click picks up a cell, Shift and a drag fills a rectangle, Ctrl and the wheel change the layer,
    //Ctrl+C copies
    if (at.shotsRole == "edmouse") {
        using editors::input::TestInput;
        auto levelScript = [](editors::Editor, int f, int) {
            auto button = [](float x, float y, Uint8 b, bool down) {
                SDL_Event e {};
                e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.button = b;
                e.button.down = down;
                e.button.x = x;
                e.button.y = y;
                SDL_PushEvent(&e);
            };
            auto move = [](float x, float y, Uint32 state) {
                SDL_Event e {};
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.x = x;
                e.motion.y = y;
                e.motion.state = state;
                SDL_PushEvent(&e);
            };
            TestInput t;
            switch (f) {
                case 5: move(16, 464, 0); break;
                case 8: SDL_SetModState(SDL_KMOD_LALT); button(16, 464, SDL_BUTTON_LEFT, true); break;    //(the ground's tile)
                case 10: button(16, 464, SDL_BUTTON_LEFT, false); SDL_SetModState(SDL_KMOD_NONE); break;
                case 14: move(80, 240, 0); break;
                case 16: SDL_SetModState(SDL_KMOD_LSHIFT); button(80, 240, SDL_BUTTON_LEFT, true); break;
                case 20: move(160, 272, SDL_BUTTON_LMASK); break;
                case 24: move(240, 304, SDL_BUTTON_LMASK); break;
                case 28: t.shot = "m1_shift_drag"; break;
                case 32: button(240, 304, SDL_BUTTON_LEFT, false); SDL_SetModState(SDL_KMOD_NONE); break;
                case 40: t.shot = "m2_rect_filled"; break;
                case 44: {
                    SDL_SetModState(SDL_KMOD_LCTRL);
                    SDL_Event e {};
                    e.type = SDL_EVENT_MOUSE_WHEEL;
                    e.wheel.y = -1;
                    SDL_PushEvent(&e);
                    break;
                }
                case 46: SDL_SetModState(SDL_KMOD_NONE); break;
                case 52: t.shot = "m3_ctrl_wheel_layer"; break;
                case 56: {
                    SDL_Event e {};
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDLK_C;
                    e.key.mod = SDL_KMOD_LCTRL;
                    e.key.down = true;
                    SDL_PushEvent(&e);
                    break;
                }
                case 62: t.shot = "m4_ctrl_c"; break;
                case 64: button(300, 300, SDL_BUTTON_RIGHT, true); break;    //(the copy put away)
                case 65: button(300, 300, SDL_BUTTON_RIGHT, false); break;
                default: break;
            }
            if (f > 90 && f % 40 == 0) t.back = true;
            if (f > 90 && f % 40 == 20) { t.px = 282; t.py = 266; t.a = true; }
            return t;
        };
        if (step == 1) {
            if (t == 1) {
                editors::input::setTestDriver(levelScript, at.shotsDir);
                return MENU_CODE_RUN_LEVEL_EDITOR;
            }
            if (t == 8) {
                editors::input::setTestDriver(nullptr, {});
                step = 1000;
                t = 0;
            }
            return MENU_CODE_NONE;
        }
    }

    //--at-role colocal: two controllers in one level editor, painting at the same time
    if (at.shotsRole == "colocal") {
        auto script = [](editors::Editor, int f, int pointer) {
            editors::input::TestInput t;
            //(controller 1 picks a tile first)
            if (pointer == 0 && f == 10)
                t.key = SDLK_T;
            if (pointer == 0 && f >= 16 && f < 24) {
                t.px = 80;
                t.py = 48;
                t.a = f >= 18 && f < 21;
            }
            //Both paint: controller 1 places a row, controller 2 removes another, at once
            if (f >= 40 && f < 90) {
                t.px = 40.0f + static_cast<float>(f - 40) * 11.0f;
                t.py = pointer == 0 ? 96.0f : 288.0f;
                t.a = true;
            }
            if (f == 100)
                t.shot = "local_two_cursors";
            if (f > 120) {
                const int k = f % 40;
                t.back = pointer == 0 && k < 2;
                if (k >= 10 && pointer == 0) {
                    t.px = 282;
                    t.py = 266;
                }
                t.a = pointer == 0 && k >= 14 && k < 16;
            }
            return t;
        };
        if (step == 1) {
            if (t == 1) {
                editors::input::setTestDriver(script, at.shotsDir, 2);
                return MENU_CODE_RUN_LEVEL_EDITOR;
            }
            if (t == 4) {
                step = 1000;
                t = 0;
            }
            return MENU_CODE_NONE;
        }
    }

    //--at-role cohost / cojoin (--at-addr): two games edit one map together, with scripted controllers
    //(--at-shotmap 1: the world editor). Both print their document's hash: they must match.
    if (at.shotsRole == "cohost" || at.shotsRole == "cojoin") {
        const bool hostRole = at.shotsRole == "cohost";
        const bool world = at.shotsMap == 1;
        static int start = -1;    //(the frame the other one was there)
        auto script = [hostRole](editors::Editor, int f, int pointer) {
            using editors::input::TestInput;
            TestInput t;
            if (pointer != 0)
                return t;
            //The host: another map/world first (the joiner must get it), then wait for the joiner
            if (hostRole && f == 5)
                t.key = SDLK_PAGEDOWN;
            //The level editor's host picks a tile to paint with (the tile mode's palette)
            if (hostRole && f == 10)
                t.key = SDLK_T;
            if (hostRole && f >= 16 && f < 24) {
                t.px = 80;
                t.py = 48;
                t.a = f >= 18 && f < 21;
            }
            const bool together = editors::coedit::people().size() >= 2;
            if (start < 0 && together)
                start = f;
            if (start < 0)
                return t;
            const int g = f - start;
            if (g == 20)
                t.shot = hostRole ? "co_host_start" : "co_join_start";
            //Paint: the host a row at y 96, the joiner one at y 320; both the row at y 224 (place against remove)
            if (g >= 30 && g < 80) {
                const float x = 40.0f + static_cast<float>(g - 30) * 11.0f;
                t.px = x;
                t.py = hostRole ? 96.0f : 320.0f;
                t.a = hostRole;
                t.b = !hostRole;
            }
            if (g >= 90 && g < 140) {
                t.px = 40.0f + static_cast<float>(g - 90) * 11.0f + (hostRole ? 0.0f : 3.0f);
                t.py = 224.0f;
                t.a = hostRole;
                t.b = !hostRole;
            }
            if (g == 150)
                t.shot = hostRole ? "co_host_painted" : "co_join_painted";
            //A joiner's Save: only the host saves the shared file
            if (!hostRole && g == 170)
                t.key = SDLK_S;
            if (g == 200 || g == 260) {
                const editors::coedit::Stats& st = editors::coedit::stats();
                printf("cotest: %s frame %d hash %016llx sent %llu applied %llu skipped %llu\n", hostRole ? "host" : "join", g,
                    static_cast<unsigned long long>(editors::coedit::documentHash()), static_cast<unsigned long long>(st.opsSent),
                    static_cast<unsigned long long>(st.opsApplied), static_cast<unsigned long long>(st.opsSkipped));
            }
            //Leave: the joiner first, then the host once alone
            const bool leave = hostRole ? (g > 300 && !together) || g > 900 : g > 280;
            if (leave) {
                const int k = f % 40;
                t.back = k < 2;
                if (k >= 10) {
                    t.px = 282;
                    t.py = 266;
                }
                t.a = k >= 14 && k < 16;
            }
            return t;
        };
        switch (step) {
            case 1:
                if (t == 1) {
                    game_values.onlinename = hostRole ? "Mario" : "Luigi";
                    editors::input::setTestDriver(script, at.shotsDir);
                    if (hostRole)
                        return world ? MENU_CODE_HOST_WORLD_EDITOR : MENU_CODE_HOST_LEVEL_EDITOR;
                    mEditorsMenu->joinAddressRef() = at.shotsAddr;
                    return MENU_CODE_JOIN_EDITOR;
                }
                //(the joiner: back here once its editor closed)
                if (t > 2 && editors::coedit::state() == editors::coedit::State::Off) {
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 60, "the editing session");
                return MENU_CODE_NONE;
        }
    }
#endif

    //--at-role controls: the controls menu (its "Button Prompts" option)
    if (at.shotsRole == "controls" && step == 1) {
        if (t == 1)
            return MENU_CODE_TO_CONTROLS_MENU;
        if (t == 20)
            shot("c1_controls_menu");
        //(then player 1's: the keys and the device)
        if (t == 24)
            game_values.playerInput.outputControls[0].menu_select.fPressed = true;
        if (t == 44) {
            shot("c2_player_controls");
            step = 1000;
            t = 0;
        }
        return MENU_CODE_NONE;
    }

    //--at-role locallobby: Start (the lobby of a local game), its options, a CPU, Continue to the local
    //match menus, back to the lobby (as it was), others let in (LAN), then a local match
    if (at.shotsRole == "locallobby" && step == 1) {
        rollback::NetGame& net = rollback::NetGame::instance();
        switch (t) {
            case 1: return MENU_CODE_TO_LOCAL_LOBBY;
            case 20: shot("l1_lobby"); break;
            case 25: return MENU_CODE_ONLINE_OPTIONS;
            case 30:
                if (net.lobby())
                    net.lobby()->setCpuCount(2);
                break;
            case 45: shot("l2_options"); break;
            case 50: return MENU_CODE_ONLINE_OPTIONS_BACK;
            case 65: shot("l3_lobby_cpus"); break;
            case 70: return MENU_CODE_ONLINE_MATCH_SETTINGS;
            case 85: shot("l4_match_type"); break;
            case 90: return MENU_CODE_MATCH_SELECTION_START;
            case 105: shot("l5_game_settings"); break;
            case 110: return MENU_CODE_BACK_TEAM_SELECT_MENU;
            case 125: shot("l6_back_match_type"); break;
            case 130: return MENU_CODE_TO_MAIN_MENU;
            case 145: shot("l7_lobby_again"); break;
            case 150: {
                std::string error;
                if (!net.setAccess(rollback::LobbyAccess::Lan, error))
                    printf("shots: can't open the lobby: %s\n", error.c_str());
                break;
            }
            case 170: shot("l8_lobby_lan"); break;
            case 175: {
                std::string error;
                net.setAccess(rollback::LobbyAccess::Nobody, error);
                break;
            }
            case 180: return MENU_CODE_ONLINE_MATCH_SETTINGS;
            case 190: return MENU_CODE_MATCH_SELECTION_START;
            case 200:
                printf("shots: local game: %d teams, players %d %d %d %d, lobby open: %d\n", g_sim.score_cnt, game_values.playercontrol[0],
                    game_values.playercontrol[1], game_values.playercontrol[2], game_values.playercontrol[3], net.active() ? 1 : 0);
                step = 1000;
                t = 0;
                return MENU_CODE_START_GAME;
        }
        return MENU_CODE_NONE;
    }

    //--at-role cpulobby: Start's lobby with no players of this machine's and four CPUs, then their match
    if (at.shotsRole == "cpulobby" && step == 1) {
        rollback::NetGame& net = rollback::NetGame::instance();
        switch (t) {
            case 1: return MENU_CODE_TO_LOCAL_LOBBY;
            case 20:
                if (net.lobby()) {
                    net.lobby()->setLocalPlayers(0);
                    net.lobby()->setCpuCount(4);
                }
                break;
            case 40: shot("c1_lobby_cpus_only"); break;
            case 45: return MENU_CODE_ONLINE_MATCH_SETTINGS;
            case 60: return MENU_CODE_MATCH_SELECTION_START;
            case 75:
                printf("shots: CPUs only: %d teams, players %d %d %d %d\n", g_sim.score_cnt, game_values.playercontrol[0],
                    game_values.playercontrol[1], game_values.playercontrol[2], game_values.playercontrol[3]);
                step = 1000;
                t = 0;
                return MENU_CODE_START_GAME;
        }
        return MENU_CODE_NONE;
    }

    //--at-role syslink: the LAN games (system link) menu, searching and then with what it found, then
    //A (scripts/menu-screenshots.sh runs hosts on the network for it: it joins the second; with no
    //hosts, it hosts a game from the menu)
    if (at.shotsRole == "syslink" && step == 1) {
        if (t == 1)
            return MENU_CODE_TO_SYSTEM_LINK_MENU;
        if (t == 15)
            shot("s1_searching");
        if (t == 260)
            shot("s2_found");
        if (t == 275)
            game_values.playerInput.outputControls[0].menu_down.fPressed = true;
        if (t == 290)
            shot("s3_second_game");
        //(and A: it joins that game, or with none found, the Host button hosts one)
        if (t == 300)
            game_values.playerInput.outputControls[0].menu_select.fPressed = true;
        if (t == 480) {
            shot("s4_lobby");
            step = 1000;
            t = 0;
        }
        return MENU_CODE_NONE;
    }

    //--at-role osk (with --at-pad 1): a text field's on-screen keyboard (Join by address's): opened
    //with A, two letters typed, B for done
    if (at.shotsRole == "osk" && step == 1) {
        COutputControl& keys = game_values.playerInput.outputControls[0];
        switch (t) {
            case 1: return MENU_CODE_TO_ONLINE_MENU;
            case 10: keys.menu_select.fPressed = true; break;
            case 20: shot("k1_keyboard"); break;
            case 24: keys.menu_select.fPressed = true; break;    //(q)
            case 28: keys.menu_down.fPressed = true; break;
            case 32: keys.menu_right.fPressed = true; break;
            case 36: keys.menu_select.fPressed = true; break;    //(s)
            case 46: shot("k2_typed"); break;
            case 50: keys.menu_cancel.fPressed = true; break;
            case 60: shot("k3_done"); step = 1000; t = 0; break;
        }
        return MENU_CODE_NONE;
    }

    //--at-role mouse: the menus with the mouse (SDL's events, as a real one sends): pointing, clicking,
    //the wheel and the right button
    if (at.shotsRole == "mouse" && step == 1) {
        auto move = [](float x, float y) {
            SDL_Event e {};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = x;
            e.motion.y = y;
            SDL_PushEvent(&e);
        };
        auto click = [](float x, float y, Uint8 button) {
            SDL_Event e {};
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = button;
            e.button.down = true;
            e.button.x = x;
            e.button.y = y;
            SDL_PushEvent(&e);
        };
        auto wheel = [](float dy) {
            SDL_Event e {};
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.y = dy;
            SDL_PushEvent(&e);
        };
        switch (t) {
            case 5: move(220, 350); break;                          //(main menu: Options)
            case 15: shot("p1_hover_options"); break;
            case 20: click(220, 350, SDL_BUTTON_LEFT); break;
            case 35: move(450, 211); break;                         //(Weapon Use Limits)
            case 45: shot("p2_options_hover_grid"); break;
            case 50: click(200, 95, SDL_BUTTON_LEFT); break;        //(Gameplay)
            case 65: move(300, 120); break;                         //(a value row)
            case 70: wheel(1); break;
            case 75: wheel(1); break;
            case 85: shot("p3_gameplay_wheel"); break;
            case 90: click(300, 120, SDL_BUTTON_LEFT); break;
            case 100: shot("p4_gameplay_clicked"); break;
            case 105: click(300, 120, SDL_BUTTON_RIGHT); break;
            case 120: shot("p5_back_to_options"); break;
            case 125: click(300, 120, SDL_BUTTON_RIGHT); break;
            case 140: move(400, 270); break;                        //(the main menu's players)
            case 145: click(416, 270, SDL_BUTTON_LEFT); break;      //(player 3: the next kind)
            case 160: shot("p6_main_players_clicked"); step = 1000; t = 0; break;
        }
        return MENU_CODE_NONE;
    }

    //--at-role recplay: a quick game, then its replay played in the same run (memory used by the match
    //before: a replay must play the same as in a fresh run)
    //(recplay2: two quick games, then the second's replay: its recording made after another match too)
    if (at.shotsRole == "recplay" || at.shotsRole == "recplay2") {
        switch (step) {
            case 1:
                next();
                if (at.shotsRole == "recplay2")
                    step = 10;
                return MENU_CODE_QUICK_GAME_START;
            case 10:
                //(back from the first match: the second)
                if (t == 10) {
                    step = 2;
                    t = 0;
                    return MENU_CODE_QUICK_GAME_START;
                }
                return MENU_CODE_NONE;
            case 2:
                //(back in the menus after the match)
                if (t == 1)
                    return MENU_CODE_TO_HISTORY_MENU;
                if (t == 20) {
                    next();
                    return MENU_CODE_HISTORY_PLAY_REPLAY;
                }
                return MENU_CODE_NONE;
            case 3:
                if (mCurrentMenu == mMatchHistoryMenu.get() && t > 5) {
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 120, "the end of the replay");
                return MENU_CODE_NONE;
        }
    }

    //--at-role go: the main menu's Go! (a random game right away; --at-shot-every for its screens)
    if (at.shotsRole == "go" && step == 1) {
        step = 1000;
        t = 0;
        return MENU_CODE_QUICK_GAME_START;
    }

    //--at-role menutour: a screenshot of each menu screen in turn (for looking over the menus)
    if (at.shotsRole == "menutour" && step == 1) {
        struct Screen { MenuCodeEnum code; const char* name; };
        static const Screen SCREENS[] = {{MENU_CODE_TO_OPTIONS_MENU, "t01_options"},
            {MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU, "t02_gameplay"}, {MENU_CODE_NONE, "t02b_gameplay_down"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_TEAM_OPTIONS_MENU, "t03_team"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_POWERUP_SELECTION_MENU, "t04_item_selection"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_POWERUP_SETTINGS_MENU, "t05_item_settings"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_PROJECTILES_OPTIONS_MENU, "t06_weapons"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_PROJECTILES_LIMITS_MENU, "t07_weapon_limits"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_GRAPHICS_OPTIONS_MENU, "t08_graphics"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_EYECANDY_OPTIONS_MENU, "t09_eyecandy"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_SOUND_OPTIONS_MENU, "t10_sound"}, {MENU_CODE_TO_OPTIONS_MENU, ""},
            {MENU_CODE_TO_ONLINE_OPTIONS_MENU, "t11_online_options"}, {MENU_CODE_TO_MAIN_MENU, ""},
            {MENU_CODE_TO_CONTROLS_MENU, "t12_controls"}, {MENU_CODE_TO_MAIN_MENU, ""},
            {MENU_CODE_TO_HISTORY_MENU, "t13_history"}, {MENU_CODE_TO_MAIN_MENU, ""},
            {MENU_CODE_TO_EDITORS_MENU, "t14_editors"}, {MENU_CODE_TO_MAIN_MENU, ""},
            {MENU_CODE_TO_SYSTEM_LINK_MENU, "t15_join"}, {MENU_CODE_TO_MAIN_MENU, ""},
            {MENU_CODE_TO_LOCAL_LOBBY, "t16_lobby"}, {MENU_CODE_ONLINE_OPTIONS, "t17_lobby_options"},
            {MENU_CODE_ONLINE_OPTIONS_BACK, ""}, {MENU_CODE_ONLINE_MATCH_SETTINGS, "t18_match_type"},
            {MENU_CODE_MATCH_SELECTION_START, "t19_game_settings"}, {MENU_CODE_NONE, "t19b_map_field"}, {MENU_CODE_TO_MODE_SETTINGS_MENU, "t20_mode_settings"},
            {MENU_CODE_BACK_TO_GAME_SETUP_MENU_FROM_MODE_SETTINGS, ""}, {MENU_CODE_TO_MAP_BROWSER_THUMBNAILS, "t21_map_thumbnails"}};
        constexpr int HOLD = 20;
        const int index = (t - 1) / HOLD;
        const int frame = (t - 1) % HOLD;
        if (index >= static_cast<int>(sizeof(SCREENS) / sizeof(SCREENS[0]))) {
            step = 1000;
            t = 0;
            return MENU_CODE_NONE;
        }
        if (frame == 0 && SCREENS[index].code != MENU_CODE_NONE)
            return SCREENS[index].code;
        //(no menu code: down three rows on the same screen)
        if (SCREENS[index].code == MENU_CODE_NONE && frame < 6 && frame % 2 == 0)
            game_values.playerInput.outputControls[0].menu_down.fPressed = true;
        if (frame == HOLD - 2 && SCREENS[index].name[0])
            shot(SCREENS[index].name);
        return MENU_CODE_NONE;
    }

    //--at-role pages: the options screens, from Gameplay on, turned with Page Down (as RB) all the
    //way round, a screenshot of each
    if (at.shotsRole == "pages" && step == 1) {
        constexpr int HOLD = 20;
        constexpr int PAGES = 10;
        const int index = (t - 1) / HOLD;
        const int frame = (t - 1) % HOLD;
        if (index >= PAGES) {
            step = 1000;
            t = 0;
            return MENU_CODE_NONE;
        }
        if (frame == 0 && index == 0)
            return MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU;
        if (frame == 0)
            menupad::noteKey(SDLK_PAGEDOWN);
        if (frame == HOLD - 2) {
            char name[32];
            snprintf(name, sizeof(name), "pg%02d", index);
            g_pendingShot = name;
        }
        return MENU_CODE_NONE;
    }

    //--at-role menuperf (with --at-perf 1): each menu screen in turn, its drawing measured over 240
    //ticks ("perf:" lines, the map: menu:<screen>)
    if (at.shotsRole == "menuperf" && step == 1) {
        struct Screen { MenuCodeEnum code; const char* name; };
        static const Screen SCREENS[] = {{MENU_CODE_NONE, "main"}, {MENU_CODE_TO_OPTIONS_MENU, "options"},
            {MENU_CODE_TO_ONLINE_OPTIONS_MENU, "online_options"}, {MENU_CODE_TO_GRAPHICS_OPTIONS_MENU, "graphics_options"},
            {MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU, "gameplay_options"}, {MENU_CODE_TO_POWERUP_SELECTION_MENU, "item_selection"},
            {MENU_CODE_TO_MAIN_MENU, "main_again"}, {MENU_CODE_TO_SYSTEM_LINK_MENU, "games_list"},
            {MENU_CODE_TO_ONLINE_MENU, "join_by_address"}, {MENU_CODE_TO_MAIN_MENU, "main_3"},
            {MENU_CODE_TO_HISTORY_MENU, "history"}, {MENU_CODE_TO_MAIN_MENU, "main_4"},
            {MENU_CODE_TO_CONTROLS_MENU, "controls"}, {MENU_CODE_TO_MAIN_MENU, "main_5"},
            {MENU_CODE_TO_LOCAL_LOBBY, "lobby"}, {MENU_CODE_ONLINE_OPTIONS, "lobby_options"},
            {MENU_CODE_ONLINE_OPTIONS_BACK, "lobby_cpus"}, {MENU_CODE_ONLINE_MATCH_SETTINGS, "match_type"},
            {MENU_CODE_MATCH_SELECTION_START, "game_settings"}};
        constexpr int HOLD = 260;
        const int index = (t - 1) / HOLD;
        const int at = (t - 1) % HOLD;
        if (index >= static_cast<int>(sizeof(SCREENS) / sizeof(SCREENS[0]))) {
            step = 1000;
            t = 0;
            return MENU_CODE_NONE;
        }
        if (at == 0)
            return SCREENS[index].code;
        if (at == 1 && SCREENS[index].code == MENU_CODE_ONLINE_OPTIONS && rollback::NetGame::instance().lobby())
            rollback::NetGame::instance().lobby()->setCpuCount(2);
        if (at == 20)
            perfstats::reset();
        if (at == HOLD - 1)
            perfstats::report(std::string("menu:") + SCREENS[index].name, -1);
#if defined(__linux__)
        //(work/perf/blitwho counts while BLITWHO_ON is set: MENUPERF_SCREEN=<name> counts that screen alone)
        if (const char* only = getenv("MENUPERF_SCREEN")) {
            if (at == 20 && std::string(only) == SCREENS[index].name)
                setenv("BLITWHO_ON", "1", 1);
            if (at == HOLD - 1)
                unsetenv("BLITWHO_ON");
        }
#endif
        return MENU_CODE_NONE;
    }

    //--at-role options: the options menu, and its Online page
    if (at.shotsRole == "options" && step == 1) {
        switch (t) {
            case 1: return MENU_CODE_TO_OPTIONS_MENU;
            case 20: shot("p1_options_menu"); break;
            case 25: return MENU_CODE_TO_ONLINE_OPTIONS_MENU;
            case 45:
                shot("p2_online_options");
                step = 1000;
                t = 0;
                break;
        }
        return MENU_CODE_NONE;
    }

    //--at-role graphics: the graphics options menu
    if (at.shotsRole == "graphics" && step == 1) {
        if (t == 1)
            return MENU_CODE_BACK_TO_GRAPHIC_OPTIONS_MENU;
        if (t == 20) {
            shot("g1_graphics_menu");
            step = 1000;
            t = 0;
        }
        return MENU_CODE_NONE;
    }

    //--at-role modeopts: every game mode's settings menu (and the boss minigame's), the d-pad
    //going down through all of it
    if (at.shotsRole == "modeopts" && step == 1) {
        const int mode = (t - 1) / 40;
        const int phase = (t - 1) % 40;
        if (mode > GAMEMODE_LAST) {
            step = 1000;
            t = 0;
            return MENU_CODE_NONE;
        }
        if (phase == 0) {
            mCurrentMenu = mode < GAMEMODE_LAST ? mModeOptionsMenu->GetOptionsMenu(mode) : mModeOptionsMenu->GetBossOptionsMenu();
            mCurrentMenu->ResetMenu();
        }
        if (phase >= 3 && phase % 3 == 0)
            game_values.playerInput.outputControls[0].menu_down.fPressed = true;
        if (phase >= 2 && phase % 3 == 2) {
            char name[32];
            snprintf(name, sizeof(name), "m%02d_%02d", mode, phase / 3);
            g_pendingShot = name;
        }
        return MENU_CODE_NONE;
    }

    //--at-role optmenus: the options menus, the online menu, the team selection
    if (at.shotsRole == "optmenus" && step == 1) {
        static const MenuCodeEnum MENUS[] = {MENU_CODE_TO_OPTIONS_MENU, MENU_CODE_TO_GAMEPLAY_OPTIONS_MENU, MENU_CODE_TO_TEAM_OPTIONS_MENU,
            MENU_CODE_TO_POWERUP_SELECTION_MENU, MENU_CODE_TO_POWERUP_SETTINGS_MENU, MENU_CODE_TO_PROJECTILES_OPTIONS_MENU,
            MENU_CODE_TO_PROJECTILES_LIMITS_MENU, MENU_CODE_TO_GRAPHICS_OPTIONS_MENU, MENU_CODE_TO_EYECANDY_OPTIONS_MENU,
            MENU_CODE_TO_SOUND_OPTIONS_MENU, MENU_CODE_TO_CONTROLS_MENU, MENU_CODE_TO_ONLINE_MENU, MENU_CODE_TO_MATCH_SELECTION_MENU};
        const int k = (t - 1) / 20;
        if (k >= static_cast<int>(sizeof(MENUS) / sizeof(MENUS[0]))) {
            step = 1000;
            t = 0;
            return MENU_CODE_NONE;
        }
        if ((t - 1) % 20 == 0)
            return MENUS[k];
        if ((t - 1) % 20 == 15) {
            char name[32];
            snprintf(name, sizeof(name), "o%02d", k);
            g_pendingShot = name;
        }
        return MENU_CODE_NONE;
    }

    //--at-role history: the match history screen and a replay of its newest match
    if (at.shotsRole == "history" || at.shotsRole == "theater") {
        switch (step) {
            case 1:
                if (t == 1)
                    return MENU_CODE_TO_HISTORY_MENU;
                if (t == 20)
                    shot("h1_history");
                //(later: long names partway and all the way through their scrolling)
                if (t == 700)
                    shot("h1b_history_scrolling");
                if (t == 1100)
                    shot("h1c_history_scrolled");
                if (t == 1104)
                    next();
                return MENU_CODE_NONE;
            case 2:
                if (t == 1) {
                    next();
                    return MENU_CODE_HISTORY_PLAY_REPLAY;
                }
                return MENU_CODE_NONE;
            case 3:
                //(the replay runs in the gameplay state; back here afterwards)
                if (mCurrentMenu == mMatchHistoryMenu.get() && t > 5) {
                    shot("h3_history_after_replay");
                    step = 1000;
                    t = 0;
                }
                timedOut(60 * 120, "the end of the replay");
                return MENU_CODE_NONE;
        }
    }

    if (step == 1) {
        if (t == 1) {
            game_values.onlinename = nick;
            if (at.shotsRole == "join")
                game_values.onlineaddress = at.shotsAddr;
            if (at.shotsRole == "host" && !at.shotsAddr.empty())
                game_values.onlineport = at.shotsAddr;
            return MENU_CODE_TO_ONLINE_MENU;
        }
        if (t == 15)
            shot(at.shotsRole == "join" ? "13_online_menu_join_filled" : "02_online_menu");
        if (t == 18)
            next();
        return MENU_CODE_NONE;
    }

    if (at.shotsRole == "fuzz") {
        //Random keys in the lobby of a host (with a second process joining it, if there is one): must not crash
        static std::mt19937 rng(777);
        if (step == 2) {
            if (t == 1)
                return MENU_CODE_ONLINE_HOST;
            if (lobby && lobby->phase() == Phase::Lobby && t > 20)
                next();
            timedOut(60 * 20, "the lobby");
            return MENU_CODE_NONE;
        }
        if (step == 3 && lobby && lobby->phase() != Phase::Closed && mCurrentMenu != mOnlineMenu.get()) {
            COutputControl& keys = game_values.playerInput.outputControls[rng() % 2];
            if (rng() % 3 == 0) {
                switch (rng() % 8) {
                    case 0: keys.menu_up.fPressed = true; break;
                    case 1: keys.menu_down.fPressed = true; break;
                    case 2: keys.menu_left.fPressed = true; break;
                    case 3: keys.menu_right.fPressed = true; break;
                    case 4:
                    case 5: keys.menu_select.fPressed = true; break;
                    case 6: keys.menu_random.fPressed = true; break;
                    default: if (rng() % 4 == 0) keys.menu_cancel.fPressed = true; break;
                }
            }
            if (t % 600 == 0 && lobby)
                lobby->setCpuCount(static_cast<int>(rng() % 4));
            return MENU_CODE_NONE;
        }
        if (step == 3 && t > 30)
            return exitSoon();
        return MENU_CODE_NONE;
    }

    if (at.shotsRole == "orig") {
        //The original offline screens the lobby borrows from: match selection, team select, game settings
        switch (step) {
            case 2:
                if (t == 1)
                    return MENU_CODE_TO_MATCH_SELECTION_MENU;
                if (t == 20)
                    shot("o1_match_selection");
                if (t == 23)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    for (int k = 0; k < 4; k++)
                        game_values.playercontrol[k] = k < 2 ? 1 : 2;
                    return MENU_CODE_MATCH_SELECTION_START;
                }
                if (t == 30)
                    shot("o2_team_select");
                if (t == 33)
                    next();
                return MENU_CODE_NONE;
            case 4:
                if (t == 1) {
                    game_values.matchtype = MatchType::SingleGame;
                    const short mode = static_cast<short>(at.mode >= 0 ? at.mode : 0);
                    mGameSettingsMenu->GameModeChanged(mode);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = mode;
                    return MENU_CODE_TO_GAME_SETUP_MENU;
                }
                if (t == 30)
                    shot("o3_game_settings");
                if (t == 33)
                    next();
                return MENU_CODE_NONE;
            default:
                return exitSoon();
        }
    }

    if (at.shotsRole == "errors") {
        //The messages of the Online menu: nothing entered, a bad address, a port in use (--at-addr), nobody there
        switch (step) {
            case 2:
                if (t == 1) {
                    game_values.onlineaddress.clear();
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 8)
                    shot("20_error_no_address");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 3:
                if (t == 1) {
                    game_values.onlineaddress = "127.0.0.1:99999";
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 8)
                    shot("21_error_bad_address");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 4:
                if (t == 1) {
                    game_values.onlineport = at.shotsAddr;
                    return MENU_CODE_ONLINE_HOST;
                }
                if (t == 8)
                    shot("22_error_port_in_use");
                if (t == 11)
                    next();
                return MENU_CODE_NONE;
            case 5:
                if (t == 1) {
                    game_values.onlineaddress = "127.0.0.1:1";
                    return MENU_CODE_ONLINE_JOIN;
                }
                if (t == 6)
                    shot("23_joining");
                if (mCurrentMenu == mOnlineMenu.get() && t > 10) {
                    next();
                    return MENU_CODE_NONE;
                }
                timedOut(60 * 40, "the connection to fail");
                return MENU_CODE_NONE;
            case 6:
                if (t == 8)
                    shot("24_error_cannot_connect");
                if (t == 12)
                    return exitSoon();
                return MENU_CODE_NONE;
            default:
                return exitSoon();
        }
    }

    //(the keys pressed here are read by the menu right after this)
    COutputControl& keys0 = game_values.playerInput.outputControls[0];

    if (at.shotsRole == "host") {
        switch (step) {
            case 2: {
                if (t == 1)
                    return MENU_CODE_ONLINE_HOST;
                if (mCurrentMenu == mOnlineMenu.get() && t > 20) {
                    shot("zz_host_failed");
                    step = 1000;
                    t = 0;
                    return MENU_CODE_NONE;
                }
                //(once the router has answered, if UPnP is on: the line says how friends join)
                bool routerAnswered = true;
                if (lobby) {
                    bool good = false;
                    routerAnswered = rollback::NetGame::instance().routerNote(good).rfind("Opening", 0) != 0 || t > 5000;
                }
                if (lobby && lobby->phase() == Phase::Lobby && t > 20 && routerAnswered) {
                    shot("03_team_host_alone");
                    next();
                }
                return MENU_CODE_NONE;
            }

            case 3:    //a joiner and a CPU player
                if (lobby && lobby->peer(1).present && lobby->pingsMeasured() && lobby->peer(1).localPlayers == 2 && t > 30) {
                    lobby->addCpu();
                    next();
                }
                timedOut(60 * 90, "a joiner");
                return MENU_CODE_NONE;

            case 4:    //the joiner is not ready yet
                if (t == 40)
                    shot("04_team_host_joiner_and_cpu");
                if (t == 44) {
                    //The host is ready too (the first player's select key): the buttons come into focus
                    keys0.menu_select.fPressed = true;
                }
                if (t > 50 && lobby && lobby->peer(1).ready)
                    next();
                timedOut(60 * 60, "the joiner to be ready");
                return MENU_CODE_NONE;

            case 5:
                //Everyone on a team of their own
                if (t == 2 && lobby) {
                    for (int slot = 0; slot < 4; slot++)
                        lobby->setSlotTeam(slot, slot);
                }
                if (t == 10)
                    shot("05_team_host_everyone_ready");
                if (t == 12) {
                    //The options page
                    return MENU_CODE_ONLINE_OPTIONS;
                }
                if (t == 24)
                    shot("05b_options_host");
                if (t == 26)
                    return MENU_CODE_ONLINE_OPTIONS_BACK;
                if (t == 30)
                    next();
                return MENU_CODE_NONE;

            case 6:    //the match menus
                if (t == 1)
                    return MENU_CODE_ONLINE_MATCH_SETTINGS;
                if (t == 20)
                    shot("06_match_type_menu");
                if (t == 23)
                    next();
                return MENU_CODE_NONE;

            case 7:
                if (t == 1)
                    return MENU_CODE_MATCH_SELECTION_START;
                if (t == 2 && lobby)
                    lobby->setInputDelay(3);    //(the joiner takes this for its cue)
                if (t == 60)
                    shot("07_host_game_settings");
                if (t == 63)
                    next();
                return MENU_CODE_NONE;

            case 8:    //the joiner looks at the settings, then may change them
                if (t == 300 && lobby)
                    lobby->setSettingsEveryone(true);
                if (t == 440 && lobby)
                    printf("shots: the host's goal after the joiner's change: %d (%s)\n", lobby->goal(), lobby->goalName().c_str());
                if (t == 480) {
                    //pick mode, goal and map
                    const short mode = static_cast<short>((at.mode >= 0 ? at.mode : 0) % GAMEMODE_LAST);
                    mGameSettingsMenu->GameModeChanged(mode);
                    game_values.gamemode = gamemodes[mGameSettingsMenu->GetCurrentGameModeID()];
                    currentgamemode = mode;
                    SModeOption* options = game_values.gamemode->GetOptions();
                    game_values.gamemode->goal = options[1].iValue;
                    mGameSettingsMenu->miGoalField[mode]->setCurrentValue(game_values.gamemode->goal);
                    if (!at.map.empty()) {
                        maplist->findexact(at.map.c_str(), false);
                        mGameSettingsMenu->miMapField->LoadCurrentMap();
                        szCurrentMapName = mGameSettingsMenu->miMapField->GetMapName();
                    }
                }
                if (t == 560)
                    shot("08_host_game_settings_map_picked");
                if (t == 700)
                    next();
                return MENU_CODE_NONE;

            case 9: {
                //Everyone on a team of their own (once the joiner has made its choices)
                if (t == 1 && lobby) {
                    for (int slot = 0; slot < 4; slot++)
                        lobby->setSlotTeam(slot, slot);
                }
                if (t < 10)
                    return MENU_CODE_NONE;
                std::string why;
                if (lobby && lobby->canStart(why)) {
                    next();
                    return MENU_CODE_START_GAME;
                }
                timedOut(60 * 20, "canStart");
                return MENU_CODE_NONE;
            }

            case 10:    //the match starts (the map is sent), is played, and ends
                if (lobby && lobby->mapTransfer().active && !shotTaken) {
                    shotTaken = true;
                    shot("09_host_map_upload");
                }
                if (g_autoTestMatchesDone >= 1 && lobby && lobby->phase() == Phase::Lobby && mCurrentMenu == mOnlineLobbyMenu.get() && t > 40) {
                    shot("10_team_host_after_match");
                    next();
                }
                timedOut(60 * 60, "the match to end");
                return MENU_CODE_NONE;

            case 11:
                if (t == 4)
                    return MENU_CODE_ONLINE_LEAVE;
                if (t == 12)
                    shot("11_leave_dialog_host");
                if (t == 16)
                    return MENU_CODE_ONLINE_LEAVE_YES;
                if (t == 40)
                    return exitSoon();
                return MENU_CODE_NONE;

            default:
                return exitSoon();
        }
    }

    //The joiner
    switch (step) {
        case 2:
            if (t == 1)
                return MENU_CODE_ONLINE_JOIN;
            if (t == 4 && lobby && lobby->phase() == Phase::Joining)
                shot("14_joining");
            if (lobby && lobby->phase() == Phase::Lobby && t > 10) {
                lobby->setLocalPlayers(2);
                next();
            }
            if (mCurrentMenu == mOnlineMenu.get() && t > 20) {
                shot("zz_join_failed");
                step = 1000;
                t = 0;
            }
            timedOut(60 * 40, "the lobby");
            return MENU_CODE_NONE;

        case 3: {
            bool cpu = false;
            for (int s = 0; lobby && s < rollback::LOBBY_SLOTS; s++)
                cpu |= lobby->slot(s).kind == rollback::SlotKind::Cpu;
            if (cpu && t > 40) {
                shot("15_team_joiner_others_locked");
                next();
            }
            timedOut(60 * 90, "the host's CPU player");
            return MENU_CODE_NONE;
        }

        case 4:    //the joiner picks another character (up/down), then is ready (select)
            if (t >= 4 && t <= 40 && (t % 4) == 0)
                keys0.menu_down.fPressed = true;    //(nine skins further)
            if (t == 4 + 36 + 4)
                keys0.menu_right.fPressed = true;   //(and another team)
            if (t == 70)
                shot("15b_team_joiner_changes_skin");
            if (t == 80)
                keys0.menu_select.fPressed = true;
            if (t == 120) {
                shot("16_team_joiner_ready");
                next();
            }
            return MENU_CODE_NONE;

        case 5:    //the host opens the settings (and sets the input delay as a sign): a look at them
            if (lobby && !lobby->inputDelayIsAuto() && lobby->inputDelay() == 3) {
                next();
                return MENU_CODE_ONLINE_VIEW_SETTINGS;
            }
            timedOut(60 * 90, "the host's settings");
            return MENU_CODE_NONE;

        case 6:
            if (t == 80)
                shot("17_joiner_settings_host_only");
            if (lobby && lobby->settingsEveryone() && t > 90) {
                next();
            }
            timedOut(60 * 60, "the host to let everyone change the settings");
            return MENU_CODE_NONE;

        case 7:    //everyone may change them: the joiner changes the goal, which the host takes over
            if (t == 20)
                shot("18_joiner_settings_everyone");
            //(the focus is on Back: two downs reach the goal field, select and right raise it)
            if (t == 30 || t == 34)
                keys0.menu_down.fPressed = true;
            if (t == 38 || t == 46)
                keys0.menu_select.fPressed = true;
            if (t == 42)
                keys0.menu_right.fPressed = true;
            if (t == 70)
                shot("18b_joiner_settings_edited_goal");
            if (!at.map.empty() && lobby && lobby->mapName() == at.map && t > 30) {
                next();
            }
            timedOut(60 * 60, "the host's map");
            return MENU_CODE_NONE;

        case 8: {    //the map is not here: the start downloads it
            if (t == 40)
                shot("19_joiner_settings_map_missing");
            const rollback::MapTransferInfo tr = lobby ? lobby->mapTransfer() : rollback::MapTransferInfo {};
            if (tr.active && !shotTaken && tr.bytesDone > 0) {
                shotTaken = true;
                shot("20_joiner_map_download");
            }
            if (g_autoTestMatchesDone >= 1 && lobby && lobby->phase() == Phase::Lobby && mCurrentMenu == mOnlineLobbyMenu.get() && t > 40) {
                shot("21_team_joiner_after_match");
                next();
            }
            timedOut(60 * 60, "the match to end");
            return MENU_CODE_NONE;
        }

        case 9:    //the host leaves: the lobby is closed
            if (mCurrentMenu == mOnlineMenu.get() && t > 12) {
                shot("22_online_menu_lobby_closed");
                next();
            }
            timedOut(60 * 60, "the host to close the lobby");
            return MENU_CODE_NONE;

        default:
            return t > 6 ? exitSoon() : MENU_CODE_NONE;
    }
}

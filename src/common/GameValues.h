#ifndef GAMEVALUES_H
#define GAMEVALUES_H

constexpr const char* TITLESTRING = "Super Mario War";

#include "EyecandyStyles.h"
#include "GameModeSettings.h"
#include "GameplayStyles.h"
#include "input.h"
#include "MatchTypes.h"

#include <array>
#include <string>
#include <vector>

class sfxSound;
class CGameMode;
struct TourStop;

void ifSoundOnPlay(sfxSound&);

short defaultPowerupSetting(size_t presetIdx, size_t powerupIdx);

enum class AppState: unsigned char {
    Splash,
    Menu,
    StartGame,
    StartWorld,
    EndGame,
    Game,
    Quit,
};

//tournament scores
struct TournamentScores {
    short       wins;
    short       type[MAXTOURNAMENTGAMES];
    short       total;      //used for running total in a tour
};

/// Gameplay-specific variables, which are reset on the start of a game.
struct GameplayFlags {
    bool pausegame = false;
    bool exitinggame = false;
    bool exityes = false;

    bool noexit = false;
    short noexittimer = 0;
    short forceexittimer = 0;

    short teamdeadcounter = 0;

    short screenshaketimer = 0;
    short screenshakeplayerid = -1;
    short screenshaketeamid = -1;
    short screenshakekillscount = 0;
    bool screenshakekillinair = false;

    short slowdownon = -1;
    short slowdowncounter = 0;

    bool showscoreboard = false;
    float scorepercentmove = 0.0f;

    bool playskidsound = false;
    bool playinvinciblesound = false;
    bool playflyingsound = false;

    bool swapplayers = false;
    float swapplayersposition = 0.0f;
    bool swapplayersblink = false;
    short swapplayersblinkcount = 0;

    float gamewindx = 0.0f;
    float gamewindy = 0.0f;

    bool windaffectsplayers = false;
    bool spinscreen = false;
    bool reversewalk = false;
    bool spotlights = false;
};


// How often the game renders, stored in CGameConfig::framelimiter (a byte in
// the old options.bin). The simulation always ticks every 16 ms (62.5 Hz).
// Any other value (e.g. an old frame delay) means VSync.
enum class FrameRateMode : short {
    VSync = 200,        // render once per display refresh
    Uncapped = 201,     // render as often as possible
    TickRate = 202,     // render once per tick (62.5 FPS), the lightest
};

inline FrameRateMode GetFrameRateMode(short framelimiter)
{
    switch (static_cast<FrameRateMode>(framelimiter)) {
        case FrameRateMode::Uncapped:
        case FrameRateMode::TickRate:
            return static_cast<FrameRateMode>(framelimiter);
        default:
            return FrameRateMode::VSync;
    }
}

// the real configuration class
class CGameConfig {
public:
    short		soundvolume;
    short		musicvolume;
    bool		fullscreen;
    SpawnStyle spawnstyle;
    AwardStyle awardstyle;
    TeamCollisionStyle teamcollision;
    short		shellttl;
    short		blueblockttl;
    short		redblockttl;
    short		grayblockttl;
    short		bombslimit;
    BonusWheelStyle bonuswheel;
    short		fireballttl;
    short		fireballlimit;
    BoomerangStyle boomerangstyle;
    short		boomeranglife;
    short		boomeranglimit;
    short		cpudifficulty;
    WarpLockStyle warplockstyle;
    short		warplocktime;
    short		hammerttl;
    short		hammerdelay;
    short		hammerlimit;
    bool		hammerpower;
    short		featherjumps;
    short		featherlimit;
    short		suicidetime;
    bool		deadteamnotice;
    short		framelimiter;   // how rendering is paced, see FrameRateMode
    short		hiddenblockrespawn;
    CInputPlayerControl inputConfiguration[4][2]; //[NumPlayers][Keyboard/Joystick]
    short		itemrespawntime;
    bool		keeppowerup;
    short		leaflimit;
    short		pwingslimit;
    short		tanookilimit;
    short		playercontrol[4];
    CPlayerInput playerInput;
    bool		playnextmusic;  //automatically advance to the next music track after one finishes
    short		outofboundstime;
    PowerupDropRateStyle overridepowerupsettings;
    bool		secretsenabled;
    GameModeSettings gamemodemenusettings;
    bool		music;
    short		pointspeed;
    short poweruppreset;
    std::array<std::array<short, NUM_POWERUPS>, NUM_POWERUP_PRESETS> allPowerupPresets;
    bool		randomskin[4];
    ScoreboardStyle scoreboardstyle;
    bool		screencrunch;
    short		shieldtime;
    ShieldStyle shieldstyle;
    bool		showwinningcrown;
	short		skinids[4];
    short		wandlimit;
    TournamentControlStyle tournamentcontrolstyle;  //ID for the player selected control style
    short		wandfreezetime;
    bool		toplayer;
    bool		smoothmotion = true;    // draw moving things between ticks (interpolated)
    short		runahead = 1;           // run-ahead: show the match this many ticks ahead (0..3), less input lag
    bool		shownetstats = false;   // online: the network info line (ping, input delay, rollbacks)
    bool		showpingbars = true;    // online and in online replays: signal bars on the other players' score panels
    short		overlaycolors = 0;      // the theater's and the editors' colors: 0 night, 1 pipe (green), 2 castle, 3 SMB3 (the status bar's blue)
    short		buttonprompts = 0;      // the controller glyphs in the theater and the editors: 0 auto, 1 Xbox, 2 original Xbox
    bool		teamcolors;
    bool		sound;
    bool		startgamecountdown;
    bool		startmodedisplay;
    short		storedpowerupdelay;
    MysteryMushroomSwapStyle swapstyle;
    short		respawn;
    // Online menu: remembered between runs
    std::string	onlinename;      // nickname in the lobby
    std::string	onlineaddress;   // last "ip[:port]" joined
    std::string	onlineport = "7300";   // port to host on

    void ReadConfig();
    void WriteConfig() const;

private:
    void ReadInputConfig();
    void ReadBinaryConfig();
    void ReadTomlConfig();

    void WriteInputConfig() const;
    void WriteTomlConfig() const;
};

// these are actually the program options
class CGameValues : public CGameConfig
{
public:
    void init();

    void resetGameplaySettings();
    void resetSecretCounters();

    bool showfps;
    bool		frameadvance;
    bool		autokill;

    AppState appstate = AppState::Splash;

    GameplayFlags flags;
    CGameMode   *gamemode;

    short screenfade = 0;
    short screenfadespeed = 0;

    MatchType matchtype;	//The currently selected match type: quick game, single, tournament, tour, world, minigame

    short		teamids[4][3];
    short		teamcounts[4];

    short		tournamentgames;	//How many games that are played in this tournament
    short		tournamentwinner;     //-2 for a tied tournament (for tours), -1 for no winner yet, 0 or greater for the team that has won the tournament

    short		tournamentcontrolteam;   //The team ID that currently has control
    short		tournamentnextcontrol;  //For round robin control style

    Minigame selectedminigame;

    short		tourindex;
    size_t		tourstopcurrent;
    std::vector<TourStop*> tourstops;

    short		worldindex;

    short		storedpowerups[4];
    short		gamepowerups[4];
    short		powerupweights[NUM_POWERUPS];

    short		worldpowerups[4][32];
    short		worldpowerupcount[4];

    short		worldpointsbonus;

    short		colorids[4];

    short		bulletbilltimer[4];
    short		bulletbillspawntimer[4];

    size_t		loadedannouncer;
    short		loadedmusic;

    short		cputurn;


    //Player input used during game.  Reads SDL_Events and sets buttons that were pressed

    TournamentScores tournament_scores[4];

    GameModeSettings gamemodesettings;

    bool		soundcapable;

    std::vector<bool> pfFilters;
    std::vector<short> piFilterIcons;
    short		selectedmapfilter;
    bool		fNeedWriteFilters;
    bool		fFiltersOn;

    short		singleplayermode;

    bool		worldskipscoreboard;

    bool		windaffectsplayers;
    bool		spinscreen;
    bool		reversewalk;
    bool		spotlights;

    bool		unlocksecret1part1[4];
    short		unlocksecret1part2;
    bool		unlocksecret2part1;
    short		unlocksecret2part2;
    short		unlocksecret3part1[4];
    short		unlocksecret3part2[4];
    bool		unlocksecretunlocked[4];
};

// Defined in global.cpp
extern CGameValues game_values;

// Defined in GameValues.cpp
extern SDL_Keycode controlkeys[2][2][4][NUM_KEYS];

#endif // GAMEVALUES_H

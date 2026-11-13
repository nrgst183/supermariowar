#include "GSMenu.h"

#include "CmdArgs.h"
#include "FileList.h"
#include "Game.h"
#include "GameMode.h"
#include "GameValues.h"
#include "gfx.h"
#include "GSGameplay.h"
#include "GameplayStyles.h"
#include "joystick.h"
#include "net.h"
#include "map.h"
#include "MapList.h"
#include "path.h"
#include "RandomNumberGenerator.h"
#include "ResourceManager.h"
#include "Score.h"
#include "SimulationState.h"
#include "gamemodes/BonusHouse.h"
#include "gamemodes/MiniBoss.h"
#include "gamemodes/MiniBoxes.h"
#include "gamemodes/MiniPipe.h"
#include "ui/MI_BonusWheel.h"
#include "ui/MI_MapBrowser.h"
#include "ui/MI_MapField.h"
#include "ui/MI_MapFilterScroll.h"
#include "ui/MI_SelectField.h"
#include "ui/MI_TournamentScoreboard.h"
#include "ui/MI_TourStop.h"
#include "ui/MI_World.h"
#include "Version.h"
#include "world.h"
#include "WorldTourStop.h"
#include "world/WorldMapIO.h"

#include "menu/BonusWheelMenu.h"
#include "menu/GameSettingsMenu.h"
#include "menu/MainMenu.h"
#include "menu/MapFilterEditMenu.h"
#include "menu/MatchSelectionMenu.h"
#include "menu/ModeOptionsMenu.h"
#include "menu/OptionsMenu.h"
#include "menu/PlayerControlsMenu.h"
#include "menu/PlayerControlsSelectMenu.h"
#include "menu/TeamSelectMenu.h"
#include "menu/TournamentScoreboardMenu.h"
#include "menu/TourStopMenu.h"
#include "menu/WorldMenu.h"

#include "menu/options/EyeCandyOptionsMenu.h"
#include "menu/options/GameplayOptionsMenu.h"
#include "menu/options/GraphicsOptionsMenu.h"
#include "menu/options/PowerupDropRatesMenu.h"
#include "menu/options/PowerupSettingsMenu.h"
#include "menu/options/ProjectileLimitsMenu.h"
#include "menu/options/ProjectileOptionsMenu.h"
#include "menu/options/SoundOptionsMenu.h"
#include "menu/options/TeamOptionsMenu.h"

#include "menu/network/NetEditServersMenu.h"
#include "menu/network/NetLobbyMenu.h"
#include "menu/network/NetNewRoomMenu.h"
#include "menu/network/NetNewRoomSettingsMenu.h"
#include "menu/network/NetRoomMenu.h"
#include "menu/network/NetServersMenu.h"

#include <format>
#include <string>
#include <cstdlib> // atoi()
#include <random>
#include <cassert>
#include <sstream>

bool MenuState::ReadTourFile()
{
    ResetTourStops();

    FILE * fp = fopen(tourlist->at(game_values.tourindex).string().c_str(), "r");
    const char* const ignorable_leads = " #\n\r\t";

    char buffer[256];
    bool fReadVersion = false;
    Version version;
    while (fgets(buffer, 256, fp) && game_values.tourstops.size() < 10) {
        if (strchr(ignorable_leads, buffer[0]))
            continue;

        if (!fReadVersion) {
            fReadVersion = true;

            char * psz = strtok(buffer, ".\n");
            if (psz)
                version.major = atoi(psz);

            psz = strtok(nullptr, ".\n");
            if (psz)
                version.minor = atoi(psz);

            psz = strtok(nullptr, ".\n");
            if (psz)
                version.patch = atoi(psz);

            psz = strtok(nullptr, ".\n");
            if (psz)
                version.build = atoi(psz);

            continue;
        }

        TourStop* ts = new TourStop();
        *ts = ParseTourStopLine(buffer, version, false);
        game_values.tourstops.push_back(ts);
    }

    if (game_values.tourstops.size() != 0) {
        mTourStopMenu->miTourStop->Refresh(game_values.tourstopcurrent);

        //For old tours, turn on the bonus wheel at the end
        if (version.major == 1 && version.minor == 7 && version.patch == 0 && version.build <= 1)
            game_values.tourstops[game_values.tourstops.size() - 1]->iBonusType = 1;
    }

    fclose(fp);

    return game_values.tourstops.size() != 0;
}

void MenuState::StartGame()
{
    printf("> StartGame\n");
#ifdef _DEBUG
    iScriptState = 2;
    fScriptRunPreGameOptions = false;
#endif

    game_values.appstate = AppState::StartGame;
    //backgroundonly = false;
    //fastmap = false;

    game_values.WriteConfig();

    //Load skins for players
    printf("Loading player skins...\n");
    for (int k = 0; k < 4; k++) {
        if (game_values.playercontrol[k] > 0) {
            if (netplay.active) {
                if (k == netplay.remotePlayerNumber) { // local player uses local skin
                    printf("  player %d -> local\n", k);
                    rm->spr_player[k] = rm->LoadFullSkin(game_values.skinids[k], game_values.colorids[k]);
                }
                else {
                    std::ostringstream path;
                    path << GetHomeDirectory() << "net_skin" << k << ".bmp";
                    printf("  player %d -> %s\n", k, path.str().c_str());

                    try {
                        rm->spr_player[k] = rm->LoadFullSkin(path.str(), k);
                    } catch (...) {
                        printf("[warning] Could not load netplay skin of player %d, using default\n", k);
                        rm->spr_player[k] = rm->LoadFullSkin(game_values.skinids[k], game_values.colorids[k]);
                    }
                }
            }
            else if (game_values.randomskin[k]) {
                game_values.skinids[k] = RANDOM_INT( skinlist->count());
                rm->spr_player[k] = rm->LoadFullSkin(game_values.skinids[k], game_values.colorids[k]);
            }
            else {
                rm->spr_player[k] = rm->LoadFullSkin(game_values.skinids[k], game_values.colorids[k]);
            }
        }
    }

    //Load announcer sounds if changed
    if (game_values.loadedannouncer != announcerlist->currentIndex()) {
        game_values.loadedannouncer = announcerlist->currentIndex();

        //Delete the old sounds
        for (int k = 0; k < PANNOUNCER_SOUND_LAST; k++)
            rm->sfx_announcer[k] = {};

        FILE * announcerfile = fopen(announcerlist->currentPath().string().c_str(), "r");

        char szBuffer[256];

        int announcerIndex = 0;

        while (fgets(szBuffer, 256, announcerfile) && announcerIndex < PANNOUNCER_SOUND_LAST) {
            //Ignore comment lines
            if (szBuffer[0] == '#' || szBuffer[0] == ' ' || szBuffer[0] == '\t' || szBuffer[0] == '\n' || szBuffer[0] == '\r')
                continue;

            //Clean off carriage returns
            for (int k = 0; k < static_cast<int>(strlen(szBuffer)); k++) {
                if (szBuffer[k] == '\r' || szBuffer[k] == '\n') {
                    szBuffer[k] = '\0';
                    break;
                }
            }

            //If it is not "[none]", then add this announcer sound
            if (strcmp(szBuffer, "[none]"))
                rm->sfx_announcer[announcerIndex] = sfxSound(convertPath(szBuffer));

            announcerIndex++;
        }

        fclose(announcerfile);
    }

    //Load soundtrack music if changed
    if (game_values.loadedmusic != static_cast<short>(musiclist->currentIndex())) {
        game_values.loadedmusic = static_cast<short>(musiclist->currentIndex());
        rm->backgroundmusic[1] = sfxMusic(musiclist->music(0)); //Stage Clear
        rm->backgroundmusic[3] = sfxMusic(musiclist->music(2)); //Tournament Menu
        rm->backgroundmusic[4] = sfxMusic(musiclist->music(3)); //Tournament Over
    }

    rm->backgroundmusic[2].stop();
    rm->sfx_announcer[11].play();

    game_values.screenfade = 8;
    game_values.screenfadespeed = 8;
    printf("< StartGame\n");
}

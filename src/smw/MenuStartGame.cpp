#include "GSMenu.h"

#include "rollback/MatchConfig.h"
#include "rollback/NetGame.h"

#include "FileList.h"
#include "GameValues.h"
#include "path.h"
#include "ResourceManager.h"
#include "SimulationState.h"
#include "ui/MI_TourStop.h"
#include "Version.h"
#include "WorldTourStop.h"

#include "menu/TourStopMenu.h"

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
    //Online, in a tournament, tour or world, only the host starts the matches (its config
    //starts them here); the world map's stage start is just the fade here
    const rollback::NetGame& net = rollback::NetGame::instance();
    if (net.seriesActive() && !net.isHost()) {
        game_values.appstate = AppState::StartGame;
        game_values.screenfade = 8;
        game_values.screenfadespeed = 8;
        return;
    }

    printf("> StartGame\n");
#ifdef _DEBUG
    iScriptState = 2;
    fScriptRunPreGameOptions = false;
#endif

    game_values.appstate = AppState::StartGame;

#ifdef SMW_HAS_EDITORS
    if (!editors::testPlaying())    //(a test play's players and mode aren't the settings)
#endif
        game_values.WriteConfig();

    //Load skins for players
    printf("Loading player skins...\n");
    for (int k = 0; k < 4; k++) {
        if (game_values.playercontrol[k] > 0) {
            if (game_values.randomskin[k]) {
                game_values.skinids[k] = RANDOM_INT( skinlist->count());
                rm->spr_player[k] = rm->LoadFullSkin(game_values.skinids[k], game_values.colorids[k]);
            }
            else {
                //(a computer player: its random skin, the one the lobby showed)
                rm->spr_player[k] = rm->LoadFullSkin(rollback::matchSkinId(k), game_values.colorids[k]);
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

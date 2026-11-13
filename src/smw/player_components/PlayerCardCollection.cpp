#include "PlayerCardCollection.h"
#include "PlayerKeyMask.h"

#include "GameValues.h"
#include "player.h"
#include "gamemodes/CardCollection.h"


void PlayerCardCollection::reset() {
    timer = 0;
    index = 0;
}

void PlayerCardCollection::update(CPlayer& player, const uint8_t keymask)
{
    if (game_values.gamemode->gamemode == game_mode_collection) {
        if (index > 0) {
            if (++timer >= 32) {
                index = 0;
                timer = 0;
            }
        }

        if (keymask & PlayerKeyMask::Turbo) {
            index++;
        } else if (keymask & ~PlayerKeyMask::Turbo) {
            index = 0;
            timer = 0;
        }

        if (index == 3 && timer < 32) {
            timer = 0;
            index = 0;

            (static_cast<CGM_Collection*>(game_values.gamemode))->ReleaseCard(player);
        }
    }
}

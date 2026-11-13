#include "PlayerSecretCode.h"
#include "PlayerKeyMask.h"

#include "objectgame.h"
#include "player.h"

using namespace PlayerKeyMask;
static const uint8_t SECRET_CODE[18] = {Left, Right, Left, Right, Jump, Jump, Left, Right, Left, Right, Jump, Jump, Left, Right, Left, Right, Jump, Jump};
static const unsigned short SECRET_CODE_TIME_MAX = 186;

PlayerSecretCode::PlayerSecretCode() {
    reset();
}

void PlayerSecretCode::reset() {
    timer = 0;
    index = 0;
}

void PlayerSecretCode::update(CPlayer& player, const uint8_t keymask) {
    if (!player.isready())
        return;

    if (index > 0) {
        if (++timer >= SECRET_CODE_TIME_MAX) {
            index = 0;
            timer = 0;
        }
    }

    if (keymask & SECRET_CODE[index]) {
        index++;
    } else if (keymask & ~SECRET_CODE[index]) {
        index = 0;
        timer = 0;
    }

    if (index == 18 && timer < SECRET_CODE_TIME_MAX) {
        index = 0;
        timer = 0;
        CheckSecret(3);
    }
}

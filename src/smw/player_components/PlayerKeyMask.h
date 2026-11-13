#pragma once

// Bit flags of the per-frame input mask handed to the player components.
namespace PlayerKeyMask {
constexpr int Jump = 1;
constexpr int Down = 2;
constexpr int Left = 4;
constexpr int Right = 8;
constexpr int Turbo = 16;
constexpr int Powerup = 32;
}  // namespace PlayerKeyMask

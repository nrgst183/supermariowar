#pragma once

#include <cstdint>
#include <string>
#include <vector>

// THE STATE OF A SERIES BETWEEN MATCHES (tournament, tour, world; see
// NetSeries.h): what every peer must agree on before the next match.
//
// Serialized (little-endian): u16 version, i16 tournament games, i16
// tournament winner, u32 tour stop, i16 single player mode, i16 world points
// bonus, u8 skip scoreboard, 4 x (i16 wins, 10 x i16 type, i16 total),
// 4 x i16 stored powerup, 4 x i16 game powerup, 4 x i16 world item count,
// 4 x 32 x i16 world items, u64 worldMapHash()
// The match config carries the host's copy (MatchConfig::series); the peers
// compare it with theirs (seriesStateHash) and start the match from it.
namespace rollback {

// From game_values (and the world map, as a hash)
std::vector<uint8_t> captureSeriesState();
// Into game_values (the world map hash is only compared). False if malformed.
bool applySeriesState(const std::vector<uint8_t>& bytes);
// The hash of a captured state (fnv1a64 of the bytes)
uint64_t seriesStateHash(const std::vector<uint8_t>& bytes);
// The hash of the world map's state that the world map menu changes: the
// player's position, the stages (completed, revived), the doors and
// bridges, the vehicles (0 without a world)
uint64_t worldMapHash();
// The standings, for logs: "winner 1 wins 2,1 totals 0,0"
std::string seriesStandings();
// For the lobby: what game_values.matchtype, tournamentgames, tourindex and
// worldindex select ("Tournament, 3 wins", "Tour"; the tour or world name)
std::string seriesMatchName();
std::string seriesFileName();

} // namespace rollback

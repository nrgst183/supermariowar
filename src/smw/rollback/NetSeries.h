#pragma once

#include "NetProtocol.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// MULTI-MATCH MODES ONLINE: TOURNAMENT, TOUR AND WORLD (a "series")
//
// The matches of a series are ordinary online matches (CONFIG, START, the
// rollback session; NetGame.h), and every peer leaves each of them with the
// same confirmed final state, so the results (tournament scores, tour
// points, won items) are computed identically everywhere. What happens
// between the matches runs in the menus, and there the host is the
// authority:
//
//  - SERIES_BEGIN: the host starts the series: match type, wins needed, the
//    tour or world file (every peer loads its own copy, checked by hash),
//    the players and teams, the settings, a seed. Every peer sets the
//    series up from it the same way (MenuState::SetupOnlineSeries).
//  - SERIES_SCREEN: whenever the host's menu changes (scoreboard, bonus
//    wheel, tour stop, world map, or "choosing": the host picks the next
//    game), it says so; the other peers show the same screen and ignore
//    their own menu input, except where a player of theirs has a say (the
//    bonus wheel of their team, their turn on the world map).
//  - Bonus wheel: every peer shows the same wheel (it is set up after
//    reseeding the gameplay random generator with the series seed and the
//    screen's step). Only the winning team may stop it: the host for its
//    own players, CPU players or after the timeout, the others ask the host
//    with WHEEL_PRESS. The host stops its wheel and sends WHEEL_STOP with
//    the wheel's state at that moment; the others continue from that state,
//    so every wheel lands on the same powerup.
//  - World map: a lockstep, relayed by the host (WorldLockstep below). Every
//    peer sends the menu keys of its players for each tick to the host
//    (WORLD_INPUT); the host collects them and sends every tick, with the
//    keys of every slot, to everybody (WORLD_TICK); every peer (the host
//    too) runs the world map one tick per WORLD_TICK, in order. The world
//    map code is deterministic given its input (the random numbers come
//    from the gameplay generator, seeded the same everywhere), so every peer
//    computes the same moves, item uses and stage starts.
//  - The next match's CONFIG carries the series state that the host has
//    (SeriesState.h): each peer compares it with its own (a mismatch is
//    reported as a desync) and plays from the host's copy.
//  - SERIES_END: the series is over (finished, or the host left it): back
//    to the lobby.
//
// A peer that is lost between matches: its players become CPU players from
// the next match (and, on the world map, from the next tick the host sends).
// Without the host the lobby is closed, which ends the series.
//
// PROTOCOL (reliable; after the u8 type):
//   SERIES_BEGIN   u8 match type, i16 tournament games, str file, u64 file
//                  hash, u32 seed, 4 x (i8 peer, u8 control, i16 color),
//                  i16 team count, 4 x 3 x i16 team members, 4 x i16 team
//                  sizes, u16 size + the simulation settings (MatchConfig.h)
//   SERIES_SCREEN  u8 screen, u16 step (counts the host's screens; the world
//                  map's epoch), u8 human slot mask
//   SERIES_END     str reason ("" when the series finished)
//   WHEEL_PRESS    u16 step                       (joiner -> host)
//   WHEEL_STOP     u16 step, f32 angle, f32 speed, f32 wind-down speed,
//                  i16 powerup
//   WORLD_INPUT    u16 epoch, u32 tick, u8 slot mask, 4 x u8 keys (joiner -> host)
//   WORLD_TICK     u16 epoch, u32 tick, u8 human slot mask, 4 x u8 keys
// The keys are the held menu keys of a player, bit k = COutputControl::keys[k].
namespace rollback {

constexpr int SERIES_SLOTS = 4;

enum class SeriesScreen : uint8_t {
    None,
    Choosing,      // the host chooses the next game (game settings menus)
    Scoreboard,
    BonusWheel,
    TourStop,
    World,
};
const char* seriesScreenName(SeriesScreen screen);

struct SeriesBeginMsg {
    uint8_t matchType = 0;         // MatchType
    int16_t tournamentGames = 0;
    std::string file;              // the tour or world file below the data directory ("" for a tournament)
    uint64_t fileHash = 0;
    uint32_t seed = 0;
    std::array<int8_t, SERIES_SLOTS> slotPeer {{-1, -1, -1, -1}};    // the peer of each human slot
    std::array<uint8_t, SERIES_SLOTS> control {};                    // game_values.playercontrol
    std::array<int16_t, SERIES_SLOTS> color {};
    int16_t teamCount = 0;
    int16_t teamIds[4][3] = {};
    int16_t teamCounts[4] = {};
    std::vector<uint8_t> settings;
};

struct SeriesScreenMsg {
    SeriesScreen screen = SeriesScreen::None;
    uint16_t step = 0;
    uint8_t humanMask = 0;    // the slots of human players that are still there
};

struct WheelStopMsg {
    uint16_t step = 0;
    float angle = 0;
    float speed = 0;
    float winddown = 0;
    int16_t powerup = 0;
};

struct WorldInputMsg {
    uint16_t epoch = 0;
    uint32_t tick = 0;
    uint8_t slotMask = 0;
    std::array<uint8_t, SERIES_SLOTS> keys {};
};

struct WorldTickMsg {
    uint16_t epoch = 0;
    uint32_t tick = 0;
    uint8_t humanMask = 0;
    std::array<uint8_t, SERIES_SLOTS> keys {};
};

// Each write puts the type byte first; each read starts after it (the
// reader of a received message: ByteReader(data + 1, size - 1)) and is
// false if the message is malformed
void writeSeriesBegin(net::ByteWriter& w, const SeriesBeginMsg& m);
bool readSeriesBegin(net::ByteReader& r, SeriesBeginMsg& m);
void writeSeriesScreen(net::ByteWriter& w, const SeriesScreenMsg& m);
bool readSeriesScreen(net::ByteReader& r, SeriesScreenMsg& m);
void writeSeriesEnd(net::ByteWriter& w, const std::string& reason);
bool readSeriesEnd(net::ByteReader& r, std::string& reason);
void writeWheelPress(net::ByteWriter& w, uint16_t step);
bool readWheelPress(net::ByteReader& r, uint16_t& step);
void writeWheelStop(net::ByteWriter& w, const WheelStopMsg& m);
bool readWheelStop(net::ByteReader& r, WheelStopMsg& m);
void writeWorldInput(net::ByteWriter& w, const WorldInputMsg& m);
bool readWorldInput(net::ByteReader& r, WorldInputMsg& m);
void writeWorldTick(net::ByteWriter& w, const WorldTickMsg& m);
bool readWorldTick(net::ByteReader& r, WorldTickMsg& m);

// THE PLAYERS OF A SERIES: who drives each slot, and the teams
struct SeriesSlots {
    std::array<int8_t, SERIES_SLOTS> peer {{-1, -1, -1, -1}};    // human slots: their peer, else -1
    std::array<uint8_t, SERIES_SLOTS> control {};                // 0 empty, 1 human, 2 CPU
    int16_t teamCount = 0;
    int16_t teamIds[4][3] = {};
    int16_t teamCounts[4] = {};

    // Bit s: slot s is a human player of this peer
    uint8_t slotsOf(int peerId) const;
    // Bit s: slot s is a human player (of a peer that is still there)
    uint8_t humanMask() const;
    // A lost peer: its players become CPU players. False if it had none.
    bool dropPeer(int peerId);
    // A player of this peer is in this team
    bool peerInTeam(int peerId, int team) const;
    // The team has a human player
    bool teamHasHuman(int team) const;
};

// THE WORLD MAP LOCKSTEP, relayed by the host (see above). One per world map
// screen (an "epoch"); the game feeds the local keys once per frame and runs
// a tick of the world map for every tick next() returns.
//
// The input window: a peer sends its keys for at most `window` ticks that it
// hasn't run yet, which bounds its input lag; the host can only run a tick
// once the keys of every peer for it are there, so a joiner's window must
// cover its round trip to the host (or the map runs slower than the frames).
class WorldLockstep {
public:
    void begin(uint16_t epoch, bool host, int localPeer, const SeriesSlots& slots, int window);
    void end() { m_active = false; }
    bool active() const { return m_active; }
    uint16_t epoch() const { return m_epoch; }
    // The ticks run here so far
    uint32_t ticksRun() const { return m_nextTick; }

    // The keys of our players (by slot; others are ignored) for our next
    // tick, once per frame. While the window is full they are kept (presses
    // are not lost) and false is returned. A joiner then has `toHost` to send.
    bool addLocal(const std::array<uint8_t, SERIES_SLOTS>& keys, WorldInputMsg* toHost);

    // Host: a joiner's keys
    void onInput(int peer, const WorldInputMsg& m);
    // Host: a peer is gone: its players are CPU players from the next tick
    void peerLost(int peer);
    // Joiner: a tick from the host
    void onTick(const WorldTickMsg& m);

    // The next tick to run here, if it is ready. The host must send it to
    // every joiner (it is the tick, as everyone runs it).
    bool next(WorldTickMsg& tick);
    // Joiner: ticks that arrived and weren't run yet
    size_t pending() const { return m_ticks.size(); }

private:
    bool m_active = false;
    bool m_host = false;
    int m_localPeer = 0;
    uint16_t m_epoch = 0;
    int m_window = 8;
    SeriesSlots m_slots;
    uint8_t m_humanMask = 0;
    uint32_t m_nextTick = 0;     // the next tick to run
    uint32_t m_added = 0;        // our keys were added for the ticks below this
    std::array<uint8_t, SERIES_SLOTS> m_held {};    // keys kept while the window was full
    // Host: the keys of each peer for the ticks from m_nextTick on
    std::array<std::deque<std::array<uint8_t, SERIES_SLOTS>>, net::MAX_PEERS> m_inputs;
    // Joiner: the ticks received and not run yet
    std::deque<WorldTickMsg> m_ticks;
    uint32_t m_received = 0;
};

} // namespace rollback

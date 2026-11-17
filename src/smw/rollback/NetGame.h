#pragma once

#include "InputFrame.h"
#include "MatchConfig.h"
#include "NetLobby.h"
#include "NetSeries.h"
#include "RollbackSession.h"
#include "Snapshot.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace cmd { struct NetArgs; }

namespace rollback {

// The rollback netplay inside the game: owns the connection (the transport
// and the lobby, NetLobby.h) and the session of the running match
// (RollbackSession), and connects them to the game. The menus and overlays
// use it as described by the MENU CONTRACT in NetLobby.h.
//
// SESSION LIFE: host() or join() -> the lobby (NetLobby) -> per match: the
// host captures the match its menus set up (MatchConfig) with the lobby's
// slots and sends it, every peer checks it (and downloads the map if it lacks
// it), the host sends START and every peer starts the match at frame 0 -> the
// match ends at a confirmed frame -> back to the lobby. A peer that leaves or
// times out in the middle of a match doesn't end it: the others agree on the
// frame from which the CPU plays its players and go on (LOST PEERS in
// RollbackSession.h; the overlay shows dropNotice()). It is out of the lobby
// (the host removes it). If the host is the one that is gone, the match goes
// on just the same, and the lobby is closed when it ends: there is no host
// migration (the host is the lobby's authority and the address the others
// joined; handing that over isn't worth its complexity for now).
//
// ONE FRAME ONLINE (GameplayState::update -> advance()):
//   receive everything that arrived -> RollbackSession::advanceFrame(): roll
//   back and re-simulate if a prediction was wrong, then run this frame's tick
//   (or wait), send the inputs -> flush the socket.
// The session calls back here to save/load snapshots (a ring of Snapshot,
// saveState/loadState), to run ticks (GameplayState::advanceTick, inside
// sim::ResimulationScope when re-simulating) and for checksums
// (peerChecksum).
//
// MATCH END (the irreversible part): after each tick, matchEndRequested()
// tells the session whether the new state asks to leave the match (the
// screen faded out for the end of the game, five seconds after game over,
// the --autotest frame limit). The session never runs past such a frame
// and only reports it once it is confirmed, so every peer leaves at the
// same frame, with the same final state (see RollbackSession.h).
// Who can join a lobby: nobody (a local game: no network at all), the local network (system
// link: found by the others' LAN games lists), or anyone (and the router's port is opened, UPnP)
enum class LobbyAccess : uint8_t { Nobody, Lan, Internet };

class NetGame : public ISessionCallbacks {
public:
    static NetGame& instance();

    // ---- lobby (menus; see the MENU CONTRACT in NetLobby.h)
    // Opens a lobby on a UDP port (0: DEFAULT_LOBBY_PORT), open to `access`. False with a message
    // on failure. (Nobody: no network is used, in any build: the lobby of a local game.)
    bool host(uint16_t port, std::string& error, LobbyAccess access = LobbyAccess::Internet);
    // Host: who can join now (opens or closes the network). Nobody only while nobody else is in.
    // False and why if it can't (the port is in use, ...).
    bool setAccess(LobbyAccess access, std::string& error);
    LobbyAccess access() const { return m_access; }
    // Whether another machine is in the lobby
    bool hasOthers() const;
    // Joins a lobby: "ip:port", "name:port", or just the address (DEFAULT_LOBBY_PORT)
    bool join(const std::string& address, std::string& error);
    // Leaves the lobby (the host closes it for everyone); in a match, leaves the match too
    void leave();
    // A lobby is open or being joined
    bool active() const;
    // The lobby ended (lobby()->statusText() says why), until the next host()/join()
    bool finished() const;
    bool isHost() const;
    // The lobby (null if none was opened)
    NetLobby* lobby() { return m_lobby.get(); }
    // The host's router (UPnP, PortMapping.h): one line for the lobby screen ("" when nothing to
    // say); `good` tells whether friends can likely reach us
    std::string routerNote(bool& good) const;
    // This computer's address on its network (what the others on it join), e.g. "192.168.1.20";
    // empty without a network (or a network build)
    static std::string lanAddress();
    // The UDP port this computer's lobby is on (0: none)
    uint16_t lobbyPort() const;
    const NetLobby* lobby() const { return m_lobby.get(); }
    // Host: starts a match with the mode, map and settings the menus have
    // selected and the lobby's slots. It starts by itself once every peer
    // accepted it (the game then switches to the gameplay state); false with
    // a message if it can't start.
    bool startMatch(std::string& error);

    // ---- multi-match modes: tournament, tour, world (NetSeries.h; the
    // menus' part is MenuOnlineSeries.cpp)
    struct SeriesEvent {
        enum class Type : uint8_t { None, Begin, Screen, WheelPress, WheelStop, End };
        Type type = Type::None;
        int peer = -1;
        SeriesBeginMsg begin;      // Begin
        SeriesScreenMsg screen;    // Screen
        WheelStopMsg stop;         // WheelStop
        uint16_t step = 0;         // WheelPress
        std::string text;          // End: why ("" if it finished)
    };
    // A series runs (from SERIES_BEGIN until it ended)
    bool seriesActive() const { return m_series.active; }
    const SeriesSlots& seriesSlots() const { return m_series.slots; }
    uint32_t seriesSeed() const { return m_series.seed; }
    // Host: starts a series of game_values.matchtype with the lobby's
    // players; `begin` gets the rest (the file, its hash) from the caller.
    // Fills in the players, the settings and the seed, and sends it.
    bool beginSeries(SeriesBeginMsg& begin, std::string& error);
    // Every peer, once the menus have set the series up from `begin`
    void seriesSetUp(const SeriesBeginMsg& begin);
    // The series is over: the host tells everybody (reason "" if it
    // finished). Prints the final standings.
    void endSeries(const std::string& reason);
    bool pollSeriesEvent(SeriesEvent& event);
    // Host: the screen it shows now
    void sendSeriesScreen(const SeriesScreenMsg& screen);
    // Joiner: asks the host to stop the bonus wheel; host: the stopped wheel
    void sendWheelPress(uint16_t step);
    void sendWheelStop(const WheelStopMsg& stop);
    // The world map lockstep: a new screen (epoch), with these human slots
    void worldBegin(uint16_t epoch, uint8_t humanMask);
    void worldEnd() { m_world.end(); }
    bool worldActive() const { return m_world.active(); }
    // Our players' keys for this frame (by slot); true with the next tick
    // to run, if one is ready (the host sends it to the others)
    bool worldStep(const std::array<uint8_t, SERIES_SLOTS>& localKeys, WorldTickMsg& tick);
    size_t worldPending() const { return m_world.pending(); }
    uint32_t worldTicksRun() const { return m_world.ticksRun(); }
    // After a series match: its results are in game_values (prints modesum)
    void seriesResultsApplied();
    // Host: the players of peers that left the lobby become CPU players
    void seriesSyncPeers();
    // The series ended and its last screen is done: prints the final state (modesum)
    void printSeriesFinal();

    // Host: a joiner's request to change the match settings (only while the host allows it, see
    // NetLobby::settingsEveryone()); the menus apply it to their widgets, last one wins
    struct SettingRequest {
        int peer = -1;
        int what = 0;     // NetLobby::SETTING_*
        int value = 0;
        std::string text;
    };
    bool popSettingRequest(SettingRequest& request);

    // ---- in a match (overlays)
    struct WaitInfo {
        bool waiting = false;    // the match waits for late inputs
        uint8_t peers = 0;       // bit p: from peer p (name: lobby()->peer(p).name)
        uint32_t waitingMs = 0;  // since when
    };
    WaitInfo waitInfo() const;
    // Round trip to a peer (the session's measure during a match), -1 if unknown
    int peerPingMs(int peer) const;
    // The running match's statistics (rollbacks, stalls...), null outside a match
    const RollbackSession::Stats* matchStats() const { return (m_inMatch && m_session) ? &m_session->stats() : nullptr; }
    // The input delay of the running (or next) match
    int inputDelay() const;
    // Why the last match was aborted ("" if it ended normally)
    const std::string& lastMatchError() const { return m_lastMatchError; }
    // The latest "X disconnected: the CPU takes over" of the running match
    // (empty if none), and how long ago it happened
    struct DropNotice {
        std::string text;
        uint32_t ageMs = 0;
    };
    DropNotice dropNotice() const;

    // ---- main loop
    // From main: hosts or joins if the command line asks for it (once).
    // False on failure (the message is printed).
    bool init(const cmd::NetArgs& args);
    void shutdown();
    // Handles network events; call once per main loop iteration
    void pump();

    // ---- --autotest
    // Host: every peer the command line waits for is in the lobby and ready,
    // and the round trips are measured
    bool readyForMatch() const;
    // Host: closes the lobby (after matchDrained(), so that every peer can finish the last match)
    void endSession(const std::string& reason);
    // Every peer has every input it needs to finish the last match
    bool matchDrained();
    // Everything is sent: the process may exit
    bool canQuit();

    // ---- gameplay
    bool inMatch() const { return m_inMatch; }
    struct Outcome {
        int ticks = 0;
        bool ended = false;     // the match is over (confirmed)
        bool failed = false;    // aborted: a peer left or timed out
    };
    Outcome advance(const InputFrame& localInput);
    // The local player leaves (exit dialog): leaves the lobby too
    void leaveMatch(const char* reason);
    // GameplayState left the match (back to the menus)
    void onMatchLeft();
    bool isLocalSlot(int slot) const;
    // The name of the player of a slot in the current (or last) match: its
    // peer's name at the start, "" for CPU and empty slots
    std::string slotPlayerName(int slot) const;
    int localPeer() const;

    // ISessionCallbacks
    void saveState(int frame) override;
    void loadState(int frame) override;
    bool advance(const InputFrame& input, bool resimulating) override;
    uint64_t checksum() override;
    void takeOverSlots(uint8_t slotMask) override;
    void onConfirmedChecksum(int frame, uint64_t sum) override;
    void onDesync(int frame, int peer, uint64_t local, uint64_t remote) override;

private:
    NetGame() = default;

    bool open(bool asHost, uint16_t port, const std::string& joinHost, uint16_t joinPort, LobbyAccess access, std::string& error);
    std::unique_ptr<IMeshTransport> createNetwork(uint16_t port, std::string& error) const;
    void openRouterPort(uint16_t port);
    void handle(const LobbyGameEvent& event, uint32_t nowMs);
    void handleMessage(int peer, const uint8_t* data, size_t size, uint32_t nowMs);
    void processConfig();
    void recheckConfig();
    void hostUpdateStart(uint32_t nowMs);
    void abortStart(int culprit, const std::string& reason);
    void returnToLobby(const std::string& abortReason);
    void beginMatch();
    void applyLobbySlots(std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot);
    void updateMatchInfo(uint32_t nowMs);
    void updateSkins();
    std::string peerName(int peer) const;
    void printStats(bool final);
    bool matchEndRequested() const;
    uint8_t matchPeers() const;
    void updateLostPeers(uint32_t nowMs);
    void onSessionFailed();
    void handleSeriesMessage(int peer, const uint8_t* data, size_t size);
    void seriesPeerGone(int peer);
    void seriesUpdate();
    void addSeriesEvent(const SeriesEvent& event);
    void applySeriesSlots(std::array<int8_t, INPUT_MAX_PLAYERS>& peerOfSlot);
    void sendSeries(int peer, const net::ByteWriter& w);

    std::unique_ptr<SwitchableTransport> m_transport;
    LobbyAccess m_access = LobbyAccess::Internet;
    uint16_t m_hostPort = 0;   // the port a host's lobby opens (or opened) the network on
    std::unique_ptr<NetLobby> m_lobby;
    std::unique_ptr<RollbackSession> m_session;
    bool m_cliDone = false;
    bool m_inMatch = false;    // from START until the match ended or was aborted

    MatchConfig m_config;
    uint8_t m_matchId = 0;
    int m_matchesStarted = 0;
    // Host: the match being started
    uint8_t m_ackedMask = 0;
    uint32_t m_startMs = 0;
    std::shared_ptr<const std::vector<uint8_t>> m_mapFile;
    // Joiner: the config of the next match
    bool m_pendingConfig = false;    // arrived, not checked yet (e.g. during the end of the last match)
    std::vector<uint8_t> m_configBytes;
    bool m_configAccepted = false;   // acknowledged, waiting for START
    bool m_waitingForMap = false;    // downloading its map

    bool m_wasClosed = false;
    uint32_t m_closeMs = 0;
    uint32_t m_matchEndMs = 0;
    uint32_t m_lastInfoMs = 0;
    std::deque<SettingRequest> m_settingRequests;
    bool m_skinsAnnounced = false;   // our skins were handed to the lobby
    uint64_t m_skinKey = 0;          // the skin ids they were made from (and how many)
    uint32_t m_skinRevision = 0;     // of the lobby: when it changes, a running match reloads the sprites
    std::string m_closeAfterMatch;   // the host closed the lobby during the match
    std::string m_abortText;         // why the running match is being aborted
    std::string m_lastMatchError;

    struct Series {
        bool active = false;
        uint8_t matchType = 0;
        uint32_t seed = 0;
        SeriesSlots slots;
        int matches = 0;                 // matches of the series that ended
        uint64_t localStateHash = 0;     // joiner: its series state when the next config arrived
        bool startRetried = false;
        bool endPending = false;         // joiner: SERIES_END arrived, the world map has ticks to run first
        std::string endReason;
    };
    Series m_series;
    WorldLockstep m_world;
    std::deque<SeriesEvent> m_seriesEvents;

    bool m_waiting = false;
    uint8_t m_waitPeers = 0;
    uint32_t m_waitStartMs = 0;

    // Lost peers of the running match (see updateLostPeers)
    std::array<std::string, net::MAX_PEERS> m_matchNames;    // the names at the start
    uint8_t m_lostSeen = 0;       // the lost peers already handled
    uint8_t m_dropsSeen = 0;      // the lost peers whose drop frame was already announced
    std::string m_dropNotice;
    uint32_t m_dropNoticeMs = 0;

    static constexpr int MAX_PREDICTION = 10;   // frames the match may run ahead of the confirmed inputs
    static constexpr int SNAPSHOT_RING = MAX_PREDICTION + 2;
    std::array<Snapshot, SNAPSHOT_RING> m_snapshots;

    struct Timing {
        double updateSeconds = 0;
        double maxUpdateSeconds = 0;
        int64_t updates = 0;
        double simSeconds = 0;
        int64_t simTicks = 0;
        double saveSeconds = 0;
        int64_t saves = 0;
        double loadSeconds = 0;
        int64_t loads = 0;
        uint32_t startMs = 0;
        int64_t lastBytes = 0;
        uint32_t lastStatsMs = 0;
        int64_t lastRollbacks = 0;
    };
    Timing m_timing;
};

} // namespace rollback

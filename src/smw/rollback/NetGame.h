#pragma once

#include "InputFrame.h"
#include "MatchConfig.h"
#include "NetLobby.h"
#include "RollbackSession.h"
#include "Snapshot.h"

#include <array>
#include <cstdint>
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
// times out in the middle of a match aborts it for everyone: the others go
// back to the lobby without it (without the host, the lobby is over).
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
class NetGame : public ISessionCallbacks {
public:
    static NetGame& instance();

    // ---- lobby (menus; see the MENU CONTRACT in NetLobby.h)
    // Opens a lobby on a UDP port (0: DEFAULT_LOBBY_PORT). False with a message on failure.
    bool host(uint16_t port, std::string& error);
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
    const NetLobby* lobby() const { return m_lobby.get(); }
    // Host: starts a match with the mode, map and settings the menus have
    // selected and the lobby's slots. It starts by itself once every peer
    // accepted it (the game then switches to the gameplay state); false with
    // a message if it can't start.
    bool startMatch(std::string& error);

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
    int localPeer() const;

    // ISessionCallbacks
    void saveState(int frame) override;
    void loadState(int frame) override;
    bool advance(const InputFrame& input, bool resimulating) override;
    uint64_t checksum() override;
    void onConfirmedChecksum(int frame, uint64_t sum) override;
    void onDesync(int frame, int peer, uint64_t local, uint64_t remote) override;

private:
    NetGame() = default;

    bool open(bool asHost, uint16_t port, const std::string& joinHost, uint16_t joinPort, std::string& error);
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
    std::string peerName(int peer) const;
    void printStats(bool final);
    bool matchEndRequested() const;
    uint8_t matchPeers() const;

    std::unique_ptr<IMeshTransport> m_transport;
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
    std::string m_closeAfterMatch;   // the host closed the lobby during the match
    std::string m_abortText;         // why the running match is being aborted
    std::string m_lastMatchError;

    bool m_waiting = false;
    uint8_t m_waitPeers = 0;
    uint32_t m_waitStartMs = 0;

    static constexpr int SNAPSHOT_RING = 10;    // max prediction + 2
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

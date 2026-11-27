#pragma once

#include "NetMesh.h"
#include "NetProtocol.h"
#include "SkinTransfer.h"
#include "Transport.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// THE ONLINE LOBBY: a persistent session in which up to four peers (machines)
// gather, set up and play any number of matches in a row.
//
// =============================================================================
// MENU CONTRACT (for the lobby screens and in-game overlays)
// =============================================================================
// Everything goes through rollback::NetGame::instance() ("net" below,
// NetGame.h) and the lobby it owns, net.lobby() (null while there is none).
// The main loop pumps the network every iteration, in menus too (net.pump()):
// the menus only call the functions below. All of them are cheap; query them
// every frame.
//
// Opening and leaving (NetGame):
//   net.host(port, error)       open a lobby on a UDP port (0: the default 7300)
//   net.join(address, error)    join "ip:port", "host:port" or just "ip" (port 7300)
//   net.leave()                 leave the lobby (the host closes it for everyone)
//   net.active()                a lobby is open (or being joined)
//   net.finished()              it ended: lobby()->statusText() says why, until
//                               the next host()/join()
//
// Lobby state (NetLobby, `lobby` below):
//   lobby.phase()               Joining, Lobby, Starting, InMatch, Closed
//   lobby.isHost(), lobby.localPeer()
//   lobby.peer(p)               p = 0..3 (0 is the host): present, name,
//                               localPlayers, ready, pingMs (round trip to this
//                               machine, -1 until measured; -1 for ourselves)
//   lobby.slot(s)               s = 0..3, the player slots of the next match:
//                               kind (Empty, Human, Cpu), peer and localIndex
//                               (which of that peer's players), team 0..3,
//                               color 0..3. Humans come first, in peer order,
//                               then the CPU players.
//   lobby.modeName(), lobby.mapName()    what the host's menus have selected
//   lobby.modeId(), lobby.goal(), lobby.goalName()    the mode's id (GameModeType; the
//                               minigames' ids are above 1000), its goal and what it counts, for the screen
//   lobby.inputDelay(), lobby.inputDelayIsAuto()
//                               the input delay the next match will use
//   lobby.statusText()          one line for the screen: "Waiting for players",
//                               "Downloading map 40%", why the lobby closed, why
//                               the last match was aborted, ... (empty: nothing)
//   lobby.mapTransfer()         map download/upload progress (bytesDone/bytesTotal)
//   lobby.canStart(why)         host: whether startMatch() would start now
//   lobby.slotSkinPath(s)       the skin file of a human slot's player ("" while it
//                               isn't here yet: draw the default skin); skins are
//                               downloaded from their owners (SkinTransfer.h) and
//                               are cosmetic only. lobby.skinRevision() grows when
//                               one arrives. lobby.setLocalSkins(...) announces ours.
//
// Lobby actions:
//   lobby.setName(name)                     our nickname (default "Player N")
//   lobby.setLocalPlayers(1 or 2)           players on this machine
//   lobby.setReady(bool)                    joiners: ready for the next match
//   lobby.setSlotTeam(slot, team), lobby.setSlotColor(slot, color)
//                                           our own slots; the host: any slot
//   lobby.addCpu(), lobby.removeCpu(slot), lobby.setCpuCount(n)   host
//   lobby.kick(peer)                        host
//   lobby.setInputDelay(n)                  host: 1..8, or -1 for automatic
//   net.startMatch(error)                   host: start a match with the mode,
//                                           map and settings its menus have
//                                           selected and the lobby's slots:
//                                           game_values.matchtype (SingleGame
//                                           or MiniGame), game_values.gamemode
//                                           and its goal, the current map of
//                                           `maplist`, the menu's mode settings
//                                           (gamemodemenusettings) and the
//                                           gameplay settings. The lobby's
//                                           slots replace playercontrol,
//                                           teams and colors.
//
// Events (lobby.pollEvent(event), call until false, e.g. once per frame):
//   PeerJoined/PeerLeft (peer, text: why it left), MatchStarting (the host
//   sent the match; maps may be downloading), MatchStarted (the game switched
//   to the gameplay state by itself), MatchAborted (text: why; while it
//   starts: a peer left, refused or timed out; in a running match: we were
//   dropped. A peer lost mid-match doesn't abort it), ReturnedToLobby (after every match, ended or aborted: the
//   game is back in MenuState and the lobby screen should show), Closed
//   (text: why; e.g. "The host closed the lobby").
//   Joined: we are in (joiners).
// Missing events are harmless: the state queries above are always right.
//
// During a match (NetGame, for the overlays):
//   net.waitInfo()     whether the match waits for late inputs, for which
//                      peers (names from lobby.peer(p).name), for how long
//   net.peerPingMs(p), net.inputDelay()
//   net.lastMatchError()   why the last match was aborted ("" if it wasn't)
// =============================================================================
//
// STATE MACHINE (phase()):
//   Joining --(mesh complete)--> Lobby --(host: CONFIG sent)--> Starting
//   Starting --(every peer has the map and accepted)--> InMatch
//   Starting --(a peer refused, left or timed out)--> Lobby
//   InMatch --(the match ended or was aborted)--> Lobby
//   any --(we left, the host closed the lobby or was lost, we were kicked,
//          joining failed)--> Closed
// The host is the authority over the lobby: joiners send it their requests
// (PEER_INFO, SLOT_REQUEST) and show what it broadcasts (LOBBY_STATE). New
// joiners are refused while a match is starting or running. Ready flags are
// cleared after every match (each joiner clears its own: they count per round).
//
// PROTOCOL (reliable unless marked; strings are u8 length + bytes):
//   PEER_INFO     joiner -> host: str name, u8 local players, u8 ready, u8 round
//                 (the last match id it knows: a ready flag counts for one round),
//                 u64 the map it lacks (the lobby's map's hash when it doesn't have it and its
//                 map downloads are off; 0: none)
//   SLOT_REQUEST  joiner -> host: u8 slot, u8 team, u8 color
//   LOBBY_STATE   host -> all: u8 round, u8 input delay, u8 delay is auto,
//                 str mode, str map, u8 peer mask,
//                 per present peer: str name, u8 local players, u8 ready,
//                 4 x slot: u8 kind, i8 peer, u8 local index, u8 team, u8 color,
//                 u8 the peers that lack the map (bit p)
//   PEER_LEFT     host -> all: u8 peer, str reason (everyone drops that peer)
//   PING          (unreliable) every 250 ms in the lobby, to every peer: u32 send time
//   PONG          (unreliable) u32 the echoed send time
//   RTT_REPORT    joiner -> host, every second: u8 connected peer mask,
//                 u8 n, n x (u8 peer, u16 round trip ms, u16 jitter ms, u8 samples)
//   BYE           u8 kind, str reason. Leave: the sender leaves the lobby (a
//                 running match is aborted). Close: the host closes the lobby
//                 (a running match is finished first). Kick: the host removes
//                 the receiver.
//   CONFIG        host -> all: u8 match id, u16 size, the match config (MatchConfig.h)
//   CONFIG_ACK    u8 match id, u8 status (0 ok, 1 refused, 2 the map is
//                 missing here: send it, 3 the map is missing and map
//                 downloads are off here), str reason
//   MAP_BEGIN     host -> peer: u8 match id, u32 size, u64 hash, str map name
//   MAP_CHUNK     host -> peer: u8 match id, u32 offset, u16 size, the bytes
//                 (at most MAP_CHUNK_BYTES; a few per frame so that the
//                 connection stays responsive)
//   START         host -> all: u8 match id: frame 0 starts now
//   MATCH_ABORT   anyone -> all: u8 match id, u8 the peer at fault, str reason.
//                 Sent in a running match by a peer that can't go on (it was
//                 dropped): the others go on without it. A peer that leaves
//                 or is lost doesn't end the match for the others (LOST PEERS
//                 in RollbackSession.h); while the match runs, the lobby
//                 passes on PEER_DROP (NetProtocol.h) like INPUT, also once
//                 the lobby is closed (the host is gone).
//   LOBBY_EXTRA   host -> all, after every LOBBY_STATE (display only): i16 game mode id (-1: unknown,
//                 -2: a tournament, tour or world), i16 goal, str goal name ("Kills", "Points"...),
//                 u8 everyone may change the settings (the host chooses; see settingsEveryone()),
//                 str the map's reference, u64 its hash (MatchConfig's: for everyone to check they have it)
//   SETTINGS_REQUEST  joiner -> host: u8 what (1 mode, 2 goal, 3 map), i16 number (mode id, goal), str text
//                 (the map's name). Honored only while the host lets everyone change the settings and
//                 the lobby is idle; the host applies it through its menus (a LobbyGameEvent::SettingsRequest)
//                 and the result comes back in LOBBY_STATE/LOBBY_EXTRA. The host is the authority:
//                 the last request wins, a refused one is dropped.
//   SKIN_INFO, SKIN_REQUEST, SKIN_BEGIN, SKIN_CHUNK
//                 between any two peers: the skins of the players (SkinTransfer.h)
//   SERIES_*, WHEEL_*, WORLD_*   the multi-match modes (NetSeries.h), handed
//                 to the game like CONFIG
namespace rollback {

constexpr int LOBBY_SLOTS = 4;
// modeId() of a lobby that edits a map or a world together instead of playing matches (its goal():
// 1 while the host is in the editor), and modeName() what it says
constexpr int MODE_ID_MAP_EDITOR = -10;
constexpr int MODE_ID_WORLD_EDITOR = -11;
inline bool isEditorModeId(int modeId) { return modeId == MODE_ID_MAP_EDITOR || modeId == MODE_ID_WORLD_EDITOR; }
constexpr int MAX_LOCAL_PLAYERS = 4;    // players on one machine (a local game: all four)
constexpr uint16_t DEFAULT_LOBBY_PORT = 7300;
constexpr size_t MAX_NAME_LENGTH = 16;
constexpr uint32_t MAP_MAX_BYTES = 1024 * 1024;
constexpr size_t MAP_CHUNK_BYTES = 1000;

// THE AUTOMATIC INPUT DELAY, from the round trip time of the slowest pair of
// peers and its jitter (mean deviation), both measured in the lobby.
//
// An input pressed on one peer reaches another one-way-latency later; the
// receiver needs it `delay` frames after it was pressed. Whatever the delay
// doesn't cover, the receiver predicts and later corrects with a rollback of
// that many frames; past maxPrediction (8) frames it must stall.
//   late  = round trip / 2 + jitter
// is how late an input arrives in the bad cases: half the round trip (as
// the game measures it: with the wait for the next frame's network poll on
// both sides, which inputs also have) and the typical swing of the one-way
// latency (about the mean deviation of the round trip, which adds two of
// them).
//   delay = clamp(ceil(late / 16 ms) - 3, 1, 6)
// leaves up to 3 frames (48 ms) to prediction: rollbacks that short are
// hardly visible, and the 5 frames of headroom before a stall absorb lost
// packets (the next packet repeats the input, a frame later) and the larger
// swings. The cap of 6 (96 ms) keeps the controls responsive; beyond it
// prediction (and stalls) must take the rest. At least 1: a frame of delay
// is hardly noticeable, and it saves most rollbacks on a LAN.
// (Measured with scripts/netplay-test.sh: 2 frames at 60 ms one way, 5 at
// 100 ms, 6 at 150 ms; there the stalls dropped by ~97% against a fixed 2.)
int autoInputDelay(float rttMs, float jitterMs);

// Round trip time of one connection over its last WINDOW samples: the
// average and the mean deviation, without the slowest quarter of the
// samples (hitches of the menu loop, e.g. loading a map preview, aren't
// the network's; the headroom of the input delay absorbs real spikes)
struct RttEstimate {
    static constexpr int WINDOW = 16;
    float rttMs = 0;
    float jitterMs = 0;
    int samples = 0;    // received so far (at most 255)

    void add(float sampleMs);

private:
    float m_window[WINDOW] = {};
    int m_next = 0;
};

struct LobbyPeer {
    bool present = false;
    std::string name;
    int localPlayers = 1;
    bool bots = false;    // its players are CPU players it drives (--net-bot: its AI's buttons, over the network)
    bool ready = false;
    int pingMs = -1;    // round trip between this machine and the peer (-1: unknown, or ourselves)
    bool lacksMap = false;    // joiners: it doesn't have the lobby's map and won't download it (see peerLacksMap)
    uint64_t lackedMap = 0;   // host: the map it said it lacks (0: none)
};

enum class SlotKind : uint8_t { Empty, Human, Cpu };

struct LobbySlot {
    SlotKind kind = SlotKind::Empty;
    int8_t peer = -1;          // Human: the peer whose player it is
    uint8_t localIndex = 0;    // Human: which of that peer's players
    bool bot = false;          // Human: its peer's AI plays it (--net-bot): a CPU player in the match, driven by that peer
    uint8_t team = 0;          // 0..3
    uint8_t color = 0;         // 0..3
};

struct MapTransferInfo {
    bool active = false;
    bool sending = false;      // the host uploads (to every peer that lacks the map)
    uint32_t bytesDone = 0;
    uint32_t bytesTotal = 0;
    std::string mapName;
};

struct LobbyEvent {
    enum class Type : uint8_t {
        None,
        Joined,
        PeerJoined,
        PeerLeft,
        MatchStarting,
        MatchStarted,
        MatchAborted,
        ReturnedToLobby,
        Closed,
    };
    Type type = Type::None;
    int peer = -1;
    std::string text;
};

// The messages NetLobby hands to the game (NetGame): the match setup and the
// session messages, and what happened to the lobby that concerns a match.
struct LobbyGameEvent {
    enum class Type : uint8_t {
        None,
        Message,       // Config, ConfigAck, Start, MatchAbort, Input or Checksum (peer, data, size)
        PeerGone,      // a peer left the lobby or lost its connection (peer, text)
        MapReceived,   // a map download finished (ok; text: the file, or the error)
        Closed,        // the lobby is over (text; ok: a running match may finish first)
        SettingsRequest, // host: a joiner asks to change the match settings (peer; `what` in size: 1 mode, 2 goal, 3 map;
                         // `value`: the mode id or goal; text: the map name). Only sent when everyone may change them.
    };
    Type type = Type::None;
    int peer = -1;
    bool ok = false;
    std::string text;
    int value = 0;
    const uint8_t* data = nullptr;    // valid until the next poll()
    size_t size = 0;
};

class NetLobby {
public:
    enum class Phase : uint8_t { Joining, Lobby, Starting, InMatch, Closed };

    struct Options {
        NetMesh::Options mesh;
        std::string name;               // empty: "Player N"
        int localPlayers = 1;
        bool bots = false;              // (testing, --net-bot) this peer's players are CPU players it drives
        // Where downloaded maps go: the first one that can be written
        std::vector<std::string> downloadDirs;
        // Skins: the folders whose skin files are hashed (to see which skins we have), and where
        // downloaded skins go (the first one that can be written)
        std::vector<std::string> skinSearchDirs;
        std::vector<std::string> skinDownloadDirs;
        // Testing: the host sends one map chunk every N frames (1: at full speed)
        int mapSlowdown = 1;
    };

    // The transport must outlive the lobby
    NetLobby(IMeshTransport& transport, const Options& options, uint32_t nowMs);

    // Handles the network; hands out what the game must act on. Call until false.
    bool poll(LobbyGameEvent& event, uint32_t nowMs);
    // Sends what is due (pings, state, map chunks). Once per pump, after poll().
    void update(uint32_t nowMs);

    // ---- queries (see the contract above)
    Phase phase() const { return m_phase; }
    bool isHost() const { return m_mesh.isHost(); }
    int localPeer() const { return m_mesh.localPeer(); }
    const LobbyPeer& peer(int p) const { return m_peers[static_cast<size_t>(p & 3)]; }
    const LobbySlot& slot(int s) const { return m_slots[static_cast<size_t>(s & 3)]; }
    int memberCount() const;
    const std::string& modeName() const { return m_modeName; }
    const std::string& mapName() const { return m_mapName; }
    int modeId() const { return m_modeId; }
    int goal() const { return m_goal; }
    const std::string& goalName() const { return m_goalName; }
    // Whether joiners may change the match settings (mode, goal, map) too; else only the host (it chooses)
    bool settingsEveryone() const { return m_settingsEveryone; }
    // Whether we may change them now
    bool canChangeSettings() const { return isHost() || m_settingsEveryone; }
    int inputDelay() const { return m_inputDelay; }
    bool inputDelayIsAuto() const { return m_delayIsAuto; }
    const std::string& statusText() const { return m_status; }
    const MapTransferInfo& mapTransfer() const { return m_transferInfo; }
    bool canStart(std::string& why) const;
    // Host: every joiner is ready and connected to every other one (canStart()
    // without the checks of the slots)
    bool everyoneReady(std::string& why) const;
    // Every pair of members has measured its round trip a few times
    bool pingsMeasured() const;
    // The slowest pair of members: round trip and jitter (ms), as measured in the lobby
    void worstRtt(float& rttMs, float& jitterMs) const;
    bool pollEvent(LobbyEvent& event);
    // The skin file of the player in a human slot, "" if it isn't here (yet)
    std::string slotSkinPath(int slot) const;
    // True while the skin of the player in this slot is being downloaded
    bool slotSkinDownloading(int slot) const;
    uint32_t skinRevision() const { return m_skins.revision(); }

    // ---- actions (see the contract above)
    void setName(const std::string& name);
    void setLocalPlayers(int count);
    // The skins of our local players (the first of each is announced to the peers)
    void setLocalSkins(const std::vector<LocalSkinFile>& skins) { m_skins.setLocal(skins); }
    // The lobby's map, for everyone to check that they have it (MatchConfig's mapRef and mapHash; hash
    // 0: nothing to check). The host sets it; the joiners get it in LOBBY_EXTRA.
    void setMapCheck(const std::string& ref, uint64_t hash);
    const std::string& mapRef() const { return m_mapRef; }
    uint64_t mapHash() const { return m_mapHash; }
    // Joiners: the lobby's map (its hash) when we don't have it and won't download it, else 0 (told to the
    // host, which tells everyone)
    void setLackedMap(uint64_t hash);
    // Whether a peer doesn't have the lobby's map and won't download it (its map downloads are off)
    bool peerLacksMap(int peer) const;
    // Whether the others' skins we lack are downloaded (off: a "?" in their place)
    void setSkinDownloads(bool on) { m_skins.setDownloads(on); }
    void setReady(bool ready);
    void setSlotTeam(int slot, int team);
    void setSlotColor(int slot, int color);
    void addCpu();
    void removeCpu(int slot);
    void setCpuCount(int count);
    void kick(int peer);
    void setInputDelay(int delay);
    // Host: who may change the match settings
    void setSettingsEveryone(bool everyone);
    // Joiner: asks the host to change a setting (ignored while only the host may); what = SETTING_*
    enum : uint8_t { SETTING_MODE = 1, SETTING_GOAL = 2, SETTING_MAP = 3 };
    void requestSetting(uint8_t what, int value, const std::string& text);
    // Leaves (joiner) or closes (host) the lobby. `abortMatch`: a running
    // match is aborted for the others (they go back to the lobby without us)
    void leave(const std::string& reason, bool abortMatch);

    // ---- for the game (NetGame)
    NetMesh& mesh() { return m_mesh; }
    // Host: what its menus have selected (shown in the lobby)
    void setMatchInfo(const std::string& mode, const std::string& map);
    // Host: the mode's id, goal and what the goal counts (shown in the lobby, never played from)
    void setMatchDetail(int modeId, int goal, const std::string& goalName);
    // The game's part of the state machine
    void setPhase(Phase phase);
    // Shows a message (and adds an event)
    void setStatus(const std::string& text);
    void addEvent(LobbyEvent::Type type, int peer, const std::string& text);
    // Host: the peers that are in the next match (bit p)
    uint8_t memberMask() const;
    // Host: removes a peer from the lobby (it left, timed out, or was kicked)
    void removePeer(int peer, const std::string& reason, bool tellIt);
    // The round: the id of the last match started. A joiner's ready flag
    // counts for the round it was given in. Every peer sets it at the start of a match.
    void setRound(uint8_t round);
    // After a match (joiner): our ready flag is cleared
    void clearReady();

    void sendConfig(uint8_t matchId, const std::vector<uint8_t>& config);
    void sendConfigAck(uint8_t matchId, uint8_t status, const std::string& reason);
    void sendStart(uint8_t matchId);
    void sendMatchAbort(uint8_t matchId, int culprit, const std::string& reason);
    // A series message (NetSeries.h) to a peer, or to everyone (-1). Reliable.
    void sendGameMessage(int peer, const uint8_t* data, size_t size);
    // A joiner: the host's match is being played (it can be joined); the host: its own
    bool matchBeingPlayed() const { return isHost() ? m_phase == Phase::InMatch : m_matchBeingPlayed; }
    // The skin file of a peer's player (its localIndex-th), "" if none or not here
    std::string peerSkinPath(int peer, int localIndex) const;
    // Host: a series (tournament, tour, world) runs: new joiners are refused
    // between its matches too
    void setSeriesLocked(bool locked);
    // Host: sends a map file to a peer (in chunks, over the next frames)
    void sendMap(int peer, uint8_t matchId, std::shared_ptr<const std::vector<uint8_t>> file, uint64_t hash, const std::string& name);
    void cancelMapTransfers();

    enum : uint8_t { ACK_OK = 0, ACK_REFUSED = 1, ACK_NEED_MAP = 2, ACK_NO_MAP = 3 };    //(3: the map is missing, its downloads are off)

    // Map transfer pacing: chunks per peer per 16 ms
    static constexpr int MAP_CHUNKS_PER_FRAME = 4;
    static constexpr uint32_t PING_INTERVAL_MS = 250;
    static constexpr uint32_t REPORT_INTERVAL_MS = 1000;
    static constexpr int MIN_PING_SAMPLES = 6;

private:
    struct HumanSettings {
        uint8_t team[MAX_LOCAL_PLAYERS] = {};
        uint8_t color[MAX_LOCAL_PLAYERS] = {};
    };
    struct MapSend {
        bool active = false;
        uint8_t matchId = 0;
        std::shared_ptr<const std::vector<uint8_t>> file;
        uint32_t offset = 0;
    };
    struct MapReceive {
        bool active = false;
        uint8_t matchId = 0;
        uint32_t size = 0;
        uint64_t hash = 0;
        std::string name;
        std::vector<uint8_t> bytes;
    };
    struct Report {
        uint8_t connectedMask = 0;
        RttEstimate rtt[net::MAX_PEERS];
        uint32_t receivedMs = 0;
        bool valid = false;
    };

    bool handleMesh(const NetMesh::Event& me, LobbyGameEvent& event, uint32_t nowMs);
    bool pollClosed(const NetMesh::Event& me, LobbyGameEvent& event);
    bool handleMessage(int peer, Channel channel, const uint8_t* data, size_t size, LobbyGameEvent& event, uint32_t nowMs);
    void onLobbyState(net::ByteReader& r);
    void onPeerInfo(int peer, net::ByteReader& r);
    void onSlotRequest(int peer, net::ByteReader& r);
    bool onMapBegin(net::ByteReader& r, LobbyGameEvent& event);
    bool onMapChunk(net::ByteReader& r, LobbyGameEvent& event);
    void addMember(int peer);
    void rebuildSlots();
    void updateInputDelay();
    void sendPeerInfo();
    void sendLobbyState();
    void sendMapChunks(uint32_t nowMs);
    void close(const std::string& reason, LobbyGameEvent& event, bool finishMatch);
    void markDirty() { m_dirty = true; }
    std::string defaultName(int peer) const;
    void sendTo(int peer, const net::ByteWriter& w, Channel channel = Channel::Reliable);
    void sendToAll(const net::ByteWriter& w, Channel channel = Channel::Reliable);
    void updateTransferInfo();

    NetMesh m_mesh;
    Options m_options;
    SkinExchange m_skins;
    Phase m_phase = Phase::Joining;
    std::array<LobbyPeer, net::MAX_PEERS> m_peers {};
    std::array<LobbySlot, LOBBY_SLOTS> m_slots {};
    std::array<HumanSettings, net::MAX_PEERS> m_human {};
    int m_cpuCount = 0;
    uint8_t m_cpuTeam[LOBBY_SLOTS] = {};
    uint8_t m_cpuColor[LOBBY_SLOTS] = {};
    std::string m_modeName;
    std::string m_mapName;
    int m_modeId = -1;
    int m_goal = 0;
    std::string m_goalName;
    bool m_settingsEveryone = false;
    int m_delayOverride = -1;
    int m_inputDelay = 2;
    bool m_delayIsAuto = true;
    std::string m_status;
    bool m_dirty = false;
    uint32_t m_lastStateMs = 0;
    bool m_peerInfoDirty = false;
    std::string m_mapRef;
    uint64_t m_mapHash = 0;
    uint64_t m_lackedMap = 0;
    uint8_t m_round = 0;
    std::array<bool, net::MAX_PEERS> m_readyFlag {};      // host: as the joiners sent them
    std::array<uint8_t, net::MAX_PEERS> m_readyRound {};  // host: in which round

    // Round trips
    std::array<RttEstimate, net::MAX_PEERS> m_rtt {};
    std::array<Report, net::MAX_PEERS> m_reports {};    // host: each joiner's view
    uint32_t m_lastPingMs = 0;
    uint32_t m_lastReportMs = 0;

    // Map transfer
    std::array<MapSend, net::MAX_PEERS> m_mapSend {};
    uint32_t m_lastChunkMs = 0;
    MapReceive m_mapReceive;
    MapTransferInfo m_transferInfo;

    std::array<LobbyEvent, 16> m_events {};
    size_t m_eventHead = 0;
    size_t m_eventCount = 0;
    std::array<uint8_t, 2048> m_buffer {};
    bool m_matchBeingPlayed = false;    // (a joiner) the host plays a match
    bool m_seriesLocked = false;
};

// MAP FILES (map transfer). The name a peer sends is only a hint for a
// readable file name: it never becomes a path.
// A file name made of the letters, digits, spaces, '-' and '_' of `hint`
// (without any directory part or extension), at most 40 characters, or "map"
std::string safeMapFileName(const std::string& hint);
// Saves a received map in `dir` (created if needed) as <safe name>.map; an
// existing file of that name with other contents is kept and the new one
// gets a unique name ("name (2).map", ...). A file with the same contents is
// reused. False (with `error`) if the bytes don't match `hash` or the file
// can't be written.
bool saveDownloadedMap(const std::string& dir, const std::string& nameHint, const std::vector<uint8_t>& bytes, uint64_t hash, std::string& path,
    std::string& error);
// A .map file in `dir` with these contents, or ""
std::string findMapByHash(const std::string& dir, uint64_t hash);
// The hash of a file's contents (fnv1a64), 0 if it can't be read or is larger than `maxBytes`
uint64_t hashFile(const std::string& path, uint64_t maxBytes = MAP_MAX_BYTES * 4ull);

} // namespace rollback

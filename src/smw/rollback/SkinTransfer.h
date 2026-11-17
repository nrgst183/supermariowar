#pragma once

#include "NetProtocol.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// SKIN TRANSFER: every peer shows the real skin of every player, in the lobby and in the match.
//
// Skins are purely cosmetic: nothing here enters MatchConfig, the snapshots or the checksums, and a
// match never waits for a skin (a player whose skin isn't here yet is drawn with the default skin
// until the download completes, which may be during the match).
//
// PROTOCOL (reliable; strings are u8 length + bytes). Unlike maps, which only the host sends, skins
// go between any two peers of the mesh:
//   SKIN_INFO     peer -> every connected peer, when it connects and when its skins change:
//                 u8 n (at most SKIN_PLAYERS), n x (str skin name, u64 hash, u32 size): the skin of
//                 each of the sender's local players (index = which local player)
//   SKIN_REQUEST  u64 hash: the receiver lacks a skin with this content, send it
//   SKIN_BEGIN    u64 hash, u32 size, str name, then
//   SKIN_CHUNK    u64 hash, u32 offset, u16 size, bytes (at most SKIN_CHUNK_BYTES, a few per frame)
//
// A peer looks for the announced hash among its own skin files (the skin folders and the downloaded
// skins): a different file with the same name is a different skin and is downloaded. A downloaded
// skin is checked against its hash and the PNG/BMP signature and saved in the download folder under
// a sanitized, unique name (never overwriting anything); the name a peer sends is never used as a path.
namespace rollback {

constexpr int SKIN_PLAYERS = 2;               // skins announced per peer (MAX_LOCAL_PLAYERS)
constexpr uint32_t SKIN_MAX_BYTES = 128 * 1024;
constexpr size_t SKIN_CHUNK_BYTES = 1000;
constexpr int SKIN_CHUNKS_PER_FRAME = 4;      // per 16 ms, over all uploads

// A skin as announced
struct SkinRef {
    std::string name;
    uint64_t hash = 0;
    uint32_t size = 0;
    bool valid() const { return hash != 0 && size > 0 && size <= SKIN_MAX_BYTES; }
};

// A skin file of this machine: the display name and the file (hash and size are read from it)
struct LocalSkinFile {
    std::string name;
    std::string path;
};

// A file name made of the letters, digits, spaces, '-' and '_' of `hint` (no directory part, no
// extension), at most 40 characters, or "skin"
std::string safeSkinFileName(const std::string& hint);
// ".png" or ".bmp" if the bytes start with that format's signature, else ""
const char* skinExtensionFor(const std::vector<uint8_t>& bytes);
// Saves a received skin in `dir` (created if needed) as <safe name>.png/.bmp; an existing file of
// that name with other contents is kept and the new one gets a unique name ("name (2).png", ...).
// A file with the same contents is reused. False (with `error`) if the bytes don't match `hash`, are
// too large, aren't a PNG or BMP, or can't be written.
bool saveDownloadedSkin(const std::string& dir, const std::string& nameHint, const std::vector<uint8_t>& bytes, uint64_t hash, std::string& path,
    std::string& error);

class SkinExchange {
public:
    // Sends a message to a peer (reliable)
    using Send = std::function<void(int peer, const uint8_t* data, size_t size)>;

    struct Options {
        std::vector<std::string> searchDirs;      // folders with skin files (hashed to find a skin)
        std::vector<std::string> downloadDirs;    // where downloads go: the first one that can be written
    };

    SkinExchange(Options options, Send send, int localPeer);

    void setLocalPeer(int peer) { m_localPeer = peer; }
    // Our players' skins; announced to every connected peer (again)
    void setLocal(const std::vector<LocalSkinFile>& skins);
    // Handles SKIN_* messages (false: not one). `type` is the first byte, `r` reads the rest.
    bool handle(int peer, net::MsgType type, net::ByteReader& r);
    // Sends what is due: announcements to newly connected peers, requests, a few chunks.
    // `connectedMask`: the peers we have a connection to (bit p).
    void update(uint32_t nowMs, uint8_t connectedMask);
    // A peer is gone (or reconnects as another one)
    void dropPeer(int peer);

    // The file of the skin of a peer's local player, "" while it isn't here (yet)
    std::string pathOf(int peer, int localIndex) const;
    // True while the skin of this player is being downloaded
    bool downloading(int peer, int localIndex) const;
    // Grows whenever a skin became available or the announcements changed
    uint32_t revision() const { return m_revision; }

private:
    struct Remote {
        std::array<SkinRef, SKIN_PLAYERS> skin {};
        std::array<bool, SKIN_PLAYERS> requested {};
    };
    struct Upload {
        int peer = 0;
        std::shared_ptr<const std::vector<uint8_t>> file;
        uint64_t hash = 0;
        uint32_t offset = 0;
        bool begun = false;
    };
    struct Download {
        bool active = false;
        uint64_t hash = 0;
        uint32_t size = 0;
        std::string name;
        std::vector<uint8_t> bytes;
    };

    std::string find(uint64_t hash) const;
    void scan() const;
    void sendInfo(int peer);
    void onInfo(int peer, net::ByteReader& r);
    void onRequest(int peer, net::ByteReader& r);
    void onBegin(int peer, net::ByteReader& r);
    void onChunk(int peer, net::ByteReader& r);
    void requestMissing(int peer);

    Options m_options;
    Send m_send;
    int m_localPeer = -1;
    std::vector<LocalSkinFile> m_local;
    std::vector<SkinRef> m_localRef;
    std::array<Remote, net::MAX_PEERS> m_remote {};
    std::array<Download, net::MAX_PEERS> m_download {};
    std::vector<Upload> m_uploads;
    uint8_t m_infoSentMask = 0;
    uint32_t m_lastChunkMs = 0;
    uint32_t m_revision = 0;
    mutable bool m_scanned = false;
    mutable std::map<uint64_t, std::string> m_known;    // hash -> file
};

} // namespace rollback

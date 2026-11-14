#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// THE ROLLBACK NETPLAY PROTOCOL
//
// Every message starts with a one byte type. All integers are little-endian,
// written byte by byte (ByteWriter); strings are a u8 length and the bytes.
//
// Channels (see Transport.h):
//  - unreliable, unsequenced: INPUT, the only message sent every tick. Losing
//    or reordering one doesn't matter: each one repeats all the inputs that
//    the receiver hasn't acknowledged yet.
//  - reliable, ordered: everything else (setup, match control, checksums).
//
// Setup (NetMesh.h): HELLO both ways on every new connection (protocol
// version and build check), WELCOME/REJECT from the host, MESH_READY when a
// joiner is connected to every other peer.
// Lobby (NetLobby.h): PEER_INFO, SLOT_REQUEST (joiner -> host), LOBBY_STATE,
// PEER_LEFT (host -> all), PING/PONG and RTT_REPORT (round trip times), BYE.
// Per match (NetLobby.h): CONFIG (host), CONFIG_ACK (each peer), MAP_BEGIN and
// MAP_CHUNK (host -> a peer that lacks the map), START (host), MATCH_ABORT.
// LOBBY_EXTRA (host -> all, with every LOBBY_STATE): the mode id and goal for the lobby screen (NetLobby.h).
// Skins (SkinTransfer.h): SKIN_INFO, SKIN_REQUEST, SKIN_BEGIN, SKIN_CHUNK between any two peers.
// Multi-match modes (NetSeries.h): SERIES_BEGIN, SERIES_SCREEN, SERIES_END,
// WHEEL_PRESS/WHEEL_STOP (bonus wheel), WORLD_INPUT/WORLD_TICK (world map).
//
// INPUT (sender -> one receiver, 26 + frames * slots bytes):
//   u8  type
//   u8  match id         (stale packets of an earlier match are ignored)
//   u32 start frame      (first input frame in this packet)
//   u8  frame count
//   u32 ack              (number of the receiver's input frames the sender has:
//                         all frames below this are known)
//   u32 current frame    (sender's simulation frame, for time sync)
//   i8  frame advantage  (sender's frames ahead of the receiver, as it estimates)
//   u32 send time (ms)   (sender's clock, echoed back for the ping)
//   u32 echo time (ms)   (latest send time received from the receiver)
//   u16 echo hold (ms)   (how long ago that echo time was received)
//   frame count x (one byte per player slot the sender drives, by slot order)
//
// CHECKSUM (reliable): u8 type, u8 match id, u32 frame, u64 peerChecksum()
// of the confirmed state at that frame.
//
// PEER_DROP (reliable, sender -> every peer of the match it still counts in;
// the agreement on a lost peer, see "LOST PEERS" in RollbackSession.h):
//   u8  type
//   u8  match id
//   u8  lost mask        (bit p: the sender counts peer p as gone)
//   per peer in the mask, ascending:
//     i32 decided frame  (from this frame on its slots are CPU players; -1: not decided yet)
//     u32 known          (the sender has all of its inputs below this frame)
//     u8  n              (inputs relayed: the frames known - n .. known - 1)
//     n x (one byte per player slot that peer drove, by slot order)
namespace rollback::net {

constexpr uint32_t PROTOCOL_MAGIC = 0x52574d53; // "SMWR"
constexpr uint16_t PROTOCOL_VERSION = 3;

constexpr int MAX_PEERS = 4;

enum class MsgType : uint8_t {
    Hello = 1,
    Welcome = 2,
    Reject = 3,
    MeshReady = 4,
    Bye = 5,

    Config = 16,
    ConfigAck = 17,
    Start = 18,
    MatchAbort = 19,

    Input = 32,
    Checksum = 33,
    PeerDrop = 34,

    PeerInfo = 48,
    LobbyState = 49,
    SlotRequest = 50,
    PeerLeft = 51,
    Ping = 52,
    Pong = 53,
    RttReport = 54,

    MapBegin = 64,
    MapChunk = 65,

    // Multi-match modes (NetSeries.h): host -> all unless noted
    SeriesBegin = 80,
    SeriesScreen = 81,
    SeriesEnd = 82,
    WheelPress = 83,     // joiner -> host
    WheelStop = 84,
    WorldInput = 85,     // joiner -> host
    WorldTick = 86,

    SkinInfo = 100,
    SkinRequest = 101,
    SkinBegin = 102,
    SkinChunk = 103,

    LobbyExtra = 104,
};

// The series messages are the range [SeriesBegin, SERIES_MSG_END)
constexpr uint8_t SERIES_MSG_END = 96;

constexpr size_t INPUT_HEADER_SIZE = 26;
constexpr int MAX_INPUT_FRAMES_PER_PACKET = 120;

class ByteWriter {
public:
    ByteWriter(uint8_t* buffer, size_t capacity)
        : m_data(buffer), m_capacity(capacity) {}

    void u8(uint8_t v) { put(&v, 1); }
    void i8(int8_t v) { u8(static_cast<uint8_t>(v)); }
    void u16(uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; put(b, 2); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; put(b, 4); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
    void f32(float v) { uint32_t bits; std::memcpy(&bits, &v, 4); u32(bits); }
    void str(std::string_view s) {
        const size_t n = s.size() > 255 ? 255 : s.size();
        u8(static_cast<uint8_t>(n));
        put(s.data(), n);
    }
    void bytes(const void* data, size_t size) { put(data, size); }

    size_t size() const { return m_size; }
    // False if anything didn't fit (the message is then incomplete)
    bool ok() const { return !m_overflow; }
    const uint8_t* data() const { return m_data; }

private:
    void put(const void* src, size_t n) {
        if (m_size + n > m_capacity) {
            m_overflow = true;
            return;
        }
        std::memcpy(m_data + m_size, src, n);
        m_size += n;
    }

    uint8_t* m_data;
    size_t m_capacity;
    size_t m_size = 0;
    bool m_overflow = false;
};

// Reads what ByteWriter wrote. Reading past the end yields zeros and makes
// ok() false, so a parser can read everything and check once.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size) {}

    uint8_t u8() { uint8_t b = 0; get(&b, 1); return b; }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    uint16_t u16() { uint8_t b[2] = {}; get(b, 2); return uint16_t(b[0] | (b[1] << 8)); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() { uint8_t b[4] = {}; get(b, 4); return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64() { const uint64_t lo = u32(); return lo | (uint64_t(u32()) << 32); }
    float f32() { const uint32_t bits = u32(); float v; std::memcpy(&v, &bits, 4); return v; }
    std::string str() {
        const size_t n = u8();
        if (m_pos + n > m_size) {
            m_overflow = true;
            m_pos = m_size;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), n);
        m_pos += n;
        return s;
    }
    const uint8_t* bytes(size_t n) {
        if (m_pos + n > m_size) {
            m_overflow = true;
            m_pos = m_size;
            return nullptr;
        }
        const uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }

    size_t remaining() const { return m_size - m_pos; }
    bool ok() const { return !m_overflow; }

private:
    void get(void* dst, size_t n) {
        if (m_pos + n > m_size) {
            m_overflow = true;
            std::memset(dst, 0, n);
            m_pos = m_size;
            return;
        }
        std::memcpy(dst, m_data + m_pos, n);
        m_pos += n;
    }

    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
    bool m_overflow = false;
};

// 64-bit FNV-1a, for file and data hashes (not cryptographic)
inline uint64_t fnv1a64(const void* data, size_t size, uint64_t hash = 0xcbf29ce484222325ull)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    return hash;
}

} // namespace rollback::net

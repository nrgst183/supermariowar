#pragma once

#include "EditorLauncher.h"

#include <SDL3/SDL.h>
#include <algorithm>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// EDITING TOGETHER (main menu > Editors > Host / Join)
//
// Several people edit the same map or world at once, each in their own game
// (over the network; on one computer, every controller gets its own cursor:
// EditorInput.h).
//
// THE MODEL. Every peer runs the editor on its own copy of the document. The
// document is a set of keyed values (Document): one per map cell or world
// tile (its tiles, tile type, block, warp, ...) and a few sections (the map
// items, hazards, platforms, backgrounds and music; the world's stages,
// vehicles, warps and start items). Once per frame each peer compares its
// document with the last synced state; every value that changed is an
// operation "key = bytes". So any tool of the editors works without knowing
// about the network.
//
// The host puts every operation in one order and sends it to everyone (the
// sender too); everyone applies the operations in that order, so all copies
// end up the same (for one key, the last change in the host's order wins).
// A peer applies its own changes at once (no waiting), and ignores others'
// changes to a key while its own change of that key is on the way (the
// host orders it after them, so it wins anyway).
//
// Joining: the host sends its whole document (as operations). Only the host
// saves to its file and switches maps or worlds (that changes the document
// for everyone); a joiner may "save as" a copy of its own.
//
// IN A GAME LOBBY (main menu > Editors > Online > Host: a lobby like a match's,
// found in Join a Game). The session's messages go over the lobby's own
// connection (NetGame's EditData messages), so it works wherever the lobby
// does. The host's Start opens the editor and asks everyone in the lobby to
// join (EditStart; who comes later is asked when they arrive); leaving the
// editor is back to the lobby, and when the host leaves it, everyone does.
namespace editors::coedit {

using Bytes = std::vector<uint8_t>;

// Little-endian byte streams for the documents' values
struct Writer {
    Bytes out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v)); u16(static_cast<uint16_t>(v >> 16)); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) { uint32_t b; std::memcpy(&b, &v, 4); u32(b); }
    void str(const std::string& s)
    {
        u16(static_cast<uint16_t>(std::min<size_t>(s.size(), 0xffff)));
        out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(s.size(), 0xffff)));
    }
    void bytes(const Bytes& b) { u32(static_cast<uint32_t>(b.size())); out.insert(out.end(), b.begin(), b.end()); }
};

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;
    Reader(const uint8_t* d, size_t n) : data(d), size(n) {}
    explicit Reader(const Bytes& b) : data(b.data()), size(b.size()) {}
    bool need(size_t n) { if (!ok || size - pos < n) ok = false; return ok; }
    uint8_t u8() { return need(1) ? data[pos++] : 0; }
    uint16_t u16() { const uint16_t lo = u8(); return static_cast<uint16_t>(lo | (u8() << 8)); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (static_cast<uint32_t>(u16()) << 16); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() { const uint32_t b = u32(); float v; std::memcpy(&v, &b, 4); return v; }
    std::string str()
    {
        const uint16_t n = u16();
        if (!need(n))
            return {};
        std::string s(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return s;
    }
    Bytes bytes()
    {
        const uint32_t n = u32();
        if (!need(n))
            return {};
        Bytes b(data + pos, data + pos + n);
        pos += n;
        return b;
    }
};

// A cell (map cell, world tile) key; anything below CELL is a section
constexpr uint32_t CELL = 0x01000000;
inline uint32_t cellKey(int x, int y) { return CELL | (static_cast<uint32_t>(x) << 12) | static_cast<uint32_t>(y); }
inline int cellX(uint32_t key) { return static_cast<int>((key >> 12) & 0xfff); }
inline int cellY(uint32_t key) { return static_cast<int>(key & 0xfff); }

// An editor's document (EmbeddedLevelEditor.cpp, EmbeddedWorldEditor.cpp)
class Document {
public:
    virtual ~Document() = default;
    // Every value of the document now (keys that are gone don't appear)
    virtual void capture(std::unordered_map<uint32_t, Bytes>& values) = 0;
    // Puts a value in (another peer's change, in the host's order)
    virtual void apply(uint32_t key, const Bytes& value) = 0;
    // After a batch of apply(): redraw what the editor keeps drawn
    virtual void applied() {}
    // The document's name ("map NMcCoy_1-3")
    virtual std::string title() = 0;
};

// ---- the session

bool host(Editor editor, uint16_t port, const std::string& name, std::string& error);
// Connects; the editor starts when the host has answered (state() == Joined)
bool join(const std::string& address, const std::string& name, std::string& error);
void leave(const std::string& reason);

// The lobby's session (NetGame's lobby, which must be open): host, or join the host's
bool hostLobby(Editor editor, const std::string& name, std::string& error);
bool joinLobby(const std::string& name, std::string& error);
bool overLobby();    // the session is the lobby's
// Joiner: the host asked us in (EditStart) since the last call, and to which editor
bool takeLobbyInvite(Editor& editor);
// Hooks the lobby's messages up (once, at startup)
void installLobbyHandler();

enum class State { Off, Connecting, Joined, Hosting, Ended };
State state();
bool active();      // hosting or joined
bool isHost();
Editor editor();    // the session's editor (a joiner learns it from the host)
std::string statusText();
constexpr uint16_t DEFAULT_PORT = 7301;

// The network: every frame (in the menus and in the editors)
void pump();

// The editor: its document (once it has loaded it), and each frame's end
bool attached();
void attach(std::unique_ptr<Document> document);
void detach();
void afterFrame(float pointerX, float pointerY, bool pointerVisible);

// What the overlay shows
struct Cursor {
    std::string name;
    SDL_Color color;
    float x = 0;
    float y = 0;
};
std::vector<Cursor> remoteCursors();
struct Person {
    std::string name;
    SDL_Color color;
    bool host = false;
    bool you = false;
};
std::vector<Person> people();    // everyone, us first
SDL_Color ourColor();
std::string notice();          // the latest notice (joins, leaves), "" when it has expired
void setNotice(const std::string& text);
std::string documentTitle();   // the host's document

// A joiner's key that only the host may use (save, new, open, previous /
// next map or world): true if it is swallowed (with a notice)
bool blockedKey(SDL_Keycode key, SDL_Keymod mod);

// Statistics for the tests
struct Stats {
    uint64_t opsSent = 0;
    uint64_t opsApplied = 0;
    uint64_t opsSkipped = 0;
};
const Stats& stats();
// A hash of the whole document (the same on every peer once in sync)
uint64_t documentHash();

} // namespace editors::coedit

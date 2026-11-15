#include "CoEdit.h"

#include "rollback/EnetTransport.h"
#include "rollback/NetConditionSim.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>

namespace editors::coedit {
namespace {

constexpr uint16_t PROTOCOL = 1;
constexpr uint32_t NOTICE_MS = 4000;
constexpr int CURSOR_EVERY = 3;    // frames between cursor updates

enum Message : uint8_t {
    MSG_HELLO = 1,      // joiner -> host: u16 protocol, str name
    MSG_WELCOME,        // host -> joiner: u16 protocol, u8 your id, u8 editor, str title
    MSG_REJECT,         // host -> joiner: str reason
    MSG_OPS,            // joiner -> host: u32 n, n x (u32 counter, u32 key, bytes)
    MSG_ORDERED,        // host -> all: u32 n, n x (u8 origin, u32 counter, u32 key, bytes)
    MSG_PEERS,          // host -> all: u8 n, n x (u8 id, str name)
    MSG_CURSOR,         // (unreliable) u8 id, i16 x, i16 y, u8 visible
    MSG_BYE,            // str reason
    MSG_NOTICE,         // host -> all: str
};

struct Peer {
    int id = -1;
    int connection = -1;
    std::string name;
    bool welcomed = false;
};

struct Op {
    uint8_t origin = 0;
    uint32_t counter = 0;
    uint32_t key = 0;
    Bytes value;
};

struct Session {
    State state = State::Off;
    bool host = false;
    Editor editor = Editor::Level;
    std::unique_ptr<rollback::EnetTransport> transport;
    int hostConnection = -1;
    int myId = 0;
    std::string myName;
    std::string title;
    std::string status;
    std::vector<Peer> peers;                // host: the joiners; joiner: everybody (from MSG_PEERS)

    std::unique_ptr<Document> doc;
    std::unordered_map<uint32_t, Bytes> baseline;
    std::unordered_map<uint32_t, uint32_t> pending;    // joiner: key -> our latest unacknowledged counter
    uint32_t counter = 0;
    std::vector<Op> early;                  // joiner: ordered ops before the document is attached

    std::map<int, Cursor> cursors;
    int cursorFrames = 0;
    int16_t lastCursorX = -1, lastCursorY = -1;
    bool lastCursorVisible = false;

    std::string notice;
    uint32_t noticeMs = 0;
    Stats stats;
};
Session g;

SDL_Color colorOf(int id)
{
    static const SDL_Color colors[] = {{232, 56, 40, 255}, {40, 184, 56, 255}, {48, 112, 232, 255}, {248, 200, 24, 255},
        {176, 72, 216, 255}, {248, 136, 24, 255}, {40, 200, 216, 255}, {248, 120, 184, 255}, {240, 240, 240, 255}};
    return colors[static_cast<size_t>(std::clamp(id, 0, 8))];
}

std::string nameOf(int id)
{
    if (id == g.myId)
        return g.myName;
    for (const Peer& p : g.peers) {
        if (p.id == id)
            return p.name;
    }
    return "Player " + std::to_string(id + 1);
}

void send(int connection, const Bytes& bytes, rollback::Channel channel = rollback::Channel::Reliable)
{
    if (g.transport && connection >= 0)
        g.transport->send(connection, channel, bytes.data(), bytes.size());
}

// Host: to every welcomed joiner (but `except`)
void broadcast(const Bytes& bytes, rollback::Channel channel = rollback::Channel::Reliable, int except = -1)
{
    for (const Peer& p : g.peers) {
        if (p.welcomed && p.connection != except)
            send(p.connection, bytes, channel);
    }
}

Bytes orderedMessage(const std::vector<Op>& ops)
{
    Writer w;
    w.u8(MSG_ORDERED);
    w.u32(static_cast<uint32_t>(ops.size()));
    for (const Op& op : ops) {
        w.u8(op.origin);
        w.u32(op.counter);
        w.u32(op.key);
        w.bytes(op.value);
    }
    return std::move(w.out);
}

void sendPeers()
{
    Writer w;
    w.u8(MSG_PEERS);
    w.u8(static_cast<uint8_t>(g.peers.size() + 1));
    w.u8(0);
    w.str(g.myName);
    for (const Peer& p : g.peers) {
        w.u8(static_cast<uint8_t>(p.id));
        w.str(p.name);
    }
    broadcast(w.out);
}

void sendNotice(const std::string& text)
{
    setNotice(text);
    Writer w;
    w.u8(MSG_NOTICE);
    w.str(text);
    broadcast(w.out);
}

void recapture()
{
    g.baseline.clear();
    if (g.doc)
        g.doc->capture(g.baseline);
}

// Host: a joiner that said hello gets the document once it is there
void welcome(Peer& peer)
{
    if (peer.welcomed || !g.doc)
        return;
    Writer w;
    w.u8(MSG_WELCOME);
    w.u16(PROTOCOL);
    w.u8(static_cast<uint8_t>(peer.id));
    w.u8(static_cast<uint8_t>(g.editor));
    w.str(g.doc->title());
    send(peer.connection, w.out);

    //The whole document, as operations
    std::vector<Op> ops;
    ops.reserve(g.baseline.size());
    std::map<uint32_t, const Bytes*> sorted;    //(sections first: a world's size before its tiles)
    for (const auto& [key, value] : g.baseline)
        sorted[key] = &value;
    for (const auto& [key, value] : sorted)
        ops.push_back(Op {0, 0, key, *value});
    send(peer.connection, orderedMessage(ops));
    peer.welcomed = true;
    printf("coedit: %s joined (%zu values sent)\n", peer.name.c_str(), ops.size());
    sendPeers();
    sendNotice(peer.name + " joined");
}

// Applies ordered operations (joiner: the pending rule, see CoEdit.h)
void applyOrdered(const std::vector<Op>& ops)
{
    if (!g.doc)
        return;
    bool any = false;
    for (const Op& op : ops) {
        if (!g.host) {
            auto it = g.pending.find(op.key);
            if (op.origin == g.myId) {
                //Our own change, back in order: already in our copy
                if (it != g.pending.end() && it->second == op.counter)
                    g.pending.erase(it);
                continue;
            }
            if (it != g.pending.end()) {
                //Ours comes after it in the host's order
                g.stats.opsSkipped++;
                continue;
            }
        }
        g.doc->apply(op.key, op.value);
        g.stats.opsApplied++;
        any = true;
    }
    if (any)
        g.doc->applied();
    //(our own changes were all sent at the end of the last frame)
    recapture();
}

void handleMessage(int connection, const uint8_t* data, size_t size)
{
    Reader r(data, size);
    const uint8_t type = r.u8();
    if (g.host) {
        auto peer = std::find_if(g.peers.begin(), g.peers.end(), [&](const Peer& p) { return p.connection == connection; });
        if (peer == g.peers.end())
            return;
        switch (type) {
            case MSG_HELLO: {
                const uint16_t protocol = r.u16();
                peer->name = r.str();
                if (protocol != PROTOCOL) {
                    Writer w;
                    w.u8(MSG_REJECT);
                    w.str("another version of the game");
                    send(connection, w.out);
                    g.transport->flush();
                    g.transport->disconnect(connection);
                    return;
                }
                if (peer->name.empty())
                    peer->name = "Player " + std::to_string(peer->id + 1);
                welcome(*peer);
                break;
            }
            case MSG_OPS: {
                if (!peer->welcomed || !g.doc)
                    return;
                const uint32_t n = r.u32();
                std::vector<Op> ops;
                for (uint32_t k = 0; k < n && r.ok; k++) {
                    Op op;
                    op.origin = static_cast<uint8_t>(peer->id);
                    op.counter = r.u32();
                    op.key = r.u32();
                    op.value = r.bytes();
                    if (r.ok)
                        ops.push_back(std::move(op));
                }
                applyOrdered(ops);
                Bytes message = orderedMessage(ops);
                for (const Peer& p : g.peers) {
                    if (p.welcomed)
                        send(p.connection, message);
                }
                break;
            }
            case MSG_CURSOR:
                if (size >= 7) {
                    Cursor& c = g.cursors[peer->id];
                    r.u8();
                    c.x = r.i16();
                    c.y = r.i16();
                    const bool visible = r.u8() != 0;
                    c.name = peer->name;
                    c.color = colorOf(peer->id);
                    if (!visible)
                        g.cursors.erase(peer->id);
                    //(to the others, with the right id)
                    Bytes relay(data, data + size);
                    relay[1] = static_cast<uint8_t>(peer->id);
                    broadcast(relay, rollback::Channel::Unreliable, connection);
                }
                break;
            default:
                break;
        }
        return;
    }

    //Joiner
    switch (type) {
        case MSG_WELCOME:
            if (r.u16() != PROTOCOL) {
                leave("another version of the game");
                return;
            }
            g.myId = r.u8();
            g.editor = static_cast<Editor>(r.u8());
            g.title = r.str();
            g.state = State::Joined;
            g.status = "Joined: " + g.title;
            printf("coedit: joined as peer %d (%s)\n", g.myId, g.title.c_str());
            break;
        case MSG_REJECT:
            g.status = "The host refused: " + r.str();
            g.state = State::Ended;
            break;
        case MSG_ORDERED: {
            const uint32_t n = r.u32();
            std::vector<Op> ops;
            ops.reserve(n);
            for (uint32_t k = 0; k < n && r.ok; k++) {
                Op op;
                op.origin = r.u8();
                op.counter = r.u32();
                op.key = r.u32();
                op.value = r.bytes();
                if (r.ok)
                    ops.push_back(std::move(op));
            }
            if (g.doc)
                applyOrdered(ops);
            else
                g.early.insert(g.early.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
            break;
        }
        case MSG_PEERS: {
            const uint8_t n = r.u8();
            g.peers.clear();
            for (uint8_t k = 0; k < n && r.ok; k++) {
                Peer p;
                p.id = r.u8();
                p.name = r.str();
                if (p.id != g.myId)
                    g.peers.push_back(p);
            }
            break;
        }
        case MSG_CURSOR: {
            const int id = r.u8();
            Cursor c;
            c.x = r.i16();
            c.y = r.i16();
            const bool visible = r.u8() != 0;
            if (id == g.myId)
                break;
            if (!visible) {
                g.cursors.erase(id);
                break;
            }
            c.name = nameOf(id);
            c.color = colorOf(id);
            g.cursors[id] = c;
            break;
        }
        case MSG_NOTICE:
            setNotice(r.str());
            break;
        case MSG_BYE:
            g.status = "The host closed the session" + std::string(r.ok ? "" : "");
            setNotice("The host closed the session: you keep your copy");
            g.state = State::Ended;
            break;
        default:
            break;
    }
}

bool openTransport(uint16_t port, std::string& error)
{
    g.transport = rollback::EnetTransport::create(port, rollback::NetConditions(), error);
    return g.transport != nullptr;
}

void reset()
{
    g = Session();
}

} // namespace

bool host(Editor editor, uint16_t port, const std::string& name, std::string& error)
{
    leave("");
    reset();
    if (!openTransport(port, error))
        return false;
    g.host = true;
    g.state = State::Hosting;
    g.editor = editor;
    g.myId = 0;
    g.myName = name.empty() ? "Host" : name;
    g.status = "Hosting on port " + std::to_string(g.transport->localPort());
    printf("coedit: hosting the %s editor on port %u\n", editor == Editor::Level ? "level" : "world", g.transport->localPort());
    return true;
}

bool join(const std::string& address, const std::string& name, std::string& error)
{
    leave("");
    reset();
    std::string hostName = address;
    uint16_t port = DEFAULT_PORT;
    const size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        hostName = address.substr(0, colon);
        port = static_cast<uint16_t>(std::atoi(address.c_str() + colon + 1));
    }
    if (hostName.empty() || port == 0) {
        error = "enter the host's address (ip or ip:port)";
        return false;
    }
    if (!openTransport(0, error))
        return false;
    g.hostConnection = g.transport->connect(hostName, port);
    if (g.hostConnection < 0) {
        error = "can't reach " + address;
        g.transport.reset();
        return false;
    }
    g.host = false;
    g.state = State::Connecting;
    g.myName = name.empty() ? "Player" : name;
    g.status = "Connecting to " + address + "...";
    return true;
}

void leave(const std::string& reason)
{
    if (!g.transport) {
        g.state = State::Off;
        return;
    }
    Writer w;
    w.u8(MSG_BYE);
    w.str(reason);
    if (g.host)
        broadcast(w.out);
    else
        send(g.hostConnection, w.out);
    g.transport->flush();
    g.transport->close(300);
    g.transport.reset();
    g.doc.reset();
    g.state = State::Off;
    printf("coedit: left the session\n");
}

State state() { return g.state; }
bool active() { return g.state == State::Hosting || g.state == State::Joined; }
bool isHost() { return g.host && g.state == State::Hosting; }
Editor editor() { return g.editor; }
std::string statusText() { return g.status; }

void pump()
{
    if (!g.transport)
        return;
    rollback::TransportEvent event;
    while (g.transport && g.transport->poll(event)) {
        switch (event.type) {
            case rollback::TransportEvent::Type::Connected:
                if (g.host) {
                    if (g.peers.size() >= 8) {
                        g.transport->disconnect(event.connection);
                        break;
                    }
                    int id = 1;
                    while (std::any_of(g.peers.begin(), g.peers.end(), [&](const Peer& p) { return p.id == id; }))
                        id++;
                    Peer p;
                    p.id = id;
                    p.connection = event.connection;
                    g.peers.push_back(p);
                } else if (event.connection == g.hostConnection) {
                    Writer w;
                    w.u8(MSG_HELLO);
                    w.u16(PROTOCOL);
                    w.str(g.myName);
                    send(g.hostConnection, w.out);
                    g.status = "Connected, waiting for the host...";
                }
                break;
            case rollback::TransportEvent::Type::Disconnected:
                if (g.host) {
                    auto it = std::find_if(g.peers.begin(), g.peers.end(), [&](const Peer& p) { return p.connection == event.connection; });
                    if (it != g.peers.end()) {
                        const std::string name = it->name;
                        const bool welcomed = it->welcomed;
                        g.cursors.erase(it->id);
                        g.peers.erase(it);
                        if (welcomed) {
                            sendPeers();
                            sendNotice(name + " left");
                        }
                    }
                } else if (event.connection == g.hostConnection) {
                    if (g.state == State::Connecting)
                        g.status = "No answer from the host";
                    else if (g.state == State::Joined)
                        setNotice("Lost the connection to the host: you keep your copy");
                    g.state = State::Ended;
                    g.cursors.clear();
                }
                break;
            case rollback::TransportEvent::Type::Received:
                handleMessage(event.connection, event.data, event.size);
                break;
            default:
                break;
        }
    }
    if (g.transport)
        g.transport->flush();
}

bool attached()
{
    return g.doc != nullptr;
}

void attach(std::unique_ptr<Document> document)
{
    g.doc = std::move(document);
    recapture();
    g.title = g.host ? g.doc->title() : g.title;
    if (g.host) {
        for (Peer& p : g.peers)
            welcome(p);
    } else if (!g.early.empty()) {
        std::vector<Op> ops = std::move(g.early);
        g.early.clear();
        applyOrdered(ops);
    }
}

void detach()
{
    g.doc.reset();
    g.baseline.clear();
    g.pending.clear();
}

void afterFrame(float pointerX, float pointerY, bool pointerVisible)
{
    if (!g.doc || !active())
        return;

    //Our changes: everything that differs from the synced state
    std::unordered_map<uint32_t, Bytes> now;
    g.doc->capture(now);
    std::vector<Op> ops;
    for (auto& [key, value] : now) {
        auto it = g.baseline.find(key);
        if (it == g.baseline.end() || it->second != value)
            ops.push_back(Op {static_cast<uint8_t>(g.myId), 0, key, value});
    }
    for (const auto& [key, value] : g.baseline) {
        if (!now.count(key))
            ops.push_back(Op {static_cast<uint8_t>(g.myId), 0, key, Bytes()});    //(gone)
    }
    if (!ops.empty()) {
        //(sections first, as when joining)
        std::sort(ops.begin(), ops.end(), [](const Op& a, const Op& b) { return a.key < b.key; });
        for (Op& op : ops)
            op.counter = ++g.counter;
        g.stats.opsSent += ops.size();
        if (g.host) {
            broadcast(orderedMessage(ops));
        } else {
            Writer w;
            w.u8(MSG_OPS);
            w.u32(static_cast<uint32_t>(ops.size()));
            for (const Op& op : ops) {
                w.u32(op.counter);
                w.u32(op.key);
                w.bytes(op.value);
                g.pending[op.key] = op.counter;
            }
            send(g.hostConnection, w.out);
        }
        g.baseline = std::move(now);
    }

    //Our pointer, now and then
    const int16_t x = static_cast<int16_t>(pointerX);
    const int16_t y = static_cast<int16_t>(pointerY);
    if (++g.cursorFrames >= CURSOR_EVERY && (x != g.lastCursorX || y != g.lastCursorY || pointerVisible != g.lastCursorVisible)) {
        g.cursorFrames = 0;
        g.lastCursorX = x;
        g.lastCursorY = y;
        g.lastCursorVisible = pointerVisible;
        Writer w;
        w.u8(MSG_CURSOR);
        w.u8(static_cast<uint8_t>(g.myId));
        w.i16(x);
        w.i16(y);
        w.u8(pointerVisible ? 1 : 0);
        if (g.host)
            broadcast(w.out, rollback::Channel::Unreliable);
        else
            send(g.hostConnection, w.out, rollback::Channel::Unreliable);
    }
    if (g.transport)
        g.transport->flush();
}

std::vector<Cursor> remoteCursors()
{
    std::vector<Cursor> list;
    if (g.state == State::Hosting || g.state == State::Joined) {
        for (const auto& [id, c] : g.cursors)
            list.push_back(c);
    }
    return list;
}

std::vector<std::pair<std::string, SDL_Color>> people()
{
    std::vector<std::pair<std::string, SDL_Color>> list;
    if (!active())
        return list;
    list.emplace_back(g.myName, colorOf(g.myId));
    if (!g.host)
        list.emplace_back(nameOf(0), colorOf(0));
    for (const Peer& p : g.peers) {
        if (p.id != 0 && (!g.host || p.welcomed))
            list.emplace_back(p.name, colorOf(p.id));
    }
    return list;
}

SDL_Color ourColor()
{
    return colorOf(g.myId);
}

std::string notice()
{
    if (g.notice.empty() || SDL_GetTicks() - g.noticeMs > NOTICE_MS)
        return {};
    return g.notice;
}

void setNotice(const std::string& text)
{
    g.notice = text;
    g.noticeMs = static_cast<uint32_t>(SDL_GetTicks());
    if (!text.empty())
        printf("coedit: %s\n", text.c_str());
}

std::string documentTitle()
{
    return g.title;
}

bool blockedKey(SDL_Keycode key, SDL_Keymod mod)
{
    if (g.host || !active())
        return false;
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool blocked = (key == SDLK_S && !shift) || key == SDLK_N || key == SDLK_F || key == SDLK_PAGEUP || key == SDLK_PAGEDOWN;
    if (blocked)
        setNotice(key == SDLK_S ? "Only the host saves the shared file (Save as makes your own copy)" : "Only the host opens other maps and worlds");
    return blocked;
}

const Stats& stats()
{
    return g.stats;
}

uint64_t documentHash()
{
    if (!g.doc)
        return 0;
    std::unordered_map<uint32_t, Bytes> values;
    g.doc->capture(values);
    std::map<uint32_t, const Bytes*> sorted;
    for (const auto& [key, value] : values)
        sorted[key] = &value;
    uint64_t h = 0xcbf29ce484222325ull;
    auto add = [&h](uint8_t b) { h = (h ^ b) * 0x100000001b3ull; };
    for (const auto& [key, value] : sorted) {
        for (int k = 0; k < 4; k++)
            add(static_cast<uint8_t>(key >> (8 * k)));
        for (uint8_t b : *value)
            add(b);
    }
    return h;
}

} // namespace editors::coedit

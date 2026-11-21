#include "CoEdit.h"

#include "rollback/Transport.h"
#ifndef NETWORK_DISABLED
#include "rollback/EnetTransport.h"
#endif
#include "rollback/NetConditionSim.h"
#include "rollback/NetGame.h"
#include "rollback/NetProtocol.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>

namespace editors::coedit {
namespace {

constexpr uint16_t PROTOCOL = 2;
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
    MSG_LOCALS,         // (unreliable) u8 id, u8 n, n x (u8 controller, i16 x, i16 y): the others at that game
};

struct Peer {
    int id = -1;
    int connection = -1;
    std::string name;
    bool hello = false;       // it said hello (a lobby's peers are there before they open the editor)
    bool welcomed = false;
};

struct Op {
    uint8_t origin = 0;
    uint32_t counter = 0;
    uint32_t key = 0;
    Bytes value;
};

// The lobby's connection as the session's transport: a connection is a lobby peer (the host is 0),
// a message is the lobby's EditData message around ours. Polling it pumps the lobby (the editor has
// its own loop: the main loop's pump doesn't run while it's open), and the lobby's peers coming and
// going are the connections'.
class LobbyTransport : public rollback::IMeshTransport {
public:
    explicit LobbyTransport(bool host) : m_host(host) {}

    void send(int connection, rollback::Channel channel, const uint8_t* data, size_t size) override
    {
        rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
        if (!lobby || connection < 0 || connection >= rollback::net::MAX_PEERS || !(m_known & (1u << connection)))
            return;
        m_buffer.assign(1, static_cast<uint8_t>(rollback::net::MsgType::EditData));
        m_buffer.insert(m_buffer.end(), data, data + size);
        lobby->mesh().send(connection, channel, m_buffer.data(), m_buffer.size());
    }

    bool poll(rollback::TransportEvent& event) override
    {
        if (m_events.empty() && !m_polled) {
            m_polled = true;
            refresh();
        }
        if (m_events.empty()) {
            m_polled = false;
            return false;
        }
        m_current = std::move(m_events.front());
        m_events.pop_front();
        event = rollback::TransportEvent();
        event.type = m_current.type;
        event.connection = m_current.connection;
        event.channel = rollback::Channel::Reliable;
        event.data = m_current.data.data();
        event.size = m_current.data.size();
        return true;
    }

    void flush() override
    {
        if (rollback::NetLobby* lobby = rollback::NetGame::instance().lobby())
            lobby->mesh().transport().flush();
    }
    void disconnect(int) override {}    //(the lobby says who is in it)
    int connect(const std::string&, uint16_t) override { return 0; }
    int connect(uint32_t, uint16_t) override { return 0; }
    bool address(int, uint32_t&, uint16_t&) const override { return false; }
    uint16_t localPort() const override { return 0; }

    // A co-edit message from a lobby peer (the lobby's message handler)
    void received(int peer, const uint8_t* data, size_t size)
    {
        if (peer < 0 || peer >= rollback::net::MAX_PEERS || (!m_host && peer != 0))
            return;
        arrived(peer);
        m_events.push_back({rollback::TransportEvent::Type::Received, peer, Bytes(data, data + size)});
    }

    // The session's editor (the host asks the lobby's peers into it)
    Editor editor = Editor::Level;

private:
    struct Queued {
        rollback::TransportEvent::Type type;
        int connection;
        Bytes data;
    };

    // The lobby's news: its peers (the host: everyone else; a joiner: the host), and its document's name
    void refresh();
    void arrived(int peer)
    {
        if (m_known & (1u << peer))
            return;
        m_known |= 1u << peer;
        m_events.push_back({rollback::TransportEvent::Type::Connected, peer, {}});
        if (m_host) {
            //(asked in: the editor opens in its game, which then says hello)
            const uint8_t invite[2] = {static_cast<uint8_t>(rollback::net::MsgType::EditStart), static_cast<uint8_t>(editor == Editor::World ? 1 : 0)};
            if (rollback::NetLobby* lobby = rollback::NetGame::instance().lobby())
                lobby->mesh().send(peer, rollback::Channel::Reliable, invite, sizeof(invite));
        }
    }

    bool m_host;
    uint32_t m_known = 0;
    bool m_polled = false;
    std::deque<Queued> m_events;
    Queued m_current {};
    Bytes m_buffer;
};

struct Session {
    State state = State::Off;
    bool host = false;
    Editor editor = Editor::Level;
    std::unique_ptr<rollback::IMeshTransport> transport;    //(ENet's, or the lobby's; none in the builds without the network)
    LobbyTransport* lobby = nullptr;                         //(the transport, when it's the lobby's)
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
    std::map<int, std::vector<LocalCursor>> locals;    // each peer's others on its computer (MSG_LOCALS)
    std::vector<LocalCursor> lastLocals;               // ours, as last sent
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

// The name of one of the others at a peer's computer ("Name P2")
std::string localName(const std::string& name, int index)
{
    return name + " P" + std::to_string(index + 1);
}

// Reads a MSG_LOCALS list (after its id)
std::vector<LocalCursor> readLocals(Reader& r)
{
    std::vector<LocalCursor> list;
    const uint8_t n = r.u8();
    for (uint8_t k = 0; k < n && k < 8 && r.ok; k++) {
        LocalCursor c;
        c.index = r.u8();
        c.x = r.i16();
        c.y = r.i16();
        if (r.ok)
            list.push_back(c);
    }
    return list;
}

// A peer's others now: who is new is announced
void setLocals(int id, std::vector<LocalCursor> list, const std::string& name)
{
    const std::vector<LocalCursor>& before = g.locals[id];
    for (const LocalCursor& c : list) {
        const bool known = std::any_of(before.begin(), before.end(), [&](const LocalCursor& b) { return b.index == c.index; });
        if (!known)
            setNotice(localName(name, c.index) + " joined");
    }
    if (list.empty())
        g.locals.erase(id);
    else
        g.locals[id] = std::move(list);
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
    //(those in the session: a lobby's peers who haven't opened the editor aren't)
    const size_t welcomed = static_cast<size_t>(std::count_if(g.peers.begin(), g.peers.end(), [](const Peer& p) { return p.welcomed; }));
    w.u8(static_cast<uint8_t>(welcomed + 1));
    w.u8(0);
    w.str(g.myName);
    for (const Peer& p : g.peers) {
        if (!p.welcomed)
            continue;
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
    if (peer.welcomed || !peer.hello || !g.doc)
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
                peer->hello = true;
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
            case MSG_LOCALS:
                if (peer->welcomed) {
                    r.u8();
                    setLocals(peer->id, readLocals(r), peer->name);
                    //(to the others, with the right id)
                    Bytes relay(data, data + size);
                    relay[1] = static_cast<uint8_t>(peer->id);
                    broadcast(relay, rollback::Channel::Unreliable, connection);
                }
                break;
            case MSG_BYE:
                //(it left the session: in a lobby, it may come back in, and is welcomed again then)
                peer->hello = false;
                if (peer->welcomed) {
                    peer->welcomed = false;
                    g.cursors.erase(peer->id);
                    g.locals.erase(peer->id);
                    sendPeers();
                    sendNotice(peer->name + " left");
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
        case MSG_LOCALS: {
            const int id = r.u8();
            std::vector<LocalCursor> list = readLocals(r);
            if (id != g.myId && r.ok)
                setLocals(id, std::move(list), nameOf(id));
            break;
        }
        case MSG_NOTICE:
            setNotice(r.str());
            break;
        case MSG_BYE:
            g.status = "The host closed the session" + std::string(r.ok ? "" : "");
            //(a lobby's: everyone goes back to the lobby)
            setNotice(g.lobby ? "The host ended the session" : "The host ended the session. You keep your copy");
            g.state = State::Ended;
            break;
        default:
            break;
    }
}

void LobbyTransport::refresh()
{
    rollback::NetGame& net = rollback::NetGame::instance();
    net.pump();
    rollback::NetLobby* lobby = net.lobby();
    const bool open = lobby && lobby->phase() != rollback::NetLobby::Phase::Closed;
    for (int p = 0; p < rollback::net::MAX_PEERS; p++) {
        const bool wanted = m_host ? p != 0 : p == 0;
        const bool present = open && wanted && p != lobby->localPeer() && lobby->peer(p).present;
        if (present) {
            arrived(p);
        } else if (m_known & (1u << p)) {
            m_known &= ~(1u << p);
            m_events.push_back({rollback::TransportEvent::Type::Disconnected, p, {}});
        }
    }
    //(the host: what the lobby and the games list show)
    if (m_host && open && g.doc) {
        std::string title = g.doc->title();
        for (const char* kind : {"map ", "world "}) {
            if (title.rfind(kind, 0) == 0)
                title.erase(0, std::strlen(kind));
        }
        net.setEditActivity(editor == Editor::World ? 2 : 1, true, title);
    }
}

bool g_lobbyInvite = false;

// Our name in a lobby's session: the lobby's (what the others see there)
std::string lobbyName(const std::string& fallback)
{
    const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
    if (lobby && lobby->localPeer() >= 0 && !lobby->peer(lobby->localPeer()).name.empty())
        return lobby->peer(lobby->localPeer()).name;
    return fallback.empty() ? "Player" : fallback;
}
Editor g_lobbyInviteEditor = Editor::Level;

bool openTransport(uint16_t port, std::string& error)
{
#ifdef NETWORK_DISABLED
    (void)port;
    error = "this build has no network support";
    return false;
#else
    g.transport = rollback::EnetTransport::create(port, rollback::NetConditions(), error);
    return g.transport != nullptr;
#endif
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
    if (!g.lobby)
        g.transport->close(300);    //(the lobby's connection goes on)
    g.transport.reset();
    g.lobby = nullptr;
    g.doc.reset();
    g.state = State::Off;
    printf("coedit: left the session\n");
}

bool hostLobby(Editor editor, const std::string& name, std::string& error)
{
    leave("");
    reset();
    if (!rollback::NetGame::instance().lobby()) {
        error = "there's no lobby";
        return false;
    }
    auto transport = std::make_unique<LobbyTransport>(true);
    transport->editor = editor;
    g.lobby = transport.get();
    g.transport = std::move(transport);
    g.host = true;
    g.state = State::Hosting;
    g.editor = editor;
    g.myId = 0;
    g.myName = lobbyName(name);
    g.status = "Editing with the lobby";
    printf("coedit: hosting the %s editor in the lobby\n", editor == Editor::Level ? "level" : "world");
    return true;
}

bool joinLobby(const std::string& name, std::string& error)
{
    leave("");
    reset();
    if (!rollback::NetGame::instance().lobby()) {
        error = "there's no lobby";
        return false;
    }
    auto transport = std::make_unique<LobbyTransport>(false);
    g.lobby = transport.get();
    g.transport = std::move(transport);
    g.hostConnection = 0;
    g.host = false;
    g.state = State::Connecting;
    g.myName = lobbyName(name);
    g.status = "Joining the host's editor...";
    return true;
}

bool overLobby() { return g.lobby != nullptr; }

bool takeLobbyInvite(Editor& editor)
{
    if (!g_lobbyInvite)
        return false;
    g_lobbyInvite = false;
    editor = g_lobbyInviteEditor;
    return true;
}

void installLobbyHandler()
{
    rollback::NetGame::instance().setEditHandler([](int peer, const uint8_t* data, size_t size) {
        if (size == 0)
            return;
        if (data[0] == static_cast<uint8_t>(rollback::net::MsgType::EditStart)) {
            const rollback::NetLobby* lobby = rollback::NetGame::instance().lobby();
            if (peer == 0 && size >= 2 && lobby && !lobby->isHost()) {
                g_lobbyInvite = true;
                g_lobbyInviteEditor = data[1] == 1 ? Editor::World : Editor::Level;
            }
            return;
        }
        if (g.lobby)
            g.lobby->received(peer, data + 1, size - 1);
    });
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
                        g.locals.erase(it->id);
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
                        setNotice("Lost the connection to the host. You keep your copy");
                    g.state = State::Ended;
                    g.cursors.clear();
                    g.locals.clear();
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
    //A lobby's session the host ended (or the lobby closed): back to the lobby, as everyone is
    if (g.lobby && !g.host && g.state == State::Ended)
        requestClose();
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

void afterFrame(float pointerX, float pointerY, bool pointerVisible, const std::vector<LocalCursor>& locals)
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

    //The others here (their controllers), as they move; and once a second anyway (unreliable: one may
    //be lost, and someone who arrives later learns of them)
    auto same = [](const std::vector<LocalCursor>& a, const std::vector<LocalCursor>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const LocalCursor& p, const LocalCursor& q) {
            return p.index == q.index && static_cast<int16_t>(p.x) == static_cast<int16_t>(q.x) && static_cast<int16_t>(p.y) == static_cast<int16_t>(q.y);
        });
    };
    static int localsFrames = 0;
    ++localsFrames;
    if ((localsFrames >= CURSOR_EVERY && !same(locals, g.lastLocals)) || (localsFrames >= 60 && !locals.empty())) {
        localsFrames = 0;
        g.lastLocals = locals;
        Writer w;
        w.u8(MSG_LOCALS);
        w.u8(static_cast<uint8_t>(g.myId));
        w.u8(static_cast<uint8_t>(std::min<size_t>(locals.size(), 8)));
        for (size_t k = 0; k < locals.size() && k < 8; k++) {
            w.u8(locals[k].index);
            w.i16(static_cast<int16_t>(locals[k].x));
            w.i16(static_cast<int16_t>(locals[k].y));
        }
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
        for (const auto& [id, others] : g.locals) {
            //(their player's color, lighter)
            SDL_Color color = colorOf(id);
            color.r = static_cast<Uint8>((color.r * 2 + 255) / 3);
            color.g = static_cast<Uint8>((color.g * 2 + 255) / 3);
            color.b = static_cast<Uint8>((color.b * 2 + 255) / 3);
            for (const LocalCursor& o : others)
                list.push_back(Cursor {localName(nameOf(id), o.index), color, o.x, o.y});
        }
    }
    return list;
}

std::vector<Person> people()
{
    std::vector<Person> list;
    if (!active())
        return list;
    //Each player, then the others at their computer ("Name P2")
    auto add = [&](int id, const std::string& name, bool host, bool you) {
        list.push_back(Person {name, colorOf(id), host, you});
        const auto it = g.locals.find(id);
        const std::vector<LocalCursor>* others = you ? &g.lastLocals : it != g.locals.end() ? &it->second : nullptr;
        if (others) {
            for (const LocalCursor& o : *others)
                list.push_back(Person {localName(name, o.index), colorOf(id), false, you});
        }
    };
    add(g.myId, g.myName, g.host, true);
    if (!g.host)
        add(0, nameOf(0), true, false);
    for (const Peer& p : g.peers) {
        if (p.id != 0 && (!g.host || p.welcomed))
            add(p.id, p.name, false, false);
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
        setNotice(key == SDLK_S ? "Only the host can save. Use Save as to keep your own copy" : "Only the host can open other maps and worlds");
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

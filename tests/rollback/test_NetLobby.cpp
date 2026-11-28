#include "doctest.h"

#include "NetLobby.h"
#include "NetProtocol.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace rollback;

namespace {

// An in-memory network of IMeshTransports: every node has a port; messages
// arrive in order, at the next poll
struct FakeMeshTransport;

struct FakeHub {
    std::vector<FakeMeshTransport*> nodes;
};

struct FakeMeshTransport : IMeshTransport {
    struct Conn {
        bool open = false;
        FakeMeshTransport* other = nullptr;
        int otherConn = -1;
    };
    struct Ev {
        TransportEvent::Type type;
        int connection;
        Channel channel;
        std::vector<uint8_t> data;
    };

    FakeHub& hub;
    uint16_t port;
    std::vector<Conn> conns;
    std::deque<Ev> queue;
    std::vector<uint8_t> current;
    size_t bytesSent = 0;
    size_t biggestReliable = 0;

    FakeMeshTransport(FakeHub& h, uint16_t p) : hub(h), port(p) { hub.nodes.push_back(this); }
    ~FakeMeshTransport() override {
        hub.nodes.erase(std::remove(hub.nodes.begin(), hub.nodes.end(), this), hub.nodes.end());
    }

    int newConn() {
        conns.push_back(Conn());
        return static_cast<int>(conns.size()) - 1;
    }

    int connect(const std::string&, uint16_t p) override { return connect(0x0100007fu, p); }
    int connect(uint32_t, uint16_t p) override {
        for (FakeMeshTransport* node : hub.nodes) {
            if (node->port != p || node == this)
                continue;
            const int mine = newConn();
            const int theirs = node->newConn();
            conns[mine] = {true, node, theirs};
            node->conns[theirs] = {true, this, mine};
            queue.push_back({TransportEvent::Type::Connected, mine, Channel::Reliable, {}});
            node->queue.push_back({TransportEvent::Type::Connected, theirs, Channel::Reliable, {}});
            return mine;
        }
        //Nobody there: the attempt fails later
        const int mine = newConn();
        queue.push_back({TransportEvent::Type::Disconnected, mine, Channel::Reliable, {}});
        return mine;
    }
    bool address(int connection, uint32_t& ipv4, uint16_t& p) const override {
        if (connection < 0 || connection >= static_cast<int>(conns.size()) || !conns[connection].open)
            return false;
        ipv4 = 0x0100007fu;
        p = conns[connection].other->port;
        return true;
    }
    uint16_t localPort() const override { return port; }

    void send(int connection, Channel channel, const uint8_t* data, size_t size) override {
        if (connection < 0 || connection >= static_cast<int>(conns.size()) || !conns[connection].open)
            return;
        bytesSent += size;
        if (channel == Channel::Reliable)
            biggestReliable = std::max(biggestReliable, size);
        const Conn& c = conns[connection];
        c.other->queue.push_back({TransportEvent::Type::Received, c.otherConn, channel, std::vector<uint8_t>(data, data + size)});
    }
    bool poll(TransportEvent& event) override {
        if (queue.empty())
            return false;
        Ev ev = std::move(queue.front());
        queue.pop_front();
        current = std::move(ev.data);
        event = TransportEvent();
        event.type = ev.type;
        event.connection = ev.connection;
        event.channel = ev.channel;
        event.data = current.data();
        event.size = current.size();
        return true;
    }
    void flush() override {}
    void disconnect(int connection) override {
        if (connection < 0 || connection >= static_cast<int>(conns.size()) || !conns[connection].open)
            return;
        Conn& c = conns[connection];
        c.open = false;
        c.other->conns[c.otherConn].open = false;
        c.other->queue.push_back({TransportEvent::Type::Disconnected, c.otherConn, Channel::Reliable, {}});
        queue.push_back({TransportEvent::Type::Disconnected, connection, Channel::Reliable, {}});
    }
    // The process dies: the others see their connections drop
    void crash() {
        for (int i = 0; i < static_cast<int>(conns.size()); i++)
            disconnect(i);
        queue.clear();
    }
};

struct Node {
    std::unique_ptr<FakeMeshTransport> transport;
    std::unique_ptr<NetLobby> lobby;
    std::vector<LobbyGameEvent> gameEvents;
    std::vector<LobbyEvent> events;
};

struct LobbyNet {
    FakeHub hub;
    std::vector<std::unique_ptr<Node>> nodes;
    uint32_t now = 1000;
    std::string downloadDir;
    std::string skinDir;    // node N: own skins in skinDir/nodeN/own, downloads in skinDir/nodeN/net

    Node& add(bool host, const std::string& name = "") {
        auto node = std::make_unique<Node>();
        node->transport = std::make_unique<FakeMeshTransport>(hub, static_cast<uint16_t>(7300 + nodes.size()));
        NetLobby::Options options;
        options.mesh.host = host;
        options.mesh.joinHost = "127.0.0.1";
        options.mesh.joinPort = 7300;
        options.mesh.buildId = "test";
        options.mesh.dataHash = 42;
        options.name = name;
        if (!downloadDir.empty())
            options.downloadDirs = {downloadDir + "/node" + std::to_string(nodes.size())};
        if (!skinDir.empty()) {
            options.skinSearchDirs = {skinDir + "/node" + std::to_string(nodes.size()) + "/own"};
            options.skinDownloadDirs = {skinDir + "/node" + std::to_string(nodes.size()) + "/net"};
        }
        node->lobby = std::make_unique<NetLobby>(*node->transport, options, now);
        nodes.push_back(std::move(node));
        return *nodes.back();
    }

    void step(int count = 1) {
        for (int i = 0; i < count; i++) {
            for (auto& node : nodes) {
                LobbyGameEvent event;
                while (node->lobby->poll(event, now)) {
                    event.data = nullptr;    //only valid until the next poll
                    node->gameEvents.push_back(event);
                }
                node->lobby->update(now);
                LobbyEvent e;
                while (node->lobby->pollEvent(e))
                    node->events.push_back(e);
            }
            now += 16;
        }
    }

    // Runs until every lobby is out of Joining (or a limit)
    void settle(int steps = 200) { step(steps); }
};

bool hasEvent(const Node& node, LobbyEvent::Type type, int peer = -2)
{
    return std::any_of(node.events.begin(), node.events.end(), [&](const LobbyEvent& e) { return e.type == type && (peer == -2 || e.peer == peer); });
}

bool hasGameEvent(const Node& node, LobbyGameEvent::Type type, int peer = -2)
{
    return std::any_of(node.gameEvents.begin(), node.gameEvents.end(), [&](const LobbyGameEvent& e) { return e.type == type && (peer == -2 || e.peer == peer); });
}

std::string tempDir(const std::string& name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("smw-test-" + name + "-" + std::to_string(std::rand()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::vector<uint8_t> testBytes(size_t size, uint8_t salt)
{
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; i++)
        bytes[i] = static_cast<uint8_t>((i * 31 + salt) ^ (i >> 7));
    return bytes;
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace


TEST_CASE("The automatic input delay grows with the round trip and its jitter, within 1..6") {
    CHECK(autoInputDelay(0, 0) == 0);
    CHECK(autoInputDelay(30, 10) == 0);       // LAN
    CHECK(autoInputDelay(140, 6) == 1);       // (70 + 6) / 16 -> 5 frames late, 4 to prediction
    CHECK(autoInputDelay(160, 25) == 3);      // (80 + 25) / 16 -> 7
    CHECK(autoInputDelay(240, 1) == 4);       // (120 + 1) / 16 -> 8
    CHECK(autoInputDelay(330, 15) == 6);      // 12 frames late: capped
    CHECK(autoInputDelay(2000, 100) == 6);
    CHECK(autoInputDelay(-5, -1) == 0);
    for (int rtt = 0; rtt < 400; rtt += 10)
        CHECK(autoInputDelay(static_cast<float>(rtt), 5) <= autoInputDelay(static_cast<float>(rtt + 10), 5));
}

TEST_CASE("RttEstimate averages the last samples") {
    RttEstimate e;
    e.add(100);
    CHECK(e.rttMs == doctest::Approx(100));
    CHECK(e.jitterMs == doctest::Approx(0));
    e.add(120);
    CHECK(e.rttMs == doctest::Approx(110));
    CHECK(e.jitterMs == doctest::Approx(10));
    for (int i = 0; i < RttEstimate::WINDOW; i++)
        e.add(50);
    CHECK(e.rttMs == doctest::Approx(50));    //the old samples are out of the window
    CHECK(e.samples == RttEstimate::WINDOW + 2);

    //A few hitches (a quarter of the window) don't count
    for (int i = 0; i < RttEstimate::WINDOW; i++)
        e.add(i % 4 == 0 ? 400.0f : 30.0f + 10.0f * static_cast<float>(i % 4));
    CHECK(e.rttMs == doctest::Approx(50).epsilon(0.01));
    CHECK(e.jitterMs == doctest::Approx(20.0f / 3).epsilon(0.01));
}

TEST_CASE("Peers join the lobby, get slots and names, and the host sees them ready") {
    LobbyNet net;
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    net.step(3);
    Node& b = net.add(false);
    net.settle();

    CHECK(host.lobby->phase() == NetLobby::Phase::Lobby);
    REQUIRE(a.lobby->phase() == NetLobby::Phase::Lobby);
    REQUIRE(b.lobby->phase() == NetLobby::Phase::Lobby);
    CHECK(a.lobby->localPeer() == 1);
    CHECK(b.lobby->localPeer() == 2);
    CHECK(hasEvent(a, LobbyEvent::Type::Joined));
    CHECK(hasEvent(host, LobbyEvent::Type::PeerJoined, 1));
    CHECK(hasEvent(host, LobbyEvent::Type::PeerJoined, 2));

    for (Node* n : {&host, &a, &b}) {
        CHECK(n->lobby->memberCount() == 3);
        CHECK(n->lobby->peer(0).name == "Hosty");
        CHECK(n->lobby->peer(1).name == "Alice");
        CHECK(n->lobby->peer(2).name == "Player 3");
        //Humans in peer order, each on its own team
        for (int s = 0; s < 3; s++) {
            CHECK(n->lobby->slot(s).kind == SlotKind::Human);
            CHECK(n->lobby->slot(s).peer == s);
            CHECK(n->lobby->slot(s).team == s);
        }
        CHECK(n->lobby->slot(3).kind == SlotKind::Empty);
    }

    //Every pair is connected (full mesh) and measures its round trip
    CHECK(a.lobby->mesh().peerConnected(2));
    CHECK(b.lobby->mesh().peerConnected(1));
    CHECK(host.lobby->pingsMeasured());
    CHECK(a.lobby->peer(0).pingMs >= 0);
    CHECK(a.lobby->peer(1).pingMs == -1);    //ourselves

    std::string why;
    CHECK_FALSE(host.lobby->canStart(why));
    CHECK(why.find("Alice") != std::string::npos);
    a.lobby->setReady(true);
    b.lobby->setReady(true);
    net.step(10);
    CHECK(host.lobby->peer(1).ready);
    CHECK(b.lobby->peer(1).ready);
    CHECK(host.lobby->canStart(why));

    //A match: every peer is in round 1. Ready flags of round 0 don't count.
    //Peer 2 finishes its match first and is ready again before the host
    //has finished: its new flag counts once the host is in round 1 too.
    a.lobby->setRound(1);
    b.lobby->setRound(1);
    a.lobby->clearReady();
    b.lobby->clearReady();
    b.lobby->setReady(true);
    net.step(10);
    CHECK_FALSE(host.lobby->peer(2).ready);    //the host is still in round 0
    host.lobby->setRound(1);
    net.step(10);
    CHECK_FALSE(host.lobby->peer(1).ready);
    CHECK_FALSE(a.lobby->peer(1).ready);
    CHECK(host.lobby->peer(2).ready);
    CHECK(a.lobby->peer(2).ready);
    CHECK_FALSE(host.lobby->canStart(why));

    //A new joiner learns the round from the host
    Node& c = net.add(false);
    net.settle();
    c.lobby->setReady(true);
    net.step(10);
    CHECK(host.lobby->peer(3).ready);
}

TEST_CASE("Slots: local players, CPU players, teams and colors") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.settle();

    a.lobby->setLocalPlayers(2);
    net.step(10);
    CHECK(host.lobby->slot(1).peer == 1);
    CHECK(host.lobby->slot(2).peer == 1);
    CHECK(host.lobby->slot(2).localIndex == 1);

    host.lobby->addCpu();
    host.lobby->addCpu();    //no room for a second one
    net.step(10);
    CHECK(a.lobby->slot(3).kind == SlotKind::Cpu);
    CHECK(host.lobby->slot(3).kind == SlotKind::Cpu);

    //A joiner changes its own slots only
    a.lobby->setSlotTeam(2, 0);
    a.lobby->setSlotColor(2, 3);
    a.lobby->setSlotTeam(0, 2);
    a.lobby->setSlotTeam(3, 2);
    net.step(10);
    CHECK(host.lobby->slot(2).team == 0);
    CHECK(host.lobby->slot(2).color == 3);
    CHECK(host.lobby->slot(0).team == 0);
    CHECK(a.lobby->slot(2).color == 3);

    //At most three players per team
    host.lobby->setSlotTeam(1, 0);
    host.lobby->setSlotTeam(3, 0);
    CHECK(host.lobby->slot(3).team != 0);

    //The host's CPU settings, then fewer CPUs
    host.lobby->setSlotColor(3, 1);
    CHECK(host.lobby->slot(3).color == 1);
    host.lobby->removeCpu(3);
    CHECK(host.lobby->slot(3).kind == SlotKind::Empty);
    host.lobby->setCpuCount(1);
    CHECK(host.lobby->slot(3).kind == SlotKind::Cpu);
    host.lobby->setCpuCount(0);
    CHECK(host.lobby->slot(3).kind == SlotKind::Empty);

    //All on one team: no match
    a.lobby->setReady(true);
    net.step(80);
    std::string why;
    CHECK_FALSE(host.lobby->canStart(why));
    CHECK(why.find("same team") != std::string::npos);
    host.lobby->setSlotTeam(1, 1);
    CHECK(host.lobby->canStart(why));
}

TEST_CASE("Joiners fill the places left to right, the CPUs move over; a team set up stays") {
    LobbyNet net;
    Node& host = net.add(true);
    net.settle();
    host.lobby->addCpu();
    host.lobby->addCpu();
    CHECK(host.lobby->slot(1).kind == SlotKind::Cpu);
    CHECK(host.lobby->slot(1).team == 1);

    //A joiner: the second place (green), the CPUs to the third and fourth
    Node& a = net.add(false);
    net.settle();
    net.step(10);
    CHECK(host.lobby->slot(1).peer == 1);
    CHECK(host.lobby->slot(1).team == 1);
    CHECK(host.lobby->slot(1).color == 1);
    CHECK(host.lobby->slot(2).kind == SlotKind::Cpu);
    CHECK(host.lobby->slot(2).team == 2);
    CHECK(host.lobby->slot(3).kind == SlotKind::Cpu);
    CHECK(host.lobby->slot(3).team == 3);
    CHECK(a.lobby->slot(1).team == 1);

    //Its second player: a CPU goes (no room), the rest stay in order
    a.lobby->setLocalPlayers(2);
    net.step(10);
    CHECK(host.lobby->slot(2).peer == 1);
    CHECK(host.lobby->slot(2).team == 2);
    CHECK(host.lobby->slot(3).kind == SlotKind::Cpu);
    CHECK(host.lobby->slot(3).team == 3);

    //Teams set up (2 v 2): kept as players come and go
    host.lobby->setSlotTeam(1, 0);
    host.lobby->setSlotTeam(3, 2);
    a.lobby->setLocalPlayers(1);
    net.step(10);
    CHECK(host.lobby->slot(1).team == 0);
}

TEST_CASE("CPUs only: the host may have no players of its own, for a game on its machine") {
    LobbyNet net;
    Node& host = net.add(true);
    net.settle();

    host.lobby->setLocalPlayers(0);
    host.lobby->setCpuCount(4);
    CHECK(host.lobby->peer(host.lobby->localPeer()).localPlayers == 0);
    for (int k = 0; k < 4; k++)
        CHECK(host.lobby->slot(k).kind == SlotKind::Cpu);
    host.lobby->setSlotTeam(1, 1);
    std::string why;
    CHECK(host.lobby->canStart(why));

    //One who joins plays: its players can't be none
    Node& a = net.add(false);
    net.settle();
    a.lobby->setLocalPlayers(0);
    CHECK(a.lobby->peer(a.lobby->localPeer()).localPlayers == 1);

    //With someone else in, the host's players are needed in the match too
    host.lobby->setCpuCount(1);
    a.lobby->setReady(true);
    net.step(80);
    CHECK_FALSE(host.lobby->canStart(why));
    CHECK(why.find("My players") != std::string::npos);
    host.lobby->setLocalPlayers(1);
    net.step(10);
    host.lobby->setSlotTeam(0, 0);
    host.lobby->setSlotTeam(1, 1);
    host.lobby->setSlotTeam(2, 2);
    CHECK(host.lobby->canStart(why));
}

TEST_CASE("Leaving, kicking, a lost peer and the host closing the lobby") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.step(3);
    Node& b = net.add(false);
    net.step(3);
    Node& c = net.add(false);
    net.settle();
    REQUIRE(host.lobby->memberCount() == 4);

    //Full: a fifth one is refused
    Node& d = net.add(false);
    net.settle();
    CHECK(d.lobby->phase() == NetLobby::Phase::Closed);
    CHECK(d.lobby->statusText().find("full") != std::string::npos);

    //A joiner leaves: removed everywhere
    a.lobby->leave("bye", false);
    net.step(10);
    CHECK(a.lobby->phase() == NetLobby::Phase::Closed);
    CHECK_FALSE(host.lobby->peer(1).present);
    CHECK_FALSE(b.lobby->peer(1).present);
    CHECK(hasEvent(b, LobbyEvent::Type::PeerLeft, 1));
    CHECK(hasGameEvent(host, LobbyGameEvent::Type::PeerGone, 1));
    CHECK(host.lobby->slot(0).peer == 0);
    CHECK(host.lobby->slot(1).peer == 2);    //slots move up

    //Kicked
    host.lobby->kick(2);
    net.step(10);
    CHECK(b.lobby->phase() == NetLobby::Phase::Closed);
    CHECK(b.lobby->statusText().find("removed") != std::string::npos);
    CHECK_FALSE(c.lobby->peer(2).present);
    CHECK_FALSE(c.lobby->mesh().peerConnected(2));

    //A new joiner gets a free id again
    Node& e = net.add(false, "Eve");
    net.settle();
    CHECK(e.lobby->localPeer() == 1);
    CHECK(c.lobby->peer(1).name == "Eve");
    CHECK(c.lobby->mesh().peerConnected(1));

    //A peer that crashes: the host removes it
    e.transport->crash();
    net.step(10);
    CHECK_FALSE(host.lobby->peer(1).present);
    CHECK(hasEvent(c, LobbyEvent::Type::PeerLeft, 1));

    //The host ends the lobby for everyone
    host.lobby->leave("closing", false, true);
    net.step(10);
    CHECK(c.lobby->phase() == NetLobby::Phase::Closed);
    CHECK(c.lobby->statusText() == "The host closed the lobby");
    CHECK(hasEvent(c, LobbyEvent::Type::Closed));
    const auto closed = std::find_if(c.gameEvents.begin(), c.gameEvents.end(), [](const LobbyGameEvent& ev) { return ev.type == LobbyGameEvent::Type::Closed; });
    REQUIRE(closed != c.gameEvents.end());
    CHECK(closed->ok);    //a running match would be finished first
    CHECK(c.lobby->closeCause() == NetLobby::CloseCause::Ended);
}

TEST_CASE("The host leaves: the lowest joiner hosts next, for everyone (HOST MIGRATION)") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.step(3);
    Node& b = net.add(false);
    net.settle();
    REQUIRE(host.lobby->memberCount() == 3);

    host.lobby->leave("bye", false);
    net.step(10);
    for (Node* n : {&a, &b}) {
        CHECK(n->lobby->phase() == NetLobby::Phase::Closed);
        CHECK(n->lobby->closeCause() == NetLobby::CloseCause::HostLeft);
        CHECK(n->lobby->successor() == 1);
    }
    CHECK(a.lobby->statusText().find("you host") != std::string::npos);
    std::string address;
    uint16_t port = 0;
    CHECK(b.lobby->peerAddress(1, address, port));
}

TEST_CASE("The host is lost: the joiners agree on who is still here, then on the next host") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.step(3);
    Node& b = net.add(false);
    net.step(3);
    Node& c = net.add(false);
    net.settle();
    REQUIRE(host.lobby->memberCount() == 4);

    host.transport->crash();
    net.step(10);
    for (Node* n : {&a, &b, &c}) {
        CHECK(n->lobby->phase() == NetLobby::Phase::Closed);
        CHECK(n->lobby->closeCause() == NetLobby::CloseCause::HostLost);
        CHECK(n->lobby->successor() == 1);
    }
}

TEST_CASE("A joiner alone with a lost host tries to get back in, no new host") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.settle();
    host.transport->crash();
    net.step(10);
    CHECK(a.lobby->phase() == NetLobby::Phase::Closed);
    CHECK(a.lobby->closeCause() == NetLobby::CloseCause::HostLost);
    CHECK(a.lobby->successor() == -1);
}

TEST_CASE("Joining during a match; the host crashing then hands the lobby on") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.settle();

    //(a match being played can be joined: NetGame's joining a match in progress)
    host.lobby->setPhase(NetLobby::Phase::InMatch);
    Node& late = net.add(false);
    net.settle();
    CHECK(late.lobby->phase() == NetLobby::Phase::Lobby);
    CHECK(late.lobby->matchBeingPlayed());

    host.transport->crash();
    net.step(10);
    for (Node* n : {&a, &late}) {
        CHECK(n->lobby->phase() == NetLobby::Phase::Closed);
        CHECK(n->lobby->closeCause() == NetLobby::CloseCause::HostLost);
        CHECK(n->lobby->successor() == 1);
        const auto closed = std::find_if(n->gameEvents.begin(), n->gameEvents.end(), [](const LobbyGameEvent& ev) { return ev.type == LobbyGameEvent::Type::Closed; });
        REQUIRE(closed != n->gameEvents.end());
        CHECK_FALSE(closed->ok);
    }
    CHECK(a.lobby->statusText() == "The host is gone: you host the lobby now");
}

TEST_CASE("A different build or game data is refused at the handshake") {
    LobbyNet net;
    net.add(true);
    Node& a = net.add(false);
    net.step(1);
    //Patch the joiner's options: a lobby with another data hash
    FakeMeshTransport other(net.hub, 7399);
    NetLobby::Options options;
    options.mesh.joinHost = "127.0.0.1";
    options.mesh.joinPort = 7300;
    options.mesh.buildId = "test";
    options.mesh.dataHash = 43;
    NetLobby wrong(other, options, net.now);
    LobbyGameEvent event;
    for (int i = 0; i < 20; i++) {
        while (wrong.poll(event, net.now)) {}
        wrong.update(net.now);
        net.step();
    }
    CHECK(wrong.phase() == NetLobby::Phase::Closed);
    CHECK(wrong.statusText().find("game data") != std::string::npos);
    CHECK(a.lobby->phase() == NetLobby::Phase::Lobby);
}

TEST_CASE("Map transfer: chunks, hash check and a saved file") {
    LobbyNet net;
    net.downloadDir = tempDir("maptransfer");
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.settle();
    host.lobby->setPhase(NetLobby::Phase::Starting);
    a.lobby->setPhase(NetLobby::Phase::Starting);

    const auto file = std::make_shared<const std::vector<uint8_t>>(testBytes(40500, 1));
    const uint64_t hash = net::fnv1a64(file->data(), file->size());
    host.lobby->sendMap(1, 1, file, hash, "../../evil/Creator_Big Map.map");
    CHECK(host.lobby->mapTransfer().active);
    net.step(1);
    net.step(1);
    //Paced: a few chunks a frame, never more than ~1 KB each
    CHECK(a.lobby->mapTransfer().active);
    CHECK(a.lobby->mapTransfer().bytesTotal == 40500);
    CHECK(host.transport->biggestReliable <= MAP_CHUNK_BYTES + 16);
    net.step(20);
    CHECK_FALSE(a.lobby->mapTransfer().active);

    const auto received = std::find_if(a.gameEvents.begin(), a.gameEvents.end(), [](const LobbyGameEvent& ev) { return ev.type == LobbyGameEvent::Type::MapReceived; });
    REQUIRE(received != a.gameEvents.end());
    CHECK(received->ok);
    //Our own name, in our own folder
    const std::filesystem::path path(received->text);
    CHECK(path.parent_path() == std::filesystem::path(net.downloadDir + "/node1"));
    CHECK(path.filename().string() == "Creator_Big Map.map");
    CHECK(readFile(received->text) == *file);
    CHECK(findMapByHash(net.downloadDir + "/node1", hash) == received->text);
    CHECK(findMapByHash(net.downloadDir + "/node1", hash + 1).empty());

    std::filesystem::remove_all(net.downloadDir);
}

TEST_CASE("Map transfer: a damaged or too large map is refused") {
    LobbyNet net;
    net.downloadDir = tempDir("mapbad");
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.settle();

    const auto file = std::make_shared<const std::vector<uint8_t>>(testBytes(2500, 2));
    host.lobby->sendMap(1, 1, file, 12345, "map");    //wrong hash
    net.step(10);
    const auto received = std::find_if(a.gameEvents.begin(), a.gameEvents.end(), [](const LobbyGameEvent& ev) { return ev.type == LobbyGameEvent::Type::MapReceived; });
    REQUIRE(received != a.gameEvents.end());
    CHECK_FALSE(received->ok);
    CHECK(received->text.find("damaged") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(net.downloadDir + "/node1/map.map"));

    //A MAP_BEGIN announcing more than the cap is refused before anything is received
    a.gameEvents.clear();
    uint8_t msg[64];
    net::ByteWriter w(msg, sizeof(msg));
    w.u8(static_cast<uint8_t>(net::MsgType::MapBegin));
    w.u8(2);
    w.u32(MAP_MAX_BYTES + 1);
    w.u64(1);
    w.str("huge");
    host.transport->send(host.lobby->mesh().connectionOf(1), Channel::Reliable, w.data(), w.size());
    net.step(2);
    const auto huge = std::find_if(a.gameEvents.begin(), a.gameEvents.end(), [](const LobbyGameEvent& ev) { return ev.type == LobbyGameEvent::Type::MapReceived; });
    REQUIRE(huge != a.gameEvents.end());
    CHECK_FALSE(huge->ok);
    CHECK(huge->text.find("too large") != std::string::npos);

    std::filesystem::remove_all(net.downloadDir);
}

TEST_CASE("Downloaded map names: no paths, unique, existing files kept") {
    CHECK(safeMapFileName("../../etc/passwd") == "passwd");
    CHECK(safeMapFileName("..\\..\\Windows\\win.ini") == "win");
    CHECK(safeMapFileName("C:evil.map") == "evil");
    CHECK(safeMapFileName("maps/Creator_My Map.map") == "Creator_My Map");
    CHECK(safeMapFileName("") == "map");
    CHECK(safeMapFileName("..") == "map");
    CHECK(safeMapFileName("/") == "map");
    CHECK(safeMapFileName("a\x01*?<>|\"b") == "ab");
    CHECK(safeMapFileName(std::string(100, 'x')).size() == 40);

    const std::string dir = tempDir("mapnames");
    const std::vector<uint8_t> one = testBytes(300, 3);
    const std::vector<uint8_t> two = testBytes(300, 4);
    const uint64_t hashOne = net::fnv1a64(one.data(), one.size());
    const uint64_t hashTwo = net::fnv1a64(two.data(), two.size());
    std::string path;
    std::string error;

    REQUIRE(saveDownloadedMap(dir, "x/Same.map", one, hashOne, path, error));
    CHECK(std::filesystem::path(path).filename() == "Same.map");
    const std::string first = path;

    //The same contents again: the same file
    REQUIRE(saveDownloadedMap(dir, "Same", one, hashOne, path, error));
    CHECK(path == first);

    //Other contents, same name: the existing map is kept
    REQUIRE(saveDownloadedMap(dir, "Same", two, hashTwo, path, error));
    CHECK(std::filesystem::path(path).filename() == "Same (2).map");
    CHECK(readFile(first) == one);
    CHECK(readFile(path) == two);
    CHECK(findMapByHash(dir, hashTwo) == path);

    //Wrong hash: nothing written
    CHECK_FALSE(saveDownloadedMap(dir, "Other", one, hashTwo, path, error));
    CHECK_FALSE(std::filesystem::exists(dir + "/Other.map"));
    //Nothing ends up outside the folder
    REQUIRE(saveDownloadedMap(dir, "../escape", one, hashOne, path, error));
    CHECK(std::filesystem::path(path).parent_path() == std::filesystem::path(dir));
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(dir).parent_path() / "escape.map"));

    std::filesystem::remove_all(dir);
}


// ---- skins

namespace {

// Bytes that look like a PNG (the signature is checked), different for every salt
std::vector<uint8_t> fakeSkin(size_t size, uint8_t salt)
{
    std::vector<uint8_t> bytes = testBytes(size, salt);
    const uint8_t png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::copy(png, png + 8, bytes.begin());
    return bytes;
}

std::string writeSkin(const std::string& dir, const std::string& file, const std::vector<uint8_t>& bytes)
{
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/" + file;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("Downloaded skin names: no paths, unique, existing files kept, only images") {
    CHECK(safeSkinFileName("../../etc/passwd") == "passwd");
    CHECK(safeSkinFileName("C:\\evil\\Mario.png") == "Mario");
    CHECK(safeSkinFileName("a\x01*?<>|\"b") == "ab");
    CHECK(safeSkinFileName("") == "skin");
    CHECK(safeSkinFileName("..") == "skin");
    CHECK(safeSkinFileName(std::string(100, 'x')).size() == 40);

    const std::string dir = tempDir("skinnames");
    const std::vector<uint8_t> one = fakeSkin(900, 1);
    const std::vector<uint8_t> two = fakeSkin(900, 2);
    const uint64_t hashOne = net::fnv1a64(one.data(), one.size());
    const uint64_t hashTwo = net::fnv1a64(two.data(), two.size());
    std::string path;
    std::string error;

    REQUIRE(saveDownloadedSkin(dir, "../Mario", one, hashOne, path, error));
    CHECK(std::filesystem::path(path).parent_path() == std::filesystem::path(dir));
    CHECK(std::filesystem::path(path).filename() == "Mario.png");
    const std::string first = path;
    REQUIRE(saveDownloadedSkin(dir, "Mario", one, hashOne, path, error));    //same contents: reused
    CHECK(path == first);
    REQUIRE(saveDownloadedSkin(dir, "Mario", two, hashTwo, path, error));    //same name, other contents: never overwritten
    CHECK(std::filesystem::path(path).filename() == "Mario (2).png");
    CHECK(readFile(first) == one);
    CHECK(readFile(path) == two);

    CHECK_FALSE(saveDownloadedSkin(dir, "Wrong", one, hashTwo, path, error));    //wrong hash
    CHECK_FALSE(std::filesystem::exists(dir + "/Wrong.png"));
    const std::vector<uint8_t> text = testBytes(500, 9);                          //not an image
    CHECK_FALSE(saveDownloadedSkin(dir, "Text", text, net::fnv1a64(text.data(), text.size()), path, error));
    const std::vector<uint8_t> huge = fakeSkin(SKIN_MAX_BYTES + 1, 3);            //too large
    CHECK_FALSE(saveDownloadedSkin(dir, "Huge", huge, net::fnv1a64(huge.data(), huge.size()), path, error));
    std::vector<uint8_t> bmp = testBytes(300, 4);
    bmp[0] = 'B';
    bmp[1] = 'M';
    REQUIRE(saveDownloadedSkin(dir, "Old", bmp, net::fnv1a64(bmp.data(), bmp.size()), path, error));
    CHECK(std::filesystem::path(path).extension() == ".bmp");

    std::filesystem::remove_all(dir);
}

// The same file, whatever separators the paths were built with ('\\' and '/' on Windows)
bool samePath(const std::string& a, const std::string& b)
{
    return std::filesystem::path(a).lexically_normal() == std::filesystem::path(b).lexically_normal();
}

TEST_CASE("Skins are announced, downloaded in chunks, hash checked and shown for every slot") {
    LobbyNet net;
    net.skinDir = tempDir("skins");
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    Node& b = net.add(false, "Bob");

    const std::vector<uint8_t> hostSkin = fakeSkin(3500, 10);
    const std::vector<uint8_t> aliceSkin = fakeSkin(2100, 20);    //Alice has a different "Cool" than the host
    //Bob has the host's skin already, under another folder name
    const std::string hostFile = writeSkin(net.skinDir + "/node0/own", "Cool.png", hostSkin);
    const std::string aliceFile = writeSkin(net.skinDir + "/node1/own", "Cool.png", aliceSkin);
    writeSkin(net.skinDir + "/node2/own", "HostsSkinCopy.png", hostSkin);
    const std::string bobFile = writeSkin(net.skinDir + "/node2/own", "Bobby.png", fakeSkin(1200, 30));

    host.lobby->setLocalSkins({{"Cool", hostFile}});
    a.lobby->setLocalSkins({{"Cool", aliceFile}});
    b.lobby->setLocalSkins({{"Bobby", bobFile}});
    net.settle(300);

    //Slots: host 0, Alice 1, Bob 2
    REQUIRE(host.lobby->slot(1).peer == 1);
    REQUIRE(host.lobby->slot(2).peer == 2);
    //Our own skin is our own file
    CHECK(host.lobby->slotSkinPath(0) == hostFile);
    CHECK(a.lobby->slotSkinPath(1) == aliceFile);

    //Alice has another file called Cool: she downloaded the host's, with its own name, next to hers, and never overwrote hers
    const std::string aliceGotHost = a.lobby->slotSkinPath(0);
    CHECK(samePath(aliceGotHost, net.skinDir + "/node1/net/Cool.png"));
    CHECK(readFile(aliceGotHost) == hostSkin);
    CHECK(readFile(aliceFile) == aliceSkin);
    //The host got Alice's, which is not its own Cool
    CHECK(samePath(host.lobby->slotSkinPath(1), net.skinDir + "/node0/net/Cool.png"));
    CHECK(readFile(host.lobby->slotSkinPath(1)) == aliceSkin);
    CHECK(readFile(hostFile) == hostSkin);
    //Bob already had the host's skin (found by its hash): it downloaded nothing of it
    CHECK(samePath(b.lobby->slotSkinPath(0), net.skinDir + "/node2/own/HostsSkinCopy.png"));
    CHECK(readFile(b.lobby->slotSkinPath(1)) == aliceSkin);
    CHECK(samePath(a.lobby->slotSkinPath(2), net.skinDir + "/node1/net/Bobby.png"));
    CHECK(samePath(host.lobby->slotSkinPath(2), net.skinDir + "/node0/net/Bobby.png"));
    //The only "Cool" Bob downloaded is Alice's, not the host's (which it had)
    CHECK(readFile(net.skinDir + "/node2/net/Cool.png") == aliceSkin);
    CHECK_FALSE(std::filesystem::exists(net.skinDir + "/node2/net/Cool (2).png"));
    CHECK_FALSE(a.lobby->slotSkinDownloading(0));
    //A CPU player has no skin
    host.lobby->addCpu();
    net.step(20);
    CHECK(host.lobby->slotSkinPath(3).empty());
    //The chunks stay small
    CHECK(host.transport->biggestReliable <= SKIN_CHUNK_BYTES + 32);

    //A joiner that changes its skin announces the new one
    const std::string newFile = writeSkin(net.skinDir + "/node1/own", "Other.png", fakeSkin(1800, 40));
    a.lobby->setLocalSkins({{"Other", newFile}});
    net.step(60);
    CHECK(readFile(host.lobby->slotSkinPath(1)) == readFile(newFile));
    CHECK(host.lobby->skinRevision() > 0);

    std::filesystem::remove_all(net.skinDir);
}

TEST_CASE("Skins: nothing unrequested, damaged or too large is accepted, and the lobby never waits for one") {
    LobbyNet net;
    net.skinDir = tempDir("skinsbad");
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    const std::string hostFile = writeSkin(net.skinDir + "/node0/own", "Cool.png", fakeSkin(2500, 50));
    host.lobby->setLocalSkins({{"Cool", hostFile}});
    net.step(1);    //not announced to Alice yet, she lacks it
    net.settle(5);

    const auto sendTo = [&](Node& from, int peer, const net::ByteWriter& w) {
        from.transport->send(from.lobby->mesh().connectionOf(peer), Channel::Reliable, w.data(), w.size());
    };
    uint8_t buffer[1200];

    //An unrequested SKIN_BEGIN and its chunk (the host never announced this hash)
    {
        net::ByteWriter w(buffer, sizeof(buffer));
        w.u8(static_cast<uint8_t>(net::MsgType::SkinBegin));
        w.u64(777);
        w.u32(300);
        w.str("../../evil");
        sendTo(host, 1, w);
        const std::vector<uint8_t> data = fakeSkin(300, 5);
        net::ByteWriter c(buffer, sizeof(buffer));
        c.u8(static_cast<uint8_t>(net::MsgType::SkinChunk));
        c.u64(777);
        c.u32(0);
        c.u16(300);
        c.bytes(data.data(), 300);
        sendTo(host, 1, c);
        net.step(3);
        CHECK(a.lobby->slotSkinPath(0) != net.skinDir + "/node1/net/evil.png");
        CHECK_FALSE(std::filesystem::exists(net.skinDir + "/node1/net/evil.png"));
    }
    //An announcement of a skin above the cap is ignored (nothing is requested)
    {
        net::ByteWriter w(buffer, sizeof(buffer));
        w.u8(static_cast<uint8_t>(net::MsgType::SkinInfo));
        w.u8(1);
        w.str("Huge");
        w.u64(99);
        w.u32(SKIN_MAX_BYTES + 1);
        sendTo(host, 1, w);
        net.step(3);
        CHECK(a.lobby->slotSkinPath(0).empty());
        CHECK_FALSE(a.lobby->slotSkinDownloading(0));
    }
    //An announced skin whose chunks carry other data: damaged, not saved, and the lobby carries on
    {
        const std::vector<uint8_t> data = fakeSkin(1500, 6);
        net::ByteWriter w(buffer, sizeof(buffer));
        w.u8(static_cast<uint8_t>(net::MsgType::SkinInfo));
        w.u8(1);
        w.str("Fake");
        w.u64(4242);    //not the hash of what follows
        w.u32(1500);
        sendTo(host, 1, w);
        net.step(3);    //Alice requests it; the host has no such skin and sends nothing
        CHECK(a.lobby->slotSkinDownloading(0));
        net::ByteWriter b(buffer, sizeof(buffer));
        b.u8(static_cast<uint8_t>(net::MsgType::SkinBegin));
        b.u64(4242);
        b.u32(1500);
        b.str("Fake");
        sendTo(host, 1, b);
        for (uint32_t offset = 0; offset < 1500; offset += 1000) {
            const uint16_t len = static_cast<uint16_t>(std::min<uint32_t>(1000, 1500 - offset));
            net::ByteWriter c(buffer, sizeof(buffer));
            c.u8(static_cast<uint8_t>(net::MsgType::SkinChunk));
            c.u64(4242);
            c.u32(offset);
            c.u16(len);
            c.bytes(data.data() + offset, len);
            sendTo(host, 1, c);
        }
        net.step(5);
        CHECK(a.lobby->slotSkinPath(0).empty());
        CHECK_FALSE(std::filesystem::exists(net.skinDir + "/node1/net/Fake.png"));
    }
    CHECK(a.lobby->phase() == NetLobby::Phase::Lobby);
    CHECK(host.lobby->phase() == NetLobby::Phase::Lobby);

    std::filesystem::remove_all(net.skinDir);
}

TEST_CASE("The mode id and goal reach every peer for the lobby screen") {
    LobbyNet net;
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    net.settle();
    CHECK(a.lobby->modeId() == -1);

    host.lobby->setMatchInfo("Classic", "Block Piles");
    host.lobby->setMatchDetail(0, 10, "Lives");
    net.step(20);
    CHECK(a.lobby->modeId() == 0);
    CHECK(a.lobby->goal() == 10);
    CHECK(a.lobby->goalName() == "Lives");
    CHECK(a.lobby->modeName() == "Classic");

    //A joiner that comes later gets it too
    Node& b = net.add(false, "Bob");
    net.settle();
    CHECK(b.lobby->goal() == 10);
    host.lobby->setMatchDetail(1000, 50, "Coins");
    net.step(20);
    CHECK(a.lobby->modeId() == 1000);
    CHECK(b.lobby->goalName() == "Coins");
}


TEST_CASE("A joiner can't change another player's slot or the host's lobby settings") {
    LobbyNet net;
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    Node& b = net.add(false, "Bob");
    net.settle();
    REQUIRE(host.lobby->slot(2).peer == 2);

    const LobbySlot hostSlot = host.lobby->slot(0);
    const LobbySlot bobSlot = host.lobby->slot(2);
    const int delay = host.lobby->inputDelay();

    //Alice asks for the slots of the host and of Bob, for CPU players, a kick and the input delay
    a.lobby->setSlotTeam(0, (hostSlot.team + 1) & 3);
    a.lobby->setSlotColor(0, (hostSlot.color + 1) & 3);
    a.lobby->setSlotTeam(2, (bobSlot.team + 1) & 3);
    a.lobby->setSlotColor(2, (bobSlot.color + 1) & 3);
    a.lobby->addCpu();
    a.lobby->setCpuCount(2);
    a.lobby->kick(2);
    a.lobby->setInputDelay(delay == 5 ? 4 : 5);
    net.step(30);

    CHECK(host.lobby->slot(0).team == hostSlot.team);
    CHECK(host.lobby->slot(0).color == hostSlot.color);
    CHECK(host.lobby->slot(2).team == bobSlot.team);
    CHECK(host.lobby->slot(2).color == bobSlot.color);
    CHECK(host.lobby->slot(3).kind == SlotKind::Empty);
    CHECK(host.lobby->peer(2).present);
    CHECK(b.lobby->phase() == NetLobby::Phase::Lobby);
    CHECK(host.lobby->inputDelay() == delay);
    CHECK(b.lobby->slot(2).team == bobSlot.team);

    //Her own slot is hers
    a.lobby->setSlotTeam(1, 3);
    net.step(30);
    CHECK(host.lobby->slot(1).team == 3);
    CHECK(b.lobby->slot(1).team == 3);
}

TEST_CASE("Match settings: the host's, unless it lets everyone change them") {
    LobbyNet net;
    Node& host = net.add(true, "Hosty");
    Node& a = net.add(false, "Alice");
    net.settle();

    const auto settingRequests = [](const Node& node) {
        return std::count_if(node.gameEvents.begin(), node.gameEvents.end(), [](const LobbyGameEvent& e) { return e.type == LobbyGameEvent::Type::SettingsRequest; });
    };

    //By default only the host
    CHECK_FALSE(host.lobby->settingsEveryone());
    CHECK(host.lobby->canChangeSettings());
    CHECK_FALSE(a.lobby->canChangeSettings());
    a.lobby->requestSetting(NetLobby::SETTING_GOAL, 20, "");
    a.lobby->requestSetting(NetLobby::SETTING_MAP, 0, "Block Piles");
    net.step(30);
    CHECK(settingRequests(host) == 0);

    //A joiner can't switch it on, and the host's own requests go nowhere
    a.lobby->setSettingsEveryone(true);
    host.lobby->requestSetting(NetLobby::SETTING_GOAL, 5, "");
    net.step(30);
    CHECK_FALSE(host.lobby->settingsEveryone());
    CHECK_FALSE(a.lobby->settingsEveryone());
    CHECK(settingRequests(host) == 0);

    //The host lets everyone: everybody knows, and the requests reach the host with their sender
    host.lobby->setSettingsEveryone(true);
    net.step(30);
    CHECK(host.lobby->settingsEveryone());
    CHECK(a.lobby->settingsEveryone());
    CHECK(a.lobby->canChangeSettings());

    a.lobby->requestSetting(NetLobby::SETTING_MODE, 3, "");
    a.lobby->requestSetting(NetLobby::SETTING_GOAL, 20, "");
    a.lobby->requestSetting(NetLobby::SETTING_MAP, 0, "Block Piles");
    a.lobby->requestSetting(99, 1, "");    //(not a setting)
    net.step(30);
    REQUIRE(settingRequests(host) == 3);
    std::vector<LobbyGameEvent> requests;
    for (const LobbyGameEvent& e : host.gameEvents)
        if (e.type == LobbyGameEvent::Type::SettingsRequest)
            requests.push_back(e);
    CHECK(requests[0].peer == 1);
    CHECK(requests[0].size == NetLobby::SETTING_MODE);
    CHECK(requests[0].value == 3);
    CHECK(requests[1].size == NetLobby::SETTING_GOAL);
    CHECK(requests[1].value == 20);
    CHECK(requests[2].size == NetLobby::SETTING_MAP);
    CHECK(requests[2].text == "Block Piles");

    //A joiner that comes later is told, too; and the host takes it back
    Node& b = net.add(false, "Bob");
    net.settle();
    CHECK(b.lobby->settingsEveryone());
    host.lobby->setSettingsEveryone(false);
    net.step(30);
    CHECK_FALSE(a.lobby->settingsEveryone());
    CHECK_FALSE(b.lobby->canChangeSettings());
    a.lobby->requestSetting(NetLobby::SETTING_GOAL, 30, "");
    net.step(30);
    CHECK(settingRequests(host) == 3);
}

TEST_CASE("Rematch votes: after a match, each member's vote reaches everyone; a new match clears them") {
    LobbyNet net;
    Node& host = net.add(true);
    Node& a = net.add(false);
    net.step(3);
    Node& b = net.add(false);
    net.settle();

    //(nothing to vote on before a match)
    CHECK_FALSE(a.lobby->canVote());
    for (Node* n : {&host, &a, &b})
        n->lobby->setRound(1);
    net.step(10);
    REQUIRE(a.lobby->canVote());

    a.lobby->setVote(NetLobby::VOTE_REMATCH);
    host.lobby->setVote(NetLobby::VOTE_NEXT_MAP);
    net.step(20);
    int rematch = 0, next = 0, waiting = 0;
    for (Node* n : {&host, &a, &b}) {
        CHECK(n->lobby->vote(1) == NetLobby::VOTE_REMATCH);
        CHECK(n->lobby->vote(0) == NetLobby::VOTE_NEXT_MAP);
        n->lobby->voteCount(rematch, next, waiting);
        CHECK(rematch == 1);
        CHECK(next == 1);
        CHECK(waiting == 1);
    }
    CHECK(a.lobby->peer(1).ready);    //(a vote makes a joiner ready)
    CHECK(host.lobby->peer(1).ready);

    b.lobby->setVote(NetLobby::VOTE_REMATCH);
    net.step(20);
    host.lobby->voteCount(rematch, next, waiting);
    CHECK(rematch == 2);
    CHECK(waiting == 0);

    //A vote for another round doesn't count; a new match clears them
    for (Node* n : {&host, &a, &b})
        n->lobby->setRound(2);
    net.step(10);
    host.lobby->voteCount(rematch, next, waiting);
    CHECK(rematch + next == 0);
    CHECK(a.lobby->vote(1) == NetLobby::VOTE_NONE);
}

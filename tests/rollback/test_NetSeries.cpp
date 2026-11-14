#include "doctest.h"

#include "NetSeries.h"

#include <vector>

using namespace rollback;
using net::ByteReader;
using net::ByteWriter;

namespace {

template <typename Write>
std::vector<uint8_t> written(Write&& write)
{
    std::vector<uint8_t> buffer(4096);
    ByteWriter w(buffer.data(), buffer.size());
    write(w);
    REQUIRE(w.ok());
    buffer.resize(w.size());
    return buffer;
}

ByteReader readerAfterType(const std::vector<uint8_t>& bytes)
{
    return ByteReader(bytes.data() + 1, bytes.size() - 1);
}

// 4 slots: 0 host, 1 and 2 peer 1, 3 a CPU; teams {0}, {1, 2}, {3}
SeriesSlots exampleSlots()
{
    SeriesSlots slots;
    slots.peer = {{0, 1, 1, -1}};
    slots.control = {{1, 1, 1, 2}};
    slots.teamCount = 3;
    slots.teamIds[0][0] = 0;
    slots.teamIds[1][0] = 1;
    slots.teamIds[1][1] = 2;
    slots.teamIds[2][0] = 3;
    slots.teamCounts[0] = 1;
    slots.teamCounts[1] = 2;
    slots.teamCounts[2] = 1;
    return slots;
}

} // namespace

TEST_CASE("Series messages survive a round trip")
{
    SeriesBeginMsg begin;
    begin.matchType = 2;
    begin.tournamentGames = 5;
    begin.file = "Mario.txt";
    begin.fileHash = 0x1122334455667788ull;
    begin.seed = 0xdeadbeef;
    begin.slotPeer = {{0, 1, -1, -1}};
    begin.control = {{1, 1, 2, 0}};
    begin.color = {{0, 1, 2, 3}};
    begin.teamCount = 3;
    begin.teamIds[0][0] = 0;
    begin.teamIds[1][0] = 1;
    begin.teamIds[2][0] = 2;
    begin.teamCounts[0] = begin.teamCounts[1] = begin.teamCounts[2] = 1;
    begin.settings = {1, 2, 3, 4, 5};
    const auto bytes = written([&](ByteWriter& w) { writeSeriesBegin(w, begin); });
    CHECK(bytes[0] == static_cast<uint8_t>(net::MsgType::SeriesBegin));
    SeriesBeginMsg read;
    ByteReader r = readerAfterType(bytes);
    REQUIRE(readSeriesBegin(r, read));
    CHECK(read.matchType == 2);
    CHECK(read.tournamentGames == 5);
    CHECK(read.file == "Mario.txt");
    CHECK(read.fileHash == begin.fileHash);
    CHECK(read.seed == begin.seed);
    CHECK(read.slotPeer == begin.slotPeer);
    CHECK(read.control == begin.control);
    CHECK(read.color == begin.color);
    CHECK(read.teamCount == 3);
    CHECK(read.teamCounts[1] == 1);
    CHECK(read.settings == begin.settings);

    //Truncated or with a bad team: refused
    for (size_t cut = 1; cut < bytes.size(); cut++) {
        ByteReader shortReader(bytes.data() + 1, cut - 1);
        SeriesBeginMsg m;
        CHECK_FALSE(readSeriesBegin(shortReader, m));
    }
    begin.teamCounts[0] = 4;
    const auto bad = written([&](ByteWriter& w) { writeSeriesBegin(w, begin); });
    ByteReader badReader = readerAfterType(bad);
    CHECK_FALSE(readSeriesBegin(badReader, read));

    SeriesScreenMsg screen {SeriesScreen::BonusWheel, 513, 0x5};
    const auto screenBytes = written([&](ByteWriter& w) { writeSeriesScreen(w, screen); });
    SeriesScreenMsg screenRead;
    ByteReader sr = readerAfterType(screenBytes);
    REQUIRE(readSeriesScreen(sr, screenRead));
    CHECK(screenRead.screen == SeriesScreen::BonusWheel);
    CHECK(screenRead.step == 513);
    CHECK(screenRead.humanMask == 0x5);
    std::vector<uint8_t> badScreen = screenBytes;
    badScreen[1] = 99;
    ByteReader bsr = readerAfterType(badScreen);
    CHECK_FALSE(readSeriesScreen(bsr, screenRead));

    const auto endBytes = written([&](ByteWriter& w) { writeSeriesEnd(w, "the host left"); });
    std::string reason;
    ByteReader er = readerAfterType(endBytes);
    REQUIRE(readSeriesEnd(er, reason));
    CHECK(reason == "the host left");

    const auto pressBytes = written([&](ByteWriter& w) { writeWheelPress(w, 77); });
    uint16_t step = 0;
    ByteReader pr = readerAfterType(pressBytes);
    REQUIRE(readWheelPress(pr, step));
    CHECK(step == 77);

    WheelStopMsg stop {9, 1.25f, 0.125f, 0.0009765625f, 17};
    const auto stopBytes = written([&](ByteWriter& w) { writeWheelStop(w, stop); });
    WheelStopMsg stopRead;
    ByteReader str = readerAfterType(stopBytes);
    REQUIRE(readWheelStop(str, stopRead));
    CHECK(stopRead.step == 9);
    CHECK(stopRead.angle == 1.25f);
    CHECK(stopRead.speed == 0.125f);
    CHECK(stopRead.winddown == 0.0009765625f);
    CHECK(stopRead.powerup == 17);

    WorldInputMsg input {3, 100000, 0x6, {{0, 7, 9, 0}}};
    const auto inputBytes = written([&](ByteWriter& w) { writeWorldInput(w, input); });
    WorldInputMsg inputRead;
    ByteReader ir = readerAfterType(inputBytes);
    REQUIRE(readWorldInput(ir, inputRead));
    CHECK(inputRead.epoch == 3);
    CHECK(inputRead.tick == 100000);
    CHECK(inputRead.slotMask == 0x6);
    CHECK(inputRead.keys == input.keys);

    WorldTickMsg tick {4, 12, 0x7, {{1, 2, 3, 4}}};
    const auto tickBytes = written([&](ByteWriter& w) { writeWorldTick(w, tick); });
    WorldTickMsg tickRead;
    ByteReader tr = readerAfterType(tickBytes);
    REQUIRE(readWorldTick(tr, tickRead));
    CHECK(tickRead.epoch == 4);
    CHECK(tickRead.tick == 12);
    CHECK(tickRead.humanMask == 0x7);
    CHECK(tickRead.keys == tick.keys);
}

TEST_CASE("Series slots: teams, humans, lost peers")
{
    SeriesSlots slots = exampleSlots();
    CHECK(slots.slotsOf(0) == 0x1);
    CHECK(slots.slotsOf(1) == 0x6);
    CHECK(slots.humanMask() == 0x7);
    CHECK(slots.peerInTeam(1, 1));
    CHECK_FALSE(slots.peerInTeam(0, 1));
    CHECK_FALSE(slots.peerInTeam(1, 2));
    CHECK(slots.teamHasHuman(1));
    CHECK_FALSE(slots.teamHasHuman(2));
    CHECK_FALSE(slots.peerInTeam(1, 7));

    CHECK(slots.dropPeer(1));
    CHECK_FALSE(slots.dropPeer(1));
    CHECK(slots.control[1] == 2);
    CHECK(slots.control[2] == 2);
    CHECK(slots.peer[1] == -1);
    CHECK(slots.humanMask() == 0x1);
    CHECK_FALSE(slots.teamHasHuman(1));
    CHECK_FALSE(slots.peerInTeam(1, 1));
}

TEST_CASE("World lockstep: every peer runs the same ticks")
{
    const SeriesSlots slots = exampleSlots();
    WorldLockstep host;
    WorldLockstep joiner;
    host.begin(5, true, 0, slots, 3);
    joiner.begin(5, false, 1, slots, 4);

    std::vector<WorldTickMsg> hostTicks;
    std::vector<WorldTickMsg> joinerTicks;
    std::vector<WorldInputMsg> inFlight;    //joiner -> host, delivered a frame later
    std::vector<WorldTickMsg> ticksInFlight;
    for (int frame = 0; frame < 200; frame++) {
        //Delivery
        for (const WorldInputMsg& m : inFlight)
            host.onInput(1, m);
        inFlight.clear();
        for (const WorldTickMsg& m : ticksInFlight)
            joiner.onTick(m);
        ticksInFlight.clear();

        std::array<uint8_t, SERIES_SLOTS> hostKeys {{static_cast<uint8_t>(frame & 0xff), 0xff, 0xff, 0xff}};
        host.addLocal(hostKeys, nullptr);
        WorldInputMsg m;
        std::array<uint8_t, SERIES_SLOTS> joinerKeys {{0xff, static_cast<uint8_t>(frame * 3), static_cast<uint8_t>(frame * 5), 0xff}};
        if (joiner.addLocal(joinerKeys, &m)) {
            CHECK(m.slotMask == 0x6);
            CHECK(m.keys[0] == 0);    //only our slots
            inFlight.push_back(m);
        }

        WorldTickMsg tick;
        if (host.next(tick)) {
            hostTicks.push_back(tick);
            ticksInFlight.push_back(tick);
        }
        if (joiner.next(tick))
            joinerTicks.push_back(tick);
    }

    REQUIRE(hostTicks.size() > 150);
    REQUIRE(joinerTicks.size() + 2 >= hostTicks.size());
    for (size_t i = 0; i < joinerTicks.size(); i++) {
        CHECK(joinerTicks[i].tick == i);
        CHECK(joinerTicks[i].keys == hostTicks[i].keys);
        CHECK(joinerTicks[i].humanMask == 0x7);
        CHECK(hostTicks[i].keys[3] == 0);    //the CPU slot has no keys
    }
    //The host's tick i has the keys each peer added for tick i
    CHECK(hostTicks[10].keys[0] == 10);
    CHECK(hostTicks[10].keys[1] == static_cast<uint8_t>(30));
    CHECK(hostTicks[10].keys[2] == static_cast<uint8_t>(50));
}

TEST_CASE("World lockstep: the host waits for a joiner, goes on without a lost one")
{
    const SeriesSlots slots = exampleSlots();
    WorldLockstep host;
    host.begin(1, true, 0, slots, 4);

    WorldTickMsg tick;
    host.addLocal({{1, 0, 0, 0}}, nullptr);
    CHECK_FALSE(host.next(tick));    //no keys of peer 1 yet

    //Keys of another epoch, or out of order: ignored
    host.onInput(1, WorldInputMsg {2, 0, 0x6, {{0, 1, 1, 0}}});
    host.onInput(1, WorldInputMsg {1, 1, 0x6, {{0, 1, 1, 0}}});
    CHECK_FALSE(host.next(tick));
    host.onInput(1, WorldInputMsg {1, 0, 0x6, {{9, 2, 3, 9}}});
    REQUIRE(host.next(tick));
    CHECK(tick.keys[0] == 1);
    CHECK(tick.keys[1] == 2);
    CHECK(tick.keys[2] == 3);
    CHECK(tick.keys[3] == 0);    //(not peer 1's slot)

    //The window: at most 4 of our own ticks ahead; presses meanwhile are kept
    int added = 0;
    for (int i = 0; i < 10; i++)
        added += host.addLocal({{static_cast<uint8_t>(1u << (i % 8)), 0, 0, 0}}, nullptr) ? 1 : 0;
    CHECK(added == 4);
    CHECK_FALSE(host.next(tick));

    //Peer 1 is lost: its players are CPU players from the next tick
    host.peerLost(1);
    REQUIRE(host.next(tick));
    CHECK(tick.tick == 1);
    CHECK(tick.humanMask == 0x1);
    CHECK(tick.keys[1] == 0);
    for (int i = 0; i < 3; i++)
        REQUIRE(host.next(tick));
    CHECK_FALSE(host.next(tick));
    //The kept presses come with the next tick
    REQUIRE(host.addLocal({{0, 0, 0, 0}}, nullptr));
    REQUIRE(host.next(tick));
    CHECK(tick.keys[0] == 0xf3);

    //A joiner drops slots that the host's ticks don't have any more
    WorldLockstep joiner;
    joiner.begin(1, false, 1, slots, 4);
    joiner.onTick(WorldTickMsg {1, 0, 0x1, {{0, 0, 0, 0}}});
    joiner.onTick(WorldTickMsg {1, 5, 0x1, {{0, 0, 0, 0}}});    //a gap: ignored
    CHECK(joiner.pending() == 1);
    REQUIRE(joiner.next(tick));
    WorldInputMsg m;
    CHECK_FALSE(joiner.addLocal({{0, 1, 1, 0}}, &m));    //its players are gone
}

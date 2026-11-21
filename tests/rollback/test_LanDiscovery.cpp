#include "doctest.h"

#include "LanDiscovery.h"

using namespace rollback::lan;

TEST_CASE("System link: the answer survives the trip")
{
    HostInfo in;
    in.port = 7300;
    in.name = "Andrew";
    in.members = 2;
    in.maxMembers = 4;
    in.inMatch = true;
    in.build = "dc63264a";
    in.mode = "Classic";
    in.map = "Block Piles";
    in.players = 3;
    in.activity = ACTIVITY_MAP_EDITOR;
    in.editing = true;
    const std::vector<uint8_t> message = encodeAnswer(in);
    HostInfo out;
    REQUIRE(decodeAnswer(message.data(), message.size(), out));
    CHECK(out.port == 7300);
    CHECK(out.name == "Andrew");
    CHECK(out.members == 2);
    CHECK(out.maxMembers == 4);
    CHECK(out.inMatch);
    CHECK(out.build == "dc63264a");
    CHECK(out.mode == "Classic");
    CHECK(out.map == "Block Piles");
    CHECK(out.players == 3);
    CHECK(out.activity == ACTIVITY_MAP_EDITOR);
    CHECK(out.editing);

    //Cut short: refused, but where an older version's answer ended (without the details, or
    //without what the lobby plays)
    const size_t firstVersion = 6 + 5 + 1 + in.name.size() + 1 + in.build.size();
    const size_t secondVersion = firstVersion + 1 + in.mode.size() + 1 + in.map.size() + 1;
    for (size_t n = 0; n < message.size(); n++) {
        if (n == firstVersion) {
            REQUIRE(decodeAnswer(message.data(), n, out));
            CHECK(out.name == "Andrew");
            CHECK(out.mode.empty());
            CHECK(out.players == 0);
        } else if (n == secondVersion) {
            REQUIRE(decodeAnswer(message.data(), n, out));
            CHECK(out.players == 3);
            CHECK(out.activity == ACTIVITY_MATCHES);
            CHECK_FALSE(out.editing);
        } else {
            CHECK_FALSE(decodeAnswer(message.data(), n, out));
        }
    }
}

TEST_CASE("System link: queries and answers are told apart")
{
    const std::vector<uint8_t> query = encodeQuery();
    CHECK(isQuery(query.data(), query.size()));
    HostInfo info;
    info.port = 1;
    const std::vector<uint8_t> answer = encodeAnswer(info);
    CHECK_FALSE(isQuery(answer.data(), answer.size()));
    CHECK_FALSE(decodeAnswer(query.data(), query.size(), info));
    const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    CHECK_FALSE(isQuery(junk, sizeof(junk)));
    CHECK_FALSE(decodeAnswer(junk, sizeof(junk), info));
}

TEST_CASE("System link: long names are cut")
{
    HostInfo in;
    in.port = 7300;
    in.name = std::string(100, 'x');
    const std::vector<uint8_t> message = encodeAnswer(in);
    HostInfo out;
    REQUIRE(decodeAnswer(message.data(), message.size(), out));
    CHECK(out.name.size() == 48);
    CHECK(ipv4Text(0x0100a8c0u) == "192.168.0.1");
}

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
    const std::vector<uint8_t> message = encodeAnswer(in);
    HostInfo out;
    REQUIRE(decodeAnswer(message.data(), message.size(), out));
    CHECK(out.port == 7300);
    CHECK(out.name == "Andrew");
    CHECK(out.members == 2);
    CHECK(out.maxMembers == 4);
    CHECK(out.inMatch);
    CHECK(out.build == "dc63264a");

    //Cut short: refused
    for (size_t n = 0; n < message.size(); n++)
        CHECK_FALSE(decodeAnswer(message.data(), n, out));
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

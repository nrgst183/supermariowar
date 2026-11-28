#include "LanDiscovery.h"

#include <enet/enet.h>

#if defined(_WIN32) && !defined(SMW_XBOX)
#include <ws2tcpip.h>
#elif !defined(_WIN32) && !defined(SMW_XBOX) && !defined(__EMSCRIPTEN__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#define SMW_HAS_GETIFADDRS
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rollback::lan {

namespace {
constexpr uint8_t MAGIC[4] = {'S', 'M', 'W', 'L'};
constexpr uint8_t VERSION = 1;
constexpr uint8_t TYPE_QUERY = 'Q';
constexpr uint8_t TYPE_ANSWER = 'A';
constexpr size_t MAX_TEXT = 48;
constexpr size_t MAX_MESSAGE = 256;
constexpr uint32_t QUERY_EVERY_MS = 1000;
constexpr uint32_t FORGET_AFTER_MS = 3500;
constexpr uint32_t ADAPTERS_EVERY_MS = 5000;    // (the network may change: adapters looked up again)
const uint32_t LOOPBACK = ENET_HOST_TO_NET_32(0x7f000001);    // 127.0.0.1

ENetSocket toSocket(uintptr_t socket)
{
    return static_cast<ENetSocket>(socket);
}

void putHeader(std::vector<uint8_t>& out, uint8_t type)
{
    out.insert(out.end(), std::begin(MAGIC), std::end(MAGIC));
    out.push_back(VERSION);
    out.push_back(type);
}

bool checkHeader(const uint8_t* data, size_t size, uint8_t type)
{
    return data && size >= 6 && std::memcmp(data, MAGIC, 4) == 0 && data[4] == VERSION && data[5] == type;
}

void putText(std::vector<uint8_t>& out, const std::string& text)
{
    //((std::min): on Windows, enet.h brings in windows.h and its min macro)
    const size_t length = (std::min)(text.size(), MAX_TEXT);
    out.push_back(static_cast<uint8_t>(length));
    out.insert(out.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length));
}

bool getText(const uint8_t* data, size_t size, size_t& at, std::string& text)
{
    if (at >= size)
        return false;
    const size_t length = data[at++];
    if (length > MAX_TEXT || at + length > size)
        return false;
    text.assign(reinterpret_cast<const char*>(data + at), length);
    at += length;
    return true;
}

// A UDP socket for the discovery: non-blocking, may broadcast
uintptr_t openSocket(uint16_t port)
{
    static bool initialized = false;
    if (!initialized) {
        if (enet_initialize() != 0)
            return ~uintptr_t(0);
        initialized = true;
    }
    const ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL)
        return ~uintptr_t(0);
    enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1);
    enet_socket_set_option(socket, ENET_SOCKOPT_REUSEADDR, 1);
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    if (enet_socket_bind(socket, &address) != 0) {
        enet_socket_destroy(socket);
        return ~uintptr_t(0);
    }
    return static_cast<uintptr_t>(socket);
}

void sendTo(uintptr_t socket, uint32_t ipv4, uint16_t port, const std::vector<uint8_t>& message)
{
    ENetAddress address;
    address.host = ipv4;
    address.port = port;
    ENetBuffer buffer;
    buffer.data = const_cast<uint8_t*>(message.data());
    buffer.dataLength = message.size();
    enet_socket_send(toSocket(socket), &address, &buffer, 1);
}

// The next datagram that came (false: none)
bool receive(uintptr_t socket, uint8_t (&data)[MAX_MESSAGE], size_t& size, ENetAddress& from)
{
    for (;;) {
        ENetBuffer buffer;
        buffer.data = data;
        buffer.dataLength = sizeof(data);
        const int length = enet_socket_receive(toSocket(socket), &from, &buffer, 1);
        if (length == -2)
            continue;    //too big, or cut: not ours
        if (length <= 0)
            return false;
        size = static_cast<size_t>(length);
        return true;
    }
}
} // namespace

std::string ipv4Text(uint32_t ipv4)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ipv4);
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return text;
}

std::string FoundGame::address() const
{
    return ipv4Text(ipv4) + ":" + std::to_string(info.port);
}

std::vector<uint8_t> encodeQuery()
{
    std::vector<uint8_t> out;
    putHeader(out, TYPE_QUERY);
    return out;
}

bool isQuery(const uint8_t* data, size_t size)
{
    return checkHeader(data, size, TYPE_QUERY);
}

std::vector<uint8_t> encodeAnswer(const HostInfo& info)
{
    std::vector<uint8_t> out;
    putHeader(out, TYPE_ANSWER);
    out.push_back(static_cast<uint8_t>(info.port & 0xff));
    out.push_back(static_cast<uint8_t>(info.port >> 8));
    out.push_back(info.members);
    out.push_back(info.maxMembers);
    out.push_back(info.inMatch ? 1 : 0);
    putText(out, info.name);
    putText(out, info.build);
    //The details (after the first version's answer: older builds stop reading before them)
    putText(out, info.mode);
    putText(out, info.map);
    out.push_back(info.players);
    //(after those: what the lobby plays)
    out.push_back(info.activity);
    out.push_back(info.editing ? 1 : 0);
    return out;
}

bool decodeAnswer(const uint8_t* data, size_t size, HostInfo& info)
{
    if (!checkHeader(data, size, TYPE_ANSWER) || size < 11)
        return false;
    HostInfo decoded;
    decoded.port = static_cast<uint16_t>(data[6] | (data[7] << 8));
    decoded.members = data[8];
    decoded.maxMembers = data[9];
    decoded.inMatch = data[10] != 0;
    size_t at = 11;
    if (!getText(data, size, at, decoded.name) || !getText(data, size, at, decoded.build))
        return false;
    //The details: all of them, or none (an older build's answer)
    if (at < size) {
        if (!getText(data, size, at, decoded.mode) || !getText(data, size, at, decoded.map) || at >= size)
            return false;
        decoded.players = data[at++];
        //(what the lobby plays: both, or neither: an older build's answer)
        if (at < size) {
            if (at + 2 > size)
                return false;
            decoded.activity = data[at++];
            decoded.editing = data[at++] != 0;
        }
    }
    if (decoded.port == 0)
        return false;
    info = std::move(decoded);
    return true;
}

// ---- this computer's network adapters

namespace {
// The IPv4 adapters that are up (not the loopback): their broadcast addresses and their own
void adapters(std::vector<uint32_t>& broadcasts, std::vector<uint32_t>& own)
{
    broadcasts.clear();
    own.clear();
#if defined(_WIN32) && !defined(SMW_XBOX)
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return;
    INTERFACE_INFO list[32];
    DWORD bytes = 0;
    if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, list, sizeof(list), &bytes, nullptr, nullptr) == 0) {
        for (DWORD k = 0; k < bytes / sizeof(INTERFACE_INFO); k++) {
            const INTERFACE_INFO& i = list[k];
            if (!(i.iiFlags & IFF_UP) || (i.iiFlags & IFF_LOOPBACK) || i.iiAddress.Address.sa_family != AF_INET)
                continue;
            const uint32_t address = i.iiAddress.AddressIn.sin_addr.s_addr;
            const uint32_t mask = i.iiNetmask.AddressIn.sin_addr.s_addr;
            own.push_back(address);
            if (i.iiFlags & IFF_BROADCAST)
                broadcasts.push_back(address | ~mask);
        }
    }
    closesocket(s);
#elif defined(SMW_HAS_GETIFADDRS)
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
        return;
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK))
            continue;
        const uint32_t address = reinterpret_cast<const sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr;
        own.push_back(address);
        if ((i->ifa_flags & IFF_BROADCAST) && i->ifa_netmask)
            broadcasts.push_back(address | ~reinterpret_cast<const sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr);
    }
    freeifaddrs(list);
#endif
    //(the plain broadcast goes too: it's the one an adapter's address may not be)
    broadcasts.erase(std::remove(broadcasts.begin(), broadcasts.end(), ENET_HOST_BROADCAST), broadcasts.end());
}
} // namespace

// ---- Responder

Responder::~Responder()
{
    close();
}

bool Responder::open()
{
    close();
    m_socket = openSocket(DISCOVERY_PORT);
    if (!isOpen())
        printf("netplay: can't listen for system link searches on UDP port %u: this game won't be listed\n", DISCOVERY_PORT);
    return isOpen();
}

void Responder::close()
{
    if (isOpen())
        enet_socket_destroy(toSocket(m_socket));
    m_socket = INVALID;
}

void Responder::answer(const HostInfo& info)
{
    if (!isOpen())
        return;
    uint8_t data[MAX_MESSAGE];
    size_t size = 0;
    ENetAddress from;
    std::vector<uint8_t> message;
    while (receive(m_socket, data, size, from)) {
        if (!isQuery(data, size))
            continue;
        if (message.empty())
            message = encodeAnswer(info);
        sendTo(m_socket, from.host, from.port, message);
        //(and to all, at the asker's port: an answer straight to it may not get there (a tunnel that
        //hasn't learned where it is, a console on another subnet), a broadcast does)
        sendTo(m_socket, ENET_HOST_BROADCAST, from.port, message);
    }
    const uint32_t nowMs = enet_time_get();
    if (!m_announced || nowMs - m_lastAnnounceMs >= QUERY_EVERY_MS) {
        m_announced = true;
        m_lastAnnounceMs = nowMs;
        if (message.empty())
            message = encodeAnswer(info);
        sendTo(m_socket, ENET_HOST_BROADCAST, ANNOUNCE_PORT, message);
    }
}

// ---- Browser

Browser::~Browser()
{
    close();
}

bool Browser::open()
{
    if (isOpen())
        return true;
    m_socket = openSocket(0);
    m_queried = false;
    m_games.clear();
    m_heard = {};
    if (isOpen() && m_announceSocket == INVALID)
        m_announceSocket = openSocket(ANNOUNCE_PORT);
    m_heard.announcePort = m_announceSocket != INVALID;
    if (isOpen() && !m_heard.announcePort)
        printf("netplay: can't listen for system link announcements on UDP port %u: searching only\n", ANNOUNCE_PORT);
    return isOpen();
}

void Browser::close()
{
    if (isOpen())
        enet_socket_destroy(toSocket(m_socket));
    m_socket = INVALID;
    if (m_announceSocket != INVALID)
        enet_socket_destroy(toSocket(m_announceSocket));
    m_announceSocket = INVALID;
    m_games.clear();
}

void Browser::update(uint32_t nowMs)
{
    if (!isOpen())
        return;

    if (!m_queried || nowMs - m_lastAdaptersMs >= ADAPTERS_EVERY_MS) {
        m_lastAdaptersMs = nowMs;
        adapters(m_broadcasts, m_ownAddresses);
    }
    if (!m_queried || nowMs - m_lastQueryMs >= QUERY_EVERY_MS) {
        m_queried = true;
        m_lastQueryMs = nowMs;
        const std::vector<uint8_t> query = encodeQuery();
        sendTo(m_socket, ENET_HOST_BROADCAST, DISCOVERY_PORT, query);
        for (uint32_t broadcast : m_broadcasts)
            sendTo(m_socket, broadcast, DISCOVERY_PORT, query);
#ifndef SMW_XBOX
        //(a game hosted on this computer: the broadcasts don't come back to it)
        sendTo(m_socket, LOOPBACK, DISCOVERY_PORT, query);
#endif
    }

    uint8_t data[MAX_MESSAGE];
    size_t size = 0;
    ENetAddress from;
    //(the answers to the queries, then the announcements: those not counted for the ping)
    for (int source = 0; source < 2; source++) {
    const uintptr_t socket = source == 0 ? m_socket : m_announceSocket;
    if (socket == INVALID)
        continue;
    while (receive(socket, data, size, from)) {
        m_heard.packets++;
        m_heard.lastFrom = from.host;
        HostInfo info;
        if (!decodeAnswer(data, size, info))
            continue;
        m_heard.answers++;
        //(a game on this computer answers each query more than once: from its loopback and its
        //adapters' addresses; it is listed once, at the loopback, which joining it from here can use)
        if (std::find(m_ownAddresses.begin(), m_ownAddresses.end(), from.host) != m_ownAddresses.end())
            from.host = LOOPBACK;
        auto it = std::find_if(m_games.begin(), m_games.end(), [&](const FoundGame& game) {
            return game.ipv4 == from.host && game.info.port == info.port;
        });
        if (it == m_games.end()) {
            m_games.push_back({});
            it = std::prev(m_games.end());
            it->ipv4 = from.host;
        }
        it->info = std::move(info);
        it->lastSeenMs = nowMs;
        //How long since the last query: the round trip (to within a frame), smoothed
        const uint32_t since = nowMs - m_lastQueryMs;
        if (source == 0 && since < QUERY_EVERY_MS) {
            const int ping = static_cast<int>(since);
            it->pingMs = it->pingMs < 0 ? ping : (it->pingMs * 3 + ping) / 4;
        }
    }
    }

    std::erase_if(m_games, [&](const FoundGame& game) { return nowMs - game.lastSeenMs > FORGET_AFTER_MS; });
}

} // namespace rollback::lan

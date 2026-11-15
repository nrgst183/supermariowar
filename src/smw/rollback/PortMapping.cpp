// Opening the host's port on the router with UPnP (see PortMapping.h). The
// work runs on a detached thread: a router that doesn't answer can take
// seconds, and the game must not wait. A generation number tells a thread
// that its port was closed in the meantime (it then removes what it added).

#include "PortMapping.h"

#include <enet/enet.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace rollback::upnp {
namespace {

using namespace detail;

constexpr const char* DESCRIPTION = "Super Mario War";
constexpr int HTTP_TIMEOUT_MS = 3000;
constexpr uint32_t LEASE_SECONDS = 7200;    // when the router doesn't take a permanent one

struct Mapping {
    bool active = false;
    uint16_t port = 0;
    std::string controlUrl;
    std::string serviceType;
};

std::mutex g_mutex;
Status g_status;
Mapping g_mapping;
std::atomic<uint32_t> g_generation {0};

void setStatus(uint32_t generation, const Status& s)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (generation == g_generation)
        g_status = s;
}

uint32_t nowMs()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool resolve(const std::string& host, uint16_t port, ENetAddress& address)
{
    address.port = port;
    return enet_address_set_host_ip(&address, host.c_str()) == 0 || enet_address_set_host(&address, host.c_str()) == 0;
}

// One HTTP exchange over TCP: the whole response ("" on failure). localIp gets this computer's
// address toward the router.
std::string http(const Url& url, const std::string& request, std::string* localIp, int timeoutMs)
{
    ENetAddress address;
    if (!resolve(url.host, url.port, address))
        return {};
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
    if (s == ENET_SOCKET_NULL)
        return {};
    enet_socket_set_option(s, ENET_SOCKOPT_RCVTIMEO, timeoutMs);
    enet_socket_set_option(s, ENET_SOCKOPT_SNDTIMEO, timeoutMs);
    std::string response;
    if (enet_socket_connect(s, &address) == 0) {
        if (localIp) {
            ENetAddress me;
            char ip[64] = {};
            if (enet_socket_get_address(s, &me) == 0 && enet_address_get_host_ip(&me, ip, sizeof(ip)) == 0)
                *localIp = ip;
        }
        size_t sent = 0;
        while (sent < request.size()) {
            ENetBuffer buffer;
            buffer.data = const_cast<char*>(request.data() + sent);
            buffer.dataLength = request.size() - sent;
            const int n = enet_socket_send(s, nullptr, &buffer, 1);
            if (n <= 0)
                break;
            sent += static_cast<size_t>(n);
        }
        const uint32_t deadline = nowMs() + static_cast<uint32_t>(timeoutMs);
        char chunk[4096];
        while (sent == request.size() && response.size() < (1u << 20)) {
            const int32_t left = static_cast<int32_t>(deadline - nowMs());
            if (left <= 0)
                break;
            enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
            if (enet_socket_wait(s, &condition, static_cast<enet_uint32>(left)) < 0 || !(condition & ENET_SOCKET_WAIT_RECEIVE))
                break;
            ENetBuffer buffer;
            buffer.data = chunk;
            buffer.dataLength = sizeof(chunk);
            const int n = enet_socket_receive(s, nullptr, &buffer, 1);
            if (n <= 0)
                break;    //closed (Connection: close ends every response)
            response.append(chunk, static_cast<size_t>(n));
        }
    }
    enet_socket_destroy(s);
    return response;
}

std::string get(const Url& url, std::string* localIp)
{
    return http(url, "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                         "\r\nConnection: close\r\nUser-Agent: SuperMarioWar UPnP/1.1\r\n\r\n",
        localIp, HTTP_TIMEOUT_MS);
}

// A SOAP action; the response body, status the HTTP status (0: no answer)
std::string soap(const std::string& controlUrl, const std::string& serviceType, const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& args, int& status, int timeoutMs = HTTP_TIMEOUT_MS)
{
    status = 0;
    Url url;
    if (!parseUrl(controlUrl, url))
        return {};
    const std::string body = soapEnvelope(serviceType, action, args);
    const std::string request = "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                                "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + serviceType + "#" + action +
                                "\"\r\nContent-Length: " + std::to_string(body.size()) +
                                "\r\nConnection: close\r\nUser-Agent: SuperMarioWar UPnP/1.1\r\n\r\n" + body;
    return httpBody(http(url, request, nullptr, timeoutMs), status);
}

// The routers on the local network: their description URLs (SSDP M-SEARCH, about 2.5 s)
std::vector<std::string> discover()
{
    std::vector<std::string> locations;
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL)
        return locations;
    ENetAddress any;
    any.host = ENET_HOST_ANY;
    any.port = 0;
    enet_socket_bind(s, &any);
    ENetAddress group;
    group.port = 1900;
    enet_address_set_host_ip(&group, "239.255.255.250");
    const char* targets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    for (int round = 0; round < 2; round++) {    //(UDP: send twice)
        for (const char* st : targets) {
            const std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nST: ") + st +
                                    "\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n\r\n";
            ENetBuffer buffer;
            buffer.data = const_cast<char*>(msg.data());
            buffer.dataLength = msg.size();
            enet_socket_send(s, &group, &buffer, 1);
        }
    }
    const uint32_t deadline = nowMs() + 2500;
    char reply[2048];
    for (;;) {
        const int32_t left = static_cast<int32_t>(deadline - nowMs());
        if (left <= 0)
            break;
        enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(s, &condition, static_cast<enet_uint32>(left)) < 0 || !(condition & ENET_SOCKET_WAIT_RECEIVE))
            break;
        ENetAddress from;
        ENetBuffer buffer;
        buffer.data = reply;
        buffer.dataLength = sizeof(reply) - 1;
        const int n = enet_socket_receive(s, &from, &buffer, 1);
        if (n <= 0)
            continue;
        const std::string location = ssdpLocation(std::string(reply, static_cast<size_t>(n)));
        if (!location.empty() && std::find(locations.begin(), locations.end(), location) == locations.end())
            locations.push_back(location);
    }
    enet_socket_destroy(s);
    return locations;
}

bool addMapping(const Service& service, uint16_t port, const std::string& localIp, uint32_t lease, int& errorCode)
{
    int status = 0;
    const std::string body = soap(service.controlUrl, service.type, "AddPortMapping",
        {{"NewRemoteHost", ""}, {"NewExternalPort", std::to_string(port)}, {"NewProtocol", "UDP"},
            {"NewInternalPort", std::to_string(port)}, {"NewInternalClient", localIp}, {"NewEnabled", "1"},
            {"NewPortMappingDescription", DESCRIPTION}, {"NewLeaseDuration", std::to_string(lease)}},
        status);
    errorCode = status == 200 ? 0 : std::atoi(xmlValue(body, "errorCode").c_str());
    if (status != 200 && errorCode == 0)
        errorCode = -1;
    return status == 200;
}

void removeMapping(const Mapping& m, int timeoutMs)
{
    int status = 0;
    soap(m.controlUrl, m.serviceType, "DeletePortMapping",
        {{"NewRemoteHost", ""}, {"NewExternalPort", std::to_string(m.port)}, {"NewProtocol", "UDP"}}, status, timeoutMs);
    printf("upnp: removed the forward of UDP port %d (%s)\n", m.port, status == 200 ? "ok" : "no answer");
}

void work(uint32_t generation, uint16_t port)
{
    Status st;
    st.state = State::Working;
    st.port = port;
    setStatus(generation, st);

    const std::vector<std::string> locations = discover();
    if (locations.empty()) {
        st.state = State::Failed;
        st.error = "no UPnP router answered";
        printf("upnp: %s\n", st.error.c_str());
        setStatus(generation, st);
        return;
    }

    std::string lastError = "the router has no internet connection service";
    for (const std::string& location : locations) {
        Url url;
        if (!parseUrl(location, url))
            continue;
        std::string localIp;
        int httpStatus = 0;
        const std::string description = httpBody(get(url, &localIp), httpStatus);
        if (httpStatus != 200 || localIp.empty())
            continue;
        for (const Service& service : wanServices(description, location)) {
            if (g_generation != generation)
                return;
            //A permanent forward first; some routers only take ones that expire (then renewed below)
            uint32_t lease = 0;
            int error = 0;
            bool ok = addMapping(service, port, localIp, lease, error);
            if (!ok && error != 718) {
                lease = LEASE_SECONDS;
                ok = addMapping(service, port, localIp, lease, error);
            }
            if (!ok) {
                //(keep the clearest reason: another service of the router may refuse for its own)
                if (error == 718)
                    lastError = "the router already forwards port " + std::to_string(port) + " to another computer";
                else if (lastError.find("already forwards") == std::string::npos)
                    lastError = "the router refused (error " + std::to_string(error) + ")";
                continue;
            }

            Mapping m;
            m.active = true;
            m.port = port;
            m.controlUrl = service.controlUrl;
            m.serviceType = service.type;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_generation != generation) {
                    //Closed while we asked: take it back
                    removeMapping(m, 1000);
                    return;
                }
                g_mapping = m;
            }

            int status = 0;
            const std::string ipBody = soap(service.controlUrl, service.type, "GetExternalIPAddress", {}, status);
            st.state = State::Open;
            st.publicAddress = status == 200 ? xmlValue(ipBody, "NewExternalIPAddress") : "";
            st.behindAnotherNat = !st.publicAddress.empty() && isPrivateIPv4(st.publicAddress);
            setStatus(generation, st);
            printf("upnp: the router forwards UDP port %d to %s; public address %s%s\n", port, localIp.c_str(),
                st.publicAddress.empty() ? "unknown" : st.publicAddress.c_str(),
                st.behindAnotherNat ? " (private: there is another NAT in front of the router)" : "");

            //An expiring forward: renew it at half time while the lobby is open
            if (lease > 0) {
                uint32_t renewAt = nowMs() + lease * 500;
                while (g_generation == generation) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (static_cast<int32_t>(nowMs() - renewAt) >= 0 && g_generation == generation) {
                        addMapping(service, port, localIp, lease, error);
                        renewAt = nowMs() + lease * 500;
                    }
                }
            }
            return;
        }
    }
    st.state = State::Failed;
    st.error = lastError;
    printf("upnp: couldn't open UDP port %d: %s\n", port, lastError.c_str());
    setStatus(generation, st);
}

}  // namespace

void openPort(uint16_t port)
{
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_status.port == port && (g_status.state == State::Working || g_status.state == State::Open))
            return;
        generation = ++g_generation;
        g_status = Status();
        g_status.state = State::Working;
        g_status.port = port;
    }
    std::thread(work, generation, port).detach();
}

void closePort()
{
    Mapping m;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_generation;    //(a thread still asking takes back what it gets)
        m = g_mapping;
        g_mapping = Mapping();
        g_status = Status();
    }
    if (m.active)
        removeMapping(m, 1000);
}

Status status()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}

}  // namespace rollback::upnp

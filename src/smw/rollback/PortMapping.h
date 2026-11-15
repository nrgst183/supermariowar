#pragma once

#include <cstdint>
#include <string>
#include <vector>

// OPENING THE HOST'S PORT ON THE ROUTER (UPnP)
//
// So that friends can join without the host forwarding the port by hand: when
// a lobby is hosted, a background thread asks the home router to forward the
// UDP port to this computer, the way games and consoles usually do it (UPnP
// Internet Gateway Device: find the router with an SSDP search on the local
// network, read its description, then AddPortMapping and
// GetExternalIPAddress over SOAP). The mapping is removed when the lobby
// closes. Nothing else is needed: no server of ours.
//
// It doesn't work everywhere: UPnP can be off in the router, and behind a
// carrier-grade NAT (the router's own internet address is private) the
// forward doesn't reach the internet. status() says what happened, for the
// lobby screen; then the port has to be forwarded by hand (or a VPN used).
//
// Sockets: ENet's (no other library). The thread never touches the game.
namespace rollback::upnp {

enum class State {
    Off,        // not asked
    Working,    // looking for the router / asking it
    Open,       // the router forwards the port; publicAddress is how friends reach us
    Failed,     // no router answered, or it refused (error says why)
};

struct Status {
    State state = State::Off;
    uint16_t port = 0;
    std::string publicAddress;    // "203.0.113.5" (Open)
    bool behindAnotherNat = false;// the router's address is itself private: the forward won't reach the internet
    std::string error;            // (Failed) a short reason
};

// Starts opening `port` (UDP) in the background; a port already open stays open
void openPort(uint16_t port);
// Removes the forward (waits at most about a second for the router)
void closePort();
Status status();

// ---- the parts that don't need the network (unit-tested)
namespace detail {

struct Url {
    std::string host;
    uint16_t port = 80;
    std::string path = "/";
};
bool parseUrl(const std::string& url, Url& out);

// The LOCATION header of an SSDP reply ("" when none)
std::string ssdpLocation(const std::string& reply);

struct Service {
    std::string type;          // urn:schemas-upnp-org:service:WANIPConnection:1
    std::string controlUrl;    // absolute
};
// The WAN connection services in a router's device description, absolute URLs (from URLBase,
// or else from where the description came from)
std::vector<Service> wanServices(const std::string& descriptionXml, const std::string& descriptionUrl);

// The text of the first <tag>...</tag> (any namespace prefix), "" when none
std::string xmlValue(const std::string& xml, const std::string& tag);

// The body of an HTTP response (chunked or not); status gets the status code (0: not HTTP)
std::string httpBody(const std::string& response, int& status);

// A SOAP request body for `action` of `serviceType` with the arguments in order
std::string soapEnvelope(const std::string& serviceType, const std::string& action,
    const std::vector<std::pair<std::string, std::string>>& args);

// Private, shared or link-local IPv4 addresses (10/8, 172.16/12, 192.168/16, 100.64/10, 169.254/16)
bool isPrivateIPv4(const std::string& ip);

}  // namespace detail
}  // namespace rollback::upnp

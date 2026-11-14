#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// The connection layer under the rollback netplay: connections to other
// peers and datagrams on two channels. EnetTransport implements it over UDP;
// the unit tests use an in-memory fake.
//
// Connections are small integers chosen by the transport. The layers above
// map them to peer ids (NetMesh) and never see addresses or sockets.
namespace rollback {

enum class Channel : uint8_t {
    Unreliable = 0,    // unsequenced, may be lost or reordered: inputs
    Reliable = 1,      // reliable and ordered: setup and control messages
};

struct TransportEvent {
    enum class Type : uint8_t { None, Connected, Disconnected, Received };

    Type type = Type::None;
    int connection = -1;
    Channel channel = Channel::Reliable;
    // Received: valid until the next poll()
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    // Queues a message on a connection. Unknown or closed connections are ignored.
    virtual void send(int connection, Channel channel, const uint8_t* data, size_t size) = 0;
    // Hands out the next event, if any. Doesn't block.
    virtual bool poll(TransportEvent& event) = 0;
    // Sends what is queued now (once per frame, after the frame's sends)
    virtual void flush() = 0;
    // Closes a connection after sending what is queued on it
    virtual void disconnect(int connection) = 0;
    // Nothing is held back to be sent later (see NetConditionSim)
    virtual bool idle() const { return true; }
    // Closes every connection gracefully, for at most `timeoutMs`
    virtual void close(uint32_t /*timeoutMs*/) {}
};

// A transport that also makes connections, for the session setup (NetMesh).
// Addresses are IPv4 in network byte order, as enet keeps them.
class IMeshTransport : public ITransport {
public:
    // Starts connecting; the Connected event follows. -1 on failure.
    virtual int connect(const std::string& host, uint16_t port) = 0;
    virtual int connect(uint32_t ipv4, uint16_t port) = 0;
    // The remote address of a connection
    virtual bool address(int connection, uint32_t& ipv4, uint16_t& port) const = 0;
    virtual uint16_t localPort() const = 0;
};

// Milliseconds of a monotonic clock (wraps after 49 days: compare differences)
inline uint32_t monotonicMs()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace rollback

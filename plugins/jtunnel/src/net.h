// The little JTunnel needs from the OS: IPv4 UDP sockets, addresses, and a semaphore the audio
// thread can post to wake the sending thread. Never used on the audio thread except Semaphore::post.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace jtunnel::net {

// Winsock needs starting before any socket call; elsewhere these do nothing. Counted, so every
// user can call them in pairs.
void startup();
void cleanup();

struct Address {
   uint32_t ip = 0; // host byte order
   uint16_t port = 0;

   bool valid() const { return ip != 0 && port != 0; }
   bool operator==(const Address &o) const { return ip == o.ip && port == o.port; }
   bool operator!=(const Address &o) const { return !(*this == o); }
   std::string toString(bool withPort = true) const;

   static Address loopback(uint16_t port) { return {0x7F000001u, port}; }
   // "192.168.1.20:47000", "studio.local:47000" or "studio.local" (with `defaultPort`). Looks
   // names up through the OS, so it can block: network threads only.
   static bool parse(const std::string &text, Address &out, uint16_t defaultPort);
};

// True if text looks like an address someone typed ("192.168.1.20", "host:port") rather than a
// tunnel name: tunnel names never contain '.' or ':'.
bool looksLikeAddress(const std::string &text);

// This machine's address on the LAN, as others would reach it (the interface the default route
// uses). Zero if there's no network.
uint32_t localIp();
// This machine's name, for default tunnel names.
std::string hostName();

class UdpSocket {
public:
   UdpSocket() = default;
   ~UdpSocket() { close(); }
   UdpSocket(const UdpSocket &) = delete;
   UdpSocket &operator=(const UdpSocket &) = delete;

   // Binds to `port` on every interface (0: any free port).
   bool open(uint16_t port);
   void close();
   bool isOpen() const { return fd_ != kInvalid; }
   uint16_t localPort() const;

   bool sendTo(const Address &to, const void *data, size_t size);
   // Waits up to timeoutMs for a packet. Returns its size, 0 on timeout, -1 on error.
   int receive(void *buffer, size_t capacity, Address &from, int timeoutMs);

private:
#ifdef _WIN32
   using Handle = uintptr_t;
   static constexpr Handle kInvalid = ~static_cast<uintptr_t>(0);
#else
   using Handle = int;
   static constexpr Handle kInvalid = -1;
#endif
   Handle fd_ = kInvalid;
};

class Semaphore {
public:
   Semaphore();
   ~Semaphore();
   Semaphore(const Semaphore &) = delete;
   Semaphore &operator=(const Semaphore &) = delete;

   // Safe on the audio thread: never blocks.
   void post();
   // True if posted, false after timeoutMs.
   bool wait(int timeoutMs);

private:
   void *handle_ = nullptr;
};

} // namespace jtunnel::net

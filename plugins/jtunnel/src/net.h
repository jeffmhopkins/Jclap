// The little JTunnel needs from the OS: IPv4 UDP sockets, addresses, and a semaphore the audio
// thread can post to wake the sending thread. Never used on the audio thread except Semaphore::post.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

   // Binds to `port` on every interface, or only on loopback (0: any free port).
   bool open(uint16_t port, bool loopbackOnly = false);
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

// A TCP socket on this computer only (127.0.0.1), for the local state server and its clients.
// Non-blocking once open.
class TcpSocket {
public:
   TcpSocket() = default;
   ~TcpSocket() { close(); }
   TcpSocket(TcpSocket &&other) noexcept : fd_(other.fd_) { other.fd_ = kInvalid; }
   TcpSocket &operator=(TcpSocket &&other) noexcept {
      if (this != &other) {
         close();
         fd_ = other.fd_;
         other.fd_ = kInvalid;
      }
      return *this;
   }
   TcpSocket(const TcpSocket &) = delete;
   TcpSocket &operator=(const TcpSocket &) = delete;

   // Listens on 127.0.0.1:port.
   bool listenLoopback(uint16_t port);
   // A waiting connection, if there is one.
   bool accept(TcpSocket &out);
   // Connects to 127.0.0.1:port, waiting up to timeoutMs (tests and tools).
   bool connectLoopback(uint16_t port, int timeoutMs);
   void close();
   bool isOpen() const { return fd_ != kInvalid; }
   uint16_t localPort() const;

   // Bytes read or written, 0 if it would have to wait, -1 once the connection is gone.
   int read(void *buffer, size_t capacity);
   int write(const void *data, size_t size);

   // Waits up to timeoutMs for any of the sockets to have something to read (or a connection to
   // accept), and for those in `writers` to take more.
   static void wait(const std::vector<const TcpSocket *> &readers, const std::vector<const TcpSocket *> &writers,
                    int timeoutMs);

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

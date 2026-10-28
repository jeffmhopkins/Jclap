#include "net.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif
#endif

namespace jtunnel::net {

void startup() {
#ifdef _WIN32
   WSADATA data;
   WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

void cleanup() {
#ifdef _WIN32
   WSACleanup();
#endif
}

std::string Address::toString(bool withPort) const {
   char text[32];
   if (withPort)
      std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                    port);
   else
      std::snprintf(text, sizeof(text), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
   return text;
}

bool Address::parse(const std::string &text, Address &out, uint16_t defaultPort) {
   std::string host = text;
   uint16_t port = defaultPort;
   const size_t colon = text.rfind(':');
   if (colon != std::string::npos) {
      host = text.substr(0, colon);
      const std::string portText = text.substr(colon + 1);
      char *end = nullptr;
      const long p = std::strtol(portText.c_str(), &end, 10);
      if (portText.empty() || *end || p <= 0 || p > 65535)
         return false;
      port = static_cast<uint16_t>(p);
   }
   while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back())))
      host.pop_back();
   while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front())))
      host.erase(host.begin());
   if (host.empty() || port == 0)
      return false;

   in_addr numeric{};
   if (inet_pton(AF_INET, host.c_str(), &numeric) == 1) {
      out.ip = ntohl(numeric.s_addr);
      out.port = port;
      return out.ip != 0;
   }
   addrinfo hints{};
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;
   addrinfo *result = nullptr;
   if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
      return false;
   out.ip = ntohl(reinterpret_cast<sockaddr_in *>(result->ai_addr)->sin_addr.s_addr);
   out.port = port;
   freeaddrinfo(result);
   return out.ip != 0;
}

bool looksLikeAddress(const std::string &text) {
   return text.find('.') != std::string::npos || text.find(':') != std::string::npos;
}

uint32_t localIp() {
   // Connecting a UDP socket sends nothing; it only picks the route, and so our address on it.
   startup();
   struct Cleanup {
      ~Cleanup() { cleanup(); }
   } cleanupLater;
#ifdef _WIN32
   SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s == INVALID_SOCKET)
      return 0;
#else
   int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s < 0)
      return 0;
#endif
   sockaddr_in to{};
   to.sin_family = AF_INET;
   to.sin_port = htons(9);
   to.sin_addr.s_addr = htonl(0xC0A80001u); // 192.168.0.1: any private address picks the LAN route
   uint32_t ip = 0;
   if (connect(s, reinterpret_cast<sockaddr *>(&to), sizeof(to)) == 0) {
      sockaddr_in local{};
      socklen_t len = sizeof(local);
      if (getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0)
         ip = ntohl(local.sin_addr.s_addr);
   }
#ifdef _WIN32
   closesocket(s);
#else
   ::close(s);
#endif
   return ip;
}

std::string hostName() {
   char name[256] = {};
   startup();
   const int failed = gethostname(name, sizeof(name) - 1);
   cleanup();
   if (failed)
      return "JTunnel";
   std::string result(name);
   // "studio.local" or "studio.lan" -> "studio".
   const size_t dot = result.find('.');
   if (dot != std::string::npos)
      result.resize(dot);
   return result.empty() ? "JTunnel" : result;
}

bool UdpSocket::open(uint16_t port) {
   close();
#ifdef _WIN32
   SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s == INVALID_SOCKET)
      return false;
   fd_ = static_cast<Handle>(s);
#else
   int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s < 0)
      return false;
   fd_ = s;
#endif
   // Room for bursts after a Wi-Fi stall.
   int bufferSize = 1 << 20;
   setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&bufferSize), sizeof(bufferSize));
   setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&bufferSize), sizeof(bufferSize));
#ifdef _WIN32
   // Otherwise an ICMP "port unreachable" from a listener that went away makes recvfrom fail.
   BOOL reportReset = FALSE;
   DWORD returned = 0;
   WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &reportReset, sizeof(reportReset), nullptr, 0, &returned, nullptr, nullptr);
#endif
   sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      close();
      return false;
   }
   return true;
}

void UdpSocket::close() {
   if (fd_ == kInvalid)
      return;
#ifdef _WIN32
   closesocket(static_cast<SOCKET>(fd_));
#else
   ::close(fd_);
#endif
   fd_ = kInvalid;
}

uint16_t UdpSocket::localPort() const {
   if (fd_ == kInvalid)
      return 0;
   sockaddr_in addr{};
   socklen_t len = sizeof(addr);
#ifdef _WIN32
   if (getsockname(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr *>(&addr), &len) != 0)
      return 0;
#else
   if (getsockname(fd_, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
      return 0;
#endif
   return ntohs(addr.sin_port);
}

bool UdpSocket::sendTo(const Address &to, const void *data, size_t size) {
   if (fd_ == kInvalid || !to.valid())
      return false;
   sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_port = htons(to.port);
   addr.sin_addr.s_addr = htonl(to.ip);
#ifdef _WIN32
   return sendto(static_cast<SOCKET>(fd_), static_cast<const char *>(data), static_cast<int>(size), 0,
                 reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == static_cast<int>(size);
#else
   return sendto(fd_, data, size, 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == static_cast<ssize_t>(size);
#endif
}

int UdpSocket::receive(void *buffer, size_t capacity, Address &from, int timeoutMs) {
   if (fd_ == kInvalid)
      return -1;
#ifdef _WIN32
   WSAPOLLFD pfd{};
   pfd.fd = static_cast<SOCKET>(fd_);
   pfd.events = POLLRDNORM;
   const int ready = WSAPoll(&pfd, 1, timeoutMs);
#else
   pollfd pfd{};
   pfd.fd = fd_;
   pfd.events = POLLIN;
   const int ready = poll(&pfd, 1, timeoutMs);
#endif
   if (ready == 0)
      return 0;
   if (ready < 0)
      return errno == EINTR ? 0 : -1;
   sockaddr_in addr{};
   socklen_t len = sizeof(addr);
#ifdef _WIN32
   const int n = recvfrom(static_cast<SOCKET>(fd_), static_cast<char *>(buffer), static_cast<int>(capacity), 0,
                          reinterpret_cast<sockaddr *>(&addr), &len);
   if (n < 0)
      return WSAGetLastError() == WSAEMSGSIZE ? 0 : -1;
#else
   const ssize_t n = recvfrom(fd_, buffer, capacity, 0, reinterpret_cast<sockaddr *>(&addr), &len);
   if (n < 0)
      return errno == EINTR || errno == EAGAIN ? 0 : -1;
#endif
   from.ip = ntohl(addr.sin_addr.s_addr);
   from.port = ntohs(addr.sin_port);
   return static_cast<int>(n);
}

// ---- Semaphore ----

#if defined(_WIN32)
Semaphore::Semaphore() : handle_(CreateSemaphoreW(nullptr, 0, 0x7FFFFFFF, nullptr)) {}
Semaphore::~Semaphore() {
   if (handle_)
      CloseHandle(handle_);
}
void Semaphore::post() { ReleaseSemaphore(handle_, 1, nullptr); }
bool Semaphore::wait(int timeoutMs) {
   return WaitForSingleObject(handle_, static_cast<DWORD>(timeoutMs)) == WAIT_OBJECT_0;
}
#elif defined(__APPLE__)
Semaphore::Semaphore() : handle_(dispatch_semaphore_create(0)) {}
Semaphore::~Semaphore() {
   if (handle_)
      dispatch_release(static_cast<dispatch_semaphore_t>(handle_));
}
void Semaphore::post() { dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(handle_)); }
bool Semaphore::wait(int timeoutMs) {
   return dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(handle_),
                                  dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeoutMs) * 1000000)) == 0;
}
#else
Semaphore::Semaphore() {
   auto *s = new sem_t;
   sem_init(s, 0, 0);
   handle_ = s;
}
Semaphore::~Semaphore() {
   auto *s = static_cast<sem_t *>(handle_);
   sem_destroy(s);
   delete s;
}
void Semaphore::post() { sem_post(static_cast<sem_t *>(handle_)); }
bool Semaphore::wait(int timeoutMs) {
   timespec deadline{};
   clock_gettime(CLOCK_REALTIME, &deadline);
   deadline.tv_sec += timeoutMs / 1000;
   deadline.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
   if (deadline.tv_nsec >= 1000000000L) {
      ++deadline.tv_sec;
      deadline.tv_nsec -= 1000000000L;
   }
   while (sem_timedwait(static_cast<sem_t *>(handle_), &deadline) != 0)
      if (errno != EINTR)
         return false;
   return true;
}
#endif

} // namespace jtunnel::net

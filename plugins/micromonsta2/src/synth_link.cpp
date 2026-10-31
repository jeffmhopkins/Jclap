#include "synth_link.h"

#include "program_dump.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
static void closeSocket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kNoSocket = -1;
static void closeSocket(Socket s) { close(s); }
#endif

namespace mm2 {

namespace {

std::vector<std::string> split(const std::string &s, char sep) {
   std::vector<std::string> out;
   size_t start = 0;
   for (;;) {
      const size_t at = s.find(sep, start);
      out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
      if (at == std::string::npos)
         return out;
      start = at + 1;
   }
}

bool toInt(const std::string &s, int &out) {
   if (s.empty())
      return false;
   char *end = nullptr;
   const long v = std::strtol(s.c_str(), &end, 10);
   if (*end)
      return false;
   out = static_cast<int>(v);
   return true;
}

// OSC messages of ints and strings, the little the script and the plugin say to each other.
void padTo4(std::string &out) {
   do
      out += '\0';
   while (out.size() % 4);
}

struct OscArg {
   bool isInt;
   int i;
   std::string s;
};
std::string oscMessage(const std::string &address, const std::vector<OscArg> &args = {}) {
   std::string out = address;
   padTo4(out);
   std::string tags = ",";
   for (const OscArg &a : args)
      tags += a.isInt ? 'i' : 's';
   out += tags;
   padTo4(out);
   for (const OscArg &a : args) {
      if (a.isInt) {
         const auto v = static_cast<uint32_t>(a.i);
         out += static_cast<char>(v >> 24);
         out += static_cast<char>(v >> 16);
         out += static_cast<char>(v >> 8);
         out += static_cast<char>(v);
      } else {
         out += a.s;
         padTo4(out);
      }
   }
   return out;
}

// The OSC string starting at `at`, moving `at` past its padding; false if it runs off the end.
bool oscString(const char *data, size_t size, size_t &at, std::string &out) {
   const void *end = at < size ? std::memchr(data + at, '\0', size - at) : nullptr;
   if (!end)
      return false;
   const size_t length = static_cast<size_t>(static_cast<const char *>(end) - (data + at));
   out.assign(data + at, length);
   at = (at + length + 4) & ~size_t(3);
   return at <= size;
}

Socket openSocket(int &port) {
   Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s == kNoSocket)
      return kNoSocket;
   // A bank's 500 lines can arrive in one burst.
   const int buffer = 1 << 21;
   setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&buffer), sizeof(buffer));
   sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   socklen_t length = sizeof(addr);
   if (bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0 ||
       getsockname(s, reinterpret_cast<sockaddr *>(&addr), &length) != 0) {
      closeSocket(s);
      return kNoSocket;
   }
   port = ntohs(addr.sin_port);
   return s;
}

void sendToScript(Socket s, const std::string &message) {
   sockaddr_in to{};
   to.sin_family = AF_INET;
   to.sin_port = htons(static_cast<uint16_t>(SynthLink::kPort));
   to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   sendto(s, message.data(), static_cast<int>(message.size()), 0, reinterpret_cast<const sockaddr *>(&to),
          sizeof(to));
}

// Waits up to `ms` for data. 1 = readable, 0 = nothing yet, -1 = error.
int waitReadable(Socket s, int ms) {
#ifdef _WIN32
   fd_set set;
   FD_ZERO(&set);
   FD_SET(s, &set);
   timeval tv{ms / 1000, (ms % 1000) * 1000};
   const int r = select(0, &set, nullptr, nullptr, &tv);
#else
   pollfd p{s, POLLIN, 0};
   const int r = poll(&p, 1, ms);
#endif
   return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

} // namespace

SynthLink &SynthLink::get() {
   static SynthLink link;
   return link;
}

bool SynthLink::parseHex(const std::string &hex, std::vector<uint8_t> &out) {
   out.clear();
   if (hex.size() % 2)
      return false;
   const auto digit = [](char c) {
      if (c >= '0' && c <= '9')
         return c - '0';
      if (c >= 'a' && c <= 'f')
         return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
         return c - 'A' + 10;
      return -1;
   };
   for (size_t i = 0; i < hex.size(); i += 2) {
      const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
      if (hi < 0 || lo < 0)
         return false;
      out.push_back(static_cast<uint8_t>(hi * 16 + lo));
   }
   return true;
}

void SynthLink::handleLine(const std::string &line) {
   std::lock_guard<std::mutex> lock(mutex_);
   const std::vector<std::string> f = split(line, '\t');
   std::vector<uint8_t> bytes;
   int ticket = 0;
   if (f[0] == "SOUND" && f.size() == 3 && toInt(f[1], ticket)) {
      // Only the answer to the latest request: an older one's panel has moved on.
      HardwarePatch p;
      if (ticket != lastTicket_ || !status_.askingSound)
         return;
      status_.askingSound = false;
      if (!parseHex(f[2], bytes) || !decodeProgram(bytes.data(), bytes.size(), 0, p)) {
         status_.error = "the synth's answer isn't a program";
         return;
      }
      status_.error.clear();
      sound_ = Sound{ticket, std::move(p)};
   } else if (f[0] == "FAILED" && f.size() == 3 && toInt(f[1], ticket)) {
      if (ticket != 0 && (ticket != lastTicket_ || !status_.askingSound))
         return;
      if (ticket != 0)
         status_.askingSound = false;
      else {
         status_.receiving = false;
         incoming_.clear();
      }
      status_.error = f[2];
   } else if (f[0] == "RECEIVING") {
      incoming_.clear();
      status_.receiving = true;
      status_.received = 0;
      status_.error.clear();
   } else if (f[0] == "PROGRAM" && f.size() == 2) {
      if (parseHex(f[1], bytes) && isProgramDump(bytes.data(), bytes.size())) {
         incoming_.push_back(std::move(bytes));
         status_.received = static_cast<int>(incoming_.size());
      }
   } else if (f[0] == "DONE") {
      status_.receiving = false;
      status_.received = static_cast<int>(incoming_.size());
      // One program on its own is U01 (the active program), which isn't a bank.
      if (incoming_.size() == 1)
         status_.error = "that was one program (U01): send the bank with U02";
      else if (!incoming_.empty()) {
         bank_ = decodeBank(incoming_);
         bankVersion_.fetch_add(1);
      }
      incoming_.clear();
   }
}

int SynthLink::requestSound(char part) {
   std::lock_guard<std::mutex> lock(mutex_);
   if (!status_.connected || (part != 'A' && part != 'B'))
      return 0;
   ++lastTicket_;
   status_.askingSound = true;
   status_.error.clear();
   pendingTicket_ = lastTicket_;
   pendingPart_ = part;
   return lastTicket_;
}

SynthLink::Status SynthLink::status() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return status_;
}

SynthLink::Sound SynthLink::sound() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return sound_;
}

std::vector<HardwarePatch> SynthLink::bank() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return bank_;
}

void SynthLink::acquire() {
   std::lock_guard<std::mutex> lock(useMutex_);
   if (users_++ > 0)
      return;
#ifdef _WIN32
   WSADATA data;
   WSAStartup(MAKEWORD(2, 2), &data);
#endif
   running_.store(true);
   thread_ = std::thread([this] { run(); });
}

// Joined here, on the main thread, when the last panel closes: never from a static destructor,
// which on Windows runs while the DLL unloads and can't wait for a thread.
void SynthLink::release() {
   std::lock_guard<std::mutex> lock(useMutex_);
   if (--users_ > 0)
      return;
   running_.store(false);
   if (thread_.joinable())
      thread_.join();
#ifdef _WIN32
   WSACleanup();
#endif
   std::lock_guard<std::mutex> state(mutex_);
   status_.connected = false;
   status_.askingSound = false;
   status_.receiving = false;
}

// Subscribes to the script's lines and repeats that every couple of seconds, which also tells
// whether the script is there: it answers each hello. A sound request the script never answers
// (it says FAILED itself when the synth doesn't) gives up after a few seconds.
void SynthLink::run() {
   using clock = std::chrono::steady_clock;
   int port = 0;
   Socket s = openSocket(port);
   if (s == kNoSocket)
      return;
   const std::string hello = oscMessage("/mm2/hello", {{true, port, {}}, {false, 0, "data"}});
   clock::time_point lastHello{}, lastAnswer{}, asked{};
   while (running_.load()) {
      const clock::time_point now = clock::now();
      if (now - lastHello > std::chrono::seconds(2)) {
         sendToScript(s, hello);
         lastHello = now;
      }
      {
         std::lock_guard<std::mutex> lock(mutex_);
         if (pendingTicket_ != 0) {
            sendToScript(s, oscMessage("/mm2/sound", {{true, pendingTicket_, {}}, {false, 0, std::string(1, pendingPart_)}}));
            pendingTicket_ = 0;
            asked = now;
         }
         if (status_.askingSound && now - asked > std::chrono::seconds(5)) {
            status_.askingSound = false;
            status_.error = "no answer from the Bitwig script";
         }
         if (status_.connected && now - lastAnswer > std::chrono::seconds(5)) {
            status_.connected = false;
            status_.askingSound = false;
            status_.receiving = false;
         }
      }
      if (waitReadable(s, 100) <= 0)
         continue;
      char packet[65536];
      const int n = static_cast<int>(recv(s, packet, sizeof(packet), 0));
      if (n <= 0)
         continue; // Windows reports a hello to a closed port (no Bitwig) as an error here
      size_t at = 0;
      std::string address, tags, text;
      if (!oscString(packet, static_cast<size_t>(n), at, address) || !oscString(packet, static_cast<size_t>(n), at, tags))
         continue;
      if (address == "/mm2/hello") {
         lastAnswer = clock::now();
         std::lock_guard<std::mutex> lock(mutex_);
         status_.connected = true;
      } else if (address == "/mm2/line" && tags == ",s" && oscString(packet, static_cast<size_t>(n), at, text)) {
         handleLine(text);
      }
   }
   closeSocket(s);
}

} // namespace mm2

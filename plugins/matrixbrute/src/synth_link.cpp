#include "synth_link.h"

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

namespace mb {

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

std::string oscMessage(const std::string &address, int port = -1, const std::string &kind = {}) {
   std::string out = address;
   padTo4(out);
   out += port < 0 ? "," : ",is";
   padTo4(out);
   if (port >= 0) {
      const auto v = static_cast<uint32_t>(port);
      out += static_cast<char>(v >> 24);
      out += static_cast<char>(v >> 16);
      out += static_cast<char>(v >> 8);
      out += static_cast<char>(v);
      out += kind;
      padTo4(out);
   }
   return out;
}

// The OSC string starting at `at`, moving `at` past its padding; false if it runs off the end.
bool oscString(const char *data, size_t size, size_t &at, std::string &out) {
   const void *end = at < size ? std::memchr(data + at, '\0', size - at) : nullptr;
   if (!end)
      return false;
   const size_t length = static_cast<const char *>(end) - (data + at);
   out.assign(data + at, length);
   at = (at + length + 4) & ~size_t(3);
   return at <= size;
}

// Bound to the first free port the script answers on (it can only open its connections as it starts).
Socket openSocket(int &port) {
   for (int p = SynthLink::kClientPortFirst; p < SynthLink::kClientPortFirst + SynthLink::kClientPortCount; ++p) {
      Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (s == kNoSocket)
         return kNoSocket;
      // A read's 160-odd lines can arrive in one burst.
      const int buffer = 1 << 20;
      setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&buffer), sizeof(buffer));
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(static_cast<uint16_t>(p));
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0) {
         port = p;
         return s;
      }
      closeSocket(s);
   }
   return kNoSocket;
}

void sendToScript(Socket s, const std::string &message) {
   sockaddr_in to{};
   to.sin_family = AF_INET;
   to.sin_port = htons(static_cast<uint16_t>(SynthLink::kPort));
   to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   sendto(s, message.data(), static_cast<int>(message.size()), 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to));
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

bool SynthLink::parsePreset(const std::string &line, HardwarePatch &out) {
   const std::vector<std::string> f = split(line, '\t');
   int preset = -1;
   if (f.size() < 4 || f.size() > 7 || f[0] != "PRESET" || !toInt(f[1], preset) || preset < 0 || preset >= kPresetCount)
      return false;
   out = HardwarePatch{};
   out.preset = preset;
   out.name = f[2];
   for (const std::string &cell : split(f[3], ',')) {
      const size_t colon = cell.find(':');
      int cc = -1;
      if (colon == std::string::npos || !toInt(cell.substr(0, colon), cc) || cc < 0 || cc > 127)
         return false;
      char *end = nullptr;
      const std::string text = cell.substr(colon + 1);
      const double v = std::strtod(text.c_str(), &end);
      if (text.empty() || *end || !(v >= 0.0 && v <= 127.0))
         return false;
      out.values.emplace_back(cc, v);
   }
   if (f.size() >= 5 && !f[4].empty()) {
      for (const std::string &cell : split(f[4], ',')) {
         const size_t colon = cell.find(':');
         int state = -1;
         if (colon == std::string::npos || colon == 0 || !toInt(cell.substr(colon + 1), state) || state < 0)
            return false;
         out.switches.emplace_back(cell.substr(0, colon), state);
      }
   }
   if (f.size() >= 6) {
      out.hasMatrix = true;
      if (!f[5].empty())
         for (const std::string &cell : split(f[5], ',')) {
            const size_t dot = cell.find('.');
            int row = -1, column = -1;
            if (dot == std::string::npos || !toInt(cell.substr(0, dot), row) || !toInt(cell.substr(dot + 1), column) ||
                row < 0 || row > 15 || column < 0 || column > 27)
               return false;
            out.matrix.emplace_back(row, column);
         }
   }
   if (f.size() == 7) {
      // "s,s,...;s,s,..." with each step -100 to 100.
      const std::vector<std::string> lfos = split(f[6], ';');
      if (lfos.size() != 2)
         return false;
      for (int i = 0; i < 2; ++i)
         for (const std::string &step : split(lfos[i], ',')) {
            int v = 0;
            if (!toInt(step, v) || v < -100 || v > 100)
               return false;
            out.customLfo[i].push_back(v / 100.0f);
         }
   }
   return !out.values.empty();
}

void SynthLink::handleLine(const std::string &line) {
   std::lock_guard<std::mutex> lock(mutex_);
   const std::vector<std::string> f = split(line, '\t');
   if (f[0] == "READING") {
      incoming_.clear();
      status_.reading = true;
      status_.found = 0;
      status_.failed = 0;
      status_.error.clear();
   } else if (f[0] == "PRESET") {
      HardwarePatch p;
      if (parsePreset(line, p)) {
         incoming_.push_back(std::move(p));
         status_.found = static_cast<int>(incoming_.size());
      }
   } else if (f[0] == "DONE") {
      // A connection made after a read starts gets the last complete read's lines, then DONE.
      std::sort(incoming_.begin(), incoming_.end(),
                [](const HardwarePatch &a, const HardwarePatch &b) { return a.preset < b.preset; });
      presets_ = std::move(incoming_);
      incoming_.clear();
      status_.reading = false;
      status_.found = static_cast<int>(presets_.size());
      if (f.size() >= 3)
         toInt(f[2], status_.failed);
      version_.fetch_add(1);
   } else if (f[0] == "FAILED") {
      incoming_.clear();
      status_.reading = false;
      status_.error = f.size() >= 2 ? f[1] : "stopped";
   }
}

bool SynthLink::requestRead() {
   if (!status().connected)
      return false;
   readRequested_.store(true);
   return true;
}

SynthLink::Status SynthLink::status() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return status_;
}

std::vector<HardwarePatch> SynthLink::presets() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return presets_;
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
   status_.reading = false;
}

// Subscribes to the script's preset lines and repeats that every couple of seconds, which also
// tells whether the script is there: it answers each hello.
void SynthLink::run() {
   using clock = std::chrono::steady_clock;
   int port = 0;
   Socket s = openSocket(port);
   if (s == kNoSocket)
      return;
   const std::string hello = oscMessage("/mb/hello", port, "data");
   clock::time_point lastHello{}, lastAnswer{};
   while (running_.load()) {
      const clock::time_point now = clock::now();
      if (now - lastHello > std::chrono::seconds(2)) {
         sendToScript(s, hello);
         lastHello = now;
      }
      if (readRequested_.exchange(false))
         sendToScript(s, oscMessage("/mb/read"));
      {
         std::lock_guard<std::mutex> lock(mutex_);
         if (status_.connected && now - lastAnswer > std::chrono::seconds(5)) {
            status_.connected = false;
            status_.reading = false;
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
      if (!oscString(packet, n, at, address) || !oscString(packet, n, at, tags))
         continue;
      if (address == "/mb/hello") {
         lastAnswer = clock::now();
         std::lock_guard<std::mutex> lock(mutex_);
         status_.connected = true;
      } else if (address == "/mb/line" && tags == ",s" && oscString(packet, n, at, text)) {
         handleLine(text);
      }
   }
   closeSocket(s);
}

} // namespace mb

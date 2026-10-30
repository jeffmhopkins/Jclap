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

// The script's messages from us carry a 4-byte big-endian length (Bitwig's RemoteConnection).
bool sendMessage(Socket s, const std::string &text) {
   std::string frame(4, '\0');
   const auto n = static_cast<uint32_t>(text.size());
   frame[0] = static_cast<char>(n >> 24);
   frame[1] = static_cast<char>(n >> 16);
   frame[2] = static_cast<char>(n >> 8);
   frame[3] = static_cast<char>(n);
   frame += text;
   return send(s, frame.data(), static_cast<int>(frame.size()), 0) == static_cast<int>(frame.size());
}

Socket connectToScript() {
   Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (s == kNoSocket)
      return kNoSocket;
   sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_port = htons(static_cast<uint16_t>(SynthLink::kPort));
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   if (connect(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
      closeSocket(s);
      return kNoSocket;
   }
   return s;
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
   if (f.size() != 4 || f[0] != "PRESET" || !toInt(f[1], preset) || preset < 0 || preset >= kPresetCount)
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

void SynthLink::run() {
   using namespace std::chrono;
   while (running_.load()) {
      Socket s = connectToScript();
      if (s == kNoSocket || !sendMessage(s, "data")) {
         if (s != kNoSocket)
            closeSocket(s);
         // Bitwig, or the script, isn't running: try again in a second.
         for (int i = 0; i < 10 && running_.load(); ++i)
            std::this_thread::sleep_for(milliseconds(100));
         continue;
      }
      {
         std::lock_guard<std::mutex> lock(mutex_);
         status_.connected = true;
      }
      std::string buffer;
      while (running_.load()) {
         if (readRequested_.exchange(false) && !sendMessage(s, "read"))
            break;
         const int r = waitReadable(s, 100);
         if (r < 0)
            break;
         if (r == 0)
            continue;
         char chunk[4096];
         const int n = static_cast<int>(recv(s, chunk, sizeof(chunk), 0));
         if (n <= 0)
            break;
         for (int i = 0; i < n; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
               if (!buffer.empty())
                  handleLine(buffer);
               buffer.clear();
            } else if (c == '\t' || (c >= ' ' && c <= '~')) {
               // Anything else (a length prefix, say) isn't part of a line.
               buffer += c;
            }
         }
      }
      closeSocket(s);
      std::lock_guard<std::mutex> lock(mutex_);
      status_.connected = false;
      status_.reading = false;
   }
}

} // namespace mb

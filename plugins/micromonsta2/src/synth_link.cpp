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
   pendingRequest_ = "sound " + std::to_string(lastTicket_) + " " + part;
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
         std::string request;
         {
            std::lock_guard<std::mutex> lock(mutex_);
            request.swap(pendingRequest_);
         }
         if (!request.empty() && !sendMessage(s, request))
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
      status_.askingSound = false;
      status_.receiving = false;
   }
}

} // namespace mm2

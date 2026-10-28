#include "discovery.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#ifdef _MSC_VER
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <mdns.h>
#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace jtunnel {
namespace {

using Clock = std::chrono::steady_clock;

const char kService[] = "_jtunnel._udp.local.";
constexpr size_t kServiceLength = sizeof(kService) - 1;
constexpr uint32_t kTtlSeconds = 30;
constexpr auto kAnnounceEvery = std::chrono::seconds(10);
constexpr auto kQueryEvery = std::chrono::seconds(5);
// A tunnel not heard from in this long has gone (it announces every 10 s).
constexpr auto kForgetAfter = std::chrono::seconds(35);
// How long a new sender listens for someone already using its name before it announces. Our first
// queries go out at once and every 2 s, and anyone with a name answers a query straight away.
constexpr auto kProbeFor = std::chrono::seconds(3);

uint64_t wallClockMs() {
   return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
         .count());
}

bool sameName(const std::string &a, const std::string &b) {
   if (a.size() != b.size())
      return false;
   for (size_t i = 0; i < a.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
         return false;
   return true;
}

bool endsWithService(const std::string &name) {
   if (name.size() <= kServiceLength + 1)
      return false;
   return sameName(name.substr(name.size() - kServiceLength), kService) && name[name.size() - kServiceLength - 1] == '.';
}

struct Published {
   enum State { Probing, Active, Blocked };
   std::string name;
   std::string instance; // "<name>._jtunnel._udp.local."
   uint16_t port = 0;
   uint64_t owner = 0;
   bool announced = false;
   State state = Probing;
   Clock::time_point created;
   uint64_t since = 0;  // when it started claiming the name, ms on the wall clock
   std::string takenBy; // while Blocked: who has the name
};

struct Seen {
   std::string name;
   net::Address address;
   Clock::time_point lastSeen;
   uint64_t owner = 0, since = 0; // from its TXT record; 0 from a sender that doesn't say
};

// Which of two claims to one name wins: the older, and for a tie the lower owner id.
bool claimsFirst(uint64_t since, uint64_t owner, uint64_t otherSince, uint64_t otherOwner) {
   return since != otherSince ? since < otherSince : owner < otherOwner;
}

} // namespace

struct Discovery::Impl {
   std::mutex mutex;
   int users = 0;
   std::thread thread;
   std::atomic<bool> running{false};
   std::condition_variable wake;
   std::atomic<bool> available{false};

   std::map<uint64_t, Published> published;
   std::vector<Published> goodbyes; // unpublished, still to say goodbye on the network
   std::vector<Seen> seen;

   // Filled by the mDNS callback for one received packet.
   struct Answer {
      std::string instance;
      uint16_t port = 0;
      uint32_t ttl = 0;
   };
   std::vector<Answer> answers;
   struct Claim {
      std::string instance;
      uint64_t owner = 0, since = 0;
   };
   std::vector<Claim> claims; // from TXT records
   bool questionForUs = false;
   sockaddr_in source{};
   bool gotSource = false;
   char text[256];

   void run();
   void settleClaims(Clock::time_point now);
   void announce(int sock, const Published &p, bool goodbye);
   void handlePacket(const sockaddr *from);
   static int callback(int sock, const sockaddr *from, size_t addrlen, mdns_entry_type_t entry, uint16_t queryId,
                       uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size,
                       size_t nameOffset, size_t nameLength, size_t recordOffset, size_t recordLength, void *user);
};

namespace {
Discovery::Impl &impl() {
   static Discovery::Impl instance;
   return instance;
}
} // namespace

Discovery &Discovery::instance() {
   static Discovery discovery;
   return discovery;
}

Discovery &Discovery::acquire() {
   Impl &d = impl();
   std::lock_guard<std::mutex> lock(d.mutex);
   if (d.users++ == 0) {
      net::startup();
      d.running = true;
      d.thread = std::thread([&d] { d.run(); });
   }
   return instance();
}

void Discovery::release() {
   Impl &d = impl();
   std::thread stopping;
   {
      std::lock_guard<std::mutex> lock(d.mutex);
      if (d.users == 0 || --d.users > 0)
         return;
      d.running = false;
      stopping = std::move(d.thread);
   }
   d.wake.notify_all();
   if (stopping.joinable())
      stopping.join();
   std::lock_guard<std::mutex> lock(d.mutex);
   // Someone may have acquired it again while it stopped.
   if (d.users == 0) {
      d.seen.clear();
      net::cleanup();
   }
}

uint64_t Discovery::newOwnerId() {
   static std::atomic<uint64_t> next{std::random_device{}() | 1ull << 40};
   return next.fetch_add(1);
}

void Discovery::publish(uint64_t owner, const std::string &name, uint16_t port) {
   Impl &d = impl();
   {
      std::lock_guard<std::mutex> lock(d.mutex);
      auto it = d.published.find(owner);
      if (it != d.published.end() && it->second.name == name && it->second.port == port)
         return;
      if (it != d.published.end() && it->second.announced)
         d.goodbyes.push_back(it->second);
      Published p;
      p.name = name;
      p.instance = name + "." + kService;
      p.port = port;
      p.owner = owner;
      p.created = Clock::now();
      p.since = wallClockMs();
      d.published[owner] = p;
   }
   d.wake.notify_all();
}

void Discovery::unpublish(uint64_t owner) {
   Impl &d = impl();
   {
      std::lock_guard<std::mutex> lock(d.mutex);
      auto it = d.published.find(owner);
      if (it == d.published.end())
         return;
      if (it->second.announced)
         d.goodbyes.push_back(it->second);
      d.published.erase(it);
   }
   d.wake.notify_all();
}

std::vector<FoundTunnel> Discovery::tunnels() const {
   Impl &d = impl();
   std::lock_guard<std::mutex> lock(d.mutex);
   std::vector<FoundTunnel> local, remote;
   for (const auto &entry : d.published)
      if (entry.second.state == Published::Active)
         local.push_back({entry.second.name, net::Address::loopback(entry.second.port), true});
   for (const Seen &s : d.seen) {
      // Our own announcements come back to us; the local entry already covers them.
      const bool ours = std::any_of(local.begin(), local.end(), [&](const FoundTunnel &t) {
         return sameName(t.name, s.name) && t.address.port == s.address.port;
      });
      if (!ours)
         remote.push_back({s.name, s.address, false});
   }
   const auto byName = [](const FoundTunnel &a, const FoundTunnel &b) { return a.name < b.name; };
   std::sort(local.begin(), local.end(), byName);
   std::sort(remote.begin(), remote.end(), byName);
   local.insert(local.end(), remote.begin(), remote.end());
   return local;
}

bool Discovery::resolve(const std::string &name, net::Address &out) const {
   for (const FoundTunnel &t : tunnels())
      if (sameName(t.name, name)) {
         out = t.address;
         return true;
      }
   return false;
}

bool Discovery::networkAvailable() const { return impl().available; }

bool Discovery::nameTaken(uint64_t owner, std::string &by) const {
   Impl &d = impl();
   std::lock_guard<std::mutex> lock(d.mutex);
   auto it = d.published.find(owner);
   if (it == d.published.end() || it->second.state != Published::Blocked)
      return false;
   by = it->second.takenBy;
   return true;
}

void Discovery::Impl::settleClaims(Clock::time_point now) {
   for (auto &entry : published) {
      const uint64_t owner = entry.first;
      Published &p = entry.second;
      // Everyone else holding this name: other instances here that announce it, and senders seen
      // on the network (not our own announcement coming back).
      bool rival = false, rivalFirst = false;
      std::string by;
      for (const auto &other : published) {
         if (other.first == owner || other.second.state != Published::Active || !sameName(other.second.name, p.name))
            continue;
         rival = true;
         by = "another track here";
         rivalFirst = rivalFirst || claimsFirst(other.second.since, other.first, p.since, owner);
      }
      for (const Seen &s : seen) {
         if (!sameName(s.name, p.name))
            continue;
         // Our own announcement coming back, or another instance here's (counted above). One
         // without an id (an older JTunnel) can only be told by its port.
         const bool here = s.owner ? published.count(s.owner) > 0 : s.address.port == p.port;
         if (here)
            continue;
         rival = true;
         by = s.address.toString();
         rivalFirst = rivalFirst || s.owner == 0 || claimsFirst(s.since, s.owner, p.since, owner);
      }
      switch (p.state) {
      case Published::Probing:
         if (rival) {
            p.state = Published::Blocked;
            p.takenBy = by;
         }
         else if (now - p.created >= kProbeFor || !available)
            p.state = Published::Active;
         break;
      case Published::Active:
         // Only another announcing claim that is older can take the name from us.
         if (rival && rivalFirst) {
            p.state = Published::Blocked;
            p.takenBy = by;
            if (p.announced)
               goodbyes.push_back(p);
            p.announced = false;
         }
         break;
      case Published::Blocked:
         if (!rival) {
            // The other one went: the name is ours now.
            p.state = Published::Active;
            p.takenBy.clear();
         }
         else
            p.takenBy = by;
         break;
      }
   }
}

int Discovery::Impl::callback(int, const sockaddr *from, size_t, mdns_entry_type_t entry, uint16_t, uint16_t rtype,
                              uint16_t, uint32_t ttl, const void *data, size_t size, size_t nameOffset, size_t,
                              size_t recordOffset, size_t recordLength, void *user) {
   Impl &d = *static_cast<Impl *>(user);
   if (from && from->sa_family == AF_INET) {
      std::memcpy(&d.source, from, sizeof(d.source));
      d.gotSource = true;
   }
   const mdns_string_t name = mdns_string_extract(data, size, &nameOffset, d.text, sizeof(d.text));
   const std::string recordName(name.str, name.length);

   if (entry == MDNS_ENTRYTYPE_QUESTION) {
      if ((rtype == MDNS_RECORDTYPE_PTR || rtype == MDNS_RECORDTYPE_ANY) && sameName(recordName, kService))
         d.questionForUs = true;
      return 0;
   }
   if (rtype == MDNS_RECORDTYPE_TXT && endsWithService(recordName)) {
      mdns_record_txt_t txt[8];
      const size_t count = mdns_record_parse_txt(data, size, recordOffset, recordLength, txt, 8);
      Claim claim;
      claim.instance = recordName;
      for (size_t i = 0; i < count; ++i) {
         const std::string key(txt[i].key.str, txt[i].key.length);
         const std::string value(txt[i].value.str, txt[i].value.length);
         if (key == "id")
            claim.owner = std::strtoull(value.c_str(), nullptr, 16);
         else if (key == "since")
            claim.since = std::strtoull(value.c_str(), nullptr, 10);
      }
      d.claims.push_back(claim);
   }
   if (rtype == MDNS_RECORDTYPE_SRV && endsWithService(recordName)) {
      char target[256];
      const mdns_record_srv_t srv = mdns_record_parse_srv(data, size, recordOffset, recordLength, target, sizeof(target));
      d.answers.push_back({recordName, srv.port, ttl});
   }
   return 0;
}

void Discovery::Impl::handlePacket(const sockaddr *from) {
   if (answers.empty())
      return;
   const auto *in = reinterpret_cast<const sockaddr_in *>(from);
   const uint32_t ip = ntohl(in->sin_addr.s_addr);
   const auto now = Clock::now();
   std::lock_guard<std::mutex> lock(mutex);
   for (const Answer &a : answers) {
      const std::string name = a.instance.substr(0, a.instance.size() - kServiceLength - 1);
      if (name.empty() || name.size() > 31 || a.port == 0)
         continue;
      const net::Address address{ip, a.port};
      uint64_t owner = 0, since = 0;
      for (const Claim &c : claims)
         if (sameName(c.instance, a.instance)) {
            owner = c.owner;
            since = c.since;
         }
      auto it = std::find_if(seen.begin(), seen.end(),
                             [&](const Seen &s) { return sameName(s.name, name) && s.address == address; });
      if (a.ttl == 0) {
         if (it != seen.end())
            seen.erase(it);
      }
      else if (it != seen.end()) {
         it->lastSeen = now;
         it->owner = owner;
         it->since = since;
      }
      else
         seen.push_back({name, address, now, owner, since});
   }
}

void Discovery::Impl::announce(int sock, const Published &p, bool goodbye) {
   alignas(4) static uint8_t buffer[2048];
   mdns_record_t ptr{};
   ptr.name = {kService, kServiceLength};
   ptr.type = MDNS_RECORDTYPE_PTR;
   ptr.data.ptr.name = {p.instance.c_str(), p.instance.size()};
   ptr.rclass = MDNS_CLASS_IN;
   ptr.ttl = kTtlSeconds;

   static const char kHost[] = "jtunnel.local.";
   mdns_record_t srv{};
   srv.name = {p.instance.c_str(), p.instance.size()};
   srv.type = MDNS_RECORDTYPE_SRV;
   srv.data.srv.priority = 0;
   srv.data.srv.weight = 0;
   srv.data.srv.port = p.port;
   srv.data.srv.name = {kHost, sizeof(kHost) - 1};
   srv.rclass = MDNS_CLASS_IN;
   srv.ttl = kTtlSeconds;

   // Who claims the name and since when, so two senders with one name can settle who keeps it.
   char idText[24], sinceText[24];
   std::snprintf(idText, sizeof(idText), "%llx", static_cast<unsigned long long>(p.owner));
   std::snprintf(sinceText, sizeof(sinceText), "%llu", static_cast<unsigned long long>(p.since));
   mdns_record_t records[3] = {srv, {}, {}};
   const char *keys[2] = {"id", "since"};
   const char *values[2] = {idText, sinceText};
   for (int i = 0; i < 2; ++i) {
      mdns_record_t &txt = records[1 + i];
      txt.name = {p.instance.c_str(), p.instance.size()};
      txt.type = MDNS_RECORDTYPE_TXT;
      txt.data.txt.key = {keys[i], std::strlen(keys[i])};
      txt.data.txt.value = {values[i], std::strlen(values[i])};
      txt.rclass = MDNS_CLASS_IN;
      txt.ttl = kTtlSeconds;
   }

   if (goodbye)
      mdns_goodbye_multicast(sock, buffer, sizeof(buffer), ptr, nullptr, 0, records, 3);
   else
      mdns_announce_multicast(sock, buffer, sizeof(buffer), ptr, nullptr, 0, records, 3);
}

void Discovery::Impl::run() {
   sockaddr_in bindAddr{};
   bindAddr.sin_family = AF_INET;
   bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
   bindAddr.sin_port = htons(MDNS_PORT);
#ifdef __APPLE__
   bindAddr.sin_len = sizeof(bindAddr);
#endif
   int sock = mdns_socket_open_ipv4(&bindAddr);
   available = sock >= 0;

   alignas(4) static uint8_t buffer[4096];
   auto lastAnnounce = Clock::now() - kAnnounceEvery;
   auto lastQuery = Clock::now() - kQueryEvery;
   auto started = Clock::now();
   bool probing = false;

   while (running) {
      const auto now = Clock::now();
      std::vector<Published> toAnnounce, toGoodbye;
      {
         std::lock_guard<std::mutex> lock(mutex);
         seen.erase(std::remove_if(seen.begin(), seen.end(),
                                   [&](const Seen &s) { return now - s.lastSeen > kForgetAfter; }),
                    seen.end());
         settleClaims(now);
         const bool periodic = now - lastAnnounce >= kAnnounceEvery || questionForUs;
         probing = false;
         for (auto &entry : published) {
            probing = probing || entry.second.state == Published::Probing;
            if (entry.second.state == Published::Active && (periodic || !entry.second.announced)) {
               toAnnounce.push_back(entry.second);
               entry.second.announced = true;
            }
         }
         if (periodic)
            lastAnnounce = now;
         questionForUs = false;
         toGoodbye.swap(goodbyes);
      }
      if (sock >= 0) {
         for (const Published &p : toGoodbye)
            announce(sock, p, true);
         for (const Published &p : toAnnounce)
            announce(sock, p, false);
         // Ask more often for the first few seconds, so a new instance fills its list quickly.
         // A sender checking whether its name is free asks every second.
         const auto queryEvery = probing                                      ? std::chrono::seconds(1)
                                 : now - started < std::chrono::seconds(6) ? std::chrono::seconds(2)
                                                                           : kQueryEvery;
         if (now - lastQuery >= queryEvery) {
            mdns_query_send(sock, MDNS_RECORDTYPE_PTR, kService, kServiceLength, buffer, sizeof(buffer), 0);
            lastQuery = now;
         }

         fd_set readable;
         FD_ZERO(&readable);
#ifdef _WIN32
         FD_SET(static_cast<SOCKET>(sock), &readable);
#else
         FD_SET(sock, &readable);
#endif
         timeval timeout{0, 100000};
         if (select(sock + 1, &readable, nullptr, nullptr, &timeout) > 0) {
            // Drain everything waiting; the socket is non-blocking. mdns_socket_listen reads one
            // packet and reports its questions and answers alike.
            for (int i = 0; i < 64; ++i) {
               answers.clear();
               claims.clear();
               gotSource = false;
               if (mdns_socket_listen(sock, buffer, sizeof(buffer), callback, this) == 0)
                  break;
               if (gotSource)
                  handlePacket(reinterpret_cast<const sockaddr *>(&source));
            }
         }
      }
      else {
         std::unique_lock<std::mutex> lock(mutex);
         wake.wait_for(lock, std::chrono::milliseconds(200));
      }
   }

   if (sock >= 0) {
      std::vector<Published> all;
      {
         std::lock_guard<std::mutex> lock(mutex);
         for (auto &entry : published)
            if (entry.second.announced)
               all.push_back(entry.second);
         for (auto &p : goodbyes)
            all.push_back(p);
         goodbyes.clear();
      }
      for (const Published &p : all)
         announce(sock, p, true);
      mdns_socket_close(sock);
   }
   available = false;
}

} // namespace jtunnel

#include "script_link.h"

#include <algorithm>
#include <cstring>

namespace jtunnel {
namespace {

constexpr auto kHelloEvery = std::chrono::seconds(1);
// The script says it's alive every second; after this long without a word it has gone (Bitwig
// closed, the extension removed) and what it said is dropped.
constexpr auto kScriptTimeout = std::chrono::seconds(4);
// Pitch bend, pressure and notes go no more often than this while they move (15 a second: every
// other frame of the extension's 30 a second, smooth enough for a meter, and a small fraction of a
// MIDI stream), and this often while they don't, so the script knows the sender is still there.
constexpr auto kReportAtMost = std::chrono::microseconds(1000000 / 15);
constexpr auto kReportAtLeast = std::chrono::seconds(1);

void padTo4(std::string &out) {
   while (out.size() % 4)
      out += '\0';
}

void putString(std::string &out, const std::string &s) {
   out += s;
   out += '\0';
   padTo4(out);
}

bool getString(const uint8_t *data, size_t size, size_t &at, std::string &out) {
   if (at >= size)
      return false;
   const uint8_t *end = static_cast<const uint8_t *>(std::memchr(data + at, 0, size - at));
   if (!end)
      return false;
   out.assign(reinterpret_cast<const char *>(data + at), static_cast<size_t>(end - (data + at)));
   at = static_cast<size_t>(end - data) + 1;
   at = (at + 3) & ~static_cast<size_t>(3);
   return at <= size;
}

} // namespace

std::string ScriptLink::message(const std::string &address, const std::string &tags, const int32_t *ints,
                                const std::string *strings) {
   std::string out;
   putString(out, address);
   putString(out, "," + tags);
   for (const char t : tags) {
      if (t == 'i') {
         const uint32_t v = static_cast<uint32_t>(*ints++);
         const char b[4] = {static_cast<char>(v >> 24), static_cast<char>(v >> 16), static_cast<char>(v >> 8),
                            static_cast<char>(v)};
         out.append(b, 4);
      }
      else if (t == 's')
         putString(out, *strings++);
   }
   return out;
}

bool ScriptLink::parse(const uint8_t *data, size_t size, std::string &address, std::string &tags,
                       std::vector<int32_t> &ints, std::vector<std::string> &strings) {
   size_t at = 0;
   if (!getString(data, size, at, address) || address.empty() || address[0] != '/')
      return false;
   if (!getString(data, size, at, tags) || tags.empty() || tags[0] != ',')
      return false;
   tags.erase(tags.begin());
   ints.clear();
   strings.clear();
   for (const char t : tags) {
      if (t == 'i') {
         if (at + 4 > size)
            return false;
         ints.push_back(static_cast<int32_t>(static_cast<uint32_t>(data[at]) << 24 | static_cast<uint32_t>(data[at + 1]) << 16 |
                                             static_cast<uint32_t>(data[at + 2]) << 8 | data[at + 3]));
         at += 4;
      }
      else if (t == 's') {
         std::string s;
         if (!getString(data, size, at, s))
            return false;
         strings.push_back(std::move(s));
      }
      else
         return false;
   }
   return true;
}

bool ScriptLink::open() {
   close();
   for (int i = 0; i < kClientPortCount; ++i)
      if (socket_.open(static_cast<uint16_t>(kClientPortFirst + i), true))
         return true;
   return false;
}

void ScriptLink::close() {
   socket_.close();
   heard_ = false;
   lastHello_ = {};
   lastReport_ = {};
}

bool ScriptLink::connected() const { return heard_ && Clock::now() - lastHeard_ < kScriptTimeout; }

void ScriptLink::forgetScriptKeys(StateStore &store) {
   for (const std::string &key : keys_)
      store.remove(key);
   keys_.clear();
}

void ScriptLink::reportExpression(const std::string &track, const std::string &color, const std::string &json) {
   if (!socket_.isOpen() || track.empty() || json.empty())
      return;
   const bool changed = json != reportedJson_ || track != reportedTrack_ || color != reportedColor_;
   const auto now = Clock::now();
   const auto since = now - lastReport_;
   if (since < kReportAtMost || (!changed && since < kReportAtLeast))
      return;
   lastReport_ = now;
   reportedTrack_ = track;
   reportedColor_ = color;
   reportedJson_ = json;
   const int32_t port = socket_.localPort();
   const std::string strings[3] = {track, color, json};
   const std::string msg = message("/jtunnel/expression", "isss", &port, strings);
   socket_.sendTo(net::Address::loopback(kScriptPort), msg.data(), msg.size());
   ++reports_;
}

void ScriptLink::poll(StateStore &store, const std::string &tunnelName, int timeoutMs) {
   if (!socket_.isOpen()) {
      forgetScriptKeys(store);
      return;
   }
   auto now = Clock::now();
   if (now - lastHello_ >= kHelloEvery) {
      lastHello_ = now;
      const int32_t port = socket_.localPort();
      const std::string hello = message("/jtunnel/hello", "is", &port, &tunnelName);
      socket_.sendTo(net::Address::loopback(kScriptPort), hello.data(), hello.size());
   }
   // A whole OSC message is one datagram; the script's largest (a track with its clips) is a few KB.
   static thread_local std::vector<uint8_t> buffer(65536);
   std::string address, tags;
   std::vector<int32_t> ints;
   std::vector<std::string> strings;
   for (int wait = timeoutMs;; wait = 0) {
      net::Address from;
      const int n = socket_.receive(buffer.data(), buffer.size(), from, wait);
      if (n <= 0)
         break;
      // Only the script, from this computer.
      if (from.ip != 0x7F000001u || !parse(buffer.data(), static_cast<size_t>(n), address, tags, ints, strings))
         continue;
      lastHeard_ = Clock::now();
      heard_ = true;
      ++received_;
      if (address == "/jtunnel/state" && tags == "ss") {
         store.put(strings[0], strings[1]);
         if (isValidStateKey(strings[0]) && std::find(keys_.begin(), keys_.end(), strings[0]) == keys_.end() &&
             keys_.size() < StateStore::kMaxKeys)
            keys_.push_back(strings[0]);
      }
      else if (address == "/jtunnel/remove" && tags == "s") {
         store.remove(strings[0]);
         keys_.erase(std::remove(keys_.begin(), keys_.end(), strings[0]), keys_.end());
      }
   }
   if (heard_ && Clock::now() - lastHeard_ >= kScriptTimeout) {
      heard_ = false;
      forgetScriptKeys(store);
   }
}

} // namespace jtunnel

#include "state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace jtunnel {
namespace {

class JsonChecker {
public:
   JsonChecker(const char *text, size_t size) : p_(text), end_(text + size) {}

   bool check() {
      space();
      if (!value(0))
         return false;
      space();
      return p_ == end_;
   }

private:
   void space() {
      while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r'))
         ++p_;
   }
   bool literal(const char *word) {
      const size_t n = std::strlen(word);
      if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, word, n))
         return false;
      p_ += n;
      return true;
   }
   bool string() {
      if (p_ >= end_ || *p_ != '"')
         return false;
      ++p_;
      while (p_ < end_) {
         const unsigned char c = static_cast<unsigned char>(*p_++);
         if (c == '"')
            return true;
         if (c < 0x20)
            return false;
         if (c == '\\') {
            if (p_ >= end_)
               return false;
            const char e = *p_++;
            if (e == 'u') {
               for (int i = 0; i < 4; ++i, ++p_)
                  if (p_ >= end_ || !std::isxdigit(static_cast<unsigned char>(*p_)))
                     return false;
            }
            else if (!std::strchr("\"\\/bfnrt", e))
               return false;
         }
      }
      return false;
   }
   bool digits() {
      const char *start = p_;
      while (p_ < end_ && *p_ >= '0' && *p_ <= '9')
         ++p_;
      return p_ > start;
   }
   bool number() {
      if (p_ < end_ && *p_ == '-')
         ++p_;
      if (p_ < end_ && *p_ == '0')
         ++p_;
      else if (!digits())
         return false;
      if (p_ < end_ && *p_ == '.') {
         ++p_;
         if (!digits())
            return false;
      }
      if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
         ++p_;
         if (p_ < end_ && (*p_ == '+' || *p_ == '-'))
            ++p_;
         if (!digits())
            return false;
      }
      return true;
   }
   bool value(int depth) {
      if (p_ >= end_ || depth > 32)
         return false;
      switch (*p_) {
      case '{': {
         ++p_;
         space();
         if (p_ < end_ && *p_ == '}') {
            ++p_;
            return true;
         }
         for (;;) {
            space();
            if (!string())
               return false;
            space();
            if (p_ >= end_ || *p_ != ':')
               return false;
            ++p_;
            space();
            if (!value(depth + 1))
               return false;
            space();
            if (p_ < end_ && *p_ == ',') {
               ++p_;
               continue;
            }
            if (p_ < end_ && *p_ == '}') {
               ++p_;
               return true;
            }
            return false;
         }
      }
      case '[': {
         ++p_;
         space();
         if (p_ < end_ && *p_ == ']') {
            ++p_;
            return true;
         }
         for (;;) {
            space();
            if (!value(depth + 1))
               return false;
            space();
            if (p_ < end_ && *p_ == ',') {
               ++p_;
               continue;
            }
            if (p_ < end_ && *p_ == ']') {
               ++p_;
               return true;
            }
            return false;
         }
      }
      case '"': return string();
      case 't': return literal("true");
      case 'f': return literal("false");
      case 'n': return literal("null");
      default: return number();
      }
   }

   const char *p_, *end_;
};

} // namespace

bool isValidJson(const char *text, size_t size) { return JsonChecker(text, size).check(); }

std::string jsonQuote(const std::string &text) {
   std::string out = "\"";
   for (const char ch : text) {
      const unsigned char c = static_cast<unsigned char>(ch);
      switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
         if (c < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
         }
         else
            out += ch;
      }
   }
   return out + "\"";
}

bool isValidStateKey(const char *key, size_t size) {
   if (size == 0 || size > kMaxStateKey)
      return false;
   for (size_t i = 0; i < size; ++i) {
      const unsigned char c = static_cast<unsigned char>(key[i]);
      if (!std::isalnum(c) && c != '/' && c != '_' && c != '.' && c != '-')
         return false;
   }
   return true;
}

// ---- StateStore ----

bool StateStore::put(const std::string &key, const std::string &value) {
   if (!isValidStateKey(key) || value.empty() || value.size() > kMaxStateBytes || !isValidJson(value))
      return false;
   std::lock_guard<std::mutex> lock(mutex_);
   auto it = values_.find(key);
   if (it != values_.end() && it->second.json == value)
      return false;
   if (it == values_.end() && values_.size() >= kMaxKeys)
      return false;
   const uint64_t v = version_.load(std::memory_order_relaxed) + 1;
   values_[key] = {value, v};
   version_.store(v, std::memory_order_release);
   updates_.fetch_add(1, std::memory_order_relaxed);
   return true;
}

bool StateStore::remove(const std::string &key) {
   std::lock_guard<std::mutex> lock(mutex_);
   if (!values_.erase(key))
      return false;
   // A removal can't be sent as a change, so it starts readers again.
   epoch_.fetch_add(1, std::memory_order_acq_rel);
   version_.store(version_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
   return true;
}

bool StateStore::get(const std::string &key, std::string &value) const {
   std::lock_guard<std::mutex> lock(mutex_);
   auto it = values_.find(key);
   if (it == values_.end())
      return false;
   value = it->second.json;
   return true;
}

void StateStore::clear() {
   std::lock_guard<std::mutex> lock(mutex_);
   if (values_.empty())
      return;
   values_.clear();
   epoch_.fetch_add(1, std::memory_order_acq_rel);
   version_.store(version_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

size_t StateStore::size() const {
   std::lock_guard<std::mutex> lock(mutex_);
   return values_.size();
}

uint64_t StateStore::changes(uint64_t since, std::vector<StateEntry> &out, uint32_t *epoch) const {
   std::lock_guard<std::mutex> lock(mutex_);
   for (const auto &kv : values_)
      if (kv.second.version > since)
         out.push_back({kv.first, kv.second.json, kv.second.version});
   if (epoch)
      *epoch = epoch_.load(std::memory_order_relaxed);
   return version_.load(std::memory_order_relaxed);
}

std::string StateStore::snapshotJson() const {
   std::lock_guard<std::mutex> lock(mutex_);
   std::string out = "{";
   bool first = true;
   for (const auto &kv : values_) {
      if (!first)
         out += ',';
      first = false;
      out += jsonQuote(kv.first);
      out += ':';
      out += kv.second.json;
   }
   return out + "}";
}

// ---- StateAssembler ----

void StateAssembler::reset() {
   partial_.clear();
   done_.clear();
}

bool StateAssembler::add(const StatePiece &piece, std::string &key, std::string &value) {
   if (!isValidStateKey(piece.key, piece.keySize))
      return false;
   std::string k(piece.key, piece.keySize);
   auto done = done_.find(k);
   if (done != done_.end() && done->second == piece.messageId)
      return false; // a refresh of what this already has
   if (piece.pieces == 1) {
      partial_.erase(k);
      done_[k] = piece.messageId;
      key = std::move(k);
      value.assign(reinterpret_cast<const char *>(piece.data), piece.dataSize);
      return true;
   }
   auto it = partial_.find(k);
   if (it == partial_.end() || it->second.messageId != piece.messageId || it->second.pieces != piece.pieces) {
      if (it == partial_.end() && partial_.size() >= kMaxPartial) {
         // Forget the one that has waited longest.
         auto oldest = std::min_element(partial_.begin(), partial_.end(), [](const auto &a, const auto &b) {
            return a.second.lastTouched < b.second.lastTouched;
         });
         partial_.erase(oldest);
      }
      Partial &p = partial_[k];
      p.messageId = piece.messageId;
      p.pieces = piece.pieces;
      p.have = 0;
      p.parts.assign(piece.pieces, std::string());
      p.got.assign(piece.pieces, false);
      it = partial_.find(k);
   }
   Partial &p = it->second;
   p.lastTouched = ++touches_;
   if (!p.got[piece.piece]) {
      p.got[piece.piece] = true;
      p.parts[piece.piece].assign(reinterpret_cast<const char *>(piece.data), piece.dataSize);
      ++p.have;
   }
   if (p.have < p.pieces)
      return false;
   value.clear();
   for (const std::string &part : p.parts)
      value += part;
   done_[k] = piece.messageId;
   partial_.erase(it);
   key = std::move(k);
   return true;
}

// ---- sending ----

void writeStateMessage(uint32_t streamId, uint32_t messageId, const std::string &key, const std::string &value,
                       std::vector<std::vector<uint8_t>> &out) {
   if (!isValidStateKey(key) || value.size() > kMaxStateBytes)
      return;
   const size_t pieces = std::max<size_t>(1, (value.size() + kStatePieceBytes - 1) / kStatePieceBytes);
   for (size_t i = 0; i < pieces; ++i) {
      StatePiece p;
      p.streamId = streamId;
      p.messageId = messageId;
      p.piece = static_cast<uint8_t>(i);
      p.pieces = static_cast<uint8_t>(pieces);
      p.key = key.data();
      p.keySize = static_cast<uint8_t>(key.size());
      const size_t at = i * kStatePieceBytes;
      p.data = reinterpret_cast<const uint8_t *>(value.data()) + at;
      p.dataSize = static_cast<uint16_t>(std::min(kStatePieceBytes, value.size() - at));
      std::vector<uint8_t> packet(kMaxPacketBytes);
      const size_t n = writeState(packet.data(), packet.size(), p);
      if (!n)
         return;
      packet.resize(n);
      out.push_back(std::move(packet));
   }
}

void StateBroadcaster::collect(const StateStore &store, bool everything, int tickMs,
                               std::vector<std::vector<uint8_t>> &out) {
   // The message id is the version the message was set at, so a refresh repeats the same id and a
   // receiver missing one piece of it can finish it from the repeat.
   const auto send = [&](const StateEntry &e) {
      writeStateMessage(streamId_, static_cast<uint32_t>(e.version), e.key, e.value, out);
   };
   scratch_.clear();
   uint32_t epoch = 0;
   const uint64_t version = store.changes(0, scratch_, &epoch);
   if (epoch != epoch_) {
      // Something was removed: the receivers can only learn that from a new stream.
      epoch_ = epoch;
      ++streamId_;
      everything = true;
   }
   if (everything) {
      for (const StateEntry &e : scratch_)
         send(e);
      sent_ = version;
      refreshAt_ = 0;
      return;
   }
   for (const StateEntry &e : scratch_)
      if (e.version > sent_)
         send(e);
   sent_ = version;
   if (scratch_.empty())
      return;
   // The refresh: the whole store once a second, a slice at a time.
   refreshCarry_ += static_cast<double>(scratch_.size()) * tickMs / 1000.0;
   while (refreshCarry_ >= 1.0) {
      refreshCarry_ -= 1.0;
      if (refreshAt_ >= scratch_.size())
         refreshAt_ = 0;
      send(scratch_[refreshAt_++]);
   }
}

// ---- the clock ----

void SharedClock::write(const ClockReading &r) {
   const uint32_t s = seq_.load(std::memory_order_relaxed);
   seq_.store(s + 1, std::memory_order_relaxed);
   std::atomic_thread_fence(std::memory_order_release);
   available_.store(r.available, std::memory_order_relaxed);
   playing_.store(r.playing, std::memory_order_relaxed);
   recording_.store(r.recording, std::memory_order_relaxed);
   looping_.store(r.looping, std::memory_order_relaxed);
   tempo_.store(r.tempo, std::memory_order_relaxed);
   beats_.store(r.beats, std::memory_order_relaxed);
   seconds_.store(r.seconds, std::memory_order_relaxed);
   barStart_.store(r.barStart, std::memory_order_relaxed);
   loopStart_.store(r.loopStart, std::memory_order_relaxed);
   loopEnd_.store(r.loopEnd, std::memory_order_relaxed);
   barNumber_.store(r.barNumber, std::memory_order_relaxed);
   tsigNum_.store(r.tsigNum, std::memory_order_relaxed);
   tsigDen_.store(r.tsigDen, std::memory_order_relaxed);
   seq_.store(s + 2, std::memory_order_release);
}

bool SharedClock::read(ClockReading &r) const {
   const uint32_t before = seq_.load(std::memory_order_acquire);
   if (before & 1)
      return false;
   r.available = available_.load(std::memory_order_relaxed);
   r.playing = playing_.load(std::memory_order_relaxed);
   r.recording = recording_.load(std::memory_order_relaxed);
   r.looping = looping_.load(std::memory_order_relaxed);
   r.tempo = tempo_.load(std::memory_order_relaxed);
   r.beats = beats_.load(std::memory_order_relaxed);
   r.seconds = seconds_.load(std::memory_order_relaxed);
   r.barStart = barStart_.load(std::memory_order_relaxed);
   r.loopStart = loopStart_.load(std::memory_order_relaxed);
   r.loopEnd = loopEnd_.load(std::memory_order_relaxed);
   r.barNumber = barNumber_.load(std::memory_order_relaxed);
   r.tsigNum = tsigNum_.load(std::memory_order_relaxed);
   r.tsigDen = tsigDen_.load(std::memory_order_relaxed);
   std::atomic_thread_fence(std::memory_order_acquire);
   return seq_.load(std::memory_order_relaxed) == before;
}

std::string clockJson(const ClockReading &c) {
   if (!c.available)
      return "{\"available\":false}";
   const auto finite = [](double v) { return std::isfinite(v) ? v : 0.0; };
   const int num = c.tsigNum > 0 ? c.tsigNum : 4, den = c.tsigDen > 0 ? c.tsigDen : 4;
   // A beat is the time signature's unit: a quarter note in 4/4, an eighth in 6/8.
   const double unit = 4.0 / den;
   const double beats = finite(c.beats);
   const double intoBar = std::max(0.0, beats - finite(c.barStart));
   const double beatInBar = intoBar / unit;
   const int beat = std::clamp(static_cast<int>(std::floor(beatInBar)), 0, std::max(0, num - 1)) + 1;
   const double phase = beatInBar - std::floor(beatInBar);
   char text[512];
   std::snprintf(text, sizeof(text),
                 "{\"available\":true,\"playing\":%s,\"recording\":%s,\"looping\":%s,\"tempo\":%.3f,"
                 "\"beats\":%.4f,\"seconds\":%.4f,\"bar\":%d,\"beat\":%d,\"beatPhase\":%.3f,"
                 "\"timeSignature\":[%d,%d],\"loop\":[%.4f,%.4f]}",
                 c.playing ? "true" : "false", c.recording ? "true" : "false", c.looping ? "true" : "false",
                 finite(c.tempo), beats, finite(c.seconds), c.barNumber + 1, beat, phase, num, den,
                 finite(c.loopStart), finite(c.loopEnd));
   return text;
}

} // namespace jtunnel

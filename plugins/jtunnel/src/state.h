// The side channel: what the DAW is doing, as small JSON messages under keys, carried beside the
// audio and MIDI and served on this computer for viewers (state_server.h).
//
// A key names one thing ("transport", "clock", "scenes", "track/3", "device"...) and its message is
// the whole of that thing, as a JSON object. A newer message for a key replaces the older one, so
// a receiver never has to apply changes in order and anything lost is put right by the next
// refresh. On a sender, the Bitwig extension (script_link.h) and the host's own transport (the
// "clock" key) fill the store; the link sends what changed at once, and everything again about once
// a second. On a receiver, the link fills the store from the network.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "protocol.h"

namespace jtunnel {

// True if text is one well-formed JSON value (object, array, string, number, true, false, null),
// with nothing but whitespace around it, nested no deeper than 32.
bool isValidJson(const char *text, size_t size);
inline bool isValidJson(const std::string &text) { return isValidJson(text.data(), text.size()); }
// A JSON string literal, quotes included.
std::string jsonQuote(const std::string &text);
// A key is 1 to 63 of: letters, digits, / _ . and -.
bool isValidStateKey(const char *key, size_t size);
inline bool isValidStateKey(const std::string &key) { return isValidStateKey(key.data(), key.size()); }

struct StateEntry {
   std::string key;
   std::string value; // a JSON value
   uint64_t version = 0; // the store's version when it last changed
};

// Every key's newest message. Thread safe.
class StateStore {
public:
   static constexpr size_t kMaxKeys = 1024;

   // Sets a key's message. False (and nothing changes) if the key or JSON is malformed, the message
   // is the one already there, or the store is full.
   bool put(const std::string &key, const std::string &value);
   bool remove(const std::string &key);
   bool get(const std::string &key, std::string &value) const;
   // Forgets everything, as when the sender restarts or the receiver listens to another tunnel.
   void clear();

   // Goes up by one for each change. 0 for a store that has never held anything.
   uint64_t version() const { return version_.load(std::memory_order_acquire); }
   // Goes up each time the store is cleared; a reader that sees it change starts again from 0.
   uint32_t epoch() const { return epoch_.load(std::memory_order_acquire); }
   size_t size() const;
   // How many messages have changed something since the store was made.
   uint32_t updates() const { return updates_.load(std::memory_order_relaxed); }

   // The entries changed after version `since` (all of them for 0), in key order. Returns the
   // version that brings the caller up to date, and the epoch it belongs to.
   uint64_t changes(uint64_t since, std::vector<StateEntry> &out, uint32_t *epoch = nullptr) const;
   // Everything, as one JSON object of key: message.
   std::string snapshotJson() const;

private:
   struct Value {
      std::string json;
      uint64_t version;
   };
   mutable std::mutex mutex_;
   std::map<std::string, Value> values_;
   std::atomic<uint64_t> version_{0};
   std::atomic<uint32_t> epoch_{0};
   std::atomic<uint32_t> updates_{0};
};

// Puts state messages back together from their pieces, which may come in any order, repeated or
// not at all. Receive thread only.
class StateAssembler {
public:
   // Takes a piece; true when it completes a message, with the message's key and value.
   bool add(const StatePiece &piece, std::string &key, std::string &value);
   void reset();

private:
   struct Partial {
      uint32_t messageId = 0;
      uint8_t pieces = 0;
      int have = 0;
      uint64_t lastTouched = 0;
      std::vector<std::string> parts;
      std::vector<bool> got;
   };
   static constexpr size_t kMaxPartial = 128;
   std::map<std::string, Partial> partial_;
   std::map<std::string, uint32_t> done_; // the last message finished for each key
   uint64_t touches_ = 0;
};

// The state packets for one message (all its pieces), appended to `out` (each a packet's bytes).
void writeStateMessage(uint32_t streamId, uint32_t messageId, const std::string &key, const std::string &value,
                       std::vector<std::vector<uint8_t>> &out);

// Decides what a sender sends of its store: everything changed since last time, now, and a slice of
// everything else each call, so the whole store goes again about once a second.
class StateBroadcaster {
public:
   explicit StateBroadcaster(uint32_t streamId = 1) : streamId_(streamId) {}
   uint32_t streamId() const { return streamId_; }
   // Called every `tickMs`: appends the packets to send now. `everything` resends the whole store
   // (a new listener arrived).
   void collect(const StateStore &store, bool everything, int tickMs, std::vector<std::vector<uint8_t>> &out);

private:
   uint32_t streamId_;
   uint64_t sent_ = 0;
   uint32_t epoch_ = 0;
   size_t refreshAt_ = 0;
   double refreshCarry_ = 0;
   std::vector<StateEntry> scratch_;
};

// The host's transport as CLAP gives it to the audio thread each block, handed to the link without
// locks: the audio thread writes, any thread reads a consistent copy.
struct ClockReading {
   bool available = false; // the host gave a transport
   bool playing = false, recording = false, looping = false;
   double tempo = 0;        // BPM
   double beats = 0;        // song position, in quarter notes
   double seconds = 0;
   double barStart = 0;     // where the current bar started, in quarter notes
   int32_t barNumber = 0;   // CLAP's: 0 for the first bar
   int tsigNum = 4, tsigDen = 4;
   double loopStart = 0, loopEnd = 0; // in quarter notes
};

class SharedClock {
public:
   // Audio thread.
   void write(const ClockReading &reading);
   // Any other thread. False if the audio thread was writing (try again).
   bool read(ClockReading &out) const;

private:
   std::atomic<uint32_t> seq_{0};
   std::atomic<bool> available_{false}, playing_{false}, recording_{false}, looping_{false};
   std::atomic<double> tempo_{0}, beats_{0}, seconds_{0}, barStart_{0}, loopStart_{0}, loopEnd_{0};
   std::atomic<int32_t> barNumber_{0}, tsigNum_{4}, tsigDen_{4};
};

// The "clock" key's message: the transport, the beat and where it is in the bar.
std::string clockJson(const ClockReading &clock);

} // namespace jtunnel

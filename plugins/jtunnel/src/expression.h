// A sender's pitch bend, pressure and held notes, channel by channel, for the side channel's
// `expression` key.
//
// The audio thread feeds it the track's MIDI (as the sender codes it, sender.h). It keeps each
// channel's pitch bend and channel pressure, and each held note with its velocity and its own
// pressure (poly pressure, or a CLAP pressure note expression); a note's own pressure ends with it.
// One track can play on many channels at once, as MPE does (a channel per note), so everything is
// listed by channel:
//
//   - each channel in use (with a held note, a bend off the centre or any pressure): its bend, -1
//     to 1 from the centre, and its pressure, 0 to 1 (the hardest of its channel pressure and its
//     notes' own);
//   - each held note: channel, pitch, velocity (0 to 127), its channel's bend, and its pressure
//     (the harder of its own and its channel's);
//   - and for the track as a whole, the channel bent furthest from the centre and the hardest
//     pressure anywhere.
//
// The audio thread publishes a snapshot when anything changes, without waiting on a lock; the state
// thread turns the latest into JSON when it reports to the Bitwig extension (script_link.h), which
// it does at a modest rate.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "sender.h"

namespace jtunnel {

struct ExpressionSnapshot {
   static constexpr int kMaxNotes = 64; // more held notes than this are left out of the list
   struct Note {
      uint8_t channel, key, velocity;
      float pressure; // the note's own, before its channel's is added
   };
   int16_t bend[16];            // 14-bit, 8192 the centre
   uint8_t channelPressure[16]; // 0 to 127
   bool hasNotes[16];
   float keyPressure[16];       // the hardest of each channel's notes' own pressure
   Note notes[kMaxNotes];
   int noteCount = 0;
};

// The snapshot as the `expression` key's JSON for one track (see the README).
std::string expressionJson(const ExpressionSnapshot &snapshot);

class ExpressionTracker {
public:
   ExpressionTracker() { reset(); }
   void reset();
   // Audio thread: takes one block's events, in order. True if anything changed.
   bool add(const TimedMidi *events, int count);
   const ExpressionSnapshot &snapshot() const { return snapshot_; }

private:
   void add(const uint8_t *data, int size);
   void rebuild();

   int16_t bend_[16];
   uint8_t channelPressure_[16];
   uint8_t velocity_[16][128]; // 0 for a key not held
   float keyPressure_[16][128];
   bool changed_ = false;
   ExpressionSnapshot snapshot_;
};

// The audio thread's latest snapshot, for the state thread.
class SharedExpression {
public:
   SharedExpression() { reset(); }
   // Audio thread: never waits. False if the reader had it; publish again next block.
   bool publish(const ExpressionSnapshot &snapshot);
   // Any other thread: a copy of the latest, and how many have been published.
   uint32_t read(ExpressionSnapshot &out) const;
   // Main thread, while the audio thread isn't running.
   void reset();

private:
   mutable std::mutex mutex_;
   ExpressionSnapshot snapshot_;
   uint32_t version_ = 0;
};

} // namespace jtunnel

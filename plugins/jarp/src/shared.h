#pragma once

// The pattern, and what the audio thread and the panel pass each other. The panel owns its copy of
// the pattern and sends the whole of it whenever it changes; the audio thread publishes a Frame
// through a TripleBuffer every block, which the panel reads each time it draws. Neither side ever
// waits for the other.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace jarp {

constexpr int kRows = 8;
// The most steps a pattern can have; the grid shows kView of them at a time and scrolls.
constexpr int kSteps = 128;
constexpr int kView = 16;
// Note lengths are in quarter steps.
constexpr int kQuarters = 4;
constexpr int kMaxLength = kSteps * kQuarters;
constexpr int kMaxNotes = kRows * kSteps;
constexpr int kMaxRatchet = 8;
constexpr int kMaxFingers = 16;
// Pattern slots 1-8.
constexpr int kSlots = 8;

// What the lanes under the grid hold for a step, for any note starting on it.
struct Lane {
   int8_t octave = 0;   // -3..+3
   uint8_t ratchet = 1; // hits, 1..kMaxRatchet
   uint8_t pressure = 0; // P: pressure adds hits, up to Max Hits
   int8_t velocity = 0; // -64..+64
   uint8_t chance = 100; // 0..100: the percent of times round the note plays
   bool operator==(const Lane &o) const {
      return octave == o.octave && ratchet == o.ratchet && pressure == o.pressure && velocity == o.velocity &&
             chance == o.chance;
   }
};

// Which of a note's own lane values override its step's.
enum Own : uint8_t { kOwnOctave = 1, kOwnRatchet = 2, kOwnPressure = 4, kOwnVelocity = 8, kOwnChance = 16, kOwnAll = 31 };

// A note on the grid: its row (0 is row 1, the lowest), the step it starts on, how long it sounds in
// quarter steps, and any lane values of its own (the bits of `own` say which of `lane` count; the
// rest come from the step).
struct Note {
   uint8_t row = 0, step = 0;
   uint16_t length = kQuarters;
   uint8_t own = 0;
   Lane lane{};
   bool operator==(const Note &o) const {
      return row == o.row && step == o.step && length == o.length && own == o.own &&
             (!(own & kOwnOctave) || lane.octave == o.lane.octave) &&
             (!(own & kOwnRatchet) || lane.ratchet == o.lane.ratchet) &&
             (!(own & kOwnPressure) || lane.pressure == o.lane.pressure) &&
             (!(own & kOwnVelocity) || lane.velocity == o.lane.velocity) &&
             (!(own & kOwnChance) || lane.chance == o.lane.chance);
   }
   // This note's lane: its own values over its step's.
   Lane over(const Lane &step) const {
      Lane l = step;
      if (own & kOwnOctave)
         l.octave = lane.octave;
      if (own & kOwnRatchet)
         l.ratchet = lane.ratchet;
      if (own & kOwnPressure)
         l.pressure = lane.pressure;
      if (own & kOwnVelocity)
         l.velocity = lane.velocity;
      if (own & kOwnChance)
         l.chance = lane.chance;
      return l;
   }
};

struct Pattern {
   // Steps before it loops, 1..kSteps.
   uint8_t length = 8;
   uint16_t count = 0;
   std::array<Note, kMaxNotes> notes{};
   std::array<Lane, kSteps> lanes{};

   // The same notes (in any order) and lanes.
   bool operator==(const Pattern &o) const {
      if (length != o.length || count != o.count || lanes != o.lanes)
         return false;
      for (int i = 0; i < count; ++i) {
         const int j = o.noteStarting(notes[i].row, notes[i].step);
         if (j < 0 || !(o.notes[j] == notes[i]))
            return false;
      }
      return true;
   }
   bool operator!=(const Pattern &o) const { return !(*this == o); }

   // The note covering (row, quarter), or -1.
   int noteAt(int row, int quarter) const {
      for (int i = 0; i < count; ++i)
         if (notes[i].row == row && quarter >= notes[i].step * kQuarters &&
             quarter < notes[i].step * kQuarters + notes[i].length)
            return i;
      return -1;
   }
   // The note starting on (row, step), or -1.
   int noteStarting(int row, int step) const {
      for (int i = 0; i < count; ++i)
         if (notes[i].row == row && notes[i].step == step)
            return i;
      return -1;
   }
   // How long a note starting on (row, step) may be, in quarters: up to the next note in its row,
   // or the end of the grid.
   int room(int row, int step, int except = -1) const {
      int end = kMaxLength;
      for (int i = 0; i < count; ++i)
         if (i != except && notes[i].row == row && notes[i].step > step)
            end = std::min(end, notes[i].step * kQuarters);
      return end - step * kQuarters;
   }
   // Adds a note, shortening it to fit before the next in its row. False if the row is taken there.
   bool add(int row, int step, int length) {
      if (count >= kMaxNotes || row < 0 || row >= kRows || step < 0 || step >= kSteps ||
          noteAt(row, step * kQuarters) >= 0)
         return false;
      notes[count++] = {static_cast<uint8_t>(row), static_cast<uint8_t>(step),
                        static_cast<uint16_t>(std::clamp(length, 1, room(row, step)))};
      return true;
   }
   void remove(int index) {
      if (index < 0 || index >= count)
         return;
      for (int i = index; i + 1 < count; ++i)
         notes[i] = notes[i + 1];
      --count;
   }
   void clear() { count = 0; }
   // Note `i`'s lane: its own values over its step's.
   Lane laneOf(int i) const { return notes[i].over(lanes[notes[i].step]); }
   bool empty() const { return count == 0; }

   // The saved form: a version byte, the length, the note count (two bytes), each note's row, step,
   // length (two bytes) and own bits, with its own lane's five bytes after them when it has any,
   // then the lane count and each lane's five bytes (octave, ratchet, pressure, velocity, chance), up
   // to the last lane that isn't plain. Version 4 (four-byte lanes, no chance) and versions 1 to 3
   // (no own lanes; 1 and 2 with 16 steps and one-byte counts and lengths) still load; a version 1
   // pattern keeps the length it had.
   static constexpr uint8_t kVersion = 5;
   std::vector<uint8_t> save() const {
      std::vector<uint8_t> b;
      b.push_back(kVersion);
      b.push_back(length);
      b.push_back(static_cast<uint8_t>(count & 0xff));
      b.push_back(static_cast<uint8_t>(count >> 8));
      for (int i = 0; i < count; ++i) {
         b.push_back(notes[i].row);
         b.push_back(notes[i].step);
         b.push_back(static_cast<uint8_t>(notes[i].length & 0xff));
         b.push_back(static_cast<uint8_t>(notes[i].length >> 8));
         b.push_back(notes[i].own & kOwnAll);
         if (notes[i].own & kOwnAll)
            putLane(b, notes[i].lane);
      }
      int lanesUsed = 0;
      for (int i = 0; i < kSteps; ++i)
         if (!(lanes[i] == Lane{}))
            lanesUsed = i + 1;
      b.push_back(static_cast<uint8_t>(lanesUsed));
      for (int i = 0; i < lanesUsed; ++i)
         putLane(b, lanes[i]);
      return b;
   }
   bool load(const uint8_t *data, size_t size) {
      if (size < 2 || data[0] < 1 || data[0] > kVersion)
         return false;
      const int version = data[0];
      Pattern p;
      p.length = length;
      size_t at = 1;
      if (version >= 2)
         p.length = static_cast<uint8_t>(std::clamp<int>(data[at++], 1, kSteps));
      const int wide = version >= 3 ? 2 : 1;
      if (size < at + wide)
         return false;
      const int n = version >= 3 ? data[at] | data[at + 1] << 8 : data[at];
      at += wide;
      const size_t noteBytes = 2 + wide + (version >= 4 ? 1 : 0);
      const size_t laneBytes = version >= 5 ? 5 : 4;
      if (n > kMaxNotes)
         return false;
      for (int i = 0; i < n; ++i) {
         if (size < at + noteBytes)
            return false;
         const int length = version >= 3 ? data[at + 2] | data[at + 3] << 8 : data[at + 2];
         const bool added = p.add(data[at], data[at + 1], length);
         const uint8_t own = version >= 4 ? data[at + noteBytes - 1] & (version >= 5 ? kOwnAll : 15) : 0;
         at += noteBytes;
         if (own) {
            if (size < at + laneBytes)
               return false;
            if (added) {
               p.notes[p.count - 1].own = own;
               p.notes[p.count - 1].lane = getLane(data + at, laneBytes);
            }
            at += laneBytes;
         }
      }
      int laneCount = 16;
      if (version >= 3) {
         if (size < at + 1)
            return false;
         laneCount = std::min<int>(data[at++], kSteps);
      }
      if (size < at + laneCount * laneBytes)
         return false;
      for (int i = 0; i < laneCount; ++i, at += laneBytes)
         p.lanes[i] = getLane(data + at, laneBytes);
      *this = p;
      return true;
   }

private:
   static void putLane(std::vector<uint8_t> &b, const Lane &l) {
      b.push_back(static_cast<uint8_t>(l.octave));
      b.push_back(l.ratchet);
      b.push_back(l.pressure);
      b.push_back(static_cast<uint8_t>(l.velocity));
      b.push_back(l.chance);
   }
   static Lane getLane(const uint8_t *d, size_t bytes) {
      Lane l;
      l.octave = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(d[0]), -3, 3));
      l.ratchet = static_cast<uint8_t>(std::clamp<int>(d[1], 1, kMaxRatchet));
      l.pressure = d[2] ? 1 : 0;
      l.velocity = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(d[3]), -64, 64));
      l.chance = bytes >= 5 ? static_cast<uint8_t>(std::min<int>(d[4], 100)) : 100;
      return l;
   }
};

// Every slot, as saved with the project and in a user preset's pattern file: a version byte, the
// slot count, then each slot's size (two bytes) and its pattern.
struct Bank {
   std::array<Pattern, kSlots> slots{};

   bool operator==(const Bank &o) const { return slots == o.slots; }

   static constexpr uint8_t kVersion = 1;
   std::vector<uint8_t> save() const {
      std::vector<uint8_t> b{kVersion, static_cast<uint8_t>(kSlots)};
      for (const Pattern &p : slots) {
         const std::vector<uint8_t> one = p.save();
         b.push_back(static_cast<uint8_t>(one.size() & 0xff));
         b.push_back(static_cast<uint8_t>(one.size() >> 8));
         b.insert(b.end(), one.begin(), one.end());
      }
      return b;
   }
   bool load(const uint8_t *data, size_t size) {
      if (size < 2 || data[0] != kVersion)
         return false;
      // On the heap, as a bank is about 86 KB.
      auto loaded = std::make_unique<Bank>();
      Bank &bank = *loaded;
      size_t at = 2;
      for (int i = 0; i < data[1]; ++i) {
         if (at + 2 > size)
            return false;
         const size_t n = data[at] | data[at + 1] << 8;
         at += 2;
         if (at + n > size)
            return false;
         if (i < kSlots && !bank.slots[i].load(data + at, n))
            return false;
         at += n;
      }
      *this = bank;
      return true;
   }

   // The next slot after `from` with notes in it, going round; `from` itself if no other has any.
   int next(int from) const {
      for (int k = 1; k <= kSlots; ++k) {
         const int i = (from + k) % kSlots;
         if (!slots[i].empty())
            return i;
      }
      return from;
   }
};

// A grid row as it stands: the held note it plays and that finger's live expression.
struct RowView {
   int8_t key = -1;    // the held note, or -1 for none
   int8_t target = -1; // in Degree mode, the pitch the degree wanted (key is the nearest held)
   uint8_t channel = 0;
   float pressure = 0.0f, slide = 0.0f, bend = 0.0f; // 0..1, 0..1, -1..1
};

struct Frame {
   // Whether the arp is running (a finger held or latched), and the playhead in steps from the
   // pattern's start (swing applied, so it moves as the notes do).
   bool running = false;
   double position = 0.0;
   std::array<RowView, kRows> rows{};
   // The hits each step played last time, for the steps whose ratchet follows pressure.
   std::array<uint8_t, kSteps> hits{};
   // The slot playing.
   int8_t slot = 0;
};

} // namespace jarp

#pragma once

// The pattern, and what the audio thread and the panel pass each other. The panel owns its copy of
// the pattern and sends the whole of it whenever it changes; the audio thread publishes a Frame
// through a TripleBuffer every block, which the panel reads each time it draws. Neither side ever
// waits for the other.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jarp {

constexpr int kRows = 8;
constexpr int kSteps = 16;
// Note lengths are in quarter steps.
constexpr int kQuarters = 4;
constexpr int kMaxLength = kSteps * kQuarters;
constexpr int kMaxNotes = kRows * kSteps;
constexpr int kMaxRatchet = 8;
constexpr int kMaxFingers = 16;

// A note on the grid: its row (0 is row 1, the lowest), the step it starts on, and how long it
// sounds in quarter steps.
struct Note {
   uint8_t row = 0, step = 0, length = kQuarters;
   bool operator==(const Note &o) const { return row == o.row && step == o.step && length == o.length; }
};

// What the lanes under the grid hold for a step, for any note starting on it.
struct Lane {
   int8_t octave = 0;   // -3..+3
   uint8_t ratchet = 1; // hits, 1..kMaxRatchet
   uint8_t pressure = 0; // P: pressure adds hits, up to Max Hits
   int8_t velocity = 0; // -64..+64
   bool operator==(const Lane &o) const {
      return octave == o.octave && ratchet == o.ratchet && pressure == o.pressure && velocity == o.velocity;
   }
};

struct Pattern {
   uint8_t count = 0;
   std::array<Note, kMaxNotes> notes{};
   std::array<Lane, kSteps> lanes{};

   // The same notes (in any order) and lanes.
   bool operator==(const Pattern &o) const {
      if (count != o.count || lanes != o.lanes)
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
                        static_cast<uint8_t>(std::clamp(length, 1, room(row, step)))};
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

   // The saved form: a version byte, the note count and each note's three bytes, then each lane's
   // four bytes.
   static constexpr uint8_t kVersion = 1;
   std::vector<uint8_t> save() const {
      std::vector<uint8_t> b;
      b.push_back(kVersion);
      b.push_back(count);
      for (int i = 0; i < count; ++i) {
         b.push_back(notes[i].row);
         b.push_back(notes[i].step);
         b.push_back(notes[i].length);
      }
      for (const Lane &l : lanes) {
         b.push_back(static_cast<uint8_t>(l.octave));
         b.push_back(l.ratchet);
         b.push_back(l.pressure);
         b.push_back(static_cast<uint8_t>(l.velocity));
      }
      return b;
   }
   bool load(const uint8_t *data, size_t size) {
      if (size < 2 || data[0] < 1 || data[0] > kVersion || data[1] > kMaxNotes ||
          size < 2 + data[1] * 3u + kSteps * 4u)
         return false;
      Pattern p;
      size_t at = 2;
      for (int i = 0; i < data[1]; ++i, at += 3)
         p.add(data[at], data[at + 1], data[at + 2]);
      for (Lane &l : p.lanes) {
         l.octave = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(data[at]), -3, 3));
         l.ratchet = static_cast<uint8_t>(std::clamp<int>(data[at + 1], 1, kMaxRatchet));
         l.pressure = data[at + 2] ? 1 : 0;
         l.velocity = static_cast<int8_t>(std::clamp<int>(static_cast<int8_t>(data[at + 3]), -64, 64));
         at += 4;
      }
      *this = p;
      return true;
   }
};

// The panel's edits: the whole pattern, applied at the start of the next block.
struct Command {
   Pattern pattern;
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
};

} // namespace jarp

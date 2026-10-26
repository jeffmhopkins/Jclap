#pragma once

// What the audio thread and the panel pass each other. The audio thread publishes a SlabFrame
// through a TripleBuffer every block; the panel reads the newest one each time it draws. The panel
// sends Commands back through a queue (jump, re-roll, carve). Neither side ever waits for the other.

#include <algorithm>
#include <array>
#include <cstdint>

#include "glyph.h"

namespace monolith {

constexpr int kSlabColumns = 5, kSlabRows = 11, kCells = kSlabColumns * kSlabRows;
constexpr int kHistory = 6;

struct Cell {
   Glyph glyph;
   // How many of the glyph's last strokes have worn away.
   uint8_t eroded = 0;
   // Which lexicon word the scribe wrote here, or -1 for a glyph carved by hand or heard in Listen.
   int16_t word = -1;

   // The strokes still standing: never fewer than one while the glyph has any.
   int visible() const { return std::max(glyph.count > 0 ? 1 : 0, static_cast<int>(glyph.count) - eroded); }
   bool operator==(const Cell &o) const { return glyph == o.glyph && eroded == o.eroded && word == o.word; }
};

struct SlabFrame {
   std::array<Cell, kCells> cells{};
   // Bumped whenever the scribe rewrites the whole slab (a new seed, density, script...).
   uint32_t generation = 0;
   // The script the slab was written in (ScriptKey::pack), saved with it.
   uint64_t key = 0;
   int length = 8;
   // The cell being read, and how far into its 16 steps the voice is.
   int current = 0;
   float step = 0.0f;
   bool playing = false;
   bool listening = false;
   // Notes heard so far in the window being transcribed.
   int heard = 0;
   // The cells read most recently, oldest first; -1 where there are none yet.
   std::array<int, kHistory> history{-1, -1, -1, -1, -1, -1};
   // The pitch each glyph row plays right now (row 0 the top), key follow included.
   // F phrygian in thirds, the defaults, until the engine says otherwise.
   std::array<int, kGridRows> rowPitch{82, 78, 75, 72, 68, 65, 61, 58};
   Orthography orthography = Orthography::Monolith;
};

struct Command {
   enum class Kind : uint8_t {
      Jump,    // read `cell` next
      Reroll,  // the scribe writes a new glyph into `cell`
      Set,     // `cell` becomes `glyph`, as carved by hand
      Restore, // every eroded stroke comes back
      Load,    // `cell` becomes `glyph` with `eroded` and `word`, from a saved state
      LoadKey, // the slab just loaded belongs to the script `key` (packed by ScriptKey::pack)
   } kind = Kind::Jump;
   int8_t cell = 0;
   uint8_t eroded = 0;
   int16_t word = -1;
   Glyph glyph;
   uint64_t key = 0;
};

} // namespace monolith

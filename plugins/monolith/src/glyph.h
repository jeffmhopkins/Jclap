#pragma once

// Glyphs and how they read as notes. Plain C++ with no plugin or GUI code, so it can be tested on
// its own.
//
// A glyph is 1 to 6 strokes drawn between the twelve nodes of a 3 x 4 grid, plus an optional dot
// and an optional overbar. Node n sits at column n % 3 and row n / 3, row 0 at the top. Each stroke
// is one note:
//
//    start node's row     pitch: the Dialect's four row pitches, row 0 the highest
//    start node's column  velocity: 80, 100, 120 from left to right
//    stroke length        duration: grid length x 1.5, rounded, at least one step
//    stroke order         rhythm: notes play back to back in the order drawn
//    same start node      chord: a stroke from the node the last one started at starts with it
//    dot                  a one-step ghost note (velocity 40) at the dot's row, after the strokes
//    overbar              the last note holds to the end of the glyph's window
//
// A glyph owns a window of 16 steps; whatever doesn't fit is cut at the end of it.
//
// The orthography is the script the glyphs are written in. It decides how a glyph looks and which
// strokes the scribe may write, but not how a stroke reads: lengths are always measured on the 3 x 4
// grid, so a glyph plays the same whichever script draws it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace monolith {

constexpr int kGridColumns = 3, kGridRows = 4, kNodes = kGridColumns * kGridRows;
constexpr int kMaxStrokes = 6;
constexpr int kWindowSteps = 16;
// Strokes plus a ghost note.
constexpr int kMaxGlyphNotes = kMaxStrokes + 1;

inline int columnOf(int node) { return node % kGridColumns; }
inline int rowOf(int node) { return node / kGridColumns; }
inline int nodeAt(int column, int row) { return column + kGridColumns * row; }

struct Stroke {
   uint8_t from = 0, to = 0;
   bool operator==(const Stroke &o) const { return from == o.from && to == o.to; }
};

struct Glyph {
   uint8_t count = 0;
   std::array<Stroke, kMaxStrokes> strokes{};
   // The dot's node, or -1 for none.
   int8_t dot = -1;
   bool bar = false;

   bool operator==(const Glyph &o) const {
      if (count != o.count || dot != o.dot || bar != o.bar)
         return false;
      for (int i = 0; i < count; ++i)
         if (!(strokes[i] == o.strokes[i]))
            return false;
      return true;
   }
   bool operator!=(const Glyph &o) const { return !(*this == o); }
};

// ---- orthographies ----

enum class Orthography : uint8_t { Monolith, Runic, Circuit, Halo, Cuneiform };
constexpr int kOrthographyCount = 5;

// How a script draws its strokes.
enum class StrokeStyle : uint8_t {
   Line,    // straight, square-ended
   Rune,    // straight and heavy, cut at an angle
   Trace,   // a circuit trace: right angles, a pad at each end
   Arc,     // along a ring when it can, otherwise straight
   Wedge,   // a wedge pressed into clay: wide head at the start, tapering to the end
};

inline StrokeStyle styleOf(Orthography o) {
   switch (o) {
   case Orthography::Monolith: return StrokeStyle::Line;
   case Orthography::Runic: return StrokeStyle::Rune;
   case Orthography::Circuit: return StrokeStyle::Trace;
   case Orthography::Halo: return StrokeStyle::Arc;
   case Orthography::Cuneiform: return StrokeStyle::Wedge;
   }
   return StrokeStyle::Line;
}

struct Point {
   float x = 0.0f, y = 0.0f;
};

// Halo's nodes sit on four rings around a point below the glyph, three to a ring: one on the
// upright spoke and one either side, fanned wider on the inner rings so they don't crowd.
constexpr float kHaloCenterX = 1.0f, kHaloCenterY = 3.6f;
constexpr float kHaloRadius[kGridRows] = {3.4f, 2.5f, 1.6f, 0.7f};
constexpr float kHaloSpread[kGridRows] = {0.30f, 0.38f, 0.55f, 0.95f};

// A Halo node's angle from upright, clockwise, in radians.
inline float haloAngle(int node) { return static_cast<float>(columnOf(node) - 1) * kHaloSpread[rowOf(node)]; }

// Where a node is drawn, in grid units: x from 0 to 2, y from 0 (top) to 3.
inline Point nodePosition(Orthography o, int node) {
   if (o == Orthography::Halo) {
      const float a = haloAngle(node), radius = kHaloRadius[rowOf(node)];
      return {kHaloCenterX + radius * std::sin(a), kHaloCenterY - radius * std::cos(a)};
   }
   return {static_cast<float>(columnOf(node)), static_cast<float>(rowOf(node))};
}

// Whether the scribe may write a stroke from `a` to `b` in this script.
inline bool strokeAllowed(Orthography o, int a, int b) {
   if (a == b || a < 0 || b < 0 || a >= kNodes || b >= kNodes)
      return false;
   const int dc = std::abs(columnOf(a) - columnOf(b)), dr = std::abs(rowOf(a) - rowOf(b));
   switch (o) {
   case Orthography::Monolith: return true;
   // Upright staves and their branches: verticals of any length, and single diagonal steps.
   case Orthography::Runic: return dc == 0 || (dc == 1 && dr == 1);
   // Traces run along the grid only.
   case Orthography::Circuit: return dc == 0 || dr == 0;
   // Around a ring, or in or out along a spoke.
   case Orthography::Halo: return dr == 0 || dc == 0;
   // Short wedges to a neighbouring node.
   case Orthography::Cuneiform: return dc <= 1 && dr <= 1;
   }
   return false;
}

// A stroke's length on the 3 x 4 grid, whatever the script.
inline float strokeLength(int a, int b) {
   const float dc = static_cast<float>(columnOf(a) - columnOf(b)), dr = static_cast<float>(rowOf(a) - rowOf(b));
   return std::sqrt(dc * dc + dr * dr);
}

// A stroke's duration in steps.
inline int strokeSteps(int a, int b) {
   return std::max(1, static_cast<int>(std::lround(strokeLength(a, b) * 1.5f)));
}

inline int velocityOfColumn(int column) { return column <= 0 ? 80 : column == 1 ? 100 : 120; }

// ---- decoding ----

struct GlyphNote {
   // In steps from the start of the glyph's window.
   int start = 0, length = 1;
   // 0 (top, highest) to 3.
   int row = 0;
   int velocity = 100;
   bool ghost = false;
   // Which stroke it came from, or -1 for the ghost note.
   int stroke = -1;
};

struct GlyphNotes {
   std::array<GlyphNote, kMaxGlyphNotes> notes{};
   int count = 0;
   // Where the strokes end, in steps (may be past the window).
   int end = 0;
};

// The notes a glyph plays, reading only its first `visible` strokes (an eroded glyph has lost the
// rest). -1 reads them all.
inline GlyphNotes decode(const Glyph &g, int visible = -1) {
   GlyphNotes out;
   const int n = std::clamp(visible < 0 ? static_cast<int>(g.count) : visible, 0, static_cast<int>(g.count));
   int t = 0, groupStart = 0, last = -1;
   for (int i = 0; i < n; ++i) {
      const Stroke &s = g.strokes[i];
      const int length = strokeSteps(s.from, s.to);
      int start;
      if (i > 0 && s.from == g.strokes[i - 1].from)
         start = groupStart;
      else
         start = groupStart = t;
      t = std::max(t, start + length);
      if (start >= kWindowSteps)
         continue;
      GlyphNote &note = out.notes[out.count++];
      note = {start, std::min(length, kWindowSteps - start), rowOf(s.from), velocityOfColumn(columnOf(s.from)), false, i};
      last = out.count - 1;
   }
   out.end = t;
   if (g.bar && last >= 0)
      out.notes[last].length = kWindowSteps - out.notes[last].start;
   if (g.dot >= 0 && g.dot < kNodes && t < kWindowSteps && n > 0)
      out.notes[out.count++] = {t, 1, rowOf(g.dot), 40, true, -1};
   return out;
}

// ---- transcribing ----

// A played note, for transcription: where it starts in the window, how long it is, which row it is
// nearest and how hard it was played.
struct PlayedNote {
   int start = 0, length = 1, row = 0, velocity = 100;
};

inline int columnOfVelocity(int velocity) { return velocity < 90 ? 0 : velocity < 110 ? 1 : 2; }

// The glyph that would play `notes` (sorted by start) back: one stroke per note, from the node of the
// note's row and loudness, as long as the gap to the next note (or the last note's own length). The
// gaps themselves are lost, since a glyph's notes always follow one another, and of notes that start
// together only the first is kept. At most six notes.
inline Glyph transcribe(const PlayedNote *notes, int count, Orthography o) {
   Glyph g;
   int previousStart = -1;
   for (int i = 0; i < count && g.count < kMaxStrokes; ++i) {
      if (notes[i].start == previousStart)
         continue;
      previousStart = notes[i].start;
      int next = -1;
      for (int j = i + 1; j < count; ++j)
         if (notes[j].start > notes[i].start) {
            next = notes[j].start;
            break;
         }
      const int wanted = std::max(1, next >= 0 ? next - notes[i].start : notes[i].length);
      const int from = nodeAt(columnOfVelocity(notes[i].velocity), std::clamp(notes[i].row, 0, kGridRows - 1));
      // The allowed stroke from there whose length plays closest to the wanted duration; on a tie,
      // the shorter one, then the lower node.
      int best = -1, bestError = 1 << 20;
      for (int to = 0; to < kNodes; ++to) {
         if (!strokeAllowed(o, from, to))
            continue;
         const int error = std::abs(strokeSteps(from, to) - wanted);
         if (error < bestError || (error == bestError && strokeLength(from, to) < strokeLength(from, best))) {
            best = to;
            bestError = error;
         }
      }
      if (best < 0)
         continue;
      g.strokes[g.count++] = {static_cast<uint8_t>(from), static_cast<uint8_t>(best)};
   }
   return g;
}

// ---- random numbers ----

// A small fast generator (splitmix64), so a seed always writes the same script on every platform.
struct Rng {
   uint64_t state = 0;

   explicit Rng(uint64_t seed = 0) : state(seed) {}
   uint64_t next() {
      uint64_t z = (state += 0x9e3779b97f4a7c15ull);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
   }
   // 0 <= x < 1.
   double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
   // 0 <= x < n.
   int below(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint64_t>(n)); }
};

// Mixes numbers into one, for seeding.
inline uint64_t hashMix(uint64_t a, uint64_t b) {
   Rng r(a * 0x9e3779b97f4a7c15ull ^ (b + 0x632be59bd9b4e019ull));
   return r.next();
}

} // namespace monolith

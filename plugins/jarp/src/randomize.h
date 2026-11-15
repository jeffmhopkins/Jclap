#pragma once

// The dice: RANDOMIZE builds a new pattern from a plain base (one note per step on row 1, octave 0,
// one hit, velocity offset 0), each ticked item rolled within its range; MUTATE nudges the pattern
// there is, each ticked item by up to its range. Unticked items stay as they are. Only the steps up
// to the pattern's length are rolled. No Visage here, so the tests can use it.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "shared.h"

namespace jarp {

enum DiceItem { kDicePlacement, kDiceNotes, kDiceLength, kDiceOctave, kDiceRatchet, kDiceVelocity, kDicePressure, kDiceItemCount };

inline const char *const kDiceItemNames[kDiceItemCount] = {"PLACEMENT", "NOTES", "LENGTH", "OCTAVE",
                                                           "RATCHET", "VELOCITY", "PRESSURE"};

struct DiceSettings {
   // Which items roll; Pressure is off until asked for.
   bool on[kDiceItemCount] = {true, true, true, true, true, true, false};
   // Placement: the share of steps left empty (RANDOMIZE) or flipped (MUTATE), 0..1.
   double placement = 0.25;
   // Notes: rows 1 to this, 1..8.
   int rows = 3;
   // Length: longest note, in quarter steps (1 = a quarter step, 8 = two steps), 1..16.
   int length = 8;
   // Octave: up to this many octaves either way, 0..3.
   int octave = 1;
   // Ratchet: up to this many hits, 1..8.
   int ratchet = 3;
   // Velocity: offsets up to this either way, 0..64.
   int velocity = 20;
   // Pressure: the share of steps whose ratchet follows pressure, 0..1.
   double pressure = 0.2;
   // Random preset: the factory category to pick from, 0 for any (1 is the first category).
   int presetCategory = 0;
};

class DiceRng {
public:
   explicit DiceRng(uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ull) {}
   uint64_t next() {
      // splitmix64
      uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
   }
   double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
   // A whole number from lo to hi, both included.
   int range(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1)); }
   bool chance(double p) { return unit() < p; }

private:
   uint64_t state_;
};

namespace dice_detail {

// A note length up to `most` quarters, leaning to whole steps.
inline int rollLength(DiceRng &rng, int most) {
   static const int kLengths[] = {1, 2, 4, 4, 4, 6, 8, 8, 12, 16};
   int pick = 4;
   for (int tries = 0; tries < 8; ++tries) {
      pick = kLengths[rng.range(0, 9)];
      if (pick <= most)
         return pick;
   }
   return std::clamp(most, 1, 16);
}

// Rebuilds `p` from notes, latest step first so each fits before the next.
inline void rebuild(Pattern &p, std::vector<Note> notes) {
   std::sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.step > b.step; });
   p.clear();
   for (const Note &n : notes)
      if (p.add(n.row, n.step, n.length)) {
         // A note keeps any lane values of its own.
         p.notes[p.count - 1].own = n.own;
         p.notes[p.count - 1].lane = n.lane;
      }
}

} // namespace dice_detail

inline Pattern randomizePattern(const Pattern &from, const DiceSettings &d, int length, uint64_t seed) {
   DiceRng rng(seed);
   const int len = std::clamp(length, 1, kSteps);
   const int rows = std::clamp(d.rows, 1, kRows);
   std::vector<Note> notes;
   if (d.on[kDicePlacement]) {
      for (int s = 0; s < len; ++s)
         if (!rng.chance(d.placement)) {
            // Keeps the row and length of a note already starting here, as the base would otherwise
            // give row 1 and one step.
            const int at = d.on[kDiceNotes] ? -1 : [&] {
               for (int i = 0; i < from.count; ++i)
                  if (from.notes[i].step == s)
                     return i;
               return -1;
            }();
            Note n{0, static_cast<uint8_t>(s), static_cast<uint16_t>(kQuarters)};
            if (at >= 0)
               n.row = from.notes[at].row;
            notes.push_back(n);
         }
   }
   else
      for (int i = 0; i < from.count; ++i)
         notes.push_back(from.notes[i]);
   for (Note &n : notes) {
      if (d.on[kDiceNotes] && n.step < len)
         n.row = static_cast<uint8_t>(rng.range(0, rows - 1));
      if (d.on[kDiceLength] && n.step < len)
         n.length = static_cast<uint16_t>(dice_detail::rollLength(rng, std::clamp(d.length, 1, 16)));
      else if (d.on[kDicePlacement] && n.step < len)
         n.length = kQuarters;
   }
   Pattern p = from;
   dice_detail::rebuild(p, notes);
   for (int s = 0; s < len; ++s) {
      Lane &l = p.lanes[s];
      if (d.on[kDiceOctave])
         l.octave = static_cast<int8_t>(d.octave > 0 && rng.chance(0.35) ? rng.range(-d.octave, d.octave) : 0);
      if (d.on[kDiceRatchet])
         l.ratchet = static_cast<uint8_t>(d.ratchet > 1 && rng.chance(0.3) ? rng.range(2, std::min(d.ratchet, kMaxRatchet)) : 1);
      if (d.on[kDiceVelocity])
         l.velocity = static_cast<int8_t>(d.velocity > 0 ? rng.range(-d.velocity, d.velocity) : 0);
      if (d.on[kDicePressure])
         l.pressure = rng.chance(d.pressure) ? 1 : 0;
   }
   return p;
}

inline Pattern mutatePattern(const Pattern &from, const DiceSettings &d, int length, uint64_t seed) {
   DiceRng rng(seed);
   const int len = std::clamp(length, 1, kSteps);
   const int rows = std::clamp(d.rows, 1, kRows);
   std::vector<Note> notes;
   for (int i = 0; i < from.count; ++i)
      notes.push_back(from.notes[i]);
   if (d.on[kDicePlacement]) {
      // Flips about half of Placement's share of steps: a note there goes, an empty step gets one.
      std::vector<Note> kept;
      for (int s = 0; s < len; ++s) {
         bool any = false;
         for (const Note &n : notes)
            any = any || n.step == s;
         if (!rng.chance(d.placement * 0.5)) {
            for (const Note &n : notes)
               if (n.step == s)
                  kept.push_back(n);
         }
         else if (!any)
            kept.push_back({static_cast<uint8_t>(rng.range(0, rows - 1)), static_cast<uint8_t>(s), kQuarters});
      }
      for (const Note &n : notes)
         if (n.step >= len)
            kept.push_back(n);
      notes = kept;
   }
   for (Note &n : notes) {
      if (n.step >= len)
         continue;
      if (d.on[kDiceNotes] && rng.chance(0.3))
         n.row = static_cast<uint8_t>(std::clamp(n.row + (rng.chance(0.5) ? 1 : -1), 0, rows - 1));
      if (d.on[kDiceLength] && rng.chance(0.3))
         n.length = static_cast<uint16_t>(dice_detail::rollLength(rng, std::clamp(d.length, 1, 16)));
   }
   Pattern p = from;
   dice_detail::rebuild(p, notes);
   for (int s = 0; s < len; ++s) {
      Lane &l = p.lanes[s];
      if (d.on[kDiceOctave] && d.octave > 0 && rng.chance(0.2))
         l.octave = static_cast<int8_t>(std::clamp(l.octave + (rng.chance(0.5) ? 1 : -1), -d.octave, d.octave));
      if (d.on[kDiceRatchet] && rng.chance(0.2))
         l.ratchet = static_cast<uint8_t>(std::clamp(l.ratchet + (rng.chance(0.5) ? 1 : -1), 1, std::max(1, d.ratchet)));
      if (d.on[kDiceVelocity] && d.velocity > 0 && rng.chance(0.5))
         l.velocity = static_cast<int8_t>(std::clamp(l.velocity + rng.range(-d.velocity / 2, d.velocity / 2), -64, 64));
      if (d.on[kDicePressure] && rng.chance(d.pressure * 0.5))
         l.pressure = l.pressure ? 0 : 1;
   }
   return p;
}

// What UNDO and REDO step through: each pattern the dice replaced.
// What a dice step replaced, to undo it: one slot's pattern (RANDOMIZE, MUTATE, a random pattern
// into the slot), or everything, for a random preset: the params, every slot and the preset's name.
struct DiceState {
   struct Whole {
      std::vector<double> values;
      Bank bank;
      std::string name;
   };

   int slot = 0;
   Pattern pattern;
   std::shared_ptr<const Whole> whole;

   DiceState() = default;
   DiceState(const Pattern &p, int inSlot = 0) : slot(inSlot), pattern(p) {}
   explicit DiceState(Whole w) : whole(std::make_shared<const Whole>(std::move(w))) {}

   bool operator==(const Pattern &p) const { return !whole && pattern == p; }
};

class DiceHistory {
public:
   static constexpr size_t kDepth = 64;
   // Keeps `before`, what a dice step is about to replace; a step after an UNDO forgets the REDOs.
   void push(const DiceState &before) {
      past_.push_back(before);
      if (past_.size() > kDepth)
         past_.erase(past_.begin());
      future_.clear();
   }
   bool canUndo() const { return !past_.empty(); }
   bool canRedo() const { return !future_.empty(); }
   // What UNDO or REDO would bring back, so the caller can keep the same kind of thing from now.
   const DiceState &nextUndo() const { return past_.back(); }
   const DiceState &nextRedo() const { return future_.back(); }
   // Steps back from `now`, returning what to restore.
   DiceState undo(const DiceState &now) {
      DiceState p = past_.back();
      past_.pop_back();
      future_.push_back(now);
      return p;
   }
   DiceState redo(const DiceState &now) {
      DiceState p = future_.back();
      future_.pop_back();
      past_.push_back(now);
      return p;
   }

private:
   std::vector<DiceState> past_, future_;
};

} // namespace jarp

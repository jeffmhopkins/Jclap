#pragma once

// JArp's factory library: every factory preset's settings and patterns, by category.
//
// Patterns are written as a line of steps, one token per step (see Pat), so a long one reads like a
// sequencer row:
//
//    Pat("1 3 2 4 1+3 _ . 4s")   rows 1, 3, 2 and 4, rows 1 and 3 together held for two steps, a
//                                rest, then row 4 short
//
// Rows are the held notes from the lowest (Rows By PITCH), or scale degrees (DEGREE). Most presets
// here are PITCH with Empty Rows WRAP, so rows 5 to 8 play rows 1 to 4 again, put an octave up by the
// Octave lane where wanted, and a triad plays them as well as a 7th chord does.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "params.h"
#include "shared.h"

namespace jarp {

namespace library {

// `s` written out `n` times.
inline std::string rep(const std::string &s, int n) {
   std::string out;
   for (int i = 0; i < n; ++i)
      out += s + " ";
   return out;
}

inline std::vector<std::string> tokens(const std::string &s) {
   std::istringstream in(s);
   std::vector<std::string> out;
   for (std::string t; in >> t;)
      out.push_back(t);
   return out;
}

// A pattern from a line of steps. Each token is one step:
//
//    .        a rest
//    _        the notes started on the step before (and tied on since) sound through this one too
//    3        row 3 for `gate` quarter steps (a whole step by default)
//    1+3      rows 1 and 3 together;  c  is rows 1 to 4
//
// and a row may end in  s  (half a step),  t  (a quarter),  l  (two steps),  :N  (N quarter steps) or
// !  (to the pattern's end). A note always stops at the next note in its row.
//
// The lanes are lines of numbers that repeat over the pattern, "." for the plain value: oct()
// octaves, vel() velocity offsets, rat() ratchet hits, prs() 1 where pressure adds hits (P).
struct Pat {
   Pattern p;

   explicit Pat(const std::string &steps, int gate = kQuarters) {
      struct Start {
         int row, step, quarters;
      };
      const std::vector<std::string> t = tokens(steps);
      const int len = std::clamp(static_cast<int>(t.size()), 1, kSteps);
      std::vector<Start> notes;
      std::vector<size_t> last;
      for (int i = 0; i < len; ++i) {
         const std::string &tok = t[i];
         if (tok == ".") {
            last.clear();
            continue;
         }
         if (tok == "_") {
            for (size_t k : last)
               notes[k].quarters = (i + 1 - notes[k].step) * kQuarters;
            continue;
         }
         last.clear();
         std::stringstream parts(tok);
         for (std::string part; std::getline(parts, part, '+');) {
            if (part.empty())
               continue;
            int q = gate;
            const size_t colon = part.find(':');
            if (colon != std::string::npos)
               q = std::atoi(part.c_str() + colon + 1);
            else if (part.back() == 's')
               q = 2;
            else if (part.back() == 't')
               q = 1;
            else if (part.back() == 'l')
               q = 2 * kQuarters;
            else if (part.back() == '!')
               q = (len - i) * kQuarters;
            if (part[0] == 'c') {
               for (int r = 0; r < 4; ++r) {
                  last.push_back(notes.size());
                  notes.push_back({r, i, q});
               }
            }
            else if (std::isdigit(static_cast<unsigned char>(part[0]))) {
               last.push_back(notes.size());
               notes.push_back({std::clamp(part[0] - '1', 0, kRows - 1), i, q});
            }
         }
      }
      p.length = static_cast<uint8_t>(len);
      // Latest first, so each note sees the next in its row and stops there.
      std::stable_sort(notes.begin(), notes.end(), [](const Start &a, const Start &b) { return a.step > b.step; });
      for (const Start &n : notes)
         p.add(n.row, n.step, n.quarters);
   }

   Pat &oct(const std::string &s) { return lane(s, [](Lane &l, int v) { l.octave = static_cast<int8_t>(std::clamp(v, -3, 3)); }); }
   Pat &vel(const std::string &s) { return lane(s, [](Lane &l, int v) { l.velocity = static_cast<int8_t>(std::clamp(v, -64, 64)); }); }
   Pat &rat(const std::string &s) { return lane(s, [](Lane &l, int v) { l.ratchet = static_cast<uint8_t>(std::clamp(v, 1, kMaxRatchet)); }); }
   Pat &prs(const std::string &s) { return lane(s, [](Lane &l, int v) { l.pressure = v ? 1 : 0; }); }

   operator Pattern() const { return p; }

private:
   template <typename F> Pat &lane(const std::string &s, F set) {
      const std::vector<std::string> t = tokens(s);
      if (t.empty())
         return *this;
      for (int i = 0; i < p.length; ++i) {
         const std::string &tok = t[i % t.size()];
         if (tok != ".")
            set(p.lanes[i], std::atoi(tok.c_str()));
      }
      return *this;
   }
};

// A pattern as a list of notes, {row, step, length in quarter steps} (rows and steps from 1, as the
// panel numbers them), and the lanes for the steps that differ: how the first presets were written.
struct LaneEdit {
   int step;
   Lane lane;
};
inline Pattern makePattern(std::initializer_list<Note> notes, std::initializer_list<LaneEdit> lanes = {},
                           int length = 8) {
   Pattern p;
   p.length = static_cast<uint8_t>(length);
   for (const Note &n : notes)
      p.add(n.row - 1, n.step - 1, n.length);
   for (const LaneEdit &e : lanes)
      p.lanes[e.step - 1] = e.lane;
   return p;
}

// One factory preset: its name and category, the params it changes from the defaults, and its
// patterns from slot 1 on.
struct Entry {
   const char *name;
   const char *category;
   std::vector<std::pair<clap_id, double>> params;
   std::vector<Pattern> patterns;
};

// Param values, by name.
namespace v {
constexpr double r32 = 0, r16T = 1, r16 = 2, r8T = 3, r8 = 4, r4 = 5;
constexpr double off = 0, chord = 1, loop = 2;
constexpr double song = 0, note = 1, bar = 2;
} // namespace v

#if defined(_MSC_VER)
#define JARP_NOINLINE __declspec(noinline)
#else
#define JARP_NOINLINE __attribute__((noinline))
#endif

// Adds the entry `make` builds. Each preset is built in a call of its own, so its patterns (a few
// KB each) are on the stack only while it is: written inline, a compiler may give every preset's
// temporaries their own room in one frame, more than a host's thread has.
template <typename F> JARP_NOINLINE void add(std::vector<Entry> &e, F make) { e.push_back(make()); }

inline std::vector<Entry> build() {
   using namespace v;
   const auto rate = [](double r) { return std::pair<clap_id, double>{kRate, r}; };
   const auto swing = [](double s) { return std::pair<clap_id, double>{kSwing, s}; };
   const std::pair<clap_id, double> wrap{kEmptyRows, 1}, degree{kRowsBy, 1};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto restart = [](double r) { return std::pair<clap_id, double>{kRestart, r}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };
   const auto pressRat = [](double x) { return std::pair<clap_id, double>{kPressRatchet, x}; };
   const auto curve = [](double x) { return std::pair<clap_id, double>{kCurve, x}; };
   const auto maxHits = [](double x) { return std::pair<clap_id, double>{kMaxHits, x}; };

   std::vector<Entry> e;

   // ---- Basics ----
   add(e, [&] { return Entry{"Init", "Basics", {}, {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {1, 7, 4}, {2, 8, 4}})}}; });
   add(e, [&] { return Entry{"Up", "Basics", {wrap}, {Pat("1 2 3 4 5 6 7 8").oct(". . . . 1 1 1 1")}}; });
   add(e, [&] { return Entry{"Down", "Basics", {wrap}, {Pat("8 7 6 5 4 3 2 1").oct("1 1 1 1 . . . .")}}; });
   add(e, [&] { return Entry{"Up Down", "Basics", {wrap, rate(r16)},
                {Pat("1 2 3 4 5 6 7 8 7 6 5 4 3 2").oct(". . . . 1 1 1 1 1 1 1 . . .")}}; });
   add(e, [&] { return Entry{"Root Fifth", "Basics", {degree, swing(0.54)}, {makePattern({{1, 1, 4}, {5, 2, 4}, {1, 3, 4}, {5, 4, 4}, {1, 5, 4}, {5, 6, 4}, {1, 7, 4}, {5, 8, 4}})}}; });
   add(e, [&] { return Entry{"Broken Thirds", "Basics", {degree, rate(r16)}, {makePattern({{1, 1, 4}, {3, 2, 4}, {2, 3, 4}, {4, 4, 4}, {3, 5, 4}, {5, 6, 4}, {4, 7, 4}, {6, 8, 4}})}}; });
   add(e, [&] { return Entry{"Outside In", "Basics", {wrap}, {Pat("1 4 2 3 1 4 2 3").oct(". 1 . . . 1 . .")}}; });
   add(e, [&] { return Entry{"Wrap Run", "Basics", {wrap, rate(r16)},
                {makePattern({{1, 1, 2}, {2, 2, 2}, {3, 3, 2}, {4, 4, 2}, {5, 5, 2}, {6, 6, 2}, {7, 7, 2}, {8, 8, 2}},
                             {{5, {1, 1, 0, 0}}, {6, {1, 1, 0, 0}}, {7, {1, 1, 0, 0}}, {8, {1, 1, 0, 0}}})}}; });

   // ---- Berlin School: driving sixteenths, octave jumps, accents ----
   add(e, [&] { return Entry{"Sequencer Pulse", "Berlin School", {wrap, rate(r16), pressVel(0.3)},
                {Pat("1s 1s 2s 1s 3s 1s 4s 1s 5s 1s 4s 1s 3s 1s 2s 1s")
                    .oct(". . . . . . . . 1 . . . . . . .")
                    .vel("18 -10 4 -10 10 -10 4 -10")}}; });
   add(e, [&] { return Entry{"Octave Bounce", "Berlin School", {wrap, rate(r16), pressVel(0.3)},
                {Pat("1s 1s 1s 1s 2s 2s 2s 2s 3s 3s 3s 3s 2s 2s 2s 2s").oct(". 1").vel("14 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Accent Shift", "Berlin School", {wrap, rate(r16), pressVel(0.2)},
                // A three-step accent over a sixteen-step line: it lands somewhere new each beat.
                {Pat("1s 2s 3s 1s 2s 3s 4s 1s 2s 3s 1s 2s 3s 4s 5s 4s").oct(". . . . . . . . . . . . . . 1 .").vel("22 -12 -12")}}; });
   add(e, [&] { return Entry{"Night Drive", "Berlin School", {wrap, rate(r16), advance(loop), pressVel(0.3)},
                {Pat("1s 1s 2s 1s 3s 1s 2s 1s 1s 1s 2s 1s 3s 1s 4s 1s").vel("16 -8 . -8"),
                 Pat("1s 1s 2s 1s 3s 1s 2s 1s 1s 1s 2s 1s 3s 1s 4s 1s").oct(". 1").vel("16 -8 . -8"),
                 Pat("2s 2s 3s 2s 4s 2s 3s 2s 2s 2s 3s 2s 4s 2s 5s 2s").oct(". . . . . . . . . . . . . . 1 .").vel("16 -8 . -8"),
                 Pat("1s 3s 5s 3s 1s 3s 5s 3s 1s 3s 5s 7s 8s 7s 5s 3s").oct(". . 1 . . . 1 . . . 1 1 1 1 1 .").vel("16 -8 . -8")}}; });
   add(e, [&] { return Entry{"Long Drive", "Berlin School", {wrap, rate(r16), pressVel(0.3)},
                // Four bars: the line, the line with octaves, a climb, and a turnaround.
                {Pat(rep("1s 1s 2s 1s 3s 1s 2s 1s", 2) + rep("1s 1s 2s 1s 3s 1s 2s 1s", 2) +
                     "1s 2s 3s 4s 2s 3s 4s 5s 3s 4s 5s 6s 4s 5s 6s 7s " + "8s 7s 6s 5s 4s 3s 2s 1s 4s 3s 2s 1s 3s 2s 4s 1s")
                    .oct(". . . . . . . . . . . . . . . . . 1 . 1 . 1 . 1 . 1 . 1 . 1 . 1 . . . . . 1 . 1 . . 1 1 . 1 1 1 1 1 1 1 . . . . . . . . . . . .")
                    .vel("16 -8 4 -8")}}; });

   // ---- Grooves ----
   add(e, [&] { return Entry{"Funk Sixteenths", "Grooves", {wrap, rate(r16), swing(0.56)},
                {Pat("1 . 3s 1s . 2s . 4 . 1s 3s . 2s . 4s 3s").vel("16 . -12 4 . -6 . 12 . 4 -10 . -6 . 6 -4")}}; });
   add(e, [&] { return Entry{"House Offbeats", "Grooves", {rate(r16), pressVel(0.4)},
                {Pat(". . 2+3+4s . . . 2+3+4s . . . 2+3+4s . . . 2+3+4s 1s").vel(". . 10 . . . 4 . . . 10 . . . 4 -10")}}; });
   add(e, [&] { return Entry{"Dotted Eighths", "Grooves", {wrap, rate(r16)},
                {Pat("1 _ _ 3 _ _ 2 _ _ 4 _ _ 5 _ 3 _").oct(". . . . . . . . . . . . 1 . . .").vel("12 . . 4 . . 8 . . 4 . . 10 . -4 .")}}; });
   add(e, [&] { return Entry{"Swing Shuffle", "Grooves", {wrap, rate(r16), swing(0.66)},
                {Pat("1s 3s 2s 4s 1s 3s 2s 5s").oct(". . . . . . . 1").vel("12 -10")}}; });
   add(e, [&] { return Entry{"Gallop", "Grooves", {rate(r16T), restart(bar)},
                {makePattern({{1, 1, 4}, {3, 2, 2}, {2, 3, 2}, {1, 4, 4}, {3, 5, 2}, {2, 6, 2}, {1, 7, 4}, {3, 8, 2},
                              {2, 9, 2}, {1, 10, 4}, {3, 11, 2}, {4, 12, 4}},
                             {{1, {0, 1, 0, 16}}, {4, {0, 1, 0, 8}}, {7, {0, 1, 0, 16}}, {10, {0, 1, 0, 8}}, {12, {0, 3, 0, 0}}},
                             12)}}; });
   add(e, [&] { return Entry{"Stabs", "Grooves", {rate(r8), pressVel(0.8)},
                {makePattern({{1, 1, 2}, {2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {1, 4, 1}, {2, 4, 1}, {3, 4, 1}, {4, 4, 1},
                              {1, 7, 2}, {2, 7, 2}, {3, 7, 2}, {4, 7, 2}},
                             {{4, {1, 1, 0, -20}}, {7, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Two Step", "Grooves", {wrap, rate(r16), swing(0.58), advance(chord)},
                {Pat("1 . 3s . . 2s 4s . 1s . 3s . . 2s 4 .").vel("14 . -6 . . 4 8 . 6 . -6 . . 4 10 ."),
                 Pat("1s 3s 5s 3s . 2s 4s 6s 1s . 3s 5s 7s 5s 4s 2s").oct(". . 1 . . . . 1 . . . 1 1 1 . .").vel("14 -6 6 -6")}}; });

   // ---- Piano ----
   add(e, [&] { return Entry{"Alberti Bass", "Piano", {wrap, rate(r16)}, {Pat("1 3 2 3 1 3 2 3").vel("10 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Broken Triplets", "Piano", {wrap, rate(r8T)}, {Pat("1 2 3 4 3 2", 6).vel("10 . . 4 . .")}}; });
   add(e, [&] { return Entry{"Rolled Ballad", "Piano", {wrap, rate(r16), pressVel(0.7)},
                {Pat("1 2 3 4 . . . . 1 2 3 5 . . . .", 28).oct(". . . . . . . . . . . 1 . . . .").vel("8 . . -4")}}; });
   add(e, [&] { return Entry{"Stride", "Piano", {rate(r8), swing(0.6)},
                {Pat("1 2+3+4s 1 2+3+4s 1 2+3+4s 2 2+3+4s").oct("-1 . -1 . -1 . -1 .").vel("10 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Nocturne Sweep", "Piano", {wrap, rate(r16T), pressVel(0.7)},
                {Pat("1 2 3 4 5 6 7 8 7 6 5 4", 8).oct(". . . . 1 1 1 2 1 1 1 .").vel("10 . . . 4 . . 8 . . . -4")}}; });
   add(e, [&] { return Entry{"Piano Verse", "Piano", {wrap, rate(r16), advance(chord), pressVel(0.6)},
                {Pat("1 3 2 3 1 3 2 3 1 3 2 3 1 4 3 4").vel("10 -8 -2 -8"),
                 Pat("1 2 3 4 5 . . . 1 2 3 4 5 . 6 7", 24).oct(". . . . 1 . . . . . . . 1 . 1 1").vel("8 . . . 10 . . . 4 . . . 10 . 4 6"),
                 Pat("1 2+3 4 2+3 1 2+3 4 2+3").oct(". . 1 . . . 1 .").vel("10 -4 6 -4")}}; });

   // ---- Guitar Fingerpicking: row 1 is the thumb's bass, rows 2 to 4 the fingers ----
   add(e, [&] { return Entry{"Travis Picking", "Guitar Fingerpicking", {wrap, rate(r8), swing(0.54)},
                {Pat("1+4 3 2 4 1 3 2+4 3", 8).oct(". . . . -1 . . .").vel("10 -6 4 -6 6 -6 4 -6")}}; });
   add(e, [&] { return Entry{"Folk Roll", "Guitar Fingerpicking", {wrap, rate(r16)},
                {Pat("1 3 4 2 3 4 1 3 4 2 3 4 2 3 4 3", 8).vel("10 -8 -4 2 -8 -4 6 -8 -4 2 -8 -4 2 -8 -4 -8")}}; });
   add(e, [&] { return Entry{"Classical Arpeggio", "Guitar Fingerpicking", {wrap, rate(r8T), pressVel(0.6)},
                // p i m a m i
                {Pat("1 2 3 4 3 2", 10).vel("10 -6 -2 6 -2 -6")}}; });
   add(e, [&] { return Entry{"Banjo Roll", "Guitar Fingerpicking", {wrap, rate(r16), pressVel(0.3)},
                // The forward roll: thumb, index, middle, in 3 + 3 + 2.
                {Pat("2s 3s 4s 2s 3s 4s 2s 3s").oct(". . 1 . . 1 . .").vel("12 -6 -2 10 -6 -2 8 -6")}}; });
   add(e, [&] { return Entry{"Fingerstyle Song", "Guitar Fingerpicking", {wrap, rate(r8), swing(0.54), advance(loop)},
                {Pat("1+4 3 2 4 1 3 2+4 3", 8).oct(". . . . -1 . . .").vel("10 -6 4 -6"),
                 Pat("1+4 3 2 4 1 3 2+4 3", 8).oct(". . . . -1 . . .").vel("10 -6 4 -6"),
                 Pat("2+4 3 1 4 2 3 1+3 4", 8).oct(". . . . . . -1 .").vel("10 -6 4 -6"),
                 Pat("1 2 3 4 3 2 4 3", 6).oct(". . . . . . 1 .").vel("10 -6 -2 6 -2 -6 8 -2")}}; });

   // ---- Strum and Harp: at 1/32, so a strum spreads like a hand across strings ----
   add(e, [&] { return Entry{"Down Strum", "Strum and Harp", {rate(r32), pressVel(0.6)},
                {Pat("1 2 3 4 . . . . . . . . . . . .", 48).vel("8 4 0 -4")}}; });
   add(e, [&] { return Entry{"Up Strum", "Strum and Harp", {rate(r32), pressVel(0.6)},
                {Pat("4 3 2 1 . . . . . . . . . . . .", 48).vel("8 4 0 -4")}}; });
   add(e, [&] { return Entry{"Strum Pattern", "Strum and Harp", {rate(r32), pressVel(0.5)},
                // Down, down, up, up, down, up, over a bar.
                {Pat("1 2 3 4 . . . . 1 2 3 4 4 3 2 1 . . . . 4 3 2 1 1 2 3 4 4 3 2 1", 14)
                    .vel("12 8 4 0 . . . . 6 3 0 -3 -2 -4 -6 -8 . . . . -2 -4 -6 -8 8 5 2 -1 -2 -4 -6 -8")}}; });
   add(e, [&] { return Entry{"Harp Glissando", "Strum and Harp", {wrap, rate(r32), pressVel(0.5)},
                {Pat("1 2 3 4 5 6 7 8 5 6 7 8 5 6 7 8 8 7 6 5 8 7 6 5 8 7 6 5 4 3 2 1", 12)
                    .oct(". . . . 1 1 1 1 2 2 2 2 3 3 3 3 3 3 3 3 2 2 2 2 1 1 1 1 . . . .")}}; });
   add(e, [&] { return Entry{"Harp Cascade", "Strum and Harp", {wrap, rate(r32), pressVel(0.5)},
                {Pat("8 7 6 5 8 7 6 5 4 3 2 1 . . . . 4 3 2 1 . . . . 4 3 2 1 . . . .", 16)
                    .oct("2 2 2 2 1 1 1 1 . . . . . . . . . . . . . . . . -1 -1 -1 -1 . . . .").vel("10 4 0 -4")}}; });
   add(e, [&] { return Entry{"Strum to Harp", "Strum and Harp", {wrap, rate(r32), advance(chord), pressVel(0.5)},
                {Pat("1 2 3 4 . . . . 1 2 3 4 4 3 2 1 . . . . 4 3 2 1 1 2 3 4 4 3 2 1", 14).vel("12 8 4 0"),
                 Pat("1 2 3 4 5 6 7 8 5 6 7 8 5 6 7 8 8 7 6 5 8 7 6 5 8 7 6 5 4 3 2 1", 12)
                     .oct(". . . . 1 1 1 1 2 2 2 2 3 3 3 3 3 3 3 3 2 2 2 2 1 1 1 1 . . . .")}}; });

   // ---- Pads and Pedals: a held bass, slow tops ----
   add(e, [&] { return Entry{"Pedal Climb", "Pads and Pedals", {swing(0.54), pressVel(0.7)},
                // Row 1 held under the whole pattern, the rest climbing over it.
                {makePattern({{1, 1, 64}, {2, 1, 1}, {3, 2, 4}, {4, 3, 3}, {2, 5, 4}, {3, 6, 2}, {4, 7, 6}, {2, 9, 2},
                              {3, 11, 8}, {2, 13, 4}, {4, 14, 4}, {3, 15, 2}, {4, 16, 4}},
                             {{1, {0, 1, 0, 12}}, {4, {0, 1, 0, -20}}, {5, {0, 1, 0, 8}}, {7, {1, 1, 0, 0}}, {8, {0, 1, 0, -12}},
                              {9, {0, 1, 0, 12}}, {11, {0, 3, 0, 0}}, {12, {0, 1, 0, -20}}, {13, {0, 1, 0, 8}},
                              {14, {0, 2, 0, 0}}, {16, {1, 1, 0, -12}}},
                             16)}}; });
   add(e, [&] { return Entry{"Slow Top", "Pads and Pedals", {rate(r8), pressVel(0.8)},
                {Pat("1!+2l . 3l . 4l . 3l . 2l . 3l . 4l . 4l .").oct(". . . . . . . . . . . . . . 1 .")}}; });
   add(e, [&] { return Entry{"Swell Layers", "Pads and Pedals", {rate(r4), pressVel(1.0), curve(-0.4)},
                {Pat("1 2 3 4 . . . .", 12).vel("-20 -10 0 8")}}; });
   add(e, [&] { return Entry{"Drone and Drift", "Pads and Pedals", {wrap, rate(r8), pressVel(0.9)},
                // Eight bars: the bass held throughout, the top drifting.
                {Pat("1!+3:12 . . . 4:8 . 2:12 . . . 3:8 . . . 4:16 . . . . . 5:12 . . 3:8 . . . 2:16 . . . "
                     "4:12 . . . 3:8 . 5:12 . . . 4:8 . . . 3:16 . . . . . 2:12 . . 4:8 . . . 3:16 . . . . .")
                    .oct(". . . . . . . . . . . . . . . . . . . . 1 . . . . . . . . . . . . . . . . . 1 . . . . . . . . . . . . . . . . . . . . . . . . .")
                    .vel("-6 . . . 4 . -4 . . . 6")}}; });
   add(e, [&] { return Entry{"Pedal Story", "Pads and Pedals", {wrap, rate(r8), advance(loop), pressVel(0.8)},
                {Pat("1!+2l . 3l . 4l . 3l .").vel("4 . . . 8 . . ."),
                 Pat("1!+3l . 4l . 5l . 4l .").oct(". . . . 1 . . .").vel("4 . . . 10 . . ."),
                 Pat("1!+4l . 5l . 6l . 7l .").oct(". . 1 . 1 . 1 .").vel("6 . . . 12 . . ."),
                 Pat("1!+3l . 2l . 3l . 4:4 2:4").vel("4 . . . . . . -6")}}; });

   // ---- Ratchet and Rhythm ----
   add(e, [&] { return Entry{"Trap Rolls", "Ratchet and Rhythm", {wrap, rate(r16), pressVel(0.3)},
                {Pat("1+4s 4s 4s 4s 4s 4s 4s 4s 1+4s 4s 4s 4s 4s 4s 4s 4s")
                    .rat("1 1 2 1 1 1 3 1 1 1 2 1 4 1 6 1").vel("12 -8 -4 -8 4 -8 -2 -8 10 -8 -4 -8 2 -8 6 -8")}}; });
   add(e, [&] { return Entry{"Stutter", "Ratchet and Rhythm", {wrap, rate(r8)},
                {Pat("1 2 3 4 1 3 2 5").rat("1 1 1 4 1 1 2 6").oct(". . . . . . . 1")}}; });
   add(e, [&] { return Entry{"Machine Gun", "Ratchet and Rhythm", {wrap, rate(r16)},
                {Pat("1 . 2 . 3 . 4 . 5 . 4 . 3 . 2 .").rat("3 . 2 . 4 . 2 . 3 . 2 . 4 . 8 .").oct(". . . . . . . . 1 . . . . . . .")}}; });
   add(e, [&] { return Entry{"Pressure Ratchets", "Ratchet and Rhythm", {wrap, rate(r8), pressRat(1.0), maxHits(6), curve(0.3)},
                {Pat("1 3 2 4 1 3 2 4").prs("1")}}; });
   add(e, [&] { return Entry{"Stutter Build", "Ratchet and Rhythm", {wrap, rate(r16), advance(loop)},
                {Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 1 1 2"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 2 1 3"),
                 Pat("1 3 2 4 1 3 2 4 5 6 7 8 5 6 7 8").rat("2 3 2 4 2 3 2 4 3 4 3 6 4 6 6 8").oct(". . . . . . . . 1 1 1 1 1 1 1 1")}}; });

   // ---- MPE Expressive: press to bring them alive ----
   add(e, [&] { return Entry{"Press Roll", "MPE Expressive", {pressVel(0.7), pressRat(0.6), maxHits(4), swing(0.54)},
                // Steps with P lit roll faster the harder each finger presses.
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}},
                             {{1, {0, 1, 0, 10}}, {2, {0, 1, 1, 0}}, {3, {0, 1, 0, -16}}, {4, {0, 2, 1, 0}},
                              {5, {0, 1, 0, 10}}, {6, {0, 1, 1, 0}}, {7, {0, 1, 0, -16}}, {8, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Press to Bloom", "MPE Expressive", {wrap, rate(r16), pressVel(1.0), curve(-0.5), pressRat(0.8), maxHits(5)},
                {Pat("1 3 2 4 3 5 4 6", 6).oct(". . . . . 1 . 1").prs(". 1 . 1 . 1 . 1")}}; });
   add(e, [&] { return Entry{"Breath Pulse", "MPE Expressive", {wrap, rate(r8), pressVel(1.0), curve(-0.3)},
                {Pat("1 2 3 4 3 2", 8)}}; });
   add(e, [&] { return Entry{"Pressure Waves", "MPE Expressive", {wrap, rate(r16), pressVel(0.9), pressRat(1.0), maxHits(8)},
                {Pat(rep("1 3 2 4 5 4 3 2", 4)).oct(". . . . 1 . . .").prs(". 1")}}; });
   add(e, [&] { return Entry{"Touch Chain", "MPE Expressive", {wrap, rate(r16), advance(chord), pressVel(1.0), pressRat(0.8), maxHits(6)},
                {Pat("1 . 2 . 3 . 4 .", 8),
                 Pat("1 3 2 4 1 3 2 4").prs("1"),
                 Pat("1 2 3 4 5 6 7 8").oct(". . . . 1 1 1 1").prs(". 1")}}; });

   // ---- Long Sequences: patterns that take bars to come round ----
   add(e, [&] { return Entry{"Four Bar Groove", "Long Sequences", {wrap, rate(r16), swing(0.55)},
                {Pat(rep("1 . 3s 1s . 2s 4 .", 2) + rep("1 . 3s 1s . 2s 4 .", 1) + "1 . 3s 5s . 4s 3s 2s " +
                     rep("2 . 4s 2s . 3s 5 .", 2) + "1 . 3s 1s . 2s 4 . 5s 4s 3s 2s 1s 2s 3s 4s")
                    .oct(". . . . . . . . . . . . . . . . . . . . . . . . . . 1 . . . . . . . . . . . 1 . . . . . . . 1 . . . . . . . . . . . . 1 . . . . . . .")
                    .vel("12 . -6 2 . -4 8 .")}}; });
   add(e, [&] { return Entry{"Slow Evolution", "Long Sequences", {wrap, rate(r16), pressVel(0.5)},
                // 32 steps, each eight a little further from the first.
                {Pat("1 2 3 4 3 2 3 4 1 2 3 5 3 2 3 5 1 3 4 5 4 3 4 6 2 3 5 6 5 3 6 7")
                    .oct(". . . . . . . . . . . 1 . . . 1 . . . 1 . . . 1 . . 1 1 1 . 1 1")
                    .vel("10 -6 -2 -6")}}; });
   add(e, [&] { return Entry{"Sixty Four", "Long Sequences", {wrap, rate(r16), pressVel(0.4)},
                {Pat(rep("1s 3s 2s 4s", 4) + rep("2s 4s 3s 5s", 4) + rep("1s 4s 3s 6s", 4) + "1s 2s 3s 4s 5s 6s 7s 8s 8s 7s 6s 5s 4s 3s 2s 1s")
                    .oct(rep(".", 16) + rep(". . . 1", 4) + rep(". . . 1", 4) + ". . . . 1 1 1 1 1 1 1 1 . . . .")
                    .vel("14 -8 4 -8")}}; });
   add(e, [&] { return Entry{"Odyssey", "Long Sequences", {wrap, rate(r16), pressVel(0.4), swing(0.53)},
                // 128 steps: eight bars that set out, climb, turn and come home.
                {Pat(rep("1s 1s 3s 1s 2s 1s 4s 1s", 2) + rep("1s 1s 3s 1s 2s 1s 4s 5s", 2) + rep("2s 2s 4s 2s 3s 2s 5s 2s", 2) +
                     rep("2s 3s 4s 5s 4s 3s 4s 5s", 2) + rep("3s 3s 5s 3s 4s 3s 6s 3s", 2) + "3s 4s 5s 6s 7s 8s 7s 6s 5s 6s 7s 8s 7s 6s 5s 4s " +
                     rep("4s 3s 2s 1s 3s 2s 1s 2s", 2) + "1s 1s 3s 1s 2s 1s 4s 1s 1 _ _ _ c . . .")
                    .oct(rep(".", 32) + rep(". . . . . . . 1", 2) + rep(". . . . . . 1 .", 2) + rep(". . . . . . . .", 2) +
                         rep(". . 1 . . . 1 .", 2) + ". . 1 1 1 1 1 1 1 1 1 1 1 1 1 . " + rep(".", 32))
                    .vel("14 -8 4 -8 8 -8 4 -8")}}; });
   add(e, [&] { return Entry{"Long Chain", "Long Sequences", {wrap, rate(r16), advance(loop), pressVel(0.4)},
                {Pat(rep("1s 2s 3s 4s 3s 2s", 4) + "1s 2s 3s 4s 5s 6s 7s 8s").oct(rep(".", 24) + ". . . . 1 1 1 1").vel("12 -6 -2"),
                 Pat(rep("2s 3s 4s 5s 4s 3s", 4) + "8s 7s 6s 5s 4s 3s 2s 1s").oct(rep(". . . 1 . .", 4) + "1 1 1 1 . . . .").vel("12 -6 -2")}}; });

   // ---- Chains: several patterns, one after another ----
   add(e, [&] { return Entry{"Chord Chain", "Chains", {advance(chord)},
                // Up, down, then a broken pattern: each new chord plays the next.
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}}),
                 makePattern({{4, 1, 4}, {3, 2, 4}, {2, 3, 4}, {1, 4, 4}, {4, 5, 4}, {3, 6, 4}, {2, 7, 4}, {1, 8, 4}}),
                 makePattern({{1, 1, 2}, {3, 2, 2}, {1, 3, 2}, {3, 4, 2}, {2, 5, 2}, {4, 6, 2}, {2, 7, 2}, {4, 8, 4}},
                             {{2, {1, 1, 0, 0}}, {6, {1, 1, 0, 0}}, {8, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Phrase Loop", "Chains", {advance(loop), swing(0.54)},
                // A climb, the climb an octave up, and a fill, round and round.
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}}),
                 Pat("1 2 3 4 3 2 3 4").oct("1"),
                 makePattern({{4, 1, 4}, {3, 2, 4}, {2, 3, 4}, {1, 4, 4}},
                             {{1, {1, 2, 0, 10}}, {2, {0, 2, 0, 0}}, {3, {0, 3, 0, 0}}, {4, {0, 4, 0, 16}}}, 4)}}; });
   add(e, [&] { return Entry{"Verse Chorus", "Chains", {wrap, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 .", 6), Pat("1 . 3 . 2 . 4 . 1 . 3 . 4 . 5 .", 6).oct(". . . . . . . . . . . . . . 1 ."),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 1s 3s 5s 3s 2s 4s 6s 8s").oct(". . 1 . . . 1 . . . 1 . . . 1 1").vel("12 -6"),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 3s 5s 7s 5s 4s 6s 8s 7s").oct(". . 1 . . . 1 . . 1 1 1 1 1 1 1").vel("12 -6")}}; });
   add(e, [&] { return Entry{"Build Up", "Chains", {wrap, rate(r16), advance(loop)},
                {Pat("1 . . . 2 . . . 3 . . . 4 . . ."), Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 ."),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"), Pat("1 3 2 4 5 7 6 8 1 3 2 4 5 7 6 8").oct(". . . . 1 1 1 1").vel("10 -6"),
                 Pat("1 3 2 4 5 7 6 8 5 6 7 8 5 6 7 8").oct(". . . . 1 1 1 1 1 1 1 1 2 2 2 2").rat("1 1 1 2 1 1 1 2 2 2 2 3 3 4 6 8")}}; });
   add(e, [&] { return Entry{"Call and Response", "Chains", {wrap, rate(r16), advance(loop), swing(0.55)},
                {Pat("1 2 3 . 1 2 4 . . . . . . . . ."), Pat(". . . . . . . . 5 4 3 . 5 4 2 .").oct("1"),
                 Pat("1 2 3 . 1 2 4 . 1 3 5 . . . . ."), Pat(". . . . . . . . 5 4 3 . 2 1 . .").oct("1 1 1 . . . . . 1 1 1 . . . . .")}}; });
   add(e, [&] { return Entry{"Eight Bars", "Chains", {wrap, rate(r16), advance(loop), pressVel(0.4)},
                {Pat("1 2 3 4 3 2 3 4 1 2 3 4 3 2 3 4"), Pat("1 2 3 4 3 2 3 4 1 2 3 4 5 4 3 4").oct(". . . . . . . . . . . . 1 . . ."),
                 Pat("2 3 4 5 4 3 4 5 2 3 4 5 4 3 4 5").oct(". . . 1 . . . 1"), Pat("2 3 4 5 6 5 4 3 2 3 4 5 6 7 8 7").oct(". . . 1 1 1 . . . . . 1 1 1 1 1"),
                 Pat("3 4 5 6 5 4 5 6 3 4 5 6 5 4 5 6").oct(". . 1 1 1 . 1 1"), Pat("4 5 6 7 6 5 6 7 8 7 6 5 4 3 2 1").oct(". 1 1 1 1 1 1 1 1 1 1 1 . . . ."),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").vel("10 -6"), Pat("1 2 3 4 5 6 7 8 1 . c . . . . .").oct(". . . . 1 1 1 1 . . . .")}}; });

   // ---- Odd Meters ----
   add(e, [&] { return Entry{"Five Four", "Odd Meters", {wrap, rate(r8)},
                // 3 + 2, in eighths.
                {Pat("1 3 2 4 3 1 3 2 4 2").vel("14 -6 -6 . . 8 -6 . . .")}}; });
   add(e, [&] { return Entry{"Seven Eight", "Odd Meters", {wrap, rate(r8)},
                // 2 + 2 + 3.
                {Pat("1 3 2 4 1 3 5").oct(". . . . . . 1").vel("14 -6 8 -6 8 -6 -2")}}; });
   add(e, [&] { return Entry{"Nine Eight", "Odd Meters", {wrap, rate(r8)},
                // 2 + 2 + 2 + 3.
                {Pat("1 3 2 4 1 3 2 4 3").vel("14 -6 6 -6 6 -6 8 -6 -4")}}; });
   add(e, [&] { return Entry{"Three Over Four", "Odd Meters", {wrap, rate(r16)},
                // The bass every four steps, the top every three.
                {Pat("1+4 . . 4 1 . 4 . 1 4 . . ", 3).vel("12 . . 4 8 . 4 . 8 4 . .")}}; });
   add(e, [&] { return Entry{"Five Over Four", "Odd Meters", {wrap, rate(r16)},
                {Pat("1+4 . . . 1 4 . . 1 . 4 . 1 . . 4 1 . . .", 3).vel("12 . . . 6 6 . . 6 . 6 . 6 . . 6 6 . . .")}}; });
   add(e, [&] { return Entry{"Odd Chain", "Odd Meters", {wrap, rate(r8), advance(loop)},
                // 7/8 then 5/8: one bar of 12/8 that lurches.
                {Pat("1 3 2 4 1 3 5").oct(". . . . . . 1").vel("14 -6 8 -6 8 -6 -2"), Pat("4 2 3 1 2").vel("12 -6 -6 8 -6")}}; });

   return e;
}

} // namespace library

} // namespace jarp

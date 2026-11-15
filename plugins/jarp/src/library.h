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
// here are PITCH with Empty Rows OCT: row n is the nth note held counting on up through the octaves,
// so a shape works for a triad or a 7th chord alike. The Grooves are bass lines on rows 1 to 3 (root,
// third, fifth), an octave down.

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
// octaves, vel() velocity offsets, rat() ratchet hits, prs() 1 where pressure adds hits (P), chc()
// the percent chance a step plays.
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
   // Chance in percent.
   Pat &chc(const std::string &s) {
      return lane(s, [](Lane &l, int v) { l.chance = static_cast<uint8_t>(std::clamp(v, 0, 100)); });
   }

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
   const std::pair<clap_id, double> octRows{kEmptyRows, 2}, degree{kRowsBy, 1};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto restart = [](double r) { return std::pair<clap_id, double>{kRestart, r}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };
   const auto pressRat = [](double x) { return std::pair<clap_id, double>{kPressRatchet, x}; };
   const auto curve = [](double x) { return std::pair<clap_id, double>{kCurve, x}; };
   const auto maxHits = [](double x) { return std::pair<clap_id, double>{kMaxHits, x}; };

   std::vector<Entry> e;

   // ---- Basics: the classic arpeggiator shapes. OCT rows: row n is the nth note held, counting on up
   // through the octaves, so they work for any chord. ----
   add(e, [&] { return Entry{"Init", "Basics", {}, {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {1, 7, 4}, {2, 8, 4}})}}; });
   add(e, [&] { return Entry{"Up", "Basics", {octRows, rate(r16)},
                {Pat("1 2 3 4 5 6 7 8")}}; });
   add(e, [&] { return Entry{"Down", "Basics", {octRows, rate(r16)},
                {Pat("8 7 6 5 4 3 2 1")}}; });
   add(e, [&] { return Entry{"Up Down", "Basics", {octRows, rate(r16)},
                {Pat("1 2 3 4 5 6 7 8 7 6 5 4 3 2")}}; });
   add(e, [&] { return Entry{"Converge", "Basics", {octRows, rate(r16)},
                {Pat("1 8 2 7 3 6 4 5")}}; });
   add(e, [&] { return Entry{"Pinky Top", "Basics", {octRows, rate(r16)},
                {Pat("1 6 2 6 3 6 4 6 5 6 4 6 3 6 2 6").vel("6 -4")}}; });
   add(e, [&] { return Entry{"Root Fifth", "Basics", {degree, swing(0.54)}, {makePattern({{1, 1, 4}, {5, 2, 4}, {1, 3, 4}, {5, 4, 4}, {1, 5, 4}, {5, 6, 4}, {1, 7, 4}, {5, 8, 4}})}}; });
   add(e, [&] { return Entry{"Broken Thirds", "Basics", {degree, rate(r16)}, {makePattern({{1, 1, 4}, {3, 2, 4}, {2, 3, 4}, {4, 4, 4}, {3, 5, 4}, {5, 6, 4}, {4, 7, 4}, {6, 8, 4}})}}; });

   // ---- Berlin School: short, driving sixteenths over a root pedal, in 3 + 3 + 2 ----
   add(e, [&] { return Entry{"Octave Line", "Berlin School", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 4s 3s 1s 4s 3s 1s 5s").vel("14 -6 0 10 -6 0 8 -4")}}; });
   add(e, [&] { return Entry{"Pedal Sequence", "Berlin School", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 5s 1s 6s 1s 7s 1s 6s 1s 5s 1s 6s 1s 8s 1s 7s").vel("12 -2 -6 0")}}; });
   add(e, [&] { return Entry{"Sequencer Bass", "Berlin School", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 1s 4s 1s 1s 3s 1s 4s 1s 1s 4s 1s 2s 3s 4s 5s").oct("-1").vel("14 -8 6 -8")}}; });
   add(e, [&] { return Entry{"Phase Drift", "Berlin School", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1s 3s 5s 3s 6s 3s 5s 3s 1s 3s 5s 7s 5s 3s").vel("12 -6 0 -6 4 -6 0")}}; });
   add(e, [&] { return Entry{"Night Drive", "Berlin School", {octRows, rate(r16), advance(loop), pressVel(0.2)},
                {Pat("1s 1s 4s 1s 1s 3s 1s 4s 1s 1s 4s 1s 1s 3s 1s 4s").vel("14 -8 6 -8"),
                 Pat("1s 4s 3s 1s 4s 3s 1s 5s 1s 4s 3s 1s 4s 3s 1s 5s").vel("14 -6 0 10 -6 0 8 -4"),
                 Pat("1s 5s 1s 6s 1s 7s 1s 6s 1s 5s 1s 6s 1s 8s 1s 7s").vel("12 -2 -6 0"),
                 Pat("1s 2s 3s 4s 5s 6s 7s 8s 7s 6s 5s 4s 3s 2s 1s 2s").vel("12 -4 0 -4")}}; });
   add(e, [&] { return Entry{"Long Drive", "Berlin School", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1s 1s 4s 1s 1s 3s 1s 4s", 2) + rep("1s 4s 3s 1s 4s 3s 1s 5s", 2) + rep("1s 1s 4s 1s 1s 3s 1s 4s", 2) +
                     "1s 5s 1s 6s 1s 7s 1s 8s 7s 6s 5s 4s 3s 2s 1s 2s")
                    .oct(rep(".", 32) + rep("1", 16) + rep(".", 16))
                    .vel("14 -8 6 -8")}}; });

   // ---- Grooves: bass lines, an octave down. Row 1 is the root, row 2 the third, row 3 the fifth;
   // the Octave lane jumps them up an octave. Put a bass patch after it. ----
   add(e, [&] { return Entry{"Funk Bass", "Grooves", {rate(r16), swing(0.56), pressVel(0.3)},
                {Pat("1 . 1t 1s . 1t 3s 1 . 1s 3s . 1 1t 1s 2s")
                    .oct("-1 . -1 -1 . -1 0 -1 . -1 -1 . -1 -1 0 -1")
                    .vel("16 . -26 4 . -26 12 8 . 0 -4 . 12 -26 14 -4")}}; });
   add(e, [&] { return Entry{"Slap Pops", "Grooves", {rate(r16), swing(0.54), pressVel(0.3)},
                {Pat("1s . 1t 1s . 1s 1t . 1s 1t . 1s . 1s 3t 1s")
                    .oct("-1 . -1 0 . -1 -1 . -1 -1 . 0 . -1 -1 0")
                    .vel("14 . -26 20 . 4 -26 . 10 -26 . 20 . 4 -20 18")}}; });
   add(e, [&] { return Entry{"House Bass", "Grooves", {rate(r16), pressVel(0.3)},
                {Pat(". . 1s . . . 1s . . . 1s . . . 3s .").oct("-1").vel(". . 12 . . . 8 . . . 12 . . . 6 .")}}; });
   add(e, [&] { return Entry{"Rolling House", "Grooves", {rate(r16), pressVel(0.3)},
                {Pat("1s . 1t 1s 1s . 1t 1s 1s . 1t 1s 3s . 3t 1s")
                    .oct("-1 . -1 -1 0 . -1 -1 -1 . -1 -1 -1 . -1 0")
                    .vel("12 . -20 4 10 . -20 4 12 . -20 4 8 . -20 10")}}; });
   add(e, [&] { return Entry{"Disco Octaves", "Grooves", {rate(r8), pressVel(0.3)},
                {Pat("1s 1s 1s 1s 1s 1s 3s 3s").oct("-1 0").vel("12 4")}}; });
   add(e, [&] { return Entry{"Acid Line", "Grooves", {rate(r16), pressVel(0.2)},
                {Pat("1s 1s 3 _ 1t 2s . 1s 1s 3s . 2s 1 _ 3s 1t")
                    .oct("-1 0 0 . -1 -1 . -1 0 -1 . 0 -1 . 0 -1")
                    .vel("14 -6 18 . -10 4 . 12 -6 6 . 18 10 . 8 -12")}}; });
   add(e, [&] { return Entry{"Root Fifth Octave", "Grooves", {rate(r8), pressVel(0.3)},
                {Pat("1 3 1 3 1 3 1 2").oct("-1 -1 0 -1 -1 -1 0 -1").vel("12 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Tresillo", "Grooves", {rate(r16), pressVel(0.3)},
                {Pat("1 _ _ 1 _ _ 1 _ 3 . . 3s 1 . 1s .")
                    .oct("-1 . . -1 . . -1 . -1 . . -1 -1 . 0 .")
                    .vel("14 . . 8 . . 10 . 4 . . -6 8 . 12 .")}}; });
   add(e, [&] { return Entry{"Dub Bass", "Grooves", {rate(r8), swing(0.55), pressVel(0.3)},
                {Pat("1l _ . 3 1 _ . 2").oct("-1").vel("10 . . 4 8 . . 0")}}; });
   add(e, [&] { return Entry{"Synth Pump", "Grooves", {rate(r16), pressVel(0.2)},
                {Pat("1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 3s 3s 1s 1s").oct("-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 0 -1").vel("12 -10 0 -10")}}; });
   add(e, [&] { return Entry{"Boogie Shuffle", "Grooves", {rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1 2 3 1 3 2 1 3").oct("-1 -1 -1 0 -1 -1 -1 -1").vel("10 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Gallop", "Grooves", {rate(r16T), restart(bar), pressVel(0.3)},
                {Pat("1 1s 1s 1 1s 1s 1 1s 1s 3 3s 1s").oct("-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 0").vel("16 -8 -8 10 -8 -8 14 -8 -8 10 -4 8")}}; });
   add(e, [&] { return Entry{"Two Step", "Grooves", {rate(r16), swing(0.58), advance(chord), pressVel(0.3)},
                {Pat("1 . 1t 3s . 1s . 1s 1 . 3s . 1t 1s 3 .")
                     .oct("-1 . -1 -1 . -1 . 0 -1 . -1 . -1 -1 -1 .")
                     .vel("14 . -24 6 . 4 . 12 12 . 4 . -24 6 8 ."),
                 Pat("1s 1s . 3s 1s . 3s 1s . 1s 2s 3s 1s . 3s 2s")
                     .oct("-1 0 . -1 0 . -1 -1 . -1 -1 -1 0 . -1 -1")
                     .vel("14 6 . 4 12 . 2 8 . 6 2 4 12 . 4 0")}}; });

   // ---- Piano: accompaniment figures ----
   add(e, [&] { return Entry{"Alberti Bass", "Piano", {octRows, rate(r16)},
                {Pat("1 3 2 3 1 3 2 3").vel("10 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Waltz Spread", "Piano", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1 3 5 7 5 3", 8).vel("10 -4 0 4 0 -4")}}; });
   add(e, [&] { return Entry{"Nocturne", "Piano", {octRows, rate(r8T), pressVel(0.7)},
                {Pat("1 3 5 7 5 3 1 3 5 8 5 3", 10).vel("10 -6 -2 4 -2 -6")}}; });
   add(e, [&] { return Entry{"Stride", "Piano", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1 2+3+4s 3 2+3+4s 1 2+3+4s 3 2+3+4s").oct("-1 . -1 . -1 . -1 .").vel("10 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Pop Eighths", "Piano", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1+2+3+4 2+3+4s 2+3+4s 2+3+4s 1+2+3+4 2+3+4s 2+3+4s 2+3+4s").vel("12 -8 0 -8")}}; });
   add(e, [&] { return Entry{"Rolled Ballad", "Piano", {octRows, rate(r16), pressVel(0.7)},
                {Pat("1 2 3 4 5 . . . . . . . . . . .", 60).vel("8 2 0 2 6")}}; });
   add(e, [&] { return Entry{"Piano Verse", "Piano", {octRows, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 3 2 3 1 3 2 3 1 3 2 3 1 4 3 4").vel("10 -8 -2 -8"),
                 Pat("1 2 3 4 5 . . . . . . . . . . .", 60).vel("8 2 0 2 6"),
                 Pat("1+2+3+4 . 2+3+4s . 2+3+4s . 2+3+4s . 1+2+3+4 . 2+3+4s . 2+3+4s . 2+3+4s .").vel("12 . -8 . 0 . -8 .")}}; });

   // ---- Guitar Fingerpicking: row 1 is the thumb's bass, the rows above the fingers ----
   add(e, [&] { return Entry{"Travis Picking", "Guitar Fingerpicking", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1+6 5 3 6 1 5 3 6", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4 6 -6 4 -4")}}; });
   add(e, [&] { return Entry{"Classical Arpeggio", "Guitar Fingerpicking", {octRows, rate(r8T), pressVel(0.6)},
                {Pat("1 4 5 6 5 4", 10).vel("10 -6 -2 6 -2 -6")}}; });
   add(e, [&] { return Entry{"Banjo Roll", "Guitar Fingerpicking", {octRows, rate(r16), pressVel(0.3)},
                {Pat("3s 5s 6s 3s 5s 6s 3s 8s").vel("12 -6 -2 10 -6 -2 8 -4")}}; });
   add(e, [&] { return Entry{"Ballad Picking", "Guitar Fingerpicking", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 4 5 6 3 4 5 6", 8).vel("10 -6 -2 2 6 -6 -2 2")}}; });
   add(e, [&] { return Entry{"Fingerstyle Song", "Guitar Fingerpicking", {octRows, rate(r8), swing(0.54), advance(loop), pressVel(0.4)},
                {Pat("1+6 5 3 6 1 5 3 6", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1+6 5 3 6 1 5 3+7 6", 8).oct(". . -1 . . . . .").vel("10 -6 4 -4"),
                 Pat("1+5 4 3 5 1 4 3 6", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1 4 5 6 5 4 3 2", 6).vel("10 -6 -2 6 -2 -6 4 0")}}; });

   // ---- Strum and Harp: at 1/32, a strum spreads like a hand across strings ----
   add(e, [&] { return Entry{"Down Strum", "Strum and Harp", {octRows, rate(r32), pressVel(0.6)},
                {Pat("1 2 3 4 5 6 . . . . . . . . . .", 56).vel("10 6 3 0 -3 -6")}}; });
   add(e, [&] { return Entry{"Up Strum", "Strum and Harp", {octRows, rate(r32), pressVel(0.6)},
                {Pat("6 5 4 3 2 1 . . . . . . . . . .", 56).vel("10 6 3 0 -3 -6")}}; });
   add(e, [&] { return Entry{"Strum Pattern", "Strum and Harp", {octRows, rate(r32), pressVel(0.5)},
                {Pat("1 2 3 4 . . . . 1 2 3 4 6 5 4 . . . . . 6 5 4 . 1 2 3 4 6 5 4 .", 16)
                    .vel("12 9 6 3 . . . . 8 5 2 0 -4 -6 -8 . . . . . -2 -4 -6 . 10 7 4 1 -2 -4 -6 .")}}; });
   add(e, [&] { return Entry{"Harp Glissando", "Strum and Harp", {octRows, rate(r32), pressVel(0.5)},
                {Pat("1 2 3 4 5 6 7 8 6 7 8 . 8 7 6 8 7 6 5 4 3 2 1 . . . . . . . . .", 12)
                    .oct(". . . . . . . . 1 1 1 . 1 1 1 . . . . . . . . . . . . . . . . .")}}; });
   add(e, [&] { return Entry{"Harp Cascade", "Strum and Harp", {octRows, rate(r32), pressVel(0.5)},
                {Pat("8 6 7 5 6 4 5 3 4 2 3 1 . . . .", 12).vel("10 4 6 2 4 0 2 -2 0 -4 -2 -6")}}; });
   add(e, [&] { return Entry{"Strum to Harp", "Strum and Harp", {octRows, rate(r32), advance(chord), pressVel(0.5)},
                {Pat("1 2 3 4 . . . . 1 2 3 4 6 5 4 . . . . . 6 5 4 . 1 2 3 4 6 5 4 .", 16).vel("12 9 6 3"),
                 Pat("1 2 3 4 5 6 7 8 6 7 8 . 8 7 6 8 7 6 5 4 3 2 1 . . . . . . . . .", 12)
                     .oct(". . . . . . . . 1 1 1 . 1 1 1 . . . . . . . . . . . . . . . . .")}}; });

   // ---- Pads and Pedals: a held bass, slow tops ----
   add(e, [&] { return Entry{"Pedal Climb", "Pads and Pedals", {swing(0.54), pressVel(0.7)},
                // Row 1 held under the whole pattern, the rest climbing over it.
                {makePattern({{1, 1, 64}, {2, 1, 1}, {3, 2, 4}, {4, 3, 3}, {2, 5, 4}, {3, 6, 2}, {4, 7, 6}, {2, 9, 2},
                              {3, 11, 8}, {2, 13, 4}, {4, 14, 4}, {3, 15, 2}, {4, 16, 4}},
                             {{1, {0, 1, 0, 12}}, {4, {0, 1, 0, -20}}, {5, {0, 1, 0, 8}}, {7, {1, 1, 0, 0}}, {8, {0, 1, 0, -12}},
                              {9, {0, 1, 0, 12}}, {11, {0, 3, 0, 0}}, {12, {0, 1, 0, -20}}, {13, {0, 1, 0, 8}},
                              {14, {0, 2, 0, 0}}, {16, {1, 1, 0, -12}}},
                             16)}}; });
   add(e, [&] { return Entry{"Slow Top", "Pads and Pedals", {octRows, rate(r8), pressVel(0.8)},
                {Pat("1!+4l . 5l . 6l . 5l . 4l . 5l . 7l . 6l .")}}; });
   add(e, [&] { return Entry{"Swell Layers", "Pads and Pedals", {octRows, rate(r4), pressVel(1.0), curve(-0.4)},
                {Pat("1 3 5 7 . . . .", 14).vel("-12 -6 0 6")}}; });
   add(e, [&] { return Entry{"Drone and Drift", "Pads and Pedals", {octRows, rate(r8), pressVel(0.9)},
                {Pat("1!+5:12 . . . 6:8 . 4:12 . . . 5:8 . . . 7:16 . . . . . 8:12 . . 6:8 . . . 5:16 . . . "
                     "6:12 . . . 5:8 . 7:12 . . . 6:8 . . . 5:16 . . . . . 4:12 . . 6:8 . . . 5:16 . . . . .")
                    .vel("-6 . . . 4 . -4 . . . 6")}}; });
   add(e, [&] { return Entry{"Pedal Story", "Pads and Pedals", {octRows, rate(r8), advance(loop), pressVel(0.8)},
                {Pat("1!+4l . 5l . 6l . 5l ."),
                 Pat("1!+5l . 6l . 7l . 6l ."),
                 Pat("1!+6l . 7l . 8l . 7l ."),
                 Pat("1!+5l . 4l . 3l . 2:4 3:4")}}; });

   // ---- Ratchet and Rhythm ----
   add(e, [&] { return Entry{"Trap Hats", "Ratchet and Rhythm", {octRows, rate(r16), pressVel(0.3)},
                {Pat("6s 6s 6s 6s 6s 6s 6 _ 6s 6s 6s 6s 6 _ 6 6")
                    .rat("1 1 1 1 1 1 3 . 1 1 2 1 4 . 2 4").vel("12 -10 0 -10 6 -10 4 . 12 -10 0 -10 4 . 0 6")}}; });
   add(e, [&] { return Entry{"Stutter", "Ratchet and Rhythm", {octRows, rate(r8)},
                {Pat("1 3 2 4 1 3 5 4").rat("1 1 1 4 1 1 2 6")}}; });
   add(e, [&] { return Entry{"Machine Gun", "Ratchet and Rhythm", {octRows, rate(r16)},
                {Pat("1 . 3 . 5 . 3 . 1 . 3 . 5 . 8 .").rat("3 . 2 . 4 . 2 . 3 . 2 . 4 . 8 .")}}; });
   add(e, [&] { return Entry{"Pressure Ratchets", "Ratchet and Rhythm", {octRows, rate(r8), pressRat(1.0), maxHits(6), curve(0.3)},
                {Pat("1 3 2 4 1 3 2 5").prs("1")}}; });
   add(e, [&] { return Entry{"Stabs", "Ratchet and Rhythm", {rate(r8), pressVel(0.8)},
                {makePattern({{1, 1, 2}, {2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {1, 4, 1}, {2, 4, 1}, {3, 4, 1}, {4, 4, 1},
                              {1, 7, 2}, {2, 7, 2}, {3, 7, 2}, {4, 7, 2}},
                             {{4, {1, 1, 0, -20}}, {7, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Ghost Notes", "Ratchet and Rhythm", {octRows, rate(r16), swing(0.56), pressVel(0.3)},
                {Pat("1 3 2 3 1 3 2 4 1 3 2 3 1 4 2 3")
                    .vel("14 -24 -8 -24 10 -24 -8 -20")
                    .chc("100 35 75 30 100 45 70 25")}}; });
   add(e, [&] { return Entry{"Stutter Build", "Ratchet and Rhythm", {octRows, rate(r16), advance(loop)},
                {Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 1 1 2"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 2 1 3"),
                 Pat("1 3 2 4 5 7 6 8 1 3 2 4 5 7 6 8").rat("2 3 2 4 2 3 2 4 3 4 3 6 4 6 6 8")}}; });

   // ---- MPE Expressive: press to bring them alive ----
   add(e, [&] { return Entry{"Press Roll", "MPE Expressive", {pressVel(0.7), pressRat(0.6), maxHits(4), swing(0.54)},
                // Steps with P lit roll faster the harder each finger presses.
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}},
                             {{1, {0, 1, 0, 10}}, {2, {0, 1, 1, 0}}, {3, {0, 1, 0, -16}}, {4, {0, 2, 1, 0}},
                              {5, {0, 1, 0, 10}}, {6, {0, 1, 1, 0}}, {7, {0, 1, 0, -16}}, {8, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Press to Bloom", "MPE Expressive", {octRows, rate(r16), pressVel(1.0), curve(-0.5), pressRat(0.8), maxHits(5)},
                {Pat("1 3 2 4 3 5 4 6", 6).prs(". 1")}}; });
   add(e, [&] { return Entry{"Breath Pulse", "MPE Expressive", {octRows, rate(r8), pressVel(1.0), curve(-0.3)},
                {Pat("1 2 3 4 3 2", 8)}}; });
   add(e, [&] { return Entry{"Pressure Waves", "MPE Expressive", {octRows, rate(r16), pressVel(0.9), pressRat(1.0), maxHits(8)},
                {Pat(rep("1 3 2 4 5 4 3 2", 4)).prs(". 1")}}; });
   add(e, [&] { return Entry{"Touch Chain", "MPE Expressive", {octRows, rate(r16), advance(chord), pressVel(1.0), pressRat(0.8), maxHits(6)},
                {Pat("1 . 2 . 3 . 4 .", 8),
                 Pat("1 3 2 4 1 3 2 4").prs("1"),
                 Pat("1 2 3 4 5 6 7 8").prs(". 1")}}; });

   // ---- Long Sequences: patterns that take bars to come round ----
   add(e, [&] { return Entry{"Four Bar Groove", "Long Sequences", {octRows, rate(r16), swing(0.55), pressVel(0.4)},
                {Pat(rep("1 . 3s 1s . 2s 4 .", 3) + "1 . 3s 5s . 4s 3s 2s " + rep("2 . 4s 2s . 3s 5 .", 2) +
                     "1 . 3s 1s . 2s 4 . 5s 4s 3s 2s 1s 2s 3s 4s")
                    .vel("12 . -6 2 . -4 8 .")}}; });
   add(e, [&] { return Entry{"Generative", "Long Sequences", {octRows, rate(r16), pressVel(0.5), swing(0.54)},
                {Pat("1 5 3 6 2 5 4 8 1 6 3 7 2 5 4 6 1 5 3 8 2 6 4 7 1 5 3 6 2 7 4 8")
                    .vel("10 -8 0 -6 4 -8 0 -4")
                    .chc("100 55 70 40 85 50 65 35")}}; });
   add(e, [&] { return Entry{"Slow Evolution", "Long Sequences", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 2 3 4 3 2 3 4 1 2 3 5 3 2 3 5 1 3 4 5 4 3 4 6 2 3 5 6 5 3 6 7").vel("10 -6 -2 -6")}}; });
   add(e, [&] { return Entry{"Odyssey", "Long Sequences", {octRows, rate(r16), pressVel(0.3), swing(0.53)},
                {Pat(rep("1s 1s 3s 1s 2s 1s 4s 1s", 2) + rep("1s 1s 3s 1s 2s 1s 4s 5s", 2) + rep("2s 2s 4s 2s 3s 2s 5s 2s", 2) +
                     rep("2s 3s 4s 5s 4s 3s 4s 5s", 2) + rep("3s 3s 5s 3s 4s 3s 6s 3s", 2) + "3s 4s 5s 6s 7s 8s 7s 6s 5s 6s 7s 8s 7s 6s 5s 4s " +
                     rep("4s 3s 2s 1s 3s 2s 1s 2s", 2) + "1s 1s 3s 1s 2s 1s 4s 1s 1 _ _ _ c . . .")
                    .vel("14 -8 4 -8 8 -8 4 -8")}}; });
   add(e, [&] { return Entry{"Long Chain", "Long Sequences", {octRows, rate(r16), advance(loop), pressVel(0.4)},
                {Pat(rep("1s 2s 3s 4s 3s 2s", 4) + "1s 2s 3s 4s 5s 6s 7s 8s").vel("12 -6 -2"),
                 Pat(rep("2s 3s 4s 5s 4s 3s", 4) + "8s 7s 6s 5s 4s 3s 2s 1s").vel("12 -6 -2")}}; });

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
   add(e, [&] { return Entry{"Verse Chorus", "Chains", {octRows, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 .", 6),
                 Pat("1 . 3 . 2 . 4 . 1 . 3 . 4 . 5 .", 6),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 1s 3s 5s 3s 2s 4s 6s 8s").vel("12 -6"),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 3s 5s 7s 5s 4s 6s 8s 7s").vel("12 -6")}}; });
   add(e, [&] { return Entry{"Build Up", "Chains", {octRows, rate(r16), advance(loop)},
                {Pat("1 . . . 2 . . . 3 . . . 4 . . ."),
                 Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 ."),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"),
                 Pat("1 3 2 4 5 7 6 8 1 3 2 4 5 7 6 8").vel("10 -6"),
                 Pat("1 3 2 4 5 7 6 8 5 6 7 8 5 6 7 8").rat("1 1 1 2 1 1 1 2 2 2 2 3 3 4 6 8")}}; });
   add(e, [&] { return Entry{"Call and Response", "Chains", {octRows, rate(r16), advance(loop), swing(0.55)},
                {Pat("1 2 3 . 1 2 4 . . . . . . . . ."),
                 Pat(". . . . . . . . 8 7 6 . 8 7 5 ."),
                 Pat("1 2 3 . 1 2 4 . 1 3 5 . . . . ."),
                 Pat(". . . . . . . . 8 7 6 . 5 4 . .")}}; });
   add(e, [&] { return Entry{"Eight Bars", "Chains", {octRows, rate(r16), advance(loop), pressVel(0.4)},
                {Pat("1 2 3 4 3 2 3 4 1 2 3 4 3 2 3 4"),
                 Pat("1 2 3 4 3 2 3 4 1 2 3 4 5 4 3 4"),
                 Pat("2 3 4 5 4 3 4 5 2 3 4 5 4 3 4 5"),
                 Pat("2 3 4 5 6 5 4 3 2 3 4 5 6 7 8 7"),
                 Pat("3 4 5 6 5 4 5 6 3 4 5 6 5 4 5 6"),
                 Pat("4 5 6 7 6 5 6 7 8 7 6 5 4 3 2 1"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").vel("10 -6"),
                 Pat("1 2 3 4 5 6 7 8 1 . c . . . . .")}}; });

   // ---- Odd Meters ----
   add(e, [&] { return Entry{"Five Four", "Odd Meters", {octRows, rate(r8)},
                {Pat("1 3 5 3 5 4 2 4 3 5").vel("14 -6 -6 . . 8 -6 . . .")}}; });
   add(e, [&] { return Entry{"Seven Eight", "Odd Meters", {octRows, rate(r8)},
                {Pat("1 4 2 5 1 3 5").vel("14 -6 8 -6 8 -6 -2")}}; });
   add(e, [&] { return Entry{"Nine Eight", "Odd Meters", {octRows, rate(r8)},
                {Pat("1 4 2 5 3 6 1 3 5").vel("14 -6 6 -6 6 -6 8 -6 -4")}}; });
   add(e, [&] { return Entry{"Three Over Four", "Odd Meters", {octRows, rate(r16)},
                {Pat("1+6 . . 6 1 . 6 . 1 6 . .", 3).vel("12 . . 4 8 . 4 . 8 4 . .")}}; });
   add(e, [&] { return Entry{"Five Over Four", "Odd Meters", {octRows, rate(r16)},
                {Pat("1+6 . . . 1 6 . . 1 . 6 . 1 . . 6 1 . . .", 3).vel("12 . . . 6 6 . . 6 . 6 . 6 . . 6 6 . . .")}}; });
   add(e, [&] { return Entry{"Odd Chain", "Odd Meters", {octRows, rate(r8), advance(loop)},
                {Pat("1 4 2 5 1 3 5").vel("14 -6 8 -6 8 -6 -2"),
                 Pat("5 3 4 2 3").vel("12 -6 -6 8 -6")}}; });

   return e;
}

} // namespace library

} // namespace jarp

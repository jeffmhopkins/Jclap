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
// so a shape works for a triad or a 7th chord alike. No pattern skips a row: each walks the notes
// held in turn, and the voicing you hold gives the chord its shape. The Grooves are bass lines by
// style, an octave down; the Drum Kit presets play a kit's kick, snare and hats from the keys held.

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

// One factory preset: its name and category, a line on what it is and how to play it (shown in the
// panel's header), the params it changes from the defaults, and its patterns from slot 1 on.
struct Entry {
   const char *name;
   const char *category;
   const char *blurb;
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
   const std::pair<clap_id, double> octRows{kEmptyRows, 2}, degree{kRowsBy, 1}, keepRows{kKeepRows, 1};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto restart = [](double r) { return std::pair<clap_id, double>{kRestart, r}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };
   const auto pressRat = [](double x) { return std::pair<clap_id, double>{kPressRatchet, x}; };
   const auto curve = [](double x) { return std::pair<clap_id, double>{kCurve, x}; };
   const auto maxHits = [](double x) { return std::pair<clap_id, double>{kMaxHits, x}; };

   std::vector<Entry> e;

   // Every pattern walks the held notes without gaps: the rows it uses run on from row 1 (or, in a
   // chain, on from the rows before) with none left out, so each note held plays, and the shape of
   // the chord comes from how it's voiced. Hold a root and fifth for a root-fifth line, a triad for
   // root-third-fifth.

   // ---- Basics: the classic arpeggiator shapes. OCT rows: row n is the nth note held, counting on up
   // through the octaves, so they work for any chord. ----
   add(e, [&] { return Entry{"Init", "Basics", "The starting pattern: up through four held notes and back.", {},
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {1, 7, 4}, {2, 8, 4}})}}; });
   add(e, [&] { return Entry{"Up", "Basics", "Every held note from the lowest up, on through the octaves.", {octRows, rate(r16)},
                {Pat("1 2 3 4 5 6 7 8")}}; });
   add(e, [&] { return Entry{"Down", "Basics", "The held notes from the top down, starting an octave or so up.", {octRows, rate(r16)},
                {Pat("8 7 6 5 4 3 2 1")}}; });
   add(e, [&] { return Entry{"Up Down", "Basics", "Up through the held notes and back, without doubling the ends.", {octRows, rate(r16)},
                {Pat("1 2 3 4 5 6 7 8 7 6 5 4 3 2")}}; });
   add(e, [&] { return Entry{"Converge", "Basics", "Lowest, highest, and inward from both ends until they meet.", {octRows, rate(r16)},
                {Pat("1 8 2 7 3 6 4 5")}}; });
   add(e, [&] { return Entry{"Pinky Top", "Basics", "Each held note in turn under a repeated top note.", {octRows, rate(r16)},
                {Pat("1 6 2 6 3 6 4 6 5 6 4 6 3 6 2 6").vel("6 -4")}}; });
   add(e, [&] { return Entry{"Root Fifth", "Basics", "Rocks between your two lowest notes: hold a root and fifth.", {swing(0.54)},
                {Pat("1 2 1 2 1 2 1 2")}}; });
   add(e, [&] { return Entry{"Broken Thirds", "Basics", "Scale steps in broken thirds from the root (DEGREE rows).", {degree, rate(r16)},
                {makePattern({{1, 1, 4}, {3, 2, 4}, {2, 3, 4}, {4, 4, 4}, {3, 5, 4}, {5, 6, 4}, {4, 7, 4}, {6, 8, 4}})}}; });

   // ---- Berlin School: short, driving sixteenths over a root pedal, in 3 + 3 + 2 ----
   add(e, [&] { return Entry{"Octave Line", "Berlin School", "A root pedal under the chord and its octave, in 3+3+2.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 4s 3s 1s 4s 2s 1s 5s").vel("14 -6 0 10 -6 0 8 -4")}}; });
   add(e, [&] { return Entry{"Pedal Sequence", "Berlin School", "The root pedal, then each held note an octave up, climbing.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 2s 1s 3s 1s 4s 1s 3s 1s 2s 1s 3s 1s 5s 1s 4s").oct(". 1").vel("12 -2 -6 0")}}; });
   add(e, [&] { return Entry{"Sequencer Bass", "Berlin School", "A low, root-heavy sixteenth bass with a run up to finish.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1s 1s 4s 1s 1s 3s 1s 4s 1s 1s 4s 1s 2s 3s 4s 5s").oct("-1").vel("14 -8 6 -8")}}; });
   add(e, [&] { return Entry{"Phase Drift", "Berlin School", "Fourteen steps against the bar, a seven-step accent drifting.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1s 2s 3s 2s 4s 2s 3s 2s 1s 2s 3s 4s 3s 2s").vel("12 -6 0 -6 4 -6 0")}}; });
   add(e, [&] { return Entry{"Night Drive", "Berlin School", "Four sequencer lines, a bar each, looping one after another.", {octRows, rate(r16), advance(loop), pressVel(0.2)},
                {Pat("1s 1s 3s 1s 1s 2s 1s 3s 1s 1s 3s 1s 1s 2s 1s 4s").vel("14 -8 6 -8"),
                 Pat("1s 4s 3s 1s 4s 2s 1s 5s 1s 4s 3s 1s 4s 2s 1s 5s").vel("14 -6 0 10 -6 0 8 -4"),
                 Pat("1s 2s 1s 3s 1s 4s 1s 3s 1s 2s 1s 3s 1s 5s 1s 4s").oct(". 1").vel("12 -2 -6 0"),
                 Pat("1s 2s 3s 4s 5s 6s 7s 8s 7s 6s 5s 4s 3s 2s 1s 2s").vel("12 -4 0 -4")}}; });
   add(e, [&] { return Entry{"Long Drive", "Berlin School", "Four bars of driving sequence that lifts an octave in the third.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1s 1s 3s 1s 1s 2s 1s 3s", 2) + rep("1s 4s 3s 1s 4s 2s 1s 5s", 2) + rep("1s 1s 3s 1s 1s 2s 1s 3s", 2) +
                     "1s 2s 1s 3s 1s 4s 1s 5s 6s 5s 4s 3s 2s 3s 4s 5s")
                    .oct(rep(".", 32) + rep("1", 16) + rep(".", 16))
                    .vel("14 -8 6 -8")}}; });

   // ---- Grooves: bass lines by style, an octave down. Row 1 is the lowest note held, row 2 the next
   // and so on, with nothing skipped: hold a root and fifth for a root-fifth line, a triad for
   // root-third-fifth. Put a bass patch after it. ----
   add(e, [&] { return Entry{"Funk Bass", "Grooves: Funk", "Sixteenth funk with ghosted pickups and an octave pop.", {rate(r16), swing(0.56), pressVel(0.3)},
                {Pat("1 . 1t 1s . 1t 2s 1 . 1s 3s . 1 1t 1s 2s")
                    .oct("-1 . -1 -1 . -1 0 -1 . -1 -1 . -1 -1 0 -1")
                    .vel("16 . -26 4 . -26 12 8 . 0 -4 . 12 -26 14 -4")}}; });
   add(e, [&] { return Entry{"Slap Pops", "Grooves: Funk", "Thumb slaps on the root, octave pops, the next note as a grace.", {rate(r16), swing(0.54), pressVel(0.3)},
                {Pat("1s . 1t 1s . 1s 1t . 1s 1t . 1s . 1s 2t 1s")
                    .oct("-1 . -1 0 . -1 -1 . -1 -1 . 0 . -1 -1 0")
                    .vel("14 . -26 20 . 4 -26 . 10 -26 . 20 . 4 -20 18")}}; });
   add(e, [&] { return Entry{"Pocket Sixteenths", "Grooves: Funk", "A laid-back pocket that walks up the chord into the next bar.", {rate(r16), swing(0.56), pressVel(0.3)},
                {Pat("1 . . 1t 2s . 1s . 1 . 3s 2t . 1s 2s 3s")
                    .oct("-1 . . -1 -1 . 0 . -1 . -1 -1 . -1 -1 -1")
                    .vel("14 . . -24 6 . 10 . 10 . 4 -24 . 6 2 8")}}; });

   add(e, [&] { return Entry{"Motown Walk", "Grooves: Soul", "A busy eighth-note walk up the chord and back down.", {octRows, rate(r8), pressVel(0.3)},
                {Pat("1 1s 2 3 4 3 2s 3").oct("-1").vel("12 -6 4 0 8 0 -4 2")}}; });
   add(e, [&] { return Entry{"Soul Pocket", "Grooves: Soul", "Long roots with a lazy push to the next note, behind the beat.", {rate(r16), swing(0.58), pressVel(0.3)},
                {Pat("1 _ . 1s . . 2 . 3 _ . 2s . 1s . 2t").oct("-1").vel("12 . . -4 . . 6 . 8 . . 0 . 4 . -20")}}; });

   add(e, [&] { return Entry{"Disco Octaves", "Grooves: Disco", "Octave-jumping eighths on the root, a step up to finish.", {rate(r8), pressVel(0.3)},
                {Pat("1s 1s 1s 1s 1s 1s 2s 2s").oct("-1 0").vel("12 4")}}; });
   add(e, [&] { return Entry{"Disco Walk", "Grooves: Disco", "Octave eighths walking through three held notes, two bars.", {rate(r8), pressVel(0.3)},
                {Pat("1s 1s 1s 1s 2s 2s 2s 2s 3s 3s 3s 3s 2s 2s 2s 2s").oct("-1 0").vel("12 4")}}; });

   add(e, [&] { return Entry{"House Bass", "Grooves: House", "Off-beat bass between the kicks, a step up at the end.", {rate(r16), pressVel(0.3)},
                {Pat(". . 1s . . . 1s . . . 1s . . . 2s .").oct("-1").vel(". . 12 . . . 8 . . . 12 . . . 6 .")}}; });
   add(e, [&] { return Entry{"Rolling House", "Grooves: House", "A rolling sixteenth line that fills the gaps between kicks.", {rate(r16), pressVel(0.3)},
                {Pat("1s . 1t 1s 1s . 1t 1s 1s . 1t 1s 2s . 2t 1s")
                    .oct("-1 . -1 -1 0 . -1 -1 -1 . -1 -1 -1 . -1 0")
                    .vel("12 . -20 4 10 . -20 4 12 . -20 4 8 . -20 10")}}; });
   add(e, [&] { return Entry{"Deep Offbeat", "Grooves: House", "Deep house: long offbeat notes stepping up the chord.", {rate(r16), swing(0.54), pressVel(0.3)},
                {Pat(". . 1 _ . . 2 _ . . 1 _ . . 3 2s").oct("-1").vel(". . 10 . . . 6 . . . 10 . . . 4 0")}}; });

   add(e, [&] { return Entry{"Acid Line", "Grooves: Techno", "Squelchy sixteenths with octave jumps; add a resonant filter.", {rate(r16), pressVel(0.2)},
                {Pat("1s 1s 3 _ 1t 2s . 1s 1s 3s . 2s 1 _ 3s 1t")
                    .oct("-1 0 0 . -1 -1 . -1 0 -1 . 0 -1 . 0 -1")
                    .vel("14 -6 18 . -10 4 . 12 -6 6 . 18 10 . 8 -12")}}; });
   add(e, [&] { return Entry{"Techno Rumble", "Grooves: Techno", "A low rumble around the kick, leaving every beat for it.", {rate(r16), pressVel(0.2)},
                {Pat(". 1t 1s 1t . 1t 1s 1t . 1t 1s 1t . 1t 2s 3t").oct("-1").vel(". -10 4 -10")}}; });

   add(e, [&] { return Entry{"Synth Pump", "Grooves: Synth", "Driving sixteenths on the root, stepping up at the bar's end.", {rate(r16), pressVel(0.2)},
                {Pat("1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 1s 2s 2s 1s 1s").oct("-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 0 -1").vel("12 -10 0 -10")}}; });
   add(e, [&] { return Entry{"Outrun Drive", "Grooves: Synth", "Octave-bouncing sixteenths that climb through the chord.", {rate(r16), pressVel(0.2)},
                {Pat("1s 1s 1s 1s 1s 1s 1s 1s 2s 2s 2s 2s 3s 3s 2s 2s").oct("-1 0").vel("10 -6")}}; });

   add(e, [&] { return Entry{"Root Fifth Octave", "Grooves: Rock", "Hold a root and fifth: root, fifth and octave in rock eighths.", {rate(r8), pressVel(0.3)},
                {Pat("1 2 1 2 1 2 1 2").oct("-1 -1 0 -1 -1 -1 0 -1").vel("12 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Rock Eighths", "Grooves: Rock", "Straight root eighths with a two-note turn into each bar.", {rate(r8), pressVel(0.3)},
                {Pat("1 1 1 1 1 1 2 3 1 1 1 1 1 1 3 2").oct("-1").vel("12 0 4 0")}}; });
   add(e, [&] { return Entry{"Gallop", "Grooves: Rock", "A metal triplet gallop on the root, a jump to finish.", {rate(r16T), restart(bar), pressVel(0.3)},
                {Pat("1 1s 1s 1 1s 1s 1 1s 1s 2 2s 1s").oct("-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 0").vel("16 -8 -8 10 -8 -8 14 -8 -8 10 -4 8")}}; });

   add(e, [&] { return Entry{"Boogie Shuffle", "Grooves: Blues", "A shuffled walk through the low three notes you hold.", {rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1 2 3 1 3 2 1 3").oct("-1 -1 -1 0 -1 -1 -1 -1").vel("10 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Boogie Walk", "Grooves: Blues", "Hold root, 3rd, 5th, 6th and b7 for the boogie-woogie walk.", {octRows, rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1 2 3 4 5 4 3 2").oct("-1").vel("10 -4 4 -4")}}; });

   add(e, [&] { return Entry{"Walking Bass", "Grooves: Jazz", "Hold a 7th chord: swung quarters walk up it and back.", {octRows, rate(r8), swing(0.62), pressVel(0.3)},
                {Pat("1 _ 2 _ 3 _ 4 _ 5 _ 4 _ 3 _ 2 1t").oct("-1").vel("8 . 2 . 6 . 2 . 8 . 2 . 6 . 0 -24")}}; });
   add(e, [&] { return Entry{"Two Feel", "Grooves: Jazz", "Half notes on the low notes held, for a ballad or the head.", {octRows, rate(r8), swing(0.62), pressVel(0.3)},
                {Pat("1 _ _ _ 2 _ _ 3t 1 _ _ _ 3 _ 2 _").oct("-1").vel("8 . . . 4 . . -20 8 . . . 4 . 2 .")}}; });

   add(e, [&] { return Entry{"Tresillo", "Grooves: Latin", "The 3+3+2 tresillo on the root, answered by the next note.", {rate(r16), pressVel(0.3)},
                {Pat("1 _ _ 1 _ _ 1 _ 2 . . 2s 1 . 1s .")
                    .oct("-1 . . -1 . . -1 . -1 . . -1 -1 . 0 .")
                    .vel("14 . . 8 . . 10 . 4 . . -6 8 . 12 .")}}; });
   add(e, [&] { return Entry{"Bossa Bass", "Grooves: Latin", "Hold root and fifth: a dotted root, then the fifth, bossa style.", {rate(r8), pressVel(0.3)},
                {Pat("1 _ _ 2 2 _ _ _").oct("-1").vel("10 . . -6 4 . . .")}}; });
   add(e, [&] { return Entry{"Tumbao", "Grooves: Latin", "Afro-Cuban tumbao: notes on the and of two and on four.", {rate(r8), pressVel(0.3)},
                {Pat(". . . 1 _ _ 2 _ . . . 2 _ _ 3 _").oct("-1").vel(". . . 10 . . 6 . . . . 8 . . 6 .")}}; });

   add(e, [&] { return Entry{"Dub Bass", "Grooves: Reggae", "Heavy, spacious dub: a long root, then short steps.", {rate(r8), swing(0.55), pressVel(0.3)},
                {Pat("1l _ . 3 1 _ . 2").oct("-1").vel("10 . . 4 8 . . 0")}}; });
   add(e, [&] { return Entry{"One Drop", "Grooves: Reggae", "Leaves beat one empty and sings through the chord.", {rate(r8), swing(0.56), pressVel(0.3)},
                {Pat(". . 1 _ 2 . 3 2 . . 1 _ 3 . 2 1").oct("-1").vel(". . 10 . 4 . 6 0 . . 10 . 6 . 2 0")}}; });
   add(e, [&] { return Entry{"Steppers", "Grooves: Reggae", "Busy roots-reggae eighths on the root, pushing on four.", {rate(r8), pressVel(0.3)},
                {Pat("1 1 2 1 1 1 2 3").oct("-1").vel("12 -4 4 -4")}}; });

   add(e, [&] { return Entry{"Boom Bap Bass", "Grooves: Hip Hop", "Swung hip hop: a long root, a pickup and a step up.", {rate(r16), swing(0.58), pressVel(0.3)},
                {Pat("1 _ _ . . . . 1s . . 2 _ . . 1s .").oct("-1").vel("12 . . . . . . 4 . . 8 . . . 0 .")}}; });
   add(e, [&] { return Entry{"Sub Slide", "Grooves: Hip Hop", "Long trap-style sub notes; give the synth glide for slides.", {rate(r16), pressVel(0.3)},
                {Pat("1 _ _ _ _ _ 1s . . . 2 _ _ . 1t 1t").oct("-1").vel("12 . . . . . 4 . . . 8 . . . -10 -6")}}; });
   add(e, [&] { return Entry{"Neo Soul", "Grooves: Hip Hop", "Loose bass wandering up an extended chord and back.", {octRows, rate(r16), swing(0.6), pressVel(0.3)},
                {Pat("1 _ . 2s . 3 _ . 4 . 3s . 2 . 1s 2t").oct("-1").vel("10 . . -4 . 6 . . 8 . -2 . 4 . 0 -20")}}; });

   add(e, [&] { return Entry{"Two Step", "Grooves: Breaks", "UK garage two-step, a new figure with each chord.", {rate(r16), swing(0.58), advance(chord), pressVel(0.3)},
                {Pat("1 . 1t 2s . 1s . 1s 1 . 2s . 1t 1s 2 .")
                     .oct("-1 . -1 -1 . -1 . 0 -1 . -1 . -1 -1 -1 .")
                     .vel("14 . -24 6 . 4 . 12 12 . 4 . -24 6 8 ."),
                 Pat("1s 1s . 3s 1s . 3s 1s . 1s 2s 3s 1s . 3s 2s")
                     .oct("-1 0 . -1 0 . -1 -1 . -1 -1 -1 0 . -1 -1")
                     .vel("14 6 . 4 12 . 2 8 . 6 2 4 12 . 4 0")}}; });
   add(e, [&] { return Entry{"Jungle Roller", "Grooves: Breaks", "Long rolling sub notes that leave room for the breaks.", {rate(r16), pressVel(0.3)},
                {Pat("1 _ _ _ _ _ . . . . 1 _ 2 _ 3s .").oct("-1").vel("12 . . . . . . . . . 6 . 8 . 4 .")}}; });

   add(e, [&] { return Entry{"Two Beat", "Grooves: Country", "Root and next note on the beats, walking up at the end.", {rate(r8), pressVel(0.3)},
                {Pat("1 . 2 . 1 . 2 . 1 . 2 . 1 2 3 .").oct("-1").vel("10 . 2 . 8 . 2 . 10 . 2 . 6 2 4 .")}}; });
   add(e, [&] { return Entry{"Walk Up", "Grooves: Country", "Root and next note, then a walk up the chord to the change.", {octRows, rate(r8), pressVel(0.3)},
                {Pat("1 . 2 . 1 . 2 . 1 . 2 . 2 3 4 5").oct("-1").vel("10 . 2 . 8 . 2 . 10 . 2 . 2 4 6 8")}}; });

   // ---- Drum Kit. Hold the drums' keys and the beat plays them: row 1 the kick, 2 the
   // snare, 3 the closed hat, 4 the open hat, which is the order the General MIDI map puts them in
   // (MIDI 36, 38, 42, 46). Empty rows rest, so a drum not held stays out, and KEEP ROWS is on, so
   // letting go of one drum's key drops it out without moving the others. Each is four bars, the fourth
   // with a fill, laid out from the song's start (RESTART SONG) so they line up with the arrangement. ----
   add(e, [&] { return Entry{"Four on the Floor", "Drum Kit", "Hold kick, snare, hats (C2 D2 F#2 A#2): a four-bar house beat.", {keepRows, rate(r16), restart(song), pressVel(0.2)},
                {Pat(rep("1s 3t 4s 3t 1s+2s 3t 4s 3t 1s 3t 4s 3t 1s+2s 3t 4s 3t", 3) +
                     "1s 3t 4s 3t 1s+2s 3t 4s 3t 1s 3t 4s 3t 1s+2s 2t 2s 2t")
                    .vel(rep("12 -10 4 -10", 15) + "12 -14 -4 6")}}; });
   add(e, [&] { return Entry{"Rock Beat", "Drum Kit", "Hold kick, snare, hats (C2 D2 F#2 A#2): four bars of rock.", {keepRows, rate(r16), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s . 3s . 2s+3s . 3s . 1s+3s . 1s+3s . 2s+3s . 3s .", 3) +
                     "1s+3s . 3s . 2s+3s . 3s . 1s+3s . 1s+3s . 2s+3s . 4s 1s")
                    .vel("12 . 0 . 10 . 0 . 8 . 6 . 10 . 4 .")}}; });
   add(e, [&] { return Entry{"Rock Song", "Drum Kit", "Three bars of rock beat, then a snare-roll fill into the next.", {keepRows, rate(r16), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s . 3s . 2s+3s . 3s . 1s+3s . 1s+3s . 2s+3s . 3s .", 3) +
                     "1s+3s . 3s . 2s+3s . 3s . 2s 2s 2s 2s 2s 2s 1s+4s .")
                    .rat(rep(".", 56) + "1 1 2 2 3 4 . .")
                    .vel(rep("12 . 0 . 10 . 0 . 8 . 6 . 10 . 0 .", 3) + "12 . 0 . 10 . 0 . -12 -8 -4 0 4 8 14 .")}}; });
   add(e, [&] { return Entry{"Boom Bap Kit", "Drum Kit", "Hold kick, snare and hat (C2 D2 F#2): four swung, dusty bars.", {keepRows, rate(r16), swing(0.58), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s . 3s . 2s+3s . 3s 1s 3s . 1s+3s . 2s+3s . 3s 1s", 3) +
                     "1s+3s . 3s . 2s+3s . 3s 1s 3s . 1s+3s . 2s+3s 1s 3s 2t")
                     .vel("12 . -4 . 10 . -4 2 -4 . 8 . 10 . -4 0")
                     .chc(rep("100 100 100 100 100 100 100 100 100 100 100 100 100 100 100 60", 3) + "100")}}; });
   add(e, [&] { return Entry{"Trap Kit", "Drum Kit", "Hold kick, snare, hats (C2 D2 F#2 A#2): half-time trap rolls.", {keepRows, rate(r16), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3t 3t 3t 3t 3t 3t 3t 1s+3t 2s+3t 3t 1s+3t 3t 3 3t 3 4s", 3) +
                     "1s+3t 3t 3t 3t 3t 3t 3t 1s+3t 2s+3t 3t 1s+3t 3t 3 3 3 3")
                     .rat(rep(". . . . . . 2 . . . . . 3 . 4 .", 3) + ". . . . . . 2 . . . . . 3 4 6 8")
                     .vel("12 -10 0 -10 4 -10 2 8 12 -10 6 -10 2 -10 4 6")}}; });
   add(e, [&] { return Entry{"Funky Break", "Drum Kit", "Hold kick, snare and hat (C2 D2 F#2): a break with ghost snares.", {keepRows, rate(r16), swing(0.54), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s 3s 1s+3s 3s 2s+3s 3s 3s 2t+3s 3s 2t+3s 1s+3s 3s 2s+3s 3s 1s+3s 2t", 3) +
                     "1s+3s 3s 1s+3s 3s 2s+3s 3s 3s 2t+3s 3s 2t+3s 1s+3s 3s 2s+3s 2t 2s 2t")
                     .vel(rep("12 -10 8 -10 14 -10 -4 -24 4 -24 8 -10 14 -10 4 -26", 3) +
                          "12 -10 8 -10 14 -10 -4 -24 4 -24 8 -10 14 -20 8 -12")}}; });
   add(e, [&] { return Entry{"Drum and Bass Beat", "Drum Kit", "Hold kick, snare, hats (C2 D2 F#2 A#2): fast two-step DnB.", {keepRows, rate(r16), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s . 3s . 2s+3s . 3s . 3s . 1s+3s . 2s+3s . 3s 2t", 3) +
                     "1s+3s . 3s . 2s+3s . 3s . 3s . 1s+3s . 2s 2t 2s+4s 2t")
                     .vel("12 . -4 . 12 . -4 . -2 . 8 . 12 . -4 -20")}}; });
   add(e, [&] { return Entry{"One Drop Kit", "Drum Kit", "Hold kick, snare, hats: reggae, kick and snare together on three.", {keepRows, rate(r16), swing(0.6), restart(song), pressVel(0.2)},
                {Pat(rep("3s . 3s . 3s . 3s . 1s+2s+3s . 3s . 3s . 4s .", 3) + "3s . 3s . 3s . 3s . 1s+2s+3s . 3s . 2s 2t 2s 4s")
                     .vel(rep("0 . -6 . 2 . -6 . 12 . -6 . 2 . 4 .", 3) + "0 . -6 . 2 . -6 . 12 . -6 . 2 -14 6 4")}}; });
   add(e, [&] { return Entry{"Shuffle Kit", "Drum Kit", "Hold kick, snare and hat (C2 D2 F#2): a triplet blues shuffle.", {keepRows, rate(r8T), restart(song), pressVel(0.2)},
                {Pat(rep("1s+3s . 3t 2s+3s . 3t 1s+3s . 3t 2s+3s . 3t", 3) + "1s+3s . 3t 2s+3s . 3t 1s+3s . 3t 2s 2s 2s")
                     .vel(rep("12 . -10 10 . -10 8 . -10 10 . -10", 3) + "12 . -10 10 . -10 8 . -10 2 6 10")}}; });

   // ---- Percussion: percussion figures for pitched sounds (a bell, mallet, pluck or
   // drum), walking the notes held. ----
   add(e, [&] { return Entry{"Son Clave", "Percussion", "The 3-2 son clave, walking through the notes you hold.", {octRows, rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1s . . 2s . . 3s . . . 2s . 1s . . .").vel("12 . . 6 . . 8 . . . 6 . 10 . . .")}}; });
   add(e, [&] { return Entry{"Rumba Clave", "Percussion", "The 3-2 rumba clave, its third hit pushed late.", {octRows, rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1s . . 2s . . . 3s . . 2s . 1s . . .").vel("12 . . 6 . . . 8 . . 6 . 10 . . .")}}; });
   add(e, [&] { return Entry{"Cascara", "Percussion", "The timbale shell pattern of salsa, for a bell or woodblock.", {octRows, rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1s . 2s . 1s 2s . 3s . 2s . 1s 3s . 2s .").vel("12 . -4 . 6 -4 . 8 . -4 . 6 8 . -4 .")}}; });
   add(e, [&] { return Entry{"Conga Tumbao", "Percussion", "Soft heel-toe, a slap, then two open tones into the next beat.", {octRows, rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1t 1t 3s 1t 1t 1t 2s 2s 1t 1t 3s 1t 1t 1t 2s 2s").vel("-18 -24 8 -24 -18 -24 10 6")}}; });
   add(e, [&] { return Entry{"Bell Six Eight", "Percussion", "The West African 12/8 bell, for a bell, kalimba or mallet.", {octRows, rate(r8T), restart(bar), pressVel(0.3)},
                {Pat("1s . 2s . 3s 1s . 2s . 3s . 2s").vel("12 . 2 . 6 4 . 2 . 6 . 2")}}; });
   add(e, [&] { return Entry{"Teental", "Percussion", "Sixteen beats of tabla strokes; the lowest note is the bass drum.", {octRows, rate(r8), restart(bar), pressVel(0.3)},
                {Pat("1+3 1+2 1+2 1+3 1+3 1+2 1+2 1+3 1+3 2s 2s 3s 3s 1+2 1+2 1+3").vel("12 0 0 6 10 0 0 6 10 -6 -6 -2 -2 0 0 8")}}; });
   add(e, [&] { return Entry{"Shaker", "Percussion", "Sixteenth shaker accents on the and, for any short sound.", {octRows, rate(r16), restart(bar), pressVel(0.2)},
                {Pat("1t 1t 2s 1t 1t 1t 2s 1t 1t 1t 2s 1t 1t 1t 2s 2t").vel("-20 -12 10 -12")}}; });
   add(e, [&] { return Entry{"Tambourine", "Percussion", "Sixteenth shakes, the hit on two and four: hold two notes.", {rate(r16), restart(bar), pressVel(0.2)},
                {Pat("1t 1t 1t 1t 2s 1t 1t 1t 1t 1t 1t 1t 2s 1t 1t 1t").vel("-12 -22 -6 -22 12 -22 -6 -22")}}; });
   add(e, [&] { return Entry{"Shaker Shuffle", "Percussion", "A triplet shaker, the swing of a blues or a 12/8.", {rate(r8T), restart(bar), pressVel(0.2)},
                {Pat("2s 1t 1t 2s 1t 1t 2s 1t 1t 2s 1t 1t").vel("8 -20 -12")}}; });
   add(e, [&] { return Entry{"Cajon", "Percussion", "Bass and slap on a box drum, a busy acoustic groove.", {rate(r16), swing(0.55), restart(bar), pressVel(0.3)},
                {Pat("1 . . 1t 2s . 1t . 1 . 1t . 2s . 1t 2t").vel("12 . . -16 10 . -18 . 10 . -16 . 12 . -18 -10")}}; });
   add(e, [&] { return Entry{"Djembe", "Percussion", "Bass, tone and slap, low to high, in a West African groove.", {rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1 . 3t 3t 2 2 . 3t 1 . 3t 3t 2 2 3s 3t").vel("12 . -6 -10 4 2 . -10 10 . -6 -10 4 2 8 -10")}}; });
   add(e, [&] { return Entry{"Bongo Martillo", "Percussion", "The bongo hammer: steady eighths, open tone on the low drum.", {rate(r16), restart(bar), pressVel(0.3)},
                {Pat("2s 2t 2s 2t 2s 2t 2s 1s 2s 2t 2s 2t 2s 2t 2s 1s").vel("10 -16 2 -16 6 -16 2 12")}}; });
   add(e, [&] { return Entry{"Maqsum", "Percussion", "The doum and tek of a darbuka or frame drum, Middle Eastern.", {rate(r16), restart(bar), pressVel(0.3)},
                {Pat("1 . 2s . . . 2s . 1 . . . 2s . 2t 2t").vel("12 . 4 . . . 4 . 10 . . . 6 . -12 -8")}}; });
   add(e, [&] { return Entry{"Marimba Ostinato", "Percussion", "An interlocking twelve-step mallet figure drifting on the bar.", {octRows, rate(r16), restart(song), pressVel(0.4)},
                {Pat("1s 2s 3s 1s 2s 4s 1s 3s 2s 4s 3s 5s").vel("10 -6 0 6 -6 2 8 -4 0 4 -4 6")}}; });

   // ---- Piano: accompaniment figures ----
   add(e, [&] { return Entry{"Alberti Bass", "Piano", "Low, high, middle, high: the classical left hand.", {octRows, rate(r16)},
                {Pat("1 3 2 3 1 3 2 3").vel("10 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Waltz Spread", "Piano", "Up and back down the chord in a bar of 3/4.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1 2 3 4 3 2", 8).vel("10 -4 0 4 0 -4")}}; });
   add(e, [&] { return Entry{"Nocturne", "Piano", "Triplets sweeping up the chord and back, a romantic left hand.", {octRows, rate(r8T), pressVel(0.7)},
                {Pat("1 2 3 4 5 4 3 2 1 2 3 4", 10).vel("10 -6 -2 4 6 2 -2 -6")}}; });
   add(e, [&] { return Entry{"Stride", "Piano", "Bass note, chord, bass note, chord, swung.", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1 2+3+4s 3 2+3+4s 1 2+3+4s 3 2+3+4s").oct("-1 . -1 . -1 . -1 .").vel("10 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Pop Eighths", "Piano", "Steady chord eighths, the full chord on each beat.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1+2+3+4 2+3+4s 2+3+4s 2+3+4s 1+2+3+4 2+3+4s 2+3+4s 2+3+4s").vel("12 -8 0 -8")}}; });
   add(e, [&] { return Entry{"Rolled Ballad", "Piano", "A slow roll up the chord, left to ring.", {octRows, rate(r16), pressVel(0.7)},
                {Pat("1 2 3 4 5 . . . . . . . . . . .", 60).vel("8 2 0 2 6")}}; });
   add(e, [&] { return Entry{"Piano Verse", "Piano", "Alberti, a rolled chord and chord eighths, one per chord.", {octRows, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 3 2 3 1 3 2 3 1 3 2 3 1 4 3 4").vel("10 -8 -2 -8"),
                 Pat("1 2 3 4 5 . . . . . . . . . . .", 60).vel("8 2 0 2 6"),
                 Pat("1+2+3+4 . 2+3+4s . 2+3+4s . 2+3+4s . 1+2+3+4 . 2+3+4s . 2+3+4s . 2+3+4s .").vel("12 . -8 . 0 . -8 .")}}; });

   // ---- Guitar Fingerpicking: the low rows are the thumb's bass, the rows above the fingers ----
   add(e, [&] { return Entry{"Travis Picking", "Guitar Fingerpicking", "An alternating thumb on the low notes, fingers on the top.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1+4 3 2 4 1 3 2 4", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4 6 -6 4 -4")}}; });
   add(e, [&] { return Entry{"Classical Arpeggio", "Guitar Fingerpicking", "Thumb then three fingers, p i m a, up and back.", {octRows, rate(r8T), pressVel(0.6)},
                {Pat("1 2 3 4 3 2", 10).vel("10 -6 -2 6 -2 -6")}}; });
   add(e, [&] { return Entry{"Banjo Roll", "Guitar Fingerpicking", "A bright forward roll, in 3+3+2 sixteenths.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("1s 2s 3s 1s 2s 3s 1s 4s").vel("12 -6 -2 10 -6 -2 8 -4")}}; });
   add(e, [&] { return Entry{"Ballad Picking", "Guitar Fingerpicking", "Thumb on the low two notes, fingers rising through the rest.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 3 4 5 2 3 4 5", 8).vel("10 -6 -2 2 6 -6 -2 2")}}; });
   add(e, [&] { return Entry{"Fingerstyle Song", "Guitar Fingerpicking", "Four picking patterns, a bar each, looping.", {octRows, rate(r8), swing(0.54), advance(loop), pressVel(0.4)},
                {Pat("1+4 3 2 4 1 3 2 4", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1+4 3 2 4 1 3 2+5 4", 8).oct(". . -1 . . . . .").vel("10 -6 4 -4"),
                 Pat("1+4 3 2 4 1 3 2 5", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1 2 3 4 5 4 3 2", 6).vel("10 -6 -2 6 -2 -6 4 0")}}; });

   // ---- Strum and Harp: at 1/32, a strum spreads like a hand across strings ----
   add(e, [&] { return Entry{"Down Strum", "Strum and Harp", "One down strum across six held notes per bar.", {octRows, rate(r32), pressVel(0.6)},
                {Pat("1 2 3 4 5 6 . . . . . . . . . .", 56).vel("10 6 3 0 -3 -6")}}; });
   add(e, [&] { return Entry{"Up Strum", "Strum and Harp", "One up strum, top string first, per bar.", {octRows, rate(r32), pressVel(0.6)},
                {Pat("6 5 4 3 2 1 . . . . . . . . . .", 56).vel("10 6 3 0 -3 -6")}}; });
   add(e, [&] { return Entry{"Strum Pattern", "Strum and Harp", "Down, down-up, up-down-up: a strumming hand.", {octRows, rate(r32), pressVel(0.5)},
                {Pat("1 2 3 4 . . . . 1 2 3 4 6 5 4 . . . . . 6 5 4 . 1 2 3 4 6 5 4 .", 16)
                    .vel("12 9 6 3 . . . . 8 5 2 0 -4 -6 -8 . . . . . -2 -4 -6 . 10 7 4 1 -2 -4 -6 .")}}; });
   add(e, [&] { return Entry{"Harp Glissando", "Strum and Harp", "A sweep up the strings and back down.", {octRows, rate(r32), pressVel(0.5)},
                {Pat("1 2 3 4 5 6 7 8 6 7 8 . 8 7 6 8 7 6 5 4 3 2 1 . . . . . . . . .", 12)
                    .oct(". . . . . . . . 1 1 1 . 1 1 1 . . . . . . . . . . . . . . . . .")}}; });
   add(e, [&] { return Entry{"Harp Cascade", "Strum and Harp", "Falling pairs, tumbling down the chord.", {octRows, rate(r32), pressVel(0.5)},
                {Pat("8 6 7 5 6 4 5 3 4 2 3 1 . . . .", 12).vel("10 4 6 2 4 0 2 -2 0 -4 -2 -6")}}; });
   add(e, [&] { return Entry{"Strum to Harp", "Strum and Harp", "A strum pattern, then a glissando, with each chord.", {octRows, rate(r32), advance(chord), pressVel(0.5)},
                {Pat("1 2 3 4 . . . . 1 2 3 4 6 5 4 . . . . . 6 5 4 . 1 2 3 4 6 5 4 .", 16).vel("12 9 6 3"),
                 Pat("1 2 3 4 5 6 7 8 6 7 8 . 8 7 6 8 7 6 5 4 3 2 1 . . . . . . . . .", 12)
                     .oct(". . . . . . . . 1 1 1 . 1 1 1 . . . . . . . . . . . . . . . . .")}}; });

   // ---- Pads and Pedals: a held bass, slow tops ----
   add(e, [&] { return Entry{"Pedal Climb", "Pads and Pedals", "The lowest note held through, the rest climbing over it.", {swing(0.54), pressVel(0.7)},
                // Row 1 held under the whole pattern, the rest climbing over it.
                {makePattern({{1, 1, 64}, {2, 1, 1}, {3, 2, 4}, {4, 3, 3}, {2, 5, 4}, {3, 6, 2}, {4, 7, 6}, {2, 9, 2},
                              {3, 11, 8}, {2, 13, 4}, {4, 14, 4}, {3, 15, 2}, {4, 16, 4}},
                             {{1, {0, 1, 0, 12}}, {4, {0, 1, 0, -20}}, {5, {0, 1, 0, 8}}, {7, {1, 1, 0, 0}}, {8, {0, 1, 0, -12}},
                              {9, {0, 1, 0, 12}}, {11, {0, 3, 0, 0}}, {12, {0, 1, 0, -20}}, {13, {0, 1, 0, 8}},
                              {14, {0, 2, 0, 0}}, {16, {1, 1, 0, -12}}},
                             16)}}; });
   add(e, [&] { return Entry{"Slow Top", "Pads and Pedals", "A held bass with a slow melody over it.", {octRows, rate(r8), pressVel(0.8)},
                {Pat("1!+2l . 3l . 4l . 3l . 2l . 3l . 5l . 4l .")}}; });
   add(e, [&] { return Entry{"Swell Layers", "Pads and Pedals", "Notes stacking up one by one into the full chord.", {octRows, rate(r4), pressVel(1.0), curve(-0.4)},
                {Pat("1 2 3 4 . . . .", 14).vel("-12 -6 0 6")}}; });
   add(e, [&] { return Entry{"Drone and Drift", "Pads and Pedals", "A drone under long notes drifting over four bars.", {octRows, rate(r8), pressVel(0.9)},
                {Pat("1!+3:12 . . . 4:8 . 2:12 . . . 3:8 . . . 5:16 . . . . . 6:12 . . 4:8 . . . 3:16 . . . "
                     "4:12 . . . 3:8 . 5:12 . . . 4:8 . . . 3:16 . . . . . 2:12 . . 4:8 . . . 3:16 . . . . .")
                    .vel("-6 . . . 4 . -4 . . . 6")}}; });
   add(e, [&] { return Entry{"Pedal Story", "Pads and Pedals", "Four bars of melody over a held bass, looping.", {octRows, rate(r8), advance(loop), pressVel(0.8)},
                {Pat("1!+2l . 3l . 4l . 3l ."),
                 Pat("1!+2l . 3l . 4l . 5l ."),
                 Pat("1!+3l . 4l . 5l . 2l ."),
                 Pat("1!+5l . 4l . 3l . 2:4 3:4")}}; });

   // ---- Ratchet and Rhythm ----
   add(e, [&] { return Entry{"Trap Hats", "Ratchet and Rhythm", "Trap hat rolls across the held notes, in 2s, 3s and 4s.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("1s 2s 3s 1s 2s 3s 1 _ 2s 3s 1s 2s 3 _ 1 2")
                    .rat("1 1 1 1 1 1 3 . 1 1 2 1 4 . 2 4").vel("12 -10 0 -10 6 -10 4 . 12 -10 0 -10 4 . 0 6")}}; });
   add(e, [&] { return Entry{"Stutter", "Ratchet and Rhythm", "A broken pattern that stutters on the off-beats.", {octRows, rate(r8)},
                {Pat("1 3 2 4 1 3 5 4").rat("1 1 1 4 1 1 2 6")}}; });
   add(e, [&] { return Entry{"Machine Gun", "Ratchet and Rhythm", "Ratcheted bursts up and down the chord, eight to finish.", {octRows, rate(r16)},
                {Pat("1 . 2 . 3 . 2 . 1 . 2 . 3 . 4 .").rat("3 . 2 . 4 . 2 . 3 . 2 . 4 . 8 .")}}; });
   add(e, [&] { return Entry{"Pressure Ratchets", "Ratchet and Rhythm", "Press harder for more hits on every step.", {octRows, rate(r8), pressRat(1.0), maxHits(6), curve(0.3)},
                {Pat("1 3 2 4 1 3 2 5").prs("1")}}; });
   add(e, [&] { return Entry{"Stabs", "Ratchet and Rhythm", "Short chord stabs in 3+3+2.", {rate(r8), pressVel(0.8)},
                {makePattern({{1, 1, 2}, {2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {1, 4, 1}, {2, 4, 1}, {3, 4, 1}, {4, 4, 1},
                              {1, 7, 2}, {2, 7, 2}, {3, 7, 2}, {4, 7, 2}},
                             {{4, {1, 1, 0, -20}}, {7, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Ghost Notes", "Ratchet and Rhythm", "Quiet off-beats left to chance around steady down-beats.", {octRows, rate(r16), swing(0.56), pressVel(0.3)},
                {Pat("1 3 2 3 1 3 2 4 1 3 2 3 1 4 2 3")
                    .vel("14 -24 -8 -24 10 -24 -8 -20")
                    .chc("100 35 75 30 100 45 70 25")}}; });
   add(e, [&] { return Entry{"Stutter Build", "Ratchet and Rhythm", "Four bars, each with more ratchets than the last.", {octRows, rate(r16), advance(loop)},
                {Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 1 1 2"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").rat("1 2 1 3"),
                 Pat("1 3 2 4 5 7 6 8 1 3 2 4 5 7 6 8").rat("2 3 2 4 2 3 2 4 3 4 3 6 4 6 6 8")}}; });

   // ---- MPE Expressive: press to bring them alive ----
   add(e, [&] { return Entry{"Press Roll", "MPE Expressive", "Steps marked P roll faster the harder each finger presses.", {pressVel(0.7), pressRat(0.6), maxHits(4), swing(0.54)},
                // Steps with P lit roll faster the harder each finger presses.
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}},
                             {{1, {0, 1, 0, 10}}, {2, {0, 1, 1, 0}}, {3, {0, 1, 0, -16}}, {4, {0, 2, 1, 0}},
                              {5, {0, 1, 0, 10}}, {6, {0, 1, 1, 0}}, {7, {0, 1, 0, -16}}, {8, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Press to Bloom", "MPE Expressive", "Soft when touched lightly, louder and busier as you press.", {octRows, rate(r16), pressVel(1.0), curve(-0.5), pressRat(0.8), maxHits(5)},
                {Pat("1 3 2 4 3 5 4 6", 6).prs(". 1")}}; });
   add(e, [&] { return Entry{"Breath Pulse", "MPE Expressive", "A gentle pulse whose loudness follows your pressure.", {octRows, rate(r8), pressVel(1.0), curve(-0.3)},
                {Pat("1 2 3 4 3 2", 8)}}; });
   add(e, [&] { return Entry{"Pressure Waves", "MPE Expressive", "Press into a note for waves of ratchets, up to eight hits.", {octRows, rate(r16), pressVel(0.9), pressRat(1.0), maxHits(8)},
                {Pat(rep("1 3 2 4 5 4 3 2", 4)).prs(". 1")}}; });
   add(e, [&] { return Entry{"Touch Chain", "MPE Expressive", "Three patterns, one per chord, each more pressure-driven.", {octRows, rate(r16), advance(chord), pressVel(1.0), pressRat(0.8), maxHits(6)},
                {Pat("1 . 2 . 3 . 4 .", 8),
                 Pat("1 3 2 4 1 3 2 4").prs("1"),
                 Pat("1 2 3 4 5 6 7 8").prs(". 1")}}; });

   // ---- Long Sequences: patterns that take bars to come round ----
   add(e, [&] { return Entry{"Four Bar Groove", "Long Sequences", "A swung four-bar figure with a run to close.", {octRows, rate(r16), swing(0.55), pressVel(0.4)},
                {Pat(rep("1 . 3s 1s . 2s 4 .", 3) + "1 . 3s 5s . 4s 3s 2s " + rep("2 . 4s 2s . 3s 5 .", 2) +
                     "1 . 3s 1s . 2s 4 . 5s 4s 3s 2s 1s 2s 3s 4s")
                    .vel("12 . -6 2 . -4 8 .")}}; });
   add(e, [&] { return Entry{"Generative", "Long Sequences", "Thirty-two steps, every one left to chance, never twice alike.", {octRows, rate(r16), pressVel(0.5), swing(0.54)},
                {Pat("1 5 3 6 2 5 4 8 1 6 3 7 2 5 4 6 1 5 3 8 2 6 4 7 1 5 3 6 2 7 4 8")
                    .vel("10 -8 0 -6 4 -8 0 -4")
                    .chc("100 55 70 40 85 50 65 35")}}; });
   add(e, [&] { return Entry{"Slow Evolution", "Long Sequences", "A figure that climbs a little higher every bar.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 2 3 4 3 2 3 4 1 2 3 5 3 2 3 5 1 3 4 5 4 3 4 6 2 3 5 6 5 3 6 7").vel("10 -6 -2 -6")}}; });
   add(e, [&] { return Entry{"Odyssey", "Long Sequences", "Eight bars climbing up the chord and back, a chord to end.", {octRows, rate(r16), pressVel(0.3), swing(0.53)},
                {Pat(rep("1s 1s 3s 1s 2s 1s 4s 1s", 2) + rep("1s 1s 3s 1s 2s 1s 4s 5s", 2) + rep("2s 2s 4s 2s 3s 2s 5s 2s", 2) +
                     rep("2s 3s 4s 5s 4s 3s 4s 5s", 2) + rep("3s 3s 5s 3s 4s 3s 6s 3s", 2) + "3s 4s 5s 6s 7s 8s 7s 6s 5s 6s 7s 8s 7s 6s 5s 4s " +
                     rep("4s 3s 2s 1s 3s 2s 1s 2s", 2) + "1s 1s 3s 1s 2s 1s 4s 1s 1 _ _ _ c . . .")
                    .vel("14 -8 4 -8 8 -8 4 -8")}}; });
   add(e, [&] { return Entry{"Long Chain", "Long Sequences", "Two 32-step patterns, one after the other.", {octRows, rate(r16), advance(loop), pressVel(0.4)},
                {Pat(rep("1s 2s 3s 4s 3s 2s", 4) + "1s 2s 3s 4s 5s 6s 7s 8s").vel("12 -6 -2"),
                 Pat(rep("2s 3s 4s 5s 4s 3s", 4) + "8s 7s 6s 5s 4s 3s 2s 1s").vel("12 -6 -2")}}; });

   // ---- Chains: several patterns, one after another ----
   add(e, [&] { return Entry{"Chord Chain", "Chains", "Up, down, then a broken pattern: each new chord plays the next.", {advance(chord)},
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}}),
                 makePattern({{4, 1, 4}, {3, 2, 4}, {2, 3, 4}, {1, 4, 4}, {4, 5, 4}, {3, 6, 4}, {2, 7, 4}, {1, 8, 4}}),
                 makePattern({{1, 1, 2}, {3, 2, 2}, {1, 3, 2}, {3, 4, 2}, {2, 5, 2}, {4, 6, 2}, {2, 7, 2}, {4, 8, 4}},
                             {{2, {1, 1, 0, 0}}, {6, {1, 1, 0, 0}}, {8, {0, 2, 0, 0}}})}}; });
   add(e, [&] { return Entry{"Phrase Loop", "Chains", "A climb, the climb an octave up, and a fill, round and round.", {advance(loop), swing(0.54)},
                {makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}}),
                 Pat("1 2 3 4 3 2 3 4").oct("1"),
                 makePattern({{4, 1, 4}, {3, 2, 4}, {2, 3, 4}, {1, 4, 4}},
                             {{1, {1, 2, 0, 10}}, {2, {0, 2, 0, 0}}, {3, {0, 3, 0, 0}}, {4, {0, 4, 0, 16}}}, 4)}}; });
   add(e, [&] { return Entry{"Verse Chorus", "Chains", "Two verse patterns and two busier chorus ones, one per chord.", {octRows, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 .", 6),
                 Pat("1 . 3 . 2 . 4 . 1 . 3 . 4 . 5 .", 6),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 1s 3s 5s 3s 2s 4s 6s 7s").vel("12 -6"),
                 Pat("1s 3s 5s 3s 2s 4s 6s 4s 3s 5s 7s 5s 4s 6s 8s 7s").vel("12 -6")}}; });
   add(e, [&] { return Entry{"Build Up", "Chains", "Five bars getting busier, to a ratchet roll.", {octRows, rate(r16), advance(loop)},
                {Pat("1 . . . 2 . . . 3 . . . 4 . . ."),
                 Pat("1 . 3 . 2 . 4 . 1 . 3 . 2 . 4 ."),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4"),
                 Pat("1 3 2 4 5 7 6 8 1 3 2 4 5 7 6 8").vel("10 -6"),
                 Pat("1 3 2 4 5 7 6 8 5 6 7 8 5 6 7 8").rat("1 1 1 2 1 1 1 2 2 2 2 3 3 4 6 8")}}; });
   add(e, [&] { return Entry{"Call and Response", "Chains", "A low phrase, answered up high, twice.", {octRows, rate(r16), advance(loop), swing(0.55)},
                {Pat("1 2 3 . 1 2 4 . . . . . . . . ."),
                 Pat(". . . . . . . . 8 7 6 . 8 7 5 ."),
                 Pat("1 2 3 . 1 2 4 . 1 3 5 . . . . ."),
                 Pat(". . . . . . . . 8 7 6 . 5 4 . .")}}; });
   add(e, [&] { return Entry{"Eight Bars", "Chains", "All eight slots, a bar each: a whole eight-bar phrase.", {octRows, rate(r16), advance(loop), pressVel(0.4)},
                {Pat("1 2 3 4 3 2 3 4 1 2 3 4 3 2 3 4"),
                 Pat("1 2 3 4 3 2 3 4 1 2 3 4 5 4 3 4"),
                 Pat("2 3 4 5 4 3 4 5 2 3 4 5 4 3 4 5"),
                 Pat("2 3 4 5 6 5 4 3 2 3 4 5 6 7 8 7"),
                 Pat("3 4 5 6 5 4 5 6 3 4 5 6 5 4 5 6"),
                 Pat("4 5 6 7 6 5 6 7 8 7 6 5 4 3 2 1"),
                 Pat("1 3 2 4 1 3 2 4 1 3 2 4 1 3 2 4").vel("10 -6"),
                 Pat("1 2 3 4 5 6 7 8 1 . c . . . . .")}}; });

   // ---- Odd Meters ----
   add(e, [&] { return Entry{"Five Four", "Odd Meters", "A bar of 5/4 in eighths, accented 3+2.", {octRows, rate(r8)},
                {Pat("1 3 5 3 5 4 2 4 3 5").vel("14 -6 -6 . . 8 -6 . . .")}}; });
   add(e, [&] { return Entry{"Seven Eight", "Odd Meters", "A bar of 7/8, accented 2+2+3.", {octRows, rate(r8)},
                {Pat("1 4 2 5 1 3 5").vel("14 -6 8 -6 8 -6 -2")}}; });
   add(e, [&] { return Entry{"Nine Eight", "Odd Meters", "A bar of 9/8, accented 2+2+2+3.", {octRows, rate(r8)},
                {Pat("1 4 2 5 3 6 1 3 5").vel("14 -6 6 -6 6 -6 8 -6 -4")}}; });
   add(e, [&] { return Entry{"Three Over Four", "Odd Meters", "The low note in fours, the next in threes, a cross-rhythm.", {octRows, rate(r16)},
                {Pat("1+2 . . 2 1 . 2 . 1 2 . .", 3).vel("12 . . 4 8 . 4 . 8 4 . .")}}; });
   add(e, [&] { return Entry{"Five Over Four", "Odd Meters", "The low note in fours, the next in fives, a cross-rhythm.", {octRows, rate(r16)},
                {Pat("1+2 . . . 1 2 . . 1 . 2 . 1 . . 2 1 . . .", 3).vel("12 . . . 6 6 . . 6 . 6 . 6 . . 6 6 . . .")}}; });
   add(e, [&] { return Entry{"Odd Chain", "Odd Meters", "A bar of 7/8, then one of 5/8, looping.", {octRows, rate(r8), advance(loop)},
                {Pat("1 4 2 5 1 3 5").vel("14 -6 8 -6 8 -6 -2"),
                 Pat("5 3 4 2 3").vel("12 -6 -6 8 -6")}}; });

   return e;
}

} // namespace library

} // namespace jarp

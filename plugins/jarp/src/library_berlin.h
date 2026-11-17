#pragma once

// JArp's Berlin School presets: the sequencer ostinatos of 1970s German electronic music, short
// staccato sixteenths over a pedal, leaping octaves, accented in 3 + 3 + 2, galloping, drifting
// against the bar, and slowly changing over many bars.
//
// They take few notes. A sequencer of the time played one line and a key transposed it, so each
// pattern here uses only rows 1 to 3 (or 1 to 2, or row 1 alone) and gets its octaves from the
// Octave lane: hold one note, a root and fifth, or a triad, as the blurb says, and move the hand to
// transpose. Empty Rows is OCT, so holding fewer notes than a pattern asks for still plays it, the
// missing notes an octave up. Pedal Bounce alone sets its LENGTH by the notes HELD.
//
// Included from library.h, after Pat, Entry and add(); build() calls addBerlin() in its place.

#include <string>
#include <utility>
#include <vector>

#include "library.h"

namespace jarp {

namespace library {

inline void addBerlin(std::vector<Entry> &e) {
   using namespace v;
   const auto rate = [](double r) { return std::pair<clap_id, double>{kRate, r}; };
   const auto swing = [](double s) { return std::pair<clap_id, double>{kSwing, s}; };
   const std::pair<clap_id, double> octRows{kEmptyRows, 2};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto restart = [](double r) { return std::pair<clap_id, double>{kRestart, r}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };
   const auto pressRat = [](double x) { return std::pair<clap_id, double>{kPressRatchet, x}; };
   const auto maxHits = [](double x) { return std::pair<clap_id, double>{kMaxHits, x}; };
   // A sequencer's short gate: half a step.
   constexpr int sh = 2;

   // ---- Pulse: one note or two, the octave doing the work ----
   add(e, [&] { return Entry{"Octave Pulse", "Berlin School: Pulse", "Hold one note: sixteenths leaping an octave in 3+3+2.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1", 16), sh).oct("0 0 1 0 0 1 0 1").vel("14 -6 6 8 -6 6 8 0")}}; });
   add(e, [&] { return Entry{"Steady Pulse", "Berlin School: Pulse", "Hold one note: an even sixteenth pulse, accents on the beat.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1", 16), sh).vel("14 -8 0 -8 6 -8 0 -8")}}; });
   add(e, [&] { return Entry{"Chugging Eighths", "Berlin School: Pulse", "Hold one note: chugging eighths with an octave leap.", {octRows, rate(r8), pressVel(0.2)},
                {Pat(rep("1", 8), 3).oct("0 0 1 0 0 1 0 0").vel("12 -4 4 -4 8 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Three Octave Pulse", "Berlin School: Pulse", "Hold one note: low, home, high and home again, three octaves.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1", 16), sh).oct("-1 0 1 0 -1 0 1 1").vel("14 -6 4 -6 8 -6 4 0")}}; });
   add(e, [&] { return Entry{"Heartbeat", "Berlin School: Pulse", "Hold one note: a double pulse, low then high, with a gap.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 . 1 1 . 1 . 1 1 . 1 1 . 1 1", sh).oct("0 1 . 0 1 . 0 . 0 1 . 0 1 . 0 1").vel("14 -2 . 6 -2 . 8 . 12 -2 . 6 -2 . 4 0")}}; });
   add(e, [&] { return Entry{"Fifth Pulse", "Berlin School: Pulse", "Hold a root and fifth: a pulse leaping between them.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 2 1 1 2 1 2 1 1 2 1 1 2 2 1", sh).oct("0 0 0 1 0 0 1 0 0 0 0 1 0 1 0 1").vel("14 -6 4 8 -6 4 8 -2")}}; });
   add(e, [&] { return Entry{"Pedal Fifth", "Berlin School: Pulse", "Hold a root and fifth: a root pedal, the fifth rocking octaves.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 2", 8), sh).oct("0 0 0 1").vel("12 -4 2 -2")}}; });

   // ---- Sequences: a triad over sixteen steps ----
   add(e, [&] { return Entry{"Octave Line", "Berlin School: Sequences", "Hold a triad: a root pedal under the chord and its octave.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 3 1 1 2 1 2", sh).oct("0 1 0 0 1 0 0 1").vel("14 -6 0 10 -6 0 8 -4")}}; });
   add(e, [&] { return Entry{"Pedal Sequence", "Berlin School: Sequences", "Hold a triad: the root pedal, the chord above it, climbing.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 2 1 3 1 1 1 3 1 2 1 3 1 2 1 1", sh).oct("0 1 0 1 0 2 0 1 0 1 0 1 0 2 0 2").vel("12 -2 -6 0")}}; });
   add(e, [&] { return Entry{"Pedal Bounce", "Berlin School: Sequences", "A root pedal under each held note, up the chord and back.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 2 1 3 1 4 1 5 1 6 1 7 1 8", sh).held(2, -2).bounce().vel("12 -4")}}; });
   add(e, [&] { return Entry{"Dark Ostinato", "Berlin School: Sequences", "Hold a minor triad: the low root under the chord up high.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 3 1 2 1 3 1 1 1 3 1 2 3 2 1", sh).oct("0 1 0 1 1 1 0 1 0 1 0 1 1 1 1 1").vel("14 -6 4 -2 8 -6 4 -2")}}; });
   add(e, [&] { return Entry{"Rising Triad", "Berlin School: Sequences", "Hold a triad: up the chord in threes against the beat.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 2 3", 5) + "1", sh).oct("0 0 0 1 1 1 0 0 0 1 1 1 0 0 0 1").vel("14 -6 0 -6")}}; });
   add(e, [&] { return Entry{"Falling Triad", "Berlin School: Sequences", "Hold a triad: down the chord in threes, an octave apart.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("3 2 1", 5) + "3", sh).oct("1 1 1 0 0 0 1 1 1 0 0 0 1 1 1 1").vel("14 -6 0 -6")}}; });
   add(e, [&] { return Entry{"Wave", "Berlin School: Sequences", "Hold a triad: up three octaves of the chord and back down.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 2 3 1 2 3 1 2 3 2 1 3 2 1 3 2", sh).oct("-1 -1 -1 0 0 0 1 1 1 1 1 0 0 0 -1 -1").vel("14 -6 0 -6")}}; });
   add(e, [&] { return Entry{"Skipping Stone", "Berlin School: Sequences", "Hold a triad: root, fifth and third hopping octaves.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 3 1 2 1 3 2 3", sh).oct("0 0 1 0 1 1 0 1").vel("14 -6 4 -4 10 -6 2 -2")}}; });

   // ---- Gallop: an eighth and two sixteenths, and triplets ----
   add(e, [&] { return Entry{"Octave Gallop", "Berlin School: Gallop", "Hold one note: an eighth and two sixteenths, galloping.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 . 1 1", 4), sh).oct("0 . 1 1 0 . 1 0").vel("14 . -4 2")}}; });
   add(e, [&] { return Entry{"Fifth Gallop", "Berlin School: Gallop", "Hold a root and fifth: the gallop, the fifth leaping.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 . 2 2", 3) + "1 . 2 1", sh).oct("0 . 0 1").vel("14 . -2 4")}}; });
   add(e, [&] { return Entry{"Triad Gallop", "Berlin School: Gallop", "Hold a triad: a gallop up the chord, turning at the octave.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 . 2 3 1 . 3 2 1 . 2 3 1 . 3 1", sh).oct("0 . 0 0 0 . 1 1 0 . 0 1 0 . 1 1").vel("14 . -4 2")}}; });
   add(e, [&] { return Entry{"Reverse Gallop", "Berlin School: Gallop", "Hold a root and fifth: two sixteenths and an eighth.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 2 . 1 1 2 . 1 1 2 . 2 2 1 .", sh).oct("0 1 0 . 0 1 1 . 0 1 0 . 0 1 1 .").vel("10 -4 8 .")}}; });
   add(e, [&] { return Entry{"Triplet Sequence", "Berlin School: Gallop", "Hold a triad: triplets up the chord, octave by octave.", {octRows, rate(r16T), pressVel(0.2)},
                {Pat(rep("1 2 3", 8), sh).oct("0 0 0 1 1 1").vel("12 -6 -2")}}; });
   add(e, [&] { return Entry{"Triplet Pedal", "Berlin School: Gallop", "Hold a root and fifth: the root, then the fifth twice.", {octRows, rate(r16T), pressVel(0.2)},
                {Pat(rep("1 2 2", 8), sh).oct("0 0 1").vel("12 -4 -2")}}; });

   // ---- Phasing: loops a few steps off the bar, so the line turns over it ----
   add(e, [&] { return Entry{"Phase Drift", "Berlin School: Phasing", "Hold a triad: fourteen steps, the accent drifting.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1 2 3 2 1 2 3 2 1 2 3 1 3 2", sh).oct("0 0 0 0 1 0 0 0 0 0 0 1 0 0").vel("12 -6 0 -6 4 -6 0")}}; });
   add(e, [&] { return Entry{"Seven Step", "Berlin School: Phasing", "Hold a root and fifth: a seven-step loop over the bar.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1 1 2 1 2 1 2", sh).oct("0 1 0 0 1 1 0").vel("14 -6 2 6 -4 4 -2")}}; });
   add(e, [&] { return Entry{"Twelve Over Sixteen", "Berlin School: Phasing", "Hold a triad: twelve steps, in line again every 3 bars.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1 1 3 1 2 1 1 3 1 2 3 2", sh).oct("0 1 0 0 1 0 0 1 0 0 0 1").vel("14 -6 2 -4 8 -6 2 -4 6 -6 2 -4")}}; });
   add(e, [&] { return Entry{"Thirteen", "Berlin School: Phasing", "Hold a triad: thirteen steps, a new shape over every bar.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1 2 3 1 2 3 1 3 2 1 3 2 1", sh).oct("0 0 0 1 1 1 0 1 1 1 0 0 0").vel("14 -6 0 6 -6 0 8 -6 0 6 -6 0 2")}}; });
   add(e, [&] { return Entry{"Fifteen", "Berlin School: Phasing", "Hold one note: an octave pulse a step short of the bar.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat(rep("1", 15), sh).oct("0 0 1 0 0 1 0 1 0 0 1 0 1 0 1").vel("14 -6 6 8 -6 6 8 -2 10 -6 6 8 -2 6 0")}}; });
   add(e, [&] { return Entry{"Interlock", "Berlin School: Phasing", "Hold a triad: a low pulse in fours, a figure in threes over it.", {octRows, rate(r16), pressVel(0.2), restart(bar)},
                {Pat("1 2 3 1 1 3 2 3 1 1 3 2 1 3 1 3", sh).oct("0 1 1 2 0 1 1 1 0 2 1 1 0 1 2 1").vel("14 -6 0 4 10 -6 0 4")}}; });

   // ---- Bass: the sequence an octave down ----
   add(e, [&] { return Entry{"Sequencer Bass", "Berlin School: Bass", "Hold a triad: a low, root-heavy line with a run to finish.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 1 1 1 3 1 1 1 1 1 1 2 3 1 2", sh).oct("-1 -1 0 -1 -1 -1 -1 0 -1 -1 0 -1 -1 -1 0 0").vel("14 -8 6 -8")}}; });
   add(e, [&] { return Entry{"Low Pulse", "Berlin School: Bass", "Hold one note: an octave-down pulse with a jump in 3+3+2.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1", 16), sh).oct("-1 -1 0 -1 -1 0 -1 0").vel("14 -8 4 6 -8 4 6 -4")}}; });
   add(e, [&] { return Entry{"Rolling Bass", "Berlin School: Bass", "Hold a root and fifth: a rolling low sixteenth line.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 2 1 1 2 1 2 1 1 2 1 2 1 2 2", sh).oct("-1 -1 -1 0 -1 -1 -1 -1 -1 -1 -1 0 -1 0 -1 0").vel("14 -8 2 6 -8 2 8 -4")}}; });
   add(e, [&] { return Entry{"Descending Bass", "Berlin School: Bass", "Hold a triad: a low root, the chord stepping down above.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 3 1 3 1 2 1 2 1 1 1 1 3 2 1 1", sh).oct("-1 0 -1 0 -1 0 -1 0 -1 -1 -1 -1 0 0 0 -1").vel("14 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Bass Gallop", "Berlin School: Bass", "Hold a root and fifth: a low gallop, the fifth to close.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 . 1 1", 3) + "1 . 2 2", sh).oct("-1 . -1 0").vel("14 . -4 4")}}; });
   add(e, [&] { return Entry{"Eighth Bass", "Berlin School: Bass", "Hold a root and fifth: slow low eighths under a sequence.", {octRows, rate(r8), pressVel(0.2)},
                {Pat("1 1 1 2 1 1 2 1", 3).oct("-1 -1 0 -1 -1 0 -1 0").vel("12 -4 4 0 8 -4 4 -2")}}; });

   // ---- Ratchets: rolls of repeated hits ----
   add(e, [&] { return Entry{"Ratchet Pulse", "Berlin School: Ratchets", "Hold one note: the octave pulse, rolling at the bar's end.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1", 16), sh).oct("0 0 1 0 0 1 0 1").vel("14 -6 6 8 -6 6 8 0").rat("1 1 1 1 1 1 1 1 1 1 1 1 1 2 1 3")}}; });
   add(e, [&] { return Entry{"Ratchet Sequence", "Berlin School: Ratchets", "Hold a triad: the octave line with double and triple hits.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 3 1 1 2 1 2 1 1 3 1 1 2 1 2", sh).oct("0 1 0 0 1 0 0 1").vel("14 -6 0 10 -6 0 8 -4").rat("1 1 1 2 1 1 1 1 1 1 1 2 1 1 3 1")}}; });
   add(e, [&] { return Entry{"Pressure Rolls", "Berlin School: Ratchets", "Hold a root and fifth, and press harder to roll the off-beats.", {octRows, rate(r16), pressVel(0.2), pressRat(0.8), maxHits(4)},
                {Pat(rep("1 2", 8), sh).oct("0 0 0 1").vel("12 -4 2 -2").prs("0 0 0 1")}}; });
   add(e, [&] { return Entry{"Roll Up", "Berlin School: Ratchets", "Hold a triad: two bars of sequence, rolls climbing to the end.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 1 3 1 1 2 1 2", 3) + "1 2 3 1 2 3 1 2", sh).oct("0 1 0 0 1 0 0 1 0 1 0 0 1 0 0 1 0 1 0 0 1 0 0 1 0 0 0 1 1 1 2 2")
                    .vel("14 -6 0 10 -6 0 8 -4")
                    .rat("1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 2 2 2 2 3 3 4 4")}}; });

   // ---- Evolving: sequences that change over many bars ----
   add(e, [&] { return Entry{"Night Drive", "Berlin School: Evolving", "Hold a triad: four sequencer lines, a bar each, looping.", {octRows, rate(r16), advance(loop), pressVel(0.2)},
                {Pat("1 1 3 1 1 2 1 3 1 1 3 1 1 2 1 1", sh).oct("0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1").vel("14 -8 6 -8"),
                 Pat("1 1 3 1 1 2 1 2 1 1 3 1 1 2 1 2", sh).oct("0 1 0 0 1 0 0 1").vel("14 -6 0 10 -6 0 8 -4"),
                 Pat("1 2 1 3 1 1 1 3 1 2 1 3 1 2 1 1", sh).oct("0 1 0 1 0 2 0 1 0 1 0 1 0 2 0 2").vel("12 -2 -6 0"),
                 Pat("1 2 3 1 2 3 1 2 1 3 2 1 3 2 1 2", sh).oct("0 0 0 1 1 1 2 2 2 1 1 1 0 0 0 0").vel("12 -4 0 -4")}}; });
   add(e, [&] { return Entry{"Long Drive", "Berlin School: Evolving", "Hold a triad: four bars that lift an octave in the third.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 3 1 1 2 1 3 1 1 3 1 1 2 1 3 1 1 3 1 1 2 1 2 1 1 3 1 1 2 1 2 "
                     "1 1 3 1 1 2 1 3 1 1 3 1 1 2 1 3 1 2 1 3 1 1 1 2 3 2 1 3 2 3 1 2", sh)
                    .oct("0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 1 0 0 1 0 1 0 0 1 0 0 1 "
                         "1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 0 0 0 0 1 0 1 1 1 1 0 0 0 1 1")
                    .vel("14 -8 6 -8")}}; });
   add(e, [&] { return Entry{"Ghost Steps", "Berlin School: Evolving", "Hold a triad: an ostinato whose off-beat steps come and go.", {octRows, rate(r16), pressVel(0.2)},
                {Pat("1 1 3 1 1 2 1 2 1 1 3 1 2 3 2 1", sh).oct("0 1 0 1 1 0 0 1 0 1 0 1 1 1 1 1").vel("14 -6 4 -2 8 -6 4 -2").chc("100 60 100 75 100 50 90 65")}}; });
   add(e, [&] { return Entry{"Slow Build", "Berlin School: Evolving", "Hold a triad: one note, then two, then all three, building.", {octRows, rate(r16), advance(loop), pressVel(0.2)},
                {Pat(rep("1", 16), sh).oct("0 0 1 0 0 1 0 1").vel("14 -6 6 8 -6 6 8 0"),
                 Pat("1 1 2 1 1 2 1 2 1 1 2 1 1 2 2 1", sh).oct("0 0 0 1 0 0 1 0 0 0 0 1 0 1 0 1").vel("14 -6 4 8 -6 4 8 -2"),
                 Pat("1 1 3 1 1 2 1 2", sh).oct("0 1 0 0 1 0 0 1").vel("14 -6 0 10 -6 0 8 -4"),
                 Pat("1 2 3 1 2 3 1 2 3 2 1 3 2 1 3 2", sh).oct("-1 -1 -1 0 0 0 1 1 1 1 1 0 0 0 -1 -1").vel("14 -6 0 -6")}}; });
   add(e, [&] { return Entry{"Shifting Sequence", "Berlin School: Evolving", "Hold a triad: one sequence, a note changing every bar.", {octRows, rate(r16), advance(loop), pressVel(0.2)},
                {Pat("1 1 3 1 2 1 3 1 1 1 3 1 2 1 3 1", sh).oct("0 1 0 1 1 1 0 1").vel("14 -6 4 -2 8 -6 4 -2"),
                 Pat("1 1 3 1 2 1 3 1 1 1 3 1 2 1 2 1", sh).oct("0 1 0 1 1 1 0 1").vel("14 -6 4 -2 8 -6 4 -2"),
                 Pat("1 1 3 1 2 1 3 1 1 1 2 1 2 1 2 1", sh).oct("0 1 0 1 1 1 0 1").vel("14 -6 4 -2 8 -6 4 -2"),
                 Pat("1 1 3 1 2 1 3 1 1 1 2 1 3 1 3 1", sh).oct("0 1 0 1 1 1 0 1 0 1 1 1 1 1 0 1").vel("14 -6 4 -2 8 -6 4 -2")}}; });
   add(e, [&] { return Entry{"Drift Chain", "Berlin School: Evolving", "Hold a triad: sixteen, twelve and fourteen steps, chained.", {octRows, rate(r16), advance(loop), pressVel(0.2), restart(bar)},
                {Pat("1 1 3 1 1 2 1 2 1 1 3 1 1 2 1 2", sh).oct("0 1 0 0 1 0 0 1").vel("14 -6 0 10 -6 0 8 -4"),
                 Pat("1 1 3 1 2 1 1 3 1 2 3 2", sh).oct("0 1 0 0 1 0 0 1 0 0 0 1").vel("14 -6 2 -4 8 -6 2 -4 6 -6 2 -4"),
                 Pat("1 2 3 2 1 2 3 2 1 2 3 1 3 2", sh).oct("0 0 0 0 1 0 0 0 0 0 0 1 0 0").vel("12 -6 0 -6 4 -6 0")}}; });
   add(e, [&] { return Entry{"Accent Walk", "Berlin School: Evolving", "Hold a triad: accents in fives, walking across five bars.", {octRows, rate(r16), pressVel(0.2)},
                {Pat(rep("1 1 3 1 1 2 1 2", 10), sh).oct("0 1 0 0 1 0 0 1").vel("14 -8 -4 -8 -4")}}; });
}

} // namespace library

} // namespace jarp

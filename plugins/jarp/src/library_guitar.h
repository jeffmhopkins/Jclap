#pragma once

// JArp's fingerpicking presets: guitar (and banjo) picking patterns by style, a few variations each.
//
// All are PITCH rows with Empty Rows OCT, laid out like a guitar's strings. Hold a triad low, say C3
// E3 G3, and the rows are the strings of an open C chord:
//
//    row 1  C3   the thumb's root (5th string)
//    row 2  E3   the thumb's other bass note (4th string)
//    row 3  G3   the first finger's string (3rd); with the Octave lane at -1, the thumb's low G (6th)
//    row 4  C4   the second finger (2nd string)
//    row 5  E4   the third finger (1st string)
//    row 6  G4   a melody note above
//
// So the thumb is rows 1 to 3 and the fingers rows 3 to 6. Hold just a root and fifth and the same
// rows play an open fifth voicing: every pattern needs only two or three notes held, and none skips a
// row, so every note held plays.

#include <vector>

#include "library.h"

namespace jarp {

namespace library {

inline void addGuitar(std::vector<Entry> &e) {
   using namespace v;
   const auto rate = [](double r) { return std::pair<clap_id, double>{kRate, r}; };
   const auto swing = [](double s) { return std::pair<clap_id, double>{kSwing, s}; };
   const std::pair<clap_id, double> octRows{kEmptyRows, 2};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };

   // ---- Fingerpicking: Travis. The thumb alternates on the beats (root, 3rd, low 5th, 3rd), the
   // fingers pick between. ----
   add(e, [&] { return Entry{"Travis Picking", "Fingerpicking: Travis", "An alternating thumb on the low notes, fingers on the top.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1+4 3 2 4 1 3 2 4", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4 6 -6 4 -4")}}; });
   add(e, [&] { return Entry{"Travis Pinch", "Fingerpicking: Travis", "Thumb root, 3rd, low 5th, 3rd; a pinch on one. Hold a triad.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1+5 4 2 3 3 5 2 4", 8).oct(". . . . -1 . . .").vel("12 -6 4 -8 8 -4 4 -6")}}; });
   add(e, [&] { return Entry{"Travis Syncopated", "Fingerpicking: Travis", "Travis with the top notes pushed ahead of the beat, two bars.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1+5 4 2 6 3 5 2 4 1+6 4 2 5 3 4 2+5 _", 8)
                    .oct(". . . . -1 . . . . . . . -1 . . .")
                    .vel("12 -6 4 6 8 -4 4 -6 12 -6 4 6 8 -6 6 .")}}; });
   add(e, [&] { return Entry{"Travis Half Time", "Fingerpicking: Travis", "The thumb on the beats, fingers in sixteenths around it.", {octRows, rate(r16), swing(0.55), pressVel(0.4)},
                {Pat("1+5 . 4 3 2 . 5 4 3 . 4 5 2 . 4 3", 10)
                    .oct(". . . . . . . . -1 . . . . . . .")
                    .vel("12 . -6 -8 8 . -2 -6 10 . -6 -2 8 . -6 -8")}}; });
   add(e, [&] { return Entry{"Fingerstyle Song", "Fingerpicking: Travis", "Four picking patterns, a bar each, looping.", {octRows, rate(r8), swing(0.54), advance(loop), pressVel(0.4)},
                {Pat("1+4 3 2 4 1 3 2 4", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1+4 3 2 4 1 3 2+5 4", 8).oct(". . -1 . . . . .").vel("10 -6 4 -4"),
                 Pat("1+4 3 2 4 1 3 2 5", 8).oct(". . -1 . . . -1 .").vel("10 -6 4 -4"),
                 Pat("1 2 3 4 5 4 3 2", 6).vel("10 -6 -2 6 -2 -6 4 0")}}; });

   // ---- Fingerpicking: Folk ----
   add(e, [&] { return Entry{"Thumb Brush", "Fingerpicking: Folk", "Boom-chick: the thumb's bass, then a finger brush. Hold a triad.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("1 4+5+6s 2 4+5+6s 3 4+5+6s 2 4+5+6s", 8)
                    .oct(". . . . -1 . . .")
                    .vel("12 -8 6 -6 10 -8 6 -6")}}; });
   add(e, [&] { return Entry{"Folk Pinch", "Fingerpicking: Folk", "A pinch, then the fingers rock in between. Hold a triad.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1+5 3 4 3 2+5 3 4 3", 8).vel("12 -6 0 -8 10 -6 0 -8")}}; });
   add(e, [&] { return Entry{"Six Eight Folk", "Fingerpicking: Folk", "Up and back over the strings in 6/8, two bars.", {octRows, rate(r8), pressVel(0.5)},
                {Pat("1 3 4 5 4 3 2 3 4 5 4 3", 12).vel("12 -6 -4 6 -4 -6")}}; });
   add(e, [&] { return Entry{"Waltz Picking", "Fingerpicking: Folk", "3/4: a bass note, then two finger pinches, two bars.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1 . 3+4+5s . 3+4+5s . 2 . 3+4+5s . 3+4+5s .", 8).vel("12 . -6 . -8 . 10 . -6 . -8 .")}}; });

   // ---- Fingerpicking: Ballad ----
   add(e, [&] { return Entry{"Ballad Picking", "Fingerpicking: Ballad", "Thumb on the low two notes, fingers rising through the rest.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 3 4 5 2 3 4 5", 8).vel("10 -6 -2 2 6 -6 -2 2")}}; });
   add(e, [&] { return Entry{"Pop Ballad", "Fingerpicking: Ballad", "Thumb and pinch, then the fingers rolling between. Hold a triad.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1+5 . 3 4 . 3 5 4 2+5 . 3 4 . 3 5 4", 12).vel("12 . -6 -2 . -6 2 -4 10 . -6 -2 . -6 2 -4")}}; });
   add(e, [&] { return Entry{"Ballad Sixteenths", "Fingerpicking: Ballad", "Up the strings and back in sixteenths, the thumb alternating.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 3 4 5 6 5 4 3 2 3 4 5 6 5 4 3", 10).vel("12 -6 -2 0 6 0 -2 -6 10 -6 -2 0 6 0 -2 -6")}}; });
   add(e, [&] { return Entry{"Slow Arpeggio", "Fingerpicking: Ballad", "Eighths left to ring, the top moving up. Hold a triad.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1 3 4 5 2 4 5 6", 24).vel("10 -6 -2 4 8 -6 -2 6")}}; });

   // ---- Fingerpicking: Classical. p i m a: the thumb, then the first three fingers. ----
   add(e, [&] { return Entry{"Classical Arpeggio", "Fingerpicking: Classical", "Thumb then three fingers, p i m a, up and back.", {octRows, rate(r8T), pressVel(0.6)},
                {Pat("1 2 3 4 3 2", 10).vel("10 -6 -2 6 -2 -6")}}; });
   add(e, [&] { return Entry{"P I M A", "Fingerpicking: Classical", "p i m a in sixteenths, the thumb walking the bass. Hold a triad.", {octRows, rate(r16), pressVel(0.6)},
                {Pat("1 3 4 5 2 3 4 5 3 3 4 5 2 3 4 5", 12)
                    .oct(". . . . . . . . -1 . . . . . . .")
                    .vel("12 -6 -2 4 8 -6 -2 4 10 -6 -2 4 8 -6 -2 4")}}; });
   add(e, [&] { return Entry{"Giuliani Arpeggio", "Fingerpicking: Classical", "p i m a m i in sextuplets, a study-style flow. Hold a triad.", {octRows, rate(r16T), pressVel(0.6)},
                {Pat(rep("1 3 4 5 4 3 2 3 4 5 4 3", 2), 8).vel("10 -6 -2 4 -2 -6")}}; });
   add(e, [&] { return Entry{"Tremolo", "Fingerpicking: Classical", "p a m i: a bass note, then the top string three times.", {octRows, rate(r16), pressVel(0.6)},
                {Pat("1 5 5 5 3 5 5 5 2 4 4 4 3 4 4 4", 3)
                    .oct(". . . . -1 . . . . . . . -1 . . .")
                    .vel("8 2 -4 -2")}}; });
   add(e, [&] { return Entry{"Romance Triplets", "Fingerpicking: Classical", "3/4 in triplets, a melody on top of i m a. Hold a triad.", {octRows, rate(r8T), pressVel(0.6)},
                {Pat("1+6 4 5 6 4 5 6 4 5 2+5 3 4 5 3 4 5 3 4", 8).vel("12 -8 -6 4 -8 -6 4 -8 -6")}}; });

   // ---- Fingerpicking: Banjo. Rolls of eight sixteenths. Hold a triad: row 4, an octave over the
   // root, stands in for the banjo's high drone string. ----
   add(e, [&] { return Entry{"Banjo Roll", "Fingerpicking: Banjo", "A bright forward roll, in 3+3+2 sixteenths.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("1s 2s 3s 1s 2s 3s 1s 4s").vel("12 -6 -2 10 -6 -2 8 -4")}}; });
   add(e, [&] { return Entry{"Backward Roll", "Fingerpicking: Banjo", "Middle, index, thumb, from the top string down, two bars.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("3s 2s 1s 3s 2s 1s 3s 2s 4s 2s 1s 4s 2s 1s 3s 1s").vel("12 -6 -2 10 -6 -2 8 -4")}}; });
   add(e, [&] { return Entry{"Forward Reverse Roll", "Fingerpicking: Banjo", "Up the strings to the drone and back down again.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("1s 2s 3s 4s 3s 2s 1s 3s").vel("12 -6 -2 8 -6 -2 10 -4")}}; });
   add(e, [&] { return Entry{"Alternating Thumb Roll", "Fingerpicking: Banjo", "Thumb, index, thumb, middle, the thumb swapping strings.", {octRows, rate(r16), pressVel(0.3)},
                {Pat("1s 2s 4s 3s 3s 2s 4s 3s").oct(". . . . -1 . . .").vel("12 -6 4 -4 10 -6 4 -4")}}; });
   add(e, [&] { return Entry{"Clawhammer", "Fingerpicking: Banjo", "Bum-ditty: a melody note, a brush down, the thumb's drone.", {octRows, rate(r8), swing(0.54), pressVel(0.4)},
                {Pat("3 . 1+2+3s 4s 2 . 1+2+3s 4s", 6).vel("12 . -4 -8 10 . -4 -8")}}; });

   // ---- Fingerpicking: Blues ----
   add(e, [&] { return Entry{"Monotonic Thumb", "Fingerpicking: Blues", "A steady muted thumb on the root, the fingers riffing on top.", {octRows, rate(r8), swing(0.62), pressVel(0.4)},
                {Pat("1s+5 4 1s 3+4 1s 5 1s+4 . 1s+5 4 1s 3 1s+2 3 1s 2", 6)
                    .vel("12 -6 4 0 6 -4 8 . 12 -6 4 -4 8 -6 4 -6")}}; });
   add(e, [&] { return Entry{"Delta Shuffle", "Fingerpicking: Blues", "Shuffle triplets: thumb on the beat, a double stop after.", {octRows, rate(r8T), pressVel(0.4)},
                {Pat("1s . 4+5s 1s . 3+4s 1s . 4+5s 1s . 2+3s").vel("12 . -4 8 . -4 10 . -4 8 . -2")}}; });
   add(e, [&] { return Entry{"Piedmont Blues", "Fingerpicking: Blues", "Ragtime alternating bass under a syncopated top, two bars.", {octRows, rate(r8), swing(0.58), pressVel(0.4)},
                {Pat("1+6 . 2 5 3 4+6 2 4 1+5 4 2 6 3 5 2+4 .", 8)
                    .oct(". . . . -1 . . . . . . . -1 . . .")
                    .vel("12 . 4 2 8 6 4 -4 12 -6 4 6 8 -2 6 .")}}; });

   // ---- Fingerpicking: Bossa. The thumb on the root and low fifth, the fingers on the chord in the
   // bossa rhythm. Hold a triad, or a 7th chord for the real sound. ----
   add(e, [&] { return Entry{"Bossa Picking", "Fingerpicking: Bossa", "Root and low fifth under the chord's bossa rhythm.", {octRows, rate(r16), pressVel(0.4)},
                {Pat("1+2+3+4s . . 2+3+4s . . 2+3+4s . 3 . 2+3+4s . . 2+3+4s . .", 8)
                    .oct(". . . . . . . . -1 . . . . . . .")
                    .vel("10 . . -6 . . -2 . 6 . -4 . . -2 . .")}}; });
   add(e, [&] { return Entry{"Bossa Two Bar", "Fingerpicking: Bossa", "The bossa rhythm, answered by a second bar that turns it round.", {octRows, rate(r16), pressVel(0.4)},
                {Pat("1+2+3+4s . . 2+3+4s . . 2+3+4s . 3 . 2+3+4s . . 2+3+4s . . "
                     "1 . 2+3+4s . . 2+3+4s . . 3 2+3+4s . . 2+3+4s . 2+3+4s .", 8)
                    .oct(". . . . . . . . -1 . . . . . . . . . . . . . . . -1 . . . . . . .")
                    .vel("10 . . -6 . . -2 . 6 . -4 . . -2 . . 8 . -4 . . -2 . . 6 -4 . . -2 . -6 .")}}; });
   add(e, [&] { return Entry{"Bossa Arpeggio", "Fingerpicking: Bossa", "The chord picked note by note over the root and low fifth.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1 . 2 3 4 . 2 3 3 . 2 3 4 . 2 .", 8)
                    .oct(". . . . . . . . -1 . . . . . . .")
                    .vel("10 . -4 -2 4 . -6 -2 8 . -4 -2 4 . -6 .")}}; });

   // ---- Fingerpicking: Celtic. 6/8 and 3/4, the strings left to ring. ----
   add(e, [&] { return Entry{"Celtic Jig", "Fingerpicking: Celtic", "A 6/8 jig across the strings, two bars. Hold a triad.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1 4 3 5 3 4 2 4 3 5 3 4", 12).vel("12 -6 -4 8 -4 -6")}}; });
   add(e, [&] { return Entry{"Celtic Cascade", "Fingerpicking: Celtic", "Low and high strings in turn, ringing over each other.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1 4 2 5 3 6 4 5 3 4 2 3", 20).vel("12 -6 -2 6 -4 4")}}; });
   add(e, [&] { return Entry{"Celtic Air", "Fingerpicking: Celtic", "A slow 3/4, every string left to ring, harp-like.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1 3 5 4 6 5 2 3 5 4 6 5", 24).vel("12 -6 -2 -4 4 -6")}}; });
   add(e, [&] { return Entry{"Drone Picking", "Fingerpicking: Celtic", "The root as a drone on each beat of 6/8, a tune around it.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1 3 4 1 5 4 1 3 2 1 4 3", 12).vel("12 -6 -2 8 2 -4")}}; });
}

} // namespace library

} // namespace jarp

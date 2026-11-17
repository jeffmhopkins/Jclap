#pragma once

// JArp's piano presets: accompaniment the way a pianist plays it under a song, by style. Included at
// the end of library.h, whose build() calls addPiano() where the piano presets go.
//
// Every piano pattern is built for a plain triad held in the middle of the keyboard: row 1 is the
// root, row 2 the third, row 3 the fifth, and the left hand's bass comes from those same keys an
// octave or two down (a note's own octave, written 1v or 3vv). So none asks for more keys than the
// notes it plays: a pattern that only plays the root (Murky Bass) needs only the root, a root-and-
// fifth one only those two. The Jazz and Gospel and Soul presets add a fourth row for the 7th: hold
// a 7th chord, or just the triad and the root an octave up takes its place (Empty Rows OCT). With
// OCT rows everywhere, holding fewer notes than a pattern's rows never leaves a gap: the missing rows
// are the held notes again an octave up. As in the rest of the library, no pattern skips a row.

#include <vector>

#include "library.h"

namespace jarp {

namespace library {

inline void addPiano(std::vector<Entry> &e) {
   using namespace v;
   const auto rate = [](double r) { return std::pair<clap_id, double>{kRate, r}; };
   const auto swing = [](double s) { return std::pair<clap_id, double>{kSwing, s}; };
   const std::pair<clap_id, double> octRows{kEmptyRows, 2};
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   const auto pressVel = [](double x) { return std::pair<clap_id, double>{kPressVelocity, x}; };

   // ---- Piano: Classical. Broken chords for the left hand, close and spread. ----
   add(e, [&] { return Entry{"Alberti Bass", "Piano: Classical", "Low, high, middle, high: the classical left hand.", {octRows, rate(r16)},
                {Pat("1 3 2 3 1 3 2 3").vel("10 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Alberti Eighths", "Piano: Classical", "Alberti bass in eighths, for a slower song.", {octRows, rate(r8), pressVel(0.5)},
                {Pat("1 3 2 3 1 3 2 3", 5).vel("8 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Alberti Wide", "Piano: Classical", "Alberti spread out: root and fifth an octave down round the 3rd.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1v 3v 2 3v 1v 3v 2 3v", 6).vel("10 -8 -2 -8")}}; });
   add(e, [&] { return Entry{"Murky Bass", "Piano: Classical", "Broken octaves on the root, a rumbling bass. Hold one note.", {octRows, rate(r16), pressVel(0.4)},
                {Pat("1v 1 1v 1 1v 1 1v 1").vel("10 -6 2 -6")}}; });
   add(e, [&] { return Entry{"Wide Arpeggio", "Piano: Classical", "Root and fifth low, up the chord and back: a flowing left hand.", {octRows, rate(r16), pressVel(0.6)},
                {Pat("1v 3v 1 2 3 2 1 3v", 8).vel("10 -6 -2 2 6 2 -2 -6")}}; });
   add(e, [&] { return Entry{"Triplet Arpeggio", "Piano: Classical", "Fifth, root, third in slow triplets over the low root: hushed.", {octRows, rate(r8T), pressVel(0.7)},
                {Pat("1v+3v 1 2 3v 1 2 3v 1 2 3v 1 2", 6).vel("10 -6 -4 2 -6 -4 4 -6 -4 2 -6 -4")}}; });
   add(e, [&] { return Entry{"Nocturne", "Piano: Classical", "Triplets sweeping up the chord and back, a romantic left hand.", {octRows, rate(r8T), pressVel(0.7)},
                {Pat("1v 3v 1 2 3 1^ 3 2 1 3v 1 2", 10).vel("10 -6 -2 4 6 2 -2 -6")}}; });

   // ---- Piano: Waltz, in 3/4: the bass on one, the chord on two and three. ----
   add(e, [&] { return Entry{"Waltz", "Piano: Waltz", "Oom-pah-pah: the root low, then the chord twice.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1v:8 . 2+3s . 2+3s .").vel("10 . -4 . -6 .")}}; });
   add(e, [&] { return Entry{"Alternating Waltz", "Piano: Waltz", "Oom-pah-pah, the bass going root then fifth below, bar by bar.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1v:8 . 2+3s . 2+3s . 3vv:8 . 1+2s . 1+2s .").vel("10 . -4 . -6 .")}}; });
   add(e, [&] { return Entry{"Waltz Spread", "Piano: Waltz", "Up and back down the chord in a bar of 3/4.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1v 3v 2 3 1^ 3", 8).vel("10 -4 0 4 0 -4")}}; });
   add(e, [&] { return Entry{"Waltz Arpeggio", "Piano: Waltz", "Two flowing bars of 3/4, the bass root then fifth.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1v 3v 2 3 1^ 3 3vv 3v 2 3 1^ 3", 8).vel("10 -4 0 4 2 -4")}}; });

   // ---- Piano: Pop. The chord on the beat, the bass under it. ----
   add(e, [&] { return Entry{"Pop Eighths", "Piano: Pop", "Steady chord eighths, the root low on each beat.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1v+2+3 1+2+3s 1v+2+3 1+2+3s 1v+2+3 1+2+3s 1v+2+3 1+2+3s", 3).vel("12 -8 0 -8")}}; });
   add(e, [&] { return Entry{"Pop Pushes", "Piano: Pop", "The chord on one, the and of two and four: 3+3+2.", {octRows, rate(r8), pressVel(0.5)},
                {Pat("1v+2+3 _ . 1+2+3 _ . 1+2+3 _").vel("12 . . 4 . . 6 .")}}; });
   add(e, [&] { return Entry{"Pop Sixteenths", "Piano: Pop", "A busy sixteenth pulse, root low on one and three.", {octRows, rate(r16), pressVel(0.4)},
                {Pat("1v+2+3 . 1+2+3s 1+2+3s . 1+2+3s . 1+2+3s 3vv+1+2 . 1+2+3s 1+2+3s . 1+2+3s . 1+2+3s")
                    .vel("12 . -6 -2 . -6 . 2 10 . -6 -2 . -6 . 4")}}; });
   add(e, [&] { return Entry{"Rock Pound", "Piano: Pop", "The chord hammered in eighths, the root low on each beat.", {octRows, rate(r8), pressVel(0.3)},
                {Pat("1v+2+3 1+2+3 1v+2+3 1+2+3 1v+2+3 1+2+3 1v+2+3 1+2+3", 3).vel("14 -4 6 -4 10 -4 6 -2")}}; });
   add(e, [&] { return Entry{"Piano Verse", "Piano: Pop", "Alberti, a rolled chord and chord eighths, one per chord.", {octRows, rate(r16), advance(chord), pressVel(0.5)},
                {Pat("1 3 2 3 1 3 2 3 1 3 2 3 1 3 2 3").vel("10 -8 -2 -8"),
                 Pat("1v 3v 1 2 3 . . . . . . . . . . .", 60).vel("8 2 0 2 6"),
                 Pat("1v+2+3 . 1+2+3s . 1v+2+3 . 1+2+3s . 1v+2+3 . 1+2+3s . 1v+2+3 . 1+2+3s .").vel("12 . -8 . 0 . -8 .")}}; });

   // ---- Piano: Ballad. Slow, legato, the notes left to ring. ----
   add(e, [&] { return Entry{"Rolled Ballad", "Piano: Ballad", "A slow roll up the chord from the bass, left to ring.", {octRows, rate(r16), pressVel(0.7)},
                {Pat("1v 3v 1 2 3 . . . . . . . . . . .", 60).vel("8 2 0 2 6")}}; });
   add(e, [&] { return Entry{"Ballad Arpeggio", "Piano: Ballad", "Eighths up from the low root to the top and back, legato.", {octRows, rate(r8), pressVel(0.7)},
                {Pat("1v 3v 2 3 1^ 3 2 3v", 8).vel("10 -4 0 2 6 2 0 -4")}}; });
   add(e, [&] { return Entry{"Ballad Sixteenths", "Piano: Ballad", "A flowing sixteenth figure over the root, a bar long.", {octRows, rate(r16), pressVel(0.7)},
                {Pat("1v 3v 1 2 3 2 1 3v 1v 3v 1 2 3 1^ 3 2", 8).vel("10 -6 -2 0 4 0 -2 -6 6 -6 -2 0 4 8 2 -2")}}; });
   add(e, [&] { return Entry{"Ballad Pulse", "Piano: Ballad", "The root held low through the bar, the chord on every beat.", {octRows, rate(r8), pressVel(0.6)},
                {Pat("1v!+2l+3l . 2l+3l . 2l+3l . 2l+3l .").vel("8 . -4 . 0 . -4 .")}}; });

   // ---- Piano: Stride and Ragtime. A low bass and a middle chord, in turn. ----
   add(e, [&] { return Entry{"Ragtime", "Piano: Stride and Ragtime", "Root low, chord, fifth lower still, chord: the ragtime left hand.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1v 1+2+3s 3vv 1+2+3s 1v 1+2+3s 3vv 1+2+3s", 3).vel("10 -6 8 -6")}}; });
   add(e, [&] { return Entry{"Stride", "Piano: Stride and Ragtime", "Bass note, chord, bass note, chord, swung.", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1v 1+2+3s 3vv 1+2+3s 1v 1+2+3s 3vv 1+2+3s", 3).vel("10 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Stride Walk", "Piano: Stride and Ragtime", "Two bars of stride, walking up the chord into the next.", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1v 1+2+3s 3vv 1+2+3s 1v 1+2+3s 3vv 1+2+3s 1v 1+2+3s 3vv 1+2+3s 1v 2v 3v 1+2+3s", 3).vel("10 -6 6 -6")}}; });
   add(e, [&] { return Entry{"Stride Tenths", "Piano: Stride and Ragtime", "Stride with the bass in tenths, root under the 3rd above.", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1v+2 1+2+3s 3vv+1 1+2+3s 1v+2 1+2+3s 3vv+1 1+2+3s", 3).vel("10 -6 6 -6")}}; });

   // ---- Piano: Blues and Boogie. Shuffled, eighths in a long-short swing. ----
   add(e, [&] { return Entry{"Shuffle Octaves", "Piano: Blues and Boogie", "A shuffled octave bass on the root. Hold one note.", {octRows, rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1v 1 1v 1 1v 1 1v 1", 3).vel("12 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Rocking Boogie", "Piano: Blues and Boogie", "Root and fifth low, rocking up to the chord tones, shuffled.", {octRows, rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1v 3v 1 3v 1v 3v 2 3v", 3).vel("12 -4 6 -4 10 -4 6 -2")}}; });
   add(e, [&] { return Entry{"Triad Boogie", "Piano: Blues and Boogie", "A shuffled boogie bass up and down the triad, an octave down.", {octRows, rate(r8), swing(0.66), pressVel(0.3)},
                {Pat("1v 2v 3v 1 3v 2v 1v 3vv", 3).vel("12 -4 6 -2 8 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Shuffle Comp", "Piano: Blues and Boogie", "Root and fifth low on one and three, chord stabs on two and four.", {octRows, rate(r8), swing(0.66), pressVel(0.4)},
                {Pat("1v 3v 1+2+3s . 1v 3v 1+2+3s 1+2+3s").vel("10 -4 6 . 8 -4 6 -2")}}; });
   add(e, [&] { return Entry{"Slow Blues Triplets", "Piano: Blues and Boogie", "The chord in triplets, root low on every beat, a slow 12/8 blues.", {octRows, rate(r8T), pressVel(0.5)},
                {Pat("1v+2+3 1+2+3 1+2+3 1v+2+3 1+2+3 1+2+3 1v+2+3 1+2+3 1+2+3 1v+2+3 1+2+3 1+2+3", 3)
                    .vel("12 -8 -4 6 -8 -4 8 -8 -4 6 -8 -2")}}; });

   // ---- Piano: Gospel and Soul. Row 4 is the 7th: hold a 7th chord, or a triad for its octave. ----
   add(e, [&] { return Entry{"Gospel Comp", "Piano: Gospel and Soul", "Swung sixteenth chord stabs over a low root. Hold a 7th chord.", {octRows, rate(r16), swing(0.58), pressVel(0.5)},
                {Pat("1v . . 2+3+4s . . 2+3+4 . 1v . 2+3+4s . 3vv 2+3+4s . 2+3+4").vel("12 . . 0 . . 6 . 8 . -2 . 6 0 . 4")}}; });
   add(e, [&] { return Entry{"Gospel Shout", "Piano: Gospel and Soul", "Octave-jumping bass under driving chords, a church shout.", {octRows, rate(r8), swing(0.6), pressVel(0.4)},
                {Pat("1v 2+3+4s 1vv 2+3+4s 1v 2+3+4s 1vv 2+3+4s", 3).vel("12 -2 8 -2 10 -2 8 2")}}; });
   add(e, [&] { return Entry{"Soul Ballad", "Piano: Gospel and Soul", "A 12/8 ballad: each beat up the chord from the low root.", {octRows, rate(r8T), pressVel(0.7)},
                {Pat("1v 2 3 4 3 2 1v 2 3 4 3 2", 6).vel("10 -6 -2 4 -2 -6 8 -6 -2 4 -2 -6")}}; });
   add(e, [&] { return Entry{"Neo Soul Comp", "Piano: Gospel and Soul", "Laid-back 7th chord pushes, some left to chance. Hold a 7th.", {octRows, rate(r16), swing(0.6), pressVel(0.6)},
                {Pat("2+3+4 _ . 1v . . 2+3+4s . . 2+3+4 _ . 1v . 2+3+4s .")
                    .vel("8 . . 10 . . -4 . . 4 . . 6 . -6 .")
                    .chc("100 . . 100 . . 70 . . 100 . . 100 . 60 .")}}; });

   // ---- Piano: Latin. Syncopated, against a clave. ----
   add(e, [&] { return Entry{"Montuno", "Piano: Latin", "A two-bar salsa montuno, broken chord on the off-beats.", {octRows, rate(r8), pressVel(0.4)},
                {Pat("1+3 . 2 1+3 . 2 1+3 . . 2 1+3 . 2 1+3 . 2", 3).vel("10 . -4 6 . -4 8 . . -4 8 . -4 6 . -2")}}; });
   add(e, [&] { return Entry{"Bossa Comp", "Piano: Latin", "Bossa nova: root then fifth low, the chord on the syncopations.", {octRows, rate(r8), pressVel(0.5)},
                {Pat("1v+2+3 . . 2+3 . . 2+3 . 3vv . 2+3 . . 2+3 . .", 3).vel("8 . . 2 . . 4 . 6 . 2 . . 4 . .")}}; });
   add(e, [&] { return Entry{"Habanera", "Piano: Latin", "The habanera bass, root and fifth, the chord on the and of two.", {octRows, rate(r16), pressVel(0.5)},
                {Pat("1v _ _ 3vv 1v+2+3 _ 2+3 _").vel("12 . . -4 6 . 2 .")}}; });

   // ---- Piano: Jazz. Swung comping, row 4 the 7th as above. ----
   add(e, [&] { return Entry{"Charleston Comp", "Piano: Jazz", "The chord on one and the and of two, swung. Hold a 7th chord.", {octRows, rate(r8), swing(0.62), pressVel(0.5)},
                {Pat("1v+2+3+4 . . 2+3+4s . . . .").vel("10 . . 4 . . . .")}}; });
   add(e, [&] { return Entry{"Jazz Comp", "Piano: Jazz", "Two bars of swung comping, the pushes left partly to chance.", {octRows, rate(r8), swing(0.62), pressVel(0.5)},
                {Pat("1v+2+3+4 . . 2+3+4s . 2+3+4s . . 3vv . 2+3+4 _ . . 2+3+4s .")
                    .vel("10 . . 2 . 4 . . 8 . 6 . . . 2 .")
                    .chc("100 . . 100 . 70 . . 100 . 100 . . . 60 .")}}; });
   add(e, [&] { return Entry{"Jazz Waltz", "Piano: Jazz", "A swung 3/4: the root, then the chord pushed onto the and of one.", {octRows, rate(r8), swing(0.62), pressVel(0.5)},
                {Pat("1v . 2+3+4 _ . 2+3+4s 3vv . 2+3+4 _ . 2+3+4s").vel("10 . 4 . . -2 8 . 4 . . -2")}}; });
}

} // namespace library

} // namespace jarp

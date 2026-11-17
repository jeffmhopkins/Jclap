#pragma once

// JArp's bass grooves: bass lines by genre, a few variations of each, in categories "Bass: <genre>".
//
// Every one is played from two held notes, or three where the line really walks a chord, never a big
// voicing: a bass line plays a few notes, so it asks for only those. Two notes means a root and fifth
// (Funk asks for the root and b7), three means a triad (Blues' shuffle asks for the root, 5th and 6th).
// Each blurb says which. Rows run on from row 1 with none skipped, and nothing reaches past row 3.
//
// The octaves come from the Octave lane, not from more held notes: a line plays an octave down, a
// row written with ^ plays an octave up from there (the octave pop), and with v an octave further
// down (the fifth below the root). Rows above the notes held go up an octave (Empty Rows OCT), so a
// two-note line still plays from one held note, and a three-note line from two.

#include <string>
#include <vector>

#include "library.h"

namespace jarp {

namespace library {

// A bass line: Pat's step line, a row ending in ^ an octave up and in v an octave down from `base`
// (an octave down), with the octaves written into the Octave lane.
inline Pat bass(const std::string &steps, int base = -1) {
   std::string line, octaves;
   for (std::string t : tokens(steps)) {
      int o = base;
      while (t.size() > 1 && (t.back() == '^' || t.back() == 'v')) {
         o += t.back() == '^' ? 1 : -1;
         t.pop_back();
      }
      line += t + " ";
      octaves += std::to_string(o) + " ";
   }
   Pat p(line);
   p.oct(octaves);
   return p;
}

inline void addBass(std::vector<Entry> &e) {
   using namespace v;
   const auto rate = [](double r) { return std::pair<clap_id, double>{kRate, r}; };
   const auto swing = [](double s) { return std::pair<clap_id, double>{kSwing, s}; };
   const auto restart = [](double r) { return std::pair<clap_id, double>{kRestart, r}; };
   const auto advance = [](double a) { return std::pair<clap_id, double>{kAdvance, a}; };
   // Every groove: OCT rows, two octaves of room for the fifth below, pressure for velocity.
   const std::pair<clap_id, double> octRows{kEmptyRows, 2}, range2{kRange, 1}, press{kPressVelocity, 0.3};
   using Params = std::vector<std::pair<clap_id, double>>;
   const auto with = [&](Params p) {
      p.insert(p.begin(), {octRows, range2, press});
      return p;
   };

   // ---- Rock: straight eighths on the root, the fifth and octave to turn the bar ----
   add(e, [&] { return Entry{"Root Fifth Octave", "Bass: Rock", "Hold root and fifth: root, fifth and octave in rock eighths.", with({rate(r8)}),
                {bass("1 2 1^ 2 1 2 1^ 2").vel("12 -4 6 -4")}}; });
   add(e, [&] { return Entry{"Rock Eighths", "Bass: Rock", "Hold root and fifth: root eighths with a turn into each bar.", with({rate(r8)}),
                {bass("1 1 1 1 1 1 2 1^ 1 1 1 1 1 1 1^ 2").vel("12 0 4 0")}}; });
   add(e, [&] { return Entry{"Dotted Drive", "Bass: Rock", "Hold root and fifth: rock in 3+3+2, fifth and octave to turn.", with({rate(r8)}),
                {bass("1 _ _ 1 _ _ 1 _ 1 _ _ 1 _ _ 2 1^").vel("12 . . 6 . . 8 . 12 . . 6 . . 4 8")}}; });
   add(e, [&] { return Entry{"Gallop", "Bass: Rock", "Hold root and fifth: a triplet gallop on the root, a jump to end.", with({rate(r16T), restart(bar)}),
                {bass("1 1s 1s 1 1s 1s 1 1s 1s 2 2s 1s^").vel("16 -8 -8 10 -8 -8 14 -8 -8 10 -4 8")}}; });

   // ---- Metal and Punk: down-picked eighths, palm-muted chugs and gallops ----
   add(e, [&] { return Entry{"Punk Eighths", "Bass: Metal and Punk", "Hold root and fifth: down-picked root eighths, fifth to finish.", with({rate(r8)}),
                {bass(rep("1:3", 12) + "2:3 2:3 2:3 2:3").vel("10 2 6 2")}}; });
   add(e, [&] { return Entry{"Metal Chug", "Bass: Metal and Punk", "Hold root and fifth: palm-muted chugs in syncopated sixteenths.", with({rate(r16)}),
                {bass("1t 1t . 1t 1t . 1t . 1t 1t . 1t 1s 1s 2 .").vel("14 -4 . 8 -4 . 10 . 14 -4 . 8 6 6 10 .")}}; });
   add(e, [&] { return Entry{"Reverse Gallop", "Bass: Metal and Punk", "Hold root and fifth: two sixteenths and an eighth, driving on.", with({rate(r16)}),
                {bass(rep("1s 1s 1 _", 3) + "2s 2s 1^ _").vel("12 -4 8 .")}}; });
   add(e, [&] { return Entry{"Thrash Pick", "Bass: Metal and Punk", "Hold root and fifth: nonstop sixteenths, accented in threes.", with({rate(r16)}),
                {bass(rep("1s", 12) + "2s 2s 1s^ 1s").vel("14 -8 -4 10 -8 -4 12 -8 -4 10 -8 -4 12 -2 8 -4")}}; });

   // ---- Blues: the shuffle, swung eighths ----
   add(e, [&] { return Entry{"Boogie Shuffle", "Bass: Blues", "Hold root, 5th and 6th: the rock-and-roll 1-5-6-5 shuffle.", with({rate(r8), swing(0.66)}),
                {bass("1 1 2 2 3 3 2 2").vel("10 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Boogie Walk", "Bass: Blues", "Hold a triad: up root, third, fifth, octave and back down.", with({rate(r8), swing(0.66)}),
                {bass("1 2 3 1^ 1^ 3 2 1").vel("10 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Texas Shuffle", "Bass: Blues", "Hold root and fifth: a shuffle rocking from root to octave.", with({rate(r8), swing(0.66)}),
                {bass(rep("1 1^", 6) + "2 2^ 2 1^").vel("10 -2")}}; });
   add(e, [&] { return Entry{"Slow Blues", "Bass: Blues", "Hold root and fifth: a slow 12/8 blues, the triplets rolling on.", with({rate(r8T), restart(bar)}),
                {bass("1 _ 1 2 _ 2 1^ _ 1^ 2 _ 1").vel("10 . -6 6 . -6 8 . -6 6 . -4")}}; });

   // ---- Country: root and the fifth below, two to the bar, with walk-ups ----
   add(e, [&] { return Entry{"Two Beat", "Bass: Country", "Hold root and fifth: root and the fifth below, two to the bar.", with({rate(r8)}),
                {bass("1 _ _ . 2v _ _ . 1 _ _ . 2v _ 2v 2v").vel("10 . . . 4 . . . 10 . . . 4 . 0 2")}}; });
   add(e, [&] { return Entry{"Train Beat", "Bass: Country", "Hold root and fifth: quarters, root and fifth below, train time.", with({rate(r8), swing(0.56)}),
                {bass("1 . 2v . 1 . 2v 1 1 . 2v . 1 . 2v 2").vel("10 . 2 . 8 . 2 -6 10 . 2 . 8 . 2 -4")}}; });
   add(e, [&] { return Entry{"Walk Up", "Bass: Country", "Hold a triad: root and fifth below, then a walk up to the change.", with({rate(r8)}),
                {bass("1 . 3v . 1 . 3v . 1 . 3v . 1 2 3 1^").vel("10 . 2 . 8 . 2 . 10 . 2 . 2 4 6 8")}}; });
   add(e, [&] { return Entry{"Bluegrass Run", "Bass: Country", "Hold a triad: two-beat roots and fifths, a run down to end.", with({rate(r8)}),
                {bass("1 . 3v . 1 . 3v . 1 . 3v . 1^ 3 2 1").vel("10 . 2 . 8 . 2 . 10 . 2 . 8 4 2 6")}}; });

   // ---- Jazz: swung quarters walking the chord, and the two feel ----
   add(e, [&] { return Entry{"Walking Bass", "Bass: Jazz", "Hold a triad: swung quarters up to the octave, down to the fifth.", with({rate(r8), swing(0.62)}),
                {bass("1 _ 2 _ 3 _ 1^ _ 3 _ 2 _ 1 _ 3v 1t").vel("8 . 2 . 6 . 2 . 8 . 2 . 6 . 0 -24")}}; });
   add(e, [&] { return Entry{"Two Feel", "Bass: Jazz", "Hold root and fifth: half notes with a skip, for the head.", with({rate(r8), swing(0.62)}),
                {bass("1 _ _ _ 2 _ _ 1t^ 1 _ _ _ 2v _ 2 _").vel("8 . . . 4 . . -20 8 . . . 4 . 2 .")}}; });
   add(e, [&] { return Entry{"Jazz Ballad", "Bass: Jazz", "Hold root and fifth: long, sparse notes for a slow ballad.", with({rate(r8), swing(0.62)}),
                {bass("1 _ _ _ _ _ 2 _ 1 _ _ _ 2v _ _ _").vel("6 . . . . . 0 . 6 . . . 0 . . .")}}; });
   add(e, [&] { return Entry{"Jazz Waltz", "Bass: Jazz", "Hold a triad: a 3/4 walk, up to the octave and back.", with({rate(r8), swing(0.62)}),
                {bass("1 _ 2 _ 3 _ 1^ _ 3 _ 2 _").vel("8 . 2 . 4 . 8 . 2 . 4 .")}}; });

   // ---- Soul: busy, melodic eighths and sixteenths around the beat ----
   add(e, [&] { return Entry{"Motown Walk", "Bass: Soul", "Hold a triad: a busy eighth walk up to the octave and back.", with({rate(r8)}),
                {bass("1 1s 2 3 1^ 3 2s 3").vel("12 -6 4 0 8 0 -4 2")}}; });
   add(e, [&] { return Entry{"Soul Pocket", "Bass: Soul", "Hold a triad: long roots, a lazy push to the next note.", with({rate(r16), swing(0.58)}),
                {bass("1 _ . 1s . . 2 . 3 _ . 2s . 1s . 2t").vel("12 . . -4 . . 6 . 8 . . 0 . 4 . -20")}}; });
   add(e, [&] { return Entry{"Stax Push", "Bass: Soul", "Hold root and fifth: pushed eighths that lean into each beat.", with({rate(r16), swing(0.54)}),
                {bass("1 _ . 1s . . 1 . . 1s 2 _ 1^ . 2s .").vel("12 . . -2 . . 8 . . -2 6 . 10 . 0 .")}}; });
   add(e, [&] { return Entry{"Soul Sixteenths", "Bass: Soul", "Hold a triad: busy sixteenths with ghost notes, up the chord.", with({rate(r16), swing(0.56)}),
                {bass("1 . 1t 2s . 1s 1 . 1 . 1t 3 . 2s 1s^ 3s").vel("12 . -24 4 . 0 8 . 10 . -24 6 . 2 10 4")}}; });

   // ---- Funk: on the one, ghost notes, octave pops; the b7 sits just under the octave ----
   add(e, [&] { return Entry{"Funk Bass", "Bass: Funk", "Hold root and b7: sixteenth funk, ghost pickups, an octave pop.", with({rate(r16), swing(0.56)}),
                {bass("1 . 1t 1s . 1t 2s 1 . 1s 2s . 1 1t 1s^ 2s").vel("16 . -26 4 . -26 12 8 . 0 -4 . 12 -26 14 -4")}}; });
   add(e, [&] { return Entry{"Slap Pops", "Bass: Funk", "Hold root and b7: thumb slaps, octave pops, the b7 as a grace.", with({rate(r16), swing(0.54)}),
                {bass("1s . 1t 1s^ . 1s 1t . 1s 1t . 1s^ . 1s 2t 1s^").vel("14 . -26 20 . 4 -26 . 10 -26 . 20 . 4 -20 18")}}; });
   add(e, [&] { return Entry{"Pocket Sixteenths", "Bass: Funk", "Hold a triad: a laid-back pocket walking up into the next bar.", with({rate(r16), swing(0.56)}),
                {bass("1 . . 1t 2s . 1s^ . 1 . 3s 2t . 1s 2s 3s").vel("14 . . -24 6 . 10 . 10 . 4 -24 . 6 2 8")}}; });
   add(e, [&] { return Entry{"On the One", "Bass: Funk", "Hold root and b7: a heavy downbeat, then space and octave pops.", with({rate(r16), swing(0.56)}),
                {bass("1 _ . . . . 1t 1s^ . 1t 2s . 1s . 1s^ 1t").vel("18 . . . . . -24 12 . -24 6 . 8 . 12 -20")}}; });

   // ---- Disco: octaves ----
   add(e, [&] { return Entry{"Disco Octaves", "Bass: Disco", "Hold root and fifth: octave-jumping eighths, a step up to end.", with({rate(r8)}),
                {bass("1s 1s^ 1s 1s^ 1s 1s^ 2s 2s^").vel("12 4")}}; });
   add(e, [&] { return Entry{"Disco Walk", "Bass: Disco", "Hold a triad: octave eighths walking through it, two bars.", with({rate(r8)}),
                {bass("1s 1s^ 1s 1s^ 2s 2s^ 2s 2s^ 3s 3s^ 3s 3s^ 2s 2s^ 2s 2s^").vel("12 4")}}; });
   add(e, [&] { return Entry{"Disco Sixteenths", "Bass: Disco", "Hold root and fifth: octaves, a sixteenth pickup into each beat.", with({rate(r16)}),
                {bass(rep("1s . 1s^ 1s", 3) + "2s . 2s^ 1s^").vel("12 . 6 -6")}}; });
   add(e, [&] { return Entry{"Philly Strut", "Bass: Disco", "Hold root and fifth: a strutting line with octave answers.", with({rate(r16), swing(0.54)}),
                {bass("1 _ . 1s^ . 1s 2 . 1 _ . 1s^ . 2s 1s^ 2s").vel("12 . . 6 . -4 8 . 12 . . 6 . -2 8 2")}}; });

   // ---- House: off the kick ----
   add(e, [&] { return Entry{"House Bass", "Bass: House", "Hold root and fifth: offbeat bass between the kicks.", with({rate(r16)}),
                {bass(". . 1s . . . 1s . . . 1s . . . 2s .").vel(". . 12 . . . 8 . . . 12 . . . 6 .")}}; });
   add(e, [&] { return Entry{"Rolling House", "Bass: House", "Hold root and fifth: rolling sixteenths filling the kick gaps.", with({rate(r16)}),
                {bass("1s . 1t 1s 1s^ . 1t 1s 1s . 1t 1s 2s . 2t 1s^").vel("12 . -20 4 10 . -20 4 12 . -20 4 8 . -20 10")}}; });
   add(e, [&] { return Entry{"Deep Offbeat", "Bass: House", "Hold a triad: deep house, long offbeats stepping up the chord.", with({rate(r16), swing(0.54)}),
                {bass(". . 1 _ . . 2 _ . . 1 _ . . 3 2s").vel(". . 10 . . . 6 . . . 10 . . . 4 0")}}; });
   add(e, [&] { return Entry{"Chicago Jack", "Bass: House", "Hold root and fifth: bouncing sixteenth stabs that jack.", with({rate(r16), swing(0.55)}),
                {bass(". 1s . 1s^ . 1s 1s . . 1s . 1s^ . 2s 1s^ .").vel(". 10 . 6 . 8 -4 . . 10 . 6 . 4 8 .")}}; });

   // ---- Techno: around the kick, hypnotic ----
   add(e, [&] { return Entry{"Acid Line", "Bass: Techno", "Hold root and fifth: squelchy sixteenths that jump octaves.", with({rate(r16)}),
                {bass("1s 1s^ 2^ _ 1t 2s . 1s 1s^ 2s . 1s^ 1 _ 2s^ 1t").vel("14 -6 18 . -10 4 . 12 -6 6 . 18 10 . 8 -12")}}; });
   add(e, [&] { return Entry{"Techno Rumble", "Bass: Techno", "Hold root and fifth: a low rumble leaving every beat to the kick.", with({rate(r16)}),
                {bass(". 1t 1s 1t . 1t 1s 1t . 1t 1s 1t . 1t 2s 1t^").vel(". -10 4 -10")}}; });
   add(e, [&] { return Entry{"Minimal Stabs", "Bass: Techno", "Hold root and fifth: sparse minimal stabs around the kick.", with({rate(r16)}),
                {bass(". . . 1t . . 1s . . . . 1t . 1s . 2t").vel(". . . 4 . . 10 . . . . 2 . 8 . 0")}}; });
   add(e, [&] { return Entry{"Hypnotic Three", "Bass: Techno", "Hold root and fifth: a three-step loop turning against the bar.", with({rate(r16)}),
                {bass("1s . 1s^ 1s . 1s^ 1s . 1s^ 1s . 1s^ 1s . 2s^ 1s").vel("12 . 4 -4 . 4 10 . 4 -4 . 4 12 . 6 -4")}}; });

   // ---- Trance: rolling sixteenths between the kicks ----
   add(e, [&] { return Entry{"Rolling Trance", "Bass: Trance", "Hold root and fifth: three sixteenths after every kick.", with({rate(r16)}),
                {bass(rep(". 1s 1s 1s", 3) + ". 2s 2s 2s").vel(". 10 -2 4")}}; });
   add(e, [&] { return Entry{"Psy Roll", "Bass: Trance", "Hold root and fifth: short, tight psytrance sixteenths, two bars.", with({rate(r16)}),
                {bass(rep(". 1t 1t 1t", 7) + ". 2t 2t 1t^").vel(". 12 0 6")}}; });
   add(e, [&] { return Entry{"Octave Roll", "Bass: Trance", "Hold root and fifth: the roll with an octave in the middle.", with({rate(r16)}),
                {bass(rep(". 1s 1s^ 1s", 3) + ". 2s 2s^ 1s^").vel(". 10 4 2")}}; });
   add(e, [&] { return Entry{"Triplet Roll", "Bass: Trance", "Hold root and fifth: two triplets after each kick, galloping.", with({rate(r8T), restart(bar)}),
                {bass(rep(". 1s 1s", 3) + ". 2s 1s^").vel(". 10 2")}}; });

   // ---- Drum and Bass: long subs under fast breaks ----
   add(e, [&] { return Entry{"Jungle Roller", "Bass: Drum and Bass", "Hold root and fifth: long rolling subs leaving room for breaks.", with({rate(r16)}),
                {bass("1 _ _ _ _ _ . . . . 1 _ 2 _ 1s^ .").vel("12 . . . . . . . . . 6 . 8 . 4 .")}}; });
   add(e, [&] { return Entry{"Reese Hold", "Bass: Drum and Bass", "Hold root and fifth: long, slow notes for a detuned reese.", with({rate(r16)}),
                {bass("1 _ _ _ _ _ _ _ _ _ . . 2 _ _ _ 1 _ _ _ _ _ _ _ . . 1s^ . 2 _ _ .").vel("10 . . . . . . . . . . . 6 . . . 10 . . . . . . . . . 4 . 6 . . .")}}; });
   add(e, [&] { return Entry{"Neuro Stabs", "Bass: Drum and Bass", "Hold root and fifth: short, nervous stabs around the snare.", with({rate(r16)}),
                {bass("1t . 1t 1s^ . . 1t . 1s . 1t^ 1t . 2s . 1t").vel("14 . 2 10 . . 4 . 12 . 8 0 . 10 . 4")}}; });
   add(e, [&] { return Entry{"Liquid Walk", "Bass: Drum and Bass", "Hold a triad: smooth, rolling notes stepping up the chord.", with({rate(r16)}),
                {bass("1 _ _ . . . 2 _ . . 3 _ . . 2s 1s^").vel("10 . . . . . 6 . . . 8 . . . 2 6")}}; });

   // ---- Dubstep: half time, room for the snare on three ----
   add(e, [&] { return Entry{"Half Time Sub", "Bass: Dubstep", "Hold root and fifth: a half-time sub, room left for the snare.", with({rate(r16)}),
                {bass("1 _ _ _ _ _ _ . . . . . . . 2 _ 1 _ _ _ _ _ . . . . 1s^ . 2 _ _ .").vel("12 . . . . . . . . . . . . . 6 . 12 . . . . . . . . . 6 . 8 . . .")}}; });
   add(e, [&] { return Entry{"Wobble", "Bass: Dubstep", "Hold root and fifth: ratchets that wobble at changing speeds.", with({rate(r16)}),
                {bass("1 1 1 1 1 1 1 1 2 2 2 2 1^ 1^ 1 1").rat("2 2 3 3 4 4 3 3 2 2 3 3 4 4 6 6").vel("10 4 6 2")}}; });
   add(e, [&] { return Entry{"Riddim", "Bass: Dubstep", "Hold root and fifth: sparse, repeating riddim stabs.", with({rate(r16)}),
                {bass("1s . . 1t 1t . . . 1s . 1t 1t . . 2s .").rat(". . . . . . . . . . . . . . 3 .").vel("12 . . 4 4 . . . 10 . 4 4 . . 8 .")}}; });
   add(e, [&] { return Entry{"Drop Stabs", "Bass: Dubstep", "Hold root and fifth: hard stabs and an octave jump for a drop.", with({rate(r16)}),
                {bass("1s . . 1s^ . . 1s . 2 _ . . 1s^ 1s . .").vel("16 . . 10 . . 12 . 10 . . . 14 6 . .")}}; });

   // ---- UK Garage: swung two-step and bouncing octaves ----
   add(e, [&] { return Entry{"Two Step", "Bass: UK Garage", "Hold a triad: two-step garage, a new figure with each chord.", with({rate(r16), swing(0.58), advance(chord)}),
                {bass("1 . 1t 2s . 1s . 1s^ 1 . 2s . 1t 1s 2 .").vel("14 . -24 6 . 4 . 12 12 . 4 . -24 6 8 ."),
                 bass("1s 1s^ . 3s 1s^ . 3s 1s . 1s 2s 3s 1s^ . 3s 2s").vel("14 6 . 4 12 . 2 8 . 6 2 4 12 . 4 0")}}; });
   add(e, [&] { return Entry{"Speed Garage", "Bass: UK Garage", "Hold root and fifth: bouncing octaves for speed garage.", with({rate(r16), swing(0.56)}),
                {bass("1s . 1s^ . 1s 1s^ . 1s . 1s^ . 1s 2s . 2s^ .").vel("12 . 8 . 4 10 . 6 . 8 . 4 10 . 8 .")}}; });
   add(e, [&] { return Entry{"Bassline House", "Bass: UK Garage", "Hold root and fifth: a bouncy, organ-style bassline groove.", with({rate(r16), swing(0.54)}),
                {bass("1s 1s^ . 1s . 1s^ 1s . 1s 1s^ . 1s . 2s^ 2s .").vel("12 6 . 2 . 8 4 . 12 6 . 2 . 8 4 .")}}; });
   add(e, [&] { return Entry{"Garage Skip", "Bass: UK Garage", "Hold root and fifth: skippy swung offbeats around the two-step.", with({rate(r16), swing(0.58)}),
                {bass(". . 1s . . 1s . . 1 _ . . 1s . 2s .").vel(". . 10 . . 4 . . 12 . . . 6 . 8 .")}}; });

   // ---- Hip Hop: long, swung notes with pickups ----
   add(e, [&] { return Entry{"Boom Bap Bass", "Bass: Hip Hop", "Hold root and fifth: a long root, a pickup and a step up, swung.", with({rate(r16), swing(0.58)}),
                {bass("1 _ _ . . . . 1s . . 2 _ . . 1s .").vel("12 . . . . . . 4 . . 8 . . . 0 .")}}; });
   add(e, [&] { return Entry{"Sub Slide", "Bass: Hip Hop", "Hold root and fifth: long 808 sub notes; add glide for slides.", with({rate(r16)}),
                {bass("1 _ _ _ _ _ 1s . . . 2 _ _ . 1t 1t").vel("12 . . . . . 4 . . . 8 . . . -10 -6")}}; });
   add(e, [&] { return Entry{"Trap Bounce", "Bass: Hip Hop", "Hold root and fifth: bouncing 808s with a rolled pickup.", with({rate(r16)}),
                {bass("1 _ _ . . . 1s . . 1s . . 1 _ 2s 1s^").rat(". . . . . . . . . . . . . . . 3").vel("12 . . . . . 6 . . 4 . . 10 . 4 2")}}; });
   add(e, [&] { return Entry{"G Funk", "Bass: Hip Hop", "Hold root and fifth: smooth West Coast bounce with octave lifts.", with({rate(r16), swing(0.58)}),
                {bass("1 _ . 1s^ . . 2 _ 1 _ . . 1t 1s^ . .").vel("12 . . 6 . . 8 . 12 . . . -10 6 . .")}}; });
   add(e, [&] { return Entry{"Lo-Fi Drift", "Bass: Hip Hop", "Hold root and fifth: a lazy, sparse line behind the beat.", with({rate(r16), swing(0.6)}),
                {bass("1 _ _ _ . . 2 _ . . . . 1 _ 2s^ .").vel("8 . . . . . 4 . . . . . 8 . 0 .")}}; });

   // ---- R&B: smooth lines through the chord ----
   add(e, [&] { return Entry{"Neo Soul", "Bass: R&B", "Hold a triad: a loose line wandering up to the octave and back.", with({rate(r16), swing(0.6)}),
                {bass("1 _ . 2s . 3 _ . 1^ . 3s . 2 . 1s 2t").vel("10 . . -4 . 6 . . 8 . -2 . 4 . 0 -20")}}; });
   add(e, [&] { return Entry{"Slow Jam", "Bass: R&B", "Hold a triad: slow-jam R&B, long notes with soft pickups.", with({rate(r16), swing(0.56)}),
                {bass("1 _ _ . . . 1s 2 _ _ . . 3 _ 2s 1s").vel("10 . . . . . -6 6 . . . . 8 . -4 -2")}}; });
   add(e, [&] { return Entry{"Gospel Climb", "Bass: R&B", "Hold a triad: a churchy climb to the octave, falling back.", with({rate(r8), swing(0.58)}),
                {bass("1 _ 2 3 1^ _ 3 2 1 _ 2 3 1^ 3 2 3").vel("10 . 0 4 10 . 2 0 10 . 0 4 10 2 0 4")}}; });
   add(e, [&] { return Entry{"Quiet Storm", "Bass: R&B", "Hold root and fifth: long, smooth notes with a soft octave lift.", with({rate(r16), swing(0.54)}),
                {bass("1 _ _ _ _ _ 2 _ 1 _ _ _ _ _ 2s 1s^").vel("8 . . . . . 4 . 8 . . . . . 0 4")}}; });

   // ---- Reggae: the bass sings; beat one left open in the one drop ----
   add(e, [&] { return Entry{"Dub Bass", "Bass: Reggae", "Hold a triad: heavy, spacious dub, a long root then short steps.", with({rate(r8), swing(0.55)}),
                {bass("1l _ . 3 1 _ . 2").vel("10 . . 4 8 . . 0")}}; });
   add(e, [&] { return Entry{"One Drop", "Bass: Reggae", "Hold a triad: beat one left empty, the line singing the chord.", with({rate(r8), swing(0.56)}),
                {bass(". . 1 _ 2 . 3 2 . . 1 _ 3 . 2 1").vel(". . 10 . 4 . 6 0 . . 10 . 6 . 2 0")}}; });
   add(e, [&] { return Entry{"Steppers", "Bass: Reggae", "Hold a triad: busy roots-reggae eighths, pushing on four.", with({rate(r8)}),
                {bass("1 1 2 1 1 1 2 3").vel("12 -4 4 -4")}}; });
   add(e, [&] { return Entry{"Rockers", "Bass: Reggae", "Hold root and fifth: punchy rockers reggae, syncopated.", with({rate(r8), swing(0.54)}),
                {bass("1 _ . 1 . . 2 . 1 _ . 1^ . . 2 1").vel("12 . . 6 . . 8 . 12 . . 8 . . 6 0")}}; });

   // ---- Ska: walking quarters, the upbeat on the guitar ----
   add(e, [&] { return Entry{"Ska Walk", "Bass: Ska", "Hold a triad: an upbeat walking line in quarters.", with({rate(r8)}),
                {bass("1 . 2 . 3 . 1^ . 3 . 2 . 1 . 3v .").vel("10 . 4 . 6 . 8 . 6 . 4 . 10 . 4 .")}}; });
   add(e, [&] { return Entry{"Rocksteady", "Bass: Ska", "Hold root and fifth: a slower, syncopated rocksteady line.", with({rate(r8), swing(0.56)}),
                {bass("1 _ . 1 2 . . 1^ 1 _ . 1 2 . 2 .").vel("12 . . 4 8 . . 4 12 . . 4 8 . 2 .")}}; });
   add(e, [&] { return Entry{"Two Tone", "Bass: Ska", "Hold root and fifth: fast, punky eighths for two-tone ska.", with({rate(r8)}),
                {bass("1 1 1^ 1 2 2 2^ 2").vel("12 0 6 0")}}; });

   // ---- Latin: tresillo, tumbao, cha-cha and cumbia ----
   add(e, [&] { return Entry{"Tresillo", "Bass: Latin", "Hold root and fifth: the 3+3+2 tresillo, answered by the fifth.", with({rate(r16)}),
                {bass("1 _ _ 1 _ _ 1 _ 2 . . 2s 1 . 1s^ .").vel("14 . . 8 . . 10 . 4 . . -6 8 . 12 .")}}; });
   add(e, [&] { return Entry{"Tumbao", "Bass: Latin", "Hold root and fifth: tumbao, notes on the and of two and on four.", with({rate(r8)}),
                {bass(". . . 1 _ _ 2 _ . . . 1^ _ _ 2 _").vel(". . . 10 . . 6 . . . . 8 . . 6 .")}}; });
   add(e, [&] { return Entry{"Cha Cha", "Bass: Latin", "Hold root and fifth: the cha-cha, two quick notes on four.", with({rate(r8)}),
                {bass("1 _ _ . 2 _ 2 2").vel("12 . . . 6 . 4 8")}}; });
   add(e, [&] { return Entry{"Cumbia", "Bass: Latin", "Hold root and fifth: cumbia, root and fifth below with pickups.", with({rate(r8)}),
                {bass("1 _ . . 2v _ . 2v 1 _ . . 2v _ 1 2v").vel("12 . . . 8 . . 0 12 . . . 8 . 2 0")}}; });

   // ---- Brazil: bossa, samba and baiao ----
   add(e, [&] { return Entry{"Bossa Bass", "Bass: Brazil", "Hold root and fifth: a dotted root, then the fifth, bossa style.", with({rate(r8)}),
                {bass("1 _ _ 2 2 _ _ _").vel("10 . . -6 4 . . .")}}; });
   add(e, [&] { return Entry{"Bossa Walk", "Bass: Brazil", "Hold a triad: bossa, the third passing on the way back up.", with({rate(r8)}),
                {bass("1 _ _ 3v 3v _ _ . 1 _ _ 2 3 _ _ .").vel("10 . . -6 4 . . . 10 . . -4 4 . . .")}}; });
   add(e, [&] { return Entry{"Samba Surdo", "Bass: Brazil", "Hold root and fifth: samba, root then the fifth below on two.", with({rate(r16)}),
                {bass(rep("1 _ _ 2v 2v _ _ .", 2)).vel("8 . . -4 12 . . .")}}; });
   add(e, [&] { return Entry{"Baiao", "Bass: Brazil", "Hold root and fifth: baiao, a dotted root and a push to the fifth.", with({rate(r16)}),
                {bass("1 _ _ 1 _ _ _ _ 2v _ _ 2v _ _ 2 _").vel("12 . . 6 . . . . 10 . . 6 . . 4 .")}}; });

   // ---- Afro: Afrobeat, highlife, afro house and amapiano ----
   add(e, [&] { return Entry{"Afrobeat", "Bass: Afro", "Hold root and fifth: a long, syncopated Afrobeat ostinato.", with({rate(r16), swing(0.54)}),
                {bass("1 . . 1s . 2s . 1 . 1s . . 2s 1s^ . . 1 . . 1s . 2s . 1s^ . 2s . 1s . 2 _ .")
                     .vel("12 . . 4 . 6 . 10 . 2 . . 6 10 . . 12 . . 4 . 6 . 10 . 4 . 2 . 8 . .")}}; });
   add(e, [&] { return Entry{"Highlife", "Bass: Afro", "Hold a triad: bright, bouncing highlife steps up the chord.", with({rate(r8), swing(0.56)}),
                {bass("1 . 2 3 1^ . 3 2").vel("10 . 2 4 8 . 4 2")}}; });
   add(e, [&] { return Entry{"Afro House", "Bass: Afro", "Hold root and fifth: a rolling 3+3+2 under the four-on-the-floor.", with({rate(r16)}),
                {bass("1 . . 1s . . 2s . . 1s . . 1 . 2t 1s^").vel("12 . . 6 . . 8 . . 4 . . 10 . -6 6")}}; });
   add(e, [&] { return Entry{"Log Drum", "Bass: Afro", "Hold root and fifth: amapiano log-drum hits, off the beat.", with({rate(r16), swing(0.56)}),
                {bass(". . . 1s . . 1s^ . 1 _ . 1s . 2s 1 .").vel(". . . 10 . . 8 . 12 . . 4 . 8 6 .")}}; });

   // ---- Pop ----
   add(e, [&] { return Entry{"Pop Pump", "Bass: Pop", "Hold root and fifth: steady pumping eighths under the chords.", with({rate(r8)}),
                {bass(rep("1", 12) + "2 2 2 2").vel("6 -4 2 -4")}}; });
   add(e, [&] { return Entry{"Pop Ballad", "Bass: Pop", "Hold root and fifth: whole and half notes for a ballad.", with({rate(r8)}),
                {bass("1 _ _ _ _ _ _ _ 1 _ _ _ 2 _ 1^ _").vel("6 . . . . . . . 6 . . . 2 . 4 .")}}; });
   add(e, [&] { return Entry{"Pop Push", "Bass: Pop", "Hold root and fifth: eighths that push ahead into each bar.", with({rate(r8)}),
                {bass("1 _ . 1 _ . 2 _ 1 _ . 1 _ . 2 1^").vel("10 . . 4 . . 6 . 10 . . 4 . . 6 8")}}; });
   add(e, [&] { return Entry{"Syncopated Pop", "Bass: Pop", "Hold root and fifth: a sixteenth pop groove, 3+3+2 and a lift.", with({rate(r16)}),
                {bass("1 . . 1s . . 1 . . . 1s . 2s . 1s^ .").vel("12 . . 4 . . 8 . . . 4 . 6 . 8 .")}}; });

   // ---- Synth: eighties sequencer bass ----
   add(e, [&] { return Entry{"Synth Pump", "Bass: Synth", "Hold root and fifth: driving root sixteenths, up at the bar's end.", with({rate(r16)}),
                {bass(rep("1s", 12) + "2s 2s 1s^ 1s").vel("12 -10 0 -10")}}; });
   add(e, [&] { return Entry{"Outrun Drive", "Bass: Synth", "Hold a triad: octave-bouncing sixteenths climbing the chord.", with({rate(r16)}),
                {bass(rep("1s 1s^", 4) + "2s 2s^ 2s 2s^ 3s 3s^ 2s 2s^").vel("10 -6")}}; });
   add(e, [&] { return Entry{"Italo Gallop", "Bass: Synth", "Hold root and fifth: Italo gallop, an eighth and two sixteenths.", with({rate(r16)}),
                {bass(rep("1 _ 1s^ 1s^", 3) + "2 _ 2s^ 1s^").vel("12 . -2 2")}}; });
   add(e, [&] { return Entry{"Synth Funk", "Bass: Synth", "Hold root and fifth: eighties synth-funk sixteenths with octaves.", with({rate(r16), swing(0.54)}),
                {bass("1s . 1s^ 1s . 1s^ . 2s 1s . 1s^ 1s . 2s^ 1s 2s").vel("12 . 8 -4 . 8 . 4 12 . 8 -4 . 8 2 4")}}; });

   // ---- Dancehall: dembow and riddims in 3+3+2 ----
   add(e, [&] { return Entry{"Dembow", "Bass: Dancehall", "Hold root and fifth: the dembow's 3+3+2 boom under reggaeton.", with({rate(r16)}),
                {bass("1 _ _ 1s . . 1 _ 1 _ _ 1s . . 2 _").vel("12 . . 4 . . 8 . 12 . . 4 . . 8 .")}}; });
   add(e, [&] { return Entry{"Dancehall Riddim", "Bass: Dancehall", "Hold root and fifth: a bouncing dancehall riddim, 3+3+2.", with({rate(r16)}),
                {bass("1 _ . 1 _ . 1 . . . 2 _ . 1s^ . .").vel("12 . . 6 . . 8 . . . 8 . . 6 . .")}}; });
   add(e, [&] { return Entry{"Moombahton", "Bass: Dancehall", "Hold root and fifth: slow, heavy dembow with octave jumps.", with({rate(r16)}),
                {bass("1 _ _ 1s^ . . 1 _ 1 _ _ 2s^ . . 1^ _").vel("14 . . 6 . . 10 . 14 . . 6 . . 8 .")}}; });
}

} // namespace library

} // namespace jarp

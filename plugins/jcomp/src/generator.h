#pragma once

// The generator: from a recipe (a genre and style, maybe a second genre blended in, a player, ENERGY
// and WEIRD, a seed) to a song. Everything is drawn from a seeded random stream, so the same recipe
// and seed always give the same song, and a section rolled again (its own dice) changes only that
// section.
//
// A section is made in steps:
//    1. its chords: the style's progression for the section's role, moved to the key, made richer or
//       plainer by the player and the style, and with WEIRD, reharmonised (tritone subs, backdoor
//       dominants, chromatic approach chords)
//    2. its rhythm: the style's comping rhythm that suits the section's energy (blended with the
//       second genre's), its twists (MODS), shaped to the energy, roughened by WEIRD, and played by
//       the player (busy or sparse, rung or chopped, runs into the changes)
// then three more takes are bred from it (mutate()), and a fill.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "harmony.h"
#include "players.h"
#include "shared.h"
#include "styles.h"

namespace jcomp {

// Whether two chords are the same harmony (root, bass, third, fifth and dominant 7th): a 9th
// turning into a 7th is a change of colour the chord sounding can carry on through, not a change
// to play; I turning into I7 (the IV's applied dominant, its b7 the note that leads on) is.
inline bool sameHarmony(const Chord &a, const Chord &b) {
   if (a.none() || b.none())
      return a.none() == b.none();
   if (a.root != b.root || a.bass != b.bass)
      return false;
   const auto &x = qualityOf(a).tones, &y = qualityOf(b).tones;
   for (int i : {3, 4, 5, 6, 8, 10})
      if ((std::find(x.begin(), x.end(), i) != x.end()) != (std::find(y.begin(), y.end(), i) != y.end()))
         return false;
   return true;
}


// A small seeded random stream (splitmix64).
struct Rng {
   uint64_t s;
   explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
   uint64_t next() {
      uint64_t z = (s += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
   }
   double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
   bool chance(double p) { return uniform() < p; }
   int range(int lo, int hi) {
      if (hi <= lo)
         return lo;
      return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1));
   }
};

inline uint32_t mix(uint32_t a, uint32_t b) {
   uint64_t z = (static_cast<uint64_t>(a) << 32 | b) + 0x9E3779B97F4A7C15ull;
   z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
   z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
   return static_cast<uint32_t>(z ^ (z >> 31));
}

struct Recipe {
   int genre = 0, style = 0;
   int genre2 = -1;    // blended in, or -1
   double blend = 0.25;
   int player = 0;
   double energy = 0.5; // moves every section's energy
   double weird = 0.2;  // 0 triads as written .. 1 extended and reharmonised, loose and odd
   uint32_t seed = 1;
   int key = 4;         // the key the song is written in
   std::array<double, kSections> sections{kArcEnergy[0], kArcEnergy[1], kArcEnergy[2], kArcEnergy[3],
                                          kArcEnergy[4], kArcEnergy[5], kArcEnergy[6]};
};

inline const Style &styleOf(const Recipe &r) { return styleAt(genreAt(r.genre), r.style); }
inline const Player &playerOf(const Recipe &r) { return kPlayerList[std::clamp(r.player, 0, kPlayers - 1)]; }

// A section's energy: its own, moved by ENERGY (0.5 leaves it).
inline double sectionEnergy(const Recipe &r, int section) {
   const double e = r.sections[static_cast<size_t>(std::clamp(section, 0, kSections - 1))];
   return std::clamp(e + (r.energy - 0.5) * 0.8, 0.0, 1.0);
}

// ---- chords ----

// Reads a progression ("C9|F9 G9|...") into a section's chords, moved up `transpose` semitones.
inline void writeProgression(Section &s, const char *text, int transpose) {
   s.chords.fill(Chord{});
   s.split = 0;
   int bar = 0;
   std::string t = text ? text : "";
   size_t at = 0;
   while (at <= t.size() && bar < kMaxProgBars) {
      size_t end = t.find('|', at);
      if (end == std::string::npos)
         end = t.size();
      const std::string barText = t.substr(at, end - at);
      std::vector<std::string> names;
      size_t a = 0;
      while (a < barText.size()) {
         while (a < barText.size() && barText[a] == ' ')
            ++a;
         size_t b = a;
         while (b < barText.size() && barText[b] != ' ')
            ++b;
         if (b > a)
            names.push_back(barText.substr(a, b - a));
         a = b;
      }
      if (!names.empty()) {
         const Chord first = chord(names[0].c_str(), transpose);
         s.chords[static_cast<size_t>(bar * 2)] = first;
         s.chords[static_cast<size_t>(bar * 2 + 1)] = first;
         if (names.size() > 1) {
            s.chords[static_cast<size_t>(bar * 2 + 1)] = chord(names[1].c_str(), transpose);
            s.split = static_cast<uint16_t>(s.split | (1u << bar));
         }
         ++bar;
      }
      at = end + 1;
   }
   s.progBars = static_cast<uint8_t>(std::max(1, bar));
}

inline bool minorChord(const Chord &c) {
   const std::vector<int> &t = qualityOf(c).tones;
   return std::find(t.begin(), t.end(), 3) != t.end() && std::find(t.begin(), t.end(), 4) == t.end();
}

// A richer version of a chord (9ths, 11ths, 13ths), or a plainer one (triads and sevenths). Given
// where it sits (the chord after it, the key and scale), a tension the key doesn't hold isn't added
// to a chord that is in the key (no 9th on iii, whose 9th is the key's b9: an avoid note); a dominant
// resolving to a minor chord takes the b9 (a minor target's tensions, not 9 and 13); and a sus chord
// gets its 13sus colour only when it resolves as a dominant (a tonic sus stays a sus).
inline Chord richer(Chord c, Rng &rng, const Chord *next = nullptr, int key = -1, int scale = kMajor) {
   bool diatonic = key >= 0;
   if (key >= 0)
      for (int i : qualityOf(c).tones)
         diatonic = diatonic && inScale(c.root + i, key, scale);
   const auto fits = [&](int interval) { return !diatonic || inScale(c.root + interval, key, scale); };
   const bool resolves = next && !next->none() && next->root == (c.root + 5) % 12;
   switch (c.quality) {
   case kMaj:
      // The V going home is a dominant (V9, V13sus), not a tonic's 6/9: the key's V, or a major chord
      // falling a fifth to a major chord from anywhere but the key's I or IV (the new key's V in a
      // modulating outro).
      if (resolves && key >= 0 &&
          (((c.root - key) % 12 + 12) % 12 == 7 ||
           (!minorChord(*next) && ((c.root - key) % 12 + 12) % 12 != 0 && ((c.root - key) % 12 + 12) % 12 != 5)))
         c.quality = rng.chance(0.7) ? kDom9 : kSus13;
      else if (fits(2))
         c.quality = rng.chance(0.5) || !fits(9) ? kAdd9 : kSixNine;
      break;
   case kMaj7: if (fits(2)) c.quality = kMaj9; break;
   case kMin:
      if (rng.chance(0.5) ? fits(10) : !fits(2))
         c.quality = fits(10) ? static_cast<uint8_t>(kMin7) : c.quality;
      else if (fits(2))
         c.quality = kMinAdd9;
      break;
   case kMin7: if (fits(2)) c.quality = rng.chance(0.6) || !fits(5) ? kMin9 : kMin11; break;
   case kDom7:
      if (resolves && minorChord(*next))
         c.quality = kFlat9;
      else if (key < 0 || fits(2))
         c.quality = rng.chance(0.6) ? kDom9 : kDom13;
      break;
   case kSus: if (key < 0 || resolves) c.quality = kSus13; break;
   case kSus7: c.quality = kSus13; break;
   case kSix: if (fits(2)) c.quality = kSixNine; break;
   default: break;
   }
   return c;
}
inline Chord plainer(Chord c) {
   switch (c.quality) {
   case kDom9: case kDom13: case kSharp9: case kFlat9: case kEleven: case kSharp11: case kSharp5: c.quality = kDom7; break;
   case kMin9: case kMin11: c.quality = kMin7; break;
   case kMaj9: case kMaj7Sharp11: c.quality = kMaj7; break;
   case kSixNine: c.quality = kSix; break;
   case kAdd9: c.quality = kMaj; break;
   case kMinAdd9: c.quality = kMin; break;
   case kSus13: c.quality = kSus7; break;
   default: break;
   }
   return c;
}
inline bool dominant(const Chord &c) {
   switch (c.quality) {
   case kDom7: case kDom9: case kDom13: case kSharp9: case kFlat9: case kEleven: case kSharp11: case kSharp5: return true;
   default: return false;
   }
}

// Other chords that could stand in for one, in the place it sits (before `next`): the panel's TRY
// INSTEAD, and WEIRD's reharmonising. Each is a standard substitution of the jazz and gospel
// vocabulary: the tritone sub, the backdoor dominant, the relative minor or major, a sus, a richer
// or plainer colour, a secondary dominant into the next chord, a diminished passing chord.
inline std::vector<std::pair<Chord, const char *>> tryInstead(const Chord &c, const Chord &next) {
   std::vector<std::pair<Chord, const char *>> out;
   if (c.none())
      return out;
   auto add = [&](Chord x, const char *why) {
      x.root = static_cast<int8_t>(((x.root % 12) + 12) % 12);
      if (x != c)
         for (const auto &o : out)
            if (o.first == x)
               return;
      if (x != c)
         out.push_back({x, why});
   };
   if (dominant(c)) {
      add({static_cast<int8_t>(c.root + 6), c.quality, -1}, "tritone sub");
      add({c.root, kSus13, -1}, "sus it");
      add({c.root, kSharp9, -1}, "sharp nine");
      add({static_cast<int8_t>(c.root + 7), kMin7, -1}, "ii before it");
   }
   if (c.quality == kMaj || c.quality == kMaj7 || c.quality == kMaj9 || c.quality == kSixNine || c.quality == kAdd9) {
      add({static_cast<int8_t>(c.root + 9), kMin7, -1}, "relative minor");
      add({static_cast<int8_t>(c.root + 4), kMin7, -1}, "iii for I");
      add({c.root, kMaj7Sharp11, -1}, "lydian");
   }
   if (c.quality == kMin || c.quality == kMin7 || c.quality == kMin9 || c.quality == kMin11) {
      add({static_cast<int8_t>(c.root + 3), kMaj7, -1}, "relative major");
      add({c.root, kMin6, -1}, "minor six");
      add({static_cast<int8_t>(c.root + 5), kDom9, -1}, "IV7 (dorian)");
   }
   if (!next.none() && next.root != c.root) {
      add({static_cast<int8_t>(next.root + 7), kDom7, -1}, "V of next");
      add({static_cast<int8_t>(next.root + 1), kDom9, -1}, "half step above");
      add({static_cast<int8_t>(next.root + 10), kDom9, -1}, "backdoor");
      add({static_cast<int8_t>(next.root + 11), kDim7, -1}, "passing dim");
   }
   Rng rng(static_cast<uint64_t>(c.root * 31 + c.quality));
   add(richer(c, rng), "richer");
   add(plainer(c), "plainer");
   return out;
}

// The chords of a section: the progression for its role, coloured and (with WEIRD) reharmonised.
inline void makeChords(const Recipe &r, int section, Section &s, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   const ProgSet &ps = g.progs[static_cast<size_t>(std::clamp(st.progSet, 0, static_cast<int>(g.progs.size()) - 1))];
   const char *text = ps.roles[roleOf(section)];
   if (!text || !*text)
      text = ps.roles[kRoleVerse];
   writeProgression(s, text, r.key);
   // How rich: the player's taste, the style's, and WEIRD ("triads, as written" to "extended").
   double rich = (playerOf(r).rich - 0.5) * 0.8 + (r.weird - 0.3) * 0.9;
   if (st.mods & kRich)
      rich += 0.4;
   if (st.mods & kPlain)
      rich = std::min(rich, -0.5);
   const double reharm = r.weird > 0.35 ? (r.weird - 0.35) * 0.55 : 0.0;
   const int scale = ps.scale;
   // The chord after a slot, as written: the bar's second half, or the next bar's first.
   const auto after = [&](const Section &sec, int b, int half) -> Chord {
      if (half == 0 && (sec.split >> b & 1))
         return sec.chords[static_cast<size_t>(b * 2 + 1)];
      return sec.chords[static_cast<size_t>(((b + 1) % sec.progBars) * 2)];
   };
   const Section written = s;
   for (int b = 0; b < s.progBars; ++b) {
      for (int half = 0; half < 2; ++half) {
         Chord &c = s.chords[static_cast<size_t>(b * 2 + half)];
         if (c.none() || (half == 1 && !(s.split >> b & 1)))
            continue;
         // Coloured against the next different chord: a dominant held two bars into a minor chord
         // still takes the b9 (the slot after its first bar is itself).
         Chord next = after(written, b, half);
         {
            int nb = b, nh = half;
            for (int k = 0; k < 2 * kMaxProgBars && next == written.chords[static_cast<size_t>(b * 2 + half)]; ++k) {
               if (nh == 0 && (written.split >> nb & 1))
                  nh = 1;
               else {
                  nb = (nb + 1) % written.progBars;
                  nh = 0;
               }
               next = after(written, nb, nh);
            }
         }
         // A chord held on from the slot before (a two-bar chord) keeps the colour it was given.
         const int prevSlot = half ? b * 2 : (b > 0 ? (s.split >> (b - 1) & 1 ? b * 2 - 1 : b * 2 - 2) : -1);
         if (prevSlot >= 0 && written.chords[static_cast<size_t>(prevSlot)] == written.chords[static_cast<size_t>(b * 2 + half)]) {
            c = s.chords[static_cast<size_t>(prevSlot)];
            continue;
         }
         // A dominant into a minor chord takes its b9 whatever the player's richness: voiced plain, its
         // rootless or spread voicing would add the natural 9th and 13th against the minor chord to come.
         if (rich >= -0.3 && c.quality == kDom7 && !next.none() && next.root == (c.root + 5) % 12 && minorChord(next)) {
            c.quality = kFlat9;
            continue;
         }
         if (rich < -0.3)
            c = plainer(c);
         else if (rich > 0.1 && rng.chance(std::min(0.9, rich)))
            c = richer(c, rng, &next, r.key, scale);
      }
      if (!(s.split >> b & 1))
         s.chords[static_cast<size_t>(b * 2 + 1)] = s.chords[static_cast<size_t>(b * 2)];
   }
   // Reharmonising: a dominant that resolves (down a fifth, to the chord after it) swapped for its
   // tritone sub, which then falls a half step (a blues I7 or a funk vamp's dominant resolves nowhere,
   // and keeps its root); or a chromatic approach (the dominant a half step above the next chord) in
   // the second half of a bar that leads to a change.
   // A tritone sub keeps the guide tones; the old chord's #5 is the sub's ninth (C#7#5's A is G9's
   // ninth: G7#5 would add a D# the chord never had). A 7b9 keeps its quality: Db7b9 holds every note
   // of G7b9.
   const auto subOf = [](Chord &d) {
      d.root = static_cast<int8_t>((d.root + 6) % 12);
      d.bass = -1;
      if (d.quality == kSharp5)
         d.quality = kDom9;
   };
   // (Not the progression's last bar: it leads to its own bar 1 only while the section loops; at the
   // section's end the next section starts somewhere else, so an approach aimed at bar 1 is wrong.)
   if (reharm > 0.0)
      for (int b = 0; b + 1 < s.progBars; ++b) {
         Chord &c = s.chords[static_cast<size_t>(b * 2)];
         const Chord next = after(s, b, 0);
         // Not the tonic: a blues's or a vamp's I7 is home, not a dominant to stand in for.
         const bool tonic = ((c.root - r.key) % 12 + 12) % 12 == 0;
         if (dominant(c) && !tonic && !next.none() && next.root == (c.root + 5) % 12 && rng.chance(reharm)) {
            subOf(c);
            if (!(s.split >> b & 1))
               s.chords[static_cast<size_t>(b * 2 + 1)] = c;
         } else if (s.split >> b & 1) {
            // The second half of a split bar: the V of a one-bar ii-V, the likeliest place for a sub.
            Chord &v = s.chords[static_cast<size_t>(b * 2 + 1)];
            const Chord to = after(s, b, 1);
            const bool home = ((v.root - r.key) % 12 + 12) % 12 == 0;
            if (dominant(v) && !home && !to.none() && to.root == (v.root + 5) % 12 && rng.chance(reharm))
               subOf(v);
         } else if (!(s.split >> b & 1) && next != c && !next.none() && !(st.mods & kFixed) &&
                    // (Not where the written bass already moves by step into the next chord, a falling
                    // line under I-V/3-vi: the approach would break the line or be the chord itself.)
                    !(next.bass >= 0 && [&] {
                       const int from = ((c.bass >= 0 ? c.bass : c.root) % 12 + 12) % 12;
                       const int d = ((from - next.bass) % 12 + 12) % 12;
                       return d == 1 || d == 2 || d == 11 || d == 10;
                    }()) &&
                    ((next.bass >= 0 ? next.bass : next.root) + 1) % 12 != c.root &&
                    rng.chance(reharm * 0.8)) {
            // (Not in a figure played as written: a son montuno's I-IV-V-IV is the clave's harmony,
            // and a chromatic dominant between its chords isn't its vocabulary; the tritone sub on a
            // resolving V above is.)
            // Ambient and the held-chord styles borrow a modal colour instead: the next chord's root,
            // suspended, a half bar early (not the slow blues, whose approach is a dominant's).
            const bool modal = ((st.mods & kLongs) && std::strcmp(g.name, "Blues") != 0) || g.family == kFamilies[6];
            s.chords[static_cast<size_t>(b * 2 + 1)] =
               modal ? Chord{next.root, static_cast<uint8_t>(kSus), -1}
                     // A half step above the bass the next chord is heard on (a slash chord's bass).
                     : Chord{static_cast<int8_t>(((next.bass >= 0 ? next.bass : next.root) + 1) % 12),
                             static_cast<uint8_t>(st.mods & kPlain ? kDom7 : kDom9), -1};
            s.split = static_cast<uint16_t>(s.split | (1u << b));
         }
      }
}

// ---- rhythms ----

inline Hit hitOf(char ch) {
   Hit h;
   switch (ch) {
   case 'X': h.vel = kAccent; break;
   case 'x': h.vel = kNormal; break;
   case 'o': h.vel = kSoft; break;
   case 'g': h.vel = kGhost; h.art = kArtChop; break;
   case 't': h.vel = kNormal; h.part = kPartTop; break;
   case 'u': h.vel = kNormal; h.part = kPartUpper; break;
   case 'b': h.vel = kNormal - 6; h.part = kPartBass; break;
   case 'f': h.vel = kNormal - 8; h.part = kPartFifth; break;
   default:
      if (ch >= '1' && ch <= '9') {
         h.vel = kNormal;
         h.part = kPartLine;
         h.degree = static_cast<uint8_t>(ch - '0');
      }
      break;
   }
   return h;
}

// Whether a pattern plays a chord on any beat (an offbeat style, a skank or a one drop, doesn't,
// and nothing should put one there).
inline bool chordOnBeat(const Pattern &p) {
   const int beat = p.perBeat();
   for (int s = 0; s < p.steps; s += beat)
      if (p.at(s).on() && !leftHand(p.at(s).part))
         return true;
   return false;
}
// Whether a chord added on step `s` would break the take's offbeat character: a beat where an offbeat
// take (a skank on the ands) has none, or beat 1 or 3 where a take keeps them empty (the one drop's
// skank on 2 and 4, the organ bubble).
inline bool keepsOff(const Pattern &p, int s) {
   const int beat = p.perBeat();
   if (s % beat)
      return false;
   if (!chordOnBeat(p))
      return true;
   if (s % strongSteps(p))
      return false;
   for (int k = 0; k < p.steps; k += strongSteps(p))
      if (p.at(k).on() && !leftHand(p.at(k).part))
         return false;
   return true;
}
// An offbeat take: its chords keep off beats 1 and 3 (a skank on the ands, the one drop's on 2
// and 4, the organ bubble), so nothing is added there.
// A take that holds its chords: at least half its strokes last two steps or more, or ring.
inline bool heldTake(const Pattern &p) {
   int held = 0, hits = 0;
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && !leftHand(p.at(s).part)) {
         ++hits;
         held += p.at(s).len >= 2 || p.at(s).art == kArtRing || p.at(s).art == kArtTopRings;
      }
   return hits > 0 && held * 2 >= hits;
}

inline bool offbeatTake(const Pattern &p) {
   const int strong = strongSteps(p);
   bool any = false;
   for (int s = 0; s < p.steps; ++s) {
      const Hit &h = p.at(s);
      if (!h.on() || !chordPart(h.part))
         continue;
      any = true;
      if (s % strong == 0)
         return false;
   }
   return any;
}

// Whether `step` is the first hit of its bar in the take.
inline bool firstInBar(const Pattern &p, int step, int perBar) {
   for (int s = step - step % perBar; s < step; ++s)
      if (p.at(s).on())
         return false;
   return true;
}

// Whether a chord hit is the first of its bar sure to play (a chance hit before it may not).
inline bool firstChord(const Pattern &p, int step) {
   const int perBar = p.stepsPerBar();
   if (!p.at(step).on() || leftHand(p.at(step).part))
      return false;
   for (int s = step - step % perBar; s < step; ++s)
      if (p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).chance >= 100)
         return false;
   return true;
}

inline bool hasLine(const Pattern &p) {
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && p.at(s).part == kPartLine)
         return true;
   return false;
}
inline bool hasLeftHand(const Pattern &p) {
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && leftHand(p.at(s).part))
         return true;
   return false;
}

// Whether DENSITY (below the middle) drops a hit: the weakest first (soft, off the beat, the left
// hand's notes under chords), spread over the knob's travel. Never the bar's first chord, nor the
// first chord of a second half bar that brings a change (`secondHalfChanges`), nor, in a bar without
// chords, its first hit. `dice` is the hit's own roll, 0..1. A figure played as written (`fixed`)
// keeps every stroke.
inline bool thinnedBy(const Pattern &p, int step, double density, double dice, bool secondHalfChanges,
                      bool fixed = false) {
   if (density >= 0.5 || !p.at(step).on() || p.at(step).part == kPartLine)
      return false;
   const int perBeat = p.perBeat();
   const int perBar = p.stepsPerBar();
   // A figure played as written keeps every stroke: thinning a timeline (the 12/8 bell, the batida's
   // and of 3, Freddie Green's four, a clave figure) breaks the figure that is the style.
   if (fixed)
      return false;
   // An offbeat take with a left hand (the Shout: bass on the beats, chords between) keeps its bass
   // on the bar's downbeat as another style keeps its first chord.
   const bool offbeatBass = offbeatTake(p) && hasLeftHand(p);
   if (offbeatBass && leftHand(p.at(step).part) && step % perBar == 0)
      return false;
   const int from = step - step % perBar;
   const Hit &h = p.at(step);
   bool chords = false;
   for (int s = from; s < from + perBar && s < p.steps; ++s)
      chords = chords || (p.at(s).on() && !leftHand(p.at(s).part));
   if (!chords && firstInBar(p, step, perBar))
      return false;
   // A muted ghost (a chucking motor's) isn't the bar's chord: the first sounding chord is kept, and
   // the ghosts go before any chord that rings.
   const auto sounding = [&](int s) { return p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).vel > kGhost + 8; }; // a ghost, as scaled by energy
   bool anySounding = false;
   for (int s = from; s < from + perBar && s < p.steps; ++s)
      anySounding = anySounding || sounding(s);
   if (!leftHand(h.part)) {
      if (!anySounding && firstChord(p, step))
         return false;
      if (anySounding && sounding(step)) {
         bool earlier = false;
         for (int s = from; s < step; ++s)
            earlier = earlier || (sounding(s) && p.at(s).chance >= 100);
         if (!earlier)
            return false;
      }
      bool firstHalf = true;
      for (int s = from; s < step; ++s)
         if (p.at(s).on() && !leftHand(p.at(s).part) && s - from >= halfBar(p))
            firstHalf = false;
      if (firstHalf && step - from >= halfBar(p) && secondHalfChanges)
         return false;
   }
   // A bass that alternates with the chords (boom-chick, oom-pah-pah: no chord on 1 or 3) is the
   // rhythm's backbone: its notes on the strong beats stay, the chicks go first.
   if (leftHand(h.part) && step % strongSteps(p) == 0) {
      bool chordOnStrong = false;
      for (int s = 0; s < p.steps && !chordOnStrong; s += strongSteps(p))
         chordOnStrong = p.at(s).on() && !leftHand(p.at(s).part);
      if (!chordOnStrong)
         return false;
   }
   const bool onBeat = step % perBeat == 0;
   const bool onEighth = p.rate == kRate8T ? step % 3 != 1 : step % 2 == 0;
   double weight = (onBeat ? 0.7 : onEighth ? 0.45 : 0.2) + 0.25 * h.vel / 127.0 + (h.vel >= kAccent ? 0.15 : 0.0) +
                   0.15 * dice;
   weight = (weight - 0.25) / 0.85;
   if (leftHand(h.part) && chords && !offbeatBass)
      weight -= 0.2;
   if (!leftHand(h.part) && h.vel <= kGhost + 8 && anySounding)
      weight -= 0.6;
   return weight < std::sqrt((0.5 - density) * 2.0);
}


// A rhythm as a pattern, on its grid (by default as its steps say: twelve a bar the triplet grid).
// One bar is played twice, so a take is at least two bars.
inline Pattern fromRhythm(const char *text, int rate = -1) {
   Pattern p;
   p.rate = static_cast<uint8_t>(rate >= 0 ? rate : rhythmTriplet(text) ? kRate8T : kRate16);
   int n = 0;
   int last = -1;
   for (const char *c = text; *c && n < kMaxSteps; ++c) {
      if (*c == '|')
         continue;
      if (*c == '-') {
         if (last >= 0 && p.at(last).len < 255)
            ++p.at(last).len;
      } else {
         p.at(n) = hitOf(*c);
         if (p.at(n).on())
            last = n;
      }
      ++n;
   }
   p.steps = static_cast<uint8_t>(std::max(1, n));
   if (p.steps == p.stepsPerBar() && p.steps * 2 <= kMaxSteps) {
      for (int s = 0; s < p.steps; ++s)
         p.at(s + p.steps) = p.at(s);
      p.steps = static_cast<uint8_t>(p.steps * 2);
   }
   return p;
}

inline Pattern fromRhythm(const Rhythm &r) { return fromRhythm(r.hits, rhythmRate(r)); }
inline int perBeat(const Pattern &p) { return p.perBeat(); }
inline bool onBeat(const Pattern &p, int s) { return s % perBeat(p) == 0; }
inline bool onEighth(const Pattern &p, int s) { return p.rate == kRate8T ? s % 3 != 1 : s % 2 == 0; }

// Whether a rhythm lives on the fine grid: sixteenths (or the middle of a triplet) as well as eighths.
// A montuno, a bossa batida, a Charleston or a shuffle doesn't, and nothing added to it (a busy
// player's ghosts, a mutation, WEIRD's strays, DENSITY) should be either.
inline bool fineGrid(const Pattern &p) {
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && !onEighth(p, s))
         return true;
   return false;
}
// A step a hit may be added on: any, on the fine grid; else only an eighth.
inline bool addable(const Pattern &p, int s, bool fine) { return fine || onEighth(p, s); }
// The bar's first hit (what thinning keeps: an offbeat style's first chop, not a chord on the one).
inline int firstHit(const Pattern &p, int bar) {
   const int n = p.stepsPerBar();
   for (int s = bar * n; s < std::min<int>(p.steps, (bar + 1) * n); ++s)
      if (p.at(s).on())
         return s;
   return -1;
}
// Whether a written hit before `step` is still holding over it (its length reaches it).
inline bool heldOver(const Pattern &p, int step) {
   for (int k = 1; k < p.steps; ++k) {
      const Hit &h = p.at((step - k + p.steps) % p.steps);
      if (h.on())
         return h.len > k;
   }
   return false;
}
inline int hitsInBar(const Pattern &p, int bar, bool chordsOnly = false) {
   const int n = p.stepsPerBar();
   int k = 0;
   for (int s = bar * n; s < std::min<int>(p.steps, (bar + 1) * n); ++s)
      k += p.at(s).on() && !(chordsOnly && leftHand(p.at(s).part));
   return k;
}

// The rhythms a style may use (its mask, on its grid).
// A three-beat style uses only three-beat rhythms; a four-beat one may borrow the other four-beat grid's
// when its own has none.
inline std::vector<const Rhythm *> rhythmsFor(const Genre &g, const Style &st, bool fills = false) {
   std::vector<const Rhythm *> out;
   const uint8_t rate = styleRate(st.mods);
   const auto &list = fills ? g.fills : g.rhythms;
   for (size_t i = 0; i < list.size(); ++i) {
      if (rhythmRate(list[i]) != rate)
         continue;
      if (!fills && st.rhythms && !(st.rhythms >> i & 1))
         continue;
      out.push_back(&list[i]);
   }
   if (out.empty() && !fills)
      for (const Rhythm &r : list)
         if (rhythmRate(r) == rate)
            out.push_back(&r);
   if (out.empty() && rate != kRate34 && rate != kRate68)
      for (const Rhythm &r : list)
         if (rhythmRate(r) != kRate34 && rhythmRate(r) != kRate68)
            out.push_back(&r);
   return out;
}

// The rhythm that suits an energy, picked with some chance.
inline const Rhythm *pickRhythm(const std::vector<const Rhythm *> &list, double e, Rng &rng) {
   if (list.empty())
      return nullptr;
   // The energy held inside the list's span: past its busiest rhythm, that one is the pick (the
   // weights would otherwise level out and a sparse rhythm turn up at the climax).
   double lo = list.front()->energy, hi = lo;
   for (const Rhythm *r : list) {
      lo = std::min(lo, r->energy);
      hi = std::max(hi, r->energy);
   }
   e = std::clamp(e, lo, hi);
   double total = 0.0;
   std::vector<double> w;
   for (const Rhythm *r : list) {
      const double d = std::abs(r->energy - e);
      w.push_back(std::exp(-(d / 0.15) * (d / 0.15)) + 0.01);
      total += w.back();
   }
   double x = rng.uniform() * total;
   for (size_t i = 0; i < list.size(); ++i) {
      x -= w[i];
      if (x <= 0.0)
         return list[i];
   }
   return list.back();
}

// Steps of b taken into a with chance `amount` (a second genre blended in), on the same grid.
// BLEND: another genre's rhythm, hit by hit. One on the other grid comes across beat by beat: a
// straight eighth onto the shuffle's third triplet, the swung one onto the straight and.
inline Pattern onGrid(const Pattern &b, uint8_t rate) {
   if (b.rate == rate)
      return b;
   Pattern out;
   out.rate = rate;
   const int from = b.rate == kRate8T ? 3 : 4, to = rate == kRate8T ? 3 : 4;
   const int beats = b.steps / from;
   out.steps = static_cast<uint8_t>(std::min(kMaxSteps, beats * to));
   static constexpr int k16to12[4] = {0, 1, 2, 2}, k12to16[3] = {0, 1, 2};
   for (int s = 0; s < b.steps; ++s) {
      if (!b.at(s).on())
         continue;
      const int sub = s % from, beat = s / from;
      const int t = beat * to + (from == 4 ? k16to12[sub] : k12to16[sub]);
      if (t < out.steps && (!out.at(t).on() || b.at(s).vel > out.at(t).vel)) {
         out.at(t) = b.at(s);
         // Its length in this grid's steps (a held quarter is three triplets, or four sixteenths).
         out.at(t).len = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(b.at(s).len * double(to) / from)), 1,
                                                               std::max(1, out.steps - t)));
      }
   }
   return out;
}
inline void blendInto(Pattern &a, const Pattern &b0, double amount, Rng &rng) {
   const Pattern b = onGrid(b0, a.rate);
   if (b.steps == 0)
      return;
   // Its rhythm only: the other genre's onsets as this one's chords (not its melody line, nor a left
   // hand this take hasn't got), never a rest over this take's hit, nor a chord over its left hand.
   // Nor on a beat an offbeat take leaves empty (a one drop's one), inside a chord it holds, or a
   // quieter hit over one of its own (its accented downbeat stays).
   const bool leftHandA = hasLeftHand(a);
   const Pattern before = a;
   for (int s = 0; s < a.steps; ++s)
      if (rng.chance(amount)) {
         Hit h = b.at(s % b.steps);
         // (Its onset and level only: this style's mods say how it's played, not a ghost's mute.)
         if (h.on()) {
            h.art = kArtAsWritten;
            if (h.vel < kSoft)
               h.vel = kSoft;
            h.push = 0;
            h.run = 0;
         }
         if (!h.on() || (a.at(s).on() && (leftHand(a.at(s).part) || a.at(s).part == kPartLine)))
            continue; // never over a left hand's note or a riff's
         if (keepsOff(before, s) || heldOver(before, s) || (before.at(s).on() && h.vel < before.at(s).vel))
            continue;
         if (h.part == kPartLine || (leftHand(h.part) && !leftHandA)) {
            h.part = kPartChord;
            h.degree = 0;
         }
         a.at(s) = h;
      }
}

// The next hit after step s (wrapping), or -1.
inline int nextHit(const Pattern &p, int s) {
   for (int k = 1; k <= p.steps; ++k)
      if (p.at((s + k) % p.steps).on())
         return (s + k) % p.steps;
   return -1;
}
inline int gapAfter(const Pattern &p, int s) {
   const int n = nextHit(p, s);
   return n < 0 ? p.steps : (n - s + p.steps) % p.steps;
}

inline void applyMods(Pattern &p, unsigned mods, Rng &rng, double pushChance = 0.75) {
   const int bar = p.stepsPerBar();
   const int beat = perBeat(p);
   const bool fine = fineGrid(p);
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      const int inBar = s % bar;
      // An anticipation is a hit an eighth or less before beat one or three: the and of 2 and of 4
      // (or the last sixteenth of either), or the last triplet of beats 2 and 4.
      const bool pushAt = p.rate == kRate8T    ? (inBar == bar - 1 || inBar == bar / 2 - 1)
                          : p.rate == kRate34 ? (inBar == bar - 1 || inBar == bar - 2)
                                              : (inBar == bar - 1 || inBar == bar - 2 || inBar == bar / 2 - 1 || inBar == bar / 2 - 2);
      if (mods & kPushes && h.on() && !onBeat(p, s) && pushAt && !leftHand(h.part))
         h.push = rng.chance(pushChance) ? 1 : 0;
      // Fewer hits, but never the rhythm's shape: only a bar of four or more chords loses one, down to
      // three (a Charleston, a clave bar or a bossa cycle keeps every hit: the thumb isn't counted).
      if (mods & kSparse && h.on() && !leftHand(h.part) && !onBeat(p, s) && h.vel < kAccent &&
          hitsInBar(p, s / bar, true) > 3 && rng.chance(0.45))
         h = Hit{};
      // (No style sets it today; a left hand and the accents would stay.)
      if (mods & kHalfTime && h.on() && !leftHand(h.part) && h.vel < kAccent && (s / beat) % 2 == 1 && rng.chance(0.7))
         h = Hit{};
      // (A busy scratch's accents ring out: "occasional chords are allowed to ring out", Stewart.)
      if (mods & kShorts && h.on() && h.len <= 1 && !leftHand(h.part) && !((mods & kBusy) && h.vel >= kAccent * 9 / 10))
         h.art = kArtChop;
      if (mods & kTopLine && h.on() && h.part == kPartChord && !onBeat(p, s) && rng.chance(0.35))
         h.part = rng.chance(0.5) ? kPartTop : kPartUpper;
   }
   if (mods & kBusy)
      for (int s = 0; s < p.steps; ++s)
         if (!p.at(s).on() && !onBeat(p, s) && addable(p, s, fine) && rng.chance(0.4)) {
            Hit g = hitOf('g');
            p.at(s) = g;
         }
   if (mods & kLongs)
      for (int s = 0; s < p.steps; ++s)
         if (p.at(s).on() && p.at(s).art != kArtChop)
            p.at(s).art = kArtRing;
   if (mods & kLeftHand) {
      for (int s = 0; s < p.steps; s += strongSteps(p)) {
         if (!p.at(s).on()) {
            p.at(s) = hitOf('b');
         } else {
            // the left hand an eighth behind the chord: the fifth, under the root the chord already
            // has (on the shuffle's third triplet, never its middle one, and never a sixteenth)
            const int e = 2;
            if (p.at(s).part == kPartChord && s % bar == 0 && s + e < p.steps && !p.at(s + e).on() && rng.chance(0.3)) {
               p.at(s + e) = hitOf('f');
               p.at(s + e).vel = kSoft;
            }
         }
      }
   }
}

// Shaped to a section's energy: down low only the anchors stay (a break is one stab on the one),
// up high the accents grow and the soft hits fill in.
inline void shapeEnergy(Pattern &p, double e, Rng &rng, bool fixed = false, unsigned mods = 0, bool swung = false) {
   const int bar = p.stepsPerBar();
   // A fixed figure (a montuno, a batida, Freddie Green's four) keeps every stroke: only louder or softer.
   if (fixed) {
      const double scale = e < 0.15 ? 0.5 + 2.2 * e : 0.82 + 0.3 * e;
      for (int s = 0; s < p.steps; ++s)
         if (p.at(s).on())
            p.at(s).vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(p.at(s).vel * scale)), 20, 127));
      return;
   }
   // The anchors are each bar's first chord (the one, or an offbeat style's first chop: not a left
   // hand's bass note, nor a ghost) and the accents.
   std::array<int, kMaxSteps / 12 + 1> first{};
   for (int b = 0; b < p.bars(); ++b) {
      int f = -1;
      for (int s = b * bar; s < std::min<int>(p.steps, (b + 1) * bar) && f < 0; ++s)
         if (p.at(s).on() && !leftHand(p.at(s).part) && p.at(s).vel > kGhost)
            f = s;
      first[static_cast<size_t>(b)] = f >= 0 ? f : firstHit(p, b);
   }
   const auto isFirst = [&](int s) { return first[static_cast<size_t>(s / bar)] == s; };
   if (e < 0.15) {
      // Only the anchors: each bar's first hit, rung (one stab on the one, or the first skank), and
      // soft: the quieter the section the softer, down to near silence at 0.
      const auto quiet = static_cast<uint8_t>(std::lround(kNormal * (0.45 + 2.5 * e)));
      // An offbeat figure over a left hand (the boogie, the shout) keeps the bass on each bar's one:
      // it's what the offbeats lean against.
      const bool offbeatBass = offbeatTake(p) && hasLeftHand(p);
      for (int s = 0; s < p.steps; ++s) {
         Hit &h = p.at(s);
         if (!h.on())
            continue;
         if (offbeatBass && leftHand(h.part) && s % bar == 0) {
            h.vel = std::min<uint8_t>(h.vel, static_cast<uint8_t>(quiet + 6)); // kept, but as quiet
            continue;
         }
         if (!isFirst(s)) {
            h = Hit{};
            continue;
         }
         h.vel = std::min(h.vel, quiet);
         // (rung, but not in a style choked short: a quiet skank is still a skank)
         if (h.part == kPartChord)
            h.art = mods & kShorts ? kArtChop : kArtRing;
      }
      if (p.empty()) {
         p.at(0) = hitOf('x');
         p.at(0).vel = quiet;
         p.at(0).art = mods & kShorts ? kArtChop : kArtRing;
      }
      return;
   }
   // A motor of muted strokes (the chicken scratch, Chic's chucking) keeps its eighths: down low the
   // ringing chords go first, not the grid they ride on.
   int ghosts = 0;
   for (int s = 0; s < p.steps; ++s)
      ghosts += p.at(s).on() && p.at(s).vel == kGhost;
   const bool motor = ghosts * 2 >= p.count();
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (!h.on())
         continue;
      // A left hand's notes (the bossa's thumb, the boom-chick's bass) and a line's are anchors too.
      const bool anchor = isFirst(s) || h.vel >= kAccent || leftHand(h.part) || h.part == kPartLine ||
                          (motor && h.vel == kGhost && onEighth(p, s) && e >= 0.2);
      if (!anchor && e < 0.4 && rng.chance((0.4 - e) * 1.6))
         h = Hit{};
      else if (!anchor && h.vel == kGhost && e < 0.5 && rng.chance(0.5))
         h = Hit{};
   }
   if (p.empty())
      p.at(0) = hitOf('x');
   // Up high it fills in: soft chords on empty eighths the rhythm allows (not where a chord is held,
   // nor in an offbeat take, whose chops keep their places, nor in a held-chord style, a line or a
   // left hand's).
   // Nor in a four to the bar (a rhythm guitar's chunk on every beat, its pickups written in: four
   // beats a bar, so not a jazz waltz's three), and a swing comper's are offbeats, not a pulse of
   // chords on the beats (the engine's DENSITY rule).
   int beatHits = 0, offHits = 0;
   bool everyBeat = p.steps > 0;
   for (int s = 0; s < p.steps; ++s) {
      const bool chordHit = p.at(s).on() && !leftHand(p.at(s).part);
      if (onBeat(p, s)) {
         beatHits += chordHit;
         everyBeat = everyBeat && (chordHit || s >= p.stepsPerBar());
      } else
         offHits += chordHit;
   }
   const bool four = everyBeat && offHits * 4 <= beatHits && (p.rate == kRate16 || p.rate == kRate8T);
   if (e > 0.7 && !four && !(mods & kLongs) && !hasLine(p) && !hasLeftHand(p) && !offbeatTake(p)) {
      const bool fine = fineGrid(p);
      for (int s = 0; s < p.steps; ++s)
         if (!p.at(s).on() && onEighth(p, s) && addable(p, s, fine) && !keepsOff(p, s) && !heldOver(p, s) &&
             !(swung && onBeat(p, s)) && rng.chance((e - 0.7) * 1.2))
            p.at(s) = hitOf('o');
   }
   // A style choked short stays short: the chords added here are chopped as its own are (its busy
   // accents excepted, as in applyMods).
   if (mods & kShorts)
      for (int s = 0; s < p.steps; ++s) {
         Hit &h = p.at(s);
         if (h.on() && h.len <= 1 && !leftHand(h.part) && h.art == kArtAsWritten && !((mods & kBusy) && h.vel >= kAccent * 9 / 10))
            h.art = kArtChop;
      }
   const double scale = 0.82 + 0.3 * e;
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (h.on())
         h.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(h.vel * scale)), 20, 127));
   }
}

// A take's bar back at its written levels, before shapeEnergy's scale (a fill made from it is scaled
// once, not twice).
inline Pattern unscaled(Pattern p, double e) {
   const double scale = 0.82 + 0.3 * e;
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on())
         p.at(s).vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(p.at(s).vel / scale)), 1, 127));
   return p;
}

// The loudest chord hit of a pattern (0 if none).
inline int chordPeak(const Pattern &p) {
   int peak = 0;
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && !leftHand(p.at(s).part))
         peak = std::max<int>(peak, p.at(s).vel);
   return peak;
}

// Hits a change added (where `before` had none) no louder than the pattern's own: a quiet take's
// added chord isn't the loudest thing in it.
inline void capAdded(Pattern &p, const Pattern &before) {
   const int peak = chordPeak(before);
   if (peak <= 0)
      return;
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on() && !(s < before.steps && before.at(s).on()) && p.at(s).vel > peak)
         p.at(s).vel = static_cast<uint8_t>(peak);
}

// WEIRD: hits left to chance, pulled off the grid, odd lengths, stray stabs.
// A fixed figure only loosens in time; a style chopped short (a skank) only chops or halves.
inline void applyWeird(Pattern &p, double w, Rng &rng, unsigned mods = 0) {
   if (w <= 0.0)
      return;
   const int peak = chordPeak(p);
   const bool fine = fineGrid(p);
   const bool fixed = mods & kFixed;
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (h.on()) {
         // A line (a riff, the tenor line) and the left hand's figure play every note as written:
         // WEIRD only nudges them.
         const bool chordHit = h.part != kPartLine && !leftHand(h.part);
         if (!fixed && chordHit && !onBeat(p, s) && rng.chance(w * 0.3))
            h.chance = static_cast<uint8_t>(rng.range(55, 90));
         if (rng.chance(w * 0.25))
            h.nudge = static_cast<int8_t>(std::clamp(rng.range(-12, 12) * w * 1.5, -double(kMaxNudge), double(kMaxNudge)));
         if (!fixed && chordHit && rng.chance(w * 0.15))
            h.art = static_cast<uint8_t>(mods & kLongs    ? rng.range(kArtRing, kArtTopRings)
                                         : mods & kShorts ? kArtChop
                                                          : rng.range(kArtChop, kArtTopRings));
      } else if (!fixed && !(mods & kLongs) && !hasLine(p) && !onBeat(p, s) && addable(p, s, fine) &&
                 rng.chance(w * w * 0.12)) {
         h = hitOf(rng.chance(0.5) ? 'o' : 'g');
         h.chance = static_cast<uint8_t>(rng.range(50, 85));
         if (peak > 0 && h.vel > peak - 6) // a stray sits under the take's own hits
            h.vel = static_cast<uint8_t>(std::max(20, peak - 6));
      }
   }
}

// The player: busy or sparse, rung or chopped, runs into the changes.
inline void applyPlayer(Pattern &p, const Player &pl, unsigned mods, Rng &rng) {
   const int bar = p.stepsPerBar();
   // A busy player's ghosted sixteenths only go where the style already plays sixteenths: none in a
   // montuno, a Freddie Green four, a bossa or a shuffle.
   const bool fine = fineGrid(p);
   const Pattern before = p;
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (!h.on()) {
         if (pl.busy > 0.7 && fine && !(mods & kFixed) && !onEighth(p, s) && s > 0 && p.at(s - 1).on() &&
             p.at(s - 1).part != kPartLine && rng.chance((pl.busy - 0.7) * 1.2))
            h = hitOf('g');
         continue;
      }
      if (pl.busy < 0.3 && !(mods & kFixed) && !onBeat(p, s) && h.vel < kAccent && !leftHand(h.part) && h.part != kPartLine &&
          rng.chance((0.3 - pl.busy) * 1.5)) {
         h = Hit{};
         continue;
      }
      // Only a piano's pedal holds the top note over the chord let go; an organ or a pad holds it all.
      const bool pedal = pl.instrument == kPiano || pl.instrument == kEPiano;
      // An organ holds its chords to the next, however close (a 12/8 ballad's triplets sustained).
      if (h.art == kArtAsWritten && h.len <= 1 && !(mods & kShorts) && (gapAfter(p, s) >= 3 || pl.instrument == kOrgan) &&
          rng.chance(pl.instrument == kOrgan ? 1.0 : pl.ring * 0.7))
         h.art = pl.ring > 0.8 && pedal ? kArtTopRings : kArtRing;
      if (h.art == kArtAsWritten && h.len <= 1 && pl.ring < 0.15 && rng.chance(0.5))
         h.art = kArtChop;
      // The run goes into the bar's first chord (the Shout's downbeat is its bass; the chord after it),
      // where the change is: on the first beat, or pushed ahead of it. A backbeat's first chord is
      // beat 2, the chord already sounding: no run goes into that.
      if (h.part == kPartChord && firstChord(p, s) && (s % bar < bar / 4 || h.push)) {
         const double runs = pl.runs + (mods & kRunsIn ? 0.5 : 0.0);
         h.run = rng.chance(std::min(1.0, runs + 0.15)) ? 1 : 0;
      }
   }
   if (p.empty()) {
      const int s = std::max(0, firstHit(before, 0));
      p.at(s) = before.at(s).on() ? before.at(s) : hitOf('x');
   }
}

// ---- mutations ----

struct Mutation {
   const char *tag;
   void (*apply)(Pattern &, Rng &);
};

inline const std::vector<Mutation> &mutations() {
   static const std::vector<Mutation> list = {
      {"busier", [](Pattern &p, Rng &rng) {
          const bool fine = fineGrid(p);
          for (int k = 0; k < 3; ++k) {
             const int s = rng.range(0, p.steps - 1);
             if (!p.at(s).on() && addable(p, s, fine) && !keepsOff(p, s))
                p.at(s) = hitOf(rng.chance(0.6) ? 'o' : 'x');
          }
       }},
      {"sparser", [](Pattern &p, Rng &rng) {
          for (int k = 0; k < 3; ++k) {
             const int s = rng.range(1, p.steps - 1);
             if (p.at(s).on() && s % p.stepsPerBar() != 0 && p.count() > 2)
                p.at(s) = Hit{};
          }
       }},
      {"push it", [](Pattern &p, Rng &rng) {
          const int bar = p.stepsPerBar();
          for (int b = 0; b < p.steps / bar; ++b) {
             const int s = b * bar + bar - (p.rate == kRate8T ? 1 : 2);
             // Chords only: a left hand's note or a line's stays as it is.
             if (p.at(s).on() && (leftHand(p.at(s).part) || p.at(s).part == kPartLine))
                continue;
             if (rng.chance(0.7)) {
                if (!p.at(s).on())
                   p.at(s) = hitOf('x');
                p.at(s).push = 1;
                p.at(s).art = kArtRing;
             }
          }
       }},
      {"chop it", [](Pattern &p, Rng &rng) {
          for (int s = 0; s < p.steps; ++s)
             if (p.at(s).on() && rng.chance(0.6))
                p.at(s).art = kArtChop;
       }},
      {"let it ring", [](Pattern &p, Rng &rng) {
          for (int s = 0; s < p.steps; ++s)
             if (p.at(s).on() && p.at(s).vel > kGhost && rng.chance(0.6))
                p.at(s).art = rng.chance(0.3) ? kArtTopRings : kArtRing;
       }},
      {"shift", [](Pattern &p, Rng &rng) {
          // A hit moved to the next step either way: a sixteenth, or an eighth in an eighth-note rhythm.
          const bool fine = fineGrid(p);
          for (int k = 0; k < 4; ++k) {
             const int s = rng.range(1, p.steps - 2);
             const int dir = rng.chance(0.5) ? 1 : -1;
             int to = s + dir;
             while (to > 0 && to < p.steps - 1 && !addable(p, to, fine))
                to += dir;
             if (to <= 0 || to >= p.steps || !addable(p, to, fine) || keepsOff(p, to))
                continue;
             if (p.at(s).on() && !p.at(to).on() && s % p.stepsPerBar() != 0) {
                p.at(to) = p.at(s);
                p.at(s) = Hit{};
                if (onBeat(p, to))
                   p.at(to).push = 0; // a beat isn't anticipated
                return;
             }
          }
       }},
      {"ghosts", [](Pattern &p, Rng &rng) {
          // Ghosted chops between the hits: on the sixteenths, or the offbeat eighths of an
          // eighth-note rhythm.
          const bool fine = fineGrid(p);
          for (int s = 0; s < p.steps; ++s)
             if (!p.at(s).on() && (fine ? !onEighth(p, s) : onEighth(p, s) && !onBeat(p, s)) && rng.chance(fine ? 0.3 : 0.15))
                p.at(s) = hitOf('g');
       }},
      {"top line", [](Pattern &p, Rng &rng) {
          for (int s = 0; s < p.steps; ++s)
             if (p.at(s).on() && p.at(s).part == kPartChord && !onBeat(p, s) && rng.chance(0.5))
                p.at(s).part = rng.chance(0.6) ? kPartTop : kPartUpper;
       }},
      {"accents", [](Pattern &p, Rng &rng) {
          for (int s = 0; s < p.steps; ++s) {
             Hit &h = p.at(s);
             if (!h.on() || h.vel == kGhost)
                continue;
             if (h.vel >= kAccent && rng.chance(0.4))
                h.vel = static_cast<uint8_t>(std::lround(h.vel * 0.82));
             else if (!onBeat(p, s) && rng.chance(0.3))
                h.vel = kAccent; // (no louder than the take's own peak: mutate() caps it)
          }
       }},
      {"run in", [](Pattern &p, Rng &) {
          for (int s = 0; s < p.steps; s += p.stepsPerBar())
             if (p.at(s).on() && p.at(s).part == kPartChord)
                p.at(s).run = 1;
       }},
      {"answer", [](Pattern &p, Rng &rng) {
          // the second bar answers the first: the same, with its ending changed. A rhythm written
          // over two bars (a clave's two sides, the bossa cycle) already answers itself: copying its
          // first bar over the second would cross the clave.
          const int bar = p.stepsPerBar();
          if (p.steps != bar * 2)
             return; // one bar, or a four-bar phrase that already turns itself round
          for (int s = 0; s < bar; ++s)
             if (p.at(bar + s).on() != p.at(s).on())
                return;
          const bool fine = fineGrid(p);
          for (int s = 0; s < bar; ++s)
             p.at(bar + s) = p.at(s);
          for (int s = bar + bar / 2; s < bar * 2; ++s)
             if (!onBeat(p, s) && addable(p, s, fine) && rng.chance(0.4)) {
                // a line or left-hand note stays as written: only the chord's hits answer
                if (p.at(s).on() && (p.at(s).part == kPartLine || leftHand(p.at(s).part)))
                   continue;
                p.at(s) = p.at(s).on() ? Hit{} : hitOf('x');
             }
       }},
      {"left hand", [](Pattern &p, Rng &) {
          for (int s = 0; s < p.steps; s += strongSteps(p))
             if (!p.at(s).on())
                p.at(s) = hitOf('b');
       }},
   };
   return list;
}

// Applies a named mutation; false if there isn't one by that name.
inline bool applyMutation(Pattern &p, const char *tag, Rng &rng) {
   for (const Mutation &m : mutations())
      if (std::strcmp(m.tag, tag) == 0) {
         const Pattern before = p;
         m.apply(p, rng);
         if (p.empty())
            p.at(0) = hitOf('x');
         capAdded(p, before);
         return true;
      }
   return false;
}

// Whether a mutation suits a style: a fixed figure only changes its accents and lengths (never
// ringing if it is chopped short); a left hand is added only where the style has one.
inline bool mutationFits(const char *tag, unsigned mods, const Pattern &parent, const char *genre = nullptr) {
   const auto is = [&](const char *t) { return std::strcmp(tag, t) == 0; };
   // Funk is on the One: nothing pushed over the bar line.
   if (genre && std::strcmp(genre, "Funk") == 0 && is("push it"))
      return false;
   if (mods & kFixed)
      // (Not chopped: a figure that is the style keeps its lengths, Freddie Green's even four and the
      // bossa's fingers as much as a montuno.)
      return is("accents") || (is("let it ring") && !(mods & kShorts));
   // A skank is choked short: it never rings.
   if (mods & kShorts && is("let it ring"))
      return false;
   // (Not into an offbeat take: the organ bubble's left hand stays off the beats.)
   if (is("left hand"))
      return ((mods & kLeftHand) || hasLeftHand(parent)) && !offbeatTake(parent);
   // Held chords stay held: no chops, no ghosts between them, nothing moved off the beat.
   if (mods & kLongs && (is("chop it") || is("busier") || is("ghosts") || is("shift") || is("answer")))
      return false;
   // An ambient drift's held chords aren't struck again an eighth early.
   if (genre && is("push it"))
      for (int i = 0; i < genreCount(); ++i)
         if (std::strcmp(genreAt(i).name, genre) == 0 && genreAt(i).family == kFamilies[6])
            return false;
   // Nor a take that holds its chords (Held Comp): chopped, it would be another style's stabs.
   if (is("chop it") && heldTake(parent))
      return false;
   // An offbeat take (a skank, a one drop) keeps its strokes where they are, choked: nothing pushed
   // ahead, shifted or rung.
   if (offbeatTake(parent) && (is("push it") || is("shift") || is("let it ring")))
      return false;
   // A line (the Afrobeat tenor's ostinato) plays as written: nothing taken out, moved or ghosted.
   if (hasLine(parent) && (is("sparser") || is("shift") || is("ghosts") || is("chop it") || is("busier") || is("push it") ||
                           is("answer")))
      return false;
   return true;
}

// A child of a pattern: one to three mutations, more with more `amount`, of those that suit the
// style (`mods`). Its tag names them.
// A take whose rhythm has a chord on a beat keeps one after DENSITY's energy, WEIRD or a mutation
// thin it (the 60s R&B's beat 3): its first in each bar comes back. Losing them all would make it an
// offbeat style (a skank's), whose changes and fills wait for an upbeat.
inline void keepBeatChord(const Pattern &before, Pattern &p) {
   if (!chordOnBeat(before) || chordOnBeat(p) || before.steps != p.steps || before.rate != p.rate)
      return;
   const int bar = p.stepsPerBar(), beat = p.perBeat();
   // At the take's level, not as written: a quiet section's chord comes back as quiet as the rest.
   int loudest = 0;
   for (int s = 0; s < p.steps; ++s)
      if (p.at(s).on())
         loudest = std::max<int>(loudest, p.at(s).vel);
   for (int from = 0; from < p.steps; from += bar)
      for (int s = from; s < from + bar && s < p.steps; s += beat)
         if (before.at(s).on() && !leftHand(before.at(s).part)) {
            p.at(s) = before.at(s);
            if (loudest > 0)
               p.at(s).vel = static_cast<uint8_t>(std::min<int>(p.at(s).vel, loudest));
            break;
         }
}

inline Pattern mutate(const Pattern &parent, double amount, Rng &rng, std::string &tag, unsigned mods = 0,
                      const char *genre = nullptr) {
   Pattern p = parent;
   const int n = 1 + static_cast<int>(amount * 2.0 + rng.uniform() * 0.7);
   tag.clear();
   const auto &list = mutations();
   std::array<int, 16> fit{};
   int fits = 0;
   for (int i = 0; i < static_cast<int>(list.size()) && fits < 16; ++i)
      if (mutationFits(list[static_cast<size_t>(i)].tag, mods, parent, genre))
         fit[static_cast<size_t>(fits++)] = i;
   int lastPick = -1;
   for (int i = 0; i < n && fits > 0; ++i) {
      int k = rng.range(0, fits - 1);
      if (fit[static_cast<size_t>(k)] == lastPick && fits > 1)
         k = (k + 1) % fits;
      const int pick = fit[static_cast<size_t>(k)];
      lastPick = pick;
      list[static_cast<size_t>(pick)].apply(p, rng);
      if (!tag.empty())
         tag += " + ";
      tag += list[static_cast<size_t>(pick)].tag;
   }
   if (p.empty())
      p.at(0) = hitOf('x');
   // The style's articulation over what was added or changed: a choked style's chords stay chopped (a
   // busy scratch's accents excepted), a held style's ring.
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (!h.on() || leftHand(h.part) || h.part == kPartLine || h.len > 1 || (s < parent.steps && parent.at(s) == h))
         continue;
      if (mods & kShorts && (h.art == kArtAsWritten || h.art == kArtRing || h.art == kArtTopRings) &&
          !((mods & kBusy) && h.vel >= kAccent * 9 / 10))
         h.art = kArtChop;
      else if (mods & kLongs && h.art == kArtAsWritten)
         h.art = kArtRing;
   }
   keepBeatChord(parent, p);
   // A take bred from another is no louder than it: an added stab or a new accent comes up to the
   // parent's loudest chord at most (a quiet section's takes stay quiet).
   const int peak = chordPeak(parent);
   if (peak > 0)
      for (int s = 0; s < p.steps; ++s) {
         const int was = s < parent.steps && parent.at(s).on() ? parent.at(s).vel : 0;
         if (p.at(s).on() && p.at(s).vel > std::max(peak, was))
            p.at(s).vel = static_cast<uint8_t>(std::max(peak, was));
      }
   return p;
}

// ---- sections and songs ----

inline Pattern baseRhythm(const Recipe &r, int section, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   const double e = sectionEnergy(r, section);
   const auto list = rhythmsFor(g, st);
   const Rhythm *rh = pickRhythm(list, e, rng);
   Pattern p = rh ? fromRhythm(*rh) : st.mods & kWaltz ? fromRhythm("x-----------", styleRate(st.mods)) : fromRhythm("x...............");
   // Interlocking parts: the style's other rhythms woven into the steps this one leaves empty.
   if (st.mods & kInterlock)
      for (const Rhythm *o : list) {
         if (o == rh)
            continue;
         const Pattern q = fromRhythm(*o);
         if (q.rate != p.rate)
            continue;
         for (int s = 0; s < p.steps; ++s)
            if (!p.at(s).on() && q.at(s % q.steps).on())
               p.at(s) = q.at(s % q.steps);
      }
   // A fixed figure (a clave's guajeo, Freddie Green's four, the batida) is the style: nothing is
   // blended into it.
   if (r.genre2 >= 0 && r.genre2 != r.genre && r.blend > 0.0 && !(st.mods & kFixed)) {
      const Genre &g2 = genreAt(r.genre2);
      Style st2 = styleAt(g2, 0);
      st2.mods = st.mods & (kTriplet | kWaltz | kSixEight);
      st2.rhythms = 0;
      // Only from the same number of beats a bar: a 4/4 rhythm doesn't go into a waltz.
      if (const Rhythm *r2 = pickRhythm(rhythmsFor(g2, st2), e, rng); r2 && (rhythmRate(*r2) == p.rate || (!p.inThree() && rhythmRate(*r2) != kRate34 && rhythmRate(*r2) != kRate68)))
         blendInto(p, fromRhythm(*r2), r.blend * 0.6, rng);
   }
   return p;
}

// A take's last bar, as a pattern of its own (its first bar when it has one).
inline Pattern lastBar(const Pattern &p) {
   Pattern f = p;
   const int bar = p.stepsPerBar();
   const int from = p.steps > bar ? (p.steps / bar - 1) * bar : 0;
   for (int s = 0; s < kMaxSteps; ++s)
      f.at(s) = s < bar && s + from < p.steps ? p.at(s + from) : Hit{};
   f.steps = static_cast<uint8_t>(std::min<int>(p.steps, bar));
   return f;
}
inline Pattern makeFill(const Recipe &r, const Pattern &base, double e, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   // A fixed figure's fill is the figure, its last stroke leaning into the next section: a montuno
   // keeps its clave (the engine plays the side the bar falls on), Freddie Green his four. It
   // anticipates the next chord only from half a beat or less before the bar line; earlier, it's an
   // accent.
   if (st.mods & kFixed) {
      Pattern f = base;
      const int half = std::max(1, perBeat(f) / 2);
      for (int s = f.steps - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            // (Over the take's own peak, at its level: not a raw accent in a quiet section.)
            f.at(s).vel = static_cast<uint8_t>(std::min(127, std::max<int>(f.at(s).vel, chordPeak(base) + 12)));
            const int toEnd = f.stepsPerBar() - s % f.stepsPerBar();
            // (Only a style that anticipates: the cha-cha's and the bell's last stroke stays on its chord.)
            f.at(s).push = ((st.mods & kPushes) || g.pushAll) && toEnd <= half && !onBeat(f, s) ? 1 : 0;
            break;
         }
      return f;
   }
   // A held-chord style's fill holds too: its own bar, rung, swelling into the next with a chord
   // pushed on the last eighth (a pad doesn't chop eighths for a fill).
   if (st.mods & kLongs) {
      Pattern f = unscaled(lastBar(base), e);
      f.steps = static_cast<uint8_t>(f.stepsPerBar());
      for (int s = f.steps; s < kMaxSteps; ++s)
         f.at(s) = Hit{};
      const int bar = f.stepsPerBar();
      // Ambient's drift holds on through its fill: the bar as it is, still ringing (a chord held over
      // two bars sounds whole), with no stutter of the old chord before the change.
      const bool drift = g.family == kFamilies[6];
      if (drift) {
         for (int s = 0; s < bar; ++s)
            if (f.at(s).on() && !leftHand(f.at(s).part))
               f.at(s).art = kArtRing;
         // (A bar that only holds the chord before it has no fill: the take plays on, the chord whole.)
         if (f.empty())
            return Pattern{};
         shapeEnergy(f, std::max(0.35, e), rng, true, st.mods);
         return f;
      }
      if (!f.at(0).on())
         f.at(0) = hitOf('x'); // the fill bar's chord from its downbeat
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !leftHand(f.at(s).part))
            f.at(s).art = kArtRing;
      // (Never over the left hand's note: a broken-chord ballad's fifth rocks on into the change.)
      int last = bar - (f.rate == kRate8T ? 1 : 2);
      if (f.at(last).on() && leftHand(f.at(last).part) && !f.at(bar - 1).on() && fineGrid(base))
         last = bar - 1;
      const auto chordOn = [&](int s) { return f.at(s).on() && !leftHand(f.at(s).part); };
      if (!chordOn(last) && !chordOn(last - 1) && !f.at(last).on()) {
         f.at(last) = hitOf('x');
         f.at(last).art = kArtRing;
      }
      // The last chord, off the beat, pushed into the next (a chord on a beat stays where it is).
      for (int s = bar - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            if (!onBeat(f, s))
               f.at(s).push = 1;
            break;
         }
      shapeEnergy(f, std::max(0.35, e), rng, true, st.mods);
      return f;
   }
   const bool fine = fineGrid(base);
   Pattern f;
   const auto fills = rhythmsFor(g, st, true);
   // The genre's fill is its own player's (Chic's chucking, the palm-wine thumb): a style played by
   // another (a pad, keys in a guitar genre) fills from its own take.
   // It's also only the take's own instrument part: a fill with a thumb's bass needs a take with a
   // left hand, and a line (the tenor line, a riff) fills from its own take.
   const bool genreFill = (st.player < 0 || st.player == g.player) && !hasLine(base);
   const Rhythm *rh = genreFill ? pickRhythm(fills, e, rng) : nullptr;
   if (rh && (rhythmRate(*rh) != base.rate || (hasLeftHand(fromRhythm(*rh)) && !hasLeftHand(base))))
      rh = nullptr;
   if (rh)
      f = fromRhythm(*rh);
   else {
      f = unscaled(lastBar(base), e); // the bar it takes the place of: a two-bar riff's answer, not its start
      const int bar = f.stepsPerBar();
      if (!hasLine(base))
         for (int s = bar - perBeat(f); s < bar; ++s)
            if (addable(f, s, fine) && !(f.at(s).on() && leftHand(f.at(s).part))) // the left hand plays on
               f.at(s) = hitOf('x');
      // Still the groove (its last beat was full already, or nothing could go there): the last beat
      // rests and its "and" leads in, accented, so the bar before the change is heard as one.
      const Pattern was = lastBar(base);
      bool same = true;
      for (int s = 0; s < bar && same; ++s)
         same = f.at(s).on() == was.at(s).on();
      if (same && !hasLine(base) && !offbeatTake(base) && f.rate != kRate8T && bar - perBeat(f) / 2 > 0) {
         for (int s = bar - perBeat(f); s < bar; ++s)
            if (f.at(s).on() && !leftHand(f.at(s).part))
               f.at(s) = Hit{};
         const int lead = bar - perBeat(f) / 2;
         if (!f.at(lead).on())
            f.at(lead) = hitOf('X');
      }
   }
   // A fill is one bar.
   f.steps = static_cast<uint8_t>(f.stepsPerBar());
   for (int s = f.steps; s < kMaxSteps; ++s)
      f.at(s) = Hit{};
   const int bar = f.stepsPerBar();
   // On the take's grid: a hit between the eighths of an eighth-note take moves to the eighth before.
   // (Not a 12/8 fill of the genre's own: its middle triplets are eighth notes, every triplet, not a
   // shuffle's.)
   if (!fine && !(rh && f.rate == kRate8T))
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !onEighth(f, s)) {
            // Onto the eighth before; where that has a hit, the louder of the two stays.
            if (!f.at(s - 1).on() || f.at(s).vel > f.at(s - 1).vel)
               f.at(s - 1) = f.at(s);
            f.at(s) = Hit{};
         }
   // A style that anticipates its changes does in its fill too (Garland's and of 2 into the bar's
   // second chord).
   if ((st.mods & kPushes) || g.pushAll)
      for (int s = 0; s < bar; ++s) {
         Hit &h = f.at(s);
         const bool at = f.rate == kRate8T    ? (s == bar - 1 || s == bar / 2 - 1)
                         : f.rate == kRate34 ? (s == bar - 1 || s == bar - 2)
                                             : (s == bar - 1 || s == bar - 2 || s == bar / 2 - 1 || s == bar / 2 - 2);
         if (at && h.on() && !onBeat(f, s) && !leftHand(h.part) && h.part != kPartLine)
            h.push = 1;
      }
   // An offbeat style's fill stays off the beats (a one drop's off 1 and 3).
   for (int s = 0; s < bar; s += perBeat(f))
      if (f.at(s).on() && !leftHand(f.at(s).part) && keepsOff(base, s))
         f.at(s) = Hit{};
   // The left hand plays on through the fill: the bar's it takes the place of.
   if (hasLeftHand(base)) {
      const int n = std::max<int>(1, base.steps), from = std::max(0, n - bar);
      for (int s = 0; s < bar; ++s)
         if (!f.at(s).on() && base.at((from + s) % n).on() && leftHand(base.at((from + s) % n).part))
            f.at(s) = base.at((from + s) % n);
   }
   // The fill's last hit anticipates the next section's chord (not in an offbeat take, where an
   // upbeat chop is the chord it's on, nor in funk, where the next section lands on the One).
   int pushed = -1;
   // (Only an offbeat leads in: a fill left with one downbeat chord anticipates nothing.)
   if (!keepsOff(base, 0) && std::strcmp(g.name, "Funk") != 0)
      for (int s = bar - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            // (Not a line's note: a riff plays as written, its last note the bar's own.)
            if (!onBeat(f, s) && f.at(s).part != kPartLine) {
               f.at(s).push = 1;
               pushed = s;
            }
            break;
         }
   // (Chopped as the take is: a busy scratch's accents still ring.)
   if (st.mods & kShorts)
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && f.at(s).len <= 1 && !leftHand(f.at(s).part) &&
             !((st.mods & kBusy) && f.at(s).vel >= kAccent * 9 / 10))
            f.at(s).art = kArtChop;
   const Hit lead = pushed >= 0 ? f.at(pushed) : Hit{};
   shapeEnergy(f, std::max(0.35, e), rng, false, st.mods, swungEighths(g, st));
   // The anticipation into the next section stays, whatever the energy took out.
   if (pushed >= 0 && !f.at(pushed).on())
      f.at(pushed) = lead;
   // The gospel shout's offbeats over its left hand, its fill come out as its groove (its beat
   // chords give way to the bass), hit every offbeat hard: the praise break's push into the next.
   // (Only the shout's: a boom-chick, a waltz or an organ bubble doesn't accent its weak beats.)
   // The fill is made from the take's last bar, so it's that bar it's compared with.
   bool same = true;
   {
      const int n = std::max<int>(1, base.steps), from = std::max(0, n - bar);
      for (int s = 0; s < bar && same; ++s)
         same = f.at(s).on() == base.at((from + s) % n).on();
   }
   // (The shout's fill always: its genre fill adds a lead-in chord the groove doesn't have.)
   if (offbeatTake(base) && hasLeftHand(base) && std::strcmp(g.name, "Gospel") == 0) {
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !leftHand(f.at(s).part))
            f.at(s).vel = kAccent;
   } else if (same && offbeatTake(base) && !hasLeftHand(base)) {
      // Any other fill that came out as the groove (a skank's, an offbeat stab's: moved onto the
      // eighths and kept off the beats) still leads into the next section: its last two chops
      // accented, and a pickup after the last (the triplet before the bar line, or a sixteenth
      // later), or else the last chop pushed into the next chord.
      int last = -1, prev = -1;
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            prev = last;
            last = s;
         }
      // (sure and on time: no WEIRD chance or nudge on the lead-in)
      for (int s : {prev, last})
         if (s >= 0) {
            f.at(s).vel = kAccent;
            f.at(s).chance = 100;
            f.at(s).nudge = 0;
         }
      const int pick = f.rate == kRate8T ? bar - 1 : last + 1;
      if (last >= 0 && (f.rate == kRate8T || fine) && pick > last && pick < bar && !f.at(pick).on()) {
         f.at(pick) = f.at(last);
         f.at(pick).vel = kNormal;
         f.at(pick).push = 1;
         f.at(last).push = 0;
      } else if (last >= 0 && !onBeat(f, last))
         f.at(last).push = 1;
   }
   // A fill leads into the next section: never under the groove's own accents. Its first and last
   // chords come up to the take's loudest.
   int peak = 0, fPeak = 0, first = -1, lastHit = -1;
   for (int s = 0; s < base.steps; ++s)
      if (base.at(s).on() && !leftHand(base.at(s).part))
         peak = std::max<int>(peak, base.at(s).vel);
   for (int s = 0; s < bar; ++s)
      if (f.at(s).on() && !leftHand(f.at(s).part)) {
         fPeak = std::max<int>(fPeak, f.at(s).vel);
         if (first < 0)
            first = s;
         lastHit = s;
      }
   if (first >= 0 && fPeak < peak)
      for (int s : {first, lastHit})
         f.at(s).vel = static_cast<uint8_t>(std::max<int>(f.at(s).vel, peak));
   // Funk's fill is a pickup: the next section's One is struck, not tied over.
   if (std::strcmp(g.name, "Funk") == 0)
      for (int s = 0; s < bar; ++s)
         f.at(s).push = 0;
   // The same instrument as the take: an organ holds its fill's chords to the next, as it does the
   // groove's (applyPlayer's rule; a pedalled or ringing player's fill stays as written, punchier).
   if (playerOf(r).instrument == kOrgan && !(st.mods & kShorts))
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !leftHand(f.at(s).part) && f.at(s).part != kPartLine && f.at(s).len <= 1 &&
             f.at(s).art == kArtAsWritten)
            f.at(s).art = kArtRing;
   return f;
}

// A figure written for a fill down or an ending, on the take's grid: a section's own (a genre's), or
// the generic one.
// Where a take's chords fall: only on the ands (a ska chop, a house stab), or only on 2 and 4 (a
// backbeat, a one drop's skank). An ending or a fill down stays there.
inline bool chordsOnly(const Pattern &p, bool ands) {
   const int beat = p.perBeat(), bar = p.stepsPerBar();
   const int and_ = p.rate == kRate8T ? 2 : beat / 2;
   int n = 0;
   for (int s = 0; s < p.steps; ++s) {
      const Hit &h = p.at(s);
      if (!h.on() || leftHand(h.part))
         continue;
      ++n;
      const int in = s % bar;
      // (The ands: every chord off the beat, on its and or its "a": a ska upstroke's chops too.)
      if (ands ? (in % beat == 0 || (p.rate == kRate8T && in % beat != and_)) : (in != beat && in != 3 * beat))
         return false;
   }
   return n > 0;
}
inline const char *downText(const Genre &g, const Pattern &base, unsigned mods) {
   const bool ands = base.stepsPerBar() >= 12 && base.rate != kRate34 && base.rate != kRate68 && chordsOnly(base, true);
   const bool off = !ands && (keepsOff(base, 0) || (base.rate == kRate16 && chordsOnly(base, false)));
   if (ands)
      return base.rate == kRate8T ? "..x-----...." : "..x-----........"; // the and of 1, held
   if (base.rate == kRate8T)
      return off ? "...x-----..." : mods & kLongs ? "x--------..." : "x-----......";
   if (base.rate == kRate34)
      return off ? "....x---...." : mods & kLongs ? "x--------..." : "x-------....";
   if (base.rate == kRate68) // on the dotted quarters
      return off ? "..x-----...." : mods & kLongs ? "x---------.." : "x-----......";
   if (off)
      return "....x-------....";
   if (mods & kLongs)
      return "x-----------....";
   const std::string n = g.name;
   // MMA's SwingFill (two half notes); its SonFill (1 and 3) for the montuno genres.
   if (n == "Swing Jazz")
      return "x-------x-------";
   if (n == "Salsa" || n == "Latin Jazz")
      return "x.......x.......";
   return "x-------........";
}
inline const char *endingText(const Genre &g, const Pattern &base, unsigned mods, const char *style = "") {
   const bool ands = base.rate != kRate34 && base.rate != kRate68 && chordsOnly(base, true);
   const bool off = !ands && (keepsOff(base, 0) || (base.rate == kRate16 && chordsOnly(base, false)));
   if (ands && !(mods & kLongs)) // the chops on the ands, then the last chord held
      return base.rate == kRate8T ? "..x..x..x..x|X-----------" : "..x...x...x...x.|X---------------";
   if (base.rate == kRate8T)
      return mods & kLongs ? "X-----------|------------" : off ? "...x.....x..|X-----------" : "x..x..x..x..|X-----------";
   if (base.rate == kRate34)
      return mods & kLongs ? "X-----------|------------" : "x...x...x...|X-----------";
   if (base.rate == kRate68) // 1 and 4, then the last chord held
      return mods & kLongs ? "X-----------|------------" : "x.....x.....|X-----------";
   if (mods & kLongs)
      return "X---------------|----------------";
   if (off)
      return "....x.......x...|X---------------";
   const std::string n = g.name;
   if (n == "Swing Jazz") // SwingEnd: four, then 1 and 3
      return "x...x...x...x...|X.......x-------";
   // The bolero broadens into its last chord (MMA's BoleroEnd), on its tresillo: no dance tag.
   if (std::strcmp(style, "Bolero") == 0)
      return "x-----x-----x...|X---------------";
   if (n == "Salsa" || n == "Latin Jazz") // the cha-cha cell (4, and, 1), the last chord held from the 1
      return "x...x...x...x.x.|X---------------";
   if (g.family == kFamilies[0]) // MMA's R&B ending: 1, 3 and 4, then the one held two beats
      return "x.......x...x...|X-------........";
   return "x...x...x...x...|X---------------";
}
// Scaled to an energy, as a take's velocities are; nothing pushed.
inline Pattern figure(const char *text, uint8_t rate, double e) {
   Pattern f = fromRhythm(text, rate);
   const double scale = 0.82 + 0.3 * e;
   for (int s = 0; s < f.steps; ++s) {
      Hit &h = f.at(s);
      h.push = 0;
      if (h.on())
         h.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(h.vel * scale)), 20, 127));
   }
   return f;
}
// The fill down, into a quieter section: a held stab with the next downbeat left clear (MMA's
// SwingFill, SonFill; a Yamaha arranger's fill from B down to A). No anticipation: the next section
// comes in on its one. The intro's is the held chord setting up the verse.
inline Pattern makeFillDown(const Recipe &r, int section, const Pattern &base, double e) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   const char *text = section == kIntro ? (base.rate == kRate8T ? "X-----......" : base.rate == kRate34 ? "X-------...." : base.rate == kRate68 ? "X---------.." : "X-------........")
                                        : downText(g, base, st.mods);
   if (section == kIntro && keepsOff(base, 0))
      text = downText(g, base, 0);
   // A held style's set-up holds to the bar's last eighth, at its own level (a pad doesn't stab).
   if (section == kIntro && (st.mods & kLongs) && !keepsOff(base, 0))
      text = base.rate == kRate8T ? "x---------.." : base.rate == kRate34 ? "x---------.." : base.rate == kRate68 ? "x---------.." : "x-------------..";
   // A fixed figure settles as it fills: the figure itself (Freddie Green keeps his four, a montuno its
   // clave, the side the bar falls on), its pushes as written (a montuno's and of 4 is the next
   // section's chord), its last stroke rung.
   if ((st.mods & kFixed) && section != kIntro) {
      Pattern f = base;
      for (int s = f.steps - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            f.at(s).art = kArtRing;
            break;
         }
      return f;
   }
   Pattern f = figure(text, base.rate, std::max(0.35, e));
   // Settling into the quieter section: never above the groove it leaves, not the loudest stab of
   // the song (MMA's fills sit at the groove's volume). The intro's set-up is no louder than the
   // intro either.
   {
      int peak = 0;
      for (int s = 0; s < base.steps; ++s)
         if (base.at(s).on() && !leftHand(base.at(s).part))
            peak = std::max<int>(peak, base.at(s).vel);
      for (int s = 0; s < f.steps; ++s)
         if (f.at(s).on() && peak > 0)
            f.at(s).vel = static_cast<uint8_t>(std::min<int>(f.at(s).vel, peak));
   }
   f.steps = static_cast<uint8_t>(f.stepsPerBar());
   for (int s = f.steps; s < kMaxSteps; ++s)
      f.at(s) = Hit{};
   // A left hand keeps its downbeat under a chord that leaves it (boom-chick's bass on the one), where
   // the take's left hand plays one (an organ bubble's never does: the one is dropped).
   if (hasLeftHand(base) && !f.at(0).on() && base.at(0).on() && leftHand(base.at(0).part)) {
      f.at(0) = base.at(0).on() && leftHand(base.at(0).part) ? base.at(0) : hitOf('b');
      f.at(0).art = kArtRing;
      f.at(0).push = 0;
      for (int s = 0; s < base.steps; ++s) // no louder than the groove's own bass
         if (base.at(s).on() && leftHand(base.at(s).part))
            f.at(0).vel = std::min(f.at(0).vel, base.at(s).vel);
   }
   return f;
}
// The song's ending (the outro's last bar or two): the last chord rung out, nothing anticipated.
inline Pattern makeEnding(const Recipe &r, const Pattern &base, double e) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   Pattern f = figure(endingText(g, base, st.mods, st.name), base.rate, std::max(0.35, e));
   // A montuno's or a guajeo's changes come on the and of 4: its last chord with them, held (an
   // anticipated last hit). The cha-cha's cell ends on the 1 ("chá"), and a bolero on a held downbeat.
   {
      const std::string n = g.name, sn = st.name;
      const bool anticipates = n == "Salsa" || (n == "Latin Jazz" && sn != "Cha-Cha" && sn != "Bolero");
      if (anticipates && base.rate == kRate16 && f.steps == 32 && f.at(14).on() && f.at(16).on()) {
         f.at(14) = f.at(16);
         f.at(14).push = 1;
         f.at(14).len = 18;
         f.at(16) = Hit{};
      }
   }
   // A house riff ends on its own last bar, then the held chord (four block quarters is a ballad's
   // cadence); an offbeat stab keeps the ending it has.
   if (std::string(g.name).find("House") != std::string::npos && !keepsOff(base, 0) && base.rate == kRate16 &&
       f.steps > f.stepsPerBar()) {
      const Pattern last = lastBar(base);
      const int bar = f.stepsPerBar();
      for (int s = 0; s < bar; ++s) {
         f.at(s) = last.at(s);
         f.at(s).push = 0;
      }
   }
   // A bossa carries its batida into the held chord, not a bar of block quarters: the figure's own
   // bar: the cycle's first, which falls on the outro's second-last bar (an even bar of an even
   // section), so the cycle isn't broken by two second bars in a row.
   if ((st.mods & kFixed) && std::string(g.name) == "Bossa Nova" && base.rate == kRate16 && f.steps > f.stepsPerBar()) {
      const int bar = f.stepsPerBar();
      for (int s = 0; s < bar; ++s) {
         f.at(s) = s < base.steps ? base.at(s) : Hit{};
         f.at(s).push = 0;
      }
   }
   // The left hand plays on to the last chord (a boom-chick tag keeps its bass alternating), but
   // not under an ending held from its first bar (the held chord is the end).
   if (hasLeftHand(base) && f.steps > f.stepsPerBar() && !(st.mods & kLongs))
      for (int s = 0; s < f.stepsPerBar(); ++s)
         if (!f.at(s).on() && s < base.steps && base.at(s).on() && leftHand(base.at(s).part))
            f.at(s) = base.at(s);
   // A style that holds its chords holds them in its ending too: the strokes before the last ring to
   // the next (the generic figures write one-step hits, a chop in a ballad).
   {
      if (!(st.mods & kShorts) && ((st.mods & kLongs) || heldTake(base)))
         for (int s = 0; s < f.steps; ++s)
            if (f.at(s).on() && !leftHand(f.at(s).part) && f.at(s).len <= 1 && f.at(s).art == kArtAsWritten)
               f.at(s).art = kArtRing;
   }
   // The last chord rings (a left hand's note after it doesn't take its place), the loudest of the
   // ending: not a softer chord after an accent, which sounds like a pickup, not the end.
   const uint8_t loudest = static_cast<uint8_t>(std::max<int>(chordPeak(f), kNormal));
   for (int s = f.steps - 1; s >= 0; --s)
      if (f.at(s).on() && !leftHand(f.at(s).part)) {
         f.at(s).art = kArtRing;
         f.at(s).vel = std::max<uint8_t>(f.at(s).vel, loudest);
         break;
      }
   return f;
}

inline Section makeSection(const Recipe &r, int section, uint32_t seed) {
   Section s;
   s.seed = seed;
   Rng rng(mix(seed, static_cast<uint32_t>(section) * 977u + 13u));
   const Style &st = styleOf(r);
   const double e = sectionEnergy(r, section);
   s.energy = static_cast<uint8_t>(std::lround(e * 100.0));
   makeChords(r, section, s, rng);
   int bars = kSectionBars[section];
   if (bars % s.progBars)
      bars = (bars / s.progBars + 1) * s.progBars;
   s.bars = static_cast<uint8_t>(std::clamp(bars, 1, kMaxBars));
   // The song's last bar holds its home chord: no approach chord (WEIRD's) into a section that
   // never comes.
   if (section == kOutro)
      s.split = static_cast<uint16_t>(s.split & ~(1u << ((s.bars - 1) % std::max<int>(1, s.progBars))));
   Pattern p = baseRhythm(r, section, rng);
   // A genre that takes every anticipation (swing) does in every style, not only those that push.
   // A fixed figure gains no pushes it lacks (the cha-cha), but takes every one it has (a montuno's
   // and of 4 always anticipates: the old chord an eighth before the change is wrong).
   const bool all = genreAt(r.genre).pushAll;
   const unsigned pushAll = all && !(st.mods & kFixed) ? kPushes : 0u;
   applyMods(p, st.mods | pushAll, rng, all ? 1.0 : 0.75);
   const Pattern written = p;
   shapeEnergy(p, e, rng, st.mods & kFixed, st.mods, swungEighths(genreAt(r.genre), st));
   applyWeird(p, r.weird, rng, st.mods);
   keepBeatChord(written, p);
   applyPlayer(p, playerOf(r), st.mods, rng);
   s.takes[0] = p;
   for (int t = 1; t < kTakes; ++t) {
      std::string tag;
      s.takes[static_cast<size_t>(t)] = mutate(p, 0.15 + 0.15 * t + r.weird * 0.3, rng, tag, st.mods, genreAt(r.genre).name);
   }
   s.fill = makeFill(r, p, e, rng);
   s.fillDown = makeFillDown(r, section, p, e);
   if (section == kOutro)
      s.ending = makeEnding(r, p, e);
   return s;
}

// A song saved before 0.6.0 (a version below 3) has no fills down and no ending: they're made for it
// from its takes, as a new song's are.
inline void completeSong(const Recipe &r, Song &song, const std::vector<uint8_t> &saved) {
   if (saved.size() < 4)
      return;
   const uint32_t version = saved[0] | saved[1] << 8 | saved[2] << 16 | static_cast<uint32_t>(saved[3]) << 24;
   // A 6/8 song saved by 0.6.x (version 3, with a 3/4 grid) was written on the 3/4 grid: the same ticks, so
   // it moves to the 6/8 rate (two dotted-quarter beats) as it is.
   if (version < 4 && styleOf(r).mods & kSixEight)
      for (Section &s : song.sections) {
         for (Pattern &p : s.takes)
            if (p.rate == kRate34)
               p.rate = kRate68;
         for (Pattern *p : {&s.fill, &s.fillDown, &s.ending})
            if (p->rate == kRate34)
               p->rate = kRate68;
      }
   if (version >= 3)
      return;
   for (int i = 0; i < kSections; ++i) {
      Section &s = song.sections[static_cast<size_t>(i)];
      const double e = s.energy / 100.0;
      if (s.fillDown.empty())
         s.fillDown = makeFillDown(r, i, s.takes[0], e);
      if (i == kOutro && s.ending.empty())
         s.ending = makeEnding(r, s.takes[0], e);
   }
}

inline int beatsOf(const Recipe &r) { return styleOf(r).mods & kWaltz ? 3 : 4; }

// With `keepChords` (a new ENERGY or BLEND: rhythm only), every section keeps its chords as edited,
// their band hits and its length; its rhythm is made again.
inline void generateSong(const Recipe &r, Song &song, bool keepChords = false) {
   const int key = ((r.key % 12) + 12) % 12;
   // A locked section keeps its chords, moved to the new key with the rest of the song.
   const int move = (key - song.key + 12) % 12;
   song.key = static_cast<uint8_t>(key);
   // A new meter: even a locked section's rhythm is made again (its chords stay).
   // 3/4 and 6/8 both have three beats here, but a waltz's three quarters are not a 6/8's two dotted
   // quarters: a section whose grid is the other one is a new meter too.
   const bool beatsMoved = song.beats != beatsOf(r);
   const bool sixEight = styleRate(styleOf(r).mods) == kRate68;
   // (And 4/4 in sixteenths and 12/8 both have four beats: the triplet grid is a meter of its own.)
   const bool twelveEight = styleRate(styleOf(r).mods) == kRate8T;
   song.beats = static_cast<uint8_t>(beatsOf(r));
   for (int i = 0; i < kSections; ++i) {
      Section &s = song.sections[static_cast<size_t>(i)];
      if (s.locked) {
         for (Chord &c : s.chords)
            if (!c.none()) {
               c.root = static_cast<int8_t>((c.root + move) % 12);
               if (c.bass >= 0)
                  c.bass = static_cast<int8_t>((c.bass + move) % 12);
            }
         if (beatsMoved || (s.takes[0].rate == kRate68) != sixEight || (s.takes[0].rate == kRate8T) != twelveEight) {
            Section made = makeSection(r, i, s.seed);
            made.chords = s.chords;
            made.split = s.split;
            made.marks = s.marks;
            made.progBars = s.progBars;
            made.bars = s.bars;
            made.locked = s.locked;
            made.generation = static_cast<uint16_t>(s.generation + 1);
            s = made;
         }
         continue;
      }
      Section made = makeSection(r, i, mix(r.seed, static_cast<uint32_t>(i)));
      if (keepChords && std::any_of(s.chords.begin(), s.chords.end(), [](const Chord &c) { return !c.none(); })) {
         // Moved to the song's key as a locked section's are (KEY may have moved since).
         for (Chord &c : s.chords)
            if (!c.none()) {
               c.root = static_cast<int8_t>((c.root + move) % 12);
               if (c.bass >= 0)
                  c.bass = static_cast<int8_t>((c.bass + move) % 12);
            }
         made.chords = s.chords;
         made.split = s.split;
         made.marks = s.marks;
         made.progBars = s.progBars;
         made.bars = s.bars;
      }
      s = made;
   }
}

// A section rolled again with its own dice; the rest of the song stays. It's made in the song's
// key (which KEY may have moved from the recipe's). With `keepChords` (a new energy, or a locked
// section) only its rhythm is new: the chords, edits included, stay.
inline void rollSection(const Recipe &r, Song &song, int section, uint32_t seed, bool keepChords = false) {
   const Section &old = song.sections[static_cast<size_t>(section)];
   Recipe rr = r;
   rr.key = song.key;
   Section s = makeSection(rr, section, seed);
   s.generation = static_cast<uint16_t>(old.generation + 1);
   s.locked = old.locked;
   if (keepChords || old.locked) {
      s.chords = old.chords;
      s.split = old.split;
      s.marks = old.marks;
      s.progBars = old.progBars;
      s.bars = old.bars;
   }
   song.sections[static_cast<size_t>(section)] = s;
}

// A mutated child taken as the section's main take; the other takes bred from it again.
inline void adoptChild(const Recipe &r, Section &s, const Pattern &child, uint32_t seed, int section = -1) {
   Rng rng(seed);
   s.takes[0] = child;
   for (int t = 1; t < kTakes; ++t) {
      std::string tag;
      s.takes[static_cast<size_t>(t)] = mutate(child, 0.15 + 0.15 * t + r.weird * 0.3, rng, tag, styleOf(r).mods, genreAt(r.genre).name);
   }
   s.fill = makeFill(r, child, s.energy / 100.0, rng);
   // The fill down and the ending made again from the new take (an offbeat take's are its own).
   if (section >= 0) {
      s.fillDown = makeFillDown(r, section, child, s.energy / 100.0);
      if (section == kOutro)
         s.ending = makeEnding(r, child, s.energy / 100.0);
   }
   s.generation = static_cast<uint16_t>(s.generation + 1);
}

// The Roman numeral of a chord in a key ("IV7", "vi", "bVII"), for the panel.
inline std::string roman(const Chord &c, int key) {
   if (c.none())
      return "N.C.";
   static const char *const up[12] = {"I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"};
   static const char *const low[12] = {"i", "bii", "ii", "biii", "iii", "iv", "#iv", "v", "bvi", "vi", "bvii", "vii"};
   const int deg = ((c.root - key) % 12 + 12) % 12;
   const std::vector<int> &t = qualityOf(c).tones;
   const bool minor = std::find(t.begin(), t.end(), 3) != t.end() && std::find(t.begin(), t.end(), 4) == t.end();
   std::string s = minor ? low[deg] : up[deg];
   // A chord on the flat fifth is named as one (a tritone sub's Bb7 in E is bV7), as chordName spells
   // it; a diminished chord there is the sharp fourth.
   if (deg == 6 && c.quality != kDim && c.quality != kDim7 && c.quality != kHalfDim)
      s = minor ? "bv" : "bV";
   const std::string suffix = qualityOf(c).suffix;
   if (minor && !suffix.empty() && suffix[0] == 'm' && suffix.rfind("maj", 0) != 0)
      s += suffix.substr(1);
   else
      s += suffix;
   return s;
}

} // namespace jcomp

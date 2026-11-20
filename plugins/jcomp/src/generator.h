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
      if (fits(2))
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
   case kDom9: case kDom13: case kSharp9: case kFlat9: case kEleven: case kSharp11: c.quality = kDom7; break;
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
   case kDom7: case kDom9: case kDom13: case kSharp9: case kFlat9: case kEleven: case kSharp11: return true;
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
         const Chord next = after(written, b, half);
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
   if (reharm > 0.0)
      for (int b = 0; b < s.progBars; ++b) {
         Chord &c = s.chords[static_cast<size_t>(b * 2)];
         const Chord next = after(s, b, 0);
         // Not the tonic: a blues's or a vamp's I7 is home, not a dominant to stand in for.
         const bool tonic = ((c.root - r.key) % 12 + 12) % 12 == 0;
         if (dominant(c) && !tonic && !next.none() && next.root == (c.root + 5) % 12 && rng.chance(reharm)) {
            c.root = static_cast<int8_t>((c.root + 6) % 12);
            c.bass = -1;
            if (!(s.split >> b & 1))
               s.chords[static_cast<size_t>(b * 2 + 1)] = c;
         } else if (!(s.split >> b & 1) && next != c && !next.none() && rng.chance(reharm * 0.8)) {
            s.chords[static_cast<size_t>(b * 2 + 1)] = {static_cast<int8_t>((next.root + 1) % 12),
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
   const int beat = p.rate == kRate8T ? 3 : 4;
   for (int s = 0; s < p.steps; s += beat)
      if (p.at(s).on() && !leftHand(p.at(s).part))
         return true;
   return false;
}
// Whether a chord added on step `s` would break the take's offbeat character: a beat where an offbeat
// take (a skank on the ands) has none, or beat 1 or 3 where a take keeps them empty (the one drop's
// skank on 2 and 4, the organ bubble).
inline bool keepsOff(const Pattern &p, int s) {
   const int beat = p.rate == kRate8T ? 3 : 4;
   if (s % beat)
      return false;
   if (!chordOnBeat(p))
      return true;
   if (s % (beat * 2))
      return false;
   for (int k = 0; k < p.steps; k += beat * 2)
      if (p.at(k).on() && !leftHand(p.at(k).part))
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

// A rhythm as a pattern. One bar is played twice, so a take is at least two bars.
inline Pattern fromRhythm(const char *text) {
   Pattern p;
   p.rate = rhythmTriplet(text) ? kRate8T : kRate16;
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

inline int perBeat(const Pattern &p) { return p.rate == kRate8T ? 3 : 4; }
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
inline std::vector<const Rhythm *> rhythmsFor(const Genre &g, const Style &st, bool fills = false) {
   std::vector<const Rhythm *> out;
   const bool triplet = st.mods & kTriplet;
   const auto &list = fills ? g.fills : g.rhythms;
   for (size_t i = 0; i < list.size(); ++i) {
      if (rhythmTriplet(list[i].hits) != triplet)
         continue;
      if (!fills && st.rhythms && !(st.rhythms >> i & 1))
         continue;
      out.push_back(&list[i]);
   }
   if (out.empty() && !fills)
      for (const Rhythm &r : list)
         if (rhythmTriplet(r.hits) == triplet)
            out.push_back(&r);
   if (out.empty())
      for (const Rhythm &r : list)
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
inline void blendInto(Pattern &a, const Pattern &b, double amount, Rng &rng) {
   if (a.rate != b.rate)
      return;
   for (int s = 0; s < a.steps; ++s)
      if (rng.chance(amount))
         a.at(s) = b.at(s % b.steps);
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
      const bool pushAt = p.rate == kRate8T ? (inBar == bar - 1 || inBar == bar / 2 - 1)
                                            : (inBar == bar - 1 || inBar == bar - 2 || inBar == bar / 2 - 1 || inBar == bar / 2 - 2);
      if (mods & kPushes && h.on() && !onBeat(p, s) && pushAt && !leftHand(h.part))
         h.push = rng.chance(pushChance) ? 1 : 0;
      // Fewer hits, but never the rhythm's shape: only a bar of four or more chords loses one, down to
      // three (a Charleston, a clave bar or a bossa cycle keeps every hit: the thumb isn't counted).
      if (mods & kSparse && h.on() && !leftHand(h.part) && !onBeat(p, s) && h.vel < kAccent &&
          hitsInBar(p, s / bar, true) > 3 && rng.chance(0.45))
         h = Hit{};
      if (mods & kHalfTime && h.on() && (s / beat) % 2 == 1 && rng.chance(0.7))
         h = Hit{};
      if (mods & kShorts && h.on() && h.len <= 1)
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
      for (int s = 0; s < p.steps; s += beat * 2) {
         if (!p.at(s).on()) {
            p.at(s) = hitOf('b');
         } else {
            // the left hand an eighth behind the chord: the fifth, under the root the chord already
            // has (on the shuffle's third triplet, never its middle one, and never a sixteenth)
            const int e = beat == 2 ? 1 : 2;
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
inline void shapeEnergy(Pattern &p, double e, Rng &rng, bool fixed = false, unsigned mods = 0) {
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
      for (int s = 0; s < p.steps; ++s) {
         Hit &h = p.at(s);
         if (!h.on())
            continue;
         if (!isFirst(s)) {
            h = Hit{};
            continue;
         }
         h.vel = std::min(h.vel, quiet);
         if (h.part == kPartChord)
            h.art = kArtRing;
      }
      if (p.empty()) {
         p.at(0) = hitOf('x');
         p.at(0).vel = quiet;
         p.at(0).art = kArtRing;
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
      else if (h.vel == kGhost && e < 0.5 && rng.chance(0.5))
         h = Hit{};
   }
   if (p.empty())
      p.at(0) = hitOf('x');
   // Up high it fills in: soft chords on empty eighths the rhythm allows (not where a chord is held,
   // nor on a beat an offbeat take keeps empty, nor in a held-chord style, a line or a left hand's).
   if (e > 0.7 && !(mods & kLongs) && !hasLine(p) && !hasLeftHand(p)) {
      const bool fine = fineGrid(p);
      for (int s = 0; s < p.steps; ++s)
         if (!p.at(s).on() && onEighth(p, s) && addable(p, s, fine) && !keepsOff(p, s) && !heldOver(p, s) &&
             rng.chance((e - 0.7) * 1.2))
            p.at(s) = hitOf('o');
   }
   const double scale = 0.82 + 0.3 * e;
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (h.on())
         h.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(h.vel * scale)), 20, 127));
   }
}

// WEIRD: hits left to chance, pulled off the grid, odd lengths, stray stabs.
// A fixed figure only loosens in time; a style chopped short (a skank) only chops or halves.
inline void applyWeird(Pattern &p, double w, Rng &rng, unsigned mods = 0) {
   if (w <= 0.0)
      return;
   const bool fine = fineGrid(p);
   const bool fixed = mods & kFixed;
   for (int s = 0; s < p.steps; ++s) {
      Hit &h = p.at(s);
      if (h.on()) {
         if (!fixed && !onBeat(p, s) && rng.chance(w * 0.3))
            h.chance = static_cast<uint8_t>(rng.range(55, 90));
         if (rng.chance(w * 0.25))
            h.nudge = static_cast<int8_t>(std::clamp(rng.range(-12, 12) * w * 1.5, -double(kMaxNudge), double(kMaxNudge)));
         if (!fixed && rng.chance(w * 0.15))
            h.art = static_cast<uint8_t>(mods & kLongs    ? rng.range(kArtRing, kArtTopRings)
                                         : mods & kShorts ? rng.range(kArtChop, kArtHalf)
                                                          : rng.range(kArtChop, kArtTopRings));
      } else if (!fixed && !(mods & kLongs) && !hasLine(p) && !onBeat(p, s) && addable(p, s, fine) &&
                 rng.chance(w * w * 0.12)) {
         h = hitOf(rng.chance(0.5) ? 'o' : 'g');
         h.chance = static_cast<uint8_t>(rng.range(50, 85));
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
      if (h.art == kArtAsWritten && h.len <= 1 && !(mods & kShorts) && gapAfter(p, s) >= 3 && rng.chance(pl.ring * 0.7))
         h.art = pl.ring > 0.8 && pedal ? kArtTopRings : kArtRing;
      if (h.art == kArtAsWritten && h.len <= 1 && pl.ring < 0.15 && rng.chance(0.5))
         h.art = kArtChop;
      if (s % bar == 0 && h.part == kPartChord) {
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
                h.vel = kNormal;
             else if (!onBeat(p, s) && rng.chance(0.3))
                h.vel = kAccent;
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
          if (p.steps < bar * 2)
             return;
          for (int s = 0; s < bar; ++s)
             if (p.at(bar + s).on() != p.at(s).on())
                return;
          const bool fine = fineGrid(p);
          for (int s = 0; s < bar; ++s)
             p.at(bar + s) = p.at(s);
          for (int s = bar + bar / 2; s < bar * 2; ++s)
             if (!onBeat(p, s) && addable(p, s, fine) && rng.chance(0.4))
                p.at(s) = p.at(s).on() ? Hit{} : hitOf('x');
       }},
      {"left hand", [](Pattern &p, Rng &) {
          const int beat = perBeat(p);
          for (int s = 0; s < p.steps; s += beat * 2)
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
         m.apply(p, rng);
         if (p.empty())
            p.at(0) = hitOf('x');
         return true;
      }
   return false;
}

// Whether a mutation suits a style: a fixed figure only changes its accents and lengths (never
// ringing if it is chopped short); a left hand is added only where the style has one.
inline bool mutationFits(const char *tag, unsigned mods, const Pattern &parent) {
   const auto is = [&](const char *t) { return std::strcmp(tag, t) == 0; };
   if (mods & kFixed)
      return is("accents") || is("chop it") || (is("let it ring") && !(mods & kShorts));
   if (is("left hand"))
      return (mods & kLeftHand) || hasLeftHand(parent);
   // Held chords stay held: no chops, no ghosts between them, nothing moved off the beat.
   if (mods & kLongs && (is("chop it") || is("busier") || is("ghosts") || is("shift")))
      return false;
   // A line (the Afrobeat tenor's ostinato) plays as written: nothing taken out, moved or ghosted.
   if (hasLine(parent) && (is("sparser") || is("shift") || is("ghosts") || is("chop it")))
      return false;
   return true;
}

// A child of a pattern: one to three mutations, more with more `amount`, of those that suit the
// style (`mods`). Its tag names them.
inline Pattern mutate(const Pattern &parent, double amount, Rng &rng, std::string &tag, unsigned mods = 0) {
   Pattern p = parent;
   const int n = 1 + static_cast<int>(amount * 2.0 + rng.uniform() * 0.7);
   tag.clear();
   const auto &list = mutations();
   std::array<int, 16> fit{};
   int fits = 0;
   for (int i = 0; i < static_cast<int>(list.size()) && fits < 16; ++i)
      if (mutationFits(list[static_cast<size_t>(i)].tag, mods, parent))
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
   return p;
}

// ---- sections and songs ----

inline Pattern baseRhythm(const Recipe &r, int section, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   const double e = sectionEnergy(r, section);
   const auto list = rhythmsFor(g, st);
   const Rhythm *rh = pickRhythm(list, e, rng);
   Pattern p = rh ? fromRhythm(rh->hits) : fromRhythm("x...............");
   // Interlocking parts: the style's other rhythms woven into the steps this one leaves empty.
   if (st.mods & kInterlock)
      for (const Rhythm *o : list) {
         if (o == rh)
            continue;
         const Pattern q = fromRhythm(o->hits);
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
      st2.mods = st.mods & kTriplet;
      st2.rhythms = 0;
      if (const Rhythm *r2 = pickRhythm(rhythmsFor(g2, st2), e, rng))
         blendInto(p, fromRhythm(r2->hits), r.blend * 0.6, rng);
   }
   return p;
}

inline Pattern makeFill(const Recipe &r, const Pattern &base, double e, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   // A fixed figure's fill is the figure, its last stroke leaning into the next section: a montuno
   // keeps its clave (the engine plays the side the bar falls on), Freddie Green his four.
   if (st.mods & kFixed) {
      Pattern f = base;
      for (int s = f.steps - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            f.at(s).vel = kAccent;
            f.at(s).push = 1;
            break;
         }
      return f;
   }
   // A held-chord style's fill holds too: its own bar, rung, swelling into the next with a chord
   // pushed on the last eighth (a pad doesn't chop eighths for a fill).
   if (st.mods & kLongs) {
      Pattern f = base;
      f.steps = static_cast<uint8_t>(f.stepsPerBar());
      for (int s = f.steps; s < kMaxSteps; ++s)
         f.at(s) = Hit{};
      const int bar = f.stepsPerBar();
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && !leftHand(f.at(s).part))
            f.at(s).art = kArtRing;
      const int last = bar - (f.rate == kRate8T ? 1 : 2);
      if (!f.at(last).on() && !f.at(last - 1).on()) {
         f.at(last) = hitOf('x');
         f.at(last).art = kArtRing;
      }
      for (int s = bar - 1; s >= 0; --s)
         if (f.at(s).on() && !leftHand(f.at(s).part)) {
            f.at(s).push = 1;
            break;
         }
      shapeEnergy(f, std::max(0.35, e), rng);
      return f;
   }
   const bool fine = fineGrid(base);
   Pattern f;
   const auto fills = rhythmsFor(g, st, true);
   // The genre's fill is its own player's (Chic's chucking, the palm-wine thumb): a style played by
   // another (a pad, keys in a guitar genre) fills from its own take.
   const bool genreFill = st.player < 0 || st.player == g.player;
   if (const Rhythm *rh = genreFill ? pickRhythm(fills, e, rng) : nullptr; rh && rhythmTriplet(rh->hits) == (base.rate == kRate8T))
      f = fromRhythm(rh->hits);
   else {
      f = base;
      const int bar = f.stepsPerBar();
      for (int s = bar - perBeat(f); s < bar; ++s)
         if (addable(f, s, fine))
            f.at(s) = hitOf('x');
   }
   // A fill is one bar.
   f.steps = static_cast<uint8_t>(f.stepsPerBar());
   for (int s = f.steps; s < kMaxSteps; ++s)
      f.at(s) = Hit{};
   const int bar = f.stepsPerBar();
   // On the take's grid: a hit between the eighths of an eighth-note take moves to the eighth before.
   if (!fine)
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
         const bool at = f.rate == kRate8T ? (s == bar - 1 || s == bar / 2 - 1)
                                           : (s == bar - 1 || s == bar - 2 || s == bar / 2 - 1 || s == bar / 2 - 2);
         if (at && h.on() && !onBeat(f, s) && !leftHand(h.part))
            h.push = 1;
      }
   // An offbeat style's fill stays off the beats (a one drop's off 1 and 3).
   for (int s = 0; s < bar; s += perBeat(f))
      if (f.at(s).on() && !leftHand(f.at(s).part) && keepsOff(base, s))
         f.at(s) = Hit{};
   // The left hand plays on through the fill.
   if (hasLeftHand(base))
      for (int s = 0; s < bar; ++s)
         if (!f.at(s).on() && base.at(s).on() && leftHand(base.at(s).part))
            f.at(s) = base.at(s);
   // The fill's last hit anticipates the next section's chord.
   for (int s = bar - 1; s >= 0; --s)
      if (f.at(s).on() && !leftHand(f.at(s).part)) {
         f.at(s).push = 1;
         break;
      }
   if (st.mods & kShorts)
      for (int s = 0; s < bar; ++s)
         if (f.at(s).on() && f.at(s).len <= 1)
            f.at(s).art = kArtChop;
   shapeEnergy(f, std::max(0.35, e), rng);
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
   Pattern p = baseRhythm(r, section, rng);
   applyMods(p, st.mods, rng, genreAt(r.genre).pushAll ? 1.0 : 0.75);
   shapeEnergy(p, e, rng, st.mods & kFixed, st.mods);
   applyWeird(p, r.weird, rng, st.mods);
   applyPlayer(p, playerOf(r), st.mods, rng);
   s.takes[0] = p;
   for (int t = 1; t < kTakes; ++t) {
      std::string tag;
      s.takes[static_cast<size_t>(t)] = mutate(p, 0.15 + 0.15 * t + r.weird * 0.3, rng, tag, st.mods);
   }
   s.fill = makeFill(r, p, e, rng);
   return s;
}

inline void generateSong(const Recipe &r, Song &song) {
   const int key = ((r.key % 12) + 12) % 12;
   // A locked section keeps its chords, moved to the new key with the rest of the song.
   const int move = (key - song.key + 12) % 12;
   song.key = static_cast<uint8_t>(key);
   for (int i = 0; i < kSections; ++i) {
      Section &s = song.sections[static_cast<size_t>(i)];
      if (s.locked) {
         for (Chord &c : s.chords)
            if (!c.none()) {
               c.root = static_cast<int8_t>((c.root + move) % 12);
               if (c.bass >= 0)
                  c.bass = static_cast<int8_t>((c.bass + move) % 12);
            }
         continue;
      }
      s = makeSection(r, i, mix(r.seed, static_cast<uint32_t>(i)));
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
      s.progBars = old.progBars;
      s.bars = old.bars;
   }
   song.sections[static_cast<size_t>(section)] = s;
}

// A mutated child taken as the section's main take; the other takes bred from it again.
inline void adoptChild(const Recipe &r, Section &s, const Pattern &child, uint32_t seed) {
   Rng rng(seed);
   s.takes[0] = child;
   for (int t = 1; t < kTakes; ++t) {
      std::string tag;
      s.takes[static_cast<size_t>(t)] = mutate(child, 0.15 + 0.15 * t + r.weird * 0.3, rng, tag, styleOf(r).mods);
   }
   s.fill = makeFill(r, child, s.energy / 100.0, rng);
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

#pragma once

// The generator: from a recipe (a genre and style, maybe a second genre blended in, a drummer and a
// percussionist, ENERGY and WEIRD, a seed) to a song. Everything is drawn from a seeded random
// stream, so the same recipe and seed always give the same song, and a section rolled again (its
// own dice) changes only that section.
//
// A section is made in steps:
//    1. the style's groove (blended with the second genre's, if any) as hits on a grid
//    2. the style's twists (MODS)
//    3. shaped to the section's energy: which lanes come in, how many soft hits, how loud
//    4. WEIRD: hits pulled off the grid, left to chance, rolled, odd lengths, stray sounds
//    5. played by the drummer: ghosts, busy hits, timing and dynamics, where the backbeat sits
// then three more variations are bred from it (mutate()), and a fill.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "kit.h"
#include "players.h"
#include "shared.h"
#include "styles.h"

namespace jdrummer {

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
   int genre2 = -1;     // blended in, or -1
   double blend = 0.3;  // how much of genre2
   int player = 5;      // drummer, 0..5
   int percussionist = 0; // 0 none, 1..6
   double energy = 0.5; // moves every section's energy
   double weird = 0.15;
   uint32_t seed = 1;
   // Each section's own energy (0..1) before ENERGY scales them all: the song's arc. Down at 0 a
   // section is silent; under about 0.2 it thins to a few anchor hits.
   std::array<double, kSections> sections{kArcEnergy[0], kArcEnergy[1], kArcEnergy[2],
                                          kArcEnergy[3], kArcEnergy[4], kArcEnergy[5],
                                          kArcEnergy[6]};
};

// ---- the grid ----

inline int velocityOf(char c) {
   switch (c) {
   case 'X': return kAccent;
   case 'x': case '2': case '3': case '4': case '?': return kNormal;
   case 'o': return kSoft;
   case 'g': return kGhost;
   default: return 0;
   }
}

inline void writeLane(Pattern &p, int pad, const char *hits) {
   for (int s = 0; hits[s] && s < kMaxSteps; ++s) {
      const int v = velocityOf(hits[s]);
      Hit &h = p.at(pad, s);
      h = Hit{};
      if (!v)
         continue;
      h.vel = static_cast<uint8_t>(v);
      if (hits[s] >= '2' && hits[s] <= '4')
         h.ratchet = static_cast<uint8_t>(hits[s] - '0');
      if (hits[s] == '?')
         h.chance = 50;
   }
}

inline int laneLength(const char *hits) {
   int n = 0;
   while (hits[n] && n < kMaxSteps)
      ++n;
   return n;
}

inline Pattern fromGroove(const Groove &g) {
   Pattern p;
   int steps = 0;
   for (const Lane &l : g.lanes) {
      writeLane(p, l.pad, l.hits);
      steps = std::max(steps, laneLength(l.hits));
   }
   p.steps = static_cast<uint8_t>(std::clamp(steps, 1, kMaxSteps));
   p.rate = g.rate;
   return p;
}

// Two grooves mixed lane by lane: each pad's lane comes from `b` with probability `amount`.
inline Pattern blendPatterns(const Pattern &a, const Pattern &b, double amount, Rng &rng) {
   Pattern out = a;
   for (int pad = 0; pad < kPads; ++pad) {
      const bool inA = a.uses(pad), inB = b.uses(pad);
      if (!inB && !inA)
         continue;
      bool fromB;
      if (inA && inB)
         fromB = rng.chance(amount);
      else if (inB)
         fromB = rng.chance(amount * 1.2);
      else
         fromB = rng.chance(amount * 0.5); // a lane only `a` has is dropped now and then
      if (!fromB)
         continue;
      for (int s = 0; s < kMaxSteps; ++s)
         out.at(pad, s) = inB && s < out.steps ? b.at(pad, s % std::max<int>(1, b.steps)) : Hit{};
   }
   return out;
}

// Steps in a beat at a pattern's rate: 4 sixteenths, 6 sixteenth triplets (12/8, two to an eighth),
// 2 eighths, 8 thirty-seconds.
inline int stepsPerBeat(const Pattern &p) { return kTicksPerBeat / kRateTicks[std::min<int>(p.rate, kRate32)]; }
inline bool onBeat(int step, int beat = 4) { return step % beat == 0; }

// Who plays what on a kit, for a tradition: the pads a twist, a fill or a player reaches for. On the
// hand kits these are that tradition's own drums, so a salsa's fill is on the congas and a maqsum's
// on the darbuka.
struct Voices {
   int kit = kDrumKit;
   unsigned trad = kTradAny;
   int kick = K, snare = SN, clap = CL, hat = CH, openHat = OH, perc = RIM, shaker = SH;
   int tone = SN; // the hand drum's open tone (on a kit, the snare)
   std::array<int, 3> toms{HT, MT, LT}; // high to low
   bool drums() const { return kit == kDrumKit; }
   bool hands() const { return kit == kPercussionKit || kit == kAfroLatinKit; }
   // Whether a pad belongs to the tradition (every pad does on the kits).
   bool allows(int pad) const { return pad >= 0 && (padTradition(kit, pad) & trad) != 0; }
};

inline Voices voicesFor(int kit, unsigned trad = kTradAny) {
   Voices v;
   v.kit = kit;
   v.trad = trad;
   if (kit == kAfroLatinKit) {
      v.clap = v.openHat = -1;
      if (trad == kTradWestAfrican) {
         v.kick = AL_DJEMBE_BASS, v.snare = AL_DJEMBE_SLAP, v.tone = AL_DJEMBE_TONE, v.hat = SHEKERE;
         v.perc = BELL, v.shaker = SHEKERE, v.toms = {KENKENI, SANGBAN, DUNDUNBA};
      } else {
         v.kick = TUMBA, v.snare = AL_CONGA_SLAP, v.tone = CONGA, v.hat = GUIRO;
         v.perc = CAMPANA, v.shaker = SHEKERE, v.toms = {AL_HIGH_BONGO, CONGA, TUMBA};
      }
   } else if (kit == kPercussionKit) {
      v.clap = v.openHat = -1;
      if (trad == kTradIndian) {
         v.kick = FRAME_DRUM, v.snare = TABLA, v.tone = TABLA, v.hat = TABLA;
         v.perc = TABLA, v.shaker = SHAKER, v.toms = {TABLA, DARBUKA_TEK, FRAME_DRUM};
      } else if (trad != kTradAny && (trad & kTradMiddleEastern) && !(trad & (kTradCuban | kTradBrazilian | kTradFlamenco))) {
         v.kick = DARBUKA_DUM, v.snare = DARBUKA_TEK, v.tone = DARBUKA_TEK, v.hat = DARBUKA_TEK;
         v.perc = DARBUKA_TEK, v.shaker = -1, v.toms = {DARBUKA_TEK, FRAME_DRUM, DARBUKA_DUM};
      } else {
         v.kick = CAJON_BASS, v.snare = CAJON_SLAP, v.tone = HIGH_CONGA, v.hat = SHAKER;
         v.perc = HIGH_BONGO, v.shaker = SHAKER, v.toms = {HIGH_BONGO, HIGH_CONGA, LOW_CONGA};
         v.clap = PALMAS;
      }
   } else if (kit != kDrumKit) {
      // FX and animals: the pads by role.
      v.kick = 0, v.snare = 2, v.clap = 3, v.hat = 6, v.openHat = 10, v.perc = 1, v.shaker = 6, v.tone = 2;
      v.toms = {9, 7, 5};
   }
   return v;
}

// Steps between a pattern's grid points: 2 in 12/8 written in 1/16T, where the odd steps are
// sextuplets between the eighths and nothing the generator adds goes there.
inline int gridOf(const Pattern &p) { return p.rate == kRate16T ? 2 : 1; }
inline int onGrid(const Pattern &p, int s) { return s - s % gridOf(p); }

// A hand tradition's timeline, the pads the rhythm is known by: the clave and the bells, the tabla
// and its bayan, the darbuka's doum and tek. Thinning, twists and WEIRD leave them be.
inline bool timeline(const Voices &v, int pad) {
   if (!v.hands() || v.trad == kTradAny)
      return false;
   if (v.kit == kAfroLatinKit)
      return pad == CLAVE || pad == CAMPANA || pad == BELL;
   return ((v.trad & kTradIndian) && (pad == TABLA || pad == FRAME_DRUM)) ||
          ((v.trad & kTradMiddleEastern) && (pad == DARBUKA_DUM || pad == DARBUKA_TEK));
}
// A timeline hit that holds the time (its ghosted ornaments can still go).
inline bool timelineHit(const Voices &v, int pad, const Hit &h) { return timeline(v, pad) && h.vel > kGhost + 10; }

// The pad a pattern's backbeat is on: the snare, else the clap; on the hand kits the slap.
inline int backbeatPad(const Pattern &p, const Voices &v) {
   if (v.drums()) {
      if (p.uses(SN))
         return SN;
      if (p.uses(CL))
         return CL;
      return SN;
   }
   return v.snare;
}
// Where ghost notes go: round the snare on a kit, among the open tones on a hand drum.
inline int ghostPad(const Pattern &p, const Voices &v) {
   if (v.hands())
      return p.uses(v.tone) ? v.tone : v.snare;
   return backbeatPad(p, v);
}

// A kick on every beat: the one thing four-on-the-floor never moves.
inline bool fourOnFloor(const Pattern &p, int kick) {
   const int b = stepsPerBeat(p);
   if (p.steps < 2 * b)
      return false;
   for (int s = 0; s < p.steps; s += b)
      if (!p.at(kick, s).on())
         return false;
   return true;
}

// The busiest of a pattern's lanes with one of these roles, or -1.
inline int busiestLane(const Pattern &p, int kit, Role a, Role b) {
   int best = -1, most = 0;
   for (int pad = 0; pad < kPads; ++pad) {
      const Role r = padRole(kit, pad);
      if (r != a && r != b)
         continue;
      int c = 0;
      for (int s = 0; s < p.steps; ++s)
         c += p.at(pad, s).on();
      if (c > most) {
         most = c;
         best = pad;
      }
   }
   return best;
}

// The hat lane a twist works on: the closed hat on a kit (or the shaker, if only it plays), the
// busiest hat or bell lane on the others.
inline int hatLane(const Pattern &p, const Voices &v) {
   if (v.drums())
      return p.uses(CH) || !p.uses(SH) ? CH : SH;
   const int b = busiestLane(p, v.kit, kHat, kPerc);
   return b >= 0 ? b : v.hat;
}

// ---- the style's twists ----

// Each twist always changes the groove it is given (a style that names one gets something for it),
// however the groove is written.
inline void applyMods(Pattern &p, unsigned mods, int kit, Rng &rng, unsigned trad = kTradAny) {
   const Voices v = voicesFor(kit, trad);
   const int n = p.steps;
   const int beat = stepsPerBeat(p);
   // Sparser first, so it doesn't take away what the other twists add.
   if (mods & kSparse) {
      int removed = 0;
      for (int pass = 0; pass < 2 && !removed; ++pass)
         for (int pad = 0; pad < kPads; ++pad)
            for (int s = 0; s < n; ++s) {
               Hit &h = p.at(pad, s);
               if (h.on() && (h.vel < kAccent || pass) && !onBeat(s, beat) && !timelineHit(v, pad, h) &&
                   (pass || rng.chance(0.35))) {
                  h = Hit{};
                  ++removed;
               }
            }
   }
   if (mods & kBusyHats) {
      const int h = hatLane(p, v);
      int added = 0;
      for (int s = 0; s < n; ++s)
         if (!p.at(h, s).on() && !(v.openHat >= 0 && p.at(v.openHat, s).on())) {
            p.at(h, s).vel = kSoft;
            ++added;
         }
      // Already in sixteenths: rolls instead.
      if (!added) {
         int rolled = 0;
         for (int k = 0; k < 32 && rolled < 2; ++k) {
            Hit &x = p.at(h, rng.range(n / 2, n - 1));
            if (x.on() && x.ratchet == 1) {
               x.ratchet = 2;
               ++rolled;
            }
         }
      }
   }
   if (mods & kOpenHats && v.openHat >= 0) {
      bool changed = false;
      for (int s = beat / 2; s < n; s += beat) {
         if (!p.at(v.openHat, s).on() || p.at(v.hat, s).on())
            changed = true;
         p.at(v.openHat, s).vel = static_cast<uint8_t>(std::max<int>(p.at(v.openHat, s).vel, kNormal));
         p.at(v.hat, s) = Hit{};
      }
      if (!changed)
         for (int s = beat / 2; s < n; s += beat)
            p.at(v.openHat, s).vel = kAccent;
   }
   if (mods & kShakerLine && v.shaker >= 0) {
      const int sh = v.shaker;
      if (p.uses(sh)) {
         // A shaker already plays: it fills in, and leans on the off-beats.
         for (int s = 0; s < n; s += gridOf(p)) {
            Hit &h = p.at(sh, s);
            if (s % beat == beat / 2)
               h.vel = static_cast<uint8_t>(std::max<int>(h.vel, kNormal + 8));
            else if (!h.on())
               h.vel = kGhost;
         }
      } else
         for (int s = 0; s < n; ++s)
            p.at(sh, s).vel = static_cast<uint8_t>(s % 2 ? kSoft : kGhost);
   }
   if (mods & kRollingPerc) {
      const int pc = v.perc;
      int added = 0;
      for (int s = 0; s < n; ++s)
         if (!p.at(pc, s).on() && rng.chance(0.55)) {
            Hit &h = p.at(pc, s);
            h.vel = static_cast<uint8_t>(s % beat == beat / 2 ? kNormal : kSoft);
            h.chance = static_cast<uint8_t>(rng.range(45, 85));
            ++added;
         }
      if (!added)
         for (int s = beat / 2; s < n; s += beat)
            p.at(pc, s).chance = static_cast<uint8_t>(rng.range(45, 85));
   }
   if (mods & kHalfTime) {
      const int bb = backbeatPad(p, v);
      bool had = false;
      const int lanes[3] = {v.drums() ? SN : bb, v.drums() ? CL : -1, v.drums() ? RIM : -1};
      for (int pad : lanes)
         if (pad >= 0)
            for (int s = 0; s < n; ++s)
               if (p.at(pad, s).vel >= kNormal && s % (2 * beat) == beat) {
                  p.at(pad, s) = Hit{};
                  had = true;
               }
      if (!had) {
         // No backbeat on 2 and 4 (a dembow, a broken beat): every second backbeat hit drops.
         int k = 0;
         for (int s = 0; s < n; ++s)
            if (p.at(bb, s).vel >= kSoft && k++ % 2 == 1)
               p.at(bb, s) = Hit{};
      }
      if (n > 2 * beat)
         p.at(bb, 2 * beat).vel = kAccent;
   }
   if (mods & kDoubleKick) {
      int added = 0;
      // Pickups, never a sixteenth either side of a kick (bar the one, which the last step leads to).
      const int spots[5] = {n - 1, 7, 10, 3, 11};
      auto lonely = [&](int s) { return s == n - 1 || (!p.at(v.kick, s - 1).on() && !p.at(v.kick, (s + 1) % n).on()); };
      for (int s : spots)
         if (s > 0 && s < n && !p.at(v.kick, s).on() && lonely(s) && rng.chance(0.7)) {
            p.at(v.kick, s).vel = kSoft;
            ++added;
         }
      for (int s : spots)
         if (added < 2 && s > 0 && s < n && !p.at(v.kick, s).on()) {
            p.at(v.kick, s).vel = kSoft;
            ++added;
         }
   }
   if (mods & kGhosty) {
      const int sn = ghostPad(p, v);
      int added = 0;
      for (int k = 0; k < 64 && added < 3; ++k) {
         const int s = rng.range(0, n - 1);
         if (s % 2 && !p.at(sn, s).on()) {
            p.at(sn, s).vel = kGhost;
            ++added;
         }
      }
   }
   if (mods & kToms) {
      for (int k = 0; k < 3; ++k) {
         const int s = std::clamp(n - 6 + 2 * k + rng.range(0, 1), 0, n - 1);
         p.at(v.toms[static_cast<size_t>(k)], s).vel = static_cast<uint8_t>(kNormal - 10 * k);
      }
   }
   if (mods & kRimShot && v.drums()) {
      // The backbeat moves to the rim: the snare's, else the clap's; with neither, a soft rim on the
      // last beat.
      const int from = p.uses(SN) ? SN : p.uses(CL) ? CL : -1;
      if (from >= 0) {
         for (int s = 0; s < n; ++s)
            if (p.at(from, s).vel >= (from == SN ? 1 : kSoft)) {
               p.at(RIM, s) = p.at(from, s);
               p.at(from, s) = Hit{};
            }
      } else
         p.at(RIM, std::max(0, n - beat)).vel = kSoft;
   }
   if (mods & kClapStack && v.drums()) {
      if (p.uses(SN)) {
         for (int s = 0; s < n; ++s)
            if (p.at(SN, s).vel >= kNormal)
               p.at(CL, s).vel = static_cast<uint8_t>(p.at(CL, s).on() ? kAccent : kNormal);
      } else if (p.uses(CL)) {
         // A clap alone: a snare goes under it.
         for (int s = 0; s < n; ++s)
            if (p.at(CL, s).vel >= kSoft)
               p.at(SN, s).vel = kNormal - 10;
      } else
         for (int s = beat; s < n; s += 2 * beat)
            p.at(CL, s).vel = kNormal;
   }
   if (mods & kBroken) {
      // Kicks pulled a sixteenth off the grid (never the one): about half of them, at least one.
      std::vector<int> kicks;
      for (int s = 1; s < n; ++s)
         if (p.at(v.kick, s).on())
            kicks.push_back(s);
      auto pull = [&](int s, int dir) {
         const int to = s + dir;
         if (to < 1 || to >= n || p.at(v.kick, to).on() || std::find(kicks.begin(), kicks.end(), to) != kicks.end())
            return false;
         // Not up against another kick.
         if ((to + dir < n && to + dir >= 0 && p.at(v.kick, to + dir).on()) || (to + dir == n && p.at(v.kick, 0).on()))
            return false;
         p.at(v.kick, to) = p.at(v.kick, s);
         p.at(v.kick, s) = Hit{};
         return true;
      };
      int moved = 0;
      for (int s : kicks)
         if (rng.chance(0.5))
            moved += pull(s, rng.chance(0.5) ? 1 : -1);
      for (size_t i = 0; i < kicks.size() && !moved; ++i)
         moved += pull(kicks[i], 1) || pull(kicks[i], -1);
      if (!moved && !kicks.empty())
         p.at(v.kick, kicks.back()).vel = kSoft; // nowhere to go: it softens
   }
   if (mods & kRatchetHats) {
      const int h = hatLane(p, v);
      int rolled = 0, first = -1;
      for (int k = 0; k < 24 && rolled < 2; ++k) {
         const int s = rng.range(n / 2, n - 1);
         const uint8_t r = static_cast<uint8_t>(rng.range(2, 4));
         if (p.at(h, s).ratchet == r || s == first)
            continue;
         first = s;
         p.at(h, s).vel = static_cast<uint8_t>(std::max<int>(p.at(h, s).vel, kNormal));
         p.at(h, s).ratchet = r;
         ++rolled;
      }
   }
}

// ---- energy ----

// The energy (0..1) a lane comes in at. On a kit the hats come first and the perc last; on the hand
// kits the drums and the bell or clave come first, the shakers and scrapers later.
inline double threshold(int kit, int pad, const Pattern &p) {
   if (kit == kDrumKit) {
      // An open hat on the off-beats is the groove's hat (house's, disco's): it comes in early.
      if (pad == OH) {
         const int beat = stepsPerBeat(p);
         int off = 0;
         for (int s = beat / 2; s < p.steps; s += beat)
            off += p.at(OH, s).on();
         return off * beat >= p.steps * 3 / 4 ? 0.15 : 0.55;
      }
      if (pad == SH)
         return 0.35;
      if (pad == CR)
         return 0.8;
      static const double byRole[kRoleCount] = {0.12, 0.28, 0.0, 0.45, 0.55, 0.6};
      return byRole[padRole(kit, pad)];
   }
   if (kit == kPercussionKit || kit == kAfroLatinKit) {
      static const double byRole[kRoleCount] = {0.05, 0.12, 0.35, 0.15, 0.0, 0.4};
      return byRole[padRole(kit, pad)];
   }
   static const double byRole[kRoleCount] = {0.07, 0.17, 0.0, 0.27, 0.23, 0.36};
   return byRole[padRole(kit, pad)];
}

inline void shapeEnergy(Pattern &p, int kit, double e, int section, Rng &rng, unsigned trad = kTradAny,
                        bool fillIn = true) {
   const int n = p.steps;
   const int beat = stepsPerBeat(p);
   // Lanes the energy is too low for drop out, unless it would leave the pattern empty.
   Pattern before = p;
   // The percussionist (section -1) keeps its busiest lane at any energy: a shaker player's shaker.
   int keepLane = -1;
   if (section < 0) {
      int most = 0;
      for (int pad = 0; pad < kPads; ++pad) {
         int c = 0;
         for (int s = 0; s < n; ++s)
            c += p.at(pad, s).on();
         if (c > most)
            most = c, keepLane = pad;
      }
   }
   for (int pad = 0; pad < kPads; ++pad)
      if (p.uses(pad) && pad != keepLane && e < threshold(kit, pad, before))
         for (int s = 0; s < kMaxSteps; ++s) {
            // An open hat that goes closes: the hat line keeps its hit.
            if (kit == kDrumKit && pad == OH && p.at(OH, s).on() && before.uses(CH) && !p.at(CH, s).on())
               p.at(CH, s) = p.at(OH, s);
            p.at(pad, s) = Hit{};
         }
   // A break drops the kick.
   if (section == kBreak && kit == kDrumKit)
      for (int pad : {K, K2})
         for (int s = 0; s < kMaxSteps; ++s)
            p.at(pad, s) = Hit{};
   if (p.empty() && e >= 0.1)
      p = before;
   // Very low energy thins to anchors: only the beats under 0.2, only the half notes of the
   // lowest lane under 0.1, nothing at all at 0.
   if (e < 0.2) {
      // The lowest sound: the kick (or the hand drum's bass), else the first lane there is.
      const int low = voicesFor(kit, trad).kick;
      int anchor = low >= 0 && p.uses(low) ? low : -1;
      for (int pad = 0; pad < kPads && anchor < 0; ++pad)
         if (p.uses(pad))
            anchor = pad;
      const Voices tv = voicesFor(kit, trad);
      for (int pad = 0; pad < kPads; ++pad)
         for (int s = 0; s < kMaxSteps; ++s) {
            Hit &h = p.at(pad, s);
            // The clave or the theka stays whole while anything plays.
            const bool keep = e >= 0.1 ? onBeat(s, beat) || h.vel >= kAccent || timelineHit(tv, pad, h)
                                       : e > 0.02 && pad == anchor && s % (2 * beat) == 0;
            if (!keep)
               h = Hit{};
         }
      if (p.empty() && e > 0.02 && anchor >= 0)
         p.at(anchor, 0).vel = kSoft;
   }
   // Below 0.75 the soft hits thin out, more the lower it goes (not the timeline's); the louder the
   // section, the louder every hit, and from 0.7 the beats are leaned on.
   const Voices sv = voicesFor(kit, trad);
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         // A kit's hats keep their line (a hole in the hats sounds like a mistake): a quiet one
         // plays its eighths, a louder one all of it.
         if (kit == kDrumKit && padRole(kit, pad) == kHat && pad != OH) {
            if (e < 0.4 && s % 2 == 1 && beat == 4 && p.uses(pad) && h.vel < 80) {
               h = Hit{};
               continue;
            }
         } else if (e < 0.75 && h.vel < 80 && !onBeat(s, beat) && !timelineHit(sv, pad, h) &&
                    rng.chance((0.75 - e) * 1.2)) {
            h = Hit{};
            continue;
         }
         double vel = h.vel * (0.7 + 0.45 * e);
         if (e >= 0.7 && onBeat(s, beat) && padRole(kit, pad) == kHat)
            vel += 10.0;
         h.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(vel)), 1, 127));
      }
   if (kit == kDrumKit && e >= 0.75 && p.uses(CH) && n >= 2 * beat) {
      // The hat opens on the last off-beat, a lift into the next bar.
      const int s = n - beat / 2;
      if (p.at(CH, s).on() && rng.chance(0.4 + e - 0.75)) {
         p.at(OH, s) = p.at(CH, s);
         p.at(CH, s) = Hit{};
      }
   }
   // High energy: the hats fill in (on a hand kit the shaker or scraper, and the drum's soft strokes
   // between its others: a conga's heel and toe, a darbuka's ka, the tabla's dayan, never its bayan)
   // and the kick gets pickups.
   if (e > 0.75 && fillIn) {
      const bool hands = kit == kPercussionKit || kit == kAfroLatinKit;
      const Voices v = voicesFor(kit, trad);
      // (West African: the djembe's tones, never a dunun.)
      const int fillIn = !hands                                          ? -1
                         : trad & (kTradIndian | kTradMiddleEastern)     ? v.snare
                         : trad == kTradWestAfrican                      ? v.tone
                                                                         : busiestLane(p, kit, kTom, kTom);
      const int lanes[2] = {kit == kDrumKit ? (p.uses(CH) || !p.uses(RD) ? CH : RD) : busiestLane(p, kit, kHat, kHat),
                            fillIn};
      for (int h : lanes)
         if (h >= 0 && p.uses(h))
            for (int s = 0; s < n; s += (h == lanes[1] ? 2 : 1) * gridOf(p))
               if (!p.at(h, s).on() && !(kit == kDrumKit && p.at(OH, s).on()) && rng.chance((e - 0.75) * 3.0))
                  p.at(h, s).vel = kGhost + 12;
   }
   if (e > 0.7 && kit == kDrumKit && rng.chance(e - 0.6) && n >= 16 && p.at(K, 0).on() && !p.at(K, n - 2).on())
      p.at(K, n - 2).vel = kSoft;
}

// ---- weird ----

inline void applyWeird(Pattern &p, int kit, double w, Rng &rng, unsigned trad = kTradAny) {
   if (w <= 0.0)
      return;
   const Voices v = voicesFor(kit, trad);
   const int n = p.steps;
   const int beat = stepsPerBeat(p);
   for (int pad = 0; pad < kPads; ++pad) {
      if (!p.uses(pad))
         continue;
      // A hit pulled a step off the grid.
      if (timeline(v, pad))
         continue;
      if (rng.chance(w * 0.7)) {
         const int s = rng.range(1, n - 1);
         const int to = std::clamp(s + (rng.chance(0.5) ? gridOf(p) : -gridOf(p)), 0, n - 1);
         const bool anchored = pad == v.kick && (s == 0 || (onBeat(s, beat) && fourOnFloor(p, v.kick)));
         if (p.at(pad, s).on() && !p.at(pad, to).on() && !anchored) {
            p.at(pad, to) = p.at(pad, s);
            p.at(pad, s) = Hit{};
         }
      }
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         if (!onBeat(s, beat) && rng.chance(w * 0.25))
            h.chance = static_cast<uint8_t>(rng.range(40, 90));
         if (padRole(kit, pad) == kHat && w > 0.3 && rng.chance((w - 0.3) * 0.25))
            h.ratchet = static_cast<uint8_t>(rng.range(2, kMaxRatchet));
      }
   }
   // Stray sounds on pads the groove doesn't use: on a kit the FX, rim and shaker; on a hand kit only
   // the drums of its own tradition.
   std::vector<int> pool;
   if (v.drums())
      pool = w > 0.4 ? std::vector<int>{FX, RIM, SH} : std::vector<int>{RIM, SH};
   else
      for (int pad = 0; pad < kPads; ++pad) {
         // Never a stray bell or clave (they are the time), nor a darbuka in a tabla groove.
         const bool foreign = v.hands() && v.trad == kTradIndian && (pad == DARBUKA_DUM || pad == DARBUKA_TEK) &&
                              !p.uses(DARBUKA_DUM) && !p.uses(DARBUKA_TEK);
         if (!p.uses(pad) && (!v.hands() || v.allows(pad)) && !timelinePad(kit, pad) && !foreign)
            pool.push_back(pad);
      }
   const int strays = static_cast<int>(w * 3.0 + rng.uniform());
   for (int k = 0; k < strays && !pool.empty(); ++k) {
      const int pad = pool[static_cast<size_t>(rng.range(0, static_cast<int>(pool.size()) - 1))];
      const int s = onGrid(p, rng.range(0, n - 1));
      if (!p.at(pad, s).on()) {
         p.at(pad, s).vel = static_cast<uint8_t>(rng.range(50, 100));
         p.at(pad, s).chance = static_cast<uint8_t>(rng.range(50, 100));
      }
   }
   // Very weird: an odd length that drifts against the bar.
   if (n == 16 && w > 0.7 && rng.chance((w - 0.7) * 2.0))
      p.steps = static_cast<uint8_t>(rng.chance(0.5) ? 15 : rng.chance(0.5) ? 14 : 13);
}

// ---- the player ----

// Hits the player adds go only before step `addBefore` (a fill's silence stays silent).
inline void applyPlayer(Pattern &p, int kit, const Player &pl, double e, Rng &rng, unsigned trad = kTradAny,
                        int addBefore = kMaxSteps) {
   const Voices v = voicesFor(kit, trad);
   const int n = p.steps;
   const int addTo = std::clamp(addBefore, 0, n);
   // Ghost notes round the backbeat, or among a hand drum's tones (not in a section kept sparse).
   if (kit != kFxKit && kit != kAnimalKit && e >= 0.2) {
      const int sn = ghostPad(p, v);
      if (p.uses(sn)) {
         const int count = static_cast<int>(std::lround(pl.ghosts * 4.0 * (0.5 + e)));
         for (int k = 0; k < count; ++k) {
            const int g = gridOf(p);
            const int s = rng.range(0, n - 1);
            if (s % (2 * g) == g && s < addTo && !p.at(sn, s).on() && !p.at(sn, s - g).on() &&
                !p.at(sn, (s + g) % n).on())
               p.at(sn, s).vel = static_cast<uint8_t>(rng.range(28, 46));
         }
      }
   }
   // Busy players add a few hits: on a kit a kick or a ghost on the snare, on a hand drum a tone.
   if (pl.busy > 0.5 && e >= 0.2) {
      const int extra = static_cast<int>((pl.busy - 0.5) * 6.0 * e + rng.uniform());
      for (int k = 0; k < extra; ++k) {
         int pad;
         uint8_t vel = kSoft;
         if (v.hands())
            pad = p.uses(v.tone) ? v.tone : hatLane(p, v);
         else if (v.drums() && rng.chance(0.5)) {
            pad = backbeatPad(p, v);
            vel = kGhost;
         } else
            pad = v.kick;
         const int s = onGrid(p, rng.range(0, n - 1));
         if (s < addTo && p.uses(pad) && !p.at(pad, s).on())
            p.at(pad, s).vel = vel;
      }
   }
   // Timing: a slow push and pull across the bar, a little jitter on every hit, and on a kit the
   // backbeat and the hats where the player sits them (the backbeat varying a little).
   const double phase = rng.uniform() * 6.283185307179586;
   const double drift = pl.loose * 6.0;
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         const Role role = padRole(kit, pad);
         double nudge = drift * std::sin(6.283185307179586 * s / std::max(1, n) + phase) +
                        (rng.uniform() * 2.0 - 1.0) * pl.loose * 4.0;
         if ((v.drums() ? role == kSnare : pad == v.snare) && h.vel >= kSoft)
            nudge += pl.backbeatFeel + (pl.loose > 0.0 ? rng.range(-2, 2) : 0);
         if (role == kHat && !(v.drums() && pad == RD))
            nudge += pl.hatFeel;
         h.nudge = static_cast<int8_t>(std::clamp(static_cast<int>(std::lround(h.nudge + nudge)), -kMaxNudge, kMaxNudge));
         const int jitter = static_cast<int>(std::lround((rng.uniform() * 2.0 - 1.0) * pl.loose * 10.0));
         int vel = h.vel + jitter;
         // A drummer's hand on the hats: the sixteenth between two eighths softer.
         if (v.drums() && pl.loose > 0.0 && (pad == CH || pad == RD) && s % 2 == 1 && p.at(pad, s - 1).on() &&
             h.vel >= p.at(pad, s - 1).vel)
            vel = static_cast<int>(vel * 0.6);
         h.vel = static_cast<uint8_t>(std::clamp(vel, 1, 127));
      }
}

// ---- fills ----

// A fill: the end of the bar (one to four beats, more with a busier player and a louder section)
// played as the genre plays it, in one of a few shapes. The kick keeps the time on a kit; on a hand
// kit the bell, clave and shakers do.
inline Pattern makeFill(const Pattern &base, int kit, FillKind kind, double fills, double e, Rng &rng,
                        unsigned trad = kTradAny) {
   const Voices v = voicesFor(kit, trad);
   Pattern f = base;
   const int n = f.steps;
   const int beat = stepsPerBeat(f);
   const int bar = std::min(n, 4 * beat);
   const int beats = std::clamp(1 + (fills >= 0.5) + (e > 0.75) + (fills > 0.75 && e > 0.55), 1, 4);
   int len = std::min(bar, beats * beat);
   if (kind == kFillDjembe || kind == kFillTihai)
      len = bar; // a call, or three phrases, take the bar
   const int from = (n - len) / beat * beat; // on a beat, in any meter
   const int shape = rng.range(0, 3);
   // Clear under the fill.
   for (int pad = 0; pad < kPads; ++pad) {
      const Role r = padRole(kit, pad);
      bool keep = isKickPad(kit, pad) && kind != kFillDrop;
      // A hat roll or a chop keeps the backbeat (a trap bar's only clap is on 3).
      if ((kind == kFillHats || kind == kFillChop) && v.drums() && r == kSnare)
         keep = true;
      if (v.hands())
         keep = r == kPerc || r == kHat;
      if (!keep)
         for (int s = from; s < n; ++s)
            f.at(pad, s) = Hit{};
   }
   if (v.hands())
      for (int pad : {v.kick, v.snare, v.tone, v.toms[0], v.toms[1], v.toms[2]})
         for (int s = from; s < n; ++s)
            f.at(pad, s) = Hit{};
   auto ramp = [&](int s) { return static_cast<uint8_t>(std::clamp(60 + (s - from) * 60 / std::max(1, len - 1), 1, 127)); };
   auto hit = [&](int pad, int s, uint8_t vel) {
      if (pad >= 0 && s >= 0 && s < n) {
         f.at(pad, s) = Hit{};
         f.at(pad, s).vel = vel;
      }
   };
   switch (kind) {
   case kFillToms:
   case kFillHands: {
      // Round the drums, high to low: on a kit the snare and toms, on hand drums the slap and the
      // drums from the highest down.
      const int drums[4] = {v.drums() ? SN : v.snare, v.toms[0], v.toms[1], v.toms[2]};
      const int sh = kind == kFillHands && v.drums() ? 0 : shape;
      for (int s = from; s < n; ++s) {
         const int i = s - from;
         switch (sh) {
         case 1: // sixteenths on the snare, then eighths round the toms
            if (i < len / 2)
               hit(drums[0], s, ramp(s));
            else if (s % 2 == 0)
               hit(drums[1 + std::min(2, (i - len / 2) * 3 / std::max(1, len - len / 2))], s, ramp(s));
            break;
         case 2: // groups of three across the beat
            hit(drums[i % 3 == 0 ? 0 : i % 3], s, static_cast<uint8_t>(i % 3 == 0 ? ramp(s) : ramp(s) - 14));
            break;
         case 3: // eighths, snare and floor tom, each with a soft pickup now and then
            if (s % 2 == 0) {
               hit(drums[(i / 2) % 2 ? 3 : 0], s, ramp(s));
               if (s > from && rng.chance(fills))
                  hit(drums[(i / 2) % 2 ? 3 : 0], s - 1, kGhost + 8);
            }
            break;
         default: // down the drums, high to low
            if (s % 2 == 0 || rng.chance(fills))
               hit(drums[std::min(3, i * 4 / std::max(1, len))], s, ramp(s));
         }
      }
      if (v.drums())
         hit(K, n - 1, kNormal);
      break;
   }
   case kFillSnare: {
      const int sn = v.drums() ? SN : v.snare;
      for (int s = from; s < n; ++s) {
         const int i = s - from;
         switch (shape) {
         case 1: // eighths, then sixteenths in the last beat
            if (s % 2 == 0 || s >= n - beat)
               hit(sn, s, ramp(s));
            break;
         case 2: // accents every third sixteenth, ghosts between
            hit(sn, s, static_cast<uint8_t>(i % 3 == 0 ? ramp(s) : kGhost + 10));
            break;
         default: // a crescendo
            if (s >= n - beat || s % 2 == 0 || rng.chance(fills))
               hit(sn, s, ramp(s));
         }
      }
      if (fills > 0.6)
         f.at(sn, n - 1).ratchet = 2;
      break;
   }
   case kFillHats: {
      const int h = v.drums() ? CH : hatLane(base, v);
      for (int s = from; s < n; ++s) {
         if (shape == 1 && v.openHat >= 0 && s % beat == beat / 2) {
            hit(v.openHat, s, ramp(s));
            continue;
         }
         hit(h, s, ramp(s));
         if (s >= n - beat && rng.chance(0.6))
            f.at(h, s).ratchet = static_cast<uint8_t>(rng.range(2, 4));
      }
      const int bb = v.drums() ? backbeatPad(base, v) : v.snare;
      bool any = false;
      for (int s = from; s < n; ++s)
         any |= f.at(bb, s).on();
      if (!any)
         hit(bb, std::max(from, n - beat), kAccent);
      break;
   }
   case kFillChop: {
      // The producer's fill: the kick and the hats drop out for the last beat (the backbeat plays
      // on), a kick picks up into the one, sometimes a stutter on the snare.
      const int cut = std::max(0, n - beat);
      for (int s = cut; s < n; ++s)
         for (int pad = 0; pad < kPads; ++pad)
            if (padRole(kit, pad) != kSnare)
               f.at(pad, s) = Hit{};
      hit(v.kick, n - 1, kNormal);
      if (shape % 2 && v.drums()) {
         const int sn = backbeatPad(base, v);
         hit(sn, n - 2, kSoft);
         f.at(sn, n - 2).ratchet = 2;
      }
      break;
   }
   case kFillDrop: {
      if (shape % 2) {
         // The band drops out: the kick too, one hit on the last off-beat into the drop.
         for (int s = from; s < n; ++s)
            for (int pad = 0; pad < kPads; ++pad)
               f.at(pad, s) = Hit{};
         hit(v.drums() ? (base.uses(SN) && !base.uses(CL) ? SN : CL) : v.snare, n - std::max(1, beat / 2), kAccent);
      } else {
         // On the clap, unless the groove plays a snare and no clap.
         const int cl = v.drums() ? (base.uses(SN) && !base.uses(CL) ? SN : CL) : v.snare;
         for (int s = from; s < n; ++s)
            if (s % 2 == 0 || s >= n - beat)
               hit(cl, s, ramp(s));
      }
      break;
   }
   case kFillFx: {
      const int fx = v.drums() ? FX : 15;
      for (int s = from; s < n; s += 2)
         hit(fx, s, ramp(s));
      f.at(fx, std::max(from, n - 2)).ratchet = 3;
      hit(v.drums() ? SN : v.snare, n - 1, kAccent);
      break;
   }
   case kFillDjembe: {
      // The djembe call, as taught: S s . s . s . s S . S . S in 4/4, T f . T t . T t . T t in 12/8,
      // the ensemble answering on the one.
      static const int call8[7] = {0, 3, 5, 7, 8, 10, 12}; // in sixteenths, 4/4
      static const int call12[7] = {0, 2, 3, 5, 6, 8, 9};   // in 12/8 eighths
      if (beat == 6)
         for (int x : call12)
            hit(v.snare, from + 2 * x, kAccent);
      else
         for (int x : call8)
            if (from + x < n)
               hit(v.snare, from + x, kAccent);
      break;
   }
   case kFillDarbuka: {
      // A tek roll, rolled into twos as it builds, landing on a doum on the last beat.
      hit(v.kick, from, kAccent);
      hit(v.kick, std::max(from, n - beat), kAccent);
      for (int s = from; s < n; ++s) {
         if (f.at(v.kick, s).on())
            continue; // a doum is not also a tek
         hit(v.snare, s, ramp(s));
         if (rng.chance(0.3 + fills * 0.4))
            f.at(v.snare, s).ratchet = 2;
      }
      break;
   }
   case kFillTihai: {
      // A tihai: a short phrase three times, the third ending on the next bar's one. With strokes
      // `d` apart and a rest of `d` between phrases, the last stroke falls `span` steps after the first.
      int d = len >= 20 ? 2 : 1;
      const int k = 3;
      int span = 2 * (k * d + d) + (k - 1) * d;
      if (span > len) {
         d = 1;
         span = 2 * (k * d + d) + (k - 1) * d;
      }
      const int start = n - span;
      // The theka plays on until the tihai starts.
      for (int pad = 0; pad < kPads; ++pad)
         for (int s = from; s < start; ++s)
            f.at(pad, s) = base.at(pad, s);
      for (int rep = 0; rep < 3; ++rep)
         for (int i = 0; i < k; ++i) {
            const int s = start + rep * (k * d + d) + i * d;
            hit(v.snare, s, static_cast<uint8_t>(std::clamp(80 + 12 * rep + (i == 0 ? 10 : 0), 1, 127)));
            if (i == 0)
               hit(v.kick, s, static_cast<uint8_t>(70 + 12 * rep));
         }
      break;
   }
   case kFillCajon: {
      // A cajon break: slaps in sixteenths building, the bass on the beats.
      for (int s = from; s < n; ++s) {
         if (s % beat == 0)
            hit(v.kick, s, ramp(s));
         else if (shape == 1 ? s % 2 == 0 : true)
            hit(v.snare, s, ramp(s));
      }
      hit(v.snare, n - 1, kAccent);
      break;
   }
   }
   return f;
}

// ---- mutations ----

// One way a pattern can change, as the MUTATE card tags a child. Some are for a kit, some for hand
// drums, some for both.
enum MutationWhere : unsigned { kOnKits = 1, kOnHands = 2, kOnBoth = 3 };
struct Mutation {
   const char *tag;
   bool (*apply)(Pattern &, const Voices &, Rng &);
   unsigned where = kOnBoth;
};

inline bool hasOnBeatHit(const Pattern &p, int pad) { return p.uses(pad); }

inline const std::vector<Mutation> &mutations() {
   static const std::vector<Mutation> m = {
      {"more ghost snares",
       [](Pattern &p, const Voices &v, Rng &r) {
          const int sn = ghostPad(p, v);
          if (!p.uses(sn))
             return false;
          int added = 0;
          for (int k = 0; k < 6 && added < 3; ++k) {
             const int s = r.range(0, p.steps - 1);
             if (s % 2 && !p.at(sn, s).on()) {
                p.at(sn, s).vel = static_cast<uint8_t>(r.range(28, 45));
                ++added;
             }
          }
          return added > 0;
       },
       kOnKits},
      {"a kick moves",
       [](Pattern &p, const Voices &v, Rng &r) {
          if (fourOnFloor(p, v.kick))
             return false;
          std::vector<int> hits;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(v.kick, s).on())
                hits.push_back(s);
          if (hits.size() < 2)
             return false;
          const int s = hits[static_cast<size_t>(r.range(1, static_cast<int>(hits.size()) - 1))];
          const int to = std::clamp(s + (r.chance(0.5) ? 1 : -1), 1, p.steps - 1);
          if (p.at(v.kick, to).on())
             return false;
          p.at(v.kick, to) = p.at(v.kick, s);
          p.at(v.kick, s) = Hit{};
          return true;
       },
       kOnKits},
      {"lays back further",
       [](Pattern &p, const Voices &v, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(v.kit, pad) == kSnare || padRole(v.kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on()) {
                      Hit &h = p.at(pad, s);
                      h.nudge = static_cast<int8_t>(std::clamp(h.nudge + r.range(5, 10), -kMaxNudge, kMaxNudge));
                      any = true;
                   }
          return any;
       }},
      {"pushes ahead",
       [](Pattern &p, const Voices &v, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(v.kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on()) {
                      Hit &h = p.at(pad, s);
                      h.nudge = static_cast<int8_t>(std::clamp(h.nudge - r.range(4, 9), -kMaxNudge, kMaxNudge));
                      any = true;
                   }
          return any;
       }},
      {"half-time feel",
       [](Pattern &p, const Voices &v, Rng &) {
          const int sn = backbeatPad(p, v);
          const int beat = stepsPerBeat(p);
          if (p.steps != 4 * beat || !p.at(sn, beat).on() || !p.at(sn, 3 * beat).on() || fourOnFloor(p, v.kick))
             return false;
          p.at(sn, beat) = Hit{};
          p.at(sn, 3 * beat) = Hit{};
          p.at(sn, 2 * beat).vel = kAccent;
          return true;
       },
       kOnKits},
      {"toms come in",
       [](Pattern &p, const Voices &v, Rng &r) {
          // Not into a club groove that has no toms.
          if (!v.drums() || (fourOnFloor(p, v.kick) && !p.uses(HT) && !p.uses(MT) && !p.uses(LT)))
             return false;
          const int toms[3] = {HT, MT, LT};
          for (int k = 0; k < 3; ++k) {
             const int s = r.range(p.steps / 2, p.steps - 1);
             p.at(toms[k], s).vel = static_cast<uint8_t>(r.range(70, 105));
          }
          return true;
       },
       kOnKits},
      {"hats open up",
       [](Pattern &p, const Voices &v, Rng &r) {
          if (!v.drums() || !p.uses(CH))
             return false;
          int moved = 0;
          const int beat = stepsPerBeat(p);
          for (int s = beat / 2; s < p.steps; s += beat)
             if (p.at(CH, s).on() && r.chance(0.6)) {
                p.at(OH, s) = p.at(CH, s);
                p.at(CH, s) = Hit{};
                ++moved;
             }
          return moved > 0;
       },
       kOnKits},
      {"busier kick",
       [](Pattern &p, const Voices &v, Rng &r) {
          if (p.steps < 2 || !p.uses(v.kick))
             return false;
          int added = 0;
          for (int k = 0; k < 4 && added < 2; ++k) {
             const int s = r.range(1, p.steps - 1);
             if (!p.at(v.kick, s).on() && !onBeat(s, stepsPerBeat(p))) {
                p.at(v.kick, s).vel = static_cast<uint8_t>(r.range(60, 90));
                ++added;
             }
          }
          return added > 0;
       },
       kOnKits},
      {"sparser",
       [](Pattern &p, const Voices &v, Rng &r) {
          int removed = 0;
          for (int k = 0; k < 24 && removed < 3; ++k) {
             const int pad = r.range(0, kPads - 1), s = r.range(0, p.steps - 1);
             Hit &h = p.at(pad, s);
             // On hand drums the bell and the clave are the time: they stay.
             if (v.hands() && (padRole(v.kit, pad) == kPerc || timelineHit(v, pad, h)))
                continue;
             if (h.on() && !onBeat(s, stepsPerBeat(p)) && h.vel < kAccent) {
                h = Hit{};
                ++removed;
             }
          }
          return removed > 0;
       }},
      {"rolls on the hats",
       [](Pattern &p, const Voices &v, Rng &r) {
          const int h = v.drums() ? CH : v.hat;
          if (!p.uses(h))
             return false;
          for (int k = 0; k < 2; ++k) {
             const int s = r.range(p.steps / 2, p.steps - 1);
             p.at(h, s).vel = static_cast<uint8_t>(std::max<int>(p.at(h, s).vel, 80));
             p.at(h, s).ratchet = static_cast<uint8_t>(r.range(2, 4));
          }
          return true;
       },
       kOnKits},
      {"hats left to chance",
       [](Pattern &p, const Voices &v, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(v.kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on() && !onBeat(s, stepsPerBeat(p)) && r.chance(0.4)) {
                      p.at(pad, s).chance = static_cast<uint8_t>(r.range(45, 85));
                      any = true;
                   }
          return any;
       }},
      {"rim clicks join",
       [](Pattern &p, const Voices &v, Rng &r) {
          if (!v.drums() || p.uses(RIM))
             return false;
          for (int k = 0; k < 3; ++k)
             p.at(RIM, r.range(0, p.steps - 1)).vel = static_cast<uint8_t>(r.range(60, 90));
          return true;
       },
       kOnKits},
      {"shaker joins",
       [](Pattern &p, const Voices &v, Rng &) {
          const int sh = v.shaker;
          if (sh < 0 || p.uses(sh) || (v.hands() && !v.allows(sh)))
             return false;
          // On the grid's eighths in 12/8 (louder on the beat's off-eighths), its sixteenths in 4/4.
          const int g = gridOf(p);
          for (int s = 0; s < p.steps; s += g)
             p.at(sh, s).vel = static_cast<uint8_t>(g == 2 ? (s % 6 ? kGhost : kSoft) : s % 2 ? kSoft : kGhost);
          return true;
       }},
      {"double-time hats",
       [](Pattern &p, const Voices &v, Rng &) {
          const int h = v.drums() ? CH : v.hat;
          if (!p.uses(h))
             return false;
          int added = 0;
          for (int s = 0; s < p.steps; ++s)
             if (!p.at(h, s).on() && !(v.openHat >= 0 && p.at(v.openHat, s).on())) {
                p.at(h, s).vel = kGhost + 10;
                ++added;
             }
          return added > 0;
       },
       kOnKits},
      {"accents move",
       [](Pattern &p, const Voices &v, Rng &r) {
          const int h = v.drums() ? CH : v.hat;
          // Only a timekeeper's accents: never a tabla's or a darbuka's strokes.
          if (!p.uses(h) || padRole(v.kit, h) != kHat)
             return false;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(h, s).on())
                p.at(h, s).vel = static_cast<uint8_t>(r.chance(0.3) ? kAccent - 10 : kSoft - 10);
          return true;
       }},
      {"clap doubles the snare",
       [](Pattern &p, const Voices &v, Rng &) {
          if (!v.drums() || !p.uses(SN) || p.uses(CL))
             return false;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(SN, s).vel >= kNormal)
                p.at(CL, s).vel = kNormal;
          return true;
       },
       kOnKits},
      {"perc answers",
       [](Pattern &p, const Voices &v, Rng &r) {
          const int pc = v.perc;
          int added = 0;
          for (int k = 0; k < 4; ++k) {
             const int s = r.range(0, p.steps - 1);
             if (!onBeat(s, stepsPerBeat(p)) && !p.at(pc, s).on()) {
                p.at(pc, s).vel = static_cast<uint8_t>(r.range(60, 100));
                ++added;
             }
          }
          return added > 0;
       },
       kOnKits},
      // A soft grace note the step before each backbeat hit. (Not a ratchet: its second hit comes
      // after the backbeat, which sounds like a double, not a drag.)
      {"drags into the backbeat",
       [](Pattern &p, const Voices &v, Rng &) {
          const int sn = backbeatPad(p, v);
          bool any = false;
          for (int s = 1; s < p.steps; ++s)
             if (p.at(sn, s).vel >= kNormal && !p.at(sn, s - 1).on()) {
                p.at(sn, s - 1).vel = kGhost + 8;
                any = true;
             }
          return any;
       },
       kOnKits},
      // Hand drums: what a hand drummer changes from one time round to the next.
      {"open tones move",
       [](Pattern &p, const Voices &v, Rng &r) {
          if (timeline(v, v.tone))
             return false; // a bol or a tek stays on its matra
          std::vector<int> tones;
          for (int s = 1; s < p.steps; ++s)
             if (p.at(v.tone, s).vel >= kNormal)
                tones.push_back(s);
          if (tones.empty())
             return false;
          const int s = tones[static_cast<size_t>(r.range(0, static_cast<int>(tones.size()) - 1))];
          const int g = gridOf(p);
          const int to = std::clamp(s + (r.chance(0.5) ? g : -g), 1, p.steps - 1);
          if (p.at(v.tone, to).on())
             return false;
          p.at(v.tone, to) = p.at(v.tone, s);
          p.at(v.tone, s) = Hit{};
          return true;
       },
       kOnHands},
      {"slap answers",
       [](Pattern &p, const Voices &v, Rng &r) {
          int added = 0;
          for (int k = 0; k < 8 && added < 2; ++k) {
             const int s = onGrid(p, r.range(p.steps / 2, p.steps - 1));
             if (!onBeat(s, stepsPerBeat(p)) && !p.at(v.snare, s).on() && !p.at(v.tone, s).on() &&
                 !timeline(v, v.snare)) {
                p.at(v.snare, s).vel = static_cast<uint8_t>(r.range(85, 110));
                ++added;
             }
          }
          return added > 0;
       },
       kOnHands},
      {"bass on the beats only",
       [](Pattern &p, const Voices &v, Rng &) {
          // Not where the bass is the rhythm's own (Saidi's two doums, the tabla's bayan).
          if (timeline(v, v.kick))
             return false;
          bool any = false;
          for (int s = 1; s < p.steps; ++s)
             if (p.at(v.kick, s).on() && !onBeat(s, stepsPerBeat(p))) {
                p.at(v.kick, s) = Hit{};
                any = true;
             }
          return any;
       },
       kOnHands},
      {"roll into the one",
       [](Pattern &p, const Voices &v, Rng &r) {
          const int pad = p.uses(v.tone) ? v.tone : v.snare;
          const int s = onGrid(p, p.steps - 1 - r.range(0, 1) * gridOf(p));
          p.at(pad, s).vel = static_cast<uint8_t>(std::max<int>(p.at(pad, s).vel, kNormal));
          p.at(pad, s).ratchet = static_cast<uint8_t>(r.range(2, 3));
          return true;
       },
       kOnHands},
   };
   return m;
}

// A child of `parent`: one twist (two or three as `amount` grows), and the tag that says how it
// differs. Ops already used for this litter (`used`) are avoided, so five children differ. On the
// hand kits only the hand drummers' twists, kept to the tradition's own drums.
// `inSong`: a section's own variation, which keeps the groove's feel (no half time mid-verse).
inline Pattern mutate(const Pattern &parent, int kit, double amount, Rng &rng, std::string &tag,
                      std::vector<int> *used = nullptr, unsigned trad = kTradAny, bool inSong = false) {
   const auto &ops = mutations();
   const Voices v = voicesFor(kit, trad);
   const unsigned where = v.hands() ? kOnHands : kOnKits;
   Pattern child = parent;
   tag.clear();
   const int wanted = 1 + (rng.chance(amount) ? 1 : 0) + (amount > 0.7 && rng.chance(amount - 0.5) ? 1 : 0);
   int applied = 0;
   for (int tries = 0; tries < 60 && applied < wanted; ++tries) {
      const int i = rng.range(0, static_cast<int>(ops.size()) - 1);
      if (!(ops[static_cast<size_t>(i)].where & where))
         continue;
      if (used && applied == 0 && std::find(used->begin(), used->end(), i) != used->end())
         continue;
      if (inSong && !std::strcmp(ops[static_cast<size_t>(i)].tag, "half-time feel"))
         continue;
      if (!ops[static_cast<size_t>(i)].apply(child, v, rng))
         continue;
      if (applied == 0) {
         tag = ops[static_cast<size_t>(i)].tag;
         if (used)
            used->push_back(i);
      } else
         tag += std::string(", ") + ops[static_cast<size_t>(i)].tag;
      ++applied;
   }
   if (tag.empty())
      tag = "nudged";
   return child;
}

// Applies the mutation with this tag, if it can (for the generator's own choices).
inline bool applyMutation(Pattern &p, const char *tag, int kit, Rng &rng, unsigned trad = kTradAny) {
   const Voices v = voicesFor(kit, trad);
   for (const Mutation &m : mutations())
      if (std::string(m.tag) == tag)
         return m.apply(p, v, rng);
   return false;
}

// ---- sections and songs ----

inline const Style &styleOf(const Recipe &r) {
   const Genre &g = genreAt(r.genre);
   return g.styles[static_cast<size_t>(std::clamp(r.style, 0, static_cast<int>(g.styles.size()) - 1))];
}

// The energy a section of a recipe is made with, 0..1.
// ENERGY scales the arc (at 50 each section plays as set), so a section set to 0 stays silent.
// Above 50 it moves each section towards full by a share of its headroom, so the chorus stays
// above the pre however hard it is pushed.
inline double sectionEnergy(const Recipe &r, int section) {
   const double arc = r.sections[static_cast<size_t>(section)];
   if (r.energy <= 0.5 || arc <= 0.0)
      return std::clamp(arc * (0.5 + r.energy), 0.0, 1.0);
   return std::clamp(arc + (1.0 - arc) * (r.energy - 0.5) * 1.2, 0.0, 1.0);
}

// A groove's length in ticks.
inline int patternTicks(const Groove &g) {
   int steps = 1;
   for (const Lane &l : g.lanes)
      steps = std::max(steps, laneLength(l.hits));
   return std::min(steps, kMaxSteps) * kRateTicks[std::min<int>(g.rate, kRate32)];
}
// Whether one pattern's cycle fits whole in the other's, so the two stay together.
inline bool sameMeter(int ticksA, int ticksB) { return ticksA % ticksB == 0 || ticksB % ticksA == 0; }

// A groove from another kit, its lanes moved to the pads of this kit that do the same job (a drum
// kit's snare onto a hand kit's slap, its hats onto a shaker), within this kit's tradition.
inline Pattern onKit(const Pattern &q, int fromKit, int toKit, unsigned trad) {
   if (fromKit == toKit)
      return q;
   const Voices v = voicesFor(toKit, trad);
   Pattern out;
   out.steps = q.steps;
   out.rate = q.rate;
   out.clear();
   std::array<bool, kPads> taken{};
   for (int pad = 0; pad < kPads; ++pad) {
      if (!q.uses(pad))
         continue;
      const Role role = padRole(fromKit, pad);
      int to = -1;
      for (int pass = 0; pass < 2 && to < 0; ++pass)
         for (int t = 0; t < kPads && to < 0; ++t)
            if (padRole(toKit, t) == role && (pass || !taken[static_cast<size_t>(t)]) && (!v.hands() || v.allows(t)))
               to = t;
      if (to < 0)
         continue;
      taken[static_cast<size_t>(to)] = true;
      for (int s = 0; s < kMaxSteps; ++s)
         if (q.at(pad, s).on() && !out.at(to, s).on())
            out.at(to, s) = q.at(pad, s);
   }
   return out;
}

// A Cuban groove borrowed into a song keeps its clave: one with another clave (2-3 against 3-2, or
// rumba against son) would cross it. A groove without a clave goes with any.
inline bool sameClave(const Groove &a, const Groove &b, unsigned trad) {
   if (trad != kTradCuban)
      return true;
   const auto clave = [](const Groove &g) -> std::string {
      for (const Lane &l : g.lanes)
         if (l.pad == CLAVE && l.hits)
            return l.hits;
      return {};
   };
   const std::string ca = clave(a), cb = clave(b);
   return ca.empty() || cb.empty() || ca == cb;
}

// The base groove of a section: the style's, sometimes its genre's other grooves for variety.
inline Pattern baseGroove(const Recipe &r, int section, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   const int own = std::clamp(st.groove, 0, static_cast<int>(g.grooves.size()) - 1);
   int gi = own;
   // The bridge, and now and then a weird verse, borrow another groove of the genre (one in the
   // same meter, so a 4/4 song doesn't fall into 12/8).
   if (g.grooves.size() > 1 && (section == kBridge || rng.chance(r.weird * 0.3))) {
      std::vector<int> others;
      const Groove &mine = g.grooves[static_cast<size_t>(own)];
      for (int i = 0; i < static_cast<int>(g.grooves.size()); ++i) {
         const Groove &o = g.grooves[static_cast<size_t>(i)];
         // A hand genre keeps its cycle (a taal, a clave, a bell) whole: only a groove as long.
         const bool fits = g.tradition != kTradAny ? patternTicks(o) == patternTicks(mine)
                                                  : sameMeter(patternTicks(o), patternTicks(mine));
         if (i != own && o.rate == mine.rate && fits && sameClave(o, mine, g.tradition))
            others.push_back(i);
      }
      if (!others.empty())
         gi = others[static_cast<size_t>(rng.range(0, static_cast<int>(others.size()) - 1))];
   }
   Pattern p = fromGroove(g.grooves[static_cast<size_t>(gi)]);
   if (r.genre2 >= 0 && r.genre2 != r.genre && r.blend > 0.0) {
      const Genre &g2 = genreAt(r.genre2);
      const Groove &gq = g2.grooves[static_cast<size_t>(rng.range(0, static_cast<int>(g2.grooves.size()) - 1))];
      Pattern q = fromGroove(gq);
      // Blended across kits by what each pad does; not across meters.
      if (q.rate == p.rate)
         p = blendPatterns(p, onKit(q, g2.kit, g.kit, g.tradition), r.blend, rng);
   }
   applyMods(p, st.mods, g.kit, rng, g.tradition);
   return p;
}

// The percussionist's groove for a section: one of its own that fits the drummer's meter (the same
// step and a bar that fits whole), else one of its grooves laid over the drummer's bar.
inline Pattern percussionPart(int who, const Pattern &drums, Rng &rng, unsigned trad = kTradAny) {
   const auto &grooves = percussionGrooves(who);
   const int ticks = patternTicks(drums);
   std::vector<int> best, fits;
   // Over a genre of its own tradition it plays that tradition's grooves (the tabla over a taal,
   // not the darbuka), when it has one.
   const int kit = percussionistKit(who);
   const auto ofTrad = [&](const Groove &gr) {
      // The Percussion kit's darbuka and tabla serve either tradition: a groove led by the darbuka
      // is Middle Eastern, one without it Indian.
      bool darbuka = false;
      for (const Lane &l : gr.lanes)
         darbuka = darbuka || (kit == kPercussionKit && (l.pad == DARBUKA_DUM || l.pad == DARBUKA_TEK));
      if (kit == kPercussionKit && trad == kTradIndian)
         return !darbuka;
      if (kit == kPercussionKit && trad == kTradMiddleEastern)
         return darbuka;
      for (const Lane &l : gr.lanes)
         if (!(padTradition(kit, l.pad) & trad))
            return false;
      return true;
   };
   bool any = false;
   for (const Groove &gr : grooves)
      any = any || ofTrad(gr);
   // Only a groove on the drummer's grid: sixteenths under a 12/8 drummer would fight it (straight
   // eighths and sixteenths go together). The same step and bar first, else one whose cycle fits.
   const auto triplet = [](int rate) { return rate == kRate16T; };
   for (int i = 0; i < static_cast<int>(grooves.size()); ++i) {
      const Groove &gr = grooves[static_cast<size_t>(i)];
      if (triplet(gr.rate) != triplet(drums.rate) || !sameMeter(patternTicks(gr), ticks) || (any && !ofTrad(gr)))
         continue;
      (gr.rate == drums.rate && patternTicks(gr) == ticks ? best : fits).push_back(i);
   }
   const std::vector<int> &pick = !best.empty() ? best : fits;
   if (!pick.empty()) {
      const Groove &gr = grooves[static_cast<size_t>(pick[static_cast<size_t>(rng.range(0, static_cast<int>(pick.size()) - 1))])];
      Pattern p = fromGroove(gr);
      return p;
   }
   // Nothing in this meter (a 5/4, a 7/8 the percussionist has no groove for): a groove at the
   // drummer's step, cut or carried on to the drummer's length (past its end from its second beat,
   // so the one isn't heard twice in a bar).
   int gi = 0;
   for (int i = 0; i < static_cast<int>(grooves.size()); ++i)
      if (triplet(grooves[static_cast<size_t>(i)].rate) == triplet(drums.rate)) {
         gi = i;
         break;
      }
   const Pattern src = fromGroove(grooves[static_cast<size_t>(gi)]);
   Pattern p;
   p.rate = src.rate;
   p.steps = static_cast<uint8_t>(std::clamp(ticks / kRateTicks[std::min<int>(src.rate, kRate32)], 1, kMaxSteps));
   p.clear();
   const int len = std::max<int>(1, src.steps), beat = std::min(stepsPerBeat(src), len - 1);
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < p.steps; ++s)
         p.at(pad, s) = src.at(pad, s < len ? s : beat + (s - len) % std::max(1, len - beat));
   return p;
}

// Where a fill starts: the first step it changes.
inline int fillStart(const Pattern &base, const Pattern &fill) {
   for (int st = 0; st < fill.steps; ++st)
      for (int pad = 0; pad < kPads; ++pad)
         if (fill.at(pad, st) != base.at(pad, st))
            return st;
   return fill.steps;
}

inline Section makeSection(const Recipe &r, int section, uint32_t seed) {
   Rng rng(seed);
   const Genre &g = genreAt(r.genre);
   const int kit = g.kit;
   const unsigned trad = g.tradition;
   const Player &pl = kDrummerList[std::clamp(r.player, 0, kDrummers - 1)];
   const double e = sectionEnergy(r, section);
   Section s;
   s.seed = seed;
   s.bars = static_cast<uint8_t>(kSectionBars[section]);
   s.energy = static_cast<uint8_t>(std::lround(e * 100.0));
   s.crash = kit == kDrumKit && (section == kChorus || e >= 0.75) && e >= 0.2 ? 1 : 0;
   // A sparse section stays sparse: no stray sounds, no added hits, no fill busier than the groove.
   const bool sparse = e < 0.2;
   const bool live = g.family == kFamilies[3] || g.family == kFamilies[4];

   Pattern base = baseGroove(r, section, rng);
   const Pattern meter = base; // its length before WEIRD, for the percussionist
   // The bar: 4/4 unless the groove is in another meter (a groove of two 4/4 bars is still 4/4).
   const int ticks = patternTicks(meter);
   s.meter = static_cast<uint16_t>(ticks % kTicksPerBar == 0 ? kTicksPerBar : ticks);
   // A live drummer moves to the ride for a chorus.
   // (Not a groove whose hat opens: funk and disco keep their hats.)
   if (kit == kDrumKit && g.family == kFamilies[3] && section == kChorus && base.uses(CH) && !base.uses(RD) &&
       !base.uses(OH) && rng.chance(0.6))
      for (int st = 0; st < base.steps; ++st) {
         base.at(RD, st) = base.at(CH, st);
         base.at(CH, st) = Hit{};
         // The foot keeps the hat on 2 and 4.
         if (st % (2 * stepsPerBeat(base)) == stepsPerBeat(base) && !base.at(PH, st).on())
            base.at(PH, st).vel = kSoft;
      }
   // A sparse or half-time style stays spacious at full tilt.
   const unsigned mods = styleOf(r).mods;
   shapeEnergy(base, kit, e, section, rng, trad, !(mods & (kSparse | kHalfTime)));
   applyWeird(base, kit, sparse ? 0.0 : r.weird, rng, trad);
   Pattern played = base;
   applyPlayer(played, kit, pl, e, rng, trad);
   s.vars[0] = played;
   // Variations: small mutations of the groove, the last a little further (and on a live kit, the
   // toms in it).
   // A lane the section's energy took out stays out of its variations.
   const auto keepOut = [&](Pattern &m, const Pattern &from, int k) {
      for (int pad = 0; pad < kPads; ++pad)
         if (!from.uses(pad) && m.uses(pad) && e < threshold(k, pad, m))
            for (int st = 0; st < kMaxSteps; ++st)
               m.at(pad, st) = Hit{};
   };
   for (int v = 1; v < kVariations; ++v) {
      std::string tag;
      Pattern m = sparse ? base : mutate(base, kit, 0.15 + 0.2 * v + r.weird * 0.3, rng, tag, nullptr, trad, true);
      if (!sparse && live && v == kVariations - 1 && kit == kDrumKit && e >= threshold(kit, HT, base))
         applyMutation(m, "toms come in", kit, rng, trad);
      keepOut(m, base, kit);
      applyPlayer(m, kit, pl, e, rng, trad);
      s.vars[static_cast<size_t>(v)] = m;
   }
   Pattern fill = sparse ? base : makeFill(base, kit, g.fill, pl.fills, e, rng, trad);
   applyPlayer(fill, kit, pl, e, rng, trad, fillStart(base, fill));
   s.fill = fill;
   // Swung in eighths, a sixteenth grid is a triplet one: the step before each beat would land on a
   // sextuplet, so whatever the twists, the player or a fill put there moves back a step (or goes).
   if (mods & kSwing8)
      for (Pattern *p : {&s.vars[0], &s.vars[1], &s.vars[2], &s.vars[3], &s.fill})
         if (p->rate == kRate16)
            for (int pad = 0; pad < kPads; ++pad) {
               // The hats' soft fill-in stays on the eighths.
               if (padRole(kit, pad) == kHat)
                  for (int st = 1; st < p->steps; st += 2)
                     if (p->at(pad, st).on() && p->at(pad, st).vel < 60)
                        p->at(pad, st) = Hit{};
               for (int st = 3; st < p->steps; st += 4) {
                  if (!p->at(pad, st).on())
                     continue;
                  if (!p->at(pad, st - 1).on())
                     p->at(pad, st - 1) = p->at(pad, st);
                  p->at(pad, st) = Hit{};
               }
            }

   if (r.percussionist > 0) {
      const int who = std::clamp(r.percussionist - 1, 0, kPercussionists - 1);
      const int pk = percussionistKit(who);
      unsigned pt = percussionistTradition(who);
      if (trad != kTradAny && (pt & trad))
         pt &= trad; // its own tradition's side, over a hand genre
      const Player &pp = kPercussionistList[who];
      Pattern perc = percussionPart(who, meter, rng, pt);
      shapeEnergy(perc, pk, e, -1, rng, pt);
      applyWeird(perc, pk, sparse ? 0.0 : r.weird * 0.5, rng, pt);
      Pattern p0 = perc;
      applyPlayer(p0, pk, pp, e, rng, pt);
      s.perc = p0;
      for (int v = 1; v < kVariations; ++v) {
         std::string tag;
         Pattern m = sparse ? perc : mutate(perc, pk, 0.15 + 0.2 * v + r.weird * 0.3, rng, tag, nullptr, pt);
         keepOut(m, perc, pk);
         applyPlayer(m, pk, pp, e, rng, pt);
         s.percVars[static_cast<size_t>(v - 1)] = m;
      }
      Pattern pf = sparse ? perc : makeFill(perc, pk, percussionistFill(who), pp.fills, e, rng, pt);
      applyPlayer(pf, pk, pp, e, rng, pt, fillStart(perc, pf));
      s.percFill = pf;
   }
   return s;
}

// Remakes every section that isn't locked, each from its own seed (from the song's seed).
inline void generateSong(const Recipe &r, Song &song) {
   for (int i = 0; i < kSections; ++i) {
      Section &s = song.sections[static_cast<size_t>(i)];
      if (s.locked)
         continue;
      s = makeSection(r, i, mix(r.seed, static_cast<uint32_t>(i)));
   }
   if (r.percussionist > 0)
      song.percKit = static_cast<uint8_t>(percussionistKit(r.percussionist - 1));
}

// Rolls one section again with a new seed, keeping its lock.
inline void rollSection(const Recipe &r, Song &song, int section, uint32_t seed) {
   Section &s = song.sections[static_cast<size_t>(section)];
   const uint8_t locked = s.locked;
   s = makeSection(r, section, seed);
   s.locked = locked;
   if (r.percussionist > 0)
      song.percKit = static_cast<uint8_t>(percussionistKit(r.percussionist - 1));
}

// Puts a mutated child in as a section's main groove, and breeds its other variations and its fill
// from it.
inline void adoptChild(const Recipe &r, Section &s, const Pattern &child, uint32_t seed) {
   Rng rng(seed);
   const Genre &g = genreAt(r.genre);
   const Player &pl = kDrummerList[std::clamp(r.player, 0, kDrummers - 1)];
   const double e = s.energy / 100.0;
   s.vars[0] = child;
   // A sparse section stays as it is; the rest are bred from the child and played (timing reset
   // first, so a feel doesn't build up across generations).
   Pattern straight = child;
   for (int pad = 0; pad < kPads; ++pad)
      for (int st = 0; st < kMaxSteps; ++st)
         straight.at(pad, st).nudge = 0;
   for (int v = 1; v < kVariations; ++v) {
      std::string tag;
      Pattern m = e < 0.2 ? child : mutate(straight, g.kit, 0.15 + 0.2 * v, rng, tag, nullptr, g.tradition, true);
      if (e >= 0.2)
         applyPlayer(m, g.kit, pl, e, rng, g.tradition);
      s.vars[static_cast<size_t>(v)] = m;
   }
   Pattern fill = e < 0.2 ? child : makeFill(straight, g.kit, g.fill, pl.fills, e, rng, g.tradition);
   if (e >= 0.2)
      applyPlayer(fill, g.kit, pl, e, rng, g.tradition, fillStart(straight, fill));
   s.fill = fill;
   ++s.generation;
}

} // namespace jdrummer

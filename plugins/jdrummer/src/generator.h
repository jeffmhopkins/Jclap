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
   int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1)); }
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

inline bool onBeat(int step) { return step % 4 == 0; }

// Pads by job, on whichever kit the groove is for.
inline int hatPad(int) { return CH; }
inline int shakerPad(int kit) { return kit == kPercussionKit ? SHAKER : SH; }
inline int percPad(int kit) { return kit == kPercussionKit ? HIGH_BONGO : PC; }
inline int snarePad(int kit) { return kit == kPercussionKit ? CAJON_SLAP : SN; }

inline int backbeatPad(const Pattern &p, int kit) {
   if (p.uses(SN))
      return SN;
   if (p.uses(CL))
      return CL;
   return snarePad(kit);
}

// ---- the style's twists ----

inline void applyMods(Pattern &p, unsigned mods, int kit, Rng &rng) {
   const int n = p.steps;
   if (mods & kBusyHats) {
      const int h = kit == kPercussionKit ? SHAKER : CH;
      for (int s = 0; s < n; ++s)
         if (!p.at(h, s).on() && !p.at(OH, s).on())
            p.at(h, s).vel = kSoft;
   }
   if (mods & kOpenHats && kit == kDrumKit)
      for (int s = 2; s < n; s += 4) {
         p.at(OH, s).vel = kNormal;
         p.at(CH, s) = Hit{};
      }
   if (mods & kShakerLine) {
      const int sh = shakerPad(kit);
      for (int s = 0; s < n; ++s)
         p.at(sh, s).vel = static_cast<uint8_t>(s % 2 ? kSoft : kGhost);
   }
   if (mods & kRollingPerc) {
      const int pc = percPad(kit);
      for (int s = 0; s < n; ++s)
         if (rng.chance(0.55)) {
            Hit &h = p.at(pc, s);
            h.vel = static_cast<uint8_t>(s % 4 == 2 ? kNormal : kSoft);
            h.chance = static_cast<uint8_t>(rng.range(45, 85));
         }
   }
   if (mods & kHalfTime) {
      for (int pad : {SN, CL, RIM})
         if (p.uses(pad)) {
            bool had = false;
            for (int s = 0; s < n; ++s)
               if (p.at(pad, s).vel >= kNormal && s % 8 == 4) {
                  p.at(pad, s) = Hit{};
                  had = true;
               }
            if (had && n > 8)
               p.at(pad, 8).vel = kAccent;
         }
   }
   if (mods & kDoubleKick) {
      for (int s : {n - 1, 7, 10})
         if (s >= 0 && s < n && !p.at(K, s).on() && rng.chance(0.7))
            p.at(K, s).vel = kSoft;
   }
   if (mods & kGhosty) {
      const int sn = backbeatPad(p, kit);
      for (int k = 0; k < 4; ++k) {
         const int s = rng.range(0, n - 1);
         if (s % 2 && !p.at(sn, s).on())
            p.at(sn, s).vel = kGhost;
      }
   }
   if (mods & kToms && kit == kDrumKit) {
      const int lanes[3] = {HT, MT, LT};
      for (int k = 0; k < 3; ++k) {
         const int s = std::min(n - 1, n - 6 + 2 * k + rng.range(0, 1));
         p.at(lanes[k], s).vel = static_cast<uint8_t>(kNormal - 10 * k);
      }
   }
   if (mods & kRimShot && kit == kDrumKit && p.uses(SN)) {
      for (int s = 0; s < n; ++s)
         if (p.at(SN, s).on()) {
            p.at(RIM, s) = p.at(SN, s);
            p.at(SN, s) = Hit{};
         }
   }
   if (mods & kClapStack && kit == kDrumKit) {
      for (int s = 0; s < n; ++s)
         if (p.at(SN, s).vel >= kNormal)
            p.at(CL, s).vel = kNormal;
   }
   if (mods & kBroken) {
      for (int s = 1; s < n; ++s)
         if (p.at(K, s).on() && rng.chance(0.5)) {
            const int to = std::clamp(s + (rng.chance(0.5) ? 1 : -1), 1, n - 1);
            if (!p.at(K, to).on()) {
               p.at(K, to) = p.at(K, s);
               p.at(K, s) = Hit{};
            }
         }
   }
   if (mods & kRatchetHats) {
      const int h = kit == kPercussionKit ? SHAKER : CH;
      for (int k = 0; k < 2; ++k) {
         const int s = rng.range(n / 2, n - 1);
         p.at(h, s).vel = kNormal;
         p.at(h, s).ratchet = static_cast<uint8_t>(rng.range(2, 4));
      }
   }
   if (mods & kSparse) {
      for (int pad = 0; pad < kPads; ++pad)
         for (int s = 0; s < n; ++s) {
            Hit &h = p.at(pad, s);
            if (h.on() && h.vel < kAccent && !onBeat(s) && rng.chance(0.35))
               h = Hit{};
         }
   }
}

// ---- energy ----

// The energy (0..1) a lane comes in at.
inline double threshold(int kit, int pad) {
   if (kit == kDrumKit) {
      if (pad == OH)
         return 0.45;
      if (pad == SH)
         return 0.35;
      if (pad == CR)
         return 0.8;
   }
   static const double byRole[kRoleCount] = {0.12, 0.28, 0.0, 0.45, 0.38, 0.6};
   const double t = byRole[padRole(kit, pad)];
   return kit == kDrumKit ? t : t * 0.6;
}

inline void shapeEnergy(Pattern &p, int kit, double e, int section, Rng &rng) {
   const int n = p.steps;
   // Lanes the energy is too low for drop out, unless it would leave the pattern empty.
   Pattern before = p;
   for (int pad = 0; pad < kPads; ++pad)
      if (p.uses(pad) && e < threshold(kit, pad))
         for (int s = 0; s < kMaxSteps; ++s)
            p.at(pad, s) = Hit{};
   // A break drops the kick.
   if (section == kBreak)
      for (int pad : {K, K2})
         if (kit == kDrumKit)
            for (int s = 0; s < kMaxSteps; ++s)
               p.at(pad, s) = Hit{};
   if (p.empty() && e >= 0.1)
      p = before;
   // Very low energy thins to anchors: only the beats under 0.2, only the half notes of the
   // lowest lane under 0.1, nothing at all at 0.
   if (e < 0.2) {
      int anchor = -1;
      for (int pad = 0; pad < kPads && anchor < 0; ++pad)
         if (p.uses(pad))
            anchor = pad;
      for (int pad = 0; pad < kPads; ++pad)
         for (int s = 0; s < kMaxSteps; ++s) {
            Hit &h = p.at(pad, s);
            const bool keep = e >= 0.1 ? onBeat(s) || h.vel >= kAccent
                                       : e > 0.02 && pad == anchor && s % 8 == 0;
            if (!keep)
               h = Hit{};
         }
      if (p.empty() && e > 0.02 && anchor >= 0)
         p.at(anchor, 0).vel = kSoft;
   }
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         if (e < 0.4 && h.vel < 80 && rng.chance((0.4 - e) * 1.6)) {
            h = Hit{};
            continue;
         }
         h.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(h.vel * (0.82 + 0.3 * e))), 1, 127));
      }
   // High energy: the hats fill in and the kick gets pickups.
   if (e > 0.7) {
      const int h = kit == kPercussionKit ? SHAKER : CH;
      if (p.uses(h))
         for (int s = 0; s < n; ++s)
            if (!p.at(h, s).on() && !p.at(OH, s).on() && rng.chance((e - 0.7) * 1.4))
               p.at(h, s).vel = kGhost + 12;
      if (rng.chance(e - 0.6) && n >= 16 && p.uses(K) && !p.at(K, n - 2).on())
         p.at(K, n - 2).vel = kSoft;
   }
}

// ---- weird ----

inline void applyWeird(Pattern &p, int kit, double w, Rng &rng) {
   if (w <= 0.0)
      return;
   int n = p.steps;
   for (int pad = 0; pad < kPads; ++pad) {
      if (!p.uses(pad))
         continue;
      // A hit pulled a step off the grid.
      if (rng.chance(w * 0.7)) {
         const int s = rng.range(1, n - 1);
         const int to = std::clamp(s + (rng.chance(0.5) ? 1 : -1), 0, n - 1);
         if (p.at(pad, s).on() && !p.at(pad, to).on() && !(pad == K && s == 0)) {
            p.at(pad, to) = p.at(pad, s);
            p.at(pad, s) = Hit{};
         }
      }
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         if (!onBeat(s) && rng.chance(w * 0.25))
            h.chance = static_cast<uint8_t>(rng.range(40, 90));
         if (padRole(kit, pad) == kHat && rng.chance(w * 0.18))
            h.ratchet = static_cast<uint8_t>(rng.range(2, kMaxRatchet));
      }
   }
   // Stray sounds on pads the groove doesn't use.
   const int strays = static_cast<int>(w * 3.0 + rng.uniform());
   for (int k = 0; k < strays; ++k) {
      const int pad = kit == kDrumKit ? (rng.chance(0.5) ? FX : rng.chance(0.5) ? PC : RIM) : rng.range(1, kPads - 1);
      const int s = rng.range(0, n - 1);
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

inline void applyPlayer(Pattern &p, int kit, const Player &pl, double e, Rng &rng) {
   const int n = p.steps;
   // Ghost notes round the backbeat (not in a section kept sparse).
   if (kit != kFxKit && kit != kAnimalKit && e >= 0.2) {
      const int sn = backbeatPad(p, kit);
      if (p.uses(sn)) {
         const int count = static_cast<int>(std::lround(pl.ghosts * 4.0 * (0.5 + e)));
         for (int k = 0; k < count; ++k) {
            const int s = rng.range(0, n - 1);
            if (s % 2 && !p.at(sn, s).on() && !p.at(sn, s ^ 1).on())
               p.at(sn, s).vel = static_cast<uint8_t>(rng.range(28, 46));
         }
      }
   }
   // Busy players add a few hits.
   if (pl.busy > 0.5 && e >= 0.2) {
      const int extra = static_cast<int>((pl.busy - 0.5) * 6.0 * e + rng.uniform());
      for (int k = 0; k < extra; ++k) {
         const int pad = rng.chance(0.6) ? (kit == kPercussionKit ? SHAKER : CH) : K;
         const int s = rng.range(0, n - 1);
         if (p.uses(pad) && !p.at(pad, s).on())
            p.at(pad, s).vel = kSoft;
      }
   }
   // Timing and dynamics.
   for (int pad = 0; pad < kPads; ++pad)
      for (int s = 0; s < n; ++s) {
         Hit &h = p.at(pad, s);
         if (!h.on())
            continue;
         const Role role = padRole(kit, pad);
         int nudge = static_cast<int>(std::lround((rng.uniform() * 2.0 - 1.0) * pl.loose * 10.0));
         if (role == kSnare)
            nudge += pl.backbeatFeel;
         if (role == kHat)
            nudge += pl.hatFeel;
         h.nudge = static_cast<int8_t>(std::clamp(nudge, -kMaxNudge, kMaxNudge));
         const int jitter = static_cast<int>(std::lround((rng.uniform() * 2.0 - 1.0) * pl.loose * 12.0));
         h.vel = static_cast<uint8_t>(std::clamp(h.vel + jitter, 1, 127));
      }
}

// ---- fills ----

inline Pattern makeFill(const Pattern &base, int kit, FillKind kind, double fills, double e, Rng &rng) {
   Pattern f = base;
   const int n = f.steps;
   const int len = std::min(n, fills >= 0.5 || e > 0.75 ? 8 : 4);
   const int from = n - len;
   // Clear the hats, backbeat and toms under the fill; the kick keeps the beat.
   for (int pad = 0; pad < kPads; ++pad)
      if (!isKickPad(pad) || kind == kFillDrop)
         for (int s = from; s < n; ++s)
            f.at(pad, s) = Hit{};
   auto ramp = [&](int s) { return static_cast<uint8_t>(std::clamp(60 + (s - from) * 60 / std::max(1, len - 1), 1, 127)); };
   const bool drums = kit == kDrumKit;
   switch (kind) {
   case kFillToms: {
      const int toms[4] = {SN, HT, MT, LT};
      for (int s = from; s < n; ++s)
         if (s % 2 == 0 || rng.chance(fills))
            f.at(drums ? toms[std::min(3, (s - from) * 4 / len)] : HIGH_CONGA, s).vel = ramp(s);
      break;
   }
   case kFillSnare: {
      const int sn = drums ? SN : snarePad(kit);
      for (int s = from; s < n; ++s)
         if (s >= n - 4 || s % 2 == 0 || rng.chance(fills))
            f.at(sn, s).vel = ramp(s);
      if (fills > 0.6)
         f.at(sn, n - 1).ratchet = 2;
      break;
   }
   case kFillHats: {
      for (int s = from; s < n; ++s) {
         Hit &h = f.at(CH, s);
         h.vel = ramp(s);
         if (s >= n - 4 && rng.chance(0.6))
            h.ratchet = static_cast<uint8_t>(rng.range(2, 4));
      }
      f.at(drums ? backbeatPad(base, kit) : snarePad(kit), n - 4 >= 0 ? n - 4 : 0).vel = kAccent;
      break;
   }
   case kFillDrop: {
      const int cl = base.uses(CL) ? CL : SN;
      for (int s = from; s < n; ++s)
         if (s % 2 == 0 || s >= n - 4)
            f.at(cl, s).vel = ramp(s);
      break;
   }
   case kFillHands: {
      const int hands[4] = {drums ? HT : HIGH_CONGA, drums ? MT : LOW_CONGA, drums ? LT : HIGH_BONGO,
                            drums ? SN : CONGA_SLAP};
      for (int s = from; s < n; ++s)
         f.at(hands[rng.range(0, 3)], s).vel = ramp(s);
      break;
   }
   case kFillFx: {
      const int fx = drums ? FX : 15;
      for (int s = from; s < n; s += 2)
         f.at(fx, s).vel = ramp(s);
      f.at(fx, n - 2).ratchet = 3;
      f.at(drums ? SN : 2, n - 1).vel = kAccent;
      break;
   }
   }
   return f;
}

// ---- mutations ----

// One way a pattern can change, as the MUTATE card tags a child.
struct Mutation {
   const char *tag;
   bool (*apply)(Pattern &, int kit, Rng &);
};

inline bool hasOnBeatHit(const Pattern &p, int pad) { return p.uses(pad); }

inline const std::vector<Mutation> &mutations() {
   static const std::vector<Mutation> m = {
      {"more ghost snares",
       [](Pattern &p, int kit, Rng &r) {
          const int sn = backbeatPad(p, kit);
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
       }},
      {"kick moves off the 1",
       [](Pattern &p, int, Rng &r) {
          std::vector<int> hits;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(K, s).on())
                hits.push_back(s);
          if (hits.size() < 2)
             return false;
          const int s = hits[static_cast<size_t>(r.range(1, static_cast<int>(hits.size()) - 1))];
          const int to = std::clamp(s + (r.chance(0.5) ? 1 : -1), 1, p.steps - 1);
          if (p.at(K, to).on())
             return false;
          p.at(K, to) = p.at(K, s);
          p.at(K, s) = Hit{};
          return true;
       }},
      {"lays back further",
       [](Pattern &p, int kit, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(kit, pad) == kSnare || padRole(kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on()) {
                      Hit &h = p.at(pad, s);
                      h.nudge = static_cast<int8_t>(std::clamp(h.nudge + r.range(5, 10), -kMaxNudge, kMaxNudge));
                      any = true;
                   }
          return any;
       }},
      {"pushes ahead",
       [](Pattern &p, int kit, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on()) {
                      Hit &h = p.at(pad, s);
                      h.nudge = static_cast<int8_t>(std::clamp(h.nudge - r.range(4, 9), -kMaxNudge, kMaxNudge));
                      any = true;
                   }
          return any;
       }},
      {"half-time feel",
       [](Pattern &p, int kit, Rng &) {
          const int sn = backbeatPad(p, kit);
          if (p.steps != 16 || !p.at(sn, 4).on() || !p.at(sn, 12).on())
             return false;
          p.at(sn, 4) = Hit{};
          p.at(sn, 12) = Hit{};
          p.at(sn, 8).vel = kAccent;
          return true;
       }},
      {"toms come in",
       [](Pattern &p, int kit, Rng &r) {
          if (kit != kDrumKit)
             return false;
          const int toms[3] = {HT, MT, LT};
          for (int k = 0; k < 3; ++k) {
             const int s = r.range(p.steps / 2, p.steps - 1);
             p.at(toms[k], s).vel = static_cast<uint8_t>(r.range(70, 105));
          }
          return true;
       }},
      {"hats open up",
       [](Pattern &p, int kit, Rng &r) {
          if (kit != kDrumKit || !p.uses(CH))
             return false;
          int moved = 0;
          for (int s = 2; s < p.steps; s += 4)
             if (p.at(CH, s).on() && r.chance(0.6)) {
                p.at(OH, s) = p.at(CH, s);
                p.at(CH, s) = Hit{};
                ++moved;
             }
          return moved > 0;
       }},
      {"busier kick",
       [](Pattern &p, int, Rng &r) {
          if (!p.uses(K))
             return false;
          int added = 0;
          for (int k = 0; k < 4 && added < 2; ++k) {
             const int s = r.range(1, p.steps - 1);
             if (!p.at(K, s).on() && !onBeat(s)) {
                p.at(K, s).vel = static_cast<uint8_t>(r.range(60, 90));
                ++added;
             }
          }
          return added > 0;
       }},
      {"sparser",
       [](Pattern &p, int, Rng &r) {
          int removed = 0;
          for (int k = 0; k < 24 && removed < 3; ++k) {
             const int pad = r.range(0, kPads - 1), s = r.range(0, p.steps - 1);
             Hit &h = p.at(pad, s);
             if (h.on() && !onBeat(s) && h.vel < kAccent) {
                h = Hit{};
                ++removed;
             }
          }
          return removed > 0;
       }},
      {"rolls on the hats",
       [](Pattern &p, int kit, Rng &r) {
          const int h = kit == kPercussionKit ? SHAKER : CH;
          if (!p.uses(h))
             return false;
          for (int k = 0; k < 2; ++k) {
             const int s = r.range(p.steps / 2, p.steps - 1);
             p.at(h, s).vel = static_cast<uint8_t>(std::max<int>(p.at(h, s).vel, 80));
             p.at(h, s).ratchet = static_cast<uint8_t>(r.range(2, 4));
          }
          return true;
       }},
      {"hats left to chance",
       [](Pattern &p, int kit, Rng &r) {
          bool any = false;
          for (int pad = 0; pad < kPads; ++pad)
             if (padRole(kit, pad) == kHat)
                for (int s = 0; s < p.steps; ++s)
                   if (p.at(pad, s).on() && !onBeat(s) && r.chance(0.4)) {
                      p.at(pad, s).chance = static_cast<uint8_t>(r.range(45, 85));
                      any = true;
                   }
          return any;
       }},
      {"rim clicks join",
       [](Pattern &p, int kit, Rng &r) {
          if (kit != kDrumKit || p.uses(RIM))
             return false;
          for (int k = 0; k < 3; ++k)
             p.at(RIM, r.range(0, p.steps - 1)).vel = static_cast<uint8_t>(r.range(60, 90));
          return true;
       }},
      {"shaker joins",
       [](Pattern &p, int kit, Rng &) {
          const int sh = shakerPad(kit);
          if (p.uses(sh))
             return false;
          for (int s = 0; s < p.steps; ++s)
             p.at(sh, s).vel = static_cast<uint8_t>(s % 2 ? kSoft : kGhost);
          return true;
       }},
      {"double-time hats",
       [](Pattern &p, int kit, Rng &) {
          const int h = kit == kPercussionKit ? SHAKER : CH;
          if (!p.uses(h))
             return false;
          int added = 0;
          for (int s = 0; s < p.steps; ++s)
             if (!p.at(h, s).on() && !p.at(OH, s).on()) {
                p.at(h, s).vel = kGhost + 10;
                ++added;
             }
          return added > 0;
       }},
      {"accents move",
       [](Pattern &p, int kit, Rng &r) {
          const int h = kit == kPercussionKit ? SHAKER : CH;
          if (!p.uses(h))
             return false;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(h, s).on())
                p.at(h, s).vel = static_cast<uint8_t>(r.chance(0.3) ? kAccent - 10 : kSoft - 10);
          return true;
       }},
      {"clap doubles the snare",
       [](Pattern &p, int kit, Rng &) {
          if (kit != kDrumKit || !p.uses(SN) || p.uses(CL))
             return false;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(SN, s).vel >= kNormal)
                p.at(CL, s).vel = kNormal;
          return true;
       }},
      {"perc answers",
       [](Pattern &p, int kit, Rng &r) {
          const int pc = percPad(kit);
          int added = 0;
          for (int k = 0; k < 4; ++k) {
             const int s = r.range(0, p.steps - 1);
             if (!onBeat(s) && !p.at(pc, s).on()) {
                p.at(pc, s).vel = static_cast<uint8_t>(r.range(60, 100));
                ++added;
             }
          }
          return added > 0;
       }},
      {"flams on the backbeat",
       [](Pattern &p, int kit, Rng &) {
          const int sn = backbeatPad(p, kit);
          bool any = false;
          for (int s = 0; s < p.steps; ++s)
             if (p.at(sn, s).vel >= kNormal && p.at(sn, s).ratchet == 1) {
                p.at(sn, s).ratchet = 2;
                any = true;
             }
          return any;
       }},
   };
   return m;
}

// A child of `parent`: one twist (two or three as `amount` grows), and the tag that says how it
// differs. Ops already used for this litter (`used`) are avoided, so five children differ.
inline Pattern mutate(const Pattern &parent, int kit, double amount, Rng &rng, std::string &tag,
                      std::vector<int> *used = nullptr) {
   const auto &ops = mutations();
   Pattern child = parent;
   tag.clear();
   const int wanted = 1 + (rng.chance(amount) ? 1 : 0) + (amount > 0.7 && rng.chance(amount - 0.5) ? 1 : 0);
   int applied = 0;
   for (int tries = 0; tries < 40 && applied < wanted; ++tries) {
      const int i = rng.range(0, static_cast<int>(ops.size()) - 1);
      if (used && applied == 0 && std::find(used->begin(), used->end(), i) != used->end())
         continue;
      if (!ops[static_cast<size_t>(i)].apply(child, kit, rng))
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

// ---- sections and songs ----

inline const Style &styleOf(const Recipe &r) {
   const Genre &g = genreAt(r.genre);
   return g.styles[static_cast<size_t>(std::clamp(r.style, 0, static_cast<int>(g.styles.size()) - 1))];
}

// The energy a section of a recipe is made with, 0..1.
// ENERGY scales the arc (at 50 each section plays as set), so a section set to 0 stays silent.
inline double sectionEnergy(const Recipe &r, int section) {
   return std::clamp(r.sections[static_cast<size_t>(section)] * (0.5 + r.energy), 0.0, 1.0);
}

// The base groove of a section: the style's, sometimes its genre's other grooves for variety.
inline Pattern baseGroove(const Recipe &r, int section, Rng &rng) {
   const Genre &g = genreAt(r.genre);
   const Style &st = styleOf(r);
   int gi = st.groove;
   // The bridge, and now and then a weird verse, borrow another groove of the genre.
   if (g.grooves.size() > 1 && (section == kBridge || rng.chance(r.weird * 0.3)))
      gi = (gi + 1 + rng.range(0, static_cast<int>(g.grooves.size()) - 2)) % static_cast<int>(g.grooves.size());
   Pattern p = fromGroove(g.grooves[static_cast<size_t>(std::clamp(gi, 0, static_cast<int>(g.grooves.size()) - 1))]);
   if (r.genre2 >= 0 && r.genre2 != r.genre && r.blend > 0.0) {
      const Genre &g2 = genreAt(r.genre2);
      const Pattern q = fromGroove(g2.grooves[static_cast<size_t>(rng.range(0, static_cast<int>(g2.grooves.size()) - 1))]);
      if (g2.kit == g.kit)
         p = blendPatterns(p, q, r.blend, rng);
   }
   applyMods(p, st.mods, g.kit, rng);
   return p;
}

inline Section makeSection(const Recipe &r, int section, uint32_t seed) {
   Rng rng(seed);
   const Genre &g = genreAt(r.genre);
   const int kit = g.kit;
   const Player &pl = kDrummerList[std::clamp(r.player, 0, kDrummers - 1)];
   const double e = sectionEnergy(r, section);
   Section s;
   s.seed = seed;
   s.bars = static_cast<uint8_t>(kSectionBars[section]);
   s.energy = static_cast<uint8_t>(std::lround(e * 100.0));
   s.crash = kit == kDrumKit && (section == kChorus || e >= 0.75) && e >= 0.2 ? 1 : 0;
   // A sparse section stays sparse: no stray sounds, no added hits, no fill busier than the groove.
   const bool sparse = e < 0.2;

   Pattern base = baseGroove(r, section, rng);
   shapeEnergy(base, kit, e, section, rng);
   applyWeird(base, kit, sparse ? 0.0 : r.weird, rng);
   Pattern played = base;
   applyPlayer(played, kit, pl, e, rng);
   s.vars[0] = played;
   // Variations: small mutations of the groove, the last a little further.
   for (int v = 1; v < kVariations; ++v) {
      std::string tag;
      Pattern m = sparse ? base : mutate(base, kit, 0.15 + 0.2 * v + r.weird * 0.3, rng, tag);
      applyPlayer(m, kit, pl, e, rng);
      s.vars[static_cast<size_t>(v)] = m;
   }
   Pattern fill = sparse ? base : makeFill(base, kit, g.fill, pl.fills, e, rng);
   applyPlayer(fill, kit, pl, e, rng);
   s.fill = fill;

   if (r.percussionist > 0) {
      const int who = std::clamp(r.percussionist - 1, 0, kPercussionists - 1);
      const auto &grooves = percussionGrooves(who);
      Pattern perc = fromGroove(grooves[static_cast<size_t>(rng.range(0, static_cast<int>(grooves.size()) - 1))]);
      shapeEnergy(perc, kPercussionKit, sparse ? e : std::max(0.3, e), -1, rng);
      applyWeird(perc, kPercussionKit, r.weird * 0.5, rng);
      applyPlayer(perc, kPercussionKit, kPercussionistList[who], e, rng);
      s.perc = perc;
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
}

// Rolls one section again with a new seed, keeping its lock.
inline void rollSection(const Recipe &r, Song &song, int section, uint32_t seed) {
   Section &s = song.sections[static_cast<size_t>(section)];
   const uint8_t locked = s.locked;
   s = makeSection(r, section, seed);
   s.locked = locked;
}

// Puts a mutated child in as a section's main groove, and breeds its other variations and its fill
// from it.
inline void adoptChild(const Recipe &r, Section &s, const Pattern &child, uint32_t seed) {
   Rng rng(seed);
   const Genre &g = genreAt(r.genre);
   const Player &pl = kDrummerList[std::clamp(r.player, 0, kDrummers - 1)];
   s.vars[0] = child;
   for (int v = 1; v < kVariations; ++v) {
      std::string tag;
      s.vars[static_cast<size_t>(v)] = mutate(child, g.kit, 0.15 + 0.2 * v, rng, tag);
   }
   s.fill = makeFill(child, g.kit, g.fill, pl.fills, s.energy / 100.0, rng);
   ++s.generation;
}

} // namespace jdrummer

#pragma once

// The dice, as JSynth's and JVerb's: RANDOMIZE writes a new patch from scratch and MUTATE nudges the
// one there is. Neither is noise. Randomize builds a patch the way a player would pick one up: an
// instrument first, then its exciter, bore, tongue and vibrato within the ranges that sound for
// that instrument (the factory presets' and the knob audit's, away from the corners where a note
// goes silent or clips). Complexity says how much it reaches for: at 0 a plain, real instrument; up
// from there a growl or a flutter, a mute, the HYBRID layer (as a pad, at times), unison, LFOs and
// routes, the delay, wider bore settings, and the Alien.
//
// Mutate moves every value by its Amount (times and frequencies on a log scale, so evenly by ear),
// keeps what is off switched off, changes a choice only to a near one (a mute to the next mute, a
// tongue to its neighbour; the instrument only when the Amount is high), keeps every value inside
// the range that sounds for the instrument (or no further out than it was), and only adds a route
// when the Amount is high.
//
// Neither touches Level, Master or the Limiter, the MIDI and MPE setup (breath source, Breath In,
// press curve, slide, bend range), Transpose or Tune, or the AIR view's look. Each param they do
// touch belongs to a section that can be locked, which both leave as it is. The random numbers are
// this file's own, so a seed gives the same patch on every platform.

#include "params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace jbreath {

// The sections a lock keeps, named for the panel's cards and tabs.
enum DiceSection {
   kDiceInstrument, // the instrument and its exciter: breath, embouchure, stiffness, bite, aperture, noise
   kDiceBore,       // the bore's knobs, holes, tube and mute
   kDicePlayer,     // the tongue: style, kick, attack, release, humanize, growl and flutter
   kDiceVibrato,    // the vibrato
   kDiceHybrid,     // the HYBRID layer
   kDiceMod,        // the LFOs, the Swell, the buses and the routes
   kDiceFx,         // the reverb and its tone, the air and the delay
   kDiceVoice,      // voice mode, glide, unison, spread and pan
   kDiceSectionCount
};
inline const char *const kDiceSectionNames[kDiceSectionCount] = {"INSTRUMENT", "BORE", "PLAYER", "VIBRATO",
                                                                 "HYBRID",     "MOD",  "FX",     "VOICE"};

// Which section a param is in, or -1 for one the dice never touch.
inline int diceSection(clap_id id) {
   if (id >= kBusBase && id < kRouteEnd)
      return kDiceMod;
   switch (id) {
   case kModel:
   case kBreath:
   case kEmbouchure:
   case kNoise:
   case kEdge:
   case kStiffness:
   case kAperture:
   case kAlienVoice:
   case kAlienMorph:
   case kAlienDrift:
   case kAlienShift:
   case kAlienSharp: return kDiceInstrument;
   case kLength:
   case kLoss:
   case kHoleCutoff:
   case kCone:
   case kRadiate:
   case kBrass:
   case kBarrel:
   case kWall:
   case kDetune:
   case kHoles:
   case kTube:
   case kMute:
   case kMuteAmount: return kDiceBore;
   case kTongueType:
   case kKick:
   case kAttack:
   case kRelease:
   case kHumanize:
   case kGrowl:
   case kFlutter:
   case kFlutterRate: return kDicePlayer;
   case kVibRate:
   case kVibDepth:
   case kVibPitch:
   case kVibDelay:
   case kVibType: return kDiceVibrato;
   case kHybridModel:
   case kHybridMix:
   case kHybridOctave:
   case kHybridDetune:
   case kHybridBreath:
   case kHybridCutoff:
   case kHybridResonance:
   case kHybridFilterBreath:
   case kHybridAttack:
   case kHybridDecay:
   case kHybridSustain:
   case kHybridRelease:
   case kHybridFollow:
   case kHybridFilterEnv: return kDiceHybrid;
   case kLfo1Rate:
   case kLfo1Shape:
   case kLfo1Sync:
   case kLfo2Rate:
   case kLfo2Shape:
   case kLfo2Sync:
   case kSwellShape:
   case kSwellTime:
   case kSwellLoop: return kDiceMod;
   case kReverbOn:
   case kRoom:
   case kRoomSize:
   case kRoomDecay:
   case kRoomDamp:
   case kRoomPreDelay:
   case kBright:
   case kAir:
   case kDelayOn:
   case kDelayMix:
   case kDelayTime:
   case kDelaySync:
   case kDelayFeedback:
   case kDelayTone:
   case kDelaySpread: return kDiceFx;
   case kVoiceMode:
   case kGlide:
   case kUnison:
   case kUnisonDetune:
   case kSpread:
   case kPan: return kDiceVoice;
   // Transpose, Tune, Level, Master, the Limiter, the breath source, Breath In, the press curve,
   // slide, the bend range and the look.
   default: return -1;
   }
}

// SplitMix64: small, fast and the same everywhere.
class DiceRng {
public:
   explicit DiceRng(uint64_t seed) : state_(seed) {}
   uint64_t next() {
      uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
   }
   // 0..1.
   double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
   double range(double lo, double hi) { return lo + (hi - lo) * unit(); }
   // Even by ear: lo to hi on a log scale (both above 0).
   double logRange(double lo, double hi) { return lo * std::pow(hi / lo, unit()); }
   bool chance(double p) { return unit() < p; }
   int below(int n) { return static_cast<int>(unit() * n) % std::max(n, 1); }
   // A standard normal, by Box-Muller.
   double normal() {
      const double u = std::max(unit(), 1e-12), v = unit();
      return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
   }
   // One of `items` by weight.
   template <typename T>
   T pick(std::initializer_list<std::pair<T, double>> items) {
      double total = 0.0;
      for (const auto &i : items)
         total += i.second;
      double r = unit() * total;
      for (const auto &i : items) {
         if (r < i.second)
            return i.first;
         r -= i.second;
      }
      return items.begin()->first;
   }
   template <typename T>
   T any(std::initializer_list<T> items) {
      return *(items.begin() + below(static_cast<int>(items.size())));
   }

private:
   uint64_t state_;
};

struct DiceSettings {
   double complexity = 0.3; // Randomize: 0 a plain instrument, 1 everything
   double amount = 0.2;     // Mutate: 0 nothing, 1 far
   unsigned locks = 0;      // a bit per DiceSection
   bool locked(int section) const { return (locks >> section) & 1u; }
};

namespace detail {

enum Model { F = 0, C = 1, S = 2, O = 3, T = 4, A = 5 };

// The values, clamped and stepped as the host would.
class Values {
public:
   explicit Values(std::vector<double> &v) : v_(v) {}
   double get(clap_id id) const { return v_[id]; }
   int choice(clap_id id) const { return static_cast<int>(std::lround(v_[id])); }
   void set(clap_id id, double value) {
      const jclap::core::ParamDef &d = kParams[id];
      value = std::clamp(value, d.min, d.max);
      if (d.flags & CLAP_PARAM_IS_STEPPED)
         value = std::round(value);
      v_[id] = value;
   }

private:
   std::vector<double> &v_;
};

// Params that sweep a range by ear are moved on a log scale.
inline bool logScaled(clap_id id) {
   switch (id) {
   case kAttack:
   case kRelease:
   case kHoleCutoff:
   case kHybridCutoff:
   case kRoomDecay:
   case kLfo1Rate:
   case kLfo2Rate:
   case kSwellTime: return true;
   default: return false;
   }
}

struct Span {
   double lo, hi;
};

// Where each knob sounds for an instrument: the factory presets' spread, a little past it, and short
// of the corners the knob audit found silent, clicking or clipping (the trumpet's embouchure,
// stiffness, loss, cone and hole cutoff at their ends, flutter near full on the reeds, a loud square
// under the sax). Randomize stays inside; Mutate never takes a value further out than it was.
inline Span sane(clap_id id, int model) {
   switch (id) {
   case kBreath: return model == A ? Span{0.95, 1.32} : Span{0.8, 1.3};
   case kEmbouchure: return model == T ? Span{0.3, 0.7} : Span{0.25, 0.75};
   case kNoise: return model == F ? Span{0.4, 2.0} : Span{0.3, 1.9};
   case kEdge: return Span{0.3, 2.0};
   case kStiffness: return model == T ? Span{0.3, 0.72} : Span{0.25, 0.72};
   case kAperture: return Span{0.3, 0.7};
   case kKick: return Span{0.1, 0.6};
   case kAttack: return Span{5.0, 400.0};
   case kRelease: return Span{50.0, 2000.0};
   case kHumanize: return Span{0.0, 0.6};
   case kGrowl: return Span{0.0, 0.6};
   case kFlutter: return Span{0.0, 0.6};
   case kFlutterRate: return Span{14.0, 34.0};
   case kVibRate: return model == A ? Span{2.4, 6.5} : Span{2.4, 7.5};
   case kVibDepth: return model == A ? Span{0.0, 0.05} : Span{0.0, 0.1};
   case kVibPitch: return model == A ? Span{0.0, 14.0} : Span{0.0, 25.0};
   case kVibDelay: return Span{0.0, 900.0};
   case kLength: return Span{0.35, 0.9};
   case kLoss: return model == T ? Span{0.015, 0.12} : Span{0.015, 0.15};
   case kHoleCutoff:
      return model == T ? Span{1000.0, 2600.0} : model == A ? Span{1150.0, 1500.0} : Span{1000.0, 3000.0};
   case kCone:
      return model == T   ? Span{0.2, 0.9}
             : model == S ? Span{0.2, 0.6}
             : model == O ? Span{0.4, 0.8}
             : model == A ? Span{0.25, 0.75}
                          : Span{0.0, 1.0};
   case kRadiate: return Span{0.3, 0.95};
   case kBrass: return Span{0.2, 0.9};
   case kBarrel: return Span{0.2, 0.7};
   case kWall: return Span{0.1, 0.9};
   case kDetune: return Span{0.0, 12.0};
   case kMuteAmount: return Span{0.4, 1.0};
   case kHybridMix: return Span{0.0, 0.5};
   case kHybridDetune: return Span{-12.0, 12.0};
   case kHybridBreath: return Span{0.7, 1.3};
   case kHybridCutoff: return Span{400.0, 4000.0};
   case kHybridResonance: return Span{0.0, 0.5};
   case kHybridFilterBreath: return Span{0.2, 0.9};
   case kHybridAttack: return Span{0.1, 0.88};
   case kHybridDecay: return Span{0.4, 0.8};
   case kHybridSustain: return Span{0.5, 1.0};
   case kHybridRelease: return Span{0.4, 0.9};
   case kHybridFollow: return Span{0.0, 1.0};
   case kHybridFilterEnv: return Span{-0.3, 0.7};
   case kLfo1Rate:
   case kLfo2Rate: return Span{0.05, 8.0};
   case kSwellTime: return Span{0.3, 12.0};
   case kRoom: return Span{0.0, 0.6};
   case kRoomSize: return Span{0.2, 0.95};
   case kRoomDecay: return Span{0.6, 7.0};
   case kRoomDamp: return Span{0.2, 0.8};
   case kRoomPreDelay: return Span{0.0, 60.0};
   case kBright: return Span{-5.0, 3.0};
   case kAir: return Span{0.0, 0.4};
   case kDelayMix: return Span{0.1, 0.45};
   case kDelayTime: return Span{0.35, 0.85};
   case kDelayFeedback: return Span{0.05, 0.7};
   case kDelayTone: return Span{0.3, 0.9};
   case kDelaySpread: return Span{0.0, 1.0};
   case kGlide: return Span{0.0, 200.0};
   case kUnisonDetune: return Span{4.0, 20.0};
   case kSpread: return Span{-0.2, 0.8};
   case kPan: return Span{-0.3, 0.3};
   case kAlienMorph: return Span{0.0, 0.8};
   case kAlienDrift: return Span{0.0, 0.9};
   case kAlienShift: return Span{-0.6, 0.6};
   case kAlienSharp: return Span{0.2, 0.95};
   default: {
      if (id >= kBusBase && id < kRouteBase) {
         if ((id - kBusBase) % kBusStride == kBusAmount)
            return Span{-0.6, 0.6};
         if ((id - kBusBase) % kBusStride == kBusLag)
            return Span{0.0, 300.0};
      }
      const jclap::core::ParamDef &d = kParams[id];
      return Span{d.min, d.max};
   }
   }
}

// What a route can move without bending the pitch, the level or the patch's own setup, and how far
// (in the destination's 0..1 range) it should at most.
struct RouteTarget {
   clap_id id;
   double most;
};
inline std::vector<RouteTarget> routeTargets(const Values &v) {
   std::vector<RouteTarget> t = {{kBreath, 0.15}, {kEmbouchure, 0.2}, {kNoise, 0.2}, {kEdge, 0.2}, {kVibDepth, 0.3},
                                 {kVibRate, 0.15}, {kAperture, 0.15}, {kAir, 0.25}, {kPan, 0.3}, {kHoleCutoff, 0.15}};
   if (v.get(kHybridMix) > 0.0) {
      t.push_back({kHybridCutoff, 0.25});
      t.push_back({kHybridMix, 0.2});
   }
   if (v.get(kGrowl) > 0.0)
      t.push_back({kGrowl, 0.3});
   if (v.get(kFlutter) > 0.0)
      t.push_back({kFlutter, 0.3});
   if (v.choice(kMute) > 0)
      t.push_back({kMuteAmount, 0.5});
   if (v.choice(kModel) == T)
      t.push_back({kBrass, 0.3});
   if (v.choice(kModel) == A) {
      t.push_back({kAlienVoice, 0.5});
      t.push_back({kAlienShift, 0.3});
   }
   return t;
}

// Sources a route can come from: the player's hands, the breath's own, and the generators.
inline int routeSource(DiceRng &rng) {
   return rng.pick<int>({{kSrcPress, 1.5}, {kSrcSlide, 1.2}, {kSrcStrike, 1.0}, {kSrcLoop, 1.0}, {kSrcKnot, 0.6},
                         {kSrcRegister, 0.8}, {kSrcDraft, 1.0}, {kSrcSwell, 1.0}, {kSrcLfo1, 1.4}, {kSrcLfo2, 1.0},
                         {kSrcRandom, 0.5}, {kSrcKey, 0.6}});
}

// Puts a new route in the first free slot. False if every slot is taken.
inline bool addRoute(Values &v, DiceRng &rng, double scale) {
   for (int r = 0; r < kRouteCount; ++r) {
      if (v.choice(routeParam(r, kRouteSource)) != kSrcOff)
         continue;
      const std::vector<RouteTarget> targets = routeTargets(v);
      const RouteTarget &t = targets[rng.below(static_cast<int>(targets.size()))];
      v.set(routeParam(r, kRouteSource), routeSource(rng));
      v.set(routeParam(r, kRouteDestination), destinationOf(t.id));
      const double sign = rng.chance(0.75) ? 1.0 : -1.0;
      v.set(routeParam(r, kRouteAmount), sign * t.most * rng.range(0.35, 1.0) * scale);
      return true;
   }
   return false;
}

// How far a route's amount may go: the most its destination's entry allows (the routes the dice
// didn't make keep where they were).
inline double routeMost(int destination) {
   if (destination < 0 || destination >= kModDestCount)
      return 0.0;
   const clap_id id = kModDestinations[destination];
   for (const RouteTarget &t : std::initializer_list<RouteTarget>{
           {kBreath, 0.15}, {kEmbouchure, 0.2}, {kNoise, 0.2}, {kEdge, 0.2}, {kVibDepth, 0.3}, {kVibRate, 0.15},
           {kAperture, 0.15}, {kAir, 0.25}, {kPan, 0.3}, {kHoleCutoff, 0.15}, {kHybridCutoff, 0.25},
           {kHybridMix, 0.2}, {kGrowl, 0.3}, {kFlutter, 0.3}, {kMuteAmount, 0.5}, {kBrass, 0.3}, {kAlienVoice, 0.5},
           {kAlienShift, 0.3}})
      if (t.id == id)
         return t.most;
   return 0.0;
}

inline void set(Values &v, clap_id id, double value, int model) {
   const Span s = sane(id, model);
   v.set(id, std::clamp(value, s.lo, s.hi));
}

inline void randomInstrument(Values &v, DiceRng &rng, int m, double c) {
   // The breath a little under or over the model's own, harder only with complexity.
   set(v, kBreath, m == A ? rng.range(1.0, 1.15 + 0.15 * c) : rng.range(0.85, 1.12 + 0.13 * c), m);
   set(v, kEmbouchure, rng.range(0.38, 0.62) + rng.range(-0.08, 0.08) * c, m);
   const double noise[6][2] = {{0.6, 1.8}, {0.5, 1.6}, {0.8, 1.8}, {0.4, 1.4}, {0.6, 1.4}, {0.4, 1.8}};
   set(v, kNoise, rng.range(noise[m][0], noise[m][1]), m);
   set(v, kEdge, rng.range(0.55, 1.5 + 0.5 * c), m);
   set(v, kStiffness, rng.range(0.38, 0.62) + rng.range(-0.12, 0.12) * c, m);
   set(v, kAperture, rng.range(0.38, 0.62), m);
   // The Alien's throat: anywhere on its ring, travelling and wandering further with complexity.
   if (m == A) {
      set(v, kAlienVoice, rng.unit(), m);
      set(v, kAlienMorph, rng.range(0.0, 0.3 + 0.5 * c), m);
      set(v, kAlienDrift, rng.range(0.1, 0.3 + 0.6 * c), m);
      set(v, kAlienShift, rng.range(-0.2, 0.2) * (1.0 + 2.0 * c), m);
      set(v, kAlienSharp, rng.range(0.35, 0.6 + 0.35 * c), m);
   }
}

inline void randomBore(Values &v, DiceRng &rng, int m, double c) {
   // A real instrument's bore is near its model's own; complexity takes it further.
   const double wide = 0.4 + 0.6 * c;
   set(v, kLength, 0.65 + rng.range(-0.15, 0.15) * wide, m);
   set(v, kLoss, rng.logRange(0.025, 0.06 + 0.08 * c), m);
   if (m == O)
      set(v, kHoleCutoff, rng.logRange(1300.0, 1700.0 + 600.0 * c), m);
   else if (m == A)
      set(v, kHoleCutoff, rng.logRange(1200.0, 1450.0), m);
   else
      set(v, kHoleCutoff, rng.logRange(1050.0 - 150.0 * c, 1700.0 + 1000.0 * c), m);
   switch (m) {
   case S: set(v, kCone, rng.range(0.28, 0.45), m); break;
   case O: set(v, kCone, rng.range(0.5, 0.68), m); break;
   case T:
      set(v, kCone, rng.chance(0.3) ? rng.range(0.7, 0.9) : rng.range(0.35, 0.6), m);
      set(v, kBrass, rng.range(0.35, 0.65 + 0.2 * c), m);
      break;
   case A: set(v, kCone, rng.range(0.3, 0.7), m); break;
   default: break;
   }
   set(v, kRadiate, rng.range(0.5, 0.8) + rng.range(-0.15, 0.15) * c, m);
   set(v, kBarrel, rng.range(0.32, 0.5) + rng.range(-0.1, 0.15) * c, m);
   set(v, kWall, rng.range(0.35, 0.65) + rng.range(-0.25, 0.25) * c, m);
   // The tone holes: Lattice 7 most, open more for the bright ones; never closed, which silences
   // the trumpet's and the oboe's high notes and smothers the rest under a dark cutoff, nor Lattice 3
   // on the trumpet. The Alien's coils keep theirs, and their tube: both pull its ghost bore off
   // the note.
   if (m == A) {
      if (rng.chance(0.3 * c))
         set(v, kDetune, rng.range(2.0, 6.0), m);
      return;
   }
   v.set(kHoles, rng.pick<int>({{1, m == T ? 0.0 : 0.3 + 0.4 * c}, {2, 1.0},
                                {3, 3.0}, {4, 1.0}, {5, 0.6 + 0.4 * c}}));
   // The tube each family is made of, mostly; anything with complexity.
   int tube = 0;
   switch (m) {
   case F: tube = rng.pick<int>({{5, 3.0}, {3, 1.0}, {1, 1.0}, {2, 0.5}, {7, 0.5}}); break;
   case C: tube = rng.pick<int>({{0, 3.0}, {1, 1.0}, {6, 1.0}, {2, 0.5}}); break;
   case S: tube = rng.pick<int>({{4, 3.0}, {5, 1.0}, {0, 1.0}}); break;
   case O: tube = rng.pick<int>({{0, 3.0}, {1, 1.5}, {2, 1.0}}); break;
   case T: tube = rng.pick<int>({{4, 3.0}, {5, 1.5}}); break;
   default: tube = rng.below(8); break;
   }
   if (rng.chance(0.3 * c))
      tube = rng.below(8);
   v.set(kTube, tube);
   if (rng.chance(0.25 * c))
      set(v, kDetune, rng.range(2.0, 6.0 + 6.0 * c), m);
   // A mute: often on the trumpet, now and then over anything else with complexity.
   if (m == T ? rng.chance(0.3 + 0.2 * c) : rng.chance(0.25 * c)) {
      v.set(kMute, m == T ? rng.pick<int>({{1, 1.0}, {2, 1.0}, {3, 1.0}, {4, 0.6}, {5, 0.8}})
                          : rng.pick<int>({{5, 2.0}, {2, 1.0}, {1, 0.5}}));
      set(v, kMuteAmount, rng.range(0.6, 1.0), m);
   }
}

inline void randomPlayer(Values &v, DiceRng &rng, int m, double c) {
   const bool breathy = m == F || m == S || m == A;
   int tongue = rng.pick<int>({{kTongueHard, 3.0}, {kTongueSoft, 2.0}, {kTongueDouble, 0.6 + 0.6 * c},
                               {kTongueBreath, breathy ? 1.0 : 0.3}});
   // A flutter tongue, with complexity, on the instruments it doesn't choke or pull off the note.
   const bool flutters = m == F || m == C || m == S;
   if (flutters && rng.chance(0.15 * c))
      tongue = kTongueFlutter;
   v.set(kTongueType, tongue);
   set(v, kKick, rng.range(0.15, 0.45 + 0.1 * c), m);
   // A long, slow note now and then; mostly a player's.
   const bool slow = rng.chance(0.12 + 0.2 * c);
   set(v, kAttack, slow ? rng.logRange(60.0, 200.0 + 200.0 * c) : rng.logRange(8.0, 50.0), m);
   set(v, kRelease, slow ? rng.logRange(200.0, 600.0 + 1000.0 * c) : rng.logRange(70.0, 260.0), m);
   if (rng.chance(0.3 + 0.3 * c))
      set(v, kHumanize, rng.range(0.05, 0.25 + 0.3 * c), m);
   // A growl or a flutter, not both: the throat or the tongue.
   if (tongue == kTongueFlutter || (flutters && rng.chance(0.2 * c))) {
      set(v, kFlutter, rng.range(0.2, 0.5), m);
      set(v, kFlutterRate, rng.range(16.0, 30.0), m);
   } else if (rng.chance(0.06 + 0.35 * c)) {
      set(v, kGrowl, rng.range(0.15, 0.3 + 0.25 * c), m);
   }
}

inline void randomVibrato(Values &v, DiceRng &rng, int m, double c) {
   // Each family's: the flute's from the breath, the clarinet's hardly any, the sax's in the jaw,
   // the oboe's quick and singing, the trumpet's in the lips.
   switch (m) {
   case F:
      v.set(kVibType, rng.chance(0.8) ? kVibBreath : kVibBoth);
      set(v, kVibDepth, rng.range(0.04, 0.1), m);
      set(v, kVibPitch, rng.range(0.0, 12.0), m);
      set(v, kVibRate, rng.range(4.4, 5.8), m);
      break;
   case C:
      v.set(kVibType, kVibBreath);
      set(v, kVibDepth, rng.chance(0.6) ? 0.0 : rng.range(0.01, 0.03), m);
      set(v, kVibPitch, rng.chance(0.75) ? 0.0 : rng.range(4.0, 18.0), m);
      set(v, kVibRate, rng.range(4.8, 5.8), m);
      break;
   case S:
      v.set(kVibType, rng.chance(0.5) ? kVibJaw : kVibBreath);
      set(v, kVibDepth, rng.range(0.015, 0.04), m);
      set(v, kVibPitch, rng.range(6.0, 14.0), m);
      set(v, kVibRate, rng.range(4.8, 5.8), m);
      break;
   case O:
      v.set(kVibType, kVibBreath);
      set(v, kVibDepth, rng.range(0.03, 0.05), m);
      set(v, kVibPitch, rng.range(5.0, 10.0), m);
      set(v, kVibRate, rng.range(5.2, 6.4), m);
      break;
   case T:
      v.set(kVibType, kVibLip);
      set(v, kVibDepth, 0.0, m);
      set(v, kVibPitch, rng.range(0.0, 8.0), m);
      set(v, kVibRate, rng.range(4.8, 6.0), m);
      break;
   default:
      // The Alien's from the breath or the lips: in the jaw, it would tune its ghost bore too.
      v.set(kVibType, rng.chance(0.7) ? kVibBreath : kVibLip);
      set(v, kVibDepth, rng.range(0.0, 0.05), m);
      set(v, kVibPitch, rng.range(0.0, 6.0 + 8.0 * c), m);
      set(v, kVibRate, rng.range(2.4, 5.0 + 1.5 * c), m);
      break;
   }
   set(v, kVibDelay, rng.range(150.0, 600.0), m);
}

inline void randomHybrid(Values &v, DiceRng &rng, int m, double c) {
   if (!rng.chance(0.55 * c))
      return;
   const int wave = rng.pick<int>({{0, 0.6}, {1, 1.2}, {2, 1.5}, {3, 0.8}, {4, 0.8}});
   v.set(kHybridModel, wave);
   // The square and the pulse sit loud under the reeds: a little less of them.
   const double most = wave >= 3 ? 0.35 : 0.45;
   set(v, kHybridMix, rng.range(0.2, most), m);
   v.set(kHybridOctave, rng.pick<int>({{0, 3.0}, {-1, 1.5}, {1, 0.4 * c}}));
   set(v, kHybridDetune, rng.chance(0.4) ? rng.range(-8.0, 8.0) : 0.0, m);
   set(v, kHybridBreath, rng.range(0.85, 1.15), m);
   set(v, kHybridCutoff, rng.logRange(600.0, 3000.0), m);
   set(v, kHybridResonance, rng.range(0.05, 0.4), m);
   set(v, kHybridFilterBreath, rng.range(0.3, 0.8), m);
   if (rng.chance(0.5)) {
      // A pad under the instrument: it swells in behind the note and rings on after it.
      set(v, kHybridAttack, rng.range(0.65, 0.85), m);
      set(v, kHybridRelease, rng.range(0.75, 0.9), m);
      set(v, kHybridFollow, rng.range(0.0, 0.3), m);
      set(v, kHybridFilterEnv, rng.range(0.1, 0.6), m);
   } else {
      set(v, kHybridAttack, rng.range(0.1, 0.35), m);
      set(v, kHybridSustain, rng.range(0.7, 1.0), m);
      set(v, kHybridFollow, rng.range(0.7, 1.0), m);
   }
}

inline void randomMod(Values &v, DiceRng &rng, double c) {
   v.set(kLfo1Rate, rng.logRange(0.3, 6.0));
   v.set(kLfo1Shape, rng.pick<int>({{kLfoSine, 2.0}, {kLfoTriangle, 1.5}, {kLfoSmooth, 1.0}, {kLfoSampleHold, 0.5 * c},
                                    {kLfoSquare, 0.3 * c}, {kLfoSaw, 0.3 * c}}));
   v.set(kLfo1Sync, rng.chance(0.6) ? 0 : 1);
   v.set(kLfo2Rate, rng.logRange(0.05, 1.0));
   v.set(kLfo2Shape, rng.pick<int>({{kLfoSmooth, 2.0}, {kLfoSine, 1.5}, {kLfoTriangle, 1.0}}));
   v.set(kSwellShape, rng.below(6));
   v.set(kSwellTime, rng.logRange(0.5, 8.0));
   v.set(kSwellLoop, rng.chance(0.3) ? 1 : 0);
   // Routes only with complexity: none at 0, up to four at 1.
   const int routes = static_cast<int>(std::floor(rng.range(0.0, 4.6 * c)));
   for (int i = 0; i < routes; ++i)
      addRoute(v, rng, 1.0);
}

inline void randomFx(Values &v, DiceRng &rng, double c) {
   v.set(kReverbOn, 1);
   // A small room stays a little one: in a tight space the low notes stack up.
   const double size = rng.range(0.35, 0.8 + 0.15 * c);
   v.set(kRoomSize, size);
   v.set(kRoom, rng.range(0.12, 0.2 + (0.1 + 0.2 * c) * size));
   v.set(kRoomDecay, rng.logRange(1.0, 3.0 + 3.0 * c));
   v.set(kRoomDamp, rng.range(0.3, 0.7));
   v.set(kRoomPreDelay, rng.range(5.0, 40.0));
   if (rng.chance(0.5))
      v.set(kBright, rng.range(-3.0, 2.0));
   if (rng.chance(0.15 + 0.2 * c))
      v.set(kAir, rng.range(0.05, 0.3));
   if (rng.chance(0.06 + 0.4 * c)) {
      v.set(kDelayOn, 1);
      if (rng.chance(0.6))
         v.set(kDelaySync, rng.any({3, 5, 6, 7, 9, 10}));
      else {
         v.set(kDelaySync, 0);
         v.set(kDelayTime, rng.range(0.4, 0.8));
      }
      v.set(kDelayMix, rng.range(0.15, 0.4));
      v.set(kDelayFeedback, rng.range(0.15, 0.55 + 0.1 * c));
      v.set(kDelayTone, rng.range(0.4, 0.8));
      v.set(kDelaySpread, rng.range(0.3, 0.9));
   }
}

inline void randomVoice(Values &v, DiceRng &rng, int m, double c) {
   const int mode = rng.pick<int>({{kPoly, 3.0}, {kLegato, 1.5}, {kMono, 0.6}});
   v.set(kVoiceMode, mode);
   if (mode != kPoly || rng.chance(0.15))
      v.set(kGlide, rng.logRange(15.0, 120.0 + 60.0 * c));
   v.set(kSpread, rng.range(0.2, 0.7));
   if (rng.chance(0.2))
      v.set(kPan, rng.range(-0.2, 0.2));
   // A section: two or three players, with complexity, on any but the Alien, whose coils beat.
   if (m != A && rng.chance(0.3 * c)) {
      v.set(kUnison, rng.pick<int>({{2, 2.0}, {3, 1.0}}));
      v.set(kUnisonDetune, rng.range(6.0, 16.0));
   }
}

// What it is: a word for its feel and one for the wind.
inline std::string diceName(DiceRng &rng, int model) {
   static const char *const first[] = {"Amber", "Smoky", "Reedy", "Dusky",  "Hollow", "Velvet", "Brassy", "Quiet",
                                       "Night", "Glass", "Paper", "Copper", "Misty",  "Low",    "Silver", "Wild"};
   static const char *const second[6][6] = {
      {"Flute", "Whistle", "Jet", "Pipe", "Air", "Breeze"},
      {"Clarinet", "Reed", "Chalumeau", "Cane", "Line", "Song"},
      {"Sax", "Tenor", "Alto", "Honk", "Ballad", "Croon"},
      {"Oboe", "Shawm", "Double Reed", "Pastoral", "Hymn", "Call"},
      {"Trumpet", "Horn", "Fanfare", "Brass", "Bugle", "Flugel"},
      {"Signal", "Helix", "Probe", "Drift", "Coil", "Ghost"}};
   const char *a = first[rng.below(static_cast<int>(std::size(first)))];
   const char *b = second[std::clamp(model, 0, 5)][rng.below(6)];
   return std::string(a) + " " + b;
}

// The model's neighbours, for a mutation that changes instrument.
inline int nearModel(int m, DiceRng &rng) {
   switch (m) {
   case F: return C;
   case C: return rng.chance(0.5) ? F : S;
   case S: return rng.chance(0.5) ? C : O;
   case O: return rng.chance(0.5) ? S : T;
   case T: return rng.chance(0.5) ? S : O;
   default: return O;
   }
}

} // namespace detail

// A new patch from scratch, by settings.complexity, leaving locked sections (and every param the
// dice never touch) as they are. Returns its name.
inline std::string randomizePatch(std::vector<double> &values, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   Values v(values);
   const double c = std::clamp(settings.complexity, 0.0, 1.0);
   const auto open = [&](int s) { return !settings.locked(s); };
   // Everything unlocked starts from its default, so nothing of the old patch hangs on.
   for (clap_id id = 0; id < kParamCount; ++id) {
      const int s = diceSection(id);
      if (s >= 0 && open(s))
         values[id] = kParams[id].defaultValue;
   }
   // The instrument first: everything else is chosen for it. Locked, it's the one there is.
   int m = v.choice(kModel);
   const int picked = rng.pick<int>({{F, 1.0}, {C, 1.0}, {S, 1.1}, {O, 0.9}, {T, 1.1}, {A, 1.2 * c}});
   if (open(kDiceInstrument)) {
      m = picked;
      v.set(kModel, m);
      randomInstrument(v, rng, m, c);
   }
   if (open(kDiceBore))
      randomBore(v, rng, m, c);
   if (open(kDicePlayer))
      randomPlayer(v, rng, m, c);
   if (open(kDiceVibrato))
      randomVibrato(v, rng, m, c);
   if (open(kDiceHybrid))
      randomHybrid(v, rng, m, c);
   if (open(kDiceFx))
      randomFx(v, rng, c);
   if (open(kDiceVoice))
      randomVoice(v, rng, m, c);
   // The routes last, so they only take destinations the patch has on.
   if (open(kDiceMod))
      randomMod(v, rng, c);
   return diceName(rng, m);
}

// The patch nudged by settings.amount, leaving locked sections (and every param the dice never
// touch) as they are.
inline void mutatePatch(std::vector<double> &values, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   Values v(values);
   const double a = std::clamp(settings.amount, 0.0, 1.0);
   if (a <= 0.0)
      return;
   const std::vector<double> before = values;
   // The instrument changes only at a high amount, and only to a near one.
   if (!settings.locked(kDiceInstrument) && a > 0.5 && rng.chance(0.25 * (a - 0.5)))
      v.set(kModel, nearModel(v.choice(kModel), rng));
   const int m = v.choice(kModel);
   // Values that are off at 0 (or 1, for unison): they stay off, so the patch stays itself.
   const auto offAtZero = [](clap_id id) {
      return id == kGrowl || id == kFlutter || id == kHumanize || id == kDetune || id == kHybridMix || id == kAir ||
             id == kVibDepth || id == kVibPitch || id == kGlide || id == kPan || id == kHybridDetune || id == kRoom;
   };
   for (clap_id id = 0; id < kParamCount; ++id) {
      const int section = diceSection(id);
      if (section < 0 || settings.locked(section) || id == kModel)
         continue;
      const jclap::core::ParamDef &d = kParams[id];
      const double now = values[id];
      // The routes: an amount moves only where a route is, a destination never.
      if (id >= kRouteBase && id < kRouteEnd) {
         const int r = static_cast<int>((id - kRouteBase) / kRouteStride);
         if ((id - kRouteBase) % kRouteStride == kRouteAmount && v.choice(routeParam(r, kRouteSource)) != kSrcOff) {
            const double most = std::max(routeMost(v.choice(routeParam(r, kRouteDestination))), std::fabs(now));
            v.set(id, std::clamp(now + rng.normal() * a * 0.1, -most, most));
         }
         continue;
      }
      // The buses: their amount and lag, never how they combine.
      if (id >= kBusBase && id < kRouteBase) {
         const int p = static_cast<int>((id - kBusBase) % kBusStride);
         if (p == kBusAmount && now != 0.0)
            v.set(id, now + rng.normal() * a * 0.1);
         else if (p == kBusLag && now != 0.0)
            v.set(id, now + rng.normal() * a * 60.0);
         continue;
      }
      if (d.flags & CLAP_PARAM_IS_STEPPED) {
         const double p = 0.25 * a;
         const int step = rng.chance(0.5) ? 1 : -1;
         switch (id) {
         case kMute: // the next mute along, never in or out
            if (v.choice(id) > 0 && rng.chance(p))
               v.set(id, std::clamp(v.choice(id) + step, 1, 5));
            break;
         case kHoles: {
            // A hole more or fewer, never to closed (nor Lattice 3 on the trumpet); the Alien's stay.
            const int lowest = m == T ? 2 : 1;
            if (m != A && rng.chance(p))
               v.set(id, std::clamp(v.choice(id) + step, std::min(lowest, v.choice(id)), 5));
            break;
         }
         case kTube:
            if (m != A && rng.chance(p))
               v.set(id, std::clamp(v.choice(id) + step, 0, 7));
            break;
         case kTongueType: {
            // Soft, hard and double are neighbours; flutter and breath stay as they are.
            const int t = v.choice(id);
            if (t <= kTongueDouble && rng.chance(p))
               v.set(id, std::clamp(t + step, 0, 2));
            break;
         }
         case kVibType:
            if (m != A && rng.chance(0.5 * p))
               v.set(id, std::clamp(v.choice(id) + step, 0, 3));
            break;
         case kHybridModel:
            if (v.get(kHybridMix) > 0.0 && rng.chance(p))
               v.set(id, std::clamp(v.choice(id) + step, 0, 4));
            break;
         case kLfo1Shape:
         case kLfo2Shape:
         case kSwellShape:
            if (rng.chance(0.5 * p))
               v.set(id, std::clamp(v.choice(id) + step, 0, 5));
            break;
         case kDelaySync: // a synced delay moves a note value along; a free one stays free
            if (v.choice(id) > 0 && rng.chance(p))
               v.set(id, std::clamp(v.choice(id) + step, 1, kDelaySyncCount - 1));
            break;
         case kUnison:
            if (v.choice(id) > 1 && rng.chance(0.5 * p))
               v.set(id, std::clamp(v.choice(id) + step, 2, 3));
            break;
         default: break; // switches, the voice mode, the octave and the LFOs' sync stay put
         }
         continue;
      }
      if (offAtZero(id) && now == 0.0)
         continue;
      // A value off where the patch doesn't use it is left there.
      if ((id == kMuteAmount && v.choice(kMute) == 0) ||
          (id >= kHybridOctave && id <= kHybridBreath && v.get(kHybridMix) == 0.0) ||
          (id >= kHybridCutoff && id <= kHybridFilterBreath && v.get(kHybridMix) == 0.0) ||
          (id >= kHybridAttack && id <= kHybridFilterEnv && v.get(kHybridMix) == 0.0) ||
          (id >= kDelayMix && id <= kDelaySpread && v.choice(kDelayOn) == 0) || (id == kFlutterRate && v.get(kFlutter) == 0.0) ||
          (id == kUnisonDetune && v.choice(kUnison) <= 1) || (id >= kAlienVoice && id <= kAlienSharp && m != A))
         continue;
      const Span s = sane(id, m);
      double next;
      if (logScaled(id))
         next = now * std::pow(s.hi / std::max(s.lo, 1e-3), rng.normal() * a * 0.12);
      else
         next = now + rng.normal() * a * 0.12 * (s.hi - s.lo);
      // Inside the range that sounds, or no further out than it was.
      next = std::clamp(next, std::min(s.lo, before[id]), std::max(s.hi, before[id]));
      // What's on stays on, if only just.
      if (offAtZero(id) && d.min >= 0.0)
         next = std::max(next, 0.01 * (d.max - d.min));
      v.set(id, next);
   }
   // A model change takes the new model's bore and vibrato into its sane ranges.
   if (m != static_cast<int>(std::lround(before[kModel]))) {
      for (clap_id id : {kBreath, kEmbouchure, kStiffness, kLoss, kHoleCutoff, kCone})
         if (!settings.locked(diceSection(id))) {
            const Span s = sane(id, m);
            v.set(id, std::clamp(v.get(id), s.lo, s.hi));
         }
      if (!settings.locked(kDiceBore) && (m == T || m == O))
         v.set(kHoles, std::max(v.choice(kHoles), m == T ? 2 : 1));
   }
   // Only a high amount adds a route.
   if (!settings.locked(kDiceMod) && a >= 0.6 && rng.chance(0.4 * a))
      addRoute(v, rng, a);
}

// Mutate's name for a patch: the name with a count after it, "Smoky Tenor ~2".
inline std::string mutantName(const std::string &name) {
   const size_t tilde = name.rfind(" ~");
   int n = 1;
   std::string base = name;
   if (tilde != std::string::npos && tilde + 2 < name.size() &&
       name.find_first_not_of("0123456789", tilde + 2) == std::string::npos) {
      n = std::atoi(name.c_str() + tilde + 2) + 1;
      base = name.substr(0, tilde);
   }
   return (base.empty() ? std::string("Untitled") : base) + " ~" + std::to_string(n);
}

// The patches the dice replaced, to step back through, and the ones stepped back from, to step
// forward again. Each holds every param with the preset name.
struct DiceSnapshot {
   std::vector<double> values;
   std::string name;
};
class DiceHistory {
public:
   static constexpr size_t kDepth = 64;
   // Before the dice change the patch: what it was. Stepping forward is gone after a new roll.
   void push(DiceSnapshot now) {
      back_.push_back(std::move(now));
      if (back_.size() > kDepth)
         back_.erase(back_.begin());
      forward_.clear();
   }
   bool canUndo() const { return !back_.empty(); }
   bool canRedo() const { return !forward_.empty(); }
   // The patch to go back to, given the one there is now (kept to step forward to).
   DiceSnapshot undo(DiceSnapshot now) { return swap(back_, forward_, std::move(now)); }
   DiceSnapshot redo(DiceSnapshot now) { return swap(forward_, back_, std::move(now)); }

private:
   static DiceSnapshot swap(std::vector<DiceSnapshot> &from, std::vector<DiceSnapshot> &to, DiceSnapshot now) {
      DiceSnapshot out = std::move(from.back());
      from.pop_back();
      to.push_back(std::move(now));
      return out;
   }
   std::vector<DiceSnapshot> back_, forward_;
};

} // namespace jbreath

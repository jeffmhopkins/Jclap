#pragma once

// The dice: RANDOMIZE writes a new patch for one part from scratch, and MUTATE nudges the part's
// patch it has now. Neither is noise. Randomize builds a patch the way a person would: it picks an
// instrument (analog or FM), a kind of envelope (a pluck, keys, a pad, a lead, an organ, a swell),
// then fills in each module within the ranges that sound, and sets the part's level for how many
// oscillators it stacked. Complexity says how much it reaches for: more oscillators, FM, a second
// filter, Shape envelopes, routes from the flock, couplings between creatures. Mutate moves every
// value it can by its Amount, keeps anything switched off switched off (so a patch stays itself),
// changes a choice only to a near one (a saw to a square, never to noise), and only adds a route
// when the Amount is high.
//
// Both work on the whole param list (values[i] is kParams()[i]'s) and touch one part only, and
// never the globals, the part's key range, polyphony, transpose or output. Each module belongs to a
// section that can be locked, which both leave as it is.
//
// The random numbers are this file's own, so a seed gives the same patch on every platform.

#include "params.h"
#include "shapes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace jsynth {

// The sections a lock keeps: what the dice can leave alone.
enum DiceSection {
   kDiceOsc,    // the oscillators and the mixer
   kDiceFm,     // the FM grid
   kDiceFilter, // both filters, their routing and the drive
   kDiceEnv,    // the three envelopes, in either mode, and the chain
   kDiceLfo,    // the wingbeat, the wind and LFO 1 and 2
   kDiceRoutes, // the routes and the mod buses
   kDiceFlock,  // how the species flies, and the couplings between creatures
   kDiceVoice,  // level, pan, voice mode, glide, drift and velocity
   kDiceSectionCount
};
inline const char *const kDiceSectionNames[kDiceSectionCount] = {"OSC", "FM", "FILTER", "ENV", "LFO", "ROUTES", "FLOCK", "VOICE"};

// Which section a part param (by offset) is in, or -1 for one the dice never touch.
inline int diceSection(clap_id offset) {
   switch (offset) {
   case kPartLevel:
   case kPartPan:
   case kVoiceMode:
   case kGlideTime:
   case kGlideMode:
   case kDrift:
   case kVelocityToAmp:
   case kSkyPan: return kDiceVoice;
   default: break;
   }
   if (offset >= kOscBase && offset < kFmBase)
      return kDiceOsc;
   if (offset >= kFmBase && offset < kMixBase)
      return kDiceFm;
   if (offset >= kMixBase && offset < kVoiceTailBase)
      return kDiceOsc;
   if (offset >= kVoiceTailBase && offset < kEnvBase)
      return kDiceFilter;
   if (offset >= kEnvBase && offset < kLfoBase)
      return (offset - kEnvBase) % kEnvStride == kEnvRange ? -1 : kDiceEnv; // the time range is only a view
   if (offset >= kLfoBase && offset < kFlockBase)
      return kDiceLfo;
   if (offset >= kFlockBase && offset < kRouteBase) {
      // Where the lures go is how the sky is played, not the sound.
      const clap_id p = offset - kFlockBase;
      return p == kWakeRetired || p == kLureX || p == kLureY ? -1 : kDiceFlock;
   }
   if (offset >= kRouteBase && offset < kRouteEnd)
      return kDiceRoutes;
   if (offset >= kShapeBase && offset < kBusBase)
      return kDiceEnv;
   if (offset >= kBusBase && offset < kPartEnd)
      return kDiceRoutes;
   return -1;
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
   double complexity = 0.5; // Randomize: 0 plain, 1 everything
   double amount = 0.2;     // Mutate: 0 nothing, 1 far
   unsigned locks = 0;      // a bit per DiceSection
   bool locked(int section) const { return (locks >> section) & 1u; }
};

namespace detail {

// One part's params, by offset, over the whole list.
class PartValues {
public:
   PartValues(std::vector<double> &values, int part) : values_(values), part_(part) {}
   int index(clap_id offset) const { return paramIndex(partId(part_, offset)); }
   double get(clap_id offset) const { return values_[index(offset)]; }
   int choice(clap_id offset) const { return static_cast<int>(get(offset) + 0.5); }
   void set(clap_id offset, double v) {
      const int i = index(offset);
      const jclap::core::ParamDef &d = kParams()[i].def;
      v = std::clamp(v, d.min, d.max);
      if (d.flags & CLAP_PARAM_IS_STEPPED)
         v = std::round(v);
      values_[i] = v;
   }
   int part() const { return part_; }

private:
   std::vector<double> &values_;
   int part_;
};

constexpr int kSaw = 2, kSquare = 3, kPulse = 4, kSine = 0, kTriangle = 1, kWhite = 12;
constexpr int kLowPass = 0, kHighPass = 1, kBandPass = 2, kNotch = 3, kPeak = 4, kLadder = 5, kCombPlus = 6, kCombMinus = 7;
// The ratios an FM operator sounds musical at.
inline const double kRatios[] = {0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 9.0, 14.0};

// A route that makes sense, and how deep it goes.
struct RouteIdea {
   int source;
   clap_id dest;
   double lo, hi;
   double weight;
};

// Whether a destination is a pitch, where a little goes a long way.
inline bool pitchDestination(clap_id dest) {
   if (dest == kPartFine)
      return true;
   for (int o = 0; o < kOscCount; ++o)
      for (OscParam p : {kOscCoarse, kOscFine, kOscRatio, kOscFixed})
         if (dest == oscParam(o, p))
            return true;
   return false;
}

inline int firstFreeRoute(const PartValues &v) {
   for (int r = 0; r < kRouteCount; ++r)
      if (v.choice(routeParam(r, kRouteSource)) == kSourceOff)
         return r;
   return -1;
}
inline bool hasRoute(const PartValues &v, int source, clap_id dest) {
   const int d = destinationIndex(dest);
   for (int r = 0; r < kRouteCount; ++r)
      if (v.choice(routeParam(r, kRouteSource)) == source && v.choice(routeParam(r, kRouteDestination)) == d)
         return true;
   return false;
}
// How many routes reach a destination.
inline int routesTo(const PartValues &v, clap_id dest) {
   const int d = destinationIndex(dest);
   int n = 0;
   for (int r = 0; r < kRouteCount; ++r)
      n += v.choice(routeParam(r, kRouteSource)) != kSourceOff && v.choice(routeParam(r, kRouteDestination)) == d;
   return n;
}
inline bool addRoute(PartValues &v, int source, clap_id dest, double amount) {
   const int r = firstFreeRoute(v);
   // Three into one knob is plenty: more and together they pin it at an end.
   if (r < 0 || destinationIndex(dest) < 0 || hasRoute(v, source, dest) || routesTo(v, dest) >= 3)
      return false;
   v.set(routeParam(r, kRouteSource), source);
   v.set(routeParam(r, kRouteDestination), destinationIndex(dest));
   v.set(routeParam(r, kRouteAmount), amount);
   return true;
}

// The routes worth making for this patch: the filter, the FM, vibrato and pulse width, the flock.
inline std::vector<RouteIdea> routeIdeas(const PartValues &v, double c) {
   const int env2 = kSourceEnv1 + 1, env3 = kSourceEnv1 + 2, lfo1 = kSourceLfo1, lfo2 = kSourceLfo1 + 1;
   const int velocity = kSourceVelocity, press = kSourceVelocity + 1, slide = kSourceVelocity + 3;
   const clap_id cutoff = filterParam(0, kFilterCutoff), res = filterParam(0, kFilterResonance);
   const int f1 = v.choice(filterParam(0, kFilterType));
   const bool sweepable = f1 != kCombPlus && f1 != kCombMinus && v.get(cutoff) < 0.9;
   const bool fm = v.get(fmCell(1, 0)) > 0.02 || v.get(fmCell(2, 0)) > 0.02 || v.get(fmCell(2, 1)) > 0.02;
   const clap_id fmMain = v.get(fmCell(1, 0)) > 0.02 ? fmCell(1, 0) : v.get(fmCell(2, 0)) > 0.02 ? fmCell(2, 0) : fmCell(2, 1);
   const int w1 = v.choice(oscParam(0, kOscWave));
   const bool pwm = w1 == kSquare || w1 == kPulse;
   std::vector<RouteIdea> ideas;
   if (sweepable) {
      ideas.push_back({env2, cutoff, 0.12, 0.45, 6.0});
      ideas.push_back({velocity, cutoff, 0.05, 0.25, 3.0});
      ideas.push_back({press, cutoff, 0.1, 0.35, 3.0});
      ideas.push_back({kSourceModWheel, cutoff, 0.15, 0.4, 2.0});
      ideas.push_back({slide, cutoff, 0.1, 0.3, 2.0});
      ideas.push_back({lfo2, cutoff, 0.04, 0.18, 1.5});
      ideas.push_back({kSourceModWheel, res, 0.1, 0.3, 0.8});
      ideas.push_back({kSourceSpeed, cutoff, 0.1, 0.3, 2.0 * c});
   }
   if (fm) {
      ideas.push_back({env3, fmMain, 0.15, 0.4, 5.0});
      ideas.push_back({velocity, fmMain, 0.1, 0.3, 3.0});
      ideas.push_back({press, fmMain, 0.15, 0.45, 3.0});
      ideas.push_back({kSourceModWheel, fmMain, 0.1, 0.3, 2.0});
      ideas.push_back({kSourceNearest, fmMain, 0.1, 0.3, 2.0 * c});
      ideas.push_back({slide, fmCell(1, 1), 0.1, 0.3, 1.0});
   }
   if (pwm)
      ideas.push_back({lfo1, oscParam(0, kOscShape), 0.1, 0.3, 3.0});
   ideas.push_back({lfo1, kPartFine, 0.01, 0.035, 2.0});
   ideas.push_back({press, oscParam(0, kOscShape), 0.1, 0.3, 1.0});
   ideas.push_back({velocity, flockParam(kSpeed), 0.2, 0.5, 2.0});
   ideas.push_back({press, flockParam(kSpeed), 0.2, 0.5, 1.5});
   ideas.push_back({lfo2, mixParam(kMixPan, 0), 0.2, 0.5, 1.0 * c});
   ideas.push_back({kSourceHeading, kPartPan, 0.2, 0.5, 1.5 * c});
   ideas.push_back({kSourceTurn, kPartFine, 0.01, 0.03, 1.0 * c});
   ideas.push_back({kSourceRandom, oscParam(0, kOscShape), 0.05, 0.2, 1.0 * c});
   ideas.push_back({kSourceNearest, res, 0.1, 0.3, 1.0 * c});
   if (v.get(mixParam(kMixLevel, 1)) > 0.01)
      ideas.push_back({lfo2, mixParam(kMixLevel, 1), 0.1, 0.3, 1.0 * c});
   return ideas;
}

// Adds up to `count` routes from the ideas, by weight, none twice.
inline void addRoutes(PartValues &v, DiceRng &rng, double c, int count) {
   std::vector<RouteIdea> ideas = routeIdeas(v, c);
   for (int n = 0; n < count && !ideas.empty(); ++n) {
      double total = 0.0;
      for (const RouteIdea &i : ideas)
         total += i.weight;
      if (total <= 0.0)
         break;
      double r = rng.unit() * total;
      size_t k = 0;
      for (; k + 1 < ideas.size() && r >= ideas[k].weight; ++k)
         r -= ideas[k].weight;
      const RouteIdea idea = ideas[k];
      ideas.erase(ideas.begin() + static_cast<long>(k));
      // Mostly the way you'd expect; now and then the other way round.
      const double sign = !pitchDestination(idea.dest) && rng.chance(0.15) ? -1.0 : 1.0;
      addRoute(v, idea.source, idea.dest, sign * rng.range(idea.lo, idea.hi));
   }
}

enum class Amp { Pluck, Keys, Pad, Lead, Organ, Swell };

// Envelope `e` as an ADSR of a kind, in milliseconds.
inline void adsr(PartValues &v, DiceRng &rng, int e, Amp kind) {
   double a = 1, d = 300, s = 0.7, r = 300;
   switch (kind) {
   case Amp::Pluck: a = rng.logRange(0.5, 4), d = rng.logRange(120, 700), s = rng.range(0.0, 0.15), r = rng.logRange(80, 400); break;
   case Amp::Keys: a = rng.logRange(1, 8), d = rng.logRange(500, 2500), s = rng.range(0.2, 0.6), r = rng.logRange(150, 700); break;
   case Amp::Pad: a = rng.logRange(150, 900), d = rng.logRange(400, 2000), s = rng.range(0.6, 1.0), r = rng.logRange(600, 3500); break;
   case Amp::Lead: a = rng.logRange(2, 25), d = rng.logRange(200, 800), s = rng.range(0.7, 1.0), r = rng.logRange(60, 350); break;
   case Amp::Organ: a = rng.logRange(1, 6), d = rng.logRange(50, 200), s = rng.range(0.95, 1.0), r = rng.logRange(30, 150); break;
   case Amp::Swell: a = rng.logRange(40, 250), d = rng.logRange(300, 900), s = rng.range(0.6, 0.9), r = rng.logRange(150, 600); break;
   }
   v.set(envParam(e, kEnvMode), kModeAdsr);
   v.set(envParam(e, kEnvAttack), timeValue(a));
   v.set(envParam(e, kEnvDecay), timeValue(d));
   v.set(envParam(e, kEnvSustain), s);
   v.set(envParam(e, kEnvRelease), timeValue(r));
   if (rng.chance(0.4))
      v.set(envParam(e, kEnvDecayCurve), rng.range(-0.4, 0.4));
}

// A modulation envelope (2 or 3): a quicker ADSR in the amp's spirit, or at more complexity a
// Shape from the library.
inline void modEnvelope(PartValues &v, DiceRng &rng, int e, Amp amp, double c) {
   if (rng.chance(0.1 + 0.45 * c)) {
      // Banks by name, then a shape within the bank; the ADSR bank would only repeat the ADSR.
      const int bank = rng.pick<int>({{0, 1.0}, {1, 1.5}, {2, 1.5}, {3, 0.8 * c}, {4, 1.0}, {5, 0.6 * c}, {6, 1.2}, {7, 1.0}, {9, 0.8}});
      v.set(envParam(e, kEnvMode), kModeShape);
      v.set(shapeParam(e, kShapeIndex), bank * kShapeBankSize + rng.below(kShapeBankSize));
      if (rng.chance(0.25)) {
         v.set(shapeParam(e, kShapeSync), 1);
         v.set(shapeParam(e, kShapeDivision), rng.any({2, 3, 4, 5, 7}));
      }
      else
         v.set(shapeParam(e, kShapeTime), shapeValue(rng.logRange(250, 8000)));
      v.set(shapeParam(e, kShapeRepeat), rng.pick<int>({{0, 2.0}, {kRepeatHeld, 1.0}, {kRepeatForever, 1.5}}));
      if (rng.chance(0.5)) {
         v.set(shapeParam(e, kShapeWarp), rng.range(-0.4, 0.4));
         v.set(shapeParam(e, kShapeBend), rng.range(-0.4, 0.4));
      }
      return;
   }
   adsr(v, rng, e, amp == Amp::Pad || amp == Amp::Swell ? rng.any({Amp::Swell, Amp::Keys}) : rng.any({Amp::Pluck, Amp::Keys}));
   if (amp == Amp::Pad)
      v.set(envParam(e, kEnvAttack), timeValue(rng.logRange(100, 1500)));
   v.set(envParam(e, kEnvVelocity), rng.chance(0.5) ? rng.range(0.1, 0.5) : 0.0);
}

inline void randomOscillators(PartValues &v, DiceRng &rng, double c, bool fm) {
   const int voices = 1 + (rng.chance(0.4 + 0.5 * c) ? 1 : 0) + (rng.chance(0.1 + 0.6 * c) ? 1 : 0);
   for (int o = 0; o < kOscCount; ++o) {
      const bool on = o < voices;
      if (fm) {
         // Sine-family waves, by ratio: Osc 1 the carrier, the others its modulators (and now and
         // then a second carrier).
         const int wave = rng.chance(o == 0 ? 0.65 : 0.7) ? kSine : rng.any({5, 6, 7, 8, 9, 10, 11, kTriangle});
         v.set(oscParam(o, kOscWave), wave);
         v.set(oscParam(o, kOscPitchMode), 1);
         v.set(oscParam(o, kOscRatio), o == 0 ? rng.any({1.0, 1.0, 1.0, 0.5, 2.0}) : rng.any(
                                                   {kRatios[0], kRatios[1], kRatios[1], kRatios[2], kRatios[2], kRatios[3],
                                                    kRatios[4], kRatios[5], kRatios[6], kRatios[7], kRatios[8], kRatios[9]}));
         if (o > 0 && rng.chance(0.3))
            v.set(oscParam(o, kOscFine), rng.range(-6, 6));
         const double level = o == 0 ? rng.range(0.8, 1.0) : on && rng.chance(0.3) ? rng.range(0.25, 0.6) : 0.0;
         v.set(mixParam(kMixLevel, o), level);
         continue;
      }
      const int wave = rng.pick<int>({{kSaw, 4.0}, {kSquare, 2.0}, {kPulse, 1.5}, {kTriangle, 1.5}, {kSine, 1.0},
                                      {5, 0.3 * c}, {8, 0.3 * c}, {10, 0.3 * c}});
      v.set(oscParam(o, kOscWave), wave);
      v.set(oscParam(o, kOscPitchMode), 0);
      if (wave == kSquare)
         v.set(oscParam(o, kOscShape), 0.5 + rng.range(-0.3, 0.3) * c);
      else if (wave == kPulse)
         v.set(oscParam(o, kOscShape), rng.range(0.1, 0.45));
      else if (rng.chance(0.3 * c))
         v.set(oscParam(o, kOscShape), rng.range(0.25, 0.75));
      if (o == 0) {
         v.set(mixParam(kMixLevel, o), rng.range(0.8, 1.0));
         if (voices > 1 && rng.chance(0.5))
            v.set(oscParam(o, kOscFine), rng.range(-8, 8));
         continue;
      }
      const int coarse = o == 1 ? rng.pick<int>({{0, 4.0}, {12, 1.5}, {-12, 1.5}, {7, 1.0}, {19, 0.4}, {24, 0.3}})
                                : rng.pick<int>({{-12, 3.0}, {0, 1.5}, {12, 1.5}, {-24, 0.5}, {7, 0.6}});
      v.set(oscParam(o, kOscCoarse), coarse);
      // Two at the same pitch beat against each other, a few cents apart.
      if (coarse == 0 || rng.chance(0.3))
         v.set(oscParam(o, kOscFine), (rng.chance(0.5) ? 1 : -1) * rng.range(3, 14));
      v.set(mixParam(kMixLevel, o), on ? rng.range(o == 1 ? 0.35 : 0.25, o == 1 ? 0.9 : 0.7) : 0.0);
      // Hard sync: Osc 2 reset by Osc 1, a fifth or more above it, for the tearing sound.
      if (o == 1 && on && rng.chance(0.2 * c)) {
         v.set(oscParam(o, kOscSync), 1);
         v.set(oscParam(o, kOscCoarse), rng.any({7, 12, 14, 19}));
         v.set(oscParam(o, kOscWave), rng.any({kSaw, kSquare}));
      }
   }
   if (voices > 1 && rng.chance(0.3 + 0.4 * c))
      for (int o = 0; o < voices; ++o)
         v.set(mixParam(kMixPan, o), (o == 0 ? 0.0 : o == 1 ? -1.0 : 1.0) * rng.range(0.1, 0.4));
   if (rng.chance(0.08 + 0.3 * c)) {
      v.set(mixParam(kNoiseLevel), rng.range(0.03, 0.18));
      v.set(mixParam(kNoiseColor), rng.range(-1.0, 1.0));
   }
}

inline void randomFm(PartValues &v, DiceRng &rng, double c, bool fm) {
   if (!fm) {
      // An analog patch now and then gets a touch of FM for edge.
      if (rng.chance(0.3 * c) && v.get(mixParam(kMixLevel, 1)) > 0.0)
         v.set(fmCell(1, 0), rng.range(0.04, 0.18));
      return;
   }
   // Osc 2 into Osc 1 always; Osc 3 into 2 (a stack) or into 1 (two modulators) at more complexity.
   v.set(fmCell(1, 0), rng.range(0.12, 0.45));
   if (rng.chance(0.25 + 0.6 * c)) {
      if (rng.chance(0.5))
         v.set(fmCell(2, 1), rng.range(0.1, 0.4));
      else
         v.set(fmCell(2, 0), rng.range(0.08, 0.3));
   }
   if (rng.chance(0.4 + 0.3 * c))
      v.set(fmCell(1, 1), rng.range(0.05, 0.35));
   if (rng.chance(0.15 * c))
      v.set(fmCell(0, 0), rng.range(0.05, 0.2));
}

inline void randomFilters(PartValues &v, DiceRng &rng, double c, bool fm) {
   const auto one = [&](int f, int type) {
      v.set(filterParam(f, kFilterType), type);
      const bool band = type == kBandPass || type == kNotch || type == kPeak;
      v.set(filterParam(f, kFilterSlope), rng.pick<int>({{0, 1.5}, {1, 3.0}, {2, band ? 0.0 : 0.5}, {3, band ? 0.0 : 0.4}}));
      double cut = 0.6, res = 0.0;
      switch (type) {
      case kLowPass: cut = rng.range(0.38, 0.85), res = 0.55 * std::pow(rng.unit(), 1.5); break;
      case kLadder: cut = rng.range(0.38, 0.82), res = 0.65 * std::pow(rng.unit(), 1.3); break;
      case kBandPass: cut = rng.range(0.38, 0.62), res = rng.range(0.15, 0.45); break;
      case kHighPass: cut = rng.range(0.05, 0.32), res = rng.range(0.0, 0.4); break;
      case kNotch: cut = rng.range(0.4, 0.8), res = rng.range(0.1, 0.5); break;
      case kPeak: cut = rng.range(0.4, 0.8), res = rng.range(0.3, 0.65); break;
      default: cut = rng.range(0.35, 0.7), res = rng.range(0.2, 0.55); break; // the combs
      }
      v.set(filterParam(f, kFilterCutoff), cut);
      v.set(filterParam(f, kFilterResonance), res);
      v.set(filterParam(f, kFilterKeyTrack), rng.range(0.2, 0.7));
      if (rng.chance(0.35))
         v.set(filterParam(f, kFilterDrive), rng.range(0.0, 0.3));
   };
   if (fm && rng.chance(0.55)) {
      // FM makes its own brightness, so the filter mostly stays out of the way.
      v.set(filterParam(0, kFilterType), kLowPass);
      v.set(filterParam(0, kFilterCutoff), rng.range(0.88, 1.0));
      v.set(filterParam(0, kFilterResonance), 0.0);
   }
   else
      one(0, rng.pick<int>({{kLowPass, 5.0}, {kLadder, 4.0}, {kBandPass, 1.0}, {kHighPass, 0.6}, {kPeak, 0.5}, {kNotch, 0.4},
                            {kCombPlus, 0.4 * c}, {kCombMinus, 0.3 * c}}));
   if (rng.chance(0.1 + 0.45 * c)) {
      const bool parallel = rng.chance(0.35);
      v.set(tailParam(kFilterRouting), parallel ? 1 : 0);
      one(1, parallel ? rng.any({kLowPass, kBandPass, kPeak, kHighPass}) : rng.any({kHighPass, kLowPass, kNotch, kPeak}));
      if (parallel) {
         v.set(tailParam(kFilterBalance), rng.range(-0.5, 0.5));
         // Osc 1 into one filter, the rest into the other, now and then.
         if (rng.chance(0.5)) {
            v.set(oscParam(0, kOscFilter), 1);
            v.set(oscParam(1, kOscFilter), 2);
            v.set(oscParam(2, kOscFilter), 2);
         }
      }
   }
   if (rng.chance(0.35 + 0.2 * c)) {
      v.set(tailParam(kDriveType), rng.below(3));
      v.set(tailParam(kDriveAmount), rng.range(0.05, 0.35));
   }
}

inline void randomLfos(PartValues &v, DiceRng &rng, double c) {
   // The wingbeat and the wind: how the creatures bob and drift.
   v.set(lfoParam(0, kLfoRate), lfoValue(rng.logRange(1.5, 8.0)));
   v.set(lfoParam(0, kLfoPush), rng.range(0.05, 0.4));
   v.set(lfoParam(1, kLfoRate), lfoValue(rng.logRange(0.05, 0.5)));
   v.set(lfoParam(1, kLfoPush), rng.range(0.1, 0.5));
   // LFO 1 for vibrato and pulse width, LFO 2 slower, for drift.
   const int l1 = kMotionCount, l2 = kMotionCount + 1;
   v.set(lfoParam(l1, kLfoShape), rng.pick<int>({{0, 3.0}, {1, 2.0}, {2, 0.6}, {3, 0.5}, {4, 0.6 * c}, {5, 1.5}}));
   if (rng.chance(0.2 + 0.2 * c)) {
      v.set(lfoParam(l1, kLfoSync), 1);
      v.set(lfoParam(l1, kLfoDivision), rng.any({2, 5, 6, 8, 11}));
   }
   else
      v.set(lfoParam(l1, kLfoRate), lfoValue(rng.logRange(2.5, 7.0)));
   if (rng.chance(0.4))
      v.set(lfoParam(l1, kLfoDelay), timeValue(rng.logRange(100, 600)));
   v.set(lfoParam(l2, kLfoShape), rng.pick<int>({{0, 2.0}, {1, 1.5}, {4, 0.5 * c}, {5, 2.0}}));
   v.set(lfoParam(l2, kLfoRate), lfoValue(rng.logRange(0.05, 1.2)));
   if (rng.chance(0.4))
      v.set(lfoParam(l2, kLfoTrigger), rng.any({1, 2}));
}

inline void randomFlock(PartValues &v, DiceRng &rng, double c) {
   const int hunter = v.part() == 1;
   v.set(flockParam(kBehaviour), rng.pick<int>({{0, hunter ? 1.0 : 3.0}, {1, 1.0}, {2, hunter ? 3.0 : 0.6}, {3, 0.8}}));
   v.set(flockParam(kCohesion), rng.range(0.2, 0.8));
   v.set(flockParam(kAlignment), rng.range(0.2, 0.8));
   v.set(flockParam(kSeparation), rng.range(0.2, 0.7));
   v.set(flockParam(kSpeed), rng.range(0.2, 0.75));
   v.set(flockParam(kWander), rng.range(0.0, 0.5));
   v.set(flockParam(kLure), rng.range(0.4, 0.8));
   v.set(flockParam(kRange), rng.range(0.2, 0.6));
   v.set(flockParam(kTailLength), rng.range(0.2, 0.9));
   v.set(flockParam(kTailSpread), rng.range(0.2, 0.8));
   // The couplings between creatures, only with complexity.
   if (rng.chance(0.35 * c))
      v.set(flockParam(kNeighbourFm), rng.range(0.05, 0.3));
   if (rng.chance(0.4 * c))
      v.set(flockParam(kTunePull), rng.range(0.1, 0.5));
   if (rng.chance(0.3 * c))
      v.set(flockParam(kBite), rng.range(0.1, 0.5));
}

// A level for the part from what it stacked, so a new patch is about as loud as the bank.
inline double balancedLevel(const PartValues &v) {
   double sum = v.get(mixParam(kNoiseLevel));
   for (int o = 0; o < kOscCount; ++o)
      sum += v.get(mixParam(kMixLevel, o));
   double db = -2.0 - 20.0 * std::log10(std::max(1.0, sum));
   db -= 4.0 * v.get(tailParam(kDriveAmount));
   db -= 3.0 * std::max(0.0, v.get(filterParam(0, kFilterResonance)) - 0.4);
   return std::clamp(db, -12.0, 0.0);
}

// What it is: a word for its feel and one for the flock.
inline std::string diceName(DiceRng &rng) {
   static const char *const first[] = {"Restless", "Glass",  "Ember",  "Hollow", "Velvet", "Copper", "Drifting", "Silver",
                                       "Paper",    "Distant", "Feral", "Amber",  "Quiet",  "Bright", "Tidal",    "Lunar",
                                       "Rusted",   "Woven",  "Static", "Golden", "Salt",   "Smoke",  "Winter",   "Wild"};
   static const char *const second[] = {"Flock", "Shoal",  "Wings", "Murmur",  "Swarm", "Current", "Feather", "Choir",
                                        "Tide",  "Ink",    "Gust",  "Hawk",    "Starling", "Comet", "Lantern", "Reed",
                                        "Bloom", "Wire",   "Bell",  "Drift",   "Heron", "Ripple",  "Kite",    "Ember"};
   const char *a = first[rng.below(static_cast<int>(std::size(first)))];
   const char *b = second[rng.below(static_cast<int>(std::size(second)))];
   if (std::string(a) == b)
      b = "Flock";
   return std::string(a) + " " + b;
}

} // namespace detail

// A new patch for `part` from scratch, by settings.complexity, leaving locked sections, the other
// part and the globals as they are. Returns the patch's name.
inline std::string randomizePart(std::vector<double> &values, int part, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   PartValues v(values, part);
   const double c = std::clamp(settings.complexity, 0.0, 1.0);
   // Everything unlocked starts from its default, so nothing of the old patch hangs on.
   for (size_t i = 0; i < paramCount(); ++i) {
      const clap_id id = kParams()[i].def.id;
      if (partOf(id) != part)
         continue;
      const int s = diceSection(offsetOf(id));
      if (s >= 0 && !settings.locked(s))
         values[i] = kParams()[i].def.defaultValue;
   }
   const auto open = [&](int s) { return !settings.locked(s); };
   // FM or analog: by the oscillators when they're locked, else a coin weighted to analog.
   const bool fm = open(kDiceOsc) ? rng.chance(0.4) : v.choice(oscParam(0, kOscPitchMode)) == 1;
   const Amp amp = rng.pick<Amp>({{Amp::Pluck, 1.5}, {Amp::Keys, 1.5}, {Amp::Pad, 1.5}, {Amp::Lead, 1.2}, {Amp::Organ, 0.6},
                                  {Amp::Swell, 0.8}});
   if (open(kDiceOsc))
      randomOscillators(v, rng, c, fm);
   if (open(kDiceFm))
      randomFm(v, rng, c, fm);
   if (open(kDiceFilter))
      randomFilters(v, rng, c, fm);
   if (open(kDiceEnv)) {
      adsr(v, rng, 0, amp);
      v.set(envParam(0, kEnvVelocity), rng.chance(0.4) ? rng.range(0.1, 0.4) : 0.0);
      modEnvelope(v, rng, 1, amp, c);
      modEnvelope(v, rng, 2, amp, c);
      if (v.choice(envParam(1, kEnvMode)) == kModeShape && v.choice(envParam(2, kEnvMode)) == kModeShape && rng.chance(0.5 * c))
         v.set(chainParam(kChain), rng.any({2, 5}));
   }
   if (open(kDiceLfo))
      randomLfos(v, rng, c);
   if (open(kDiceFlock))
      randomFlock(v, rng, c);
   if (open(kDiceRoutes)) {
      // The defaults' three routes are gone; these replace them.
      for (int r = 0; r < kRouteCount; ++r)
         v.set(routeParam(r, kRouteSource), kSourceOff);
      const int count = 1 + static_cast<int>(std::lround(c * 6.0 * rng.range(0.5, 1.0)));
      addRoutes(v, rng, c, count);
      // A bus, at the top: two touches mixed, then on to the filter or the FM.
      if (rng.chance(std::max(0.0, c - 0.4))) {
         const int bus = 0;
         v.set(busParam(bus, kBusMode), rng.any<int>({kBusSum, kBusMax}));
         v.set(busParam(bus, kBusCurve), rng.below(4));
         v.set(busParam(bus, kBusLag), timeValue(rng.logRange(10, 300)));
         const int press = kSourceVelocity + 1;
         addRoute(v, press, busParam(bus, kBusOffset), rng.range(0.5, 1.0));
         addRoute(v, rng.any<int>({kSourceModWheel, kSourceSpeed, kSourceVelocity + 3}), busParam(bus, kBusOffset), rng.range(0.4, 0.9));
         const bool fmOn = v.get(fmCell(1, 0)) > 0.02;
         addRoute(v, kSourceBus1 + bus, fmOn && rng.chance(0.5) ? fmCell(1, 0) : filterParam(0, kFilterCutoff), rng.range(0.2, 0.4));
      }
   }
   if (open(kDiceVoice)) {
      const bool mono = (amp == Amp::Lead && rng.chance(0.5)) || rng.chance(0.08);
      if (mono) {
         v.set(kVoiceMode, rng.chance(0.75) ? 2 : 1);
         v.set(kGlideTime, timeValue(rng.logRange(20, 180)));
         v.set(kGlideMode, 1);
      }
      if (rng.chance(0.3 + 0.4 * c))
         v.set(kDrift, rng.range(0.05, 0.3));
      v.set(kVelocityToAmp, rng.range(0.3, 1.0));
      if (rng.chance(0.3 * c))
         v.set(kSkyPan, rng.range(0.1, 0.5));
      v.set(kPartLevel, balancedLevel(v));
   }
   return diceName(rng);
}

// `part`'s patch nudged by settings.amount, leaving locked sections, the other part and the globals
// as they are.
inline void mutatePart(std::vector<double> &values, int part, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   PartValues v(values, part);
   const double a = std::clamp(settings.amount, 0.0, 1.0);
   if (a <= 0.0)
      return;
   const std::vector<double> before = values;
   const auto was = [&](clap_id offset) { return before[v.index(offset)]; };
   // Values that are off when they're 0: they stay off, mostly, so a mutated patch is still itself.
   const auto offAtZero = [](clap_id o) {
      if (o >= kFmBase && o < kFmBase + kOscCount * kOscCount)
         return true;
      for (int i = 0; i < kOscCount; ++i)
         if (o == mixParam(kMixLevel, i))
            return true;
      for (int f = 0; f < kFilterCount; ++f)
         if (o == filterParam(f, kFilterDrive))
            return true;
      for (int e = 0; e < kEnvCount; ++e)
         if (o == envParam(e, kEnvDelay) || o == envParam(e, kEnvHold) || o == envParam(e, kEnvVelocity))
            return true;
      for (int l = 0; l < kLfoCount; ++l)
         if (o == lfoParam(l, kLfoDelay))
            return true;
      return o == mixParam(kNoiseLevel) || o == tailParam(kDriveAmount) || o == kGlideTime || o == kDrift || o == kSkyPan ||
             o == flockParam(kNeighbourFm) || o == flockParam(kTunePull) || o == flockParam(kBite) ||
             (o >= kBusBase && o < kPartEnd);
   };
   for (size_t i = 0; i < paramCount(); ++i) {
      const Param &p = kParams()[i];
      const clap_id id = p.def.id;
      if (partOf(id) != part)
         continue;
      const clap_id o = offsetOf(id);
      const int section = diceSection(o);
      if (section < 0 || settings.locked(section))
         continue;
      const double span = p.def.max - p.def.min, now = values[i];
      // Routes: only their depth, and only the ones that exist.
      if (o >= kRouteBase && o < kRouteEnd) {
         const int r = (o - kRouteBase) / kRouteStride;
         if ((o - kRouteBase) % kRouteStride != kRouteAmount || v.choice(routeParam(r, kRouteSource)) == kSourceOff)
            continue;
         const int d = v.choice(routeParam(r, kRouteDestination));
         const bool pitch = d >= 0 && d < static_cast<int>(destinations().size()) && pitchDestination(destinations()[d]);
         // No deeper than half way, or than it was, so stacked routes can't pin a knob at an end.
         const double most = std::max(std::fabs(now), pitch ? 0.04 : 0.5);
         v.set(o, std::clamp(now + rng.normal() * a * (pitch ? 0.01 : 0.15), -most, most));
         continue;
      }
      if (p.unit == Unit::Choice) {
         if (!rng.chance(0.2 * a))
            continue;
         const int c = static_cast<int>(now + 0.5);
         // Only to a near choice, and only where every choice sounds.
         if (o >= kOscBase && o < kFmBase && (o - kOscBase) % kOscStride == kOscWave) {
            if (c <= kPulse)
               v.set(o, rng.below(kPulse + 1));
            else if (c < kWhite)
               v.set(o, 5 + rng.below(kWhite - 5));
         }
         else if (o >= kFilterBase && o < kEnvBase && (o - kFilterBase) % kFilterStride == kFilterType) {
            if (c == kLowPass || c == kLadder)
               v.set(o, c == kLowPass ? kLadder : kLowPass);
            else if (c == kBandPass || c == kPeak)
               v.set(o, c == kBandPass ? kPeak : kBandPass);
            else if (c == kCombPlus || c == kCombMinus)
               v.set(o, c == kCombPlus ? kCombMinus : kCombPlus);
         }
         else if (o >= kFilterBase && o < kEnvBase && (o - kFilterBase) % kFilterStride == kFilterSlope)
            v.set(o, c + (rng.chance(0.5) ? 1 : -1));
         else if (o >= kLfoBase && o < kFlockBase && (o - kLfoBase) % kLfoStride == kLfoShape)
            v.set(o, rng.below(static_cast<int>(kLfoShapeNames.size())));
         else if (o == tailParam(kDriveType))
            v.set(o, rng.below(static_cast<int>(kDriveTypeNames.size())));
         else if (o >= kShapeBase && o < kChainBase && (o - kShapeBase) % kShapeStride == kShapeIndex)
            v.set(o, (c / kShapeBankSize) * kShapeBankSize + rng.below(kShapeBankSize)); // within its bank
         continue;
      }
      if (offAtZero(o) && now == 0.0) {
         // Now and then it comes in, a little: either way from 0 for a bipolar one.
         if (rng.chance(0.12 * a)) {
            const double x = span * rng.range(0.02, 0.25) * a;
            v.set(o, p.def.min < 0.0 ? (rng.chance(0.5) ? 0.5 : -0.5) * x : p.def.min + x);
         }
         continue;
      }
      switch (p.unit) {
      case Unit::Semitones:
         // A coarse pitch moves by an octave or a fifth, now and then.
         if (rng.chance(0.25 * a))
            v.set(o, now + rng.any({-12.0, 12.0, -7.0, 7.0, -5.0, 5.0}));
         break;
      case Unit::Cents: v.set(o, now + rng.normal() * a * 12.0); break;
      case Unit::Ratio:
         if (rng.chance(0.3 * a)) {
            const double *r = std::min_element(std::begin(kRatios), std::end(kRatios),
                                               [now](double x, double y) { return std::fabs(x - now) < std::fabs(y - now); });
            const long k = std::clamp<long>((r - std::begin(kRatios)) + (rng.chance(0.5) ? 1 : -1), 0,
                                            static_cast<long>(std::size(kRatios)) - 1);
            v.set(o, kRatios[k]);
         }
         break;
      case Unit::FixedHz:
      case Unit::Degrees: break;
      case Unit::Decibels: v.set(o, std::min(0.0, now + rng.normal() * a * 2.0)); break;
      default: v.set(o, now + rng.normal() * a * 0.15 * span); break;
      }
   }
   // Keep what makes a patch playable: the amp's attack and the FM and resonance no wilder than
   // they were or than a sane top, the filter open enough to hear, and enough oscillator to sound.
   const auto atMost = [&](clap_id o, double most) { v.set(o, std::min(v.get(o), std::max(was(o), most))); };
   const auto atLeast = [&](clap_id o, double least) { v.set(o, std::max(v.get(o), std::min(was(o), least))); };
   const auto within = [&](clap_id o, double most) {
      const double m = std::max(most, std::fabs(was(o)));
      v.set(o, std::clamp(v.get(o), -m, m));
   };
   if (!settings.locked(kDiceEnv)) {
      // The amp's attack moves least of all: a pluck that swells is no longer a pluck.
      atMost(envParam(0, kEnvAttack), timeValue(600));
      within(envParam(0, kEnvAttackCurve), 0.5);
      // A note that waits long to sound reads as one that doesn't.
      atMost(envParam(0, kEnvDelay), timeValue(20));
      atMost(envParam(0, kEnvHold), timeValue(300));
      for (int e = 1; e < kEnvCount; ++e)
         atMost(envParam(e, kEnvDelay), timeValue(300));
   }
   if (!settings.locked(kDiceFm))
      for (int from = 0; from < kOscCount; ++from)
         for (int to = 0; to < kOscCount; ++to)
            atMost(fmCell(from, to), from == to ? 0.45 : 0.6);
   if (!settings.locked(kDiceFilter))
      for (int f = 0; f < kFilterCount; ++f) {
         atMost(filterParam(f, kFilterResonance), 0.75);
         const int type = v.choice(filterParam(f, kFilterType));
         const clap_id cutoff = filterParam(f, kFilterCutoff);
         if (type == kLowPass || type == kLadder)
            atLeast(cutoff, 0.36);
         if (type == kHighPass)
            atMost(cutoff, 0.45);
         // A band pass passes little outside the voice's range, and less the steeper it is.
         if (type == kBandPass) {
            atLeast(cutoff, 0.35);
            atMost(cutoff, 0.68);
            atMost(filterParam(f, kFilterSlope), 1);
         }
      }
   // Pans no wider than half way, or than they were, so the part never pans itself out of one side.
   if (!settings.locked(kDiceVoice))
      within(kPartPan, 0.5);
   if (!settings.locked(kDiceFilter))
      within(tailParam(kFilterBalance), 0.6);
   if (!settings.locked(kDiceFlock))
      for (FlockParam p : {kNeighbourFm, kBite})
         atMost(flockParam(p), 0.4);
   if (!settings.locked(kDiceOsc)) {
      for (int o = 0; o < kOscCount; ++o)
         within(mixParam(kMixPan, o), 0.6);
      double sum = 0.0;
      for (int o = 0; o < kOscCount; ++o)
         sum += v.get(mixParam(kMixLevel, o));
      if (sum < 0.4)
         v.set(mixParam(kMixLevel, 0), v.get(mixParam(kMixLevel, 0)) + 0.4 - sum);
   }
   // A high Amount can bring in a new route.
   if (!settings.locked(kDiceRoutes) && rng.chance(std::max(0.0, a - 0.4)))
      addRoutes(v, rng, a, 1);
}

// Mutate's name for a patch: the name with a count after it, "Starling Bells ~2".
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
// forward again. Each holds every param with the preset name and bus names.
struct DiceSnapshot {
   std::vector<double> values;
   std::string name;
   BusNames buses{};
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

} // namespace jsynth

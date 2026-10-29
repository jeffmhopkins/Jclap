#pragma once

// The dice, as JSynth's: RANDOMIZE writes a new reverb from scratch and MUTATE nudges the one there
// is. Neither is noise. Randomize picks a kind of space first (a room, a plate, a hall, an ambient
// wash, a cloud), then fills in each card within the ranges that sound for it. Complexity says how
// much of what JVerb adds it reaches for: shimmer and feedback in the loop, the drive, the echo,
// more bodies routed to more targets, the orbit past 100%. Mutate moves every value by its Amount
// (times and frequencies on a log scale, so evenly by ear), keeps what is off switched off, changes a
// choice only to a near one, and holds the loop and the echo below where a tail would grow.
//
// Mix and Level are never touched, so a send stays a send and the gain stays where it was set; nor
// are Run, the clock or Freeze. Each param belongs to a section that can be locked, which both leave
// as it is. The random numbers are this file's own, so a seed gives the same space on every
// platform.

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

namespace jverb {

// The sections a lock keeps, named for the panel's cards and tabs.
enum DiceSection {
   kDiceSpace,  // decay, size, gravity, bodies, motion, predelay, damping
   kDiceShape,  // attack, diffusion, early/late, bass, the input cuts and React
   kDiceOrbit,  // orbit pan, tone and swirl
   kDiceBodies, // each body's mass, velocity, target and amount
   kDiceLoop,   // feedback, shimmer and its pitch, tilt, smear and safety
   kDiceDrive,  // the drive
   kDiceEcho,   // the echo
   kDiceOutput, // width and duck
   kDiceSectionCount
};
inline const char *const kDiceSectionNames[kDiceSectionCount] = {"SPACE", "SHAPE", "ORBIT", "BODIES",
                                                                 "LOOP",  "DRIVE", "ECHO",  "OUTPUT"};

// Which section a param is in, or -1 for one the dice never touch.
inline int diceSection(clap_id id) {
   if (bodyOf(id) >= 0)
      return kDiceBodies;
   switch (id) {
   case kDecay:
   case kSize:
   case kGravity:
   case kBodies:
   case kMotion:
   case kPredelay:
   case kDampHigh: return kDiceSpace;
   case kAttack:
   case kDiffusion:
   case kEarlyLate:
   case kBassMult:
   case kBassXover:
   case kLowCut:
   case kHighCut:
   case kReact: return kDiceShape;
   case kOrbitPan:
   case kOrbitTone:
   case kOrbitSwirl: return kDiceOrbit;
   case kFeedback:
   case kShimmer:
   case kShimmerPitch:
   case kTilt:
   case kSmear:
   case kSafety: return kDiceLoop;
   case kDriveOn:
   case kDriveWhere:
   case kDriveType:
   case kDrive:
   case kDriveTone:
   case kDriveBias:
   case kDriveMix:
   case kDriveTrim: return kDiceDrive;
   case kEchoOn:
   case kEchoWhere:
   case kEchoTime:
   case kEchoSync:
   case kEchoFeedback:
   case kEchoTone:
   case kEchoPingPong:
   case kEchoMix: return kDiceEcho;
   case kWidth:
   case kDuck: return kDiceOutput;
   default: return -1; // Run, Clock, Sync Bars, Freeze, Mix, Level
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
   double complexity = 0.5; // Randomize: 0 a plain space, 1 everything
   double amount = 0.2;     // Mutate: 0 nothing, 1 far
   unsigned locks = 0;      // a bit per DiceSection
   bool locked(int section) const { return (locks >> section) & 1u; }
};

namespace detail {

// The values a section's params take, clamped and stepped as the host would.
class Values {
public:
   explicit Values(std::vector<double> &v) : v_(v) {}
   double get(clap_id id) const { return v_[id]; }
   int choice(clap_id id) const { return static_cast<int>(v_[id] + 0.5); }
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
   case kDecay:
   case kSize:
   case kGravity:
   case kDampHigh:
   case kBassMult:
   case kBassXover:
   case kLowCut:
   case kHighCut:
   case kEchoTime: return true;
   default: return bodyOf(id) >= 0 && bodyRole(id) == kBody1Mass;
   }
}

enum class Space { Room, Plate, Hall, Ambient, Cloud };

// What a body's motion can move without bending anything the patch doesn't have on.
inline Target bodyTarget(const Values &v, DiceRng &rng) {
   std::vector<std::pair<Target, double>> t = {{Target::Decay, 1.0}, {Target::Damping, 1.5}, {Target::Diffusion, 1.0},
                                              {Target::EarlyLate, 1.0}, {Target::Tilt, 1.2}, {Target::Width, 1.2}};
   if (v.get(kShimmer) > 0.0)
      t.push_back({Target::Shimmer, 1.2});
   if (v.get(kFeedback) > 0.0)
      t.push_back({Target::Feedback, 0.8});
   if (v.choice(kDriveOn) == 1)
      t.push_back({Target::Drive, 1.0});
   double total = 0.0;
   for (const auto &x : t)
      total += x.second;
   double r = rng.unit() * total;
   for (const auto &x : t) {
      if (r < x.second)
         return x.first;
      r -= x.second;
   }
   return t.front().first;
}

inline void randomSpace(Values &v, DiceRng &rng, Space space, double c) {
   double decay = 2, size = 1, predelay = 20, damp = 7000;
   switch (space) {
   case Space::Room: decay = rng.logRange(0.4, 1.4), size = rng.logRange(0.25, 0.8), predelay = rng.range(0, 15), damp = rng.logRange(4000, 12000); break;
   case Space::Plate: decay = rng.logRange(1.0, 3.5), size = rng.logRange(0.4, 1.2), predelay = rng.range(0, 25), damp = rng.logRange(8000, 16000); break;
   case Space::Hall: decay = rng.logRange(1.8, 5.0), size = rng.logRange(0.8, 2.2), predelay = rng.range(10, 45), damp = rng.logRange(4500, 10000); break;
   case Space::Ambient: decay = rng.logRange(5.0, 18.0), size = rng.logRange(1.5, 4.5), predelay = rng.range(20, 80), damp = rng.logRange(3500, 9000); break;
   case Space::Cloud: decay = rng.logRange(12.0, 40.0), size = rng.logRange(3.0, 9.0), predelay = rng.range(30, 150), damp = rng.logRange(3000, 8000); break;
   }
   v.set(kDecay, decay);
   v.set(kSize, size);
   v.set(kPredelay, predelay);
   v.set(kDampHigh, damp);
   v.set(kGravity, rng.logRange(0.5, 2.0));
   // Classic spaces move little, the washes more; past 100% only with complexity.
   const bool classic = space == Space::Room || space == Space::Plate || space == Space::Hall;
   v.set(kMotion, classic ? rng.range(0.05, 0.45) : rng.range(0.3, 0.9 + 0.6 * c));
   v.set(kBodies, rng.pick<int>({{2, 0.5}, {3, 3.0}, {4, 1.0 + c}, {5, 0.5 + c}, {6, 0.3 + c}}));
}

inline void randomShape(Values &v, DiceRng &rng, Space space, double c) {
   const bool wash = space == Space::Ambient || space == Space::Cloud;
   if (wash && rng.chance(0.3 + 0.3 * c))
      v.set(kAttack, rng.range(0.2, 0.8)); // a swell
   else if (rng.chance(0.12 * c))
      v.set(kAttack, -rng.range(0.3, 0.8)); // reverse windows
   v.set(kDiffusion, space == Space::Plate ? rng.range(0.8, 1.0) : rng.range(0.5, 0.95));
   v.set(kEarlyLate, space == Space::Room ? rng.range(0.35, 0.7) : wash ? rng.range(0.8, 1.0) : rng.range(0.6, 0.9));
   v.set(kBassMult, rng.logRange(0.8, 1.6));
   v.set(kBassXover, rng.logRange(150, 500));
   v.set(kLowCut, rng.logRange(20, 160));
   v.set(kHighCut, rng.logRange(6000, 18000));
   v.set(kReact, rng.range(0.0, 0.6));
}

inline void randomOrbit(Values &v, DiceRng &rng, double c) {
   v.set(kOrbitPan, rng.range(0.3, 1.1 + 0.5 * c));
   v.set(kOrbitTone, rng.range(0.2, 0.9 + 0.5 * c));
   v.set(kOrbitSwirl, rng.range(0.2, 0.9 + 0.6 * c));
}

inline void randomLoop(Values &v, DiceRng &rng, Space space, double c) {
   const bool wash = space == Space::Ambient || space == Space::Cloud;
   if (rng.chance((wash ? 0.3 : 0.08) + 0.4 * c)) {
      v.set(kShimmer, rng.range(0.1, 0.45));
      v.set(kShimmerPitch, rng.pick<int>({{12, 4.0}, {7, 1.5}, {19, 0.8}, {24, 0.8}, {-12, 1.0}, {5, 0.5}}));
   }
   // Feedback stays below where the tail would grow on its own: that's the Extremes' job.
   if (rng.chance((wash ? 0.25 : 0.05) + 0.45 * c))
      v.set(kFeedback, rng.range(0.15, wash ? 0.7 : 0.5));
   if (rng.chance(0.5))
      v.set(kTilt, rng.range(-0.25, 0.25));
   v.set(kSmear, rng.range(0.1, 0.6));
}

inline void randomDrive(Values &v, DiceRng &rng, double c) {
   if (!rng.chance(0.08 + 0.45 * c))
      return;
   v.set(kDriveOn, 1);
   v.set(kDriveWhere, rng.pick<int>({{0, 1.2}, {1, 1.0}, {2, 1.0}}));
   const int type = rng.pick<int>({{0, 2.0}, {1, 2.0}, {2, 0.8}, {3, 0.6 * c}, {4, 0.6 * c}});
   v.set(kDriveType, type);
   const double drive = rng.range(3.0, 16.0 + 8.0 * c);
   v.set(kDrive, drive);
   v.set(kDriveTone, rng.range(-0.4, 0.3));
   v.set(kDriveBias, rng.range(0.05, 0.4));
   v.set(kDriveMix, rng.range(0.35, 1.0));
   // Back down by about what it adds, so a driven space isn't simply louder.
   v.set(kDriveTrim, -0.35 * drive);
}

inline void randomEcho(Values &v, DiceRng &rng, double c) {
   if (!rng.chance(0.08 + 0.45 * c))
      return;
   v.set(kEchoOn, 1);
   v.set(kEchoWhere, rng.pick<int>({{0, 1.5}, {1, 0.8 * c}, {2, 1.5}}));
   if (rng.chance(0.6))
      v.set(kEchoSync, rng.any({4, 5, 6, 7, 9, 10}));
   else {
      v.set(kEchoSync, 0);
      v.set(kEchoTime, rng.logRange(120, 900));
   }
   v.set(kEchoFeedback, rng.range(0.2, 0.65));
   v.set(kEchoTone, rng.range(-0.5, 0.2));
   v.set(kEchoPingPong, rng.chance(0.5) ? 1 : 0);
   v.set(kEchoMix, rng.range(0.2, 0.5));
}

inline void randomBodies(Values &v, DiceRng &rng, double c) {
   for (int b = 0; b < kMaxBodies; ++b) {
      v.set(bodyParam(b, kBody1Mass), rng.logRange(0.5, 2.0));
      v.set(bodyParam(b, kBody1Velocity), rng.range(0.85 - 0.2 * c, 1.15 + 0.2 * c));
      if (rng.chance(0.1 + 0.55 * c)) {
         v.set(bodyParam(b, kBody1Target), static_cast<double>(bodyTarget(v, rng)));
         v.set(bodyParam(b, kBody1Amount), (rng.chance(0.75) ? 1.0 : -1.0) * rng.range(0.2, 0.7));
      }
   }
}

// What it is: a word for its feel and one for the sky.
inline std::string diceName(DiceRng &rng) {
   static const char *const first[] = {"Hollow", "Velvet", "Distant", "Silver", "Frozen", "Drifting", "Amber", "Quiet",
                                       "Vast",   "Glass",  "Deep",    "Lunar",  "Ember",  "Pale",     "Dark",  "Slow"};
   static const char *const second[] = {"Nebula", "Orbit",  "Comet",   "Hall",    "Chamber", "Halo",  "Eclipse", "Void",
                                        "Aurora", "Pulsar", "Horizon", "Cathedral", "Dust", "Tide",  "Ring",    "Drift"};
   const char *a = first[rng.below(static_cast<int>(std::size(first)))];
   const char *b = second[rng.below(static_cast<int>(std::size(second)))];
   return std::string(a) + " " + b;
}

} // namespace detail

// A new space from scratch, by settings.complexity, leaving locked sections (and Mix, Level, Run,
// the clock and Freeze) as they are. Returns its name.
inline std::string randomizeSpace(std::vector<double> &values, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   Values v(values);
   const double c = std::clamp(settings.complexity, 0.0, 1.0);
   const auto open = [&](int s) { return !settings.locked(s); };
   // Everything unlocked starts from its default, so nothing of the old space hangs on.
   for (clap_id id = 0; id < kParamCount; ++id) {
      const int s = diceSection(id);
      if (s >= 0 && open(s))
         values[id] = kParams[id].defaultValue;
   }
   const Space space = rng.pick<Space>({{Space::Room, 1.0}, {Space::Plate, 1.0}, {Space::Hall, 1.5},
                                        {Space::Ambient, 1.2 + c}, {Space::Cloud, 0.4 + c}});
   if (open(kDiceSpace))
      randomSpace(v, rng, space, c);
   if (open(kDiceShape))
      randomShape(v, rng, space, c);
   if (open(kDiceOrbit))
      randomOrbit(v, rng, c);
   if (open(kDiceLoop))
      randomLoop(v, rng, space, c);
   if (open(kDiceDrive))
      randomDrive(v, rng, c);
   if (open(kDiceEcho))
      randomEcho(v, rng, c);
   // The bodies after the rest, so they only take targets the patch has on.
   if (open(kDiceBodies))
      randomBodies(v, rng, c);
   if (open(kDiceOutput)) {
      v.set(kWidth, rng.range(0.8, 1.4));
      if (rng.chance(0.25))
         v.set(kDuck, rng.range(0.1, 0.5));
   }
   return diceName(rng);
}

// The space nudged by settings.amount, leaving locked sections as they are.
inline void mutateSpace(std::vector<double> &values, const DiceSettings &settings, uint64_t seed) {
   using namespace detail;
   DiceRng rng(seed);
   Values v(values);
   const double a = std::clamp(settings.amount, 0.0, 1.0);
   if (a <= 0.0)
      return;
   const std::vector<double> before = values;
   // Values that are off at 0: they stay off, mostly, so the space stays itself.
   const auto offAtZero = [](clap_id id) {
      return id == kFeedback || id == kShimmer || id == kDuck || id == kPredelay || id == kAttack || id == kReact;
   };
   for (clap_id id = 0; id < kParamCount; ++id) {
      const int section = diceSection(id);
      if (section < 0 || settings.locked(section))
         continue;
      const jclap::core::ParamDef &d = kParams[id];
      const double now = values[id], span = d.max - d.min;
      const int body = bodyOf(id);
      // A body's amount only where it's routed, and its target only to another while it is.
      if (body >= 0 && bodyRole(id) == kBody1Amount) {
         if (v.choice(bodyParam(body, kBody1Target)) != static_cast<int>(Target::Off))
            v.set(id, std::clamp(now + rng.normal() * a * 0.15 * span, -std::max(0.7, std::fabs(now)), std::max(0.7, std::fabs(now))));
         continue;
      }
      if (body >= 0 && bodyRole(id) == kBody1Target) {
         if (v.choice(id) != static_cast<int>(Target::Off) && rng.chance(0.15 * a))
            v.set(id, static_cast<double>(bodyTarget(v, rng)));
         continue;
      }
      // Switches and slots stay put; a near choice now and then for the rest.
      if (id == kDriveOn || id == kEchoOn || id == kEchoPingPong || id == kDriveWhere || id == kEchoWhere || id == kSafety)
         continue;
      if (id == kDriveType) {
         if (rng.chance(0.2 * a))
            v.set(id, rng.below(static_cast<int>(DriveType::Count)));
         continue;
      }
      if (id == kEchoSync) {
         // A synced echo moves a note value along; a free one stays free.
         if (v.choice(id) > 0 && rng.chance(0.25 * a))
            v.set(id, std::max(1, v.choice(id) + (rng.chance(0.5) ? 1 : -1)));
         continue;
      }
      if (id == kShimmerPitch) {
         if (v.get(kShimmer) > 0.0 && rng.chance(0.15 * a))
            v.set(id, rng.any({12.0, 7.0, 19.0, 24.0, -12.0, 5.0}));
         continue;
      }
      if (id == kBodies) {
         if (rng.chance(0.2 * a))
            v.set(id, now + (rng.chance(0.5) ? 1 : -1));
         continue;
      }
      if (offAtZero(id) && now == 0.0) {
         // Now and then it comes in, a little: either way from 0 for a bipolar one.
         if (rng.chance(0.12 * a)) {
            const double x = span * rng.range(0.02, 0.2) * a;
            v.set(id, d.min < 0.0 ? (rng.chance(0.5) ? 0.5 : -0.5) * x : d.min + x);
         }
         continue;
      }
      if (logScaled(id))
         v.set(id, now * std::pow(d.max / d.min, rng.normal() * a * 0.12));
      else
         v.set(id, now + rng.normal() * a * 0.15 * span);
   }
   // Keep the tail one that fades: the loop and the echo no hotter than they were or than where a
   // tail starts to grow, and the drive's trim following its drive.
   const auto atMost = [&](clap_id id, double most) { v.set(id, std::min(v.get(id), std::max(before[id], most))); };
   if (!settings.locked(kDiceLoop)) {
      atMost(kFeedback, 0.6);
      // Tilt is in the loop too: past a quarter either way, with feedback, it can feed the bass or
      // the top back louder than it went in.
      const double tilt = std::max(0.25, std::fabs(before[kTilt]));
      v.set(kTilt, std::clamp(v.get(kTilt), -tilt, tilt));
   }
   if (!settings.locked(kDiceEcho))
      atMost(kEchoFeedback, 0.75);
   if (!settings.locked(kDiceDrive)) {
      atMost(kDrive, 24.0);
      v.set(kDriveTrim, std::min(v.get(kDriveTrim), before[kDriveTrim] - 0.35 * (v.get(kDrive) - before[kDrive])));
   }
   if (!settings.locked(kDiceSpace))
      atMost(kMotion, 1.0);
   if (!settings.locked(kDiceOrbit))
      for (clap_id id : {kOrbitPan, kOrbitTone, kOrbitSwirl})
         atMost(id, 1.2);
}

// Mutate's name for a space: the name with a count after it, "Shimmer Halo ~2".
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

// The spaces the dice replaced, to step back through, and the ones stepped back from, to step
// forward again. Each holds every param with the preset name.
struct DiceSnapshot {
   std::vector<double> values;
   std::string name;
};
class DiceHistory {
public:
   static constexpr size_t kDepth = 64;
   // Before the dice change the space: what it was. Stepping forward is gone after a new roll.
   void push(DiceSnapshot now) {
      back_.push_back(std::move(now));
      if (back_.size() > kDepth)
         back_.erase(back_.begin());
      forward_.clear();
   }
   bool canUndo() const { return !back_.empty(); }
   bool canRedo() const { return !forward_.empty(); }
   // The space to go back to, given the one there is now (kept to step forward to).
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

} // namespace jverb

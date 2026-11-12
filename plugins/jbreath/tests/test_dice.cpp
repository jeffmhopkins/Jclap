// The dice: every patch RANDOMIZE writes, at any complexity, and every one MUTATE nudges, at any
// amount, plays as a factory preset must (finite, sounding at a usable level, in tune) at a low and
// a middle note. A seed is the same patch every time, a lock keeps its section, and neither touches
// Level, Master, the Limiter, the MIDI and MPE setup, Transpose, Tune or the look.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"
#include "randomize.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jbreath;

namespace {

constexpr uint32_t kBlock = 512;
constexpr const char *kId = "com.jeffmhopkins.jclap.jbreath";
constexpr double kRate = 48000.0;

struct Run {
   jclap::core::TestHost &host;
   std::vector<float> left;

   void seconds(double s) {
      left.clear();
      uint32_t frames = static_cast<uint32_t>(s * kRate);
      float outL[kBlock], outR[kBlock];
      float *out[2] = {outL, outR};
      while (frames > 0) {
         const uint32_t n = std::min(frames, kBlock);
         host.process(n, nullptr, out);
         left.insert(left.end(), outL, outL + n);
         frames -= n;
      }
   }
   double rms() const {
      double sum = 0.0;
      for (float s : left)
         sum += s * s;
      return left.empty() ? 0.0 : std::sqrt(sum / left.size());
   }
   bool finite() const {
      return std::all_of(left.begin(), left.end(), [](float v) { return std::isfinite(v); });
   }
   // The pitch near `hz`, by normalised autocorrelation over the last 0.2 s (as the preset test).
   double pitch(double hz) const {
      const size_t n = std::min(left.size(), static_cast<size_t>(0.2 * kRate));
      const float *x = left.data() + left.size() - n;
      const int lo = static_cast<int>(kRate / (hz * 1.3)), hi = static_cast<int>(kRate / (hz / 1.3));
      const int m = static_cast<int>(n) - hi - 2;
      if (m <= 0)
         return 0.0;
      std::vector<double> r(hi + 2, 0.0);
      int best = lo;
      for (int L = lo - 1; L <= hi + 1; ++L) {
         double s = 0, e0 = 0, e1 = 0;
         for (int i = 0; i < m; ++i) {
            s += x[i] * x[i + L];
            e0 += x[i] * x[i];
            e1 += x[i + L] * x[i + L];
         }
         r[L] = s / std::sqrt(e0 * e1 + 1e-20);
         if (L >= lo && L <= hi && r[L] > r[best])
            best = L;
      }
      const double a = r[best - 1], b = r[best], c = r[best + 1], den = a - 2 * b + c;
      return kRate / (best + (den != 0 ? 0.5 * (a - c) / den : 0.0));
   }
};

double cents(double got, double want) { return got > 0 ? 1200.0 * std::log2(got / want) : 9999.0; }
double hzOf(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

// A patch plays keys 50 and 62 as the factory preset test does: finite, between -40 and -6 dB, and
// within 35 cents of the note (or a whole octave from it, past the instrument's range).
bool plays(const std::vector<double> &v, const char *what, uint64_t seed, double knob) {
   jclap::core::TestHost h(clap_entry, kId, kRate);
   Run run{h};
   for (clap_id id = 0; id < kParamCount; ++id)
      if (v[id] != kParams[id].defaultValue)
         h.setParam(id, v[id]);
   const double tune = std::round(v[kTune]) + v[kFine] / 100.0;
   bool ok = true;
   for (int key : {50, 62}) {
      h.note(CLAP_EVENT_NOTE_ON, key, 0.75, key);
      run.seconds(1.2);
      const double db = 20.0 * std::log10(run.rms() + 1e-12);
      const double hz = hzOf(key + tune);
      double best = 9999.0;
      for (double oct : {-2.0, -1.0, 0.0, 1.0, 2.0})
         best = std::min(best, std::fabs(cents(run.pitch(hz * std::pow(2.0, oct)), hz * std::pow(2.0, oct))));
      const bool good = run.finite() && db > -40.0 && db < -6.0 && best < 35.0;
      if (!good)
         std::printf("%s seed %llu at %.2f (%s), key %d: %.1f dB, %.0f cents%s\n", what,
                     static_cast<unsigned long long>(seed), knob, kModelNames[static_cast<int>(v[kModel])], key, db,
                     best, run.finite() ? "" : ", not finite");
      if (!good)
         for (clap_id id = 0; id < kParamCount; ++id)
            if (diceSection(id) >= 0 && v[id] != kParams[id].defaultValue) {
               char t[64];
               formatParam(id, v[id], t, sizeof(t));
               std::printf("   %s = %s\n", kParams[id].name, t);
            }
      ok = ok && good;
      h.note(CLAP_EVENT_NOTE_OFF, key, 0.0, key);
      run.seconds(1.5);
      ok = ok && run.finite();
   }
   return ok;
}

std::vector<double> defaults() {
   std::vector<double> d(kParamCount);
   for (clap_id id = 0; id < kParamCount; ++id)
      d[id] = kParams[id].defaultValue;
   return d;
}

// Every param the dice must never touch is as it was.
bool untouched(const std::vector<double> &before, const std::vector<double> &after) {
   for (clap_id id = 0; id < kParamCount; ++id)
      if (diceSection(id) < 0 && before[id] != after[id]) {
         std::printf("the dice moved %s\n", kParams[id].name);
         return false;
      }
   return true;
}

bool inRange(const std::vector<double> &v) {
   for (clap_id id = 0; id < kParamCount; ++id)
      if (!(v[id] >= kParams[id].min && v[id] <= kParams[id].max))
         return false;
   return true;
}

} // namespace

int main() {
   // The never-touched params set away from their defaults, so a slip would show.
   std::vector<double> base = defaults();
   base[kLevel] = -3.0;
   base[kMaster] = -1.5;
   base[kLimiter] = 2.0;
   base[kBreathSource] = kSourceAuto;
   base[kCurve] = 1.2;
   base[kSlide] = 0.4;
   base[kBendRange] = 24.0;
   base[kPalette] = 2.0;
   base[kTurbulence] = 0.7;

   // The sections all belong somewhere, and their names are the panel's.
   JCLAP_CHECK(std::string(kDiceSectionNames[kDiceInstrument]) == "INSTRUMENT");
   for (clap_id id : {kTune, kFine, kLevel, kMaster, kLimiter, kBreathSource, kBreathIn, kCurve, kSlide, kBendRange,
                      kLook, kGlow, kLoopX, kLoopSizeMin, kSmoke, kColours})
      JCLAP_CHECK_EQ(diceSection(id), -1);

   // Randomize at each complexity, a handful of seeds each, then each patch mutated a few times at
   // each amount: every one plays.
   int rolls = 0, models[6] = {};
   for (double complexity : {0.0, 0.25, 0.5, 0.75, 1.0})
      for (uint64_t seed = 1; seed <= 12; ++seed) {
         DiceSettings dice;
         dice.complexity = complexity;
         std::vector<double> v = base;
         const uint64_t s = seed * 7919 + static_cast<uint64_t>(complexity * 100);
         JCLAP_CHECK(!randomizePatch(v, dice, s).empty());
         JCLAP_CHECK(untouched(base, v));
         JCLAP_CHECK(inRange(v));
         // At 0, a plain, real instrument: no Alien, no layer, no routes, no delay, no unison.
         if (complexity == 0.0) {
            JCLAP_CHECK(v[kModel] != 5.0 && v[kHybridMix] == 0.0 && v[kDelayOn] == 0.0 && v[kUnison] == 1.0);
            for (int r = 0; r < kRouteCount; ++r)
               JCLAP_CHECK_EQ(v[routeParam(r, kRouteSource)], 0.0);
         }
         ++models[static_cast<int>(v[kModel])];
         JCLAP_CHECK(plays(v, "randomize", s, complexity));
         ++rolls;
      }
   for (double amount : {0.1, 0.4, 0.7, 1.0})
      for (uint64_t seed = 1; seed <= 8; ++seed) {
         DiceSettings dice;
         dice.complexity = 0.25 * static_cast<double>(seed % 5);
         dice.amount = amount;
         std::vector<double> v = base;
         const uint64_t s = seed * 104729 + static_cast<uint64_t>(amount * 100);
         randomizePatch(v, dice, s);
         for (int n = 0; n < 4; ++n)
            mutatePatch(v, dice, s * 31 + n);
         JCLAP_CHECK(untouched(base, v));
         JCLAP_CHECK(inRange(v));
         JCLAP_CHECK(plays(v, "mutate", s, amount));
         ++rolls;
      }
   // Each factory preset mutated hard: still plays.
   {
      const auto &presets = factoryPresets();
      DiceSettings dice;
      dice.amount = 0.6;
      for (size_t i = 0; i < presets.size(); i += 3) {
         std::vector<double> v = base;
         for (const auto &[id, x] : presets[i].values)
            if (!isLookParam(id))
               v[id] = x;
         const std::vector<double> before = v;
         mutatePatch(v, dice, 1000 + i);
         mutatePatch(v, dice, 2000 + i);
         JCLAP_CHECK(untouched(before, v));
         JCLAP_CHECK(plays(v, presets[i].name.c_str(), i, dice.amount));
         ++rolls;
      }
   }
   std::printf("%d rolls; models:", rolls);
   for (int m = 0; m < 6; ++m)
      std::printf(" %s %d", kModelNames[m], models[m]);
   std::printf("\n");
   // Every family turns up, and the Alien only with complexity.
   for (int m = 0; m < 6; ++m)
      JCLAP_CHECK(models[m] > 0);

   // A seed is the same patch, and the same name, every time; another seed is another patch.
   {
      DiceSettings dice;
      dice.complexity = 0.7;
      dice.amount = 0.5;
      std::vector<double> a = base, b = base, c = base;
      const std::string na = randomizePatch(a, dice, 42), nb = randomizePatch(b, dice, 42);
      randomizePatch(c, dice, 43);
      JCLAP_CHECK(a == b && na == nb);
      JCLAP_CHECK(a != c);
      mutatePatch(a, dice, 5);
      mutatePatch(b, dice, 5);
      JCLAP_CHECK(a == b);
      // Mutate at 0 changes nothing.
      dice.amount = 0.0;
      mutatePatch(c = a, dice, 6);
      JCLAP_CHECK(c == a);
   }

   // A lock keeps its section, through both, whichever it is; the open section still changes.
   for (int open = 0; open < kDiceSectionCount; ++open) {
      DiceSettings dice;
      dice.complexity = 1.0;
      dice.amount = 1.0;
      dice.locks = ~(1u << open) & ((1u << kDiceSectionCount) - 1);
      std::vector<double> v = base;
      bool changed = false;
      for (uint64_t seed = 1; seed <= 4; ++seed) {
         randomizePatch(v, dice, 300 + seed + open * 17);
         mutatePatch(v, dice, 400 + seed + open * 17);
      }
      bool kept = true;
      for (clap_id id = 0; id < kParamCount; ++id) {
         if (diceSection(id) == open)
            changed = changed || v[id] != base[id];
         else
            kept = kept && v[id] == base[id];
      }
      if (!kept || !changed)
         std::printf("lock: only %s open: kept the rest %d, changed it %d\n", kDiceSectionNames[open], kept, changed);
      JCLAP_CHECK(kept && changed);
   }
   // With the instrument locked, Randomize builds around the one there is.
   {
      DiceSettings dice;
      dice.complexity = 1.0;
      dice.locks = 1u << kDiceInstrument;
      for (uint64_t seed = 1; seed <= 8; ++seed) {
         std::vector<double> v = base;
         v[kModel] = 4.0;
         v[kEdge] = 1.6;
         randomizePatch(v, dice, seed);
         JCLAP_CHECK(v[kModel] == 4.0 && v[kEdge] == 1.6);
      }
   }
   // Mutate keeps what's off switched off.
   {
      DiceSettings dice;
      dice.amount = 1.0;
      for (uint64_t seed = 1; seed <= 20; ++seed) {
         std::vector<double> v = base;
         v[kModel] = 1.0;
         mutatePatch(v, dice, seed);
         JCLAP_CHECK(v[kHybridMix] == 0.0 && v[kGrowl] == 0.0 && v[kFlutter] == 0.0 && v[kDelayOn] == 0.0 &&
                     v[kMute] == 0.0 && v[kUnison] == 1.0);
      }
   }

   JCLAP_CHECK(mutantName("Smoky Tenor") == "Smoky Tenor ~1" && mutantName("Smoky Tenor ~1") == "Smoky Tenor ~2");
   JCLAP_CHECK(mutantName("") == "Untitled ~1");

   // UNDO and REDO step back and forward through what the dice replaced.
   {
      DiceHistory h;
      JCLAP_CHECK(!h.canUndo() && !h.canRedo());
      h.push({{1.0}, "a"});
      h.push({{2.0}, "b"});
      DiceSnapshot now{{3.0}, "c"};
      now = h.undo(now);
      JCLAP_CHECK(now.name == "b" && h.canRedo());
      now = h.redo(now);
      JCLAP_CHECK(now.name == "c");
   }

   return jclap::core::testResult();
}

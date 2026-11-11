// Bakes src/tuning_table.h: for every model and MIDI note, the bore length (as a ratio of the
// model's nominal length) that really plays the note's pitch, and the gain that brings its steady
// tone to the same loudness as every other note's.
//
//    jbreath-tune > src/tuning_table.h
//
// Each note is tongued and held at the model's tuning breath, the way a voice starts a note (its
// tongue, attack, kick and support at the default settings), its pitch measured by autocorrelation, and the length
// corrected by secant steps until it is within a cent. That's done at 88.2 and 96 kHz, the two
// rates voices run at for 44.1 and 48 kHz hosts; the ratio barely moves above that. A note the
// model can't sound at its pitch is marked as not playing; the plugin borrows another.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "models.h"

namespace {

using namespace jbreath;

double kFs = 96000.0;
constexpr int kFirst = 24, kLast = 108, kNotes = kLast - kFirst + 1;

std::unique_ptr<Model> make(int id) {
   switch (id) {
   case kFlute: return std::make_unique<Flute>();
   case kClarinet: return std::make_unique<Clarinet>();
   case kSax: return std::make_unique<Sax>();
   case kOboe: return std::make_unique<Oboe>();
   case kAlien: {
      // (Singing "ah", held still, so the gain levels the voice, not where a harmonic happens to
      // fall in a resonance as it wanders.)
      auto a = std::make_unique<Alien>();
      a->character.morph = a->character.drift = a->character.rough = a->character.wet = 0.0f;
      a->character.talk = 0.0f;
      return a;
   }
   default: return std::make_unique<Trumpet>();
   }
}

double measureHz(const std::vector<float> &x, double lo, double hi) {
   const int n = static_cast<int>(x.size());
   const int minLag = static_cast<int>(kFs / hi), maxLag = std::min(static_cast<int>(kFs / lo), n / 2);
   std::vector<double> r(maxLag + 2, 0.0);
   double e0 = 0, best = -1;
   for (int i = 0; i < n - maxLag; ++i)
      e0 += x[i] * x[i];
   if (e0 < 1e-9)
      return 0;
   for (int L = minLag; L <= maxLag + 1 && L < static_cast<int>(r.size()); ++L) {
      double s = 0, e1 = 0;
      for (int i = 0; i < n - maxLag; ++i) {
         s += x[i] * x[i + L];
         e1 += x[i + L] * x[i + L];
      }
      r[L] = s / std::sqrt(e0 * e1 + 1e-12);
      best = std::max(best, r[L]);
   }
   if (best < 0.8)
      return 0; // not a steady tone
   for (int L = minLag + 1; L < maxLag; ++L)
      if (r[L] > 0.92 * best && r[L] >= r[L - 1] && r[L] >= r[L + 1]) {
         const double a = r[L - 1], b = r[L], c = r[L + 1], den = a - 2 * b + c;
         return kFs / (L + (den != 0 ? 0.5 * (a - c) / den : 0));
      }
   return 0;
}

struct Played {
   double hz = 0, rms = 0;
};

// How BreathVoice tongues each model (its articulation() table, voice.h): the release surge, the
// kick, the support for a soft start and its growth under the middle note (not used here), the
// tongue's noise, and the middle note. Keep in step with voice.h.
struct Articulation {
   double surge, kick, support, noise, middle, low;
};
const Articulation kArticulation[kModelCount] = {
   {1.0, 0.6, 0.25, 0.5, 72.0, 0.0}, {0.25, 0.6, 0.1, 1.0, 62.0, 0.0}, {1.0, 1.0, 0.6, 1.0, 61.0, 0.8},
   {1.0, 0.7, 0.6, 1.0, 69.0, 0.8},  {1.0, 0.4, 0.25, 1.0, 65.0, 0.0},  {0.25, 0.4, 0.3, 0.5, 62.0, 0.5},
};

// `speaking`: the breath is a candidate for the softest that sounds the note, played as the
// voice's lightest touch: a soft strike, so no kick or surge to speak of and a slower tongue, and
// without the voice's support. (A note the support has started can hang on far below where it
// starts by itself, a reed's hysteresis, but only just; the voice's lightest touch must start by
// itself, and the support only makes it start sooner.) Otherwise the note is tongued as at mf, well
// over where it speaks.
Played playSteady(Model &m, int id, double len, double hz, double target = -1.0, bool speaking = false) {
   m.reset();
   m.notePitch(hz);
   m.articulate(0, 0.7); // (the Alien sings its syllable)
   const int n = static_cast<int>((hz < 80 ? 1.1 : 0.9) * kFs), tail = hz < 80 ? 16000 : 9000;
   std::vector<float> out;
   if (target < 0.0)
      target = modelInfo(id).tuneBreath;
   // As BreathVoice tongues a note at the default settings (Hard tongue, Attack 25 ms, Kick 0.25),
   // struck at 0.7 (or 0.1, see above): the mouth's pressure builds behind the tongue in one stage
   // (90% in 25 ms times the strike's attackScale), and the tongue's valve lets it go over 3 ms once
   // it's nearly all there, with the release surge (a 4 ms pulse) and the tongue's noise; and the
   // kick lifts the breath over where the note speaks for 25 ms.
   const Articulation &a = kArticulation[id];
   const double strike = speaking ? 0.1 : 0.7, kick = 1.6 * strike * std::sqrt(strike);
   const double pitch = 69.0 + 12.0 * std::log2(hz / 440.0);
   const double registerKick = std::clamp(std::exp2((a.middle - pitch) / 12.0), 0.25, 1.0);
   const double speak = speaking ? target : 0.65 * target; // (for the kick: about where it speaks)
   const auto coef = [](double ms) { return 1.0 - std::exp(-1000.0 / (ms * kFs)); };
   const double puffCoef = coef(25.0 * 1.4 * std::pow(0.4 / 1.4, strike) / 2.3), valveCoef = coef(0.6);
   const int valveLength = static_cast<int>(0.003 * kFs), pulseLength = static_cast<int>(0.004 * kFs);
   const double pulseAmp = a.surge * registerKick * 1.0 * kick / 1.6;
   const double noise = (0.06 + 0.94 * strike * strike) * a.noise;
   double puff = 0.0, breath = 0.0;
   int valveAt = -1, pulseAt = -1;
   for (int i = 0; i < n; ++i) {
      double b = target;
      b += a.kick * registerKick * 0.25 * kick * std::exp(-i / (0.025 * kFs)) * std::max(0.0, b - speak);
      puff += (b - puff) * (b > puff ? puffCoef : coef(9.0));
      if (valveAt < 0 && (puff >= std::min(0.9 * b, 1.15 * speak) || i >= static_cast<int>(0.04 * kFs)))
         valveAt = pulseAt = 0;
      double valve = 1.0;
      if (valveAt < valveLength) {
         const double x = valveAt < 0 ? 0.0 : static_cast<double>(valveAt) / valveLength;
         valve = x * x * (3.0 - 2.0 * x);
         valveAt += valveAt >= 0 ? 1 : 0;
      }
      breath += (puff * valve - breath) * (puff * valve > breath ? valveCoef : coef(9.0));
      double surge = 0.0;
      if (pulseAt >= 0 && pulseAt < pulseLength) {
         const double x = std::sin(3.14159265358979 * pulseAt / pulseLength);
         surge = pulseAmp * puff * x * x;
         ++pulseAt;
      }
      const double airIn = std::min(1.0, breath / std::max(speak, 0.1));
      const float tongue = static_cast<float>(noise * std::exp(-i / (0.017 * kFs)) * airIn * airIn);
      const float y = m.tick(static_cast<float>(breath + surge), 0.5f, len, tongue);
      if (i >= n - tail)
         out.push_back(y);
   }
   Played p;
   for (float v : out)
      p.rms += v * v;
   p.rms = std::sqrt(p.rms / out.size());
   if (std::isfinite(p.rms) && p.rms > 1e-4)
      p.hz = measureHz(out, hz * 0.7, hz * 1.4);
   return p;
}

struct Entry {
   double ratio = 1, gain = 1, speak = 0;
   bool plays = false;
};

// The softest breath that sounds the note: a steady tone at its pitch within 20 dB of the level at
// the tuning breath. The voices start their breath there, so the lightest touch plays.
double speakingBreath(Model &m, int id, double len, double hz, double rms) {
   double lo = 0.02, hi = modelInfo(id).tuneBreath;
   for (int it = 0; it < 9; ++it) {
      const double mid = 0.5 * (lo + hi);
      // With the voice's drift correction at that breath, as it plays.
      const double drift = breathDrift(id, mid) - breathDrift(id, modelInfo(id).tuneBreath);
      const Played p = playSteady(m, id, len * std::pow(2.0, drift / 1200.0), hz, mid, true);
      const bool speaks = p.hz > 0 && std::fabs(1200 * std::log2(p.hz / hz)) < 50 && p.rms > 0.1 * rms;
      (speaks ? hi : lo) = mid;
   }
   return hi;
}

Entry tune(Model &m, int id, double hz) {
   Entry e;
   const double nominal = m.nominal(hz);
   double L0 = nominal;
   Played p0 = playSteady(m, id, L0, hz);
   if (p0.hz <= 0)
      return e;
   double L1 = L0 * p0.hz / hz;
   Played p1 = p0;
   bool ok = false;
   for (int it = 0; it < 10; ++it) {
      p1 = playSteady(m, id, L1, hz);
      if (p1.hz <= 0)
         break;
      if (std::fabs(1200 * std::log2(p1.hz / hz)) < 1.0 + 0.2 * it) {
         ok = true;
         break;
      }
      const double g = (1 / p1.hz - 1 / p0.hz) / (L1 - L0 + 1e-9);
      const double L2 = L1 + (1 / hz - 1 / p1.hz) / (g != 0 ? g : 1e-9);
      L0 = L1;
      p0 = p1;
      L1 = std::clamp(L2, L1 * 0.8, L1 * 1.2);
   }
   if (!ok)
      return e;
   e.ratio = L1 / nominal;
   e.gain = 0.1 * modelInfo(id).level / p1.rms; // about -20 dBFS RMS at the tuning breath (by the model's level)
   // A note this quiet is barely oscillating, and levelling it would only bring up its noise.
   e.plays = e.ratio > 0.5 && e.ratio < 1.5 && e.gain < 8.0;
   if (e.plays && kFs == 96000.0)
      e.speak = speakingBreath(m, id, L1, hz, p1.rms);
   return e;
}

std::vector<Entry> bake(double fs) {
   kFs = fs;
   std::vector<Entry> table(kTuneRows * kNotes);
   std::vector<std::thread> workers;
   for (int row = 0; row < kTuneRows; ++row)
      workers.emplace_back([row, &table] {
         // (A row past the models is the sax tuned down to a bigger horn.)
         const int id = row < kModelCount ? row : kSax;
         auto m = make(id);
         if (row >= kModelCount)
            m->character.tuneSemis = kSaxHornTune[row - kModelCount];
         m->prepare(kFs);
         for (int k = 0; k < kNotes; ++k) {
            const double hz = 440.0 * std::pow(2.0, (kFirst + k - 69) / 12.0);
            Entry &e = table[row * kNotes + k];
            e = tune(*m, id, hz);
            // (The bigger horns' low notes are richer and peakier still: their level a little under.)
            if (row >= kModelCount)
               e.gain *= kSaxHornLevel;
            std::fprintf(stderr, "%s%s %d: %s %.4f %.2f %.3f\n", modelInfo(id).name, row >= kModelCount ? " (horn)" : "",
                         kFirst + k, e.plays ? "plays" : "-----", e.ratio, e.gain, e.speak);
         }
      });
   for (auto &w : workers)
      w.join();
   return table;
}

} // namespace

int main() {
   const std::vector<Entry> t88 = bake(88200.0), t96 = bake(96000.0);
   std::printf("#pragma once\n\n// Generated by tools/tune.cpp (jbreath-tune): don't edit by hand.\n//\n");
   std::printf("// For each model and MIDI note: the bore length as a ratio of the nominal length with voices at\n");
   std::printf("// 88.2 and at 96 kHz, the gain that levels its steady tone, the softest breath that sounds it,\n");
   std::printf("// and whether the model sounds it. After the models, the sax tuned down to each bigger horn.\n\n");
   std::printf("namespace jbreath {\n\nconstexpr int kTuneFirstNote = %d, kTuneLastNote = %d;\n\n", kFirst, kLast);
   std::printf("struct TuneEntry {\n   float ratio88, ratio96, gain, speak;\n   bool plays;\n};\n\n");
   std::printf("inline const TuneEntry kTuning[%d][%d] = {\n", kTuneRows, kNotes);
   static const char *horns[kSaxHorns] = {"Sax: tenor", "Sax: baritone", "Sax: bass", "Sax: contrabass"};
   for (int id = 0; id < kTuneRows; ++id) {
      std::printf("   { // %s\n", id < kModelCount ? modelInfo(id).name : horns[id - kModelCount]);
      for (int k = 0; k < kNotes; ++k) {
         const Entry &a = t88[id * kNotes + k], &b = t96[id * kNotes + k];
         const bool plays = a.plays && b.plays;
         std::printf("      {%.5ff, %.5ff, %.4ff, %.3ff, %s},%s", plays ? a.ratio : 1.0, plays ? b.ratio : 1.0,
                     plays ? b.gain : 1.0, plays ? b.speak : 0.0, plays ? "true" : "false",
                     (k % 2 == 1 || k == kNotes - 1) ? "\n" : "");
      }
      std::printf("   },\n");
   }
   std::printf("};\n\n} // namespace jbreath\n");
   return 0;
}

// Bakes src/tuning_table.h: for every model and MIDI note, the bore length (as a ratio of the
// model's nominal length) that really plays the note's pitch, and the gain that brings its steady
// tone to the same loudness as every other note's.
//
//    jbreath-tune > src/tuning_table.h
//
// Each note is tongued and held at the model's tuning breath, the way a voice starts a note (the
// attack and kick of the default settings), its pitch measured by autocorrelation, and the length
// corrected by secant steps until it is within a cent. That's done at 88.2 and 96 kHz, the two
// rates voices run at for 44.1 and 48 kHz hosts; the ratio barely moves above that. A note the
// model can't sound at its pitch is marked as not playing; the plugin borrows another.

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

Played playSteady(Model &m, int id, double len, double hz) {
   m.reset();
   m.notePitch(hz);
   const int n = static_cast<int>((hz < 80 ? 1.1 : 0.9) * kFs), tail = hz < 80 ? 16000 : 9000;
   std::vector<float> out;
   const double target = modelInfo(id).tuneBreath;
   // As BreathVoice starts a note: breath rising with a 25 ms attack, a kick of 0.25 that dies away
   // over 40 ms, and the tongue's noise on the air.
   const double rise = 1.0 - std::exp(-4000.0 / (25.0 * kFs));
   double breath = 0.0;
   for (int i = 0; i < n; ++i) {
      const double kick = 1.0 + 0.25 * std::exp(-i / (0.04 * kFs));
      breath += (target * kick - breath) * rise;
      const float tongue = static_cast<float>(std::exp(-i / (0.017 * kFs)) * std::min(1.0, breath * 20.0));
      const float y = m.tick(static_cast<float>(breath), 0.5f, len, tongue);
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
   double ratio = 1, gain = 1;
   bool plays = false;
};

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
   e.gain = 0.1 / p1.rms; // about -20 dBFS RMS at the tuning breath
   // A note this quiet is barely oscillating, and levelling it would only bring up its noise.
   e.plays = e.ratio > 0.5 && e.ratio < 1.5 && e.gain < 8.0;
   return e;
}

std::vector<Entry> bake(double fs) {
   kFs = fs;
   std::vector<Entry> table(kModelCount * kNotes);
   std::vector<std::thread> workers;
   for (int id = 0; id < kModelCount; ++id)
      workers.emplace_back([id, &table] {
         auto m = make(id);
         m->prepare(kFs);
         for (int k = 0; k < kNotes; ++k) {
            const double hz = 440.0 * std::pow(2.0, (kFirst + k - 69) / 12.0);
            table[id * kNotes + k] = tune(*m, id, hz);
            std::fprintf(stderr, "%s %d: %s %.4f %.2f\n", modelInfo(id).name, kFirst + k,
                         table[id * kNotes + k].plays ? "plays" : "-----", table[id * kNotes + k].ratio,
                         table[id * kNotes + k].gain);
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
   std::printf("// 88.2 and at 96 kHz, the gain that levels its steady tone, and whether the model sounds it.\n\n");
   std::printf("namespace jbreath {\n\nconstexpr int kTuneFirstNote = %d, kTuneLastNote = %d;\n\n", kFirst, kLast);
   std::printf("struct TuneEntry {\n   float ratio88, ratio96, gain;\n   bool plays;\n};\n\n");
   std::printf("inline const TuneEntry kTuning[%d][%d] = {\n", kModelCount, kNotes);
   for (int id = 0; id < kModelCount; ++id) {
      std::printf("   { // %s\n", modelInfo(id).name);
      for (int k = 0; k < kNotes; ++k) {
         const Entry &a = t88[id * kNotes + k], &b = t96[id * kNotes + k];
         const bool plays = a.plays && b.plays;
         std::printf("      {%.5ff, %.5ff, %.4ff, %s},%s", plays ? a.ratio : 1.0, plays ? b.ratio : 1.0, plays ? b.gain : 1.0,
                     plays ? "true" : "false", (k % 2 == 1 || k == kNotes - 1) ? "\n" : "");
      }
      std::printf("   },\n");
   }
   std::printf("};\n\n} // namespace jbreath\n");
   return 0;
}

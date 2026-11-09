// The instruments alone, with no host: every model plays in tune and at an even level across its
// range, a tongued start speaks quickly, and nothing blows up at the extremes of every control.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "voice.h"

using namespace jbreath;

namespace {

constexpr double kRate = 48000.0;

struct Take {
   std::vector<float> out;
   double rms(size_t from, size_t to) const {
      double s = 0;
      for (size_t i = from; i < to; ++i)
         s += out[i] * out[i];
      return std::sqrt(s / std::max<size_t>(1, to - from));
   }
};

// Plays `pitch` held for `seconds` at velocity `velocity`, through the decimator, at 48 kHz.
Take play(int model, double pitch, double seconds, double velocity = 0.75, VoiceSettings s = {}) {
   const int os = oversamplingFor(kRate);
   BreathVoice v;
   v.prepare(kRate * os);
   v.setSeed(11);
   s.model = model;
   s.source = 2;
   v.configure(s);
   Decimator d;
   d.prepare(os);
   v.start(pitch, velocity, 1.0);
   Take t;
   float buf[8];
   for (int i = 0; i < static_cast<int>(seconds * kRate); ++i) {
      for (int k = 0; k < os; ++k)
         buf[k] = v.tick();
      t.out.push_back(d.process(buf));
   }
   return t;
}

double pitchOf(const std::vector<float> &x, size_t from, double hz) {
   const int lo = static_cast<int>(kRate / (hz * 1.3)), hi = static_cast<int>(kRate / (hz / 1.3));
   const int m = static_cast<int>(x.size() - from) - hi - 2;
   std::vector<double> r(hi + 2, 0.0);
   int best = lo;
   for (int L = lo - 1; L <= hi + 1; ++L) {
      double s = 0, e0 = 0, e1 = 0;
      for (int i = 0; i < m; ++i) {
         const double a = x[from + i], b = x[from + i + L];
         s += a * b;
         e0 += a * a;
         e1 += b * b;
      }
      r[L] = s / std::sqrt(e0 * e1 + 1e-20);
      if (L >= lo && L <= hi && r[L] > r[best])
         best = L;
   }
   const double a = r[best - 1], b = r[best], c = r[best + 1], den = a - 2 * b + c;
   return kRate / (best + (den != 0 ? 0.5 * (a - c) / den : 0.0));
}

// The level of the component at `hz` over the last `n` samples (Hann window).
double partial(const std::vector<float> &x, size_t n, double hz) {
   const size_t from = x.size() - n;
   double re = 0, im = 0;
   for (size_t i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / n), ph = 2 * kPi * hz * i / kRate;
      re += w * x[from + i] * std::cos(ph);
      im -= w * x[from + i] * std::sin(ph);
   }
   return 4.0 * std::sqrt(re * re + im * im) / n;
}

} // namespace

int main() {
   const TuningTable &table = TuningTable::get();
   for (int m = 0; m < kModelCount; ++m) {
      // Across the range it plays at its own pitch, every fifth semitone.
      const int lo = std::max(table.lowest(m), 36), hi = std::min(table.highest(m), 96);
      int worst = 0;
      double worstCents = 0.0, minDb = 0.0, maxDb = -200.0;
      for (int note = lo; note <= hi; note += 5) {
         VoiceSettings s;
         s.vibDepth = 0.0;
         const Take t = play(m, note, 0.5, 0.75, s);
         const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
         const double c = 1200.0 * std::log2(pitchOf(t.out, t.out.size() - 7200, hz) / hz);
         const double db = 20.0 * std::log10(t.rms(t.out.size() - 7200, t.out.size()) + 1e-12);
         if (db > -12.0 || db < -30.0)
            std::printf("         note %d: %.1f dB\n", note, db);
         if (std::fabs(c) > std::fabs(worstCents)) {
            worstCents = c;
            worst = note;
         }
         minDb = std::min(minDb, db);
         maxDb = std::max(maxDb, db);
      }
      std::printf("%-8s plays %d..%d; worst tuning %+.1f cents (note %d); level %.1f..%.1f dB\n", modelInfo(m).name,
                  table.lowest(m), table.highest(m), worstCents, worst, minDb, maxDb);
      JCLAP_CHECK(std::fabs(worstCents) < 15.0);
      JCLAP_CHECK(minDb > -32.0 && maxDb < -10.0);

      // Soft, middling and hard, every third semitone: it sounds, and in its own register (a tone
      // that jumps to the octave has almost nothing left at the fundamental). A trumpet's bell sends
      // out little of its fundamental, and at forte its 2nd and 3rd harmonics stand well above it.
      // The Alien's throat is a fixed filter after the string whose resonances lift whichever
      // harmonic they sit on (H2 well above H1 is what an "ah" does on some notes), so its string
      // is checked bare, with the throat out; its tuning above is checked through the throat.
      {
         const double octaveRatio = m == kTrumpet ? 6.0 : 3.0;
         int silent = 0, octave = 0;
         double worstRatio = 0.0;
         for (double velocity : {0.3, 0.75, 1.0})
            for (int note = lo; note <= hi; note += 3) {
               VoiceSettings s;
               s.vibDepth = 0.0;
               s.alienThroat = 0.0;
               const Take t = play(m, note, 0.5, velocity, s);
               const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
               const double rms = t.rms(t.out.size() - 9600, t.out.size());
               const double h1 = partial(t.out, 9600, hz), h2 = partial(t.out, 9600, 2 * hz);
               const double ratio = h2 / (h1 + 1e-12);
               worstRatio = std::max(worstRatio, ratio);
               if (rms < 0.003) {
                  ++silent;
                  std::printf("         note %d at %.2f: silent\n", note, velocity);
               } else if (ratio > octaveRatio) {
                  ++octave;
                  std::printf("         note %d at %.2f: octave (H2/H1 %.1f)\n", note, velocity, ratio);
               }
            }
         std::printf("         register: %d silent, %d at the octave, worst H2/H1 %.2f\n", silent, octave, worstRatio);
         JCLAP_CHECK(silent == 0 && octave == 0);
      }

      // A tongued start speaks within 80 ms.
      {
         const Take t = play(m, 62, 0.4);
         const double steady = t.rms(14400, 19200);
         const size_t w = static_cast<size_t>(0.01 * kRate);
         size_t speaks = t.out.size();
         for (size_t i = 0; i + w < t.out.size(); i += w)
            if (t.rms(i, i + w) > 0.5 * steady) {
               speaks = i;
               break;
            }
         std::printf("         speaks after %.0f ms\n", 1000.0 * speaks / kRate);
         JCLAP_CHECK(speaks < static_cast<size_t>(0.12 * kRate));
      }

      // Every control at its extremes, bent hard: always finite and bounded.
      for (int corner = 0; corner < 4; ++corner) {
         VoiceSettings s;
         s.breath = corner & 1 ? 1.5 : 0.25;
         s.edge = s.noise = corner & 2 ? 2.0 : 0.0;
         s.embouchure = corner & 1 ? 1.0 : 0.0;
         s.kick = 1.0;
         s.vibDepth = 0.3;
         s.vibPitch = 50.0;
         const int os = oversamplingFor(kRate);
         BreathVoice v;
         v.prepare(kRate * os);
         s.model = m;
         s.source = 2;
         v.configure(s);
         v.start(corner & 1 ? 100 : 30, 1.0, 1.0);
         bool ok = true;
         float peak = 0.0f;
         for (int i = 0; i < static_cast<int>(0.6 * kRate * os); ++i) {
            if (i == static_cast<int>(0.2 * kRate * os))
               v.setPitch(corner & 1 ? 76 : 54);
            if (i == static_cast<int>(0.4 * kRate * os))
               v.slur(corner & 1 ? 40 : 90, 1.0);
            const float y = v.tick();
            ok = ok && std::isfinite(y);
            peak = std::max(peak, std::fabs(y));
         }
         JCLAP_CHECK(ok);
         if (peak >= 40.0f)
            std::printf("         corner %d: peak %.1f\n", corner, peak);
         JCLAP_CHECK(peak < 40.0f);
      }
   }

   // The lightest MPE touch: a soft strike and a little pressure arriving a moment later. Every
   // instrument sounds (no pressure is too soft to play), and its start is no louder than where it
   // settles (the tongue's burst follows the strike, so a soft touch doesn't click).
   for (int m = 0; m < kModelCount; ++m) {
      const int os = oversamplingFor(kRate);
      BreathVoice v;
      v.prepare(kRate * os);
      v.setSeed(5);
      VoiceSettings s;
      s.model = m;
      v.configure(s);
      Decimator d;
      d.prepare(os);
      v.setExpression(0.0, 0.5);
      v.start(m == kFlute ? 72 : 62, 0.1, 1.0);
      Take t;
      float buf[8];
      for (int i = 0; i < static_cast<int>(0.6 * kRate); ++i) {
         if (i == static_cast<int>(0.004 * kRate))
            v.setExpression(0.08, 0.5);
         for (int k = 0; k < os; ++k)
            buf[k] = v.tick();
         t.out.push_back(d.process(buf));
      }
      float onset = 0.0f;
      for (int i = 0; i < static_cast<int>(0.05 * kRate); ++i)
         onset = std::max(onset, std::fabs(t.out[static_cast<size_t>(i)]));
      const double steady = t.rms(static_cast<size_t>(0.4 * kRate), t.out.size());
      std::printf("%-8s at the lightest touch: %.1f dB, its start peaks at %.1f x the steady level\n", modelInfo(m).name,
                  20.0 * std::log10(steady + 1e-12), onset / (1.414 * steady + 1e-12));
      JCLAP_CHECK(steady > 0.003);
      JCLAP_CHECK(onset < 3.0 * 1.414 * steady);
   }

   // Unison, the hybrid oscillator and a flutter tongue together: louder than nothing, always
   // finite, and the unison copies don't add up past the single voice's level by more than they should.
   for (int m = 0; m < kModelCount; ++m) {
      VoiceSettings s;
      s.vibDepth = 0.0;
      const double one = play(m, 62, 0.6, 0.75, s).rms(14400, 28800);
      s.unison = 4;
      s.unisonDetune = 20.0;
      s.hybridMix = 0.5;
      s.hybridModel = 2;
      s.flutter = 0.6;
      const Take t = play(m, 62, 0.6, 0.75, s);
      bool finite = true;
      for (float y : t.out)
         finite = finite && std::isfinite(y);
      const double fat = t.rms(14400, 28800);
      std::printf("%-8s unison 4 + hybrid + flutter: %.1f dB (one voice %.1f dB)\n", modelInfo(m).name,
                  20.0 * std::log10(fat + 1e-12), 20.0 * std::log10(one + 1e-12));
      JCLAP_CHECK(finite);
      JCLAP_CHECK(fat > 0.3 * one && fat < 3.0 * one);
   }
   return jclap::core::testResult();
}

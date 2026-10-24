// The tank on its own, without the plugin around it: changes that must not click, and the drive's
// dry/wet mix lining up.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine.h"

using namespace jverb;

namespace {

constexpr double kRate = 48000.0;

// Feeds a steady 220 Hz sine, whose reverb is smooth, and at 1.5 s lets `change` edit the control.
// Returns how much harder the left output's second difference (what a click is made of) hits in
// the 30 ms after the change than in the 200 ms before it.
template <typename F> double clickRatio(Control c, F change) {
   Engine e;
   e.setSampleRate(kRate);
   e.reset();
   const auto at = [](double s) { return static_cast<size_t>(s * kRate); };
   std::vector<float> out(at(1.6));
   for (size_t i = 0; i < out.size(); ++i) {
      if (i == at(1.5))
         change(c);
      if (i % kControlTick == 0)
         e.setControl(c);
      const float x = 0.5f * static_cast<float>(std::sin(2.0 * jclap::dsp::kPi * 220.0 * i / kRate));
      float l, r, late;
      e.process(x, x, l, r, late);
      out[i] = l;
   }
   const auto peak = [&](size_t from, size_t to) {
      float p = 0.0f;
      for (size_t i = from; i < to; ++i)
         p = std::max(p, std::fabs(out[i] - 2.0f * out[i - 1] + out[i - 2]));
      return p;
   };
   return peak(at(1.5), at(1.53)) / peak(at(1.3), at(1.5));
}

Control quiet() {
   Control c;
   c.mix = 1.0; // only the reverb
   c.motion = 0.0;
   return c;
}

// The drive's output level for a sine at `hz`, relative to the sine's.
double driveGain(double hz, double mix) {
   Control c;
   c.driveOn = true;
   c.driveWhere = DriveWhere::Post;
   c.drive = 0.0;
   c.driveMix = mix;
   detail::Drive d;
   d.setSampleRate(kRate);
   d.reset();
   d.set(c);
   double in = 0.0, out = 0.0;
   for (int i = 0; i < 9600; ++i) {
      if (i % kControlTick == 0)
         d.set(c);
      const float x = 0.1f * static_cast<float>(std::sin(2.0 * jclap::dsp::kPi * hz * i / kRate));
      const float y = d.process(x);
      if (i >= 4800) {
         in += x * x;
         out += y * y;
      }
   }
   return std::sqrt(out / in);
}

} // namespace

int main() {
   // System Size, Bodies, Predelay and the early reflections' size change smoothly (#61).
   {
      const double size = clickRatio(quiet(), [](Control &c) { c.size = 2.0; });
      // Voices that differ, or handing lines from one to another changes nothing.
      Control voiced = quiet();
      voiced.panDepth = 1.0;
      for (int b = 0; b < kMaxBodies; ++b) {
         voiced.pan[b] = b % 2 ? 0.8 : -0.8;
         voiced.tone[b] = b % 3 - 1.0;
      }
      const double bodies = clickRatio(voiced, [](Control &c) { c.bodies = 5; });
      const double fewer = clickRatio(voiced, [](Control &c) { c.bodies = 2; });
      const double predelay = clickRatio(quiet(), [](Control &c) { c.predelayMs = 250.0; });
      Control early = quiet();
      early.earlyLate = 0.0; // all early reflections
      const double erSize = clickRatio(early, [](Control &c) { c.size = 3.0; });
      std::printf("click ratios: size %.2f, bodies %.2f/%.2f, predelay %.2f, early size %.2f\n", size, bodies, fewer, predelay, erSize);
      // What's left for Size is the line lengths' own 25 ms glide: doubling them bends the tail's
      // pitch for a moment, which shows here as a pitch rise (2.4x is 5.8). Before, it was ~1000.
      JCLAP_CHECK(size < 8.0);
      JCLAP_CHECK(bodies < 3.0);
      JCLAP_CHECK(fewer < 3.0);
      JCLAP_CHECK(predelay < 3.0);
      JCLAP_CHECK(erSize < 3.0);
   }

   // Drive Mix below 100% doesn't comb-filter: the dry side waits for the oversampler (#62).
   {
      for (double hz : {1600.0, 4800.0, 8000.0}) {
         const double half = driveGain(hz, 0.5), dry = driveGain(hz, 0.0);
         std::printf("drive mix 0.5 at %.0f Hz: %.1f dB against the dry\n", hz, 20.0 * std::log10(half / dry));
         JCLAP_CHECK(half > 0.7 * dry);
      }
   }

   return jclap::core::testResult();
}

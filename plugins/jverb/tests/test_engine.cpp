// The tank on its own, without the plugin around it: changes that must not click, and the drive's
// dry/wet mix lining up.

#include <jclap/core/test_host.h>

#include <algorithm>
#include <chrono>
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
// the 30 ms after the change than in the 200 ms before it or the 50 ms from 50 ms after.
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
   // Against the steadier of before and after: a change can leave a harsher sound (a drive turned
   // on), and that isn't a click.
   return peak(at(1.5), at(1.53)) / std::max(peak(at(1.3), at(1.5)), peak(at(1.55), at(1.6)));
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
   d.setMode(c.driveType, false);
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
      // pitch for a moment, a little sharper than the tail before or after. Before, it was ~1000.
      JCLAP_CHECK(size < 3.0);
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

   // Drive On, its slot and type, and the echo's slot fade instead of switching in a sample (#80).
   {
      const auto drive = [](DriveWhere where) {
         Control c = quiet();
         c.driveWhere = where;
         c.drive = 24.0;
         return c;
      };
      struct Case {
         const char *name;
         double ratio;
      };
      Control post = drive(DriveWhere::Post);
      post.driveOn = true;
      Control echo = quiet();
      echo.echoOn = true;
      echo.echoMix = 0.8;
      const Case cases[] = {
         {"drive on, pre", clickRatio(drive(DriveWhere::Pre), [](Control &c) { c.driveOn = true; })},
         {"drive on, post", clickRatio(drive(DriveWhere::Post), [](Control &c) { c.driveOn = true; })},
         {"drive off, post", clickRatio(post, [](Control &c) { c.driveOn = false; })},
         {"drive type, post", clickRatio(post, [](Control &c) { c.driveType = DriveType::Fold; })},
         {"drive slot pre to post", clickRatio(post, [](Control &c) { c.driveWhere = DriveWhere::Pre; })},
         {"echo slot into to after", clickRatio(echo, [](Control &c) { c.echoWhere = EchoWhere::After; })},
         {"echo slot into to loop", clickRatio(echo, [](Control &c) { c.echoWhere = EchoWhere::Loop; })},
      };
      for (const Case &k : cases) {
         std::printf("click ratio, %s: %.2f\n", k.name, k.ratio);
         // Before, these were 76 to 1332.
         JCLAP_CHECK(k.ratio < 2.0);
      }
   }

   // Freeze holds the tail: its level and treble over a minute stay close (they fell 6 dB and
   // darkened 11 dB before).
   {
      Engine e;
      e.setSampleRate(kRate);
      e.reset();
      Control c;
      c.mix = 1.0;
      c.decay = 8.0;
      c.earlyLate = 1.0;
      uint32_t rng = 1;
      double level[2] = {}, treble[2] = {};
      float prev = 0.0f;
      const auto at = [](double s) { return static_cast<size_t>(s * kRate); };
      for (size_t i = 0; i < at(64.0); ++i) {
         if (i == at(1.0))
            c.freeze = true;
         if (i % kControlTick == 0)
            e.setControl(c);
         float x = 0.0f;
         if (i < at(0.8)) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            x = ((rng & 0xffff) / 32768.0f - 1.0f) * 0.3f;
         }
         float l, r, late;
         e.process(x, x, l, r, late);
         const int slot = i >= at(8.0) && i < at(12.0) ? 0 : i >= at(60.0) ? 1 : -1;
         if (slot >= 0) {
            level[slot] += l * l;
            treble[slot] += (l - prev) * (l - prev);
         }
         prev = l;
      }
      const double drop = 10.0 * std::log10(level[0] / level[1]);
      const double darker = 10.0 * std::log10((treble[0] / level[0]) / (treble[1] / level[1]));
      std::printf("freeze, 10 s to 62 s: level -%.2f dB, treble -%.2f dB\n", drop, darker);
      JCLAP_CHECK(drop < 1.5);
      JCLAP_CHECK(darker < 3.0);
   }

   // A reset doesn't clear the whole of every delay line (#81), and still starts silent.
   {
      Engine e;
      e.setSampleRate(192000.0);
      Control c = quiet();
      for (int i = 0; i < 96000; ++i) {
         if (i % kControlTick == 0)
            e.setControl(c);
         float l, r, late;
         e.process(0.5f, -0.5f, l, r, late);
      }
      const auto start = std::chrono::steady_clock::now();
      e.reset();
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      std::printf("reset at 192 kHz: %.3f ms\n", ms);
      JCLAP_CHECK(ms < 1.0);
      float peak = 0.0f;
      for (int i = 0; i < 192000; ++i) {
         if (i % kControlTick == 0)
            e.setControl(c);
         float l, r, late;
         e.process(0.0f, 0.0f, l, r, late);
         peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
      }
      JCLAP_CHECK(peak == 0.0f);
   }

   // Every body is heard at the same level whatever the count (the pairs don't share out evenly
   // among three, five or six).
   {
      double total = 0.0;
      for (int bodies = 2; bodies <= kMaxBodies; ++bodies) {
         double energy = 0.0;
         for (int b = 0; b < bodies; ++b) {
            const int pairs = 8 / bodies + (b < 8 % bodies ? 1 : 0);
            const double g = Engine::busGain(b, bodies);
            energy += g * g * pairs;
            JCLAP_CHECK(std::fabs(g * g * pairs - 8.0 / bodies) < 1e-5);
         }
         total += std::fabs(energy - 8.0);
      }
      JCLAP_CHECK(total < 1e-4);
   }

   return jclap::core::testResult();
}

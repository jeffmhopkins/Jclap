// Checks jclap::dsp by measuring what comes out: pitch and DC of each wave, filter gains at a few
// frequencies, envelope timing and VCA smoothing.

#include <jclap/core/test_host.h>
#include <jclap/dsp/denormals.h>
#include <jclap/dsp/envelope.h>
#include <jclap/dsp/filter.h>
#include <jclap/dsp/lfo.h>
#include <jclap/dsp/math.h>
#include <jclap/dsp/oscillator.h>
#include <jclap/dsp/smoother.h>
#include <jclap/dsp/vca.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace jclap::dsp;

namespace {

constexpr double kRate = 48000.0;

// The gain, in dB, of a filter on a steady sine, measured after it settles.
double filterGainDb(Filter filter, double hz) {
   filter.reset();
   double in = 0.0, out = 0.0;
   const int n = static_cast<int>(kRate);
   for (int i = 0; i < n; ++i) {
      const float x = static_cast<float>(std::sin(2.0 * kPi * hz * i / kRate));
      const float y = filter.process(x);
      if (i >= n / 2) {
         in += x * x;
         out += y * y;
      }
   }
   return 10.0 * std::log10(out / in);
}

Filter makeFilter(Filter::Mode mode, Filter::Slope slope, double cutoff, double resonance = 0.0) {
   Filter f;
   f.setSampleRate(kRate);
   f.setMode(mode);
   f.setSlope(slope);
   f.setCutoff(cutoff);
   f.setResonance(resonance);
   return f;
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

} // namespace

int main() {
   JCLAP_CHECK(near(noteToHz(69), 440.0, 1e-9));
   JCLAP_CHECK(near(noteToHz(81), 880.0, 1e-9));
   JCLAP_CHECK(near(dbToGain(-6.0206), 0.5, 1e-4));
   JCLAP_CHECK(near(gainToDb(0.1), -20.0, 1e-9));
   JCLAP_CHECK_EQ(softClip(0.5f), 0.5f);
   JCLAP_CHECK_EQ(softClip(-0.8f), -0.8f);
   JCLAP_CHECK(softClip(1.0f) > 0.9f && softClip(1.0f) < 1.0f && softClip(100.0f) <= 1.0f);
   JCLAP_CHECK_EQ(softClip(-3.0f), -softClip(3.0f));

   // ---- oscillator: every wave is in tune, centred and in range ----
   for (auto wave : {Oscillator::Wave::Sine, Oscillator::Wave::Triangle, Oscillator::Wave::Saw,
                     Oscillator::Wave::Square, Oscillator::Wave::Pulse}) {
      Oscillator osc;
      osc.setSampleRate(kRate);
      osc.setWave(wave);
      osc.setPulseWidth(0.2);
      osc.setFrequency(440.0);
      osc.reset(0.25); // so no crossing lands exactly on the first sample
      double sum = 0.0, peak = 0.0;
      int crossings = 0;
      float last = osc.process();
      for (int i = 1; i < kRate; ++i) {
         const float s = osc.process();
         // Count one crossing per cycle, on the edge that every wave has once.
         const bool falling = wave == Oscillator::Wave::Saw || wave == Oscillator::Wave::Square ||
                              wave == Oscillator::Wave::Pulse;
         if (falling ? last > 0.0f && s <= 0.0f : last <= 0.0f && s > 0.0f)
            ++crossings;
         sum += s;
         peak = std::max(peak, static_cast<double>(std::fabs(s)));
         last = s;
      }
      JCLAP_CHECK(crossings >= 439 && crossings <= 441);
      JCLAP_CHECK(std::fabs(sum / kRate) < 0.01);
      // A centred 20% pulse spends most of its time just below zero and peaks at 1.6.
      JCLAP_CHECK(peak > 0.9 && peak < (wave == Oscillator::Wave::Pulse ? 1.7 : 1.05));
   }

   // A 20% pulse is high a fifth of the time (above its centre line once the DC is taken out).
   {
      Oscillator osc;
      osc.setSampleRate(kRate);
      osc.setWave(Oscillator::Wave::Pulse);
      osc.setPulseWidth(0.2);
      osc.setFrequency(100.0);
      int high = 0;
      for (int i = 0; i < kRate; ++i)
         high += osc.process() > 0.0f;
      JCLAP_CHECK(near(high / kRate, 0.2, 0.01));
   }

   // PolyBLEP spreads the saw's reset over two samples: no single jump is the whole 2.0 drop.
   {
      Oscillator osc;
      osc.setSampleRate(kRate);
      osc.setWave(Oscillator::Wave::Saw);
      osc.setFrequency(1234.0);
      float last = osc.process(), biggest = 0.0f;
      for (int i = 0; i < 4800; ++i) {
         const float s = osc.process();
         biggest = std::max(biggest, std::fabs(s - last));
         last = s;
      }
      JCLAP_CHECK(biggest < 1.6f);
   }

   // ---- filter ----
   using Mode = Filter::Mode;
   using Slope = Filter::Slope;
   // Low pass: flat below, -3 dB at the cutoff, 12 dB more per octave per stage above.
   {
      const Filter lp12 = makeFilter(Mode::LowPass, Slope::Db12, 1000.0);
      JCLAP_CHECK(near(filterGainDb(lp12, 100.0), 0.0, 0.3));
      JCLAP_CHECK(near(filterGainDb(lp12, 1000.0), -3.0, 0.5));
      JCLAP_CHECK(near(filterGainDb(lp12, 4000.0), -24.0, 2.0));
      for (Slope slope : {Slope::Db24, Slope::Db36, Slope::Db48}) {
         const Filter f = makeFilter(Mode::LowPass, slope, 1000.0);
         const double stages = static_cast<int>(slope);
         JCLAP_CHECK(near(filterGainDb(f, 100.0), 0.0, 0.3));
         JCLAP_CHECK(near(filterGainDb(f, 1000.0), -3.0, 0.5));
         JCLAP_CHECK(near(filterGainDb(f, 4000.0), -24.0 * stages, 3.0 * stages));
      }
   }
   // High pass: the mirror image.
   {
      const Filter hp24 = makeFilter(Mode::HighPass, Slope::Db24, 1000.0);
      JCLAP_CHECK(near(filterGainDb(hp24, 10000.0), 0.0, 0.5));
      JCLAP_CHECK(near(filterGainDb(hp24, 1000.0), -3.0, 0.5));
      JCLAP_CHECK(near(filterGainDb(hp24, 250.0), -48.0, 4.0));
   }
   // Band pass: unity at the cutoff, and steeper skirts with more stages.
   {
      const Filter bp1 = makeFilter(Mode::BandPass, Slope::Db12, 1000.0);
      const Filter bp2 = makeFilter(Mode::BandPass, Slope::Db24, 1000.0);
      JCLAP_CHECK(near(filterGainDb(bp1, 1000.0), 0.0, 0.5));
      JCLAP_CHECK(near(filterGainDb(bp2, 1000.0), 0.0, 0.5));
      JCLAP_CHECK(filterGainDb(bp1, 4000.0) < -6.0);
      JCLAP_CHECK(filterGainDb(bp2, 4000.0) < filterGainDb(bp1, 4000.0) - 6.0);
      JCLAP_CHECK(filterGainDb(bp2, 250.0) < filterGainDb(bp1, 250.0) - 6.0);
   }
   // Resonance peaks at the cutoff.
   JCLAP_CHECK(filterGainDb(makeFilter(Mode::LowPass, Slope::Db24, 1000.0, 0.8), 1000.0) > 6.0);
   // Sweeping the cutoff every sample at full resonance stays stable.
   {
      Filter f = makeFilter(Mode::LowPass, Slope::Db48, 1000.0, 1.0);
      Oscillator osc;
      osc.setSampleRate(kRate);
      osc.setWave(Oscillator::Wave::Saw);
      osc.setFrequency(110.0);
      double peak = 0.0;
      for (int i = 0; i < kRate; ++i) {
         f.setCutoff(20.0 * std::pow(1000.0, 0.5 + 0.5 * std::sin(2.0 * kPi * 3.0 * i / kRate)));
         const float y = f.process(osc.process());
         JCLAP_CHECK(std::isfinite(y));
         peak = std::max(peak, static_cast<double>(std::fabs(y)));
      }
      JCLAP_CHECK(peak < 50.0);
   }

   // ---- envelope ----
   {
      Envelope env;
      env.setSampleRate(kRate);
      env.setAttack(0.01);
      env.setDecay(0.1);
      env.setSustain(0.5);
      env.setRelease(0.2);
      JCLAP_CHECK(!env.isActive());
      env.gateOn();
      // The attack takes its 10 ms (480 samples), give or take a sample.
      int samples = 0;
      while (env.stage() == Envelope::Stage::Attack && samples < 10000) {
         env.process();
         ++samples;
      }
      JCLAP_CHECK(samples >= 479 && samples <= 482);
      JCLAP_CHECK_EQ(env.level(), 1.0f);
      // Decay lands on the sustain level and holds it.
      for (int i = 0; i < 0.1 * kRate; ++i)
         env.process();
      JCLAP_CHECK(env.stage() == Envelope::Stage::Sustain);
      JCLAP_CHECK_EQ(env.level(), 0.5f);
      // Release falls, and finishes within its time.
      env.gateOff();
      float last = env.level();
      samples = 0;
      while (env.isActive() && samples < 100000) {
         const float level = env.process();
         JCLAP_CHECK(level <= last);
         last = level;
         ++samples;
      }
      JCLAP_CHECK(samples <= 0.2 * kRate);
      JCLAP_CHECK_EQ(env.level(), 0.0f);
      // A retrigger mid-release climbs from where the level is, without a jump.
      env.gateOn();
      for (int i = 0; i < 2000; ++i)
         env.process();
      env.gateOff();
      for (int i = 0; i < 1000; ++i)
         env.process();
      const float before = env.level();
      env.gateOn();
      JCLAP_CHECK(env.process() > before);
      JCLAP_CHECK(env.level() - before < 0.01f);
   }

   // ---- LFO: in range, at its rate, and every shape starts its cycle where it should ----
   for (auto shape : {Lfo::Shape::Sine, Lfo::Shape::Triangle, Lfo::Shape::Saw, Lfo::Shape::Square, Lfo::Shape::Random}) {
      Lfo lfo;
      lfo.setSampleRate(kRate);
      lfo.setShape(shape);
      lfo.setRate(4.0);
      lfo.reset();
      double lo = 1.0, hi = -1.0, sum = 0.0;
      int changes = 0;
      double last = lfo.process();
      for (int i = 1; i < kRate; ++i) {
         const double v = lfo.process();
         lo = std::min(lo, v);
         hi = std::max(hi, v);
         sum += v;
         changes += v != last;
         last = v;
      }
      JCLAP_CHECK(lo >= -1.0 && hi <= 1.0 && hi - lo > 0.5);
      if (shape == Lfo::Shape::Random) {
         // A new value once per cycle.
         JCLAP_CHECK(changes >= 3 && changes <= 4);
      } else {
         JCLAP_CHECK(std::fabs(sum / kRate) < 0.01);
      }
   }
   {
      Lfo lfo;
      lfo.setSampleRate(kRate);
      lfo.setShape(Lfo::Shape::Triangle);
      lfo.setRate(kRate / 8.0);
      JCLAP_CHECK(lfo.process() == 0.0 && lfo.process() == 0.5 && lfo.process() == 1.0 && lfo.process() == 0.5);
   }

   // A looping envelope cycles attack and decay while held, then releases.
   {
      Envelope env;
      env.setSampleRate(kRate);
      env.setAttack(0.01);
      env.setDecay(0.01);
      env.setSustain(0.2);
      env.setRelease(0.01);
      env.setLoop(true);
      env.gateOn();
      int peaks = 0;
      Envelope::Stage last = env.stage();
      for (int i = 0; i < 0.1 * kRate; ++i) {
         env.process();
         peaks += last == Envelope::Stage::Attack && env.stage() == Envelope::Stage::Decay;
         last = env.stage();
         JCLAP_CHECK(env.stage() != Envelope::Stage::Sustain);
      }
      // About 100 ms / (10 ms + a fast climb from 0.2) per cycle.
      JCLAP_CHECK(peaks >= 5 && peaks <= 12);
      env.gateOff();
      for (int i = 0; i < 0.02 * kRate; ++i)
         env.process();
      JCLAP_CHECK(!env.isActive());
   }

   // ---- denormals: inside the scope a subnormal product flushes to zero (on x86 and ARM64) ----
   {
      volatile float tiny = 1e-30f, scale = 1e-10f;
      {
         ScopedNoDenormals noDenormals;
         const float product = tiny * scale;
#if defined(JCLAP_DSP_DENORMALS_SSE) || defined(JCLAP_DSP_DENORMALS_ARM64)
         JCLAP_CHECK_EQ(product, 0.0f);
#endif
         (void)product;
      }
      JCLAP_CHECK(tiny * scale != 0.0f);
   }

   // ---- smoother and VCA ----
   {
      Smoother s;
      s.setSampleRate(kRate);
      s.setTime(0.01);
      s.reset(0.0);
      s.setTarget(1.0);
      JCLAP_CHECK(s.process() < 0.1);
      for (int i = 0; i < 0.01 * kRate; ++i)
         s.process();
      JCLAP_CHECK(near(s.value(), 1.0, 0.01));
   }
   {
      Vca vca;
      vca.setSampleRate(kRate);
      JCLAP_CHECK_EQ(vca.process(0.5f), 0.5f);
      JCLAP_CHECK_EQ(vca.process(0.5f, 0.0f), 0.0f);
      JCLAP_CHECK(near(vca.process(1.0f, 0.25f), 0.25, 1e-6));
      // A gain change glides instead of jumping.
      vca.setGain(0.0);
      JCLAP_CHECK(vca.process(1.0f) > 0.9f);
      for (int i = 0; i < 0.01 * kRate; ++i)
         vca.process(1.0f);
      JCLAP_CHECK(vca.process(1.0f) < 0.02f);
      // Exponential response: half the control is -30 dB.
      vca.setGain(1.0);
      vca.reset();
      vca.setResponse(Vca::Response::Exponential);
      JCLAP_CHECK(near(gainToDb(vca.process(1.0f, 0.5f)), -30.0, 1e-3));
      JCLAP_CHECK_EQ(vca.process(1.0f, 1.0f), 1.0f);
   }
   // A gate let go before the attack has moved ends in silence, without a blip upward.
   {
      Envelope env;
      env.setSampleRate(48000);
      env.setRelease(0.1);
      env.gateOn();
      env.gateOff();
      float peak = 0.0f;
      for (int i = 0; i < 100 && env.isActive(); ++i)
         peak = std::max(peak, env.process());
      JCLAP_CHECK(peak == 0.0f && !env.isActive());
   }

   return jclap::core::testResult();
}

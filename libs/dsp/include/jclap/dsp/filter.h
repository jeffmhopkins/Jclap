#pragma once

// A resonant low, high or band pass filter with a choice of slope. Each stage is a zero-delay-feedback
// state variable filter (Zavalishin's topology-preserving transform, as in Cytomic's SVF), which stays
// stable and in tune when the cutoff moves every sample. Stages are tuned as a Butterworth cascade so
// the corner stays sharp, and the resonance goes on the last one.
//
//    Filter filter;
//    filter.setSampleRate(sampleRate);
//    filter.setMode(Filter::Mode::LowPass);
//    filter.setSlope(Filter::Slope::Db24);
//    filter.setCutoff(1200.0);
//    filter.setResonance(0.4);
//    float out = filter.process(in);
//
// Low and high pass fall 12 dB per octave per stage. Band pass has unity gain at the cutoff and
// falls 6 dB per octave per stage on each side.

#include <algorithm>
#include <cmath>

#include "math.h"

namespace jclap::dsp {

class Filter {
public:
   enum class Mode { LowPass, HighPass, BandPass };
   enum class Slope { Db12 = 1, Db24, Db36, Db48 };
   static constexpr int kMaxStages = 4;

   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      updateCoefficients();
   }
   void setMode(Mode mode) { mode_ = mode; }
   Mode mode() const { return mode_; }
   void setSlope(Slope slope) {
      if (slope == slope_)
         return;
      // Stages coming back in start clean, not from wherever they were left.
      for (int i = stages(); i < static_cast<int>(slope); ++i)
         stages_[i].ic1 = stages_[i].ic2 = 0.0;
      slope_ = slope;
      updateQ();
   }
   Slope slope() const { return slope_; }
   int stages() const { return static_cast<int>(slope_); }
   // Hz, clamped to 10 Hz..0.45 x the sample rate. Cheap enough to call every sample. A NaN is
   // ignored, so one bad modulation value can't leave the filter stuck at NaN.
   void setCutoff(double hz) {
      if (std::isnan(hz))
         return;
      cutoff_ = hz;
      updateCoefficients();
   }
   double cutoff() const { return cutoff_; }
   // 0 is no resonance; 1 is a sharp peak just short of self-oscillation. A NaN is ignored.
   void setResonance(double amount) {
      if (std::isnan(amount))
         return;
      amount = std::clamp(amount, 0.0, 1.0);
      if (amount == resonance_)
         return;
      resonance_ = amount;
      updateQ();
   }
   double resonance() const { return resonance_; }

   // Clear the stages' memory, for a new note or after a reset.
   void reset() {
      for (Stage &s : stages_)
         s.ic1 = s.ic2 = 0.0;
   }

   float process(float in) {
      double x = in;
      const int n = stages();
      for (int i = 0; i < n; ++i)
         x = stages_[i].process(x, mode_);
      return static_cast<float>(x);
   }

private:
   struct Stage {
      double k = 1.414, a1 = 0.0, a2 = 0.0, a3 = 0.0;
      double ic1 = 0.0, ic2 = 0.0;

      void set(double g, double q) {
         k = 1.0 / q;
         a1 = 1.0 / (1.0 + g * (g + k));
         a2 = g * a1;
         a3 = g * a2;
      }

      double process(double v0, Mode mode) {
         const double v3 = v0 - ic2;
         const double v1 = a1 * ic1 + a2 * v3;
         const double v2 = ic2 + a2 * ic1 + a3 * v3;
         ic1 = 2.0 * v1 - ic1;
         ic2 = 2.0 * v2 - ic2;
         switch (mode) {
         case Mode::HighPass:
            return v0 - k * v1 - v2;
         case Mode::BandPass:
            return k * v1;
         case Mode::LowPass:
         default:
            return v2;
         }
      }
   };

   // Each stage's Q only changes with the slope or resonance, so it is worked out then; a cutoff
   // change, which may come every sample, costs one tan().
   void updateQ() {
      const int n = stages();
      for (int i = 0; i < n; ++i) {
         // Butterworth: the poles of an order-2n filter, two per stage.
         q_[i] = 1.0 / (2.0 * std::cos((2.0 * i + 1.0) * kPi / (4.0 * n)));
         if (i == n - 1)
            q_[i] *= std::pow(30.0, resonance_);
      }
      updateCoefficients();
   }

   void updateCoefficients() {
      const double hz = std::clamp(cutoff_, 10.0, 0.45 * sampleRate_);
      const double g = std::tan(kPi * hz / sampleRate_);
      const int n = stages();
      for (int i = 0; i < n; ++i)
         stages_[i].set(g, q_[i]);
   }

   Mode mode_ = Mode::LowPass;
   Slope slope_ = Slope::Db12;
   double sampleRate_ = 48000.0, cutoff_ = 1000.0, resonance_ = 0.0;
   Stage stages_[kMaxStages];
   double q_[kMaxStages] = {0.7071067811865476, 0.0, 0.0, 0.0};
};

} // namespace jclap::dsp

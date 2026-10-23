#pragma once

// A one-pole smoother for parameter changes, so a jump in a knob's value glides instead of clicking.
//
//    Smoother cutoff;
//    cutoff.setSampleRate(sampleRate);
//    cutoff.setTime(0.02);           // about 20 ms to settle
//    cutoff.setTarget(newValue);     // whenever the param changes
//    double now = cutoff.process();  // once per sample

#include <cmath>

namespace jclap::dsp {

class Smoother {
public:
   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      update();
   }
   // Time to get within about 1% of a new target (five time constants). 0 means no smoothing.
   void setTime(double seconds) {
      time_ = seconds;
      update();
   }
   void setTarget(double target) { target_ = target; }
   // Jump straight to a value, for the first block or after a reset.
   void reset(double value) { current_ = target_ = value; }

   double process() { return current_ += (target_ - current_) * coef_; }
   double value() const { return current_; }
   double target() const { return target_; }

private:
   void update() {
      const double samples = time_ * sampleRate_ / 5.0;
      coef_ = samples > 1.0 ? 1.0 - std::exp(-1.0 / samples) : 1.0;
   }

   double sampleRate_ = 48000.0, time_ = 0.0, coef_ = 1.0;
   double current_ = 0.0, target_ = 0.0;
};

} // namespace jclap::dsp

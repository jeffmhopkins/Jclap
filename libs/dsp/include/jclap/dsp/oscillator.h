#pragma once

// An oscillator with the basic analog waves. Saw, square and pulse are band-limited with PolyBLEP
// and the triangle with PolyBLAMP, so high notes alias far less than the naive shapes would. Every
// wave has no DC offset. Sine, triangle, saw and square are -1..1; the pulse is centred rather than
// scaled, so a width w spans -2w..2-2w (a 20% pulse peaks at 1.6, the narrowest at 1.98).
//
//    Oscillator osc;
//    osc.setSampleRate(sampleRate);
//    osc.setWave(Oscillator::Wave::Pulse);
//    osc.setPulseWidth(0.25);
//    osc.setFrequency(noteToHz(60));
//    float sample = osc.process();

#include <algorithm>
#include <cmath>

#include "math.h"

namespace jclap::dsp {

class Oscillator {
public:
   enum class Wave { Sine, Triangle, Saw, Square, Pulse };

   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      setFrequency(frequency_);
   }
   void setWave(Wave wave) { wave_ = wave; }
   Wave wave() const { return wave_; }
   // Clamped to 0..Nyquist.
   void setFrequency(double hz) {
      frequency_ = hz;
      increment_ = std::clamp(hz / sampleRate_, 0.0, 0.5);
   }
   double frequency() const { return frequency_; }
   // The pulse wave's duty cycle: the fraction of each cycle spent high. 0.5 is a square. Clamped
   // to 0.01..0.99 so the pulse never vanishes.
   void setPulseWidth(double width) { pulseWidth_ = std::clamp(width, 0.01, 0.99); }
   double pulseWidth() const { return pulseWidth_; }

   // Restart the cycle, 0..1. Call on a note-on for a consistent attack, or leave it free running.
   void reset(double phase = 0.0) { phase_ = phase - std::floor(phase); }
   double phase() const { return phase_; }

   float process() {
      const double t = phase_, dt = increment_;
      double out = 0.0;
      switch (wave_) {
      case Wave::Sine:
         out = std::sin(2.0 * kPi * t);
         break;
      case Wave::Triangle:
         // Corners at t = 0 (top, the slope drops by 8 per cycle) and t = 0.5 (bottom, it rises by 8).
         out = 4.0 * std::fabs(t - 0.5) - 1.0;
         out += 4.0 * dt * (polyBlamp(wrap(t + 0.5), dt) - polyBlamp(t, dt));
         break;
      case Wave::Saw:
         out = 2.0 * t - 1.0 - polyBlep(t, dt);
         break;
      case Wave::Square:
      case Wave::Pulse: {
         const double width = wave_ == Wave::Square ? 0.5 : pulseWidth_;
         // The level and the falling edge's correction both come from the phase since that edge, so
         // rounding can't put them on opposite sides of it (a one-sample spike of 2).
         double fall = t + 1.0 - width;
         const bool high = fall < 1.0;
         if (!high)
            fall -= 1.0;
         out = (high ? 1.0 : -1.0) + polyBlep(t, dt) - polyBlep(fall, dt);
         // A pulse's average is 2 * width - 1; take it out so narrow pulses stay centred.
         out -= 2.0 * width - 1.0;
         break;
      }
      }
      phase_ += dt;
      if (phase_ >= 1.0)
         phase_ -= 1.0;
      return static_cast<float>(out);
   }

private:
   static double wrap(double t) { return t >= 1.0 ? t - 1.0 : t; }

   // The difference between a band-limited step and a hard one, for a step at t = 0 that rises by 2.
   static double polyBlep(double t, double dt) {
      if (t < dt) {
         t /= dt;
         return t + t - t * t - 1.0;
      }
      if (t > 1.0 - dt) {
         t = (t - 1.0) / dt;
         return t * t + t + t + 1.0;
      }
      return 0.0;
   }

   // The same for a corner, integrated from polyBlep: a slope change of 2s per sample needs s times this.
   static double polyBlamp(double t, double dt) {
      if (t < dt) {
         t = t / dt - 1.0;
         return -t * t * t / 3.0;
      }
      if (t > 1.0 - dt) {
         t = (t - 1.0) / dt + 1.0;
         return t * t * t / 3.0;
      }
      return 0.0;
   }

   Wave wave_ = Wave::Sine;
   double sampleRate_ = 48000.0, frequency_ = 440.0;
   double increment_ = 440.0 / 48000.0, phase_ = 0.0, pulseWidth_ = 0.5;
};

} // namespace jclap::dsp

#pragma once

// A low-frequency oscillator for modulation: sine, triangle, saw, square or sample-and-hold random,
// -1..1. Not band-limited, which is right for control signals.
//
//    Lfo lfo;
//    lfo.setSampleRate(sampleRate);
//    lfo.setShape(Lfo::Shape::Triangle);
//    lfo.setRate(5.0);
//    lfo.reset();                 // on note-on, for an LFO that restarts with each note
//    double mod = lfo.process();  // once per sample

#include <cmath>
#include <cstdint>

#include "math.h"

namespace jclap::dsp {

class Lfo {
public:
   enum class Shape { Sine, Triangle, Saw, Square, Random };

   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      setRate(rate_);
   }
   void setShape(Shape shape) { shape_ = shape; }
   Shape shape() const { return shape_; }
   // Hz.
   void setRate(double hz) {
      rate_ = hz;
      increment_ = std::fabs(hz) / sampleRate_;
   }
   double rate() const { return rate_; }

   // Restart the cycle, 0..1. Random picks a new value.
   void reset(double phase = 0.0) {
      phase_ = phase - std::floor(phase);
      held_ = nextRandom();
   }

   double process() {
      double out = 0.0;
      switch (shape_) {
      case Shape::Sine:
         out = std::sin(2.0 * kPi * phase_);
         break;
      case Shape::Triangle:
         // Starts at 0 rising, like the sine.
         out = phase_ < 0.25 ? 4.0 * phase_ : phase_ < 0.75 ? 2.0 - 4.0 * phase_ : 4.0 * phase_ - 4.0;
         break;
      case Shape::Saw:
         out = 2.0 * phase_ - 1.0;
         break;
      case Shape::Square:
         out = phase_ < 0.5 ? 1.0 : -1.0;
         break;
      case Shape::Random:
         out = held_;
         break;
      }
      phase_ += increment_;
      if (phase_ >= 1.0) {
         phase_ -= std::floor(phase_);
         held_ = nextRandom();
      }
      return out;
   }

private:
   // A small xorshift, -1..1: no allocation or locking, so it is safe on the audio thread.
   double nextRandom() {
      seed_ ^= seed_ << 13;
      seed_ ^= seed_ >> 17;
      seed_ ^= seed_ << 5;
      return seed_ / 2147483648.0 - 1.0;
   }

   Shape shape_ = Shape::Sine;
   double sampleRate_ = 48000.0, rate_ = 1.0, increment_ = 1.0 / 48000.0, phase_ = 0.0, held_ = 0.0;
   uint32_t seed_ = 0x2545f491;
};

} // namespace jclap::dsp

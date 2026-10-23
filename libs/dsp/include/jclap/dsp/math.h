#pragma once

// Small conversions shared by the rest of jclap::dsp.

#include <cmath>

namespace jclap::dsp {

constexpr double kPi = 3.14159265358979323846;

// MIDI note number (fractional is fine) to Hz, with A4 = note 69 = 440 Hz.
inline double noteToHz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

// Decibels to a linear gain, and back. gainToDb(0) is -infinity.
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double gain) { return 20.0 * std::log10(gain); }

// Leaves samples under 0.8 alone and bends louder ones smoothly toward +/-1, so overs saturate
// instead of clipping hard. For the end of a signal chain.
inline float softClip(float x) {
   constexpr float knee = 0.8f, room = 1.0f - knee;
   const float a = std::fabs(x);
   if (a <= knee)
      return x;
   const float y = knee + room * std::tanh((a - knee) / room);
   return x < 0.0f ? -y : y;
}

} // namespace jclap::dsp

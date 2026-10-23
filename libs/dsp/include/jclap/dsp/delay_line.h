#pragma once

// A delay line read at any fractional delay, for reverbs, choruses and pitch shifters. Reads use
// four-point Hermite interpolation, so a delay that moves every sample (modulation, Doppler) stays
// smooth and bright.
//
//    DelayLine line;
//    line.setMaxDelay(sampleRate);          // in activate(): one second; allocates
//    line.write(in);                        // once per sample
//    float out = line.read(delaySamples);   // any delay from 1 to the max, as often as you like
//
// write() moves time forward, so read(1) right after it is the sample written the call before.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jclap::dsp {

class DelayLine {
public:
   // Room for delays up to `samples` long. Allocates and clears, so call it outside the audio thread.
   void setMaxDelay(int samples) {
      size_t size = 4;
      while (size < static_cast<size_t>(samples) + 4)
         size <<= 1;
      buffer_.assign(size, 0.0f);
      mask_ = size - 1;
      pos_ = 0;
   }
   int maxDelay() const { return static_cast<int>(buffer_.size()) - 4; }

   void reset() {
      std::fill(buffer_.begin(), buffer_.end(), 0.0f);
      pos_ = 0;
   }

   void write(float x) {
      pos_ = (pos_ + 1) & mask_;
      buffer_[pos_] = x;
   }

   // The sample written `delay` writes ago, for a whole number of samples.
   float tap(int delay) const { return buffer_[(pos_ - static_cast<size_t>(delay)) & mask_]; }

   // Between samples by Hermite interpolation; clamped to 1..maxDelay().
   float read(float delay) const {
      delay = std::clamp(delay, 1.0f, static_cast<float>(maxDelay()));
      const int whole = static_cast<int>(delay);
      const float t = delay - static_cast<float>(whole);
      const float y0 = tap(whole - 1), y1 = tap(whole), y2 = tap(whole + 1), y3 = tap(whole + 2);
      const float c1 = 0.5f * (y2 - y0);
      const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
      const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
      return ((c3 * t + c2) * t + c1) * t + y1;
   }

private:
   std::vector<float> buffer_ = std::vector<float>(4, 0.0f);
   size_t mask_ = 3, pos_ = 0;
};

} // namespace jclap::dsp

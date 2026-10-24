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
   // Room for delays up to `samples` long (at least 1). Allocates and clears, so call it outside the
   // audio thread.
   void setMaxDelay(int samples) {
      samples = std::max(samples, 1);
      size_t size = 4;
      while (size < static_cast<size_t>(samples) + 4)
         size <<= 1;
      buffer_.assign(size, 0.0f);
      mask_ = size - 1;
      pos_ = 0;
      written_ = size;
   }
   int maxDelay() const { return static_cast<int>(buffer_.size()) - 4; }

   // Silence, at once whatever the length: nothing is cleared, but samples from before the reset read
   // as zero until they've been written over. Safe on the audio thread, where clearing a long line
   // could take longer than a block.
   void reset() {
      pos_ = 0;
      written_ = 0;
   }

   void write(float x) {
      pos_ = (pos_ + 1) & mask_;
      buffer_[pos_] = x;
      if (written_ <= mask_)
         ++written_;
   }

   // The sample written `delay` writes ago, for a whole number of samples (0 is the latest).
   float tap(int delay) const {
      const auto d = static_cast<size_t>(delay);
      return d < written_ ? buffer_[(pos_ - d) & mask_] : 0.0f;
   }

   // Between samples by Hermite interpolation; clamped to 1..maxDelay(), and NaN reads as 1.
   float read(float delay) const {
      if (!(delay >= 1.0f))
         delay = 1.0f;
      delay = std::min(delay, static_cast<float>(std::max(1, maxDelay())));
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
   // Samples written since the last reset, up to the buffer's size: older ones read as zero.
   size_t written_ = 4;
};

} // namespace jclap::dsp

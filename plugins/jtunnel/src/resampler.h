// A stereo windowed-sinc resampler whose ratio can change every block. The receiver uses it to
// follow the sender's clock (a ratio a few hundred ppm from 1) and to convert between sample rates;
// the sender uses it to bring any host rate to Opus's 48 kHz. No allocation after construction.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jtunnel {

class Resampler {
public:
   static constexpr int kHalfTaps = 16;
   static constexpr int kTaps = 2 * kHalfTaps;
   static constexpr int kPhases = 256;
   static constexpr int kCapacity = 1 << 14;

   Resampler() : table_((kPhases + 1) * kTaps), left_(kCapacity), right_(kCapacity) {
      setCutoff(0.92);
      reset();
   }

   // Builds the filter for a ratio (input samples per output sample). Cut off just under the
   // lower of the two Nyquist frequencies. Fast enough for a format change, not for every block.
   void configureFor(double ratio) { setCutoff(0.92 * std::min(1.0, 1.0 / ratio)); }

   // The cutoff as a fraction of the input's Nyquist frequency.
   void setCutoff(double cutoff) {
      cutoff_ = cutoff;
      for (int p = 0; p <= kPhases; ++p) {
         const double frac = static_cast<double>(p) / kPhases;
         double sum = 0.0;
         float *row = &table_[static_cast<size_t>(p) * kTaps];
         for (int k = 0; k < kTaps; ++k) {
            const double t = k - kHalfTaps + 1 - frac;
            const double x = t * cutoff;
            const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = kaiser(t / kHalfTaps);
            row[k] = static_cast<float>(sinc * w);
            sum += sinc * w;
         }
         // Unity gain at DC for every phase, so a fractional position never wobbles the level.
         for (int k = 0; k < kTaps; ++k)
            row[k] = static_cast<float>(row[k] / sum);
      }
   }
   double cutoff() const { return cutoff_; }

   // Empties it. The first output is the first input pushed after this, kHalfTaps samples later.
   void reset() {
      std::fill(left_.begin(), left_.end(), 0.0f);
      std::fill(right_.begin(), right_.end(), 0.0f);
      // Zeros before the start so the filter has history to read.
      pushed_ = kHalfTaps;
      position_ = static_cast<double>(kHalfTaps) - 1.0;
   }

   // Input samples not yet passed by the read position.
   double buffered() const { return static_cast<double>(pushed_) - position_; }
   int space() const { return kCapacity - static_cast<int>(pushed_ - first()) - 1; }
   uint64_t pushed() const { return pushed_; }
   // The input index (absolute, counting from reset) that the next output sample is taken at.
   double position() const { return position_; }

   void pushInterleaved(const float *interleaved, int n) {
      n = std::min(n, space());
      for (int i = 0; i < n; ++i) {
         const size_t at = static_cast<size_t>(pushed_ & (kCapacity - 1));
         left_[at] = interleaved[2 * i];
         right_[at] = interleaved[2 * i + 1];
         ++pushed_;
      }
   }

   void push(const float *l, const float *r, int n) {
      n = std::min(n, space());
      for (int i = 0; i < n; ++i) {
         const size_t at = static_cast<size_t>(pushed_ & (kCapacity - 1));
         left_[at] = l[i];
         right_[at] = r[i];
         ++pushed_;
      }
   }

   // How many more input samples it needs before produce(n, ratio) can run (0 if none).
   int needed(int n, double ratio) const {
      if (n <= 0)
         return 0;
      const double last = position_ + (n - 1) * ratio;
      const int64_t want = static_cast<int64_t>(std::floor(last)) + kHalfTaps + 1;
      const int64_t more = want - static_cast<int64_t>(pushed_);
      return more > 0 ? static_cast<int>(more) : 0;
   }

   // How many outputs the input buffered so far allows.
   int producible(double ratio) const {
      const double room = static_cast<double>(pushed_) - kHalfTaps - 1 - position_;
      return room < 0 ? 0 : static_cast<int>(std::floor(room / ratio)) + 1;
   }

   // Writes n output samples, stepping `ratio` input samples for each. needed() must be 0.
   void produce(float *l, float *r, int n, double ratio) {
      for (int j = 0; j < n; ++j) {
         const double fl = std::floor(position_);
         const int64_t base = static_cast<int64_t>(fl) - kHalfTaps + 1;
         const double phase = (position_ - fl) * kPhases;
         const int p = std::min(static_cast<int>(phase), kPhases - 1);
         const float mix = static_cast<float>(phase - p);
         const float *row0 = &table_[static_cast<size_t>(p) * kTaps];
         const float *row1 = row0 + kTaps;
         float sl = 0.0f, sr = 0.0f;
         for (int k = 0; k < kTaps; ++k) {
            const float c = row0[k] + (row1[k] - row0[k]) * mix;
            const size_t at = static_cast<size_t>(static_cast<uint64_t>(base + k) & (kCapacity - 1));
            sl += left_[at] * c;
            sr += right_[at] * c;
         }
         l[j] = sl;
         r[j] = sr;
         position_ += ratio;
      }
   }

private:
   static constexpr double kPi = 3.14159265358979323846;

   // The oldest sample a future output can still read.
   uint64_t first() const {
      const int64_t oldest = static_cast<int64_t>(std::floor(position_)) - kHalfTaps + 1;
      return oldest > 0 ? static_cast<uint64_t>(oldest) : 0;
   }

   static double besselI0(double x) {
      double sum = 1.0, term = 1.0;
      for (int k = 1; k < 30; ++k) {
         term *= (x / (2.0 * k)) * (x / (2.0 * k));
         sum += term;
      }
      return sum;
   }

   // Kaiser window, beta 8.6: about 90 dB of stopband from 32 taps.
   static double kaiser(double x) {
      if (x <= -1.0 || x >= 1.0)
         return 0.0;
      constexpr double kBeta = 8.6;
      return besselI0(kBeta * std::sqrt(1.0 - x * x)) / besselI0(kBeta);
   }

   std::vector<float> table_;
   std::vector<float> left_, right_;
   uint64_t pushed_ = 0;
   double position_ = 0.0;
   double cutoff_ = 0.92;
};

} // namespace jtunnel

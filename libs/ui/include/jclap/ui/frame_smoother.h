#pragma once

// Shows frames the audio thread publishes as smooth motion.
//
// The audio thread publishes a frame (the bodies, the flock) as it processes a block, and the host
// processes blocks when it likes: one every 10 or 20 ms with a big buffer, often two at once and
// then none. A panel that draws whatever is newest at each screen refresh shows the motion in those
// steps: some refreshes move twice as far, some not at all.
//
// A FrameSmoother keeps the last few frames, each stamped with how far into the audio it was taken,
// and runs a clock of its own at the display's pace, about a block behind the newest. Each refresh
// then falls between two frames it already has, and the panel draws partway between them. When the
// stamps don't move (the audio has stopped, or a host that doesn't stamp them), it shows the newest.

#include <algorithm>
#include <array>
#include <utility>

namespace jclap::ui {

template <typename Frame, int Capacity = 8>
class FrameSmoother {
public:
   // A frame just read, `time` seconds into the audio. One no newer than the newest replaces it.
   void push(const Frame &frame, double time) {
      if (count_ > 0) {
         const double newest = times_[index(count_ - 1)];
         // The stream started again (or jumped back): start over from here.
         if (time < newest - kSnap)
            clear();
         else if (time <= newest) {
            frames_[index(count_ - 1)] = frame;
            return;
         }
      }
      if (count_ > 0) {
         // How long the frames take to come: the clock runs that far behind, so it reaches the newest
         // frame only as the next one comes. That is the audio between them, or the wall time when the
         // host runs blocks in bursts, but not the wait after the audio stopped for a while.
         const double audio = time - times_[index(count_ - 1)];
         const double gap = std::min(std::max(audio, sinceNewest_), 2.0 * audio + 0.005);
         peak_ = std::max(peak_, std::min(gap, kSnap));
      }
      if (count_ == Capacity) {
         first_ = (first_ + 1) % Capacity;
         --count_;
      }
      frames_[index(count_)] = frame;
      times_[index(count_)] = time;
      ++count_;
      sinceNewest_ = 0.0;
   }

   // Moves the clock on `dt` seconds, gently pulled to its place behind the newest frame.
   void advance(float dt) {
      if (count_ == 0)
         return;
      sinceNewest_ += dt;
      // The lag eases back down (by 10 ms a second) when the blocks come more often again.
      peak_ = std::max(0.0, peak_ - 0.01 * dt);
      const double newest = times_[index(count_ - 1)];
      const double target = newest + sinceNewest_ - (peak_ + kMargin);
      clock_ += dt;
      if (!started_ || clock_ < target - kSnap || clock_ > target + kSnap)
         clock_ = target;
      else
         clock_ += (target - clock_) * std::min(1.0, 1.5 * dt);
      started_ = true;
      clock_ = std::clamp(clock_, times_[index(0)], newest);
   }

   void clear() {
      count_ = first_ = 0;
      peak_ = sinceNewest_ = 0.0;
      started_ = false;
   }

   bool empty() const { return count_ == 0; }
   const Frame &newest() const { return frames_[index(count_ - 1)]; }

   // The frames either side of the clock, and how far it is from `older` to `newer` (0..1). Both are
   // the newest when the clock has caught up with it. Not to be called while empty().
   const Frame &older() const { return frames_[index(around().first)]; }
   const Frame &newer() const { return frames_[index(around().second)]; }
   float mix() const {
      const auto [a, b] = around();
      const double ta = times_[index(a)], tb = times_[index(b)];
      return tb > ta ? static_cast<float>(std::clamp((clock_ - ta) / (tb - ta), 0.0, 1.0)) : 1.0f;
   }

private:
   // Snap the clock (or start over) when it is this far from where it should be, in seconds.
   static constexpr double kSnap = 0.25;
   // Behind the slowest the frames have lately come, a little more, so jitter doesn't catch it up.
   static constexpr double kMargin = 0.004;

   int index(int i) const { return (first_ + i) % Capacity; }
   std::pair<int, int> around() const {
      for (int i = count_ - 1; i > 0; --i)
         if (times_[index(i - 1)] <= clock_)
            return {i - 1, i};
      return {0, std::min(1, count_ - 1)};
   }

   std::array<Frame, Capacity> frames_{};
   std::array<double, Capacity> times_{};
   int first_ = 0, count_ = 0;
   double clock_ = 0.0, peak_ = 0.0, sinceNewest_ = 0.0;
   bool started_ = false;
};

} // namespace jclap::ui

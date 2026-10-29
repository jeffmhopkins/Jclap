#pragma once

// Paces an animation to the display.
//
// Visage checks its timers only when the window draws, once a screen refresh, and counts in whole
// milliseconds. A 16 ms timer then misses every refresh that comes even a little early (and one that
// comes late makes the next one early), so the animation holds a frame and then jumps two. Started at
// 1 ms instead, the timer is checked at every refresh, and a FramePacer decides which refreshes step
// the animation: every one on a 60 Hz screen, every other one at 120 or 144 Hz. So it steps at a
// steady rate that divides the screen's, at least about 60 a second, without beating against it.
//
//    startTimer(1);
//    void timerCallback() override {
//       if (const float dt = pacer_.tick())
//          step(dt);
//    }

#include <algorithm>
#include <chrono>

namespace jclap::ui {

class FramePacer {
public:
   explicit FramePacer(float framesPerSecond = 60.0f) : period_(1.0 / framesPerSecond) {}

   // Seconds since the last step if this refresh should step (at most a tenth of a second, so a
   // window that was hidden doesn't leap), otherwise 0.
   float tick() { return tick(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

   // The same, at `now` seconds on any steady clock (for tests).
   float tick(double now) {
      if (last_ < 0.0) {
         last_ = lastStep_ = now;
         return static_cast<float>(period_);
      }
      // How often the refreshes come, smoothed over a few of them.
      const double gap = std::clamp(now - last_, 0.001, 0.05);
      last_ = now;
      refresh_ = refresh_ <= 0.0 ? gap : refresh_ + (gap - refresh_) * 0.1;
      // Step every `stride` refreshes: as many as fit in a period, rounding up only from three
      // quarters, so 90 and 100 Hz screens step on every refresh and 120 and 144 Hz on every other.
      // Counted in time, with half a refresh to spare, so one that comes early or late still counts.
      const int stride = std::max(1, static_cast<int>(period_ / refresh_ + 0.25));
      const double since = now - lastStep_;
      if (since < (stride - 0.5) * refresh_)
         return 0.0f;
      lastStep_ = now;
      return static_cast<float>(std::min(since, 0.1));
   }

private:
   double period_;
   double last_ = -1.0, lastStep_ = -1.0, refresh_ = 0.0;
};

} // namespace jclap::ui

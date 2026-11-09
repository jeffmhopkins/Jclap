#pragma once

// The DELAY tab's echo: a plain stereo delay after the body and before the reverb. Its time is set
// in ms, or synced to the host's tempo as a note value. Feedback sets how many repeats there are,
// and Tone how much darker each repeat gets. Spread sends the repeats back and forth between the
// sides (ping-pong) instead of straight down each one.

#include <jclap/dsp/delay_line.h>

#include <algorithm>
#include <cmath>

namespace jbreath {

struct DelaySettings {
   bool on = false;
   double mix = 0.3, timeMs = 375.0, feedback = 0.4, tone = 0.72, spread = 0.5;
};

// The Delay Sync choices, as beats (quarter notes); 0 is free (ms).
inline const char *const kDelaySyncNames[] = {"Free", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
                                              "1/8D", "1/4T", "1/4",  "1/4D",  "1/2",  "1/2D", "1/1"};
constexpr int kDelaySyncCount = static_cast<int>(sizeof(kDelaySyncNames) / sizeof(kDelaySyncNames[0]));
inline double delaySyncBeats(int sync) {
   static const double beats[kDelaySyncCount] = {0.0,   0.125, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5,
                                                 0.75,  2.0 / 3, 1.0,  1.5,   2.0,     3.0,   4.0};
   return beats[std::clamp(sync, 0, kDelaySyncCount - 1)];
}
// The Delay Time knob, 0..1, to ms: 10 ms to 2 s on a log scale.
inline double delayTimeMs(double v) { return 10.0 * std::pow(200.0, std::clamp(v, 0.0, 1.0)); }

class StereoDelay {
public:
   static constexpr double kMaxSeconds = 4.0;
   void prepare(double fs) {
      fs_ = fs;
      for (auto &l : lines_)
         l.setMaxDelay(static_cast<int>(fs * kMaxSeconds) + 8);
      reset();
   }
   void reset() {
      for (auto &l : lines_)
         l.reset();
      lp_[0] = lp_[1] = 0.0f;
      wet_ = 0.0f;
      delay_ = target_;
   }
   void set(const DelaySettings &s) {
      settings_ = s;
      target_ = std::clamp(s.timeMs * 0.001 * fs_, 2.0, kMaxSeconds * fs_);
      // Tone: the repeats' lowpass, from 400 Hz (a muffled, distant echo) to open.
      const double hz = 400.0 * std::pow(48.0, std::clamp(s.tone, 0.0, 1.0));
      lpCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * 3.14159265358979 * std::min(hz, 0.45 * fs_) / fs_));
   }
   // Whether there is anything to hear: on, or still ringing after being turned off.
   bool active() const { return settings_.on || wet_ > 1e-4f; }
   // How long the repeats ring after the input stops, to -80 dB.
   double tailSeconds() const {
      const double fb = std::clamp(settings_.feedback, 0.0, 0.95);
      const double repeats = fb > 1e-3 ? std::log(1e-4) / std::log(fb) : 1.0;
      return settings_.timeMs * 0.001 * (repeats + 1.0);
   }

   void tick(float &l, float &r) {
      // The level fades in and out, so turning it on or off doesn't click.
      wet_ += ((settings_.on ? 1.0f : 0.0f) - wet_) * 0.0005f;
      if (wet_ < 1e-5f && !settings_.on)
         return;
      // A time that moves glides there (a tape delay's pitch bend), never jumps.
      delay_ += (target_ - delay_) * 0.0002;
      const float fb = static_cast<float>(std::clamp(settings_.feedback, 0.0, 0.95));
      const float spread = static_cast<float>(std::clamp(settings_.spread, 0.0, 1.0));
      float a = lines_[0].read(delay_), b = lines_[1].read(delay_);
      lp_[0] += (a - lp_[0]) * lpCoef_;
      lp_[1] += (b - lp_[1]) * lpCoef_;
      a = lp_[0];
      b = lp_[1];
      // Ping-pong: the mono input goes in on one side, and each side's repeats feed the other.
      const float mono = 0.5f * (l + r);
      const float inL = (1.0f - spread) * l + spread * mono, inR = (1.0f - spread) * r;
      const float backL = (1.0f - spread) * a + spread * b, backR = (1.0f - spread) * b + spread * a;
      lines_[0].write(inL + softClip(backL * fb));
      lines_[1].write(inR + softClip(backR * fb));
      const float mix = static_cast<float>(std::clamp(settings_.mix, 0.0, 1.0)) * wet_;
      l += a * mix;
      r += b * mix;
   }

private:
   static float softClip(float x) { return x / (1.0f + std::fabs(x) * 0.25f); }
   double fs_ = 48000.0, delay_ = 1000.0, target_ = 1000.0;
   DelaySettings settings_;
   jclap::dsp::DelayLine lines_[2];
   float lp_[2] = {0.0f, 0.0f}, lpCoef_ = 0.5f, wet_ = 0.0f;
};

} // namespace jbreath

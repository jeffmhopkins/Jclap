#pragma once

// JVerb's audio: input filters, predelay and early reflections, diffusion and attack shaping, a
// sixteen-line feedback delay network with an allpass in every line, an outer loop (drive, shimmer, smear, tilt and a
// safety limiter), a long echo that can sit before the tank, in the loop or after it, and the
// output stage. The orbit sim lives in the plugin; it hands this engine each body's position, tone
// and phase once per control tick (setControl), and process() runs per sample.
//
// Nothing here allocates after setSampleRate(), takes a lock, or calls the OS.

#include <jclap/dsp/delay_line.h>
#include <jclap/dsp/filter.h>
#include <jclap/dsp/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "params.h"

namespace jverb {

constexpr int kLines = 16;
// Samples per control tick: params, the orbit and every derived coefficient update this often, and
// gains ramp across it.
constexpr int kControlTick = 32;

// Everything the engine needs for one control tick, in plain units, with host modulation and the
// bodies' routing already added.
struct Control {
   double decay = 4.0, size = 1.0, motion = 0.4, attack = 0.0, diffusion = 0.7, earlyLate = 0.75;
   double predelayMs = 20.0, dampHigh = 7000.0, bassMult = 1.2, bassXover = 250.0, lowCut = 40.0, highCut = 16000.0;
   double feedback = 0.0, shimmer = 0.0, shimmerPitch = 12.0, tilt = 0.0, smear = 0.3;
   bool safetyHard = false, freeze = false;
   bool driveOn = false;
   DriveWhere driveWhere = DriveWhere::Loop;
   DriveType driveType = DriveType::Tube;
   double drive = 12.0, driveTone = 0.0, driveBias = 0.2, driveMix = 1.0, driveTrim = 0.0;
   bool echoOn = false, echoPingPong = true;
   EchoWhere echoWhere = EchoWhere::Into;
   double echoSeconds = 0.375, echoFeedback = 0.45, echoTone = -0.2, echoMix = 0.5;
   double width = 1.0, duck = 0.0, mix = 0.35, levelDb = 0.0;
   // Bodies: how many are in the system, and each one's left/right position (-1..1). The tank's
   // lines are shared out among them in pairs, and each body is heard as its own voice in the tail:
   //  - panDepth (0..2): how far each body's voice narrows to its own band of the spectrum and
   //    moves from a wide spread to where the body is. Past 1 the voices are thrown to the edges
   //    and their stereo turns inside out.
   //  - tone (-2 darkest..2 brightest): its voice's colour and level, and its lines' damping. Past
   //    +-1 the cuts and boosts get extreme.
   //  - phase (-1..1): where its phaser sweep sits; as it moves, the voice swirls.
   //  - swirlDepth (0..2): how deep every voice's phaser goes. Past 1 it feeds back and rings.
   // And motion (0..2): up to 0.8 the tank's drift bends a pass by at most 4 cents; past it, it grows
   // to about 30 cents at 1 and a seasick 150 cents at 2, and runs up to four times faster.
   int bodies = 3;
   double pan[kMaxBodies] = {};
   double panDepth = 0.0, swirlDepth = 0.0;
   double tone[kMaxBodies] = {};
   double phase[kMaxBodies] = {};
};

namespace detail {

// A value that steps linearly to each new target over one control tick, so gains never zipper.
struct Ramp {
   float value = 0.0f, step = 0.0f;
   int left = 0;
   void set(float target) {
      step = (target - value) / kControlTick;
      left = kControlTick;
   }
   void jump(float target) {
      value = target;
      left = 0;
   }
   // Lands on the target now: after a reset, so a restarted instance starts where a fresh one does.
   void settle() {
      value += step * static_cast<float>(left);
      left = 0;
   }
   float next() {
      if (left > 0) {
         value += step;
         --left;
      }
      return value;
   }
};

// Leaves the tank's signal untouched below 2 and bends it softly toward 3 above, a last guard so
// that nothing (a host sending full-scale square waves into Freeze, say) can make the lines run away.
inline float tankLimit(float x) {
   const float m = std::fabs(x);
   return m < 2.0f ? x : std::copysign(2.0f + std::tanh(m - 2.0f), x);
}

// The wet signal's last guard: untouched up to 1, then bending softly toward 1.2.
inline float wetGuard(float x) {
   const float m = std::fabs(x);
   return m < 1.0f ? x : std::copysign(1.0f + 0.2f * std::tanh((m - 1.0f) / 0.2f), x);
}

struct OnePole {
   float a = 1.0f, y = 0.0f;
   void setCutoff(double hz, double sampleRate) {
      a = static_cast<float>(1.0 - std::exp(-2.0 * jclap::dsp::kPi * std::min(hz, 0.45 * sampleRate) / sampleRate));
   }
   float lowpass(float x) { return y += a * (x - y); }
   float highpass(float x) { return x - lowpass(x); }
};

// A Schroeder allpass around a DelayLine. Its length glides toward `target` a little every sample
// (`glide` per sample, set with the sample rate), so a length that changes each control tick
// bends smoothly instead of stepping. With `ease` below 1 the glide also starts gently: the length
// follows `aim`, which itself eases toward the target, so the read speed never jumps either.
struct Allpass {
   jclap::dsp::DelayLine line;
   float length = 100.0f, aim = 100.0f, target = 100.0f, gain = 0.5f, glide = 0.001f, ease = 1.0f;
   float process(float x) {
      aim += ease * (target - aim);
      length += glide * (aim - length);
      const float d = line.read(length);
      const float w = x + gain * d;
      line.write(w);
      return d - gain * w;
   }
};

// A slow sine that drifts a tank line's length, so a long tail blurs instead of ringing. Its depth is
// set in cents of pitch bend per pass, not in samples, so however fast it runs it never bends a pass
// by more than that. A rotating phasor, renormalised each control tick, saves a sin() per sample.
struct Drift {
   float x = 1.0f, y = 0.0f, cw = 1.0f, sw = 0.0f;
   Ramp depth;
   void start(double phase) {
      x = static_cast<float>(std::cos(2.0 * jclap::dsp::kPi * phase));
      y = static_cast<float>(std::sin(2.0 * jclap::dsp::kPi * phase));
   }
   // `hz` of drift that bends a pass by at most `cents`, but never swings further than `most` samples.
   // `speed` (0..1) slows the swing without changing its depth: at 0 the read point holds still.
   void set(double hz, double cents, double sampleRate, double most, double speed = 1.0) {
      const double w = 2.0 * jclap::dsp::kPi * hz / sampleRate;
      cw = static_cast<float>(std::cos(w * speed));
      sw = static_cast<float>(std::sin(w * speed));
      // A delay swinging by A samples at w radians per sample bends pitch by up to a factor 1 + A w.
      depth.set(static_cast<float>(std::min(most, (std::exp2(cents / 1200.0) - 1.0) / w)));
      const float r = 1.0f / std::sqrt(x * x + y * y);
      x *= r;
      y *= r;
   }
   // Where the swing is now, in samples.
   float value() const { return depth.value * y; }
   float next() {
      const float nx = x * cw - y * sw;
      y = x * sw + y * cw;
      x = nx;
      return depth.next() * y;
   }
};

// A delay-line pitch shifter: two read heads sweep a short window, crossfaded with Hann windows that
// sum to one.
struct PitchShifter {
   jclap::dsp::DelayLine line;
   double phase = 0.0;
   float window = 2400.0f;
   float process(float x, double ratio) {
      line.write(x);
      phase += (1.0 - ratio) / window;
      phase -= std::floor(phase);
      float y = 0.0f;
      for (int head = 0; head < 2; ++head) {
         double p = phase + 0.5 * head;
         p -= std::floor(p);
         const float w = static_cast<float>(std::sin(jclap::dsp::kPi * p));
         y += w * w * line.read(1.0f + static_cast<float>(p) * window);
      }
      return y;
   }
};

// Plays the input backwards in overlapping windows, for reverse swells.
struct Reverser {
   jclap::dsp::DelayLine line;
   double phase = 0.0;
   float process(float x, float window) {
      line.write(x);
      phase += 1.0 / window;
      phase -= std::floor(phase);
      float y = 0.0f;
      for (int head = 0; head < 2; ++head) {
         double p = phase + 0.5 * head;
         p -= std::floor(p);
         const float w = static_cast<float>(std::sin(jclap::dsp::kPi * p));
         y += w * w * line.read(1.0f + 2.0f * static_cast<float>(p) * window);
      }
      return y;
   }
};

// 2x oversampling with a 31-tap half-band lowpass (Blackman-windowed sinc), up and down.
class Oversampler {
public:
   Oversampler() {
      constexpr int n = kTaps;
      double sum = 0.0;
      for (int i = 0; i < n; ++i) {
         const double m = i - (n - 1) / 2.0;
         const double sinc = m == 0.0 ? 0.5 : std::sin(jclap::dsp::kPi * 0.5 * m) / (jclap::dsp::kPi * m);
         const double t = 2.0 * jclap::dsp::kPi * i / (n - 1);
         const double window = 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2.0 * t);
         kernel_[i] = static_cast<float>(sinc * window);
         sum += kernel_[i];
      }
      for (float &k : kernel_)
         k = static_cast<float>(k / sum);
   }
   void reset() {
      up_.fill(0.0f);
      down_.fill(0.0f);
   }
   // One input sample in, two oversampled out.
   void upsample(float x, float &a, float &b) {
      a = filter(up_, upPos_, 2.0f * x);
      b = filter(up_, upPos_, 0.0f);
   }
   // Two oversampled samples in, one out.
   float downsample(float a, float b) {
      filter(down_, downPos_, a);
      return filter(down_, downPos_, b);
   }

   static constexpr int kTaps = 31;
   // How late the round trip comes out, in input samples: each filter is (kTaps - 1) / 2 samples
   // at the 2x rate.
   static constexpr int kLatency = (kTaps - 1) / 2;

private:
   float filter(std::array<float, 2 * kTaps> &hist, int &pos, float x) {
      pos = (pos + 1) % kTaps;
      hist[pos] = hist[pos + kTaps] = x;
      float y = 0.0f;
      const float *h = &hist[pos + 1];
      for (int i = 0; i < kTaps; ++i)
         y += kernel_[i] * h[i];
      return y;
   }
   std::array<float, kTaps> kernel_{};
   std::array<float, 2 * kTaps> up_{}, down_{};
   int upPos_ = 0, downPos_ = 0;
};

// The drive: tone emphasis, gain, a waveshaper at 2x, de-emphasis, DC blocker, trim and its own mix.
class Drive {
public:
   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      emph_.setCutoff(900.0, sampleRate);
      deemph_.setCutoff(900.0, sampleRate);
      dc_.setCutoff(12.0, sampleRate);
   }
   void reset() {
      os_.reset();
      dryLine_.fill(0.0f);
      emph_.y = deemph_.y = dc_.y = 0.0f;
      held_ = 0.0f;
      holdCount_ = 0;
      first_ = true;
   }
   // The shaper and the slot it's in. The engine fades the drive out before it calls this with a
   // change, and back in after, so the switch is never heard as a step.
   void setMode(DriveType type, bool inLoop) {
      type_ = type;
      inLoop_ = inLoop;
   }
   void set(const Control &c) {
      gain_.set(static_cast<float>(jclap::dsp::dbToGain(c.drive)));
      // In the loop, up to 100% Feedback the makeup undoes all the drive's gain (see process). Past it,
      // it undoes less and less (as a power of the gain, 1 to 0), until at 150% it undoes none, so the
      // drive pushes the loop into the shaper and it screams instead of choking.
      makeup_.set(static_cast<float>(1.0 - std::clamp((c.feedback - 1.0) / 0.5, 0.0, 1.0)));
      // Tone's emphasis: up to +-50%, a straight 1 -+ 0.75 Tone on the lows and 1 +- 0.75 Tone on the
      // highs; past it, on in even dB steps to 6 dB each way at the ends, rather than the lows
      // dropping toward nothing (and the de-emphasis lifting them back up to 12 dB).
      const double t = std::clamp(c.driveTone, -1.0, 1.0), past = std::max(0.0, std::fabs(t) - 0.5);
      const double lift = past > 0.0 ? 1.375 * jclap::dsp::dbToGain(6.47 * past) : 1.0 + 0.75 * std::fabs(t);
      const double cut = past > 0.0 ? 0.625 * jclap::dsp::dbToGain(-3.84 * past) : 1.0 - 0.75 * std::fabs(t);
      emphLow_.set(static_cast<float>(t < 0.0 ? lift : cut));
      emphHigh_.set(static_cast<float>(t < 0.0 ? cut : lift));
      bias_.set(static_cast<float>(c.driveBias));
      mix_.set(static_cast<float>(c.driveMix));
      trim_.set(static_cast<float>(jclap::dsp::dbToGain(c.driveTrim)));
      if (first_) {
         first_ = false;
         for (Ramp *r : {&gain_, &emphLow_, &emphHigh_, &bias_, &mix_, &trim_, &makeup_})
            r->settle();
      }
   }
   float process(float x) {
      const float gain = gain_.next(), low = emphLow_.next(), high = emphHigh_.next(), bias = bias_.next(), mix = mix_.next(),
                  trim = trim_.next();
      const float makeup = makeup_.next();
      // Tone tilts the spectrum into the shaper and back out, so it changes what distorts, not the
      // overall balance.
      const float lo = emph_.lowpass(x);
      float v = (lo * low + (x - lo) * high) * gain;
      // The dry side of the mix, delayed to line up with the oversampled path, or Mix below 100%
      // comb-filters. Crush doesn't oversample, so it mixes with the undelayed input.
      dryPos_ = (dryPos_ + 1) % Oversampler::kLatency;
      const float dryLate = dryLine_[dryPos_];
      dryLine_[dryPos_] = x;
      float y, dry = dryLate;
      if (type_ == DriveType::Crush) {
         dry = x;
         // Fewer bits as the drive rises, and a sample-and-hold whose rate falls with the bias.
         const float bits = 12.0f - 9.0f * std::min(1.0f, (gain - 1.0f) / 60.0f);
         const float steps = std::exp2(bits);
         if (holdCount_-- <= 0) {
            held_ = std::round(std::clamp(v, -1.0f, 1.0f) * steps) / steps;
            holdCount_ = static_cast<int>(bias * 15.0f);
         }
         y = held_;
      }
      else {
         float a, b;
         os_.upsample(v, a, b);
         y = os_.downsample(shape(a, bias), shape(b, bias));
      }
      y = dc_.highpass(y);
      const float lo2 = deemph_.lowpass(y);
      y = lo2 / low + (y - lo2) / high;
      // Most shapes come out near full scale whatever the drive, so bring them back to the level
      // that went in before the trim. In the loop, quiet signals must not come out louder than they
      // went in, or the loop would grow below 100% feedback: there the makeup undoes the whole
      // small-signal gain (drive times the shape's slope at rest), and the trim can only cut. Past
      // 100% Feedback the makeup leaves some of the drive's gain in (see set).
      if (inLoop_)
         y *= std::min(1.0f, trim) / (std::pow(std::max(1.0f, gain), makeup) * slope(bias));
      else
         y *= trim / std::sqrt(std::max(1.0f, gain));
      return dry + mix * (y - dry);
   }

private:
   // How steep shape() is around its resting point: the gain it gives a quiet signal.
   float slope(float bias) const {
      switch (type_) {
      case DriveType::Tube: {
         const float t = std::tanh(0.6f * bias);
         return std::max(0.05f, 1.0f - t * t);
      }
      case DriveType::Fuzz:
         return 2.5f * std::exp(-2.5f * 0.4f * bias);
      case DriveType::Fold:
         return 1.5707963f * (1.0f + bias);
      default:
         return 1.0f;
      }
   }

   float shape(float x, float bias) const {
      switch (type_) {
      case DriveType::Tape:
         return std::tanh(x);
      case DriveType::Tube: {
         const float b = 0.6f * bias;
         return std::tanh(x + b) - std::tanh(b);
      }
      case DriveType::Fuzz: {
         const float z = x + 0.4f * bias;
         return z >= 0.0f ? 1.0f - std::exp(-2.5f * z) : -1.0f + std::exp(2.5f * z);
      }
      case DriveType::Fold:
         return std::sin(1.5707963f * x * (1.0f + bias));
      default:
         return x;
      }
   }

   double sampleRate_ = 48000.0;
   DriveType type_ = DriveType::Tube;
   bool inLoop_ = true, first_ = true;
   Oversampler os_;
   std::array<float, Oversampler::kLatency> dryLine_ = {};
   int dryPos_ = 0;
   OnePole emph_, deemph_, dc_;
   Ramp gain_, emphLow_, emphHigh_, bias_, mix_, trim_, makeup_;
   float held_ = 0.0f;
   int holdCount_ = 0;
};

// One body's voice: the sum of its share of the tank's lines, on the way out. It narrows to the
// body's band of the spectrum, takes the body's tone, swirls through a four-stage phaser and is placed
// where the body is. With every depth at zero it passes its lines through unchanged, left and right.
class BodyVoice {
public:
   void setSampleRate(double sampleRate) {
      sr_ = sampleRate;
      glide_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.08 * sr_)));
      for (int ch = 0; ch < 2; ++ch)
         shelf_[ch].setCutoff(kShelfHz, sr_);
      reset();
   }
   void reset() {
      for (int ch = 0; ch < 2; ++ch) {
         for (OnePole &f : lo_[ch])
            f.y = 0.0f;
         for (OnePole &f : hi_[ch])
            f.y = 0.0f;
         shelf_[ch].y = 0.0f;
         for (int i = 0; i < kStages; ++i)
            apX_[ch][i] = apY_[ch][i] = 0.0f;
         last_[ch] = 0.0f;
      }
      first_ = true;
   }
   // The band runs from lowHz to highHz (0 leaves that side open). spread, swirl: 0..2; pan, sweep:
   // -1..1; tone: -2..2.
   void set(double lowHz, double highHz, double spread, double pan, double tone, double sweep, double swirl) {
      lowOpen_ = lowHz <= 0.0;
      highOpen_ = highHz <= 0.0;
      for (int ch = 0; ch < 2; ++ch)
         for (int i = 0; i < 2; ++i) {
            lo_[ch][i].setCutoff(lowOpen_ ? 20.0 : lowHz, sr_);
            hi_[ch][i].setCutoff(highOpen_ ? 20000.0 : highHz, sr_);
         }
      // Past full spread each voice is pushed further out, to the edges, and its own stereo inverts.
      const double over = std::max(0.0, spread - 1.0);
      const double angle = (std::clamp(spread * pan * (1.0 + 2.0 * over), -1.0, 1.0) + 1.0) * 0.25 * jclap::dsp::kPi;
      target_[kSpread] = static_cast<float>(std::min(1.0, spread));
      target_[kSide] = static_cast<float>(1.0 - spread);
      target_[kMidL] = static_cast<float>(std::sqrt(2.0) * std::cos(angle));
      target_[kMidR] = static_cast<float>(std::sqrt(2.0) * std::sin(angle));
      // Up to +-1, a 12 dB cut or 4 dB lift above the shelf and 3 dB of level; past it, up to 24 dB
      // of cut or 14 of lift.
      const double lift = 4.0 * tone + 6.0 * std::max(0.0, tone - 1.0);
      target_[kHigh] = static_cast<float>(jclap::dsp::dbToGain(tone < 0.0 ? 12.0 * tone : lift));
      target_[kLevel] = static_cast<float>(jclap::dsp::dbToGain(3.0 * std::clamp(tone, -1.0, 1.0)));
      // Past full swirl the phaser sweeps wider and feeds back on itself, up to a ringing 0.8.
      const double ring = std::max(0.0, swirl - 1.0);
      target_[kSweep] = static_cast<float>((kSweepDepth + 0.1 * ring) * std::clamp(sweep, -1.0, 1.0));
      // Past full swirl the phaser's share also grows from half to all of the voice.
      target_[kSwirl] = static_cast<float>(std::clamp(swirl, 0.0, 1.0) + ring);
      target_[kRing] = static_cast<float>(0.8 * ring);
      if (first_) {
         first_ = false;
         for (int i = 0; i < kGains; ++i)
            gain_[i] = target_[i];
      }
   }
   void process(float inL, float inR, float &outL, float &outR) {
      for (int i = 0; i < kGains; ++i)
         gain_[i] += glide_ * (target_[i] - gain_[i]);
      float x[2] = {inL, inR};
      for (int ch = 0; ch < 2; ++ch) {
         float v = x[ch];
         // The band: two one-poles at each edge, so it falls 12 dB an octave outside it.
         const float upper = highOpen_ ? v : hi_[ch][1].lowpass(hi_[ch][0].lowpass(v));
         const float lower = lowOpen_ ? 0.0f : lo_[ch][1].lowpass(lo_[ch][0].lowpass(v));
         v += gain_[kSpread] * (upper - lower - v);
         const float l = shelf_[ch].lowpass(v);
         v = (l + gain_[kHigh] * (v - l)) * gain_[kLevel];
         // The phaser, its two sides swept opposite ways.
         const float a = ch ? -gain_[kSweep] : gain_[kSweep];
         float ap = v + gain_[kRing] * last_[ch];
         for (int i = 0; i < kStages; ++i) {
            const float y = a * (ap - apY_[ch][i]) + apX_[ch][i];
            apX_[ch][i] = ap;
            apY_[ch][i] = y;
            ap = y;
         }
         last_[ch] = ap;
         // The feedback can lift a resonance by up to 1 / (1 - ring); scaling by 1 - 0.75 ring lets the
         // loudest one ring out at up to +6 dB (the wet signal's guard catches anything hotter).
         x[ch] = v + gain_[kSwirl] * 0.5f * ((1.0f - 0.75f * gain_[kRing]) * ap - v);
      }
      const float mid = 0.5f * (x[0] + x[1]), side = 0.5f * (x[0] - x[1]) * gain_[kSide];
      outL = side + mid * gain_[kMidL];
      outR = -side + mid * gain_[kMidR];
   }

private:
   static constexpr int kStages = 4;
   static constexpr double kShelfHz = 2500.0, kSweepDepth = 0.85;
   enum { kSpread, kSide, kMidL, kMidR, kHigh, kLevel, kSweep, kSwirl, kRing, kGains };
   double sr_ = 48000.0;
   float glide_ = 0.001f;
   bool first_ = true, lowOpen_ = true, highOpen_ = true;
   OnePole lo_[2][2], hi_[2][2], shelf_[2];
   float apX_[2][kStages] = {}, apY_[2][kStages] = {}, last_[2] = {};
   float gain_[kGains] = {}, target_[kGains] = {};
};

// A stereo delay up to kEchoMaxSeconds. Its time glides like a tape transport, so a change in time
// bends the pitch of the repeats rather than clicking. Each repeat goes
// through a tone tilt, a 60 Hz high-pass and a soft saturator, so feedback past 100% blooms into
// distortion instead of blowing up. Ping-pong crosses the feedback between channels.
class Echo {
public:
   void setSampleRate(double sampleRate) {
      sr_ = sampleRate;
      for (auto &l : line_)
         l.setMaxDelay(static_cast<int>(kEchoMaxSeconds * sr_) + 8);
      glide_ = 1.0 - std::exp(-1.0 / (0.12 * sr_));
      for (int ch = 0; ch < 2; ++ch) {
         hp_[ch].setCutoff(60.0, sr_);
         tone_[ch].setCutoff(1800.0, sr_);
      }
      reset();
   }
   void reset() {
      for (int ch = 0; ch < 2; ++ch) {
         line_[ch].reset();
         hp_[ch].y = tone_[ch].y = 0.0f;
         fb_[ch] = 0.0f;
      }
      first_ = true;
   }
   void set(const Control &c) {
      target_ = std::clamp(c.echoSeconds, 0.005, kEchoMaxSeconds - 0.01) * sr_;
      if (first_)
         time_ = target_;
      feedback_.set(static_cast<float>(c.echoFeedback));
      // Past +-50% each step goes twice as far: at the ends, -13.5 dB of lows and +4.5 dB of highs, or
      // -18 dB of highs.
      const double t = std::clamp(c.echoTone, -1.0, 1.0), past = std::max(0.0, std::fabs(t) - 0.5);
      low_.set(static_cast<float>(jclap::dsp::dbToGain(t < 0 ? 0.0 : -9.0 * (t + past))));
      high_.set(static_cast<float>(jclap::dsp::dbToGain(t < 0 ? 12.0 * (t - past) : 3.0 * (t + past))));
      pingPong_ = c.echoPingPong;
      if (first_) {
         first_ = false;
         for (Ramp *r : {&feedback_, &low_, &high_})
            r->settle();
      }
   }
   // Takes a stereo pair and returns only the repeats.
   void process(const float in[2], float out[2]) {
      time_ += glide_ * (target_ - time_);
      const float fb = feedback_.next(), lo = low_.next(), hi = high_.next();
      for (int ch = 0; ch < 2; ++ch)
         out[ch] = line_[ch].read(static_cast<float>(time_));
      for (int ch = 0; ch < 2; ++ch) {
         float v = out[ch];
         const float l = tone_[ch].lowpass(v);
         v = hp_[ch].highpass(l * lo + (v - l) * hi);
         fb_[ch] = std::tanh(v * fb);
      }
      if (pingPong_) {
         // Mono in on the left, then each repeat swaps sides.
         line_[0].write(0.5f * (in[0] + in[1]) + fb_[1]);
         line_[1].write(fb_[0]);
      }
      else
         for (int ch = 0; ch < 2; ++ch)
            line_[ch].write(in[ch] + fb_[ch]);
   }

private:
   double sr_ = 48000.0;
   jclap::dsp::DelayLine line_[2];
   OnePole hp_[2], tone_[2];
   float fb_[2] = {};
   // In double: a float glide toward a target of ~200,000 samples stalls a few hundred samples short.
   double time_ = 0.0, target_ = 0.0, glide_ = 0.001;
   bool first_ = true, pingPong_ = true;
   Ramp feedback_, low_, high_;
};

} // namespace detail

class Engine {
public:
   void setSampleRate(double sampleRate) {
      sr_ = sampleRate;
      const auto samples = [this](double seconds) { return static_cast<int>(seconds * sr_) + 8; };
      for (auto &l : predelay_)
         l.setMaxDelay(samples(1.0));
      for (int ch = 0; ch < 2; ++ch) {
         for (auto &ap : diffuser_[ch])
            ap.line.setMaxDelay(samples(0.03));
         for (auto &ap : bloom_[ch])
            ap.line.setMaxDelay(samples(0.5));
         for (auto &ap : smear_[ch])
            ap.line.setMaxDelay(samples(0.05));
         reverse_[ch].line.setMaxDelay(samples(2.5));
         shifter_[ch].line.setMaxDelay(samples(0.2));
         shifter_[ch].window = static_cast<float>(0.06 * sr_);
         drive_[ch].setSampleRate(sr_);
         lowCut_[ch].setSampleRate(sr_);
         lowCut_[ch].setMode(jclap::dsp::Filter::Mode::HighPass);
         highCut_[ch].setSampleRate(sr_);
         highCut_[ch].setMode(jclap::dsp::Filter::Mode::LowPass);
         loopHp_[ch].setCutoff(40.0, sr_);
      }
      // Half as long again, for the drift: it never swings past half a line's length.
      for (auto &line : lines_)
         line.setMaxDelay(samples(1.5 * kMaxLineSeconds));
      for (auto &line : tankAp_)
         line.setMaxDelay(samples(1.5 * kMaxTankApSeconds));
      for (auto &line : tankAp2_)
         line.setMaxDelay(samples(kMaxTankApSeconds));
      for (auto &v : voice_)
         v.setSampleRate(sr_);
      duckEnv_ = 0.0f;
      duckAttack_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sr_)));
      duckRelease_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.3 * sr_)));
      echo_.setSampleRate(sr_);
      lengthGlide_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.025 * sr_)));
      panGlide_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.08 * sr_)));
      busFadeStep_ = static_cast<float>(1.0 / (0.05 * sr_));
      slotStep_ = static_cast<float>(1.0 / (0.005 * sr_));
      freezeFadeStep_ = static_cast<float>(1.0 / (0.05 * sr_));
      // Predelay may move its read point by at most half a sample per sample, and the early size
      // its farthest tap by a quarter, so a big jump becomes a short, bounded pitch swoop.
      earlySlew_ = static_cast<float>(0.25 / (kEarlyMs[kEarlyTaps - 1] * 0.001 * sr_));
      // Every length that moves eases into its glide over a few ms: a glide that starts at full speed
      // bends the waveform at a corner, a softer click but still a click.
      easeGlide_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sr_)));
      for (int ch = 0; ch < 2; ++ch) {
         for (auto &ap : bloom_[ch]) {
            ap.glide = lengthGlide_;
            ap.ease = easeGlide_;
         }
         for (auto &ap : smear_[ch]) {
            ap.glide = lengthGlide_;
            ap.ease = easeGlide_;
         }
      }
      reset();
   }

   void reset() {
      for (auto &l : predelay_)
         l.reset();
      for (int ch = 0; ch < 2; ++ch) {
         for (auto &ap : diffuser_[ch])
            ap.line.reset();
         for (auto &ap : bloom_[ch])
            ap.line.reset();
         for (auto &ap : smear_[ch])
            ap.line.reset();
         reverse_[ch].line.reset();
         shifter_[ch].line.reset();
         drive_[ch].reset();
         lowCut_[ch].reset();
         highCut_[ch].reset();
         loopHp_[ch].y = 0.0f;
         tiltLp_[ch].y = 0.0f;
         tailTiltLp_[ch].y = 0.0f;
         loopOut_[ch] = 0.0f;
      }
      for (int k = 0; k < kLines; ++k) {
         lines_[k].reset();
         tankAp_[k].reset();
         tankAp2_[k].reset();
         bassLp_[k].y = highLp_[k].y = 0.0f;
         // Spread start phases, the same after every reset so renders repeat.
         drift_[k].start(0.618034 * k);
         apDrift_[k].start(0.381966 * k + 0.25);
      }
      echo_.reset();
      for (auto &v : voice_)
         v.reset();
      for (int b = 0; b < kMaxBodies; ++b)
         bodySwirl_[b] = lastPhase_[b] = 0.0;
      duckEnv_ = 0.0f;
      busFade_ = 1.0f;
      undamped_ = 0.0f;
      driveAmt_ = 0.0f;
      echoAmt_ = 1.0f;
      first_ = true;
   }

   // Once per control tick, before that tick's samples.
   void setControl(const Control &c) {
      c_ = c;
      const double size = std::clamp(c.size, 0.25, 10.0);
      const bool freeze = c.freeze;
      // Lines and their decay. Sixteen lines always, whatever the bodies do: each loop is the line
      // plus the allpass inside it, and decays to -60 dB in the Decay time.
      const double rt = std::max(0.05, c.decay);
      // Up to Motion 80%, at most kDriftCents a pass (the factory presets all sit here). Past it the
      // drift grows steadily, to about 30 cents at 100% and 150 at 200%, and runs up to four times faster.
      const double motion = std::clamp(c.motion, 0.0, 2.0), wild = std::max(0.0, (motion - kMotionWild) / (2.0 - kMotionWild));
      const double driftCents = std::min(1.0, motion) * kDriftCents + 146.0 * wild;
      const double apDriftCents = std::min(1.0, motion) * kApDriftCents + 29.0 * wild;
      const double driftRate = 1.0 + 3.0 * wild;
      const int bodies = std::clamp(c.bodies, 1, kMaxBodies);
      driftSpeed_ = first_ ? (freeze ? 0.0 : 1.0)
                           : std::clamp(driftSpeed_ + (freeze ? -1.0 : 1.0) * kControlTick / (0.5 * sr_), 0.0, 1.0);
      double loss = 0.0;
      for (int k = 0; k < kLines; ++k) {
         const double seconds = std::min(kMaxLineSeconds - 0.01, size * kLineMs[k] * 0.001);
         const double apSeconds = std::min(2.0, size) * kTankApMs[k] * 0.001;
         targetLength_[k] = static_cast<float>(seconds * sr_);
         targetApLength_[k] = static_cast<float>(apSeconds * sr_);
         const double ap2Seconds = std::min(2.0, size) * kTankAp2Ms[k] * 0.001;
         targetApLength2_[k] = static_cast<float>(ap2Seconds * sr_);
         if (freeze && driftSpeed_ == 0.0) {
            // Once the drift has stopped, every read lands on a whole sample (a fraction of a sample
            // less than the length asked for at most), where reading between samples can't dull it.
            const auto whole = [](float length, float drift) { return std::round(length + drift) - drift; };
            targetLength_[k] = whole(targetLength_[k], drift_[k].value());
            targetApLength_[k] = whole(targetApLength_[k], apDrift_[k].value());
            targetApLength2_[k] = std::round(targetApLength2_[k]);
         }
         const double loop = seconds + apSeconds + ap2Seconds;
         const double mid = std::pow(10.0, -3.0 * loop / rt);
         const double bass = std::min(0.9999, std::pow(10.0, -3.0 * loop / (rt * c.bassMult)));
         loss += 1.0 - std::max(mid, bass);
         // Frozen, the loop neither loses nor gains: the allpasses and the mix are lossless, so the
         // tail holds its level and colour for as long as Freeze stays on.
         gainMid_[k].set(static_cast<float>(freeze ? 1.0 : mid));
         gainBass_[k].set(static_cast<float>(freeze ? 1.0 : bass));
         bassLp_[k].setCutoff(c.bassXover, sr_);
         const int b = (k / 2) % bodies;
         // A body's tone moves its lines' treble damping by up to an octave each way, so a dark body's
         // part of the tail also dies darker.
         // Past +-1, up to three octaves.
         const double tone = std::clamp(c.tone[b], -2.0, 2.0);
         const double octaves = tone * (1.0 + 0.5 * std::max(0.0, std::fabs(tone) - 1.0));
         highLp_[k].setCutoff(std::clamp(c.dampHigh * std::exp2(octaves), 300.0, 0.45 * sr_), sr_);
         // Motion is how far the lines drift, as pitch. A swing never reaches past half a line's
         // length, so even the wildest drift reads only what was written.
         // Frozen, the drift slows to a stop where it is: a moving read point between samples loses
         // a little treble on every pass.
         drift_[k].set(kDriftHz[k] * driftRate, driftCents, sr_, 0.45 * targetLength_[k], driftSpeed_);
         apDrift_[k].set(kApDriftHz[k] * driftRate, apDriftCents, sr_, 0.45 * targetApLength_[k], driftSpeed_);
         // The body's phase sweep also nudges the gains of the allpasses in its lines. Neither that
         // nor its phaser changes a line's length, so nothing bends pitch.
         targetApOffset_[k] = static_cast<float>(kPhaseLoopDepth * std::clamp(c.swirlDepth, 0.0, 2.0) *
                                                 std::clamp(c.phase[b], -1.0, 1.0));
      }
      // Each body's voice: with n bodies the spectrum splits into n bands, log-spaced from about
      // 150 Hz to 6 kHz, body 1 lowest.
      const double spread = std::clamp(c.panDepth, 0.0, 2.0), swirl = std::clamp(c.swirlDepth, 0.0, 2.0);
      // A new body count gives every voice a new band and hands line pairs to other voices. The old
      // voices, as they were, keep playing their old share of the lines while the new ones fade in
      // over 50 ms, rather than everything switching in one sample.
      if (bodies != delayBodies_ && !first_) {
         std::copy(std::begin(voice_), std::end(voice_), std::begin(oldVoice_));
         oldBodies_ = delayBodies_;
         busFade_ = 0.0f;
      }
      for (int b = 0; b < bodies; ++b) {
         const auto edge = [&](int i) { return kBandLowHz * std::pow(kBandHighHz / kBandLowHz, static_cast<double>(i) / bodies); };
         voice_[b].set(b == 0 ? 0.0 : edge(b), b == bodies - 1 ? 0.0 : edge(b + 1), spread, std::clamp(c.pan[b], -1.0, 1.0),
                       std::clamp(c.tone[b], -2.0, 2.0), c.phase[b], swirl);
      }
      // What each body does to its voice, for the panel: where it sits, its tone, and its swirl as how
      // fast its phaser is sweeping times how deep (smoothed over about 100 ms).
      const double swirlSmooth = 1.0 - std::exp(-kControlTick / (0.1 * sr_));
      for (int b = 0; b < kMaxBodies; ++b) {
         const bool on = b < bodies;
         bodyPan_[b] = on ? std::clamp(c.panDepth * c.pan[b], -1.0, 1.0) : 0.0;
         bodyTone_[b] = on ? std::clamp(c.tone[b], -2.0, 2.0) : 0.0;
         const double phase = on ? std::clamp(c.phase[b], -1.0, 1.0) : 0.0;
         const double rate = first_ ? 0.0 : std::fabs(phase - lastPhase_[b]) * sr_ / kControlTick;
         bodySwirl_[b] += swirlSmooth * (std::clamp(c.swirlDepth, 0.0, 2.0) * std::min(1.0, rate / kSwirlFull) - bodySwirl_[b]);
         lastPhase_[b] = phase;
      }
      // From 50% up, Diffusion sets the tank's allpass gain between kTankApGainMin and Max; below,
      // it falls on to nothing at 0%, where the bare lines ring metallic and grainy.
      const double diffusion = std::clamp(c.diffusion, 0.0, 1.0);
      const double apGainMid = kTankApGainMin + (kTankApGainMax - kTankApGainMin) * 0.5;
      tankApGain_.set(static_cast<float>(diffusion >= 0.5 ? kTankApGainMin + (kTankApGainMax - kTankApGainMin) * diffusion
                                                           : apGainMid * diffusion / 0.5));
      delayBodies_ = bodies;

      // Input.
      for (int ch = 0; ch < 2; ++ch) {
         lowCut_[ch].setCutoff(c.lowCut);
         highCut_[ch].setCutoff(c.highCut);
         for (int i = 0; i < kDiffusers; ++i) {
            // Fixed lengths: set the glide's target too, or the length would creep toward it.
            diffuser_[ch][i].length = diffuser_[ch][i].target = static_cast<float>(kDiffuserMs[ch][i] * 0.001 * sr_);
            diffuser_[ch][i].gain = static_cast<float>(c.diffusion * (i < 2 ? 0.72 : 0.62));
         }
         const double attack = std::max(0.0, c.attack);
         for (int i = 0; i < 3; ++i) {
            bloom_[ch][i].target = static_cast<float>(kBloomMs[ch][i] * 0.001 * sr_ * std::min(2.0, std::sqrt(size)));
            bloom_[ch][i].gain = static_cast<float>(0.55 + 0.3 * attack);
         }
         for (int i = 0; i < 2; ++i) {
            smear_[ch][i].target = static_cast<float>(kSmearMs[ch][i] * 0.001 * sr_);
            smear_[ch][i].gain = static_cast<float>(0.7 * c.smear);
         }
         drive_[ch].set(c);
         tiltLp_[ch].setCutoff(700.0, sr_);
         tailTiltLp_[ch].setCutoff(700.0, sr_);
      }
      targetReverseWindow_ = static_cast<float>((0.08 + 0.5 * std::max(0.0, -c.attack)) * std::min(2.0, std::sqrt(size)) * sr_);
      // These two move the early taps, so they slide there at a limited speed: a 32-sample ramp
      // clicked.
      targetPredelay_ = static_cast<float>(c.predelayMs * 0.001 * sr_);
      targetEarlySize_ = static_cast<float>(std::min(4.0, size));
      attack_.set(static_cast<float>(c.attack));
      inputGain_.set(freeze ? 0.0f : 1.0f);

      // Loop.
      // The outer loop goes around the whole tank, whose resonances get louder the longer the decay:
      // their peak gain grows about as 1 / (what a line loses each pass). Scaling the loop by that
      // loss keeps Feedback meaning the same at every decay: below 100% the tail still dies away,
      // past it the loop grows into the safety stage. The curve is measured: a bisection on the
      // Feedback where a tail first sustains, over Decay 1 to 30 s and Size 0.5 to 3, lands it
      // between 96% and 123%, mostly 100% to 115%.
      const double meanLoss = loss / kLines;
      // Past Size 3 the long lines ring up more than the curve expects, so it eases off a little there
      // (measured at Size 6 and 10).
      // Below 50% Diffusion the tank's allpasses thin out and its resonances ring up harder, so the
      // loop eases off there too, to kLoopBareTank of the curve at 0% (measured at Size 0.3 to 3,
      // Decay 4 and 20 s).
      const double loopScale = std::min(kLoopMax, kLoopCalibration * std::pow(meanLoss, kLoopExponent)) *
                               std::pow(std::min(1.0, 3.0 / size), 0.2) *
                               (kLoopBareTank + (1.0 - kLoopBareTank) * std::min(1.0, diffusion / 0.5));
      loopScale_.set(static_cast<float>(loopScale / kLoopMax));
      loopGain_.set(freeze ? 0.0f : static_cast<float>(c.feedback * loopScale));
      shimmer_.set(static_cast<float>(c.shimmer));
      // Below kLoopFloor Feedback, the shimmer still goes round the loop at that much, on its own, so
      // it stacks up from Feedback 0. At or past it, it rides the loop's own gain.
      shimmerLift_.set(freeze ? 0.0f : static_cast<float>(std::max(0.0, kLoopFloor - c.feedback) * loopScale));
      pitchRatio_ = std::exp2(c.shimmerPitch / 12.0);
      smearMix_.set(static_cast<float>(c.smear));
      // Up to +-40%, a 6 dB seesaw. Past it the loop would pile up bass far faster than treble (the
      // tank damps the treble on every pass), so the bass lift slows and the treble lift steepens:
      // at full Tilt, +4.2 dB of bass or +8.4 dB of treble.
      const double over = std::max(0.0, std::fabs(c.tilt) - 0.4);
      tiltLow_.set(static_cast<float>(jclap::dsp::dbToGain(c.tilt < -0.4 ? 2.4 + 3.0 * over : -6.0 * c.tilt)));
      tiltHigh_.set(static_cast<float>(jclap::dsp::dbToGain(c.tilt > 0.4 ? 2.4 + 10.0 * over : 6.0 * c.tilt)));
      // Tilt also colours the tail as it's heard, fading out as Feedback rises to kLoopFloor: there the
      // loop, which tilts the tail again on every pass, has taken over.
      const double tailTilt = c.tilt * std::max(0.0, 1.0 - c.feedback / kLoopFloor);
      tailTiltLow_.set(static_cast<float>(jclap::dsp::dbToGain(-kTailTiltDb * tailTilt)));
      tailTiltHigh_.set(static_cast<float>(jclap::dsp::dbToGain(kTailTiltDb * tailTilt)));

      // Echo.
      echo_.set(c);
      echoMix_.set(c.echoOn ? static_cast<float>(c.echoMix) : 0.0f);

      // Output.
      const double el = std::clamp(c.earlyLate, 0.0, 1.0) * 0.5 * jclap::dsp::kPi;
      earlyGain_.set(static_cast<float>(std::cos(el) * 1.4));
      lateGain_.set(static_cast<float>(std::sin(el)));
      width_.set(static_cast<float>(c.width));
      duck_.set(static_cast<float>(c.duck));
      dry_.set(static_cast<float>(1.0 - c.mix));
      wet_.set(static_cast<float>(c.mix));
      level_.set(static_cast<float>(jclap::dsp::dbToGain(c.levelDb)));

      if (first_) {
         first_ = false;
         for (int k = 0; k < kLines; ++k) {
            length_[k] = aimLength_[k] = targetLength_[k];
            apLength_[k] = aimApLength_[k] = targetApLength_[k];
            apLength2_[k] = aimApLength2_[k] = targetApLength2_[k];
            apOffset_[k] = targetApOffset_[k];
            drift_[k].depth.settle();
            apDrift_[k].depth.settle();
         }
         for (int ch = 0; ch < 2; ++ch) {
            for (auto &ap : bloom_[ch])
               ap.length = ap.aim = ap.target;
            for (auto &ap : smear_[ch])
               ap.length = ap.aim = ap.target;
         }
         reverseWindow_ = targetReverseWindow_;
         driveSlot_ = c.driveWhere;
         driveType_ = c.driveType;
         for (auto &d : drive_)
            d.setMode(driveType_, driveSlot_ == DriveWhere::Loop);
         driveAmt_ = c.driveOn ? 1.0f : 0.0f;
         echoSlot_ = c.echoWhere;
         echoAmt_ = 1.0f;
         predelaySamples_ = aimPredelay_ = targetPredelay_;
         earlySize_ = aimEarlySize_ = targetEarlySize_;
         for (detail::Ramp &r : gainMid_)
            r.settle();
         for (detail::Ramp &r : gainBass_)
            r.settle();
         for (detail::Ramp *r : {&tankApGain_, &attack_, &inputGain_, &loopGain_, &loopScale_,
                                 &shimmer_, &shimmerLift_, &smearMix_, &tiltLow_, &tiltHigh_,
                                 &tailTiltLow_, &tailTiltHigh_, &echoMix_, &earlyGain_, &lateGain_,
                                 &width_, &duck_, &dry_, &wet_, &level_})
            r->settle();
      }
   }

   // One stereo sample. Also returns the late tank's output, for the panel's glow.
   void process(float inL, float inR, float &outL, float &outR, float &lateLevel) {
      const float dryL = inL, dryR = inR;
      float x[2] = {lowCut_[0].process(highCut_[0].process(inL)), lowCut_[1].process(highCut_[1].process(inR))};
      // Drive On, its slot and its type, and the echo's slot, never switch in one sample: the drive
      // fades out (over 5 ms) before its slot or type changes and back in after, and fades in from
      // a cleared start when it comes on; the echo fades its input and output the same way when it
      // moves, so no step is written into its repeats either.
      stepSlots();
      // Eased, so the fade has no corners: a linear one bends the waveform where it starts and stops.
      const auto ease = [](float t) { return t * t * (3.0f - 2.0f * t); };
      const float driveAmt = ease(driveAmt_);
      const auto drive = [&](float *v) {
         for (int ch = 0; ch < 2; ++ch)
            v[ch] += driveAmt * (drive_[ch].process(v[ch]) - v[ch]);
      };
      const bool driving = driveAmt > 0.0f;
      const bool pre = driving && driveSlot_ == DriveWhere::Pre;
      const bool loop = driving && driveSlot_ == DriveWhere::Loop;
      const bool post = driving && driveSlot_ == DriveWhere::Post;
      if (pre)
         drive(x);
      // The echo runs every sample in whichever slot it sits, even switched off (its mix is then
      // zero), so turning it back on never replays repeats from long ago.
      const float echoAmt = ease(echoAmt_);
      const float echoMix = echoMix_.next() * echoAmt;
      const EchoWhere echoWhere = echoSlot_;
      float echoed[2] = {0.0f, 0.0f};
      if (echoWhere == EchoWhere::Into) {
         const float in[2] = {x[0] * echoAmt, x[1] * echoAmt};
         echo_.process(in, echoed);
         for (int ch = 0; ch < 2; ++ch)
            x[ch] += echoMix * echoed[ch];
      }

      // Predelay, and early reflections tapped off the same lines.
      aimPredelay_ += std::clamp(targetPredelay_ - aimPredelay_, -kPredelaySlew, kPredelaySlew);
      aimEarlySize_ += std::clamp(targetEarlySize_ - aimEarlySize_, -earlySlew_, earlySlew_);
      predelaySamples_ += easeGlide_ * (aimPredelay_ - predelaySamples_);
      earlySize_ += easeGlide_ * (aimEarlySize_ - earlySize_);
      const float pd = predelaySamples_, es = earlySize_;
      float early[2] = {0.0f, 0.0f}, delayed[2];
      for (int ch = 0; ch < 2; ++ch) {
         predelay_[ch].write(x[ch]);
         delayed[ch] = predelay_[ch].read(std::max(1.0f, pd));
         for (int t = ch; t < kEarlyTaps; t += 2)
            early[ch] += kEarlyGain[t] * predelay_[ch].read(pd + static_cast<float>(kEarlyMs[t] * 0.001 * sr_) * es);
      }

      // Diffuse, then shape the attack: a slow bloom one way, reversed windows the other.
      const float attack = attack_.next(), inGain = inputGain_.next();
      reverseWindow_ += lengthGlide_ * (targetReverseWindow_ - reverseWindow_);
      float inject[2];
      for (int ch = 0; ch < 2; ++ch) {
         float d = delayed[ch];
         for (auto &ap : diffuser_[ch])
            d = ap.process(d);
         float shaped = d;
         if (attack > 0.0f) {
            float b = d;
            for (auto &ap : bloom_[ch])
               b = ap.process(b);
            shaped = d + attack * (b - d);
         }
         const float r = reverse_[ch].process(d, reverseWindow_);
         if (attack < 0.0f)
            shaped = d + -attack * (r - d);
         inject[ch] = shaped * inGain + loopOut_[ch];
      }

      // The tank. Each line is read with its drift, through the allpass inside it, then damped.
      float y[kLines], o[kLines];
      const float apGain = tankApGain_.next();
      undamped_ += std::clamp((c_.freeze ? 1.0f : 0.0f) - undamped_, -freezeFadeStep_, freezeFadeStep_);
      const float undamped = undamped_;
      for (int k = 0; k < kLines; ++k) {
         aimLength_[k] += easeGlide_ * (targetLength_[k] - aimLength_[k]);
         aimApLength_[k] += easeGlide_ * (targetApLength_[k] - aimApLength_[k]);
         aimApLength2_[k] += easeGlide_ * (targetApLength2_[k] - aimApLength2_[k]);
         length_[k] += lengthGlide_ * (aimLength_[k] - length_[k]);
         apLength_[k] += lengthGlide_ * (aimApLength_[k] - apLength_[k]);
         apLength2_[k] += lengthGlide_ * (aimApLength2_[k] - apLength2_[k]);
         apOffset_[k] += panGlide_ * (targetApOffset_[k] - apOffset_[k]);
         const float g = std::clamp(apGain + apOffset_[k], 0.0f, kTankApGainCeiling);
         float v = lines_[k].read(length_[k] + drift_[k].next());
         const float d = tankAp_[k].read(apLength_[k] + apDrift_[k].next());
         const float w = v + g * d;
         tankAp_[k].write(w);
         v = d - g * w;
         const float d2 = tankAp2_[k].read(apLength2_[k]);
         const float w2 = v + g * d2;
         tankAp2_[k].write(w2);
         v = d2 - g * w2;
         y[k] = v;
         // Bass and treble decay. Written as a lowpass shelf after the mid gain, then a lowpass for the
         // treble, so the loop's gain at every frequency stays at or below the larger of the two
         // decay gains: splitting into a low band and a damped high band and summing them can come
         // out above both near the crossover, and at long decays the tank then grows.
         const float gMid = gainMid_[k].next(), gBass = gainBass_[k].next();
         const float shelved = gMid * v + (gBass - gMid) * bassLp_[k].lowpass(v);
         // Frozen, the damping fades out (over freezeFade_), or every pass would darken the tail.
         const float damped = highLp_[k].lowpass(shelved);
         o[k] = damped + undamped * (shelved - damped);
      }
      // A 16 x 16 Hadamard mix: every line feeds every other equally and no energy is gained or lost.
      for (int h = 1; h < kLines; h *= 2)
         for (int i = 0; i < kLines; i += 2 * h)
            for (int j = i; j < i + h; ++j) {
               const float a = o[j], b = o[j + h];
               o[j] = a + b;
               o[j + h] = a - b;
            }
      // What's heard: each body's lines, even ones on the left and odd on the right, through that
      // body's voice.
      float bus[kMaxBodies][2] = {};
      for (int k = 0; k < kLines; ++k)
         bus[(k / 2) % delayBodies_][k & 1] += kOutSign[k] * y[k];
      // The outer loop takes the lines before the voices, so a voice's boost or ring is heard but
      // never fed back.
      float late[2] = {0.0f, 0.0f}, plain[2] = {0.0f, 0.0f};
      for (int b = 0; b < delayBodies_; ++b) {
         float l, r;
         const float g = busGain(b, delayBodies_);
         voice_[b].process(g * bus[b][0], g * bus[b][1], l, r);
         late[0] += l;
         late[1] += r;
         plain[0] += bus[b][0];
         plain[1] += bus[b][1];
      }
      // Just after the body count changed: the old voices on their old lines, fading out.
      if (busFade_ < 1.0f) {
         const float fade = busFade_;
         busFade_ = std::min(1.0f, busFade_ + busFadeStep_);
         float oldBus[kMaxBodies][2] = {}, old[2] = {0.0f, 0.0f};
         for (int k = 0; k < kLines; ++k)
            oldBus[(k / 2) % oldBodies_][k & 1] += kOutSign[k] * y[k];
         for (int b = 0; b < oldBodies_; ++b) {
            float l, r;
            const float g = busGain(b, oldBodies_);
            oldVoice_[b].process(g * oldBus[b][0], g * oldBus[b][1], l, r);
            old[0] += l;
            old[1] += r;
         }
         for (int ch = 0; ch < 2; ++ch)
            late[ch] = fade * late[ch] + (1.0f - fade) * old[ch];
      }
      for (int ch = 0; ch < 2; ++ch) {
         late[ch] *= kOutScale;
         plain[ch] *= kOutScale;
      }
      for (int k = 0; k < kLines; ++k)
         lines_[k].write(detail::tankLimit(0.25f * o[k] + kInjectSign[k] * kInjectScale * inject[k & 1]));

      // The outer loop, heard next sample.
      const float shimmer = shimmer_.next(), shimmerLift = shimmerLift_.next(), smearMix = smearMix_.next();
      const float tl = tiltLow_.next(), th = tiltHigh_.next(), fb = loopGain_.next(), loopScale = loopScale_.next();
      for (int ch = 0; ch < 2; ++ch) {
         float v = plain[ch];
         if (loop)
            v += driveAmt * (drive_[ch].process(v) - v);
         v = loopHp_[ch].highpass(v);
         const float shifted = shifter_[ch].process(v, pitchRatio_);
         v += shimmer * (shifted - v);
         const float lifted = shimmerLift * shimmer * shifted;
         float s = v;
         for (auto &ap : smear_[ch])
            s = ap.process(s);
         v += smearMix * (s - v);
         const float lo = tiltLp_[ch].lowpass(v);
         v = lo * tl + (v - lo) * th;
         loopPre_[ch] = v;
         loopTail_[ch] = v * fb + lifted;
      }
      if (echoWhere == EchoWhere::Loop) {
         // Echoes of the shaped tail go back into the tank, so it returns in long, spaced passes,
         // even with Feedback at zero. The safety stage below keeps the two loops bounded.
         const float in[2] = {loopPre_[0] * echoAmt, loopPre_[1] * echoAmt};
         echo_.process(in, echoed);
         for (int ch = 0; ch < 2; ++ch)
            loopTail_[ch] += 0.6f * echoMix * inGain * loopScale * echoed[ch];
      }
      for (int ch = 0; ch < 2; ++ch) {
         const float v = loopTail_[ch];
         loopOut_[ch] = c_.safetyHard ? std::clamp(v, -1.0f, 1.0f) : std::tanh(v);
      }

      // Output.
      const float ttl = tailTiltLow_.next(), tth = tailTiltHigh_.next();
      for (int ch = 0; ch < 2; ++ch) {
         const float lo = tailTiltLp_[ch].lowpass(late[ch]);
         late[ch] = lo * ttl + (late[ch] - lo) * tth;
      }
      const float eg = earlyGain_.next(), lg = lateGain_.next();
      float wet[2] = {early[0] * eg + late[0] * lg, early[1] * eg + late[1] * lg};
      if (post)
         drive(wet);
      if (echoWhere == EchoWhere::After) {
         const float in[2] = {wet[0] * echoAmt, wet[1] * echoAmt};
         echo_.process(in, echoed);
         for (int ch = 0; ch < 2; ++ch)
            wet[ch] += echoMix * echoed[ch];
      }
      // Past 100% Feedback the loop runs up into its safety stage and the tail can come out hotter
      // than full scale; above it, the wet signal bends softly toward 1.2. Below, nothing changes.
      for (float &v : wet)
         v = detail::wetGuard(v);
      const float w = width_.next();
      const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * w;
      const float level = std::max(std::fabs(dryL), std::fabs(dryR));
      duckEnv_ += (level > duckEnv_ ? duckAttack_ : duckRelease_) * (level - duckEnv_);
      const float duck = 1.0f - duck_.next() * std::min(1.0f, 4.0f * duckEnv_);
      const float dg = dry_.next(), wg = wet_.next() * duck, lv = level_.next();
      outL = (dryL * dg + (mid + side) * wg) * lv;
      outR = (dryR * dg + (mid - side) * wg) * lv;
      lateLevel = std::max(std::fabs(late[0]), std::fabs(late[1]));
   }

   // The eight line pairs don't share out evenly among three, five or six bodies (five gives three
   // bodies two pairs and two bodies one), so each body's voice is levelled by how many it has:
   // every body is heard equally loud, as with two or four.
   static float busGain(int b, int bodies) {
      constexpr int kPairs = kLines / 2;
      const int pairs = kPairs / bodies + (b < kPairs % bodies ? 1 : 0);
      return static_cast<float>(std::sqrt(static_cast<double>(kPairs) / bodies / pairs));
   }

   // Moves the drive's and the echo's fades one sample on. Each fades out when what it should be
   // differs from what it is, switches once silent, and fades back in.
   void stepSlots() {
      const bool driveMatches = driveSlot_ == c_.driveWhere && driveType_ == c_.driveType;
      if (driveAmt_ == 0.0f && !driveMatches) {
         driveSlot_ = c_.driveWhere;
         driveType_ = c_.driveType;
         for (auto &d : drive_)
            d.setMode(driveType_, driveSlot_ == DriveWhere::Loop);
      }
      const float driveTarget = c_.driveOn && driveMatches ? 1.0f : 0.0f;
      if (driveAmt_ == 0.0f && driveTarget > 0.0f)
         // Starting from silence: whatever its filters and oversampler held from last time is stale.
         for (auto &d : drive_)
            d.reset();
      driveAmt_ += std::clamp(driveTarget - driveAmt_, -slotStep_, slotStep_);
      if (echoAmt_ == 0.0f && echoSlot_ != c_.echoWhere)
         echoSlot_ = c_.echoWhere;
      echoAmt_ += std::clamp((echoSlot_ == c_.echoWhere ? 1.0f : 0.0f) - echoAmt_, -slotStep_, slotStep_);
   }

   // The mean length of body b's lines, in milliseconds, for the panel's readout.
   double delayMs(int b) const {
      double sum = 0.0;
      int count = 0;
      for (int k = 0; k < kLines; ++k)
         if ((k / 2) % delayBodies_ == b) {
            sum += length_[k];
            ++count;
         }
      return count ? 1000.0 * sum / count / sr_ : 0.0;
   }

   // What body b does to its share of the tail, as the panel shows it: where it sits (-1..1), its
   // tone (-2 darkest..2 brightest, past +-1 extreme) and how hard its phase is swirling (0..2).
   double bodyPan(int b) const { return bodyPan_[b]; }
   double bodyTone(int b) const { return bodyTone_[b]; }
   double bodySwirl(int b) const { return bodySwirl_[b]; }

   static constexpr double kMaxLineSeconds = 2.1;
   static constexpr double kMaxTankApSeconds = 0.06;

private:
   static constexpr double kLoopCalibration = 3.3, kLoopExponent = 0.83, kLoopMax = 1.25, kLoopBareTank = 0.8;
   // Below this Feedback, Shimmer and Tilt still act on their own (see setControl), so they're heard
   // from Feedback 0. Factory presets that use them sit at or above it, and sound as they always did.
   static constexpr double kLoopFloor = 0.4;
   // How far Tilt lifts or cuts the tail as heard, in dB at full Tilt, with Feedback at 0.
   static constexpr double kTailTiltDb = 9.0;
   // Line lengths at size 1, in ms: spread evenly on a log scale and none a simple ratio of another.
   // Adjacent lines go to opposite sides, so each side spans the whole range.
   static constexpr double kLineMs[kLines] = {29.3, 33.1, 37.9, 42.7, 48.9, 55.3, 63.1, 71.9,
                                              81.7, 92.9, 105.7, 120.3, 137.1, 155.9, 177.7, 201.9};
   // The allpass inside each line, in ms at size 1, shuffled against the line lengths.
   static constexpr double kTankApMs[kLines] = {11.3, 5.9, 17.7, 8.3, 21.1, 6.7, 14.9, 9.7,
                                                23.3, 4.9, 12.7, 19.3, 7.1, 16.1, 10.3, 13.9};
   static constexpr double kTankAp2Ms[kLines] = {3.1, 6.3, 2.3, 4.7, 7.9, 2.9, 5.3, 3.7,
                                                 6.9, 2.1, 4.1, 7.3, 3.3, 5.9, 2.7, 4.9};
   // Diffusion sets the tank's allpass gain between these from 50% up (below, down to 0).
   static constexpr double kTankApGainMin = 0.2, kTankApGainMax = 0.65;
   static constexpr float kTankApGainCeiling = 0.8f;
   // How far a body's phase sweep moves its loop allpass gains, and the span of the bodies' bands.
   static constexpr double kPhaseLoopDepth = 0.12;
   static constexpr double kBandLowHz = 150.0, kBandHighHz = 6000.0;
   // A phase sweep moving this fast (full range per second) shows as full swirl.
   static constexpr double kSwirlFull = 2.0;
   // Drift rates in Hz, one per line, spread over a decade and shuffled, and the most a pass bends.
   static constexpr double kDriftHz[kLines] = {0.137, 0.713, 0.291, 1.093, 0.173, 0.557, 0.389, 0.941,
                                               0.113, 0.811, 0.241, 0.479, 0.617, 1.171, 0.331, 0.887};
   static constexpr double kApDriftHz[kLines] = {0.53, 0.19, 0.97, 0.31, 0.71, 0.23, 1.31, 0.43,
                                                 0.61, 0.17, 1.13, 0.37, 0.83, 0.27, 0.47, 1.01};
   static constexpr double kDriftCents = 4.0, kApDriftCents = 1.0;
   // Where Motion starts to warble.
   static constexpr double kMotionWild = 0.8;
   static constexpr float kInjectSign[kLines] = {1, 1, -1, 1, 1, -1, -1, -1, 1, -1, -1, 1, 1, 1, -1, -1};
   static constexpr float kOutSign[kLines] = {1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, 1};
   static constexpr float kInjectScale = 0.3f, kOutScale = 0.6f;
   static constexpr int kDiffusers = 6;
   static constexpr double kDiffuserMs[2][kDiffusers] = {{4.77, 3.59, 12.73, 9.31, 17.93, 23.11},
                                                         {5.13, 3.31, 13.71, 8.89, 19.27, 21.73}};
   static constexpr double kBloomMs[2][3] = {{41.0, 67.0, 101.0}, {47.0, 71.0, 107.0}};
   static constexpr double kSmearMs[2][2] = {{17.3, 27.9}, {19.1, 25.7}};
   static constexpr int kEarlyTaps = 8;
   static constexpr double kEarlyMs[kEarlyTaps] = {7.1, 11.3, 17.9, 23.3, 31.1, 41.9, 53.3, 67.1};
   static constexpr float kEarlyGain[kEarlyTaps] = {0.55f, 0.5f, 0.42f, -0.38f, 0.3f, -0.26f, 0.2f, 0.16f};

   double sr_ = 48000.0;
   Control c_;
   bool first_ = true;

   jclap::dsp::Filter lowCut_[2], highCut_[2];
   jclap::dsp::DelayLine predelay_[2];
   detail::Allpass diffuser_[2][kDiffusers], bloom_[2][3], smear_[2][2];
   detail::Reverser reverse_[2];
   detail::PitchShifter shifter_[2];
   detail::Drive drive_[2];
   detail::OnePole loopHp_[2], tiltLp_[2], tailTiltLp_[2];
   float loopOut_[2] = {}, loopPre_[2] = {}, loopTail_[2] = {};
   detail::Echo echo_;
   float reverseWindow_ = 4800.0f, targetReverseWindow_ = 4800.0f;
   double pitchRatio_ = 2.0;

   jclap::dsp::DelayLine lines_[kLines], tankAp_[kLines], tankAp2_[kLines];
   detail::OnePole bassLp_[kLines], highLp_[kLines];
   detail::Ramp gainMid_[kLines], gainBass_[kLines], tankApGain_;
   detail::Drift drift_[kLines], apDrift_[kLines];
   float length_[kLines] = {}, targetLength_[kLines] = {}, apLength_[kLines] = {}, targetApLength_[kLines] = {};
   float apLength2_[kLines] = {}, targetApLength2_[kLines] = {};
   // Where each length's glide is heading this sample, easing toward its target (see easeGlide_).
   float aimLength_[kLines] = {}, aimApLength_[kLines] = {}, aimApLength2_[kLines] = {};
   float apOffset_[kLines] = {}, targetApOffset_[kLines] = {};
   detail::BodyVoice voice_[kMaxBodies], oldVoice_[kMaxBodies];
   float lengthGlide_ = 0.001f, panGlide_ = 0.001f, easeGlide_ = 0.004f;
   int delayBodies_ = 3, oldBodies_ = 3;
   // How far a body-count crossfade has got (1 when there's none), and how far it moves a sample.
   // oldVoice_ holds the voices as they were before the change.
   float busFade_ = 1.0f, busFadeStep_ = 0.0005f;
   // How far Freeze has faded the tank's damping out (0..1), and its step per sample.
   float undamped_ = 0.0f, freezeFadeStep_ = 0.0005f;
   // How fast the tank's drift swings (1, slowing to 0 over half a second while frozen).
   double driftSpeed_ = 1.0;
   // The drive's and echo's slot fades (0..1) and the slot and type each is really in.
   float driveAmt_ = 0.0f, echoAmt_ = 1.0f, slotStep_ = 0.004f;
   DriveWhere driveSlot_ = DriveWhere::Loop;
   DriveType driveType_ = DriveType::Tube;
   EchoWhere echoSlot_ = EchoWhere::Into;
   double bodyPan_[kMaxBodies] = {}, bodyTone_[kMaxBodies] = {}, bodySwirl_[kMaxBodies] = {}, lastPhase_[kMaxBodies] = {};

   static constexpr float kPredelaySlew = 0.5f;
   float predelaySamples_ = 0.0f, aimPredelay_ = 0.0f, targetPredelay_ = 0.0f;
   float earlySize_ = 1.0f, aimEarlySize_ = 1.0f, targetEarlySize_ = 1.0f, earlySlew_ = 1e-4f;
   detail::Ramp attack_, inputGain_;
   detail::Ramp loopGain_, loopScale_, shimmer_, shimmerLift_, smearMix_, tiltLow_, tiltHigh_, tailTiltLow_, tailTiltHigh_;
   detail::Ramp echoMix_;
   detail::Ramp earlyGain_, lateGain_, width_, duck_, dry_, wet_, level_;
   float duckEnv_ = 0.0f, duckAttack_ = 0.01f, duckRelease_ = 0.001f;
};

} // namespace jverb

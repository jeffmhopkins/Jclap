#pragma once

// The voice's building blocks. jclap::dsp has a filter, envelope and LFO, but a JSynth voice needs
// more of each (notch and peak, a ladder and a comb; delay, hold, curves and loop modes; smooth
// random, delay and tempo sync), and it runs 40 of them sample by sample, so these are small,
// float, and allocate nothing after prepare().

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jsynth::dsp {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

// A small xorshift: our own, so a render is the same on every platform and every run from a seed.
struct Random {
   uint32_t state = 0x2545f491;
   void seed(uint32_t s) { state = s ? s : 0x2545f491; }
   uint32_t next() {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return state;
   }
   // -1..1.
   float bipolar() { return static_cast<float>(next()) / 2147483648.0f - 1.0f; }
   // 0..1.
   float unipolar() { return static_cast<float>(next()) / 4294967296.0f; }
};

// ---- fast helpers ----

// The fractional part, for phases: floor() is a library call without SSE 4.1, and this runs for every
// oscillator every sample.
inline float frac(float x) {
   const float i = static_cast<float>(static_cast<int>(x));
   const float f = x - i;
   return f < 0.0f ? f + 1.0f : f;
}

// tanh to within about 0.2%, smooth, and exact in the limits: a Pade approximant, clamped where it
// meets 1.
inline float fastTanh(float x) {
   if (x > 3.0f)
      return 1.0f;
   if (x < -3.0f)
      return -1.0f;
   const float x2 = x * x;
   return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---- sine ----

// A sine from a table, for phase in cycles (any value; only its fraction is used).
class Sine {
public:
   static float at(float phase) {
      const Table &t = table();
      const float p = frac(phase) * kSize;
      const int i = static_cast<int>(p);
      const float f = p - static_cast<float>(i);
      return t.v[i] + (t.v[i + 1] - t.v[i]) * f;
   }

private:
   static constexpr int kSize = 4096;
   struct Table {
      float v[kSize + 1];
      Table() {
         for (int i = 0; i <= kSize; ++i)
            v[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * i / kSize));
      }
   };
   static const Table &table() {
      static const Table t;
      return t;
   }
};

// ---- oscillator waves ----

// The waves, in kWaveNames order.
enum Wave {
   kSine,
   kTriangle,
   kSaw,
   kSquare,
   kPulse,
   kHalfSine,
   kAbsSine,
   kQuarter,
   kAltSine,
   kAltHalf,
   kCamel,
   kAltCamel,
   kWhite,
   kPink,
   kRandom, // a tonal wave, Sine to Alt Camel, drawn fresh for each note
};

inline float polyBlep(float t, float dt) {
   if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0f;
   }
   if (t > 1.0f - dt) {
      t = (t - 1.0f) / dt;
      return t * t + t + t + 1.0f;
   }
   return 0.0f;
}

// One sample of `wave` at phase `p` (cycles, 0..1), which advanced by `dt` cycles this sample.
// Shape is pulse width for Square and Pulse (Square is centred, Pulse narrows to 5%), and for the
// others a phase distortion that bends the cycle toward its start (0.5 is none).
inline float wave(int w, float p, float dt, float shape) {
   dt = std::clamp(std::fabs(dt), 1e-6f, 0.5f);
   if (w != kSquare && w != kPulse && w != kSaw) {
      // Phase distortion: shape 0.5 is straight; toward 0 or 1 the first half of the cycle is
      // squeezed or stretched.
      const float knee = std::clamp(shape, 0.05f, 0.95f);
      p = p < knee ? 0.5f * p / knee : 0.5f + 0.5f * (p - knee) / (1.0f - knee);
   }
   switch (w) {
   default:
   case kSine:
      return Sine::at(p);
   case kTriangle:
      return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
   case kSaw:
      return 2.0f * p - 1.0f - polyBlep(p, dt);
   case kSquare:
   case kPulse: {
      const float width = w == kSquare ? 0.1f + 0.8f * shape : 0.05f + 0.45f * shape;
      float q = p < width ? 1.0f : -1.0f;
      q += polyBlep(p, dt);
      float t2 = p - width;
      if (t2 < 0.0f)
         t2 += 1.0f;
      q -= polyBlep(t2, dt);
      // Remove the DC a narrow pulse has.
      return q - (2.0f * width - 1.0f);
   }
   // The TX81Z's eight waves (wave 1 is the sine above), each built from the sine.
   case kHalfSine:
      return p < 0.5f ? Sine::at(p) : 0.0f;
   case kAbsSine:
      return std::fabs(Sine::at(0.5f * p)) * 2.0f - 1.27324f; // its mean, 4/pi, taken off
   case kQuarter:
      return (p < 0.25f || (p >= 0.5f && p < 0.75f)) ? std::fabs(Sine::at(p)) : 0.0f;
   case kAltSine:
      return p < 0.5f ? Sine::at(2.0f * p) : 0.0f;
   case kAltHalf:
      return p < 0.5f ? std::fabs(Sine::at(2.0f * p)) : 0.0f;
   case kCamel:
      return p < 0.5f ? std::fabs(Sine::at(2.0f * p)) : -std::fabs(Sine::at(2.0f * p));
   case kAltCamel:
      return p < 0.5f ? std::fabs(Sine::at(4.0f * p)) * (p < 0.25f ? 1.0f : -1.0f) : 0.0f;
   }
}

// White or pink noise. Pink is Paul Kellet's economy filter on white.
struct Noise {
   Random rng;
   float b0 = 0, b1 = 0, b2 = 0;
   void reset() { b0 = b1 = b2 = 0; }
   float white() { return rng.bipolar(); }
   float pink() {
      const float w = rng.bipolar();
      b0 = 0.99765f * b0 + w * 0.0990460f;
      b1 = 0.96300f * b1 + w * 0.2965164f;
      b2 = 0.57000f * b2 + w * 1.0526913f;
      return (b0 + b1 + b2 + w * 0.1848f) * 0.25f;
   }
};

// ---- filters ----

// One zero-delay-feedback state variable stage (Zavalishin's TPT SVF): low, high, band, notch and
// peak from the same state. Stable however fast g moves.
struct Svf {
   float ic1 = 0, ic2 = 0;
   void reset() { ic1 = ic2 = 0; }
   // g = tan(pi fc / fs); k = 1/Q.
   void process(float x, float g, float k, float &lp, float &bp, float &hp) {
      const float a1 = 1.0f / (1.0f + g * (g + k));
      const float v3 = x - ic2;
      const float v1 = a1 * ic1 + g * a1 * v3;
      const float v2 = ic2 + g * v1;
      ic1 = 2.0f * v1 - ic1;
      ic2 = 2.0f * v2 - ic2;
      lp = v2;
      bp = v1;
      hp = x - k * v1 - v2;
   }
};

// A 4-pole transistor ladder, solved with one zero-delay step per sample: each stage is a TPT
// one-pole, the feedback loop is solved linearly, and the tanh on the input uses that estimate.
struct Ladder {
   float s[4] = {};
   void reset() { s[0] = s[1] = s[2] = s[3] = 0; }
   // Passes `x` through as a wide-open ladder would, keeping its state where that ladder's would be,
   // so it can start filtering again at any sample without a step.
   float pass(float x) {
      s[0] = s[1] = s[2] = s[3] = x;
      return x;
   }
   float process(float x, float g, float resonance) {
      const float G = g / (1.0f + g);
      const float k = 4.0f * resonance; // self-oscillates near 4
      // The output if the loop were linear: y = G^4 u + S, with u = x - k y.
      const float G2 = G * G, G4 = G2 * G2;
      const float S = G * G2 * (1.0f - G) * s[0] + G2 * (1.0f - G) * s[1] + G * (1.0f - G) * s[2] + (1.0f - G) * s[3];
      const float y0 = (G4 * x + S) / (1.0f + k * G4);
      // The nonlinearity on the estimate, then the stages for real.
      float u = fastTanh(x - k * y0);
      for (int i = 0; i < 4; ++i) {
         const float v = (u - s[i]) * G;
         const float out = v + s[i];
         s[i] = out + v;
         u = out;
      }
      // Made up for the loop's loss, so the passband stays at unity as the resonance rises.
      return u * (1.0f + k);
   }
};

// A comb tuned to a frequency: positive feedback (Comb +) rings at its harmonics, negative (Comb -)
// at the odd ones. The delay is preallocated for the lowest cutoff at the highest rate.
struct Comb {
   std::vector<float> buffer;
   int write = 0;
   // How much of the buffer has been written since the last reset, the only part a reset clears,
   // so resetting a comb that never ran (most voices') costs nothing.
   int used = 0;
   void prepare(int maxSamples) {
      buffer.assign(static_cast<size_t>(std::max(4, maxSamples)), 0.0f);
      write = used = 0;
   }
   void reset() {
      std::fill(buffer.begin(), buffer.begin() + used, 0.0f);
      write = used = 0;
   }
   // Takes up `o`'s state (same size), copying only what it has written.
   void copyFrom(const Comb &o) {
      const int n = std::max(used, o.used);
      std::copy(o.buffer.begin(), o.buffer.begin() + n, buffer.begin());
      write = o.write;
      used = o.used;
   }
   float process(float x, float delaySamples, float feedback) {
      const int n = static_cast<int>(buffer.size());
      const float d = std::clamp(delaySamples, 1.0f, static_cast<float>(n - 2));
      float r = static_cast<float>(write) - d;
      if (r < 0.0f)
         r += static_cast<float>(n);
      const int i0 = static_cast<int>(r);
      const int i1 = i0 + 1 >= n ? 0 : i0 + 1;
      const float f = r - static_cast<float>(i0);
      const float delayed = buffer[i0] + (buffer[i1] - buffer[i0]) * f;
      const float y = x + feedback * delayed;
      buffer[write] = std::clamp(y, -4.0f, 4.0f);
      if (++write >= n)
         write = 0;
      used = std::max(used, write == 0 ? n : write);
      return 0.5f * (x + delayed);
   }
};

// ---- envelope ----

// Delay, attack, hold, decay, sustain, release, with a curve per segment (-1..1 bends it). Loop Held
// repeats attack and decay while the gate is held. Loop Free keeps repeating after the gate drops
// too, until the voice ends; the amp envelope always releases on gate off.
//
// Or Shape mode, after XAOC's Zadar: a drawn contour (a table, from shapes.h) stretched over one
// time, warped in time and bent in level, played forward or reversed, and repeated a number of
// times, while the gate is held, or forever. It ends holding its last level. A forced gate off (the
// amp) fades it out over the release while the contour keeps moving under the fade. A chain holds
// an envelope waiting (arm()) until the one before it gets far enough through (trigger()).
class Envelope {
public:
   enum Stage { kIdle, kDelay, kAttack, kHold, kDecay, kSustain, kRelease, kShape, kWait, kDone };
   enum Retrigger { kSmooth, kRestart, kIgnore };
   struct Settings {
      float delay = 0, attack = 0.005f, hold = 0, decay = 0.3f, sustain = 0.7f, release = 0.3f; // seconds
      float attackCurve = 0, decayCurve = 0, releaseCurve = 0;
      int loop = 0; // 0 Off, 1 Held, 2 Free
      // Shape mode.
      bool shapeMode = false;
      const float *table = nullptr; // `points` + 1 levels, 0..1
      int points = 0;
      float time = 2.0f;            // seconds a cycle
      float warp = 0, bend = 0;     // -1..1
      int repeats = 1;              // cycles before it ends, 0 for forever
      bool held = false;            // repeats while the gate is held instead
      bool reverse = false;
      int retrigger = kSmooth;
   };

   // Starts from wherever the level is, so a retrigger never clicks.
   void gateOn() {
      gate_ = true;
      if (settings_.shapeMode) {
         trigger();
         return;
      }
      start_ = level_;
      t_ = 0;
      stage_ = kDelay;
   }
   // `force` releases even a Free loop (the amp envelope).
   void gateOff(bool force) {
      gate_ = false;
      if (settings_.shapeMode) {
         if (force && stage_ != kIdle && !releasing_) {
            releasing_ = true;
            releaseT_ = 0;
            from_ = level_;
         }
         return;
      }
      if (stage_ != kIdle && stage_ != kRelease && (force || settings_.loop != 2)) {
         from_ = level_;
         t_ = 0;
         stage_ = kRelease;
      }
   }
   // Shape mode, in a chain: the gate is on but the contour waits for trigger(), holding its level.
   void arm() {
      gate_ = true;
      releasing_ = false;
      gain_ = 1;
      cycles_ = 0;
      stage_ = kWait;
   }
   // Shape mode: plays the contour from the top, or (Smooth) from where it is at the level now.
   void trigger() {
      const Settings &s = settings_;
      const bool playing = stage_ == kShape && !releasing_;
      if (s.retrigger == kIgnore && playing)
         return;
      float x = 0;
      if (s.retrigger == kSmooth && stage_ != kIdle && level_ > 1e-3f)
         x = phaseAtLevel(level_);
      t_ = x;
      cycles_ = 0;
      releasing_ = false;
      gain_ = 1;
      stage_ = kShape;
      ++runs_;
   }
   void reset() {
      stage_ = kIdle;
      level_ = 0;
      t_ = 0;
      gain_ = 1;
      releasing_ = false;
   }
   void set(const Settings &s) { settings_ = s; }
   bool active() const { return stage_ != kIdle; }
   bool releasing() const { return stage_ == kRelease || releasing_; }
   float level() const { return level_; }
   Stage stage() const { return stage_; }
   // The level the release started from.
   float releaseFrom() const { return from_; }
   // Shape mode: where it is in the cycle, 0..1.
   float phase() const { return t_; }
   // Shape mode: how far through all its cycles, 0..1 (0 while waiting), and how many times it has
   // started, so a chain can tell one run from the next.
   float progress() const {
      if (stage_ == kDone)
         return 1.0f;
      if (stage_ != kShape)
         return 0.0f;
      const int n = settings_.held || settings_.repeats <= 0 ? 1 : settings_.repeats;
      return std::min(1.0f, (static_cast<float>(cycles_ % n) + t_) / static_cast<float>(n));
   }
   uint32_t runs() const { return runs_; }

   // The contour at `x` (0..1 of a cycle) with the settings' warp, bend and direction.
   float shapeLevel(float x) {
      const Settings &s = settings_;
      if (!s.table || s.points <= 0)
         return 0.0f;
      if (s.reverse)
         x = 1.0f - x;
      // Warp + bows time so the contour comes later, - so it comes sooner.
      x = warp_(std::clamp(x, 0.0f, 1.0f), -0.75f * s.warp);
      const float pos = x * static_cast<float>(s.points);
      const int i = std::min(static_cast<int>(pos), s.points - 1);
      const float y = s.table[i] + (s.table[i + 1] - s.table[i]) * (pos - static_cast<float>(i));
      return bend_(y, 0.75f * s.bend);
   }

   // Advances by `dt` seconds.
   float process(float dt) {
      const Settings &s = settings_;
      if (s.shapeMode)
         return processShape(dt);
      switch (stage_) {
      case kIdle:
         break;
      case kDelay:
         t_ += dt;
         if (t_ >= s.delay) {
            t_ = 0;
            start_ = level_;
            stage_ = kAttack;
         }
         break;
      case kAttack: {
         // Progress is kept as a fraction, so a modulated time changes the rate, never the position.
         t_ = s.attack <= 0.0f ? 1.0f : std::min(1.0f, t_ + dt / s.attack);
         const float x = t_;
         level_ = start_ + (1.0f - start_) * shape_(x, -0.5f + s.attackCurve);
         if (x >= 1.0f) {
            level_ = 1.0f;
            t_ = 0;
            stage_ = kHold;
         }
         break;
      }
      case kHold:
         t_ += dt;
         if (t_ >= s.hold) {
            t_ = 0;
            stage_ = kDecay;
         }
         break;
      case kDecay: {
         t_ = s.decay <= 0.0f ? 1.0f : std::min(1.0f, t_ + dt / s.decay);
         const float x = t_;
         level_ = 1.0f - (1.0f - s.sustain) * shape_(x, 0.6f + 0.4f * s.decayCurve);
         if (x >= 1.0f) {
            t_ = 0;
            if (s.loop != 0) {
               start_ = level_;
               stage_ = kAttack;
            }
            else
               stage_ = kSustain;
         }
         break;
      }
      case kSustain:
         // Follows sustain changes smoothly.
         level_ += (s.sustain - level_) * std::min(1.0f, dt * 300.0f);
         if (s.loop != 0) {
            start_ = level_;
            stage_ = kAttack;
         }
         break;
      case kRelease: {
         t_ = s.release <= 0.0f ? 1.0f : std::min(1.0f, t_ + dt / s.release);
         const float x = t_;
         level_ = from_ * (1.0f - shape_(x, 0.6f + 0.4f * s.releaseCurve));
         if (x >= 1.0f) {
            level_ = 0;
            stage_ = kIdle;
         }
         break;
      }
      default:
         // Shape stages left over from a switch to ADSR: let it go from where it is.
         from_ = level_;
         t_ = 0;
         stage_ = gate_ ? kSustain : kRelease;
         break;
      }
      return level_;
   }

private:
   // 0..1 to 0..1, bowed: c > 0 rises fast then slows (the RC shape decays have), c < 0 the reverse.
   // The divisor only changes with the curve, so it is kept between calls.
   struct Bow {
      float k = 0, scale = 1;
      float operator()(float x, float c) {
         c = std::clamp(c, -0.95f, 0.95f);
         if (std::fabs(c) < 1e-3f)
            return x;
         const float kk = c * 8.0f;
         if (kk != k) {
            k = kk;
            scale = 1.0f / (1.0f - std::exp(-kk));
         }
         return (1.0f - std::exp(-kk * x)) * scale;
      }
   };

   float processShape(float dt) {
      const Settings &s = settings_;
      switch (stage_) {
      case kShape: {
         t_ += dt / std::max(s.time, 1e-3f);
         if (t_ >= 1.0f) {
            ++cycles_;
            const bool last = s.held ? !gate_ : s.repeats > 0 && cycles_ >= s.repeats;
            if (last) {
               t_ = 1.0f;
               stage_ = kDone;
            }
            else
               t_ -= std::floor(t_);
         }
         contour_ = shapeLevel(t_);
         break;
      }
      case kWait:
      case kDone:
         break;
      case kIdle:
         return level_;
      default:
         // ADSR stages left over from a switch to Shape: pick the contour up from here.
         trigger();
         contour_ = shapeLevel(t_);
         break;
      }
      if (releasing_) {
         releaseT_ = s.release <= 0.0f ? 1.0f : std::min(1.0f, releaseT_ + dt / s.release);
         gain_ = 1.0f - shape_(releaseT_, 0.6f + 0.4f * s.releaseCurve);
         if (releaseT_ >= 1.0f) {
            reset();
            return level_;
         }
      }
      level_ = (stage_ == kWait ? level_ : contour_ * gain_);
      if (stage_ == kWait)
         contour_ = level_;
      return level_;
   }

   // The first place in the cycle where the contour passes `level`, or the top if it never does.
   float phaseAtLevel(float level) {
      constexpr int n = 256;
      float prev = shapeLevel(0.0f);
      for (int i = 1; i <= n; ++i) {
         const float x = static_cast<float>(i) / n, y = shapeLevel(x);
         if ((prev - level) * (y - level) <= 0.0f && prev != y)
            return x - (1.0f / n) * (y - level) / (y - prev);
         prev = y;
      }
      return 0.0f;
   }

   Bow shape_, warp_, bend_;

   Settings settings_;
   Stage stage_ = kIdle;
   float level_ = 0, start_ = 0, from_ = 0, t_ = 0;
   // Shape mode: the contour before the release fade, the fade, and how many cycles have played.
   float contour_ = 0, gain_ = 1, releaseT_ = 0;
   int cycles_ = 0;
   uint32_t runs_ = 0;
   bool gate_ = false, releasing_ = false;
};

// ---- LFO ----

// Six shapes (kLfoShapeNames order), -1..1, with a fade-in delay.
class Lfo {
public:
   enum Shape { kSine, kTriangle, kSaw, kSquare, kSampleHold, kSmooth };

   void seed(uint32_t s) {
      seed_ = s;
      rng_.seed(s);
      held_ = rng_.bipolar();
      next_ = rng_.bipolar();
      cycle_ = INT64_MIN;
   }
   void restart(float phase) {
      phase_ = phase - std::floor(phase);
      age_ = 0;
   }
   float phase() const { return phase_; }

   // Advances by dt seconds at `hz`; returns -1..1 (times the delay's fade-in).
   float process(float dt, float hz, int shape, float delay) {
      age_ += dt;
      phase_ += hz * dt;
      if (phase_ >= 1.0f) {
         phase_ -= std::floor(phase_);
         held_ = next_;
         next_ = rng_.bipolar();
      }
      cycle_ = INT64_MIN;
      return at(shape, delay);
   }

   // Held to a timeline instead: `cycles` is how many cycles in it is (the host's beats over the
   // division's, plus Phase), so it lands on the same place in the bar however playback got there.
   // Its random values are drawn from the cycle's number, so a looped bar repeats them.
   float lock(float dt, double cycles, int shape, float delay) {
      age_ += dt;
      const double whole = std::floor(cycles);
      phase_ = static_cast<float>(cycles - whole);
      if (phase_ >= 1.0f)
         phase_ = 0.0f;
      const int64_t c = static_cast<int64_t>(whole);
      if (c != cycle_) {
         held_ = draw(c);
         next_ = draw(c + 1);
         cycle_ = c;
      }
      return at(shape, delay);
   }

private:
   float at(int shape, float delay) const {
      float v = 0;
      const float p = phase_;
      switch (shape) {
      default:
      case kSine:
         v = Sine::at(p);
         break;
      case kTriangle:
         v = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
         break;
      case kSaw:
         v = 2.0f * p - 1.0f;
         break;
      case kSquare:
         v = p < 0.5f ? 1.0f : -1.0f;
         break;
      case kSampleHold:
         v = held_;
         break;
      case kSmooth: {
         // Cosine-interpolated random, so it glides between values.
         const float t = 0.5f - 0.5f * Sine::at(0.25f + 0.5f * p);
         v = held_ + (next_ - held_) * t;
         break;
      }
      }
      const float fade = delay <= 0.0f ? 1.0f : std::min(1.0f, age_ / delay);
      return v * fade;
   }
   // -1..1 for cycle `c`, the same every time for this seed.
   float draw(int64_t c) const {
      uint64_t z = (static_cast<uint64_t>(seed_) << 32) ^ static_cast<uint64_t>(c);
      z += 0x9e3779b97f4a7c15ull;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      z ^= z >> 31;
      return static_cast<float>(static_cast<uint32_t>(z >> 32)) / 2147483648.0f - 1.0f;
   }

   Random rng_;
   uint32_t seed_ = 0;
   int64_t cycle_ = INT64_MIN;
   float phase_ = 0, age_ = 0, held_ = 0, next_ = 0;
};

// ---- oversampling ----

// A polyphase IIR half-band decimator, 2:1: two chains of first-order allpasses, as in Laurent de
// Soras' HIIR (8 coefficients for a 0.05 transition band: flat to 0.45 of the output rate and over
// 100 dB down past 0.55). It has no latency to report, only a small phase delay near the top.
class HalfBand {
public:
   void reset() {
      for (int i = 0; i < kCount; ++i)
         x_[i] = y_[i] = 0;
   }
   // Two input samples in, oldest first; one out.
   float process(float in0, float in1) {
      float a = in1, b = in0;
      for (int i = 0; i < kCount; i += 2) {
         const float ya = (a - y_[i]) * kCoefs[i] + x_[i];
         x_[i] = a;
         y_[i] = a = ya;
         const float yb = (b - y_[i + 1]) * kCoefs[i + 1] + x_[i + 1];
         x_[i + 1] = b;
         y_[i + 1] = b = yb;
      }
      return 0.5f * (a + b);
   }

private:
   static constexpr int kCount = 8;
   static constexpr float kCoefs[kCount] = {0.0358327884f, 0.1340901419f, 0.2720401434f, 0.4243248713f,
                                            0.5720571972f, 0.7062921421f, 0.8271247620f, 0.9415030942f};
   float x_[kCount] = {}, y_[kCount] = {};
};

// ---- small helpers ----

inline float softClip(float x) { return fastTanh(x); }
inline float hardClip(float x) { return std::clamp(x, -1.0f, 1.0f); }
// Asymmetric, like a tube stage: even harmonics, and its DC removed by the caller's filter.
inline float tube(float x) { return x >= 0.0f ? fastTanh(x) : fastTanh(0.6f * x) / 0.6f * 0.8f; }

} // namespace jsynth::dsp

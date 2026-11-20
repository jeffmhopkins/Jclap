#pragma once

// J2D2's sound: no Visage, no CLAP, so the tests can drive it directly.
//
// Every held note is a Voice with two halves, mixed by the DROID <-> DRONE morph:
//
//   DROID  A Speaker that talks in phrases: a rapid collage of short, hard-gated events drawn from
//          eight measured kinds (chirp, bell sweep, glide, sample-and-hold steps, audio-rate warble,
//          buzz block, ring crossing, tweet), shaped by the Personality XY (energy, feeling). Mood
//          sets the tempo, register, spread, which way pitches go, brightness, vibrato, droop, drive
//          and which kinds get picked, and ends a phrase with a question, a sigh or a squeal.
//          Velocity and pressure open its filter and push its drive.
//   DRONE  Five layers on the note, tuned in just intonation against the lowest held note:
//          TONE (detuned saws and a sine body under a slowly moving lowpass), PARTIALS (a cloud of
//          harmonics that bloom and fade and drift across the field), AIR (noise through narrow
//          resonators at the note's harmonics), METAL (bell modes) and SUB (an octave under the
//          lowest note). ATONAL moves PARTIALS and METAL from pure to a bounded inharmonic far end.
//
// The DROID <-> DRONE ribbon is an equal-power crossfade: droid only at the left end, an even mix
// in the middle, drone only at the right. Between the ends both sound: phrases on new notes, and
// pressure on a held note pops bleeps through the drone. Letting go of a key cuts its droid voice
// within a few milliseconds. The droid half goes through a small "speaker" (band-limited and gently
// clipped); then everything goes through a room and the master.
//
// The numbers here come from the sketch engine in the project files (j2d2_atoms.py and
// j2d2_drone.py), which was matched to spectrograms of real droid clips.

#include <jclap/dsp/filter.h>
#include <jclap/dsp/math.h>
#include <jclap/dsp/reverb.h>
#include <jclap/dsp/stereo_delay.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "params.h"

namespace j2d2 {

using jclap::dsp::kPi;
constexpr double kTwoPi = 2.0 * kPi;

inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
inline double smooth01(double x) {
   x = clamp01(x);
   return x * x * (3.0 - 2.0 * x);
}

// xorshift64*: small, fast, and the same everywhere, so tests can rely on a seed.
struct Rng {
   uint64_t s = 0x9E3779B97F4A7C15ull;
   void seed(uint64_t v) { s = v * 0x2545F4914F6CDD1Dull + 0x9E3779B97F4A7C15ull; }
   uint64_t next() {
      s ^= s >> 12;
      s ^= s << 25;
      s ^= s >> 27;
      return s * 0x2545F4914F6CDD1Dull;
   }
   double uni() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
   double range(double a, double b) { return a + (b - a) * uni(); }
   int integer(int lo, int hiExclusive) { return lo + static_cast<int>(uni() * (hiExclusive - lo)); }
   bool chance(double p) { return uni() < p; }
   double normal() {
      const double u = std::max(1e-12, uni()), v = uni();
      return std::sqrt(-2.0 * std::log(u)) * std::cos(kTwoPi * v);
   }
};

// A sine table, so the many partials cost a lookup each.
struct SineTable {
   static constexpr int kSize = 4096;
   float t[kSize + 1];
   SineTable() {
      for (int i = 0; i <= kSize; ++i)
         t[i] = static_cast<float>(std::sin(kTwoPi * i / kSize));
   }
};
inline const SineTable &sineTable() {
   static const SineTable table;
   return table;
}
// sin(2 pi phase), phase in 0..1.
inline float sine01(double phase) {
   const SineTable &st = sineTable();
   const double x = (phase - std::floor(phase)) * SineTable::kSize;
   const int i = static_cast<int>(x);
   const float f = static_cast<float>(x - i);
   return st.t[i] + f * (st.t[i + 1] - st.t[i]);
}

// Slow smooth random motion: about `rate` new targets a second, eased between with a cosine.
// Returns 0..1.
struct Walk {
   double a = 0.5, b = 0.5, x = 0.0;
   void init(Rng &r) {
      a = r.uni();
      b = r.uni();
      x = r.uni();
   }
   double tick(double dt, double rate, Rng &r) {
      x += rate * dt;
      while (x >= 1.0) {
         x -= 1.0;
         a = b;
         b = r.uni();
      }
      const double s = 0.5 - 0.5 * std::cos(kPi * x);
      return a + (b - a) * s;
   }
};

// ------------------------------------------------------------------------------------------------
// Mood: the Personality XY mapped to how the droid talks. A continuous map, so any point between the
// named moods blends them.

struct Mood {
   double tempo = 1, up = 0.45, spread = 5, centre = 90, bright = 0.35, vib = 0, vrate = 6, bend = 0, contour = 0,
          range = 1, drive = 1, jitter = 0;
   double weights[kSyllableCount] = {};
   double gapMax = 0.03, pause = 0.03;
   enum Tail { kNoTail, kRise, kFall, kBurst } tail = kNoTail;
};

inline Mood moodFor(double e, double f) {
   e = std::clamp(e, -1.0, 1.0);
   f = std::clamp(f, -1.0, 1.0);
   const double pos = std::max(0.0, f), neg = std::max(0.0, -f), hi = std::max(0.0, e), lo = std::max(0.0, -e);
   Mood m;
   m.weights[kChirp] = 2.0 + 1.5 * hi;
   m.weights[kBell] = 0.3 + 3.0 * pos * (0.4 + hi);
   m.weights[kGlide] = 1.0 + 3.0 * lo;
   m.weights[kSteps] = 2.0 + 2.0 * pos + 1.0 * hi;
   m.weights[kWarble] = 0.6 + 4.0 * neg * hi + 1.5 * hi * pos;
   m.weights[kBuzz] = 0.3 + 4.5 * neg * (1 - hi) * (1 - 0.7 * lo);
   m.weights[kRing] = 0.3 + 2.5 * neg * (1 - lo);
   m.weights[kTweet] = 0.5 + 1.5 * pos * (1 - hi) + 1.0 * lo;
   m.tempo = std::pow(2.0, -0.75 * e);                         // excited: 0.6x durations; sad: 1.7x
   m.up = std::clamp(0.45 + 0.4 * f + 0.1 * e, 0.08, 0.92);   // which way pitches go
   m.spread = 5.0 + 3.0 * hi - 2.0 * lo;                       // excited leaps; sad stays narrow
   m.centre = 90.0 + 3.0 * e + 2.5 * f;
   m.bright = clamp01(0.35 + 0.3 * hi + 0.35 * neg * (1 - lo) - 0.3 * lo);
   m.vib = 0.6 * neg * hi + 0.35 * lo * neg;                   // worried trembles, sad wavers
   m.vrate = 6.0 + 3.0 * e;
   m.bend = -2.5 * lo * (0.5 + neg) + 1.0 * hi * pos;          // sad droops at the end of every sound
   m.contour = std::clamp(f * 0.9, -0.9, 0.9);                 // step runs climb when happy, sink when sad
   m.range = std::clamp(1.0 + 0.45 * e + 0.2 * f, 0.4, 1.6);
   m.drive = 1.0 + 3.5 * neg * (1 - lo) * (1 - 0.5 * hi);      // grumpy is harsh
   m.jitter = 0.5 * neg * hi;
   m.gapMax = 0.01 + 0.07 * lo + 0.02 * (1 - hi);
   m.pause = 0.03 + 0.25 * lo;
   m.tail = (f > 0.15 && e < 0.6) ? Mood::kRise : (f < -0.3 && e < 0.2) ? Mood::kFall : (e > 0.6 && f > 0.3) ? Mood::kBurst : Mood::kNoTail;
   return m;
}

// ------------------------------------------------------------------------------------------------
// One droid sound: a pitch path under a hard gate, synthesised a sample at a time.

enum Timbre { kSine, kSoft, kBright };
enum EventKind { kEvChirp, kEvBell, kEvGlide, kEvSteps, kEvWarble, kEvBuzz, kEvRing, kEvTweet, kEvRise, kEvFall, kEvNone };

struct Event {
   int kind = kEvNone;
   int n = 0, i = 0;        // length and position, samples
   int attack = 1, release = 1;
   double m = 90, oct = 0, dir = 1, tau = 0.3, peak = 0.5, endBend = 0;
   double shown = 90; // the pitch sounding now, as a MIDI note, for the panel
   int timbre = kSine;
   double amp = 1.0;
   double ph = 0.0, ph2 = 0.0;
   // Steps: up to 9 pitches, in one or two voices, each held stepLen samples, slewed over 2 ms.
   int steps = 0, stepLen = 1;
   bool two = false;
   float stepPitch[2][9] = {};
   double slewF[2] = {0, 0};
   // Warble: an audio-rate LFO on the pitch.
   double rate = 60, rateDrift = 0, depth = 0.5, lfo = 0;
   // Buzz: a narrow pulse, differentiated.
   double f0 = 200, duty = 0.12, wobRate = 10, prev = 0;
   // Ring: two sines multiplied, one sweeping.
   double mb = 76;
   // Tweet: a wandering pitch, chopped by a fast AM.
   float pts[24] = {};
   int nPts = 0;
   double tweetSm = 0, amRate = 10, amSm = 0;
   // The mood's touch on every sound: vibrato, droop or lift, and jitter.
   double vib = 0, vrate = 6, vph = 0, bend = 0, jitter = 0;
   float jit[32] = {};
   int nJit = 0;

   bool active() const { return kind != kEvNone && i < n; }
};

// The band-limited oscillator the droid sounds use: a sine, a slightly coloured whistle, or the
// measured bright stack (H2 -10, H3 -14, H4 -18 dB: about 1/h^1.5).
inline float droidOsc(double ph, double hz, int timbre, double sr) {
   const double x = kTwoPi * ph;
   const double s1 = std::sin(x);
   if (timbre == kSine)
      return static_cast<float>(s1 + 0.01 * std::sin(2 * x));
   const double c1 = std::cos(x);
   if (timbre == kSoft) {
      const double s2 = 2 * s1 * c1, c2 = c1 * c1 - s1 * s1;
      const double s3 = s2 * c1 + c2 * s1;
      return static_cast<float>(s1 + 0.05 * s2 + 0.03 * s3);
   }
   double s = s1, c = c1, out = s1;
   const int top = std::min(24, static_cast<int>(0.45 * sr / std::max(1.0, hz)));
   for (int h = 2; h <= top; ++h) {
      const double sn = s * c1 + c * s1, cn = c * c1 - s * s1;
      s = sn;
      c = cn;
      out += s * std::pow(static_cast<double>(h), -1.5);
   }
   return static_cast<float>(out / 1.6);
}

// The droid voice of one note.
class Speaker {
public:
   void prepare(double sr) { sr_ = sr; }
   void reset() {
      ev_ = Event{};
      inPhrase_ = false;
      wait_ = 0;
      left_ = 0;
      tailDone_ = true;
      fade_ = 1.0;
      fadeStep_ = 0.0;
      cap_ = elapsed_ = 0;
   }
   void seed(uint64_t s) { rng_.seed(s); }

   // The key was let go: whatever is playing fades out over a few milliseconds (no click), and the
   // rest of the phrase is dropped.
   void stop() {
      if (busy() && fadeStep_ == 0.0)
         fadeStep_ = 1.0 / std::max(1.0, kStopSeconds * sr_);
   }
   bool stopping() const { return fadeStep_ > 0.0; }

   // Starts a phrase `seconds` long in `mood`, with the register offset in semitones. It never runs
   // past `seconds`: the ending (a question, sigh or squeal) is fitted inside it, and a short phrase
   // (under 0.4 s) has no ending, so a short LENGTH gives one or two crisp sounds.
   void phrase(const Mood &mood, double seconds, double reg, const double *mix) {
      mood_ = mood;
      reg_ = reg;
      mix_ = mix;
      inPhrase_ = true;
      const bool ending = seconds >= 0.4 && mood.tail != Mood::kNoTail;
      tailDone_ = !ending;
      const double tailSeconds = !ending                      ? 0.0
                                 : mood.tail == Mood::kFall  ? 0.7 * std::sqrt(mood.tempo)
                                 : mood.tail == Mood::kBurst ? 0.12
                                                             : 0.28 * mood.tempo;
      left_ = static_cast<int>(std::max(0.0, seconds - tailSeconds) * sr_);
      cap_ = static_cast<int>(seconds * sr_);
      elapsed_ = 0;
      wait_ = 0;
      prev_ = -1;
      fade_ = 1.0;
      fadeStep_ = 0.0;
   }
   // One syllable, about `seconds` long: a kind picked by the mood and the SYLLABLES mix, stretched
   // or squeezed to the length. What one key press speaks when PER KEY is SYLLABLE.
   void syllable(const Mood &mood, double seconds, double reg, const double *mix) {
      // Each kind's natural length at tempo 1 (the middle of its range in make()).
      static const double kNatural[kSyllableCount] = {0.19, 0.22, 0.30, 0.42, 0.11, 0.105, 0.25, 0.65};
      // (Steps divide the length among their steps instead.)
      mood_ = mood;
      reg_ = reg;
      mix_ = mix;
      reset();
      double w[kSyllableCount], sum = 0;
      for (int k = 0; k < kSyllableCount; ++k)
         sum += (w[k] = mood_.weights[k] * (mix_ ? mix_[k] : 1.0));
      if (sum <= 1e-9)
         return;
      double x = rng_.uni() * sum;
      int k = 0;
      for (; k < kSyllableCount - 1 && x >= w[k]; ++k)
         x -= w[k];
      stretch_ = seconds / (kNatural[k] * mood_.tempo);
      target_ = seconds;
      make(k);
      stretch_ = 1.0;
      target_ = 0.0;
      ev_.amp = rng_.range(0.85, 1.0);
   }
   // One sound now, unless one is already playing (pressure popping bleeps through a drone).
   bool pop(const Mood &mood, double reg, int kind) {
      if (ev_.active() || inPhrase_)
         return false;
      mood_ = mood;
      reg_ = reg;
      fade_ = 1.0;
      fadeStep_ = 0.0;
      cap_ = 0;
      make(kind);
      return true;
   }
   bool busy() const { return inPhrase_ || ev_.active(); }
   // Moves everything it says by `semitones` (the note's pitch bend, and DEGRADE's wow), live.
   void setPitchOffset(double semitones) { pitchMul_ = std::pow(2.0, semitones / 12.0); }
   int kind() const { return ev_.active() ? ev_.kind : kEvNone; }
   double pitch() const { return ev_.shown; }

   float tick() {
      if (cap_ > 0 && ++elapsed_ >= cap_)
         stop();
      if (fadeStep_ > 0.0) {
         fade_ -= fadeStep_;
         if (fade_ <= 0.0) {
            reset();
            return 0.0f;
         }
         return static_cast<float>(fade_ * fade_) * play();
      }
      return play();
   }

private:
   static constexpr double kStopSeconds = 0.008;

   float play() {
      if (!ev_.active()) {
         if (!inPhrase_)
            return 0.0f;
         if (wait_ > 0) {
            --wait_;
            left_ -= 1;
            return 0.0f;
         }
         if (left_ > 0)
            next();
         else if (!tailDone_) {
            tailDone_ = true;
            if (!tail()) {
               inPhrase_ = false;
               return 0.0f;
            }
         } else {
            inPhrase_ = false;
            return 0.0f;
         }
      }
      const float y = render();
      if (!ev_.active() && inPhrase_) {
         // The gap to the next sound, sometimes a pause; now and then they overlap a little.
         wait_ = static_cast<int>(rng_.range(0.0, mood_.gapMax) * sr_);
         if (rng_.chance(mood_.pause))
            wait_ += static_cast<int>(rng_.range(0.12, 0.35) * mood_.tempo * sr_);
      }
      // Drive, per mood: grumpy is harsh.
      const double g = mood_.drive;
      return g > 1.05 ? static_cast<float>(std::tanh(g * y) / std::tanh(g)) : y;
   }

   // A duration in [a, b] seconds at the mood's tempo; the middle, stretched, when a syllable has a
   // set length.
   double D(double a, double b) { return target_ > 0.0 ? 0.5 * (a + b) * mood_.tempo * stretch_ : rng_.range(a, b) * mood_.tempo; }
   double note() { return std::clamp(mood_.centre + reg_ + rng_.normal() * mood_.spread, 60.0, 112.0); }
   int timbre(double bias = 0.0) { return rng_.chance(mood_.bright + bias) ? kBright : kSoft; }
   bool up() { return rng_.chance(mood_.up); }

   void next() {
      double w[kSyllableCount], sum = 0;
      for (int k = 0; k < kSyllableCount; ++k)
         sum += (w[k] = mood_.weights[k] * (mix_ ? mix_[k] : 1.0));
      if (sum <= 1e-9) {
         left_ = 0;
         return;
      }
      int k = 0;
      for (int tries = 0; tries < 4; ++tries) {
         double x = rng_.uni() * sum;
         for (k = 0; k < kSyllableCount - 1 && x >= w[k]; ++k)
            x -= w[k];
         if (k != prev_ || !rng_.chance(0.6))
            break;
      }
      prev_ = k;
      make(k);
      ev_.amp = rng_.range(0.6, 1.0);
      left_ -= ev_.n;
   }

   void begin(int kind, double seconds, double attack = 0.003, double release = 0.006) {
      ev_ = Event{};
      ev_.kind = kind;
      ev_.n = std::max(8, static_cast<int>(seconds * sr_));
      ev_.attack = std::max(1, static_cast<int>(attack * sr_));
      ev_.release = std::max(1, static_cast<int>(release * sr_));
      ev_.ph = rng_.uni();
      ev_.ph2 = rng_.uni();
      ev_.amp = 1.0;
      ev_.vib = mood_.vib;
      ev_.vrate = mood_.vrate * (1 + 0.15 * rng_.normal());
      ev_.vph = rng_.uni();
      ev_.bend = mood_.bend;
      ev_.jitter = mood_.jitter;
      if (ev_.jitter > 0) {
         ev_.nJit = std::clamp(static_cast<int>(seconds * 25) + 3, 3, 32);
         for (int j = 0; j < ev_.nJit; ++j)
            ev_.jit[j] = static_cast<float>(rng_.normal());
      }
   }

   void make(int kind) {
      switch (kind) {
      case kChirp:
         begin(kEvChirp, D(0.08, 0.30));
         ev_.m = note() + 2;
         ev_.oct = rng_.range(0.4, 1.1) * mood_.range;
         ev_.dir = up() ? 1 : -1;
         ev_.tau = rng_.range(0.15, 0.5);
         ev_.timbre = timbre(0.35);
         break;
      case kBell:
         begin(kEvBell, D(0.12, 0.32), 0.004, 0.01);
         ev_.m = note() - 1;
         ev_.oct = rng_.range(0.8, 2.0) * mood_.range;
         ev_.peak = rng_.range(0.35, 0.6);
         ev_.endBend = rng_.range(-0.4, 0.2) + (up() ? 0.5 : -0.5) * std::fabs(mood_.contour);
         ev_.timbre = timbre();
         break;
      case kGlide:
         begin(kEvGlide, D(0.15, 0.45), 0.004, 0.012);
         ev_.m = note() - 3;
         ev_.oct = rng_.range(0.3, 1.2) * mood_.range * (up() ? 1 : -1);
         ev_.timbre = timbre();
         break;
      case kSteps: {
         const int k = rng_.integer(3, 9);
         const bool two = rng_.chance(0.45);
         const double len = target_ > 0.0 ? target_ / k : D(0.03, 0.11);
         begin(kEvSteps, len * k);
         ev_.steps = k;
         ev_.stepLen = std::max(1, static_cast<int>(len * sr_));
         ev_.n = ev_.stepLen * k;
         ev_.two = two;
         int order = 0;
         if (rng_.chance(std::fabs(mood_.contour)))
            order = mood_.contour > 0 ? 1 : -1;
         for (int v = 0; v < (two ? 2 : 1); ++v) {
            for (int j = 0; j < k; ++j)
               ev_.stepPitch[v][j] = static_cast<float>(note() - (v ? 8 : 0));
            if (order > 0)
               std::sort(ev_.stepPitch[v], ev_.stepPitch[v] + k);
            else if (order < 0)
               std::sort(ev_.stepPitch[v], ev_.stepPitch[v] + k, [](float a, float b) { return a > b; });
            ev_.slewF[v] = mtof(ev_.stepPitch[v][0]);
         }
         ev_.m = ev_.stepPitch[0][0];
         ev_.timbre = kSine;
         break;
      }
      case kWarble:
         begin(kEvWarble, D(0.06, 0.16));
         ev_.m = note() + 3;
         ev_.rate = rng_.range(45, 90) / std::sqrt(mood_.tempo);
         ev_.rateDrift = rng_.range(-0.3, 0.3);
         ev_.depth = rng_.range(0.35, 0.75) * std::sqrt(mood_.range);
         ev_.dir = up() ? 1 : -1;
         ev_.timbre = timbre(-0.2);
         break;
      case kBuzz:
         begin(kEvBuzz, D(0.06, 0.15), 0.002, 0.004);
         ev_.f0 = rng_.range(150, 350) * std::pow(2.0, (reg_ + mood_.centre - 90) / 24.0);
         ev_.dir = up() ? 1 : -1;
         ev_.wobRate = rng_.range(8, 20);
         ev_.duty = rng_.range(0.06, 0.2);
         ev_.m = 69 + 12 * std::log2(ev_.f0 / 440.0);
         ev_.amp = 0.8;
         break;
      case kRing:
         begin(kEvRing, D(0.15, 0.35), 0.004, 0.01);
         ev_.m = note() - 4;
         ev_.mb = note() - 14;
         ev_.oct = rng_.range(1.0, 2.2);
         ev_.dir = up() ? 1 : -1;
         ev_.timbre = kSine;
         break;
      case kTweet: {
         const double d = D(0.4, 0.9);
         begin(kEvTweet, d);
         ev_.m = note() + 2;
         ev_.nPts = std::clamp(static_cast<int>(d * rng_.range(5, 9)) + 3, 3, 24);
         for (int j = 0; j < ev_.nPts; ++j)
            ev_.pts[j] = static_cast<float>(rng_.normal() * 1.5 * mood_.range);
         ev_.tweetSm = ev_.pts[0];
         ev_.dir = up() ? 1 : -1;
         ev_.amRate = rng_.range(8, 12) / mood_.tempo;
         ev_.timbre = kSoft;
         break;
      }
      default:
         break;
      }
   }

   // The end of a phrase: a question rising, a sigh falling, or a squeal up.
   bool tail() {
      switch (mood_.tail) {
      case Mood::kRise:
         begin(kEvRise, 0.28 * mood_.tempo);
         ev_.m = mood_.centre + reg_ - 2;
         ev_.timbre = kSoft;
         ev_.amp = 0.8;
         return true;
      case Mood::kFall:
         begin(kEvFall, 0.7 * std::sqrt(mood_.tempo), 0.01, 0.15);
         ev_.m = mood_.centre + reg_ - 2;
         ev_.timbre = kSoft;
         ev_.amp = 0.8;
         return true;
      case Mood::kBurst:
         begin(kEvChirp, 0.12);
         ev_.m = mood_.centre + reg_ + 4;
         ev_.oct = 1.2;
         ev_.dir = 1;
         ev_.tau = rng_.range(0.15, 0.5);
         ev_.timbre = timbre(0.35);
         ev_.amp = 0.8;
         return true;
      default:
         return false;
      }
   }

   // The current sound's next sample.
   float render() {
      Event &e = ev_;
      const double t = static_cast<double>(e.i) / e.n, sec = e.i / sr_;
      // The mood's touch, in semitones.
      double mod = 0.0;
      if (e.vib != 0.0 || e.bend != 0.0 || e.jitter != 0.0) {
         mod = e.vib * std::sin(kTwoPi * (e.vph + e.vrate * sec)) + e.bend * t;
         if (e.nJit > 1) {
            const double x = t * (e.nJit - 1);
            const int j = std::min(static_cast<int>(x), e.nJit - 2);
            mod += e.jitter * (e.jit[j] + (x - j) * (e.jit[j + 1] - e.jit[j]));
         }
      }
      double hz = 440.0;
      double y = 0.0;
      switch (e.kind) {
      case kEvChirp: {
         const double shape = (1 - std::exp(-t / e.tau)) / (1 - std::exp(-1 / e.tau));
         hz = mtof(e.m + mod) * std::pow(2.0, e.dir * e.oct * shape);
         break;
      }
      case kEvBell: {
         const double w = t < e.peak ? t / e.peak : (1 - t) / (1 - e.peak);
         hz = mtof(e.m + mod) * std::pow(2.0, e.oct * std::pow(std::sin(kPi / 2 * w), 1.6) + e.endBend * t);
         break;
      }
      case kEvGlide: {
         const double s = t * t * (3 - 2 * t);
         hz = mtof(e.m + mod) * std::pow(2.0, e.oct * s);
         break;
      }
      case kEvSteps: {
         const int j = std::min(e.i / e.stepLen, e.steps - 1);
         const double a = std::exp(-1.0 / (0.002 * sr_));
         double sum = 0.0;
         for (int v = 0; v < (e.two ? 2 : 1); ++v) {
            e.slewF[v] = a * e.slewF[v] + (1 - a) * mtof(e.stepPitch[v][j]);
            double &ph = v ? e.ph2 : e.ph;
            const double f = std::min(0.45 * sr_, e.slewF[v] * std::pow(2.0, mod / 12.0) * pitchMul_);
            ph += f / sr_;
            ph -= std::floor(ph);
            sum += droidOsc(ph, f, kSine, sr_) * (v ? 0.7 : 1.0);
         }
         e.shown = e.stepPitch[0][j];
         y = sum / (e.two ? 1.4 : 1.0);
         break;
      }
      case kEvWarble: {
         const double rate = e.rate * std::pow(2.0, e.rateDrift * t);
         e.lfo += rate / sr_;
         hz = mtof(e.m + mod) * std::pow(2.0, e.depth * std::sin(kTwoPi * e.lfo) + 0.4 * e.dir * t);
         break;
      }
      case kEvBuzz: {
         const double f = e.f0 * std::pow(2.0, 0.4 * e.dir * t + 0.08 * std::sin(kTwoPi * e.wobRate * sec)) * pitchMul_;
         e.ph += f / sr_;
         e.ph -= std::floor(e.ph);
         // A narrow pulse with polyBLEP edges, minus its mean, then a gentle differentiator.
         const double dt = f / sr_;
         const auto blep = [dt](double p) {
            if (p < dt) {
               p /= dt;
               return p + p - p * p - 1.0;
            }
            if (p > 1.0 - dt) {
               p = (p - 1.0) / dt;
               return p * p + p + p + 1.0;
            }
            return 0.0;
         };
         double p = (e.ph < e.duty ? 1.0 : 0.0) + blep(e.ph);
         double q = e.ph - e.duty;
         q -= std::floor(q);
         p -= blep(q);
         p -= e.duty;
         const double out = p - 0.6 * e.prev;
         e.prev = p;
         y = out * 1.4;
         e.shown = 69 + 12 * std::log2(f / 440.0);
         break;
      }
      case kEvRing: {
         const double fa = std::min(0.45 * sr_, mtof(e.m) * pitchMul_);
         const double fb = std::min(0.45 * sr_, mtof(e.mb) * std::pow(2.0, e.oct * (e.dir > 0 ? t : 1 - t)) * pitchMul_);
         e.ph += fa / sr_;
         e.ph2 += fb / sr_;
         e.ph -= std::floor(e.ph);
         e.ph2 -= std::floor(e.ph2);
         y = std::sin(kTwoPi * e.ph) * std::sin(kTwoPi * e.ph2) * 1.3;
         break;
      }
      case kEvTweet: {
         const double x = t * (e.nPts - 1);
         const int j = std::min(static_cast<int>(x), e.nPts - 2);
         const double target = e.pts[j] + (x - j) * (e.pts[j + 1] - e.pts[j]);
         e.tweetSm += (target - e.tweetSm) * (1.0 - std::exp(-1.0 / (0.007 * sr_)));
         hz = mtof(e.m + e.tweetSm + 2.5 * e.dir * t + mod);
         const double g = std::sin(kTwoPi * e.amRate * sec + 1.0);
         const double am = clamp01(g * 3 + 1.2);
         e.amSm += (am - e.amSm) * (1.0 - std::exp(-1.0 / (0.0006 * sr_)));
         break;
      }
      case kEvRise:
         hz = mtof(e.m + mod) * std::pow(2.0, t * t);
         break;
      case kEvFall:
         hz = mtof(e.m + mod) * std::pow(2.0, -1.3 * std::pow(t, 1.5));
         break;
      default:
         break;
      }
      if (e.kind != kEvSteps && e.kind != kEvBuzz && e.kind != kEvRing) {
         hz = std::min(hz * pitchMul_, 0.45 * sr_);
         e.ph += hz / sr_;
         e.ph -= std::floor(e.ph);
         y = droidOsc(e.ph, hz, e.timbre, sr_);
         if (e.kind == kEvTweet)
            y *= e.amSm;
         e.shown = 69 + 12 * std::log2(std::max(20.0, hz) / 440.0);
      }
      // The hard gate: the measured envelopes are rectangular, with tiny ramps.
      const double gate = std::min(1.0, std::min(static_cast<double>(e.i + 1) / e.attack, static_cast<double>(e.n - e.i) / e.release));
      ++e.i;
      if (e.i >= e.n)
         e.kind = kEvNone;
      return static_cast<float>(y * gate * e.amp);
   }

   double sr_ = 48000.0;
   Rng rng_;
   Mood mood_;
   Event ev_;
   const double *mix_ = nullptr;
   double reg_ = 0.0;
   bool inPhrase_ = false, tailDone_ = true;
   double fade_ = 1.0, fadeStep_ = 0.0, stretch_ = 1.0, target_ = 0.0;
   int cap_ = 0, elapsed_ = 0;
   double pitchMul_ = 1.0;
   int wait_ = 0, left_ = 0, prev_ = -1;
};

// ------------------------------------------------------------------------------------------------
// The drone: five layers on one note.

// Just intonation against the chord's root, in cents from equal temperament, by interval.
inline const double kJustCents[12] = {0, 11.7, 3.9, 15.6, -13.7, -2.0, -9.8, 2.0, 13.7, -15.6, 17.6, -11.7};

// The harmonics PARTIALS draws from: octaves, fifths and major thirds are always consonant with the
// note; the 'blue' ones (7, 9, 11, 13, 14, 15) only fade in as ATONAL rises.
inline const int kPureHarmonics[10] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 16};
inline const int kBlueHarmonics[6] = {7, 9, 11, 13, 14, 15};
// METAL's modes: tuned (a 12th, two octaves and a third, three octaves, and a fifth above), and
// where a free bar's modes sit, the furthest ATONAL ever takes them.
inline const double kMetalPure[4] = {3.0, 5.0, 8.0, 12.0};
inline const double kMetalBar[4] = {2.756, 5.404, 8.933, 13.34};

// A two-pole resonator (TPT state variable band pass), for AIR.
struct Resonator {
   double ic1 = 0, ic2 = 0, g = 0.1, k = 0.01, a1 = 0, a2 = 0, a3 = 0;
   void set(double hz, double q, double sr) {
      g = std::tan(kPi * std::min(hz, 0.45 * sr) / sr);
      k = 1.0 / q;
      a1 = 1.0 / (1.0 + g * (g + k));
      a2 = g * a1;
      a3 = g * a2;
   }
   double process(double v0) {
      const double v3 = v0 - ic2;
      const double v1 = a1 * ic1 + a2 * v3;
      const double v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2 * v1 - ic1;
      ic2 = 2 * v2 - ic2;
      return k * v1;
   }
};

// The drone's motion and timbre controls for one voice, after modulation.
struct DroneControls {
   double level[kLayerCount] = {0.7, 0.6, 0.35, 0.3, 0.55};
   double partialsAtonal = 0.2, metalAtonal = 0.2;
   double evolve = 1, drift = 1, spread = 0.8;
   double cutoffOct = 0.0;   // TONE's lowpass, octaves up or down from where its hand has it
   double pressure = 0.0;    // opens TONE and AIR
};

class Drone {
public:
   static constexpr int kPartials = 12;
   static constexpr int kBlock = 32; // control rate: walks and filters update every 32 samples

   void prepare(double sr) {
      sr_ = sr;
      tone_.setSampleRate(sr);
      tone_.setMode(jclap::dsp::Filter::Mode::LowPass);
      air_.setSampleRate(sr);
      air_.setMode(jclap::dsp::Filter::Mode::LowPass);
      subCoef_ = 1.0 / (0.02 * sr);
   }

   // A new note at `hz`. `seed` makes every note's motion its own. A voice that is still sounding
   // (stolen, or retriggered while releasing) keeps its oscillator phases and filter states, so the
   // new note picks up without a click.
   void start(double hz, uint64_t seed, bool sounding = false) {
      rng_.seed(seed);
      hz_ = hz;
      if (!sounding) {
         tone_.reset();
         air_.reset();
         for (double &p : saw_)
            p = rng_.uni();
         body_ = sub_ = 0.0;
         subMix_ = 0.0;
         levelsSet_ = false; // the first block's levels apply at once
      }
      for (Walk *w : {&toneDrift_, &toneHand_, &airBand_, &subWalk_})
         w->init(rng_);
      // PARTIALS: nine pure harmonics and three blue ones, each with its own bloom, drift and pan.
      int pure[10];
      std::copy(std::begin(kPureHarmonics), std::end(kPureHarmonics), pure);
      for (int i = 9; i > 0; --i)
         std::swap(pure[i], pure[rng_.integer(0, i + 1)]);
      int blue[6];
      std::copy(std::begin(kBlueHarmonics), std::end(kBlueHarmonics), blue);
      for (int i = 5; i > 0; --i)
         std::swap(blue[i], blue[rng_.integer(0, i + 1)]);
      for (int i = 0; i < kPartials; ++i) {
         Partial &p = partials_[i];
         p.h = i < 9 ? pure[i] : blue[i - 9];
         p.pure = i < 9;
         p.stretch = std::min(60.0, 1200.0 * std::log2(1.0 + 0.0004 * p.h * p.h));
         p.detune = std::clamp(rng_.normal() * 10.0, -18.0, 18.0);
         p.threshold = rng_.range(0.15, 0.55);
         p.ampRate = rng_.range(0.05, 0.25);
         if (!sounding)
            p.ph = rng_.uni();
         p.wob.init(rng_);
         p.amp.init(rng_);
         p.pan.init(rng_);
      }
      for (int i = 0; i < 4; ++i) {
         if (!sounding)
            res_[i] = Resonator{};
         resQ_[i] = rng_.range(60, 140);
         resAmp_[i].init(rng_);
      }
      for (int i = 0; i < 4; ++i) {
         Mode &m = metal_[i];
         m.scatter = std::clamp(rng_.normal() * 12.0, -20.0, 20.0);
         if (!sounding)
            m.ph = rng_.uni();
         m.trem = rng_.range(0.2, 1.2);
         m.tremPh = rng_.uni();
         m.ampRate = rng_.range(0.04, 0.15);
         m.wob.init(rng_);
         m.amp.init(rng_);
      }
      counter_ = 0;
   }

   void setPitch(double hz) { hz_ = hz; }

   // One stereo sample of the five layers, before the amp envelope.
   void tick(const DroneControls &c, bool withSub, float &l, float &r) {
      if (counter_-- <= 0) {
         counter_ = kBlock - 1;
         control(c);
         // Layer levels, the sub's swell and METAL's pan glide across the block instead of
         // stepping, and the SUB crossfades over about 20 ms when it moves to another note.
         for (int k = 0; k < kLayerCount; ++k) {
            if (!levelsSet_)
               levelNow_[k] = c.level[k];
            levelStep_[k] = (c.level[k] - levelNow_[k]) / kBlock;
         }
         levelsSet_ = true;
      }
      for (int k = 0; k < kLayerCount; ++k)
         levelNow_[k] += levelStep_[k];
      subMix_ += ((withSub ? 1.0 : 0.0) - subMix_) * subCoef_;
      subAmpNow_ += (subAmp_ - subAmpNow_) * 0.002;
      metalLNow_ += (metalL_ - metalLNow_) * 0.002;
      metalRNow_ += (metalR_ - metalRNow_) * 0.002;
      const double *level = levelNow_;
      const double f0 = hz_;
      double L = 0.0, R = 0.0;
      // TONE: three saws a few cents apart and a sine body, under the lowpass the hand moves.
      if (level[kLayerTone] > 0.0) {
         static const double kDetune[3] = {-0.0022, 0.0, 0.0024};
         double saw = 0.0;
         for (int i = 0; i < 3; ++i) {
            const double dt = toneHz_ * (1.0 + kDetune[i]) / sr_;
            double &p = saw_[i];
            p += dt;
            p -= std::floor(p);
            double s = 2.0 * p - 1.0;
            if (p < dt) {
               const double x = p / dt;
               s -= x + x - x * x - 1.0;
            } else if (p > 1.0 - dt) {
               const double x = (p - 1.0) / dt;
               s -= x * x + x + x + 1.0;
            }
            saw += s;
         }
         body_ += toneHz_ / sr_;
         body_ -= std::floor(body_);
         const double x = 0.6 * saw / 3.0 + 0.5 * sine01(body_);
         const double y = tone_.process(static_cast<float>(x)) * kToneGain * level[kLayerTone];
         L += y * 0.9;
         R += y * 0.9;
      }
      // PARTIALS: each harmonic on its own pan path.
      if (level[kLayerPartials] > 0.0) {
         double pl = 0.0, pr = 0.0;
         for (Partial &p : partials_) {
            if (p.gainNow <= 1e-5 && p.gainStep <= 0.0)
               continue;
            p.ph += p.hz / sr_;
            p.ph -= std::floor(p.ph);
            p.gainNow += p.gainStep;
            p.panNow += p.panStep;
            const double s = sine01(p.ph) * p.gainNow;
            pl += s * std::cos(p.panNow * kPi / 2);
            pr += s * std::sin(p.panNow * kPi / 2);
         }
         L += pl * kPartialsGain * level[kLayerPartials];
         R += pr * kPartialsGain * level[kLayerPartials];
      }
      // AIR: noise through narrow resonators at the note's harmonics, plus a moving band of breath.
      if (level[kLayerAir] > 0.0) {
         const double n = rng_.uni() * 2.0 - 1.0;
         double y = 0.0;
         for (int i = 0; i < 4; ++i) {
            if (resGain_[i] <= 0.0)
               continue;
            resNow_[i] += resStep_[i];
            y += res_[i].process(n) * resNow_[i];
         }
         y += 0.035 * kAirBreath * air_.process(static_cast<float>(n));
         y *= kAirGain * level[kLayerAir];
         // It sits opposite TONE in the field.
         L += y * 0.7;
         R += y * 1.0;
      }
      // METAL: bell modes swelling in and out, each with a little tremolo.
      if (level[kLayerMetal] > 0.0) {
         double y = 0.0;
         for (Mode &m : metal_) {
            m.ph += m.hz / sr_;
            m.ph -= std::floor(m.ph);
            m.tremPh += m.trem / sr_;
            m.gainNow += m.gainStep;
            y += sine01(m.ph) * m.gainNow * (1.0 + 0.25 * sine01(m.tremPh));
         }
         y *= kMetalGain * level[kLayerMetal];
         L += y * metalLNow_;
         R += y * metalRNow_;
      }
      // SUB: an octave under the lowest held note, breathing slowly.
      if (subMix_ > 1e-4 && level[kLayerSub] > 0.0) {
         sub_ += 0.5 * f0 / sr_;
         sub_ -= std::floor(sub_);
         const double y = sine01(sub_) * subAmpNow_ * subMix_ * kSubGain * level[kLayerSub];
         L += y * 0.7;
         R += y * 0.7;
      }
      l = static_cast<float>(L);
      r = static_cast<float>(R);
   }

   // Each layer's fixed gain, so at equal fader settings they come out about equally loud (the
   // sketch normalised each to the same RMS).
   static constexpr double kToneGain = 0.33, kPartialsGain = 0.44, kAirGain = 1.0, kAirBreath = 0.06, kMetalGain = 0.5,
                           kSubGain = 0.275;

private:
   struct Partial {
      int h = 1;
      bool pure = true;
      double stretch = 0, detune = 0, threshold = 0.3, ampRate = 0.1, ph = 0, hz = 0;
      double gainNow = 0, gainStep = 0, panNow = 0.5, panStep = 0;
      Walk wob, amp, pan;
   };
   struct Mode {
      double scatter = 0, ph = 0, hz = 0, trem = 0.5, tremPh = 0, ampRate = 0.1, gainNow = 0, gainStep = 0;
      Walk wob, amp;
   };

   void control(const DroneControls &c) {
      const double dt = kBlock / sr_;
      const double f0 = hz_;
      // TONE drifts a few cents and its hand opens and closes the lowpass.
      toneHz_ = f0 * std::pow(2.0, c.drift * 0.05 * (toneDrift_.tick(dt, 0.15, rng_) - 0.5) / 12.0);
      const double hand = toneHand_.tick(dt, 0.12 * c.evolve, rng_);
      tone_.setCutoff(f0 * std::pow(2.0, 1.4 + 2.2 * hand + c.cutoffOct + 1.5 * c.pressure));
      // PARTIALS.
      const double a = clamp01(c.partialsAtonal);
      for (Partial &p : partials_) {
         const double wob = (p.wob.tick(dt, 0.1, rng_) - 0.5) * c.drift * (4.0 + 30.0 * a);
         const double cents = a * (p.stretch + p.detune) + wob;
         p.hz = f0 * p.h * std::pow(2.0, cents / 1200.0);
         const double gate = p.pure ? 1.0 : smooth01((a - p.threshold) / 0.25);
         double amp = std::pow(p.amp.tick(dt, p.ampRate * c.evolve, rng_), 3.0) / std::pow(p.h, 0.6) * gate;
         if (p.hz > 9000.0 || p.hz > 0.45 * sr_)
            amp = 0.0;
         const double pan = 0.5 + (p.pan.tick(dt, 0.06 * c.evolve, rng_) - 0.5) * c.spread;
         p.gainStep = (amp - p.gainNow) / kBlock;
         p.panStep = (pan - p.panNow) / kBlock;
      }
      // AIR.
      static const int kAirH[4] = {1, 2, 3, 5};
      for (int i = 0; i < 4; ++i) {
         const double fh = f0 * kAirH[i];
         const double amp = 0.1 + 0.9 * std::pow(resAmp_[i].tick(dt, 0.2 * c.evolve, rng_), 1.5);
         // Noise through a band pass comes out in proportion to its bandwidth (f / Q), so each is
         // scaled back to the same loudness.
         resGain_[i] = fh > 8000.0 ? 0.0 : amp / std::pow(kAirH[i], 0.3) * std::sqrt(resQ_[i] / fh) * 28.0;
         res_[i].set(fh, resQ_[i] * (1.0 - 0.5 * c.pressure), sr_);
         resStep_[i] = (resGain_[i] - resNow_[i]) / kBlock;
      }
      air_.setCutoff(f0 * std::pow(2.0, 2.0 + 2.5 * airBand_.tick(dt, 0.1 * c.evolve, rng_) + 1.0 * c.pressure));
      // METAL: each mode slides in log pitch from tuned toward the bar.
      const double m = clamp01(c.metalAtonal);
      for (int i = 0; i < 4; ++i) {
         Mode &md = metal_[i];
         double fold = 1.0;
         while (f0 * kMetalPure[i] / fold > 7000.0)
            fold *= 2.0;
         const double wob = (md.wob.tick(dt, 0.08, rng_) - 0.5) * c.drift * (3.0 + 25.0 * m);
         const double lr = std::log(kMetalPure[i]) * (1 - m) + std::log(kMetalBar[i]) * m;
         md.hz = std::min(0.45 * sr_, f0 * std::exp(lr) / fold * std::pow(2.0, (m * md.scatter + wob) / 1200.0));
         const double amp = std::pow(md.amp.tick(dt, md.ampRate * c.evolve, rng_), 4.0) / std::pow(kMetalPure[i], 0.3);
         md.gainStep = (amp - md.gainNow) / kBlock;
      }
      metalPan_ += (rng_.uni() - 0.5) * 0.002;
      metalPan_ = std::clamp(metalPan_, -0.6, 0.6);
      metalL_ = std::cos((metalPan_ * c.spread + 1) * kPi / 4) * 1.41;
      metalR_ = std::sin((metalPan_ * c.spread + 1) * kPi / 4) * 1.41;
      // SUB.
      subAmp_ = 0.55 + 0.45 * subWalk_.tick(dt, 0.05 * c.evolve, rng_);
   }

   double sr_ = 48000.0, hz_ = 110.0, toneHz_ = 110.0;
   Rng rng_;
   int counter_ = 0;
   double saw_[3] = {0, 0, 0}, body_ = 0, sub_ = 0, subAmp_ = 0.7, subAmpNow_ = 0.7, subMix_ = 0.0;
   double subCoef_ = 1.0 / (0.02 * 48000.0);
   double levelNow_[kLayerCount] = {}, levelStep_[kLayerCount] = {};
   bool levelsSet_ = false;
   double metalLNow_ = 1, metalRNow_ = 1;
   jclap::dsp::Filter tone_, air_;
   Walk toneDrift_, toneHand_, airBand_, subWalk_;
   Partial partials_[kPartials];
   Resonator res_[4];
   double resQ_[4] = {}, resGain_[4] = {}, resNow_[4] = {}, resStep_[4] = {};
   Walk resAmp_[4];
   Mode metal_[4];
   double metalPan_ = 0, metalL_ = 1, metalR_ = 1;
};

// ------------------------------------------------------------------------------------------------
// Envelopes and LFOs.

// DEGRADE: the droid as an old, broken machine. On the droid's bus, before its speaker: the sample
// rate and bit depth drop, the voice cuts out now and then, and hiss and crackle ride on it. The
// Engine also takes wow (a slow, warped pitch sag) from here for every voice, and closes and
// overdrives the speaker. At 0 it does nothing.
class Degrade {
public:
   void prepare(double sr) {
      sr_ = sr;
      rng_.seed(0x5eed);
      reset();
   }
   void reset() {
      held_ = env_ = crack_ = 0.0;
      phase_ = 1.0;
      gapLeft_ = 0;
      gain_ = 1.0;
      wowPh_ = flutterPh_ = 0.0;
      drift_.init(rng_);
   }
   // Advances `dt` seconds; returns the pitch sag in semitones (never sharp: a tired motor).
   double wow(double d, double dt) {
      wowPh_ += 0.45 * dt;
      flutterPh_ += 6.0 * dt;
      wowPh_ -= std::floor(wowPh_);
      flutterPh_ -= std::floor(flutterPh_);
      const double drift = drift_.tick(dt, 0.3, rng_);
      return -d * (0.5 + 0.5 * std::sin(kTwoPi * wowPh_) + 0.12 * std::sin(kTwoPi * flutterPh_) + 0.9 * drift);
   }
   float tick(float x, double d) {
      if (d <= 1e-3)
         return x;
      // Follows the droid's level, so the hiss and crackle stop when it does.
      env_ = std::max(static_cast<double>(std::fabs(x)), env_ * 0.9995);
      // A lower sample rate (24 kHz down to about 2 kHz), held between samples.
      phase_ += 24000.0 * std::pow(2.0, -3.6 * d) / sr_;
      if (phase_ >= 1.0) {
         phase_ -= std::floor(phase_);
         // Fewer bits: 14 down to 4.
         const double levels = std::pow(2.0, 14.0 - 10.0 * d);
         held_ = std::round(x * levels) / levels;
      }
      double y = held_;
      // Dropouts: more often and longer the further it is turned up.
      if (gapLeft_ > 0)
         --gapLeft_;
      else if (rng_.chance(d * d * 3.0 / sr_))
         gapLeft_ = static_cast<int>(rng_.range(0.015, 0.03 + 0.15 * d) * sr_);
      gain_ += ((gapLeft_ > 0 ? 0.0 : 1.0) - gain_) * 0.03;
      y *= gain_;
      // Hiss and crackle on the voice.
      if (rng_.chance(d * 60.0 / sr_))
         crack_ = (rng_.chance(0.5) ? 1.0 : -1.0) * rng_.range(0.2, 0.8);
      crack_ *= 0.6;
      y += env_ * d * ((rng_.uni() - 0.5) * 0.25 + crack_);
      return static_cast<float>(y);
   }

private:
   double sr_ = 48000.0, held_ = 0, phase_ = 1, env_ = 0, crack_ = 0, gain_ = 1, wowPh_ = 0, flutterPh_ = 0;
   int gapLeft_ = 0;
   Walk drift_;
   Rng rng_;
};

// ADSR with times in seconds: a curved attack, linear decay, and a release that eases out.
struct Adsr {
   enum Stage { kIdle, kAttack, kDecay, kSustain, kRelease } stage = kIdle;
   double level = 0.0, from = 0.0;
   void gate(bool on) {
      if (on) {
         stage = kAttack;
         from = level;
      } else if (stage != kIdle) {
         stage = kRelease;
         from = level;
      }
   }
   void kill() {
      stage = kIdle;
      level = 0.0;
   }
   // Advances `dt` seconds; returns the level, 0..1.
   double tick(double dt, double a, double d, double s, double r) {
      switch (stage) {
      case kAttack:
         level += dt / std::max(0.001, a);
         if (level >= 1.0) {
            level = 1.0;
            stage = kDecay;
         }
         break;
      case kDecay:
         level -= dt / std::max(0.001, d) * (1.0 - s);
         if (level <= s) {
            level = s;
            stage = kSustain;
         }
         break;
      case kSustain:
         level = s;
         break;
      case kRelease:
         level -= dt / std::max(0.001, r) * std::max(0.05, from);
         if (level <= 0.0) {
            level = 0.0;
            stage = kIdle;
         }
         break;
      case kIdle:
         break;
      }
      return level;
   }
   bool idle() const { return stage == kIdle; }
};

// A shape LFO, -1..1, free running.
struct ShapeLfo {
   double phase = 0.0, held = 0.0, from = 0.0, to = 0.0;
   Rng rng;
   double tick(double dt, double hz, int shape) {
      phase += hz * dt;
      bool wrapped = false;
      while (phase >= 1.0) {
         phase -= 1.0;
         wrapped = true;
      }
      if (wrapped) {
         held = rng.uni() * 2.0 - 1.0;
         from = to;
         to = rng.uni() * 2.0 - 1.0;
      }
      return value(shape);
   }
   double value(int shape) const {
      const double p = phase;
      switch (shape) {
      case kShapeTriangle:
         return 1.0 - 4.0 * std::fabs(p - 0.5);
      case kShapeSaw:
         return 2.0 * p - 1.0;
      case kShapeSquare:
         return p < 0.5 ? 1.0 : -1.0;
      case kShapeSampleHold:
         return held;
      case kShapeSmooth:
         return from + (to - from) * (0.5 - 0.5 * std::cos(kPi * p));
      default:
         return std::sin(kTwoPi * p);
      }
   }
};

// ------------------------------------------------------------------------------------------------
// The engine: voices, the droid speaker bus, the room and the master.

// Every param, as the audio hears it this block.
struct Settings {
   double v[kParamCount] = {};
   double operator[](int id) const { return v[id]; }
};

// What the panel shows of one voice.
struct VoiceView {
   bool active = false;
   int note = -1;
   double energy = 0, feeling = 0, pressure = 0, droid = 0, drone = 0, pan = 0, level = 0;
   int event = kEvNone;
   double eventPitch = 0;
};

class Engine {
public:
   static constexpr int kBlock = 32;

   void prepare(double sr) {
      sr_ = sr;
      for (Voice &v : voices_) {
         v.speaker.prepare(sr);
         v.drone.prepare(sr);
         v.droidLp.setSampleRate(sr);
         v.droidLp.setMode(jclap::dsp::Filter::Mode::LowPass);
      }
      speakerHp_.setSampleRate(sr);
      speakerHp_.setMode(jclap::dsp::Filter::Mode::HighPass);
      speakerHp_.setCutoff(250.0);
      speakerLp_.setSampleRate(sr);
      speakerLp_.setMode(jclap::dsp::Filter::Mode::LowPass);
      speakerLp_.setCutoff(10000.0);
      room_.prepare(sr);
      delay_.prepare(sr);
      degrade_.prepare(sr);
      sineTable();
      reset();
   }

   void reset() {
      for (Voice &v : voices_) {
         v.active = false;
         v.held = false;
         v.amp.kill();
         v.env1.kill();
         v.env2.kill();
         v.speaker.reset();
         v.droidLp.reset();
      }
      speakerHp_.reset();
      speakerLp_.reset();
      room_.reset();
      delay_.reset();
      degrade_.reset();
      counter_ = 0;
   }

   void seed(uint64_t s) { rng_.seed(s); }
   // The host's tempo, for a synced delay.
   void setTempo(double bpm) {
      if (bpm > 1.0)
         tempo_ = bpm;
   }

   // A note starts. `id` identifies it for expression and note-off (the CLAP note id, or the key).
   // Returns the voice it got.
   int noteOn(int key, int channel, int32_t noteId, double velocity, const Settings &s) {
      const bool mono = s[kPoly] >= 0.5;
      const int limit = mono ? 1 : std::clamp(static_cast<int>(std::lround(s[kVoices])), 1, kMaxVoices);
      int slot = -1;
      if (mono) {
         slot = 0;
      } else {
         // A free voice, else the quietest releasing one, else the oldest.
         for (int i = 0; i < limit && slot < 0; ++i)
            if (!voices_[i].active)
               slot = i;
         double quiet = 2.0;
         for (int i = 0; i < limit && slot < 0; ++i)
            if (!voices_[i].held && voices_[i].amp.level < quiet)
               quiet = voices_[i].amp.level;
         for (int i = 0; i < limit && slot < 0; ++i)
            if (!voices_[i].held && voices_[i].amp.level == quiet)
               slot = i;
         if (slot < 0) {
            uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < limit; ++i)
               if (voices_[i].age < oldest) {
                  oldest = voices_[i].age;
                  slot = i;
               }
         }
      }
      Voice &v = voices_[slot];
      const bool legato = mono && v.active && v.held;
      v.active = true;
      v.held = true;
      v.key = key;
      v.channel = channel;
      v.noteId = noteId;
      v.velocity = std::clamp(velocity, 0.0, 1.0);
      v.bend = 0.0;
      v.pressure = 0.0;
      v.slide = 0.5;
      v.age = ++ageCounter_;
      v.randomE = rng_.normal() * 0.5;
      v.randomF = rng_.normal() * 0.5;
      v.pan = kPans[slot];
      v.popClock = 0.0;
      if (!legato) {
         v.amp.gate(true);
         v.env1.gate(true);
         v.env2.gate(true);
         const bool sounding = v.droneGainNow > 1e-4;
         v.drone.start(pitchHz(v, s), rng_.next(), sounding);
         v.panSet = sounding;
         v.speaker.seed(rng_.next());
      } else {
         v.drone.setPitch(pitchHz(v, s));
      }
      // Every new note speaks (a syllable or a phrase, LENGTH long), unless the ribbon is all the
      // way to drone.
      const double morph = std::clamp(s[kMorph] + modOf(v, kDestMorph, s), 0.0, 1.0);
      // Over a drone a note has to be played hard enough to speak (SPEAK ABOVE): soft notes only
      // hum. The bar rises with the ribbon, to its full height from the middle on.
      v.speaks = v.velocity >= s[kSpeakAbove] * std::min(1.0, 2.0 * morph) - 1e-9;
      if (morph < 0.98 && v.speaks) {
         const Mood mood = moodFor(energyOf(v, s), feelingOf(v, s));
         speak(v, mood, s[kLength] * rng_.range(0.9, 1.1), s);
      }
      return slot;
   }

   void noteOff(int key, int channel, int32_t noteId) {
      for (Voice &v : voices_)
         if (v.active && v.held && matches(v, key, channel, noteId)) {
            v.held = false;
            v.amp.gate(false);
            v.env1.gate(false);
            v.env2.gate(false);
         }
   }
   void releaseAll() {
      for (Voice &v : voices_)
         if (v.active && v.held) {
            v.held = false;
            v.amp.gate(false);
            v.env1.gate(false);
            v.env2.gate(false);
         }
   }
   void choke(int key, int channel, int32_t noteId) {
      for (Voice &v : voices_)
         if (v.active && (key < 0 || matches(v, key, channel, noteId))) {
            v.active = false;
            v.held = false;
            v.amp.kill();
            v.speaker.reset();
         }
   }

   // Per-note expression: bend in semitones, pressure 0..1, slide (MPE timbre) 0..1.
   void bend(int key, int channel, int32_t noteId, double semitones) {
      for (Voice &v : voices_)
         if (v.active && matches(v, key, channel, noteId))
            v.bend = semitones;
   }
   void pressure(int key, int channel, int32_t noteId, double p) {
      for (Voice &v : voices_)
         if (v.active && matches(v, key, channel, noteId))
            v.pressure = clamp01(p);
   }
   void slide(int key, int channel, int32_t noteId, double t) {
      for (Voice &v : voices_)
         if (v.active && matches(v, key, channel, noteId))
            v.slide = clamp01(t);
   }

   // Adds `frames` stereo samples to out (which the caller has zeroed).
   void process(float *outL, float *outR, int frames, const Settings &s) {
      for (int k = 0; k < kSyllableCount; ++k)
         mix_[k] = s[kSyllable0 + k];
      jclap::dsp::RoomSettings rs;
      rs.mix = 0.6 * s[kSpaceMix];
      rs.size = s[kSpaceSize];
      rs.decay = s[kSpaceDecay];
      rs.damp = s[kSpaceDamp];
      rs.preDelayMs = s[kSpacePreDelay];
      room_.set(rs);
      jclap::dsp::DelaySettings ds;
      ds.on = s[kDelayOn] >= 0.5;
      ds.mix = s[kDelayMix];
      const int sync = static_cast<int>(std::lround(s[kDelaySync]));
      ds.timeMs = sync > 0 ? jclap::dsp::delaySyncBeats(sync) * 60000.0 / tempo_ : jclap::dsp::delayTimeMs(s[kDelayTime]);
      ds.feedback = s[kDelayFeedback];
      ds.tone = s[kDelayTone];
      ds.spread = s[kDelaySpread];
      delay_.set(ds);
      // DEGRADE closes the speaker in from both ends and drives it harder.
      const double degrade = clamp01(s[kDegrade]);
      speakerHp_.setCutoff(250.0 * std::pow(2.0, 1.5 * degrade));
      speakerLp_.setCutoff(10000.0 * std::pow(2.0, -2.8 * degrade));
      const double speakerDrive = 1.3 + 3.5 * degrade;
      const double master = s[kMasterLevel] <= -59.9 ? 0.0 : std::pow(10.0, s[kMasterLevel] / 20.0);
      const double width = s[kWidth];
      double peak = 0.0;
      for (int i = 0; i < frames; ++i) {
         if (counter_-- <= 0) {
            counter_ = kBlock - 1;
            control(s);
         }
         double droid = 0.0, L = 0.0, R = 0.0;
         for (Voice &v : voices_) {
            if (!v.active)
               continue;
            // The droid half: through the voice's own velocity and pressure filter and drive.
            if (v.speaker.busy()) {
               double y = v.speaker.tick();
               y = std::tanh(v.droidDrive * y) / std::tanh(v.droidDrive);
               y = v.droidLp.process(static_cast<float>(y)) * v.droidLevel;
               droid += y;
               v.droidMeter = std::max(v.droidMeter * 0.9995, std::fabs(y));
            }
            // The drone half.
            if (v.droneGain > 1e-4 || v.droneGainNow > 1e-4) {
               float l = 0.0f, r = 0.0f;
               v.drone.tick(v.controls, v.sub, l, r);
               v.droneGainNow += v.droneGainStep;
               const double g = v.droneGainNow;
               v.panLNow += (v.panL - v.panLNow) * 0.005;
               v.panRNow += (v.panR - v.panRNow) * 0.005;
               L += l * g * v.panLNow;
               R += r * g * v.panRNow;
            }
         }
         // The small speaker the droid talks through: band limited, gently clipped.
         droid = degrade_.tick(static_cast<float>(droid), degrade);
         droid = speakerLp_.process(speakerHp_.process(static_cast<float>(droid)));
         droid = std::tanh(speakerDrive * droid) / std::tanh(speakerDrive);
         L += droid * kDroidGain;
         R += droid * kDroidGain;
         // Width, then the delay, then the room, then the master.
         const double mid = 0.5 * (L + R), side = 0.5 * (L - R) * width;
         float l = static_cast<float>(mid + side), r = static_cast<float>(mid - side);
         delay_.tick(l, r);
         room_.tick(l, r);
         l = jclap::dsp::softClip(static_cast<float>(l * master));
         r = jclap::dsp::softClip(static_cast<float>(r * master));
         outL[i] += l;
         outR[i] += r;
         peak = std::max(peak, static_cast<double>(std::max(std::fabs(l), std::fabs(r))));
      }
      peak_ = peak;
   }

   // The loudest output sample of the last process call.
   double peak() const { return peak_; }
   int activeVoices() const {
      int n = 0;
      for (const Voice &v : voices_)
         n += v.active;
      return n;
   }
   VoiceView view(int i) const {
      const Voice &v = voices_[i];
      VoiceView out;
      out.active = v.active;
      out.note = v.key;
      out.energy = v.shownE;
      out.feeling = v.shownF;
      out.pressure = v.pressure;
      out.droid = v.droidMeter;
      out.drone = v.droneGainNow * v.amp.level;
      out.pan = v.pan;
      out.level = v.amp.level;
      out.event = v.speaker.kind();
      out.eventPitch = v.speaker.pitch();
      return out;
   }
   double lfo(int i) const { return lfoValue_[i]; }

   static constexpr double kDroidGain = 0.42;

private:
   struct Voice {
      bool active = false, held = false, sub = false;
      int key = -1, channel = -1;
      int32_t noteId = -1;
      double velocity = 0.8, bend = 0, pressure = 0, slide = 0.5, randomE = 0, randomF = 0;
      uint64_t age = 0;
      double pan = 0, panL = 1, panR = 1, panLNow = 1, panRNow = 1;
      bool panSet = false, speaks = true;
      double popClock = 0;
      Adsr amp, env1, env2;
      double envValue[2] = {0, 0};
      Speaker speaker;
      Drone drone;
      DroneControls controls;
      jclap::dsp::Filter droidLp;
      double droidDrive = 1.0, droidLevel = 1.0, droidMeter = 0;
      double droneGain = 0, droneGainNow = 0, droneGainStep = 0;
      double shownE = 0, shownF = 0;
      double breathPh = 0, breathRate = 0.08;
   };

   // A key speaks: one syllable or a whole phrase, by PER KEY.
   void speak(Voice &v, const Mood &mood, double seconds, const Settings &s) {
      if (s[kPerKey] >= 0.5)
         v.speaker.phrase(mood, seconds, registerOf(v, s), mix_);
      else
         v.speaker.syllable(mood, seconds, registerOf(v, s), mix_);
   }

   static bool matches(const Voice &v, int key, int channel, int32_t noteId) {
      if (noteId >= 0 && v.noteId >= 0)
         return v.noteId == noteId;
      return v.key == key && (channel < 0 || v.channel < 0 || v.channel == channel);
   }

   // A source's modulation sent to `dest` on voice v: envelopes 0..1, LFOs -1..1, times their amount.
   double modOf(const Voice &v, int dest, const Settings &s) const {
      double sum = 0.0;
      if (static_cast<int>(std::lround(s[kEnv1Dest])) == dest)
         sum += v.envValue[0] * s[kEnv1Amount];
      if (static_cast<int>(std::lround(s[kEnv2Dest])) == dest)
         sum += v.envValue[1] * s[kEnv2Amount];
      if (static_cast<int>(std::lround(s[kLfo1Dest])) == dest)
         sum += lfoValue_[0] * s[kLfo1Amount];
      if (static_cast<int>(std::lround(s[kLfo2Dest])) == dest)
         sum += lfoValue_[1] * s[kLfo2Amount];
      return sum;
   }

   double energyOf(const Voice &v, const Settings &s) const {
      return std::clamp(s[kEnergy] + s[kPressToEnergy] * v.pressure + s[kRandomMood] * v.randomE + modOf(v, kDestEnergy, s), -1.0,
                        1.0);
   }
   double feelingOf(const Voice &v, const Settings &s) const {
      return std::clamp(s[kFeeling] + s[kSlideToFeeling] * (v.slide - 0.5) * 2.0 + s[kVelToFeeling] * (v.velocity - 0.5) * 2.0 +
                           s[kRandomMood] * v.randomF + modOf(v, kDestFeeling, s),
                        -1.0, 1.0);
   }
   // The droid's register: where the key is (around middle C), how far that moves it, and the knob.
   double registerOf(const Voice &v, const Settings &s) const { return s[kRegister] + s[kKeyTrack] * (v.key + v.bend - 60.0); }

   // The drone's pitch: just intonation against the lowest held note, or equal temperament.
   double pitchHz(const Voice &v, const Settings &s) const {
      double cents = 0.0;
      if (s[kTuning] < 0.5) {
         int root = v.key;
         for (const Voice &o : voices_)
            if (o.active && o.held && o.key < root)
               root = o.key;
         cents = kJustCents[((v.key - root) % 12 + 12) % 12];
      }
      return mtof(v.key + v.bend + cents / 100.0);
   }

   // Every 32 samples: envelopes, LFOs, modulation, each voice's controls.
   void control(const Settings &s) {
      const double dt = kBlock / sr_;
      lfoValue_[0] = lfos_[0].tick(dt, s[kLfo1Rate], static_cast<int>(std::lround(s[kLfo1Shape])));
      lfoValue_[1] = lfos_[1].tick(dt, s[kLfo2Rate], static_cast<int>(std::lround(s[kLfo2Shape])));
      wow_ = degrade_.wow(clamp01(s[kDegrade]), dt);
      // The lowest sounding drone note gets the SUB.
      int lowest = -1;
      for (int i = 0; i < kMaxVoices; ++i)
         if (voices_[i].active && voices_[i].held && (lowest < 0 || voices_[i].key < voices_[lowest].key))
            lowest = i;
      for (int i = 0; i < kMaxVoices; ++i) {
         Voice &v = voices_[i];
         if (!v.active)
            continue;
         // With nothing held, the SUB stays on whichever note had it, fading out with it.
         if (lowest >= 0)
            v.sub = i == lowest;
         v.envValue[0] = v.env1.tick(dt, s[kEnv1Attack], s[kEnv1Decay], s[kEnv1Sustain], s[kEnv1Release]);
         v.envValue[1] = v.env2.tick(dt, s[kEnv2Attack], s[kEnv2Decay], s[kEnv2Sustain], s[kEnv2Release]);
         const double amp = v.amp.tick(dt, s[kAttack], s[kDecay], s[kSustain], s[kRelease]);
         const double morph = std::clamp(s[kMorph] + modOf(v, kDestMorph, s), 0.0, 1.0);
         const double e = energyOf(v, s), f = feelingOf(v, s);
         v.shownE = e;
         v.shownF = f;
         // Velocity and pressure play the droid voice: filter and drive up, level with velocity.
         const double vel = v.velocity * s[kVelResponse], pr = v.pressure * s[kPressResponse];
         v.droidLp.setCutoff(2200.0 * std::pow(2.0, 2.4 * vel + 2.6 * pr));
         v.droidDrive = 1.0 + 5.0 * std::pow(std::max(vel, pr), 1.5);
         // Over a drone the droid steps back a little, so the two sit together.
         // The ribbon is an equal-power crossfade: droid only at 0, an even mix at 0.5, drone only at 1.
         const double droneMix = std::sin(morph * kPi / 2), droidMix = std::cos(morph * kPi / 2);
         v.droidLevel = (0.35 + 0.65 * (s[kVelResponse] > 0 ? v.velocity : 1.0)) * droidMix;
         // The drone half, by the ribbon, breathing.
         v.breathPh += v.breathRate * dt;
         const double breath = 1.0 + s[kBreath] * 0.35 * std::sin(kTwoPi * v.breathPh);
         // An eased attack and release (^1.5), as in the sketch.
         // One curve for every stage (attack, decay, release, retrigger), so moving between them
         // never steps the level.
         const double shaped = std::pow(amp, 1.5);
         v.droneGain = 1.2 * droneMix * shaped * breath * (0.6 + 0.4 * v.velocity);
         v.droneGainStep = (v.droneGain - v.droneGainNow) / kBlock;
         DroneControls &c = v.controls;
         static const int kLayerDest[kLayerCount] = {kDestTone, kDestPartials, kDestAir, kDestMetal, kDestSub};
         for (int k = 0; k < kLayerCount; ++k)
            c.level[k] = clamp01(s[kLayer0 + k] + modOf(v, kLayerDest[k], s));
         c.partialsAtonal = clamp01(s[kPartialsAtonal] + modOf(v, kDestPartialsAtonal, s));
         c.metalAtonal = clamp01(s[kMetalAtonal] + modOf(v, kDestMetalAtonal, s));
         c.evolve = std::clamp(s[kEvolve] + 2.0 * modOf(v, kDestEvolve, s), 0.0, 2.0);
         c.drift = std::clamp(s[kDrift] + 2.0 * modOf(v, kDestDrift, s), 0.0, 2.0);
         c.spread = s[kSpread];
         c.cutoffOct = 3.0 * modOf(v, kDestCutoff, s);
         c.pressure = v.pressure * s[kPressResponse];
         v.drone.setPitch(pitchHz(v, s));
         const double pan = std::clamp(v.pan * s[kSpread] * 1.25 + modOf(v, kDestPan, s), -1.0, 1.0);
         v.panL = std::cos((pan + 1) * kPi / 4) * 1.41421356;
         v.panR = std::sin((pan + 1) * kPi / 4) * 1.41421356;
         if (!v.panSet) {
            v.panLNow = v.panL;
            v.panRNow = v.panR;
            v.panSet = true;
         }
         // A held note talks again now and then (TALK), less toward the drone end.
         if (v.held && v.speaks && !v.speaker.busy() && morph < 0.98 && s[kTalk] > 0.0) {
            const double rate = s[kTalk] * 0.9 * std::pow(1.0 - morph, 1.5) / std::max(0.3, moodFor(e, f).tempo);
            if (rng_.chance(rate * dt))
               speak(v, moodFor(e, f), s[kLength] * rng_.range(0.6, 1.1), s);
         }
         // Pressure on a held drone pops bleeps through: POP a second at full press, as press^2.
         if (morph > 0.02 && v.held && s[kPop] > 0.0 && v.pressure > 0.02 && !v.speaker.busy()) {
            if (rng_.chance(s[kPop] * v.pressure * v.pressure * dt)) {
               static const int kPopKinds[6] = {kChirp, kBell, kSteps, kWarble, kTweet, kGlide};
               v.speaker.pop(moodFor(e, f), registerOf(v, s), kPopKinds[rng_.integer(0, 6)]);
            }
         }
         // The note's pitch bend moves what the droid says too, live, with DEGRADE's wow.
         v.speaker.setPitchOffset(v.bend + wow_);
         v.droidMeter *= 0.97;
         // Done once both halves are quiet.
         if (!v.held)
            v.speaker.stop();
         if (v.amp.idle() && !v.speaker.busy() && !v.held) {
            v.active = false;
            v.droneGainNow = 0.0;
         } else if (!v.held && v.amp.idle()) {
            v.droneGain = 0.0;
         }
      }
   }

   static constexpr double kPans[kMaxVoices] = {-0.4, 0.3, -0.15, 0.45, -0.3, 0.2, -0.05, 0.35};

   double sr_ = 48000.0;
   Voice voices_[kMaxVoices];
   Rng rng_;
   uint64_t ageCounter_ = 0;
   int counter_ = 0;
   double mix_[kSyllableCount] = {1, 1, 1, 1, 1, 1, 1, 1};
   ShapeLfo lfos_[2];
   double lfoValue_[2] = {0, 0};
   jclap::dsp::Filter speakerHp_, speakerLp_;
   jclap::dsp::Room room_;
   jclap::dsp::StereoDelay delay_;
   Degrade degrade_;
   double tempo_ = 120.0, wow_ = 0.0;
   double peak_ = 0.0;
};

} // namespace j2d2

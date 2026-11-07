#pragma once

// One JBreath voice: the five instrument models (models.h) and the player that blows them.
//
// The player turns a note's velocity, pressure and slide into breath and embouchure: a tongued
// attack with a small burst of pressure (the kick), breath that follows pressure with the attack
// and release times, vibrato that grows in after a delay and wanders a little as a player's does,
// and the pitch, gliding when a note slurs into the next. The bore length comes from the baked
// tuning table, so every note plays in tune.
//
// A voice runs at the internal rate (the host's rate times the oversampling factor); the engine
// sums the voices there and decimates once.

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "models.h"
#include "tuning_table.h"

namespace jbreath {

// What the params set on a voice (see params.h for their ranges).
struct VoiceSettings {
   int model = kClarinet;
   double breath = 1.0, embouchure = 0.5, noise = 1.0, edge = 1.0;
   double kick = 0.25, attackMs = 25.0, releaseMs = 90.0;
   double glideMs = 0.0;
   double vibRate = 5.1, vibDepth = 0.05, vibPitch = 0.0, vibDelayMs = 350.0;
   int source = 0; // BreathSource
   double breathIn = 0.0; // the Breath In parameter, 0..1
   double curve = 1.0, slide = 0.5;
};

// A model's tuning for a (fractional) MIDI note: the bore-length ratio and the levelling gain, and
// how many octaves the note is moved to land where the model can play it. Notes the model can't
// sound inside its range borrow their neighbours' tuning; notes past either end play an octave (or
// more) in, so every key sounds.
struct Tuning {
   double ratio = 1.0, gain = 1.0;
   int octaves = 0;
};

class TuningTable {
public:
   static const TuningTable &get() {
      static const TuningTable table;
      return table;
   }
   // At the voices' rate `fs`: the table has lengths for 88.2 and 96 kHz, and holds the 96 kHz ones
   // above that.
   Tuning at(int model, double note, double fs) const {
      const double w = std::clamp((fs - 88200.0) / (96000.0 - 88200.0), 0.0, 1.0);
      model = std::clamp(model, 0, kModelCount - 1);
      note = std::clamp(note, 0.0, 127.0);
      const int n = std::min(static_cast<int>(note), 126);
      const double f = note - n;
      const Resolved &a = notes_[model][n], &b = notes_[model][n + 1];
      Tuning t;
      t.octaves = a.octaves;
      const double ra = a.ratio88 + w * (a.ratio96 - a.ratio88), rb = b.ratio88 + w * (b.ratio96 - b.ratio88);
      if (b.octaves == a.octaves) {
         t.ratio = ra + f * (rb - ra);
         t.gain = a.gain + f * (b.gain - a.gain);
      } else {
         t.ratio = ra;
         t.gain = a.gain;
      }
      return t;
   }
   // The lowest and highest notes the model plays at their own pitch.
   int lowest(int model) const { return low_[std::clamp(model, 0, kModelCount - 1)]; }
   int highest(int model) const { return high_[std::clamp(model, 0, kModelCount - 1)]; }

private:
   struct Resolved {
      double ratio88 = 1.0, ratio96 = 1.0, gain = 1.0;
      int octaves = 0;
   };
   TuningTable() {
      constexpr int count = kTuneLastNote - kTuneFirstNote + 1;
      for (int m = 0; m < kModelCount; ++m) {
         const TuneEntry *e = kTuning[m];
         int lo = 0, hi = count - 1;
         while (lo < count && !e[lo].plays)
            ++lo;
         while (hi > lo && !e[hi].plays)
            --hi;
         low_[m] = kTuneFirstNote + lo;
         high_[m] = kTuneFirstNote + hi;
         for (int note = 0; note < 128; ++note) {
            Resolved &r = notes_[m][note];
            int k = note;
            while (k < low_[m]) {
               k += 12;
               ++r.octaves;
            }
            while (k > high_[m]) {
               k -= 12;
               --r.octaves;
            }
            // Inside the range, a gap takes its tuning from the notes either side that play.
            const int idx = k - kTuneFirstNote;
            int below = idx, above = idx;
            while (below > lo && !e[below].plays)
               --below;
            while (above < hi && !e[above].plays)
               ++above;
            const double f = above > below ? static_cast<double>(idx - below) / (above - below) : 0.0;
            const auto mix = [f](float x, float y) { return x + f * (y - x); };
            const TuneEntry &a = e[below], &c = e[above];
            r.ratio88 = mix(a.ratio88, c.ratio88);
            r.ratio96 = mix(a.ratio96, c.ratio96);
            r.gain = mix(a.gain, c.gain);
         }
      }
   }
   Resolved notes_[kModelCount][128];
   int low_[kModelCount] = {}, high_[kModelCount] = {};
};

class BreathVoice {
public:
   // Allocates the models for the internal rate. Not real-time safe.
   void prepare(double internalRate) {
      fs_ = internalRate;
      models_[kFlute] = std::make_unique<Flute>();
      models_[kClarinet] = std::make_unique<Clarinet>();
      models_[kSax] = std::make_unique<Sax>();
      models_[kOboe] = std::make_unique<Oboe>();
      models_[kTrumpet] = std::make_unique<Trumpet>();
      for (auto &m : models_)
         m->prepare(fs_);
      fallCoef_ = coef(15.0);
      stopCoef_ = coef(0.75);
      gainCoef_ = coef(5.0);
      envCoef_ = coef(30.0);
      reset();
   }
   void setSeed(uint32_t seed) {
      for (int i = 0; i < kModelCount; ++i)
         models_[i]->rng.s = seed * (i + 1) | 1u;
      jitter_.s = seed ^ 0x51ed270bu;
   }

   void configure(const VoiceSettings &s) {
      // A different instrument starts from silence: a bore full of another model's sound would only
      // click.
      if (s.model != settings_.model) {
         models_[std::clamp(s.model, 0, kModelCount - 1)]->reset();
         len_ = -1.0;
      }
      settings_ = s;
      settings_.model = std::clamp(s.model, 0, kModelCount - 1);
      Model &m = model();
      m.character.noise = static_cast<float>(s.noise);
      m.character.edge = static_cast<float>(s.edge);
      attackCoef_ = coef(s.attackMs / 4.0);
      releaseCoef_ = coef(s.releaseMs / 4.0);
      glideCoef_ = coef(std::max(3.0, s.glideMs / 3.0));
   }

   // A note from silence (or a stolen voice): the tongue releases the air with a kick.
   void start(double pitch, double velocity, double gain) {
      if (!active_ || releasing_) {
         model().reset();
         breath_ = 0.0;
         pitchNow_ = pitch;
         len_ = -1.0;
         hold_ = 1.0;
         levelNow_ = 0.0;
      }
      // The player's level, pitch and bore length are worked out before the first sample.
      counter_ = 0;
      active_ = true;
      releasing_ = false;
      stop_ = false;
      age_ = 0;
      kickAge_ = 0;
      tongue_ = 1.0f;
      velocity_ = velocity;
      gain_ = gain;
      pitch_ = pitch;
      quiet_ = 0;
   }
   // Mono: the tongue stops the air for 35 ms, then releases it on the new note with a kick.
   void retongue(double pitch, double velocity) {
      if (!active_ || releasing_)
         return start(pitch, velocity, gain_);
      stop_ = true;
      age_ = 0;
      kickAge_ = -static_cast<int>(0.035 * fs_);
      tongue_ = 1.0f;
      velocity_ = velocity;
      pitch_ = pitch;
      hold_ = 1.0;
   }
   // Legato: the fingers change, the air doesn't stop.
   void slur(double pitch, double velocity) {
      if (!active_ || releasing_)
         return start(pitch, velocity, gain_);
      age_ = 0;
      pitch_ = pitch;
      velocity_ = std::max(velocity_ * 0.8, velocity);
   }
   void release() {
      if (active_)
         releasing_ = true;
   }
   // Only the instrument playing has sound in it: the others were cleared when it was picked.
   void reset() {
      active_ = releasing_ = stop_ = false;
      breath_ = 0.0;
      level_ = 0.0f;
      env_ = 0.0;
      if (models_[settings_.model])
         model().reset();
   }

   // The note's pitch in semitones (key, tune and bend), and its expression, 0..1.
   void setPitch(double pitch) { pitch_ = pitch; }
   void setExpression(double pressure, double timbre) {
      pressure_ = pressure;
      timbre_ = timbre;
   }
   void setGain(double gain) { gain_ = gain; }

   bool isActive() const { return active_; }
   bool isReleasing() const { return releasing_; }
   // A rough loudness, for stealing the quietest and for the panel.
   float level() const { return level_; }
   // The breath blowing now, 0..about 1.3, and the pitch sounding, for the panel.
   float breath() const { return static_cast<float>(breath_); }
   double hz() const { return 440.0 * std::pow(2.0, (pitchNow_ - 69.0) / 12.0); }

   // One sample at the internal rate.
   float tick() {
      if (!active_)
         return 0.0f;
      if ((counter_++ & (kControl - 1)) == 0)
         control();
      // Breath follows its target: the attack time going up, the release time (or a held note's
      // quicker fall) going down, and 3 ms when the tongue stops it.
      const double target = targetBreath();
      const double c = target > breath_ ? attackCoef_ : stop_ ? stopCoef_ : releasing_ ? releaseCoef_ : fallCoef_;
      breath_ += (target - breath_) * c;
      len_ += lenStep_;
      gainNow_ += (gainTarget_ - gainNow_) * gainCoef_;
      tongue_ *= tongueDecay_;
      ++age_;
      ++kickAge_;
      // The tongue's noise rides on the air, so there's none without breath.
      const float tongue = tongue_ * static_cast<float>(std::min(1.0, breath_ * 20.0));
      const float y = model().tick(static_cast<float>(breath_), static_cast<float>(emb_), len_, tongue) *
                      static_cast<float>(gainNow_);
      if (!std::isfinite(y)) {
         // A model pushed somewhere it can't go: silence it rather than the whole plugin.
         reset();
         return 0.0f;
      }
      env_ += (std::fabs(y) - env_) * envCoef_;
      return y;
   }

private:
   static constexpr int kControl = 16;

   Model &model() { return *models_[settings_.model]; }
   double coef(double ms) const { return 1.0 - std::exp(-1000.0 / (std::max(ms, 0.05) * fs_)); }

   // The breath the player is aiming for now.
   double targetBreath() const {
      if (releasing_ || (stop_ && kickAge_ < 0))
         return 0.0;
      double level = levelNow_;
      if (level <= 0.0)
         return 0.0;
      const ModelInfo &info = modelInfo(settings_.model);
      double b = info.breathOffset + info.breathScale * settings_.breath * std::pow(level, settings_.curve);
      // Fading out towards no pressure at all, rather than stopping at the offset.
      b *= std::min(1.0, level / 0.08);
      // The tongue's release comes with a burst of pressure that kicks the oscillation off.
      if (kickAge_ >= 0)
         b *= 1.0 + settings_.kick * kickEnv_;
      return b * vibBreath_;
   }

   // Every kControl samples: the player's level, vibrato, pitch and the bore length.
   void control() {
      const double dt = kControl / fs_;
      if (stop_ && kickAge_ >= 0)
         stop_ = false;
      kickEnv_ = kickAge_ >= 0 ? std::exp(-kickAge_ / (0.04 * fs_)) : 0.0;
      tongueDecay_ = static_cast<float>(std::exp(-1.0 / (0.017 * fs_)));

      // Breath from velocity, pressure, the Breath In input, or (Auto) velocity until pressure or
      // the input takes over.
      const double vel = velocity_;
      switch (settings_.source) {
      case 1:
         levelNow_ = pressure_;
         break;
      case 2:
         levelNow_ = vel;
         break;
      case 3:
         levelNow_ = settings_.breathIn;
         break;
      default: {
         const double blown = std::max(pressure_, settings_.breathIn);
         if (blown > 0.04)
            hold_ *= std::exp(-dt / 0.15);
         levelNow_ = std::max(blown, vel * hold_);
         break;
      }
      }

      // Vibrato grows in after its delay; its rate and depth wander a little.
      const double vd = std::clamp((age_ / fs_ - settings_.vibDelayMs / 1000.0) / 0.6, 0.0, 1.0);
      // (Steps scaled from the prototype's, which wandered every 256 samples at 96 kHz.)
      const double walk = std::sqrt(dt * 375.0), pull = 0.02 * dt * 375.0;
      vibRate_ += pull * (settings_.vibRate - vibRate_) + 0.05 * jitter_.white() * walk;
      vibAmt_ += pull * (1.0 - vibAmt_) + 0.04 * jitter_.white() * walk;
      vibPhase_ += 2 * kPi * vibRate_ * dt;
      if (vibPhase_ > 2 * kPi)
         vibPhase_ -= 2 * kPi;
      const double vib = vibAmt_ * vd * std::sin(vibPhase_);
      vibBreath_ = 1.0 + settings_.vibDepth * vib;

      // Embouchure: the knob, moved by MPE slide.
      emb_ = std::clamp(settings_.embouchure + settings_.slide * (timbre_ - 0.5) * 2.0, 0.0, 1.0);

      // Pitch glides to the note (a finger lands in a couple of ms, or the Glide time).
      const double g = 1.0 - std::pow(1.0 - glideCoef_, kControl);
      pitchNow_ += (pitch_ - pitchNow_) * g;
      const double sounding = pitchNow_ + settings_.vibPitch / 100.0 * vib;
      const Tuning t = TuningTable::get().at(settings_.model, sounding, fs_);
      const double hz = 440.0 * std::pow(2.0, (sounding - 69.0) / 12.0 + t.octaves);
      Model &m = model();
      m.notePitch(hz);
      const double len = m.nominal(hz) * t.ratio;
      if (len_ < 0.0) {
         len_ = len;
         lenStep_ = 0.0;
         gainNow_ = t.gain * gain_;
      } else {
         lenStep_ = (len - len_) / kControl;
      }
      gainTarget_ = t.gain * gain_;

      // Silent long enough after letting go (or tongued to nothing): the voice is free.
      level_ = static_cast<float>(env_);
      if (releasing_ && breath_ < 1e-4 && env_ < 2e-5) {
         if (++quiet_ > static_cast<int>(0.02 * fs_ / kControl))
            reset();
      } else {
         quiet_ = 0;
      }
   }

   std::unique_ptr<Model> models_[kModelCount];
   VoiceSettings settings_;
   Noise jitter_;
   double fs_ = 96000.0;
   bool active_ = false, releasing_ = false, stop_ = false;
   double breath_ = 0.0, levelNow_ = 0.0, hold_ = 1.0;
   double velocity_ = 1.0, pressure_ = 0.0, timbre_ = 0.5, gain_ = 1.0;
   double pitch_ = 60.0, pitchNow_ = 60.0, emb_ = 0.5;
   double len_ = -1.0, lenStep_ = 0.0, gainNow_ = 0.0, gainTarget_ = 0.0;
   double attackCoef_ = 0.01, releaseCoef_ = 0.001, glideCoef_ = 0.01;
   double fallCoef_ = 0.001, stopCoef_ = 0.01, gainCoef_ = 0.002, envCoef_ = 0.0003;
   double kickEnv_ = 0.0, vibBreath_ = 1.0, vibRate_ = 5.1, vibAmt_ = 1.0, vibPhase_ = 0.0;
   double env_ = 0.0;
   float tongue_ = 0.0f, tongueDecay_ = 0.9994f;
   float level_ = 0.0f;
   int age_ = 0, kickAge_ = 0, quiet_ = 0;
   unsigned counter_ = 0;
};

// Brings the voices' sum from the internal rate down to the host's: a windowed-sinc lowpass just
// under the host's Nyquist, evaluated once per output sample.
class Decimator {
public:
   void prepare(int factor) {
      factor_ = std::max(1, factor);
      taps_ = factor_ == 1 ? 1 : 24 * factor_ + 1;
      h_.assign(static_cast<size_t>(taps_), 0.0f);
      x_.assign(static_cast<size_t>(taps_) * 2, 0.0f);
      pos_ = 0;
      if (factor_ == 1) {
         h_[0] = 1.0f;
         return;
      }
      const double fc = 0.46 / factor_, mid = (taps_ - 1) / 2.0;
      double sum = 0.0;
      for (int i = 0; i < taps_; ++i) {
         const double k = i - mid;
         const double s = k == 0 ? 2 * fc : std::sin(2 * kPi * fc * k) / (kPi * k);
         const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (taps_ - 1)) + 0.08 * std::cos(4 * kPi * i / (taps_ - 1));
         h_[static_cast<size_t>(i)] = static_cast<float>(s * w);
         sum += s * w;
      }
      for (float &v : h_)
         v = static_cast<float>(v / sum);
   }
   void reset() {
      std::fill(x_.begin(), x_.end(), 0.0f);
      pos_ = 0;
   }
   int factor() const { return factor_; }
   // `factor` samples in, one out.
   float process(const float *in) {
      for (int i = 0; i < factor_; ++i) {
         x_[static_cast<size_t>(pos_)] = x_[static_cast<size_t>(pos_ + taps_)] = in[i];
         if (++pos_ == taps_)
            pos_ = 0;
      }
      // x_[pos_ .. pos_ + taps_) is the history, oldest first.
      const float *x = x_.data() + pos_;
      float acc = 0.0f;
      for (int i = 0; i < taps_; ++i)
         acc += h_[static_cast<size_t>(i)] * x[i];
      return acc;
   }

private:
   std::vector<float> h_, x_;
   int factor_ = 1, taps_ = 1, pos_ = 0;
};

// How many times the host's rate the voices run at: at least 88.2 kHz, where the models were tuned.
inline int oversamplingFor(double hostRate) {
   int f = 1;
   while (hostRate * f < 88000.0 && f < 8)
      ++f;
   return f;
}

} // namespace jbreath

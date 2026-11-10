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
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include <jclap/dsp/filter.h>
#include <jclap/dsp/oscillator.h>

#include "models.h"
#include "params.h"
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

   // Modulation (params.h): the routes in use, the generators and the buses. A destination's
   // value is its param's plus each route's amount times its source, in the param's full range.
   struct Route {
      int source = 0, dest = 0;
      float amount = 0.0f;
   };
   Route routes[kRouteCount];
   int routeCount = 0;
   double lfoRate[kLfoCount] = {1.0, 0.25};
   int lfoShape[kLfoCount] = {kLfoSine, kLfoSmooth}, lfoSync[kLfoCount] = {0, 1};
   int swellShape = kSwellArch;
   double swellTime = 3.0;
   bool swellLoop = false;
   double busAmount[kBusCount] = {}, busLag[kBusCount] = {};
   int busMode[kBusCount] = {}, busCurve[kBusCount] = {}, busPolarity[kBusCount] = {};
   double tune = 0.0, fine = 0.0, level = 0.0; // only as bases for their routes
   // The rest of the exciter and bore (Character in models.h), the player's habits, and the air.
   double stiffness = 0.5, aperture = 0.5, growl = 0.0, cone = 0.5, brass = 0.5;
   // The Alien's voice: Accent, Wobble, Drift, Shift and Sharp, the bow, Talk, Pace, Hold, Glide,
   // Bend and Consonants; Breathy, Spread, Flip and Follow; and whether it sings typed words.
   double alienVoice = 0.0, alienMorph = 0.0, alienDrift = 0.2, alienShift = 0.0, alienSharp = 0.5, alienBow = 0.0, alienTalk = 0.5, alienPaceSec = 0.0, alienLinger = 0.2, alienGlide = 30.0, alienBend = 0.6, alienStrength = 1.0;
   double alienBreathy = 0.25, alienSpread = 0.0, alienFlip = 0.0, alienFollow = 0.0;
   double alienRough = 0.45, alienGrowl = 0.0, alienWet = 0.2;
   uint32_t alienSounds = 0xffu, alienVowels = 0x1fu;
   int alienOrder = 1;
   bool alienTyped = false;
   double loss = 0.04, cutoffHz = 1300.0, detune = 0.0, humanize = 0.0, air = 0.0;
   int tongueType = kTongueHard, vibType = kVibBreath;
   // HYBRID: a plain subtractive voice on the same breath: its wave, octave and detune, how loud it
   // is blown against the instrument, and its lowpass, which the breath opens.
   int hybridModel = 2, hybridOctave = 0; // hybridModel is the wave (kHybridWaveNames)
   double hybridMix = 0.0, hybridDetune = 0.0, hybridBreath = 1.0;
   double hybridCutoff = 1800.0, hybridResonance = 0.2, hybridFilterBreath = 0.6;
   // Its own envelope (times in ms), how much its level still follows the breath (0 is a pad that
   // plays at the note's velocity and keeps sounding through its release), and how far the envelope
   // opens the lowpass (-1..1, up to 5 octaves either way).
   double hybridAttackMs = 5.0, hybridDecayMs = 250.0, hybridSustain = 1.0, hybridReleaseMs = 60.0;
   double hybridFollow = 1.0, hybridFilterEnv = 0.0;
   // A flutter tongue on any style, the note's pan, and unison: copies of the voice, detuned.
   double flutter = 0.0, flutterRate = 24.0, pan = 0.0, unisonDetune = 12.0;
   int unison = 1;
   // The bore's holes and what the tube is made of (Body in output.h colours the sound after).
   int holes = 3, tube = 0;
   // The mute in the bell (Mute below) and how far it's in, 0..1.
   int mute = 0;
   double muteAmount = 1.0;
};

constexpr int kMaxUnison = 4;

// A model's tuning for a (fractional) MIDI note: the bore-length ratio and the levelling gain, and
// how many octaves the note is moved to land where the model can play it. Notes the model can't
// sound inside its range borrow their neighbours' tuning; notes past either end play an octave (or
// more) in, so every key sounds.
struct Tuning {
   double ratio = 1.0, gain = 1.0, speak = 0.0;
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
      model = std::clamp(model, 0, kTuneRows - 1);
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
         t.speak = a.speak + f * (b.speak - a.speak);
      } else {
         t.ratio = ra;
         t.gain = a.gain;
         t.speak = a.speak;
      }
      return t;
   }
   // The lowest and highest notes the model plays at their own pitch.
   int lowest(int model) const { return low_[std::clamp(model, 0, kTuneRows - 1)]; }
   int highest(int model) const { return high_[std::clamp(model, 0, kTuneRows - 1)]; }

private:
   struct Resolved {
      double ratio88 = 1.0, ratio96 = 1.0, gain = 1.0, speak = 0.0;
      int octaves = 0;
   };
   TuningTable() {
      constexpr int count = kTuneLastNote - kTuneFirstNote + 1;
      for (int m = 0; m < kTuneRows; ++m) {
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
            r.speak = mix(a.speak, c.speak);
         }
      }
   }
   Resolved notes_[kTuneRows][128];
   int low_[kTuneRows] = {}, high_[kTuneRows] = {};
};

// A mute in the bell (or the open end of any of the models), per voice: Open, Straight, Cup,
// Harmon, Plunger or Bucket, pushed in by `amount` (0 open, 1 fully in). A mute does two things
// (Ancell 1960, JASA 32: 1101; Backus 1976, JASA 60: 470; Fletcher & Rossing 14.x):
// - what it lets out: a filter on the radiated sound, here. The straight mute's cone and its gap
//   make a Helmholtz resonator: lows cut, a nasal peak near 1.8 kHz. The cup adds a cup over that:
//   a resonance near 900 Hz and the highs cut. The Harmon seals the bell and lets the sound out
//   through its stem: a strong, sharp resonance near 2 kHz with the fundamental weak, buzzy. A
//   plunger closes over the bell: a resonant lowpass that falls as it closes (the "wah"). A
//   bucket's felt absorbs the highs: dark and soft.
// - what it sends back into the bore: less radiated means more reflected, so the bell's cutoff
//   rises a little (felt also absorbs: loss). That's in applyCharacter, and the bore length
//   correction keeps the note in tune (a mute played as is sharpens it a few cents).
// The amount glides (about 8 ms, as fast as a hand can move one) and the filters are set again
// only while it moves (once per control block at most), their gains following it, so working it
// while a note sounds, or a route jumping it, doesn't click. A new note starts where it's set.
class Mute {
public:
   // `step` is how often set() is called, in samples.
   void prepare(double fs, int step) {
      fs_ = fs;
      glide_ = 1.0 - std::exp(-step / (0.008 * fs));
      reset();
   }
   void reset() {
      for (Biquad &b : bq_)
         b.clear();
      snap_ = true;
   }
   bool active() const { return on_; }
   // The amount in now (gliding to the one set), for the bore (bore()).
   double amount() const { return mute_ <= kOpen ? 0.0 : amount_; }
   void set(int mute, double target) {
      mute = std::clamp(mute, 0, kMuteKinds - 1);
      target = std::clamp(target, 0.0, 1.0);
      if (mute != mute_)
         snap_ = true;
      double amount = snap_ ? target : amount_ + (target - amount_) * glide_;
      if (std::fabs(amount - target) < 1e-4)
         amount = target;
      snap_ = false;
      if (mute == mute_ && amount == amount_)
         return;
      // Starting from open (or back to it), the filters' memories are of nothing.
      if (!on_)
         for (Biquad &b : bq_)
            b.clear();
      mute_ = mute;
      amount_ = amount;
      on_ = mute != kOpen && amount > 0.0;
      if (!on_)
         return;
      const double a = amount;
      double db = 0.0;
      switch (mute) {
      case kStraight:
         bq_[0].lowshelf(700.0, -14.0 * a, fs_);
         bq_[1].peak(1800.0, 2.5, 8.0 * a, fs_);
         bq_[2].highshelf(std::min(6000.0, 0.4 * fs_), -3.0 * a, fs_);
         db = -1.0 * a;
         break;
      case kCup:
         bq_[0].lowshelf(200.0, -4.0 * a, fs_);
         bq_[1].peak(950.0, 2.0, 7.0 * a, fs_);
         bq_[2].highshelf(1500.0, -16.0 * a, fs_);
         db = 0.0;
         break;
      case kHarmon:
         bq_[0].lowshelf(1000.0, -15.0 * a, fs_);
         bq_[1].peak(1900.0, 4.5, 13.0 * a, fs_);
         bq_[2].highshelf(std::min(4500.0, 0.4 * fs_), -8.0 * a, fs_);
         db = -1.0 * a;
         break;
      case kPlunger: {
         // Closing: the opening's resonance falls from 4.5 kHz to 600 Hz and sharpens, and
         // what's above it is shut in.
         const double f = 4500.0 * std::pow(2.0, -2.9 * a);
         bq_[0].lowshelf(150.0, -3.0 * a, fs_);
         bq_[1].peak(f, 1.2 + 2.0 * a, 6.0 * a, fs_);
         bq_[2].highshelf(std::min(1.5 * f, 0.4 * fs_), -20.0 * a, fs_);
         db = 1.5 * a * a;
         break;
      }
      default: // bucket
         bq_[0].lowshelf(300.0, 2.0 * a, fs_);
         bq_[1].peak(3000.0, 0.7, -6.0 * a, fs_);
         bq_[2].highshelf(900.0, -12.0 * a, fs_);
         db = 4.0 * a;
         break;
      }
      gain_ = static_cast<float>(std::pow(10.0, db / 20.0));
   }
   float tick(float x) {
      if (!on_)
         return x;
      return bq_[2].tick(bq_[1].tick(bq_[0].tick(x))) * gain_;
   }
   // What the mute does inside the bore at `amount` (applyCharacter): the bell's cutoff and the
   // loss added.
   static void bore(int mute, double amount, float &cutoff, float &loss) {
      static const float cut[kMuteKinds] = {0.0f, 0.15f, 0.05f, 0.3f, 0.2f, -0.1f};
      static const float lossAdd[kMuteKinds] = {0.0f, 0.0f, 0.01f, 0.0f, 0.0f, 0.04f};
      const int m = std::clamp(mute, 0, kMuteKinds - 1);
      const float a = static_cast<float>(std::clamp(amount, 0.0, 1.0));
      cutoff *= 1.0f + cut[m] * a;
      loss += lossAdd[m] * a;
   }
   enum { kOpen, kStraight, kCup, kHarmon, kPlunger, kBucket, kMuteKinds };

private:
   Biquad bq_[3];
   double fs_ = 96000.0, amount_ = -1.0, glide_ = 1.0;
   bool snap_ = true;
   int mute_ = -1;
   bool on_ = false;
   float gain_ = 1.0f;
};

class BreathVoice {
public:
   // Allocates the models for the internal rate. Not real-time safe.
   void prepare(double internalRate) {
      fs_ = internalRate;
      for (auto &copy : models_) {
         copy[kFlute] = std::make_unique<Flute>();
         copy[kClarinet] = std::make_unique<Clarinet>();
         copy[kSax] = std::make_unique<Sax>();
         copy[kOboe] = std::make_unique<Oboe>();
         copy[kTrumpet] = std::make_unique<Trumpet>();
         copy[kAlien] = std::make_unique<Alien>();
         for (auto &m : copy)
            m->prepare(fs_);
      }
      for (auto &o : osc_)
         o.setSampleRate(fs_);
      filter_.setSampleRate(fs_);
      filter_.setSlope(jclap::dsp::Filter::Slope::Db24);
      mute_.prepare(fs_, kControl);
      airLP_.setHz(1500.0, fs_);
      roughCoef_ = 1.0 - std::exp(-2.0 * kPi * 30.0 * kControl / fs_);
      // Each of the breath's two smoothing stages (see tick()): together a held note's fall takes
      // about 35 ms to 90%, and the tongue stops the air in about 2 ms.
      fallCoef_ = coef(9.0);
      stopCoef_ = coef(0.5);
      gainCoef_ = coef(5.0);
      synthCoef_ = coef(4.0);
      envCoef_ = coef(30.0);
      pulseLength_ = std::max(1, static_cast<int>(kPulseMs * 0.001 * fs_));
      valveLength_ = std::max(1, static_cast<int>(kValveMs * 0.001 * fs_));
      valveCoef_ = coef(0.6);
      hpCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 2000.0 / fs_));
      reset();
   }
   void setSeed(uint32_t seed) {
      for (int u = 0; u < kMaxUnison; ++u)
         for (int i = 0; i < kModelCount; ++i)
            models_[u][i]->rng.s = (seed ^ (0x2545f491u * u)) * (i + 1 + 7 * u) | 1u;
      air_.s = seed ^ 0x68e31da4u;
      jitter_.s = seed ^ 0x51ed270bu;
   }

   void configure(const VoiceSettings &s) {
      // A different instrument starts from silence: a bore full of another model's sound would only
      // click.
      if (s.model != settings_.model) {
         for (auto &copy : models_)
            copy[std::clamp(s.model, 0, kModelCount - 1)]->reset();
         len_[0] = -1.0;
      }
      settings_ = s;
      settings_.model = std::clamp(s.model, 0, kModelCount - 1);
      settings_.hybridModel = std::clamp(s.hybridModel, 0, 4);
      settings_.unison = std::clamp(s.unison, 1, kMaxUnison);
      static constexpr jclap::dsp::Oscillator::Wave waves[] = {
         jclap::dsp::Oscillator::Wave::Sine, jclap::dsp::Oscillator::Wave::Triangle, jclap::dsp::Oscillator::Wave::Saw,
         jclap::dsp::Oscillator::Wave::Square, jclap::dsp::Oscillator::Wave::Pulse};
      for (auto &o : osc_) {
         o.setWave(waves[settings_.hybridModel]);
         o.setPulseWidth(0.25);
      }
      live_ = settings_;
      liveAttack_ = live_.attackMs;
      liveRelease_ = live_.releaseMs;
      liveGlide_ = live_.glideMs;
      applyCharacter();
      updateAttack();
      releaseCoef_ = coef(s.releaseMs / kReleasePerStage);
      glideCoef_ = coef(std::max(3.0, s.glideMs / 3.0));
   }

   // A note from silence (or a stolen voice): the tongue releases the air with a kick.
   //
   // A voice still sounding is never cut: the same note played again is tongued again on the air
   // and bore that are still there (as a player re-tongues), and a different note waits a few
   // milliseconds while the old one fades out, then starts from silence.
   void start(double pitch, double velocity, double gain) {
      if (fading_ > 0 || (active_ && releasing_ && std::fabs(pitch - pitch_) > 0.5 && env_ > kAudible)) {
         if (fading_ <= 0)
            fading_ = fadeLength_ = std::max(1, static_cast<int>(kFadeMs * 0.001 * fs_));
         next_ = {pitch, velocity, gain, false, 0.5};
         gain_ = gain;
         return;
      }
      begin(pitch, velocity, gain);
   }

private:
   void begin(double pitch, double velocity, double gain) {
      const bool fresh = !active_ || (releasing_ && (std::fabs(pitch - pitch_) > 0.5 || env_ <= kAudible));
      if (fresh) {
         for (auto &copy : models_)
            copy[settings_.model]->reset();
         filter_.reset();
         mute_.reset();
         // Each unison copy of the oscillator starts somewhere of its own, so they don't sum to one.
         for (int u = 0; u < kMaxUnison; ++u)
            osc_[u].reset(u == 0 ? 0.0 : 0.5 + 0.5 * jitter_.white());
         breath_ = puff_ = 0.0;
         valveAt_ = -1;
         flutterDepth_ = flutterDip_ = flutterPhase_ = 0.0;
         pitchNow_ = pitch;
         len_[0] = -1.0;
         levelNow_ = 0.0;
         hybEnv_ = 0.0;
      }
      // Velocity only blows the note if no pressure arrives in its first few milliseconds (see
      // control()), so an MPE note starts on its pressure, never on a burst from its velocity.
      hold_ = 0.0;
      // The player's level, pitch and bore length are worked out before the first sample.
      counter_ = 0;
      active_ = true;
      releasing_ = false;
      stop_ = false;
      age_ = 0;
      kickAge_ = 0;
      pulseAt_ = -1;
      velocity_ = velocity;
      tongue_ = tongueAmount();
      pressureSeen_ = false;
      gain_ = gain;
      // A player never plays two notes quite the same: a little off in pitch, breath, attack and
      // vibrato (Humanize), and each note's own detune.
      for (double &r : human_)
         r = jitter_.white();
      updateAttack();
      pitch_ = pitch;
      quiet_ = 0;
      lift_ = 0.0;
      key_ = pitch;
      random_ = jitter_.white();
      for (int i = 0; i < kLfoCount; ++i) {
         if (settings_.lfoSync[i] == 0)
            lfoPhase_[i] = 0.0;
         lfoHeld_[i] = lfoLast_[i] = jitter_.white();
      }
      swellAge_ = 0.0;
      // The HYBRID envelope starts again from where it is (from 0 after silence).
      hybStage_ = kHybAttack;
      articulate(velocity);
   }

public:
   // Mono: the tongue stops the air for 35 ms, then releases it on the new note with a kick.
   void retongue(double pitch, double velocity) {
      if (!active_ || releasing_)
         return start(pitch, velocity, gain_);
      stop_ = true;
      age_ = 0;
      kickAge_ = -static_cast<int>(0.035 * fs_);
      pulseAt_ = -1;
      velocity_ = velocity;
      tongue_ = tongueAmount();
      pitch_ = pitch;
      hold_ = 1.0;
      articulate(velocity);
      hybStage_ = kHybAttack;
   }
   // Legato: the fingers change, the air doesn't stop.
   void slur(double pitch, double velocity) {
      if (!active_ || releasing_)
         return start(pitch, velocity, gain_);
      age_ = 0;
      pitch_ = pitch;
      velocity_ = std::max(velocity_ * 0.8, velocity);
      articulate(velocity);
      // Double tonguing: every note gets its own light tongue, even slurred.
      if (settings_.tongueType == kTongueDouble) {
         kickAge_ = 0;
         pulseAt_ = -1;
         tongue_ = 0.6f * strike();
      }
   }
   void release(double velocity = 0.5) {
      if (fading_ > 0) {
         // Let go before it began (a very short tap): it starts and is let go at once.
         next_.release = true;
         next_.lift = velocity;
         return;
      }
      if (active_ && !releasing_) {
         releasing_ = true;
         lift_ = std::clamp(velocity, 0.0, 1.0);
         hybStage_ = kHybRelease;
         for (auto &copy : models_)
            if (copy[settings_.model])
               copy[settings_.model]->letGo();
      }
   }
   // Only the instrument playing has sound in it: the others were cleared when it was picked.
   void reset() {
      active_ = releasing_ = stop_ = false;
      fading_ = 0;
      breath_ = puff_ = 0.0;
      level_ = 0.0f;
      env_ = 0.0;
      hybEnv_ = 0.0;
      hybStage_ = kHybIdle;
      if (models_[0][settings_.model])
         for (auto &copy : models_)
            copy[settings_.model]->reset();
      filter_.reset();
      mute_.reset();
      len_[0] = -1.0;
   }
   // Where the note sits left to right, -1..1, before the spread: the Pan param and its routes.
   float pan() const { return static_cast<float>(live_.pan); }

   // The note's pitch in semitones (key, tune and bend), and its expression, 0..1.
   void setPitch(double pitch) { pitch_ = pitch; }
   void setExpression(double pressure, double timbre) {
      pressure_ = pressure;
      timbre_ = timbre;
   }
   void setGain(double gain) { gain_ = gain; }
   // The note's bend now, -1..1 over its bend range: the Glide source.
   void setBend(double bend) { bend_ = std::clamp(bend, -1.0, 1.0); }
   // The note's bend in semitones: how far the Alien's throat follows it (by its Bend).
   void setBendSemitones(double semis) { bendSemis_ = semis; }
   // Seconds on a clock every voice shares, for LFOs that run free of the notes.
   void setClock(const double *seconds) { clock_ = seconds; }

   // The sources and each destination's offset (in its param's 0..1 range) at the last control
   // tick, for the panel.
   const float *sources() const { return sources_; }
   const float *offsets() const { return offsets_; }

   bool isActive() const { return active_; }
   // A voice fading out for the note that takes it next already belongs to that note.
   bool isReleasing() const { return releasing_ && fading_ <= 0; }
   // A rough loudness, for stealing the quietest and for the panel.
   float level() const { return level_; }
   // The breath blowing now, 0..about 1.3, and the pitch sounding, for the panel.
   float breath() const { return static_cast<float>(breath_); }
   // The syllable of its words the Alien is singing, or -1 (and on another instrument).
   int saying() const { return models_[0][settings_.model]->saying(); }
   // The Alien's words, and which of them the next note (or slur) sings: its syllable's place.
   void setBook(const speech::Book *book) {
      for (auto &copy : models_)
         if (copy[kAlien])
            copy[kAlien]->setBook(book);
   }
   void setWord(uint32_t word) { word_ = word; }
   double hz() const { return 440.0 * std::pow(2.0, (pitchNow_ - 69.0) / 12.0); }

   // One sample at the internal rate.
   float tick() {
      if (!active_)
         return 0.0f;
      if ((counter_++ & (kControl - 1)) == 0)
         control();
      // Breath follows its target through two smoothing stages, so it leaves silence (and every
      // pressure step from the controller) with no corner: a reed or lip valve's flow goes as the
      // square root of the pressure, so a breath that starts with a slope starts with a click.
      // Each stage takes the attack time going up, the release time (or a held note's quicker
      // fall) going down, and the tongue's stop when it stops the air.
      //
      // A tongued note is a valve: the first stage is the mouth's pressure, building behind the
      // tongue, and the tongue lets it go only once it is a little past where the note speaks (or
      // nearly all there). It opens in a few ms, so the reed or lips see the pressure arrive quickly
      // and the note speaks at once, however softly. (A reed's pressure creeping up through its
      // threshold starts it late, and the slower, the later: the dynamic threshold.) A breath
      // attack has no tongue: the breath itself rises through both stages.
      const double target = targetBreath();
      const auto stage = [this](double to, double from, double up) {
         return to > from ? up : stop_ ? stopCoef_ : releasing_ ? releaseCoef_ : fallCoef_;
      };
      const bool tongued = settings_.tongueType != kTongueBreath;
      puff_ += (target - puff_) * stage(target, puff_, tongued ? puffCoef_ : attackCoef_);
      if (valveAt_ < 0 && !releasing_ && target > 0.0 && kickAge_ >= 0 &&
          (!tongued || puff_ >= std::min(0.9 * target, kValveSpeak * speak_) || kickAge_ >= kValveWaitMs * 0.001 * fs_))
         valveAt_ = 0;
      double valve = 1.0;
      if (valveAt_ < valveLength_) {
         const double x = valveAt_ < 0 ? 0.0 : static_cast<double>(valveAt_) / valveLength_;
         valve = x * x * (3.0 - 2.0 * x);
         valveAt_ += valveAt_ >= 0 ? 1 : 0;
      }
      breath_ += (puff_ * valve - breath_) * stage(puff_ * valve, breath_, tongued ? valveCoef_ : attackCoef_);
      // (A breath that has died away behind the tongue, as it stops the air between two notes,
      // waits for the tongue's release again.)
      if (stop_ && breath_ < 1e-4 && tongued)
         valveAt_ = -1;
      gainNow_ += (gainTarget_ - gainNow_) * gainCoef_;
      tongue_ *= tongueDecay_;
      ++age_;
      ++kickAge_;
      // The tongue's noise rides on the air, so there's none without breath: it comes in with the
      // air as it reaches the note's speaking pressure.
      const double airIn = std::clamp(breath_ / std::max(speak_, 0.1), 0.0, 1.0);
      const float tongue = tongue_ * static_cast<float>(airIn * airIn);
      // The tongue's release: the pressure built up behind it is let go at once, a short surge of
      // flow that sets the bore ringing, so a tongued note starts on the wave it makes instead of
      // growing out of the reed's noise. It comes as the tongue lets go (or, with no tongue, as the
      // air builds up), as hard as the note was struck: a soft touch has none.
      if (pulseAt_ < 0 && kickAge_ >= 0 && !releasing_ && target > 0.0 &&
          (tongued ? valveAt_ >= 0 : puff_ >= 0.6 * target && puff_ >= 0.5 * speak_)) {
         pulseAt_ = 0;
         pulseAmp_ = articulation().surge * registerKick() * std::min(4.0 * live_.kick, 2.0) * kickScale() * kickOf(strike()) /
                     kickOf(1.0) * puff_;
      }
      double surge = 0.0;
      if (pulseAt_ >= 0 && pulseAt_ < pulseLength_) {
         const double x = std::sin(kPi * pulseAt_ / pulseLength_);
         surge = pulseAmp_ * x * x;
         ++pulseAt_;
      }
      // A growl in the throat or a fluttering tongue roughens the air on its way in.
      // A flutter tongue chops the air, but never below what keeps the note's reed or lips going (a
      // reed that stops has to start again, and a slow one wouldn't before the next beat): it
      // roughens a soft note instead of silencing it.
      const double air = breath_ + surge, floor = std::min(air, kFlutterFloor * speak_);
      const float blown = static_cast<float>((air - flutterDip_ * (air - floor)) * growlShake_);
      const float mix = static_cast<float>(live_.hybridMix);
      float y = 0.0f;
      if (mix < 0.999f) {
         // Unison: copies of the instrument, each a little detuned, summed at equal power.
         float sum = 0.0f;
         airPos_ = (airPos_ + 1) & (kAirLen - 1);
         airBlown_[airPos_] = blown;
         airTongue_[airPos_] = tongue;
         for (int u = 0; u < unison_; ++u) {
            len_[u] += lenStep_[u];
            // Each copy past the first is blown a few ms behind it (see the unison's note-on).
            float b = blown, tg = tongue;
            if (u > 0 && stagger_[u] > 0) {
               const int at = (airPos_ - stagger_[u]) & (kAirLen - 1);
               b = airBlown_[at];
               tg = airTongue_[at];
            }
            sum += models_[u][settings_.model]->tick(b, static_cast<float>(emb_), len_[u], tg);
         }
         y = mute_.tick(sum * unisonGain_ * static_cast<float>(gainNow_) * (1.0f - mix));
      }
      if (mix > 0.0f) {
         // HYBRID: the oscillator, as loud as the breath blows it, through a lowpass the breath opens.
         synthLevel_ += (synthTarget_ - synthLevel_) * synthCoef_;
         float sum = 0.0f;
         for (int u = 0; u < unison_; ++u)
            sum += osc_[u].process();
         const float shaken = static_cast<float>(shake_);
         y += filter_.process(sum * unisonGain_) * static_cast<float>(synthLevel_ * gain_ * levelMod_) * shaken * mix;
      }
      // Air: the breath heard around the instrument, not through it.
      if (live_.air > 0.0) {
         const float w = air_.white();
         const float hiss = w - airLP_.tick(w);
         y += hiss * static_cast<float>(live_.air * 0.05 * std::sqrt(std::max(0.0, breath_)) * gain_ * levelMod_);
      }
      if (!std::isfinite(y)) {
         // A model pushed somewhere it can't go: silence it rather than the whole plugin.
         reset();
         return 0.0f;
      }
      if (fading_ > 0) {
         // Fading out for the next note: a quarter cosine to nothing, then that note starts.
         const double f = static_cast<double>(fading_) / fadeLength_;
         y *= static_cast<float>(std::sin(0.5 * kPi * f));
         if (--fading_ == 0) {
            const Next n = next_;
            reset();
            begin(n.pitch, n.velocity, n.gain);
            if (n.release)
               release(n.lift);
         }
      }
      env_ += (std::fabs(y) - env_) * envCoef_;
      // How knotted the sound is: the share of it above about 2 kHz.
      hpX_ += (y - hpX_) * hpCoef_;
      const float hi = y - hpX_;
      hiEnv_ += (std::fabs(hi) - hiEnv_) * envCoef_;
      return y;
   }

private:
   static constexpr int kControl = 16;
   // The articulation's shape (see targetBreath() and start()): how long the tongue's burst lasts,
   // the extra breath that starts a soft note and how long it lasts, how long velocity waits for
   // pressure, how quickly a sounding voice fades for the next note, and a level under which a
   // voice is silent enough to restart at once. Each release stage is the Release time over this.
   static constexpr double kKickMs = 25.0, kSupportMs = 60.0, kWaitMs = 6.0, kFadeMs = 4.0,
                           kPulseMs = 4.0, kShortestAttackMs = 2.0;
   // The tongue's valve: it lets go once the mouth's pressure is this far past where the note
   // speaks (or after this long, so a slow Attack still swells), and opens over this long.
   static constexpr double kValveSpeak = 1.15, kValveWaitMs = 40.0, kValveMs = 3.0;
   // The least a flutter tongue leaves of the air, against where the note speaks.
   static constexpr double kFlutterFloor = 1.0;
   static constexpr double kAudible = 3e-4, kReleasePerStage = 6.7;
   // The note waiting for a fade to finish, and whether it was let go meanwhile (and how).
   struct Next {
      double pitch, velocity, gain;
      bool release;
      double lift;
   };
   Next next_{60.0, 1.0, 1.0, false, 0.5};
   int fading_ = 0, fadeLength_ = 1;
   uint32_t word_ = 0;
   // The tongue's release surge: where it is (-1 before it), how long, and how big.
   int pulseAt_ = -1, pulseLength_ = 1;
   // The tongue's valve: how far it has opened (-1 shut, waiting for the pressure behind it) and
   // how long it takes.
   int valveAt_ = 0, valveLength_ = 1;
   double pulseAmp_ = 0.0;

   Model &model() { return *models_[0][settings_.model]; }
   // A new note (or a slurred one) for every copy: the Alien's next syllable, its consonant as hard
   // as the note is struck or blown.
   void articulate(double velocity) {
      // (The pressure only once it's been played: a fresh note's is the last one's.)
      const double hardness = std::clamp(pressureSeen_ ? std::max(velocity, pressure_) : velocity, 0.0, 1.0);
      for (auto &copies : models_)
         if (copies[settings_.model])
            copies[settings_.model]->articulate(word_, hardness);
   }
   double coef(double ms) const { return 1.0 - std::exp(-1000.0 / (std::max(ms, 0.05) * fs_)); }

   // The breath the player is aiming for now.
   double targetBreath() const {
      if (releasing_ || (stop_ && kickAge_ < 0))
         return 0.0;
      double level = levelNow_;
      if (level <= 0.0)
         return 0.0;
      double b = breathAt(settings_.model, speak_, level);
      if (kickAge_ >= 0) {
         // A reed or lips only start once the pressure is a little past where they speak (the
         // dynamic threshold): a player gives a soft note that much more at its start, then eases
         // to the note's own breath, so even the softest touch speaks in good time.
         // (The hybrid voice speaks at any breath, so as it takes over it needs no more than a reed's
         // usual support.)
         const double blown = std::min(1.0, level / 0.03);
         const double reed = supportNow_;
         const double support = reed + (std::min(reed, 0.25) - reed) * live_.hybridMix;
         b = std::max(b, blown * speak_ * (1.0 + support * supportEnv_));
         // The tongue's release lets out the pressure built up behind it, a burst that kicks the
         // oscillation off: none for a soft touch ("da"), up to the Kick for a hard one ("ta").
         // It lifts the breath over where the note speaks (pushing a note near its threshold harder
         // would only make a soft note's start many times louder than the note).
         b += articulation().kick * registerKick() * live_.kick * kickScale() * kickOf(strike()) * kickEnv_ *
              std::max(0.0, b - speak_);
      }
      return b * vibBreath_ * (1.0 + 0.12 * live_.humanize * human_[1]);
   }
   // The breath for the player's level on `model`, whose note starts speaking at `speak`: from
   // there at the lightest touch up to the model's full breath (its offset and scale, times the
   // Breath knob), so every pressure above nothing sounds; under the lightest touch it fades out.
   double breathAt(int model, double speak, double level) const {
      if (level <= 0.0)
         return 0.0;
      const ModelInfo &info = modelInfo(model);
      const double top = std::max(info.breathOffset + info.breathScale * live_.breath, 1.08 * speak + 0.02);
      const double from = std::min(speak * 1.02, top);
      return (from + (top - from) * std::pow(level, live_.curve)) * std::min(1.0, level / 0.03);
   }
   // How hard the note was struck, 0..1: the tongue's force.
   float strike() const { return static_cast<float>(std::clamp(velocity_, 0.0, 1.0)); }
   // The tongue's burst for a strike, over the Kick: rising smoothly from nothing at the softest.
   // A high note's shorter period builds up sooner and takes less of the tongue's burst before it
   // settles (a real ff attack overshoots less up high), so the kick halves for each octave over
   // the instrument's middle (and no more below it, where a burst would overblow a low note).
   double registerKick() const {
      return std::clamp(std::exp2((articulation().middle - pitch_) / 12.0), 0.25, 1.0);
   }
   static double kickOf(double strike) { return 1.6 * strike * std::sqrt(strike); }
   // How many times longer this note's period is than the instrument's middle note's. A reed or lip
   // grows by about the same share on each trip round the bore, so a low note takes that many
   // times longer to build up, and a player holds the extra breath that starts it that much longer
   // (and gives a low note a little more of it); a high note speaks at once and is eased sooner,
   // or the extra breath would only make its start louder than the note.
   double periodRatio() const { return std::exp2((articulation().middle - pitch_) / 12.0); }
   double supportMs() const { return kSupportMs * std::clamp(periodRatio(), 0.5, 2.5); }
   double supportAmount() const {
      return articulation().support * std::pow(std::clamp(periodRatio(), 1.0, 2.5), articulation().low);
   }

   // Each tongue's burst of pressure, its noise, and how it eases the attack.
   double kickScale() const {
      switch (settings_.tongueType) {
      case kTongueSoft:
         return 0.35;
      case kTongueBreath:
         return 0.0;
      default:
         return 1.0;
      }
   }
   // The tongue's noise burst: its style's, as hard as the note was struck (a soft touch barely
   // tongues at all).
   float tongueAmount() const {
      const float s = strike(), force = 0.06f + 0.94f * s * s;
      switch (settings_.tongueType) {
      case kTongueSoft:
         return 0.35f * force * static_cast<float>(articulation().noise);
      case kTongueBreath:
         return 0.0f;
      default:
         return force * static_cast<float>(articulation().noise);
      }
   }
   // How long the breath takes to reach the player's pressure (to 90%): the Attack time, scaled by
   // how hard the note was struck, evenly in log time from 1.4 times it at the softest touch to 0.4
   // times it at the hardest (35 ms down to 10 ms at the default 25 ms), and slower for the soft
   // and breath styles. On a tongued note it builds behind the tongue, which lets it go as it
   // passes where the note speaks (see tick()); the instrument's own build-up comes after.
   double attackScale() const {
      const double t = settings_.tongueType == kTongueSoft ? 1.6 : settings_.tongueType == kTongueBreath ? 2.0 : 1.0;
      return t * 1.4 * std::pow(0.4 / 1.4, strike()) * (1.0 + 0.6 * live_.humanize * human_[2]);
   }
   // Two stages of tau each reach 90% in about 3.9 tau (a breath attack), one in about 2.3 tau (the
   // mouth's pressure behind a tongue, its valve and a short smoothing after it). Never quicker than
   // a tongue lets go: a breath that steps would click.
   void updateAttack() {
      const double ms = std::max(kShortestAttackMs, liveAttack_ * attackScale());
      attackCoef_ = coef(ms / 3.9);
      puffCoef_ = coef(ms / 2.3);
   }

   // How each instrument takes the tongue: the size of its release surge, its kick, the extra
   // breath that starts its soft notes, and its tongue noise (all over the shared shape above). A
   // clarinet's reed speaks at once and its tone is the bore's pressure differentiated, so it needs
   // little surge or support; a flute's jet overblows on a burst, so it takes less kick, and its
   // edge already hisses, so half the noise (a real flute's attack is no noisier up top than its
   // note); a trumpet's lips blare on a burst, so less kick; the conical reeds build up slowly
   // from the tongue's start, so they take it all (the oboe a little less: its low notes overshoot
   // on their own), and more support for their soft notes (a slow reed would otherwise start a soft
   // note and lose it), more again on their low notes (support times periodRatio^low); the Alien's
   // twin reeds are the clarinet's valve on a slow helix, between the two. The middle note the
   // kick and the support's timing are set for.
   struct Articulation {
      double surge, kick, support, noise, middle, low;
   };
   const Articulation &articulation() const {
      static const Articulation table[kModelCount] = {
         {1.0, 0.6, 0.25, 0.5, 72.0, 0.0}, {0.25, 0.6, 0.1, 1.0, 62.0, 0.0}, {1.0, 1.0, 0.6, 1.0, 61.0, 0.8},
         {1.0, 0.7, 0.6, 1.0, 69.0, 0.8},  {1.0, 0.4, 0.25, 1.0, 65.0, 0.0},  {0.25, 0.4, 0.3, 0.5, 62.0, 0.5},
      };
      return table[settings_.model];
   }
   // The breath noise's share at the breath blowing now: under a third at the softest, rising to all
   // of it at full breath (three quarters of it half way up). A soft note's tone falls away faster than the rush of air through the
   // reed, jet or lips, so at its full share the noise stood out over a soft note (as much as 20 dB
   // more of it, against the tone, than at forte); a player blowing softly also blows less air.
   // (Not the Alien's: its breath is a voice's.)
   double softNoise() const {
      if (settings_.model == kAlien || breath_ <= 0.0)
         return 1.0;
      const ModelInfo &info = modelInfo(settings_.model);
      const double top = std::max(info.breathOffset + info.breathScale * live_.breath, 1.08 * speak_ + 0.02);
      const double dyn = std::clamp((breath_ - speak_) / std::max(0.05, top - speak_), 0.0, 1.0);
      return 0.3 + 0.7 * std::pow(dyn, 0.7);
   }
   void applyCharacter() {
      // HOLES: the tone-hole lattice below the sounding hole. Above its cutoff the waves leave
      // through the open holes instead of reflecting, so the bore stops supporting those harmonics
      // (Benade: f_c ~ 0.11 c (b/a) / sqrt(s t)). Fewer open holes radiate and pass less (a
      // single open hole above a closed tube reflects highs from further down, out of tune with the
      // note), so the effective cutoff falls; all open is the bore simply cut off at the hole.
      // On the trumpet the same scale moves the bell's cutoff. Lattice 7 is as each model was tuned.
      static const float holes[] = {0.5f, 0.64f, 0.8f, 1.0f, 1.22f, 1.5f};
      // TUBE: what the inner surface does to each trip (the only part of the material that really
      // matters inside the bore, Backus 1964, Coltman 1971): viscothermal losses at the wall, larger
      // on porous or rough wood, which grow with frequency (so they also pull the cutoff down), and
      // more turbulence noise over a rough bore. The wall's own vibration and the stylised colour
      // are the Body's (output.h).
      struct Surface { float loss, cut, noise; };
      static const Surface tubes[] = {
         {0.0f, 1.0f, 1.0f},     // grenadilla: dense, oiled, smooth: as tuned
         {0.03f, 0.86f, 1.1f},   // boxwood: more open grain
         {0.06f, 0.74f, 1.2f},   // maple: porous, soft
         {0.05f, 0.82f, 1.6f},   // bamboo: rough, with the nodes' ridges
         {-0.02f, 1.08f, 1.0f},  // brass: drawn smooth
         {-0.025f, 1.12f, 0.95f},// silver: polished
         {0.015f, 0.93f, 1.0f},  // resin: smooth, but soft and damped
         {-0.03f, 1.18f, 0.9f},  // glass: the smoothest and hardest
      };
      const float holeK = holes[std::clamp(live_.holes, 0, 5)];
      const Surface &tube = tubes[std::clamp(live_.tube, 0, 7)];
      mute_.set(settings_.mute, live_.muteAmount);
      const float noise = static_cast<float>(live_.noise) * tube.noise * softNoise();
      for (auto &copy : models_) {
         Character &c = copy[settings_.model]->character;
         c.noise = noise;
         c.edge = static_cast<float>(live_.edge);
         c.growl = static_cast<float>(1.0 + 4.0 * live_.growl); // the reed's own rasp at 0, five times at 1
         c.stiffness = static_cast<float>(live_.stiffness);
         c.aperture = static_cast<float>(live_.aperture);
         c.cone = static_cast<float>(live_.cone);
         c.brass = static_cast<float>(live_.brass);
         c.vowel = static_cast<float>(live_.alienVoice);
         c.morph = static_cast<float>(live_.alienMorph);
         c.drift = static_cast<float>(live_.alienDrift);
         c.shift = static_cast<float>(live_.alienShift);
         c.tuneSemis = static_cast<float>(settings_.tune);
         c.sharp = static_cast<float>(live_.alienSharp);
         c.bow = static_cast<float>(live_.alienBow);
         c.talk = static_cast<float>(live_.alienTalk);
         c.paceSec = static_cast<float>(live_.alienPaceSec);
         c.linger = static_cast<float>(live_.alienLinger);
         c.glideMs = static_cast<float>(live_.alienGlide);
         c.bendSemis = static_cast<float>(bendSemis_ * live_.alienBend);
         c.consonants = static_cast<float>(live_.alienStrength);
         c.sounds = live_.alienSounds;
         c.vowels = live_.alienVowels;
         c.order = live_.alienOrder;
         c.breathy = static_cast<float>(live_.alienBreathy);
         c.spread = static_cast<float>(live_.alienSpread);
         c.flip = static_cast<float>(live_.alienFlip);
         c.follow = static_cast<float>(live_.alienFollow);
         c.typed = live_.alienTyped;
         c.rough = static_cast<float>(live_.alienRough);
         c.rasp = static_cast<float>(live_.alienGrowl);
         c.kick = static_cast<float>(live_.kick);
         c.wet = static_cast<float>(live_.alienWet);
         float cutoff = static_cast<float>(live_.cutoffHz / 1300.0) * holeK * tube.cut, loss = tube.loss;
         Mute::bore(settings_.mute, mute_.amount(), cutoff, loss);
         c.loss = std::max(0.0f, static_cast<float>(live_.loss) + loss);
         c.cutoff = std::clamp(cutoff, 0.25f, 3.1f);
      }
   }

   // Auto and Mix: velocity blows a note that has no pressure. It waits a few milliseconds for
   // the pressure an MPE controller sends just after its note-on (a hard strike starting on its
   // velocity and then falling to a light pressure would be a burst, not a note); with none, the
   // velocity holds the note, until pressure arrives and takes over.
   void holdVelocity(double dt) {
      if (pressureSeen_)
         hold_ *= std::exp(-dt / 0.03);
      else if (age_ >= kWaitMs * 0.001 * fs_)
         hold_ = 1.0;
   }

   // Every kControl samples: the player's level, vibrato, pitch and the bore length.
   void control() {
      const double dt = kControl / fs_;
      if (stop_ && kickAge_ >= 0)
         stop_ = false;
      kickEnv_ = kickAge_ >= 0 ? std::exp(-kickAge_ / (kKickMs * 0.001 * fs_)) : 0.0;
      supportEnv_ = kickAge_ >= 0 ? std::exp(-kickAge_ / (supportMs() * 0.001 * fs_)) : 0.0;
      supportNow_ = supportAmount();
      tongueDecay_ = static_cast<float>(std::exp(-1.0 / (0.017 * fs_)));

      // Breath from velocity, pressure, the Breath In input, (Auto) velocity until pressure or the
      // input takes over, or (Mix) the input shaped by each note's pressure or velocity.
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
      case 4:
         // Mix: Breath In is the lungs, shared by every note, and each note's own pressure (or its
         // velocity, until pressure arrives) is how much of it that note lets through.
         if (pressure_ > 0.002)
            pressureSeen_ = true;
         holdVelocity(dt);
         levelNow_ = settings_.breathIn * std::max(pressure_, vel * hold_);
         break;
      default: {
         // Velocity holds the note until pressure (or the input) arrives; from then on the pressure
         // is the breath and the velocity only how the note was tongued.
         const double blown = std::max(pressure_, settings_.breathIn);
         if (blown > 0.002)
            pressureSeen_ = true;
         holdVelocity(dt);
         levelNow_ = std::max(blown, vel * hold_);
         break;
      }
      }

      modulate(dt);

      // Vibrato grows in after its delay; its rate and depth wander a little.
      const double hum = live_.humanize;
      const double vd = std::clamp((age_ / fs_ - live_.vibDelayMs * (1.0 + 0.3 * hum * human_[3]) / 1000.0) / 0.6, 0.0, 1.0);
      // (Steps scaled from the prototype's, which wandered every 256 samples at 96 kHz.)
      const double walk = std::sqrt(dt * 375.0), pull = 0.02 * dt * 375.0;
      vibRate_ += pull * (live_.vibRate * (1.0 + 0.06 * hum * human_[4]) - vibRate_) + 0.05 * jitter_.white() * walk;
      vibAmt_ += pull * (1.0 - vibAmt_) + 0.04 * jitter_.white() * walk;
      vibPhase_ += 2 * kPi * vibRate_ * dt;
      if (vibPhase_ > 2 * kPi)
         vibPhase_ -= 2 * kPi;
      const double vib = vibAmt_ * vd * std::sin(vibPhase_);
      const int vt = settings_.vibType;
      vibBreath_ = vt == kVibBreath || vt == kVibBoth ? 1.0 + live_.vibDepth * vib : 1.0;
      const double vibEmb = vt == kVibJaw || vt == kVibBoth ? 1.6 * live_.vibDepth * vib : 0.0;
      const double vibCents = live_.vibPitch + (vt == kVibLip ? 150.0 * live_.vibDepth : 0.0);

      // Growl: a rough, low flutter of the throat; Flutter tongue: a rolled R through the note.
      const double growl = live_.growl;
      rough_ += (jitter_.white() - rough_) * roughCoef_;
      shake_ = 1.0 + growl * 0.35 * std::clamp(6.0 * rough_, -1.0, 1.0);
      // The Flutter knob rolls it on any style; the Flutter style rolls it at least halfway. It
      // rolls from the tongue's release, coming in and going out over a beat or so (a flutter that
      // started or stopped at the bottom of a beat would step the air).
      const double flutter = std::max(live_.flutter, settings_.tongueType == kTongueFlutter ? 0.5 : 0.0);
      const bool rolling = flutter > 0.0 && !releasing_ && valveAt_ >= 0;
      flutterDepth_ += ((rolling ? flutter : 0.0) - flutterDepth_) * (1.0 - std::exp(-dt / 0.025));
      flutterDip_ = 0.0;
      if (flutterDepth_ > 1e-4) {
         flutterPhase_ += live_.flutterRate * dt;
         if (flutterPhase_ >= 1.0) {
            flutterPhase_ -= 1.0;
            if (rolling)
               tongue_ = std::max(tongue_, static_cast<float>(0.6 * flutter));
         }
         const double c = 0.5 - 0.5 * std::cos(2 * kPi * flutterPhase_);
         flutterDip_ = 0.9 * flutterDepth_ * c * c;
      }
      growlShake_ = shake_;
      shake_ *= 1.0 - flutterDip_;

      // Embouchure: the knob, moved by MPE slide (and a jaw vibrato).
      emb_ = std::clamp(live_.embouchure + settings_.slide * (timbre_ - 0.5) * 2.0 + vibEmb, 0.0, 1.0);

      // Pitch glides to the note (a finger lands in a couple of ms, or the Glide time).
      const double g = 1.0 - std::pow(1.0 - glideCoef_, kControl);
      pitchNow_ += (pitch_ - pitchNow_) * g;
      const double sounding = pitchNow_ + pitchMod_ + vibCents / 100.0 * vib +
                              (10.0 * hum * human_[0] + live_.detune * human_[5]) / 100.0;
      const Tuning t = TuningTable::get().at(tuningRow(settings_.model, settings_.tune), sounding, fs_);
      speak_ = t.speak;
      const double hz = 440.0 * std::pow(2.0, (sounding - 69.0) / 12.0 + t.octaves);
      Model &m = model();
      m.notePitch(hz);
      // A player keeps a note in tune as they blow softer or harder (a flautist rolls the
      // headjoint in, a trumpeter lips up): the bore is shortened or lengthened by the model's drift
      // at this breath, measured against the breath its tuning was baked at.
      const double drift = breathDrift(settings_.model, breath_) - breathDrift(settings_.model, modelInfo(settings_.model).tuneBreath);
      const double len = (m.nominal(hz) * t.ratio - m.cutoffDelay(hz)) *
                         std::pow(2.0, drift / 1200.0);
      // Unison: the copies spread evenly across the detune, the first in the middle of an odd count.
      const int unison = std::clamp(settings_.unison, 1, kMaxUnison);
      const bool fresh = len_[0] < 0.0;
      for (int u = 0; u < unison; ++u) {
         const double cents = unison == 1 ? 0.0 : live_.unisonDetune * (static_cast<double>(u) / (unison - 1) - 0.5);
         const double lu = len * std::pow(2.0, cents / 1200.0);
         if (fresh || u >= unison_) {
            if (!fresh)
               models_[u][settings_.model]->reset();
            len_[u] = lu;
            lenStep_[u] = 0.0;
            // The copies come in a few ms apart, as players in a section do: copies that started
            // together would beat in step, so the unison's level would sink and swell with them.
            // Each is blown by the breath as it was a few ms earlier, so it starts just as smoothly.
            // (Copies with no detune play as one, in step; so the spread grows with the detune.)
            static const double kStagger[kMaxUnison] = {0.0, 0.0047, 0.0029, 0.0071};
            const double spread = std::clamp(std::fabs(live_.unisonDetune) / 10.0, 0.0, 1.0);
            stagger_[u] = std::min(kAirLen - 1, static_cast<int>(kStagger[u] * spread * fs_));
            if (fresh && u == 0) {
               std::fill(std::begin(airBlown_), std::end(airBlown_), 0.0f);
               std::fill(std::begin(airTongue_), std::end(airTongue_), 0.0f);
            }
         } else {
            lenStep_[u] = (lu - len_[u]) / kControl;
         }
      }
      for (int u = 1; u < unison; ++u)
         models_[u][settings_.model]->notePitch(hz);
      unison_ = unison;
      unisonGain_ = 1.0f / std::sqrt(static_cast<float>(unison));
      if (fresh)
         gainNow_ = t.gain * gain_ * levelMod_;
      gainTarget_ = t.gain * gain_ * levelMod_;

      // HYBRID: the oscillator at its own octave and detune (and the unison's), as loud as the breath
      // blows it against the instrument's full breath, through a lowpass that opens with the breath
      // and follows the note half way.
      if (live_.hybridMix > 0.0) {
         const double hs = sounding + 12.0 * settings_.hybridOctave + live_.hybridDetune / 100.0;
         const double hhz = 440.0 * std::pow(2.0, (hs - 69.0) / 12.0);
         for (int u = 0; u < unison; ++u) {
            const double cents = unison == 1 ? 0.0 : live_.unisonDetune * (static_cast<double>(u) / (unison - 1) - 0.5);
            osc_[u].setFrequency(hhz * std::pow(2.0, cents / 1200.0));
         }
         const ModelInfo &info = modelInfo(settings_.model);
         const double full = std::max(info.breathOffset + info.breathScale * live_.breath, 0.1);
         const double blow = std::clamp(breath_ / full, 0.0, 1.5);
         stepHybridEnvelope();
         // Follow mixes a level blown by the breath with one set by the note's velocity alone.
         const double blown = std::pow(blow, 1.5 / std::max(0.5, live_.hybridBreath));
         const double struck = 0.45 + 0.55 * strike();
         const double follow = std::clamp(live_.hybridFollow, 0.0, 1.0);
         synthTarget_ = 0.18 * hybEnv_ * (follow * blown + (1.0 - follow) * struck);
         // The breath opens the lowpass in proportion to how much the level follows it.
         const double open = std::pow(2.0, live_.hybridFilterBreath * 6.0 * (blow - 0.7) * (0.25 + 0.75 * follow) +
                                               live_.hybridFilterEnv * 5.0 * hybEnv_);
         filter_.setCutoff(std::clamp(live_.hybridCutoff * open * std::sqrt(hhz / 261.63), 30.0, 0.45 * fs_));
         filter_.setResonance(live_.hybridResonance);
      } else {
         synthLevel_ = synthTarget_ = 0.0;
         hybEnv_ = 0.0;
      }

      // Silent long enough after letting go (or tongued to nothing): the voice is free.
      level_ = static_cast<float>(env_);
      // A HYBRID pad still in its release keeps the voice going after the breath has stopped.
      // So does the Alien, still saying its closing consonants.
      const bool padding = (live_.hybridMix > 0.0 && hybEnv_ > 1e-4 && live_.hybridFollow < 0.999) ||
                           (live_.hybridMix < 0.999 && models_[0][settings_.model]->ringing());
      if (releasing_ && fading_ <= 0 && breath_ < 1e-4 && env_ < 2e-5 && !padding) {
         if (++quiet_ > static_cast<int>(0.02 * fs_ / kControl))
            reset();
      } else {
         quiet_ = 0;
      }
   }

   // The HYBRID envelope, one control tick on: a straight attack, then decay and release that fall
   // like a real envelope (by 99% over their times).
   void stepHybridEnvelope() {
      const double dt = kControl / fs_;
      const auto rate = [&](double ms) { return 1.0 - std::exp(-dt * 4.6 / std::max(0.001, ms / 1000.0)); };
      const double sustain = std::clamp(live_.hybridSustain, 0.0, 1.0);
      switch (hybStage_) {
      case kHybAttack:
         hybEnv_ += dt / std::max(0.001, live_.hybridAttackMs / 1000.0);
         if (hybEnv_ >= 1.0) {
            hybEnv_ = 1.0;
            hybStage_ = kHybDecay;
         }
         break;
      case kHybDecay:
         hybEnv_ += (sustain - hybEnv_) * rate(live_.hybridDecayMs);
         if (std::fabs(hybEnv_ - sustain) < 1e-4)
            hybStage_ = kHybSustain;
         break;
      case kHybSustain:
         hybEnv_ = sustain;
         break;
      case kHybRelease:
         hybEnv_ -= hybEnv_ * rate(live_.hybridReleaseMs);
         if (hybEnv_ < 1e-5) {
            hybEnv_ = 0.0;
            hybStage_ = kHybIdle;
         }
         break;
      default:
         hybEnv_ = 0.0;
         break;
      }
   }

   // One generator's value now: an LFO -1..1.
   double lfo(int i, double dt) {
      double &ph = lfoPhase_[i];
      if (live_.lfoSync[i] == 1 && clock_)
         ph = std::fmod(*clock_ * live_.lfoRate[i], 1.0);
      else
         ph += live_.lfoRate[i] * dt;
      if (ph >= 1.0) {
         ph -= std::floor(ph);
         lfoLast_[i] = lfoHeld_[i];
         lfoHeld_[i] = jitter_.white();
      }
      const double x = ph;
      switch (live_.lfoShape[i]) {
      case kLfoTriangle:
         return 1.0 - 4.0 * std::fabs(x - 0.5);
      case kLfoSaw:
         return 2.0 * x - 1.0;
      case kLfoSquare:
         return x < 0.5 ? 1.0 : -1.0;
      case kLfoSampleHold:
         return lfoHeld_[i];
      case kLfoSmooth: {
         const double f = x * x * (3 - 2 * x);
         return lfoLast_[i] + (lfoHeld_[i] - lfoLast_[i]) * f;
      }
      default:
         return std::sin(2 * kPi * x);
      }
   }
   // The Swell: one contour over its time, 0..1.
   double swell(double dt) {
      swellAge_ += dt;
      double t = swellAge_ / std::max(0.05, live_.swellTime);
      if (live_.swellLoop)
         t -= std::floor(t);
      else
         t = std::min(t, 1.0);
      switch (live_.swellShape) {
      case kSwellRise:
         return t * t * (3 - 2 * t);
      case kSwellFall:
         return 1.0 - t * t * (3 - 2 * t);
      case kSwellWave:
         return 0.5 - 0.5 * std::cos(4 * kPi * t);
      case kSwellBloom:
         return t < 0.15 ? t / 0.15 : 1.0 - 0.6 * (t - 0.15) / 0.85;
      case kSwellTide:
         return 0.5 - 0.5 * std::cos(2 * kPi * t * t);
      default:
         return std::sin(kPi * t);
      }
   }
   static double busCurve(double x, int curve) {
      switch (curve) {
      case 1:
         return x * std::fabs(x);
      case 2:
         return (x < 0 ? -1.0 : 1.0) * std::sqrt(std::fabs(x));
      case 3: {
         const double a = std::clamp(0.5 + 0.5 * x, 0.0, 1.0);
         return 2.0 * (a * a * (3 - 2 * a)) - 1.0;
      }
      case 4:
         return std::fabs(x);
      default:
         return x;
      }
   }

   // The sources now, the buses they feed, and every route's offset on its destination; then the
   // destinations' live values.
   void modulate(double dt) {
      float *src = sources_;
      const ModelInfo &info = modelInfo(settings_.model);
      src[kSrcOff] = 0.0f;
      src[kSrcStrike] = static_cast<float>(velocity_);
      src[kSrcPress] = static_cast<float>(pressure_);
      src[kSrcGlide] = static_cast<float>(bend_);
      src[kSrcSlide] = static_cast<float>(timbre_);
      src[kSrcLift] = static_cast<float>(lift_);
      src[kSrcLoop] = static_cast<float>(std::clamp(breath_, 0.0, 1.0));
      src[kSrcKnot] = static_cast<float>(std::clamp(3.0 * hiEnv_ / (env_ + 1e-6), 0.0, 1.0));
      src[kSrcFold] = static_cast<float>(std::clamp((breath_ - info.tuneBreath) / 0.35, 0.0, 1.0));
      const TuningTable &table = TuningTable::get();
      const int row = tuningRow(settings_.model, settings_.tune);
      const double lo = table.lowest(row), hi = std::max(lo + 1.0, static_cast<double>(table.highest(row)));
      src[kSrcRegister] = static_cast<float>(std::clamp((pitchNow_ - lo) / (hi - lo), 0.0, 1.0));
      src[kSrcVapor] = static_cast<float>(std::clamp(0.5 * live_.noise * std::min(1.0, breath_), 0.0, 1.0));
      // A draught: a slow random walk pulled back to the middle.
      draft_ += (-0.4 * draft_) * dt + 0.35 * std::sqrt(dt) * jitter_.white();
      draft_ = std::clamp(draft_, -1.0, 1.0);
      src[kSrcDraft] = static_cast<float>(draft_);
      src[kSrcSwell] = static_cast<float>(swell(dt));
      src[kSrcLfo1] = static_cast<float>(lfo(0, dt));
      src[kSrcLfo2] = static_cast<float>(lfo(1, dt));
      src[kSrcKey] = static_cast<float>(std::clamp((key_ - 60.0) / 48.0, -1.0, 1.0));
      src[kSrcRandom] = static_cast<float>(random_);

      float *off = offsets_;
      std::fill(off, off + kModDestCount, 0.0f);
      const VoiceSettings &s = settings_;
      if (s.routeCount > 0) {
         // The buses first, from every source but the buses, then everything else from any source.
         float in[kBusCount][kRouteCount];
         int inCount[kBusCount] = {};
         for (int i = 0; i < s.routeCount; ++i) {
            const VoiceSettings::Route &r = s.routes[i];
            if (isBusDest(r.dest) && r.source < kSrcBus1) {
               const int b = r.dest - kFirstBusDest;
               in[b][inCount[b]++] = src[r.source] * r.amount;
            }
         }
         for (int b = 0; b < kBusCount; ++b) {
            double x = s.busAmount[b];
            if (inCount[b] > 0) {
               double m = in[b][0];
               for (int i = 1; i < inCount[b]; ++i) {
                  switch (s.busMode[b]) {
                  case kBusMultiply:
                     m *= in[b][i];
                     break;
                  case kBusMin:
                     m = std::min<double>(m, in[b][i]);
                     break;
                  case kBusMax:
                     m = std::max<double>(m, in[b][i]);
                     break;
                  default:
                     m += in[b][i];
                     break;
                  }
               }
               x += m;
            }
            x = busCurve(std::clamp(x, -1.0, 1.0), s.busCurve[b]);
            switch (s.busPolarity[b]) {
            case 1:
               x = 0.5 + 0.5 * x;
               break;
            case 2:
               x = 2.0 * x - 1.0;
               break;
            case 3:
               x = -x;
               break;
            default:
               break;
            }
            x = std::clamp(x, -1.0, 1.0);
            const double lagged = s.busLag[b] > 0.5 ? 1.0 - std::exp(-dt * 1000.0 / s.busLag[b]) : 1.0;
            bus_[b] += (x - bus_[b]) * lagged;
            src[kSrcBus1 + b] = static_cast<float>(bus_[b]);
         }
         for (int i = 0; i < s.routeCount; ++i) {
            const VoiceSettings::Route &r = s.routes[i];
            if (!isBusDest(r.dest))
               off[r.dest] += src[r.source] * r.amount;
         }
      }
      else {
         for (int b = 0; b < kBusCount; ++b)
            src[kSrcBus1 + b] = static_cast<float>(bus_[b] = s.busAmount[b]);
      }

      // Each destination's live value: its param plus the offset over the param's range.
      const auto at = [&](int dest, double base) {
         const auto &p = kParams[kModDestinations[dest]];
         return off[dest] == 0.0f ? base : std::clamp(base + off[dest] * (p.max - p.min), p.min, p.max);
      };
      live_.breath = at(0, s.breath);
      live_.embouchure = at(1, s.embouchure);
      live_.noise = at(2, s.noise);
      live_.edge = at(3, s.edge);
      live_.kick = at(4, s.kick);
      const double attack = at(5, s.attackMs), release = at(6, s.releaseMs);
      pitchMod_ = (at(7, s.tune) - s.tune) + (at(8, s.fine) - s.fine) / 100.0;
      const double glide = at(9, s.glideMs);
      live_.vibRate = at(10, s.vibRate);
      live_.vibDepth = at(11, s.vibDepth);
      live_.vibPitch = at(12, s.vibPitch);
      live_.vibDelayMs = at(13, s.vibDelayMs);
      live_.curve = at(14, s.curve);
      levelMod_ = off[15] == 0.0f ? 1.0 : std::pow(10.0, (at(15, s.level) - s.level) / 20.0);
      live_.lfoRate[0] = at(16, s.lfoRate[0]);
      live_.lfoRate[1] = at(17, s.lfoRate[1]);
      live_.swellTime = at(18, s.swellTime);
      live_.stiffness = at(kDest<kStiffness>, s.stiffness);
      live_.aperture = at(kDest<kAperture>, s.aperture);
      live_.growl = at(kDest<kGrowl>, s.growl);
      live_.humanize = at(kDest<kHumanize>, s.humanize);
      live_.hybridMix = at(kDest<kHybridMix>, s.hybridMix);
      live_.hybridDetune = at(kDest<kHybridDetune>, s.hybridDetune);
      live_.hybridCutoff = at(kDest<kHybridCutoff>, s.hybridCutoff);
      live_.hybridResonance = at(kDest<kHybridResonance>, s.hybridResonance);
      live_.hybridFilterBreath = at(kDest<kHybridFilterBreath>, s.hybridFilterBreath);
      live_.flutter = at(kDest<kFlutter>, s.flutter);
      live_.flutterRate = at(kDest<kFlutterRate>, s.flutterRate);
      live_.pan = at(kDest<kPan>, s.pan);
      live_.unisonDetune = at(kDest<kUnisonDetune>, s.unisonDetune);
      live_.muteAmount = at(kDest<kMuteAmount>, s.muteAmount);
      live_.loss = at(kDest<kLoss>, s.loss);
      live_.cutoffHz = at(kDest<kHoleCutoff>, s.cutoffHz);
      live_.cone = at(kDest<kCone>, s.cone);
      live_.brass = at(kDest<kBrass>, s.brass);
      live_.alienVoice = at(kDest<kAlienVoice>, s.alienVoice);
      live_.alienMorph = at(kDest<kAlienMorph>, s.alienMorph);
      live_.alienDrift = at(kDest<kAlienDrift>, s.alienDrift);
      live_.alienShift = at(kDest<kAlienShift>, s.alienShift);
      live_.alienSharp = at(kDest<kAlienSharp>, s.alienSharp);
      live_.alienBow = at(kDest<kAlienBow>, s.alienBow);
      live_.alienTalk = at(kDest<kAlienTalk>, s.alienTalk);
      live_.alienPaceSec = s.alienPaceSec;
      live_.alienLinger = at(kDest<kAlienLinger>, s.alienLinger);
      live_.alienGlide = at(kDest<kAlienGlide>, s.alienGlide);
      live_.alienBend = at(kDest<kAlienBend>, s.alienBend);
      live_.alienStrength = at(kDest<kAlienStrength>, s.alienStrength);
      live_.alienSounds = s.alienSounds;
      live_.alienVowels = s.alienVowels;
      live_.alienOrder = s.alienOrder;
      live_.alienBreathy = at(kDest<kAlienBreathy>, s.alienBreathy);
      live_.alienSpread = at(kDest<kAlienSpread>, s.alienSpread);
      live_.alienFlip = at(kDest<kAlienFlip>, s.alienFlip);
      live_.alienFollow = at(kDest<kAlienFollow>, s.alienFollow);
      live_.alienTyped = s.alienTyped;
      live_.alienRough = at(kDest<kAlienRough>, s.alienRough);
      live_.alienGrowl = at(kDest<kAlienGrowl>, s.alienGrowl);
      live_.alienWet = at(kDest<kAlienWet>, s.alienWet);
      live_.detune = at(kDest<kDetune>, s.detune);
      live_.air = at(kDest<kAir>, s.air);
      applyCharacter();
      if (attack != liveAttack_) {
         liveAttack_ = attack;
         updateAttack();
      }
      if (release != liveRelease_) {
         liveRelease_ = release;
         releaseCoef_ = coef(release / kReleasePerStage);
      }
      if (glide != liveGlide_) {
         liveGlide_ = glide;
         glideCoef_ = coef(std::max(3.0, glide / 3.0));
      }
   }

   std::unique_ptr<Model> models_[kMaxUnison][kModelCount];
   jclap::dsp::Oscillator osc_[kMaxUnison];
   jclap::dsp::Filter filter_;
   Mute mute_;
   double synthLevel_ = 0.0, synthTarget_ = 0.0, synthCoef_ = 0.001;
   // The HYBRID envelope, 0..1, stepped every control tick.
   enum HybStage { kHybIdle, kHybAttack, kHybDecay, kHybSustain, kHybRelease };
   int hybStage_ = kHybIdle;
   double hybEnv_ = 0.0;
   int unison_ = 1;
   float unisonGain_ = 1.0f;
   double human_[6] = {};
   double shake_ = 1.0, growlShake_ = 1.0, rough_ = 0.0, roughCoef_ = 0.03, flutterPhase_ = 0.0;
   double flutterDepth_ = 0.0, flutterDip_ = 0.0;
   Noise air_;
   OnePoleLP airLP_;
   VoiceSettings settings_, live_;
   float sources_[kModSourceCount] = {};
   float offsets_[kModDestCount] = {};
   const double *clock_ = nullptr;
   double lfoPhase_[kLfoCount] = {}, lfoHeld_[kLfoCount] = {}, lfoLast_[kLfoCount] = {};
   double bendSemis_ = 0.0;
   double swellAge_ = 0.0, draft_ = 0.0, random_ = 0.0, bend_ = 0.0, lift_ = 0.0, key_ = 60.0;
   double bus_[kBusCount] = {};
   double pitchMod_ = 0.0, levelMod_ = 1.0, liveAttack_ = -1.0, liveRelease_ = -1.0, liveGlide_ = -1.0;
   float hpX_ = 0.0f, hiEnv_ = 0.0f, hpCoef_ = 0.1f;
   Noise jitter_;
   double fs_ = 96000.0;
   bool active_ = false, releasing_ = false, stop_ = false;
   double breath_ = 0.0, puff_ = 0.0, levelNow_ = 0.0, hold_ = 1.0, speak_ = 0.0;
   bool pressureSeen_ = false;
   double velocity_ = 1.0, pressure_ = 0.0, timbre_ = 0.5, gain_ = 1.0;
   double pitch_ = 60.0, pitchNow_ = 60.0, emb_ = 0.5;
   // The breath and tongue of the last few ms, which the later unison copies are blown by.
   static constexpr int kAirLen = 2048;
   int stagger_[kMaxUnison] = {}, airPos_ = 0;
   float airBlown_[kAirLen] = {}, airTongue_[kAirLen] = {};
   double len_[kMaxUnison] = {-1.0, -1.0, -1.0, -1.0}, lenStep_[kMaxUnison] = {}, gainNow_ = 0.0, gainTarget_ = 0.0;
   double attackCoef_ = 0.01, puffCoef_ = 0.01, valveCoef_ = 0.1, releaseCoef_ = 0.001, glideCoef_ = 0.01;
   double fallCoef_ = 0.001, stopCoef_ = 0.01, gainCoef_ = 0.002, envCoef_ = 0.0003;
   double kickEnv_ = 0.0, supportEnv_ = 0.0, supportNow_ = 0.0, vibBreath_ = 1.0, vibRate_ = 5.1, vibAmt_ = 1.0, vibPhase_ = 0.0;
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

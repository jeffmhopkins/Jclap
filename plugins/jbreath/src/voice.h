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
   double stiffness = 0.5, aperture = 0.5, growl = 0.2, cone = 0.5, brass = 0.5;
   double loss = 0.04, cutoffHz = 1300.0, detune = 0.0, humanize = 0.0, air = 0.0;
   int tongueType = kTongueHard, vibType = kVibBreath;
   // HYBRID: a second instrument on the same breath.
   int hybridModel = kFlute, hybridOctave = 0;
   double hybridMix = 0.0, hybridDetune = 0.0, hybridBreath = 1.0;
};

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
         t.speak = a.speak + f * (b.speak - a.speak);
      } else {
         t.ratio = ra;
         t.gain = a.gain;
         t.speak = a.speak;
      }
      return t;
   }
   // The lowest and highest notes the model plays at their own pitch.
   int lowest(int model) const { return low_[std::clamp(model, 0, kModelCount - 1)]; }
   int highest(int model) const { return high_[std::clamp(model, 0, kModelCount - 1)]; }

private:
   struct Resolved {
      double ratio88 = 1.0, ratio96 = 1.0, gain = 1.0, speak = 0.0;
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
            r.speak = mix(a.speak, c.speak);
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
      hybrid_[kFlute] = std::make_unique<Flute>();
      hybrid_[kClarinet] = std::make_unique<Clarinet>();
      hybrid_[kSax] = std::make_unique<Sax>();
      hybrid_[kOboe] = std::make_unique<Oboe>();
      hybrid_[kTrumpet] = std::make_unique<Trumpet>();
      for (auto &m : models_)
         m->prepare(fs_);
      for (auto &m : hybrid_)
         m->prepare(fs_);
      airLP_.setHz(1500.0, fs_);
      roughCoef_ = 1.0 - std::exp(-2.0 * kPi * 30.0 * kControl / fs_);
      fallCoef_ = coef(15.0);
      stopCoef_ = coef(0.75);
      gainCoef_ = coef(5.0);
      envCoef_ = coef(30.0);
      hpCoef_ = static_cast<float>(1.0 - std::exp(-2.0 * kPi * 2000.0 / fs_));
      reset();
   }
   void setSeed(uint32_t seed) {
      for (int i = 0; i < kModelCount; ++i) {
         models_[i]->rng.s = seed * (i + 1) | 1u;
         hybrid_[i]->rng.s = (seed ^ 0x2545f491u) * (i + 7) | 1u;
      }
      air_.s = seed ^ 0x68e31da4u;
      jitter_.s = seed ^ 0x51ed270bu;
   }

   void configure(const VoiceSettings &s) {
      // A different instrument starts from silence: a bore full of another model's sound would only
      // click.
      if (s.model != settings_.model) {
         models_[std::clamp(s.model, 0, kModelCount - 1)]->reset();
         len_ = -1.0;
      }
      if (s.hybridModel != settings_.hybridModel) {
         hybrid_[std::clamp(s.hybridModel, 0, kModelCount - 1)]->reset();
         hlen_ = -1.0;
      }
      settings_ = s;
      settings_.model = std::clamp(s.model, 0, kModelCount - 1);
      settings_.hybridModel = std::clamp(s.hybridModel, 0, kModelCount - 1);
      live_ = settings_;
      liveAttack_ = live_.attackMs;
      liveRelease_ = live_.releaseMs;
      liveGlide_ = live_.glideMs;
      applyCharacter();
      attackCoef_ = coef(s.attackMs / 4.0 * attackScale());
      releaseCoef_ = coef(s.releaseMs / 4.0);
      glideCoef_ = coef(std::max(3.0, s.glideMs / 3.0));
   }

   // A note from silence (or a stolen voice): the tongue releases the air with a kick.
   void start(double pitch, double velocity, double gain) {
      if (!active_ || releasing_) {
         model().reset();
         hybrid().reset();
         hlen_ = -1.0;
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
      velocity_ = velocity;
      tongue_ = tongueAmount();
      flutterPhase_ = 0.0;
      pressureSeen_ = false;
      gain_ = gain;
      // A player never plays two notes quite the same: a little off in pitch, breath, attack and
      // vibrato (Humanize), and each note's own detune.
      for (double &r : human_)
         r = jitter_.white();
      attackCoef_ = coef(liveAttack_ / 4.0 * attackScale());
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
   }
   // Mono: the tongue stops the air for 35 ms, then releases it on the new note with a kick.
   void retongue(double pitch, double velocity) {
      if (!active_ || releasing_)
         return start(pitch, velocity, gain_);
      stop_ = true;
      age_ = 0;
      kickAge_ = -static_cast<int>(0.035 * fs_);
      velocity_ = velocity;
      tongue_ = tongueAmount();
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
      // Double tonguing: every note gets its own light tongue, even slurred.
      if (settings_.tongueType == kTongueDouble) {
         kickAge_ = 0;
         tongue_ = 0.6f * strike();
      }
   }
   void release(double velocity = 0.5) {
      if (active_ && !releasing_) {
         releasing_ = true;
         lift_ = std::clamp(velocity, 0.0, 1.0);
      }
   }
   // Only the instrument playing has sound in it: the others were cleared when it was picked.
   void reset() {
      active_ = releasing_ = stop_ = false;
      breath_ = 0.0;
      level_ = 0.0f;
      env_ = 0.0;
      if (models_[settings_.model])
         model().reset();
      if (hybrid_[settings_.hybridModel])
         hybrid().reset();
      hlen_ = -1.0;
   }

   // The note's pitch in semitones (key, tune and bend), and its expression, 0..1.
   void setPitch(double pitch) { pitch_ = pitch; }
   void setExpression(double pressure, double timbre) {
      pressure_ = pressure;
      timbre_ = timbre;
   }
   void setGain(double gain) { gain_ = gain; }
   // The note's bend now, -1..1 over its bend range: the Glide source.
   void setBend(double bend) { bend_ = std::clamp(bend, -1.0, 1.0); }
   // Seconds on a clock every voice shares, for LFOs that run free of the notes.
   void setClock(const double *seconds) { clock_ = seconds; }

   // The sources and each destination's offset (in its param's 0..1 range) at the last control
   // tick, for the panel.
   const float *sources() const { return sources_; }
   const float *offsets() const { return offsets_; }

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
      // A growl in the throat or a fluttering tongue roughens the air on its way in.
      const float blown = static_cast<float>(breath_ * shake_);
      const float mix = static_cast<float>(live_.hybridMix);
      float y = 0.0f;
      if (mix < 0.999f)
         y = model().tick(blown, static_cast<float>(emb_), len_, tongue) * static_cast<float>(gainNow_) * (1.0f - mix);
      if (mix > 0.0f) {
         if (hlen_ > 0.0) {
            hlen_ += hlenStep_;
            hgainNow_ += (hgainTarget_ - hgainNow_) * gainCoef_;
            y += hybrid().tick(blown * static_cast<float>(hratio_), static_cast<float>(emb_), hlen_, tongue) *
                 static_cast<float>(hgainNow_) * mix;
         }
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
      env_ += (std::fabs(y) - env_) * envCoef_;
      // How knotted the sound is: the share of it above about 2 kHz.
      hpX_ += (y - hpX_) * hpCoef_;
      const float hi = y - hpX_;
      hiEnv_ += (std::fabs(hi) - hiEnv_) * envCoef_;
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
      double b = breathAt(settings_.model, speak_, level);
      // The tongue's release comes with a burst of pressure that kicks the oscillation off: as hard
      // as the note was struck, so a soft touch starts softly and a hard one with a "ta".
      if (kickAge_ >= 0)
         b *= 1.0 + live_.kick * kickScale() * (0.15 + 1.35 * strike()) * kickEnv_;
      return b * vibBreath_ * (1.0 + 0.08 * live_.humanize * human_[1]);
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
   // The same breath for another instrument.
   double breathFor(int model, double speak) const { return breathAt(model, speak, std::max(levelNow_, 0.0)); }
   // How hard the note was struck, 0..1: the tongue's force.
   float strike() const { return static_cast<float>(std::clamp(velocity_, 0.0, 1.0)); }

   Model &hybrid() { return *hybrid_[settings_.hybridModel]; }
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
         return 0.35f * force;
      case kTongueBreath:
         return 0.0f;
      default:
         return force;
      }
   }
   // The attack: the style's, slower for a soft touch and quicker for a hard one.
   double attackScale() const {
      const double t = settings_.tongueType == kTongueSoft ? 1.8 : settings_.tongueType == kTongueBreath ? 1.3 : 1.0;
      return t * (2.2 - 1.6 * strike()) * (1.0 + 0.4 * live_.humanize * human_[2]);
   }
   void applyCharacter() {
      for (Model *m : {&model(), &hybrid()}) {
         Character &c = m->character;
         c.noise = static_cast<float>(live_.noise);
         c.edge = static_cast<float>(live_.edge);
         c.growl = static_cast<float>(live_.growl / 0.2);
         c.stiffness = static_cast<float>(live_.stiffness);
         c.aperture = static_cast<float>(live_.aperture);
         c.cone = static_cast<float>(live_.cone);
         c.brass = static_cast<float>(live_.brass);
         c.loss = static_cast<float>(live_.loss);
         c.cutoff = static_cast<float>(live_.cutoffHz / 1300.0);
      }
   }

   // Every kControl samples: the player's level, vibrato, pitch and the bore length.
   void control() {
      const double dt = kControl / fs_;
      if (stop_ && kickAge_ >= 0)
         stop_ = false;
      kickEnv_ = kickAge_ >= 0 ? std::exp(-kickAge_ / (0.04 * fs_)) : 0.0;
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
         if (pressureSeen_)
            hold_ *= std::exp(-dt / 0.03);
         levelNow_ = settings_.breathIn * std::max(pressure_, vel * hold_);
         break;
      default: {
         // Velocity holds the note until pressure (or the input) arrives; from then on the pressure
         // is the breath and the velocity only how the note was tongued.
         const double blown = std::max(pressure_, settings_.breathIn);
         if (blown > 0.002)
            pressureSeen_ = true;
         if (pressureSeen_)
            hold_ *= std::exp(-dt / 0.03);
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
      const double growl = std::max(0.0, live_.growl - 0.2) / 0.8;
      rough_ += (jitter_.white() - rough_) * roughCoef_;
      shake_ = 1.0 + growl * 0.35 * std::clamp(6.0 * rough_, -1.0, 1.0);
      if (settings_.tongueType == kTongueFlutter && !releasing_ && age_ > 0.06 * fs_) {
         flutterPhase_ += 24.0 * dt;
         if (flutterPhase_ >= 1.0) {
            flutterPhase_ -= 1.0;
            tongue_ = std::max(tongue_, 0.3f);
         }
         const double c = 0.5 + 0.5 * std::cos(2 * kPi * flutterPhase_);
         shake_ *= 1.0 - 0.45 * c * c;
      }

      // Embouchure: the knob, moved by MPE slide (and a jaw vibrato).
      emb_ = std::clamp(live_.embouchure + settings_.slide * (timbre_ - 0.5) * 2.0 + vibEmb, 0.0, 1.0);

      // Pitch glides to the note (a finger lands in a couple of ms, or the Glide time).
      const double g = 1.0 - std::pow(1.0 - glideCoef_, kControl);
      pitchNow_ += (pitch_ - pitchNow_) * g;
      const double sounding = pitchNow_ + pitchMod_ + vibCents / 100.0 * vib +
                              (6.0 * hum * human_[0] + live_.detune * human_[5]) / 100.0;
      const Tuning t = TuningTable::get().at(settings_.model, sounding, fs_);
      speak_ = t.speak;
      const double hz = 440.0 * std::pow(2.0, (sounding - 69.0) / 12.0 + t.octaves);
      Model &m = model();
      m.notePitch(hz);
      // A player keeps a note in tune as they blow softer or harder (a flautist rolls the
      // headjoint in, a trumpeter lips up): the bore is shortened or lengthened by the model's drift
      // at this breath, measured against the breath its tuning was baked at.
      const double drift = breathDrift(settings_.model, breath_) - breathDrift(settings_.model, modelInfo(settings_.model).tuneBreath);
      const double len = (m.nominal(hz) * t.ratio - (m.character.cutoff != 1.0f ? m.cutoffDelay(hz) : 0.0)) *
                         std::pow(2.0, drift / 1200.0);
      if (len_ < 0.0) {
         len_ = len;
         lenStep_ = 0.0;
         gainNow_ = t.gain * gain_ * levelMod_;
      } else {
         lenStep_ = (len - len_) / kControl;
      }
      gainTarget_ = t.gain * gain_ * levelMod_;

      // HYBRID: the second instrument at its own octave and detune, blown with the same breath.
      if (live_.hybridMix > 0.0) {
         const int hm = settings_.hybridModel;
         const double hs = sounding + 12.0 * settings_.hybridOctave + live_.hybridDetune / 100.0;
         const Tuning ht = TuningTable::get().at(hm, hs, fs_);
         const double hhz = 440.0 * std::pow(2.0, (hs - 69.0) / 12.0 + ht.octaves);
         Model &h = hybrid();
         if (hlen_ < 0.0)
            h.reset();
         h.notePitch(hhz);
         const double hlen = h.nominal(hhz) * ht.ratio - (h.character.cutoff != 1.0f ? h.cutoffDelay(hhz) : 0.0);
         if (hlen_ < 0.0) {
            hlen_ = hlen;
            hlenStep_ = 0.0;
            hgainNow_ = ht.gain * gain_ * levelMod_;
         } else {
            hlenStep_ = (hlen - hlen_) / kControl;
         }
         hgainTarget_ = ht.gain * gain_ * levelMod_;
         const double a = breathFor(settings_.model, speak_), b = breathFor(hm, ht.speak);
         if (a > 1e-4)
            hratio_ = b / a * live_.hybridBreath;
      } else {
         hlen_ = -1.0;
      }

      // Silent long enough after letting go (or tongued to nothing): the voice is free.
      level_ = static_cast<float>(env_);
      if (releasing_ && breath_ < 1e-4 && env_ < 2e-5) {
         if (++quiet_ > static_cast<int>(0.02 * fs_ / kControl))
            reset();
      } else {
         quiet_ = 0;
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
      const double lo = table.lowest(settings_.model), hi = std::max(lo + 1.0, static_cast<double>(table.highest(settings_.model)));
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
            if (r.dest >= kFirstBusDest && r.source < kSrcBus1) {
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
            if (r.dest < kFirstBusDest)
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
      live_.loss = at(kDest<kLoss>, s.loss);
      live_.cutoffHz = at(kDest<kHoleCutoff>, s.cutoffHz);
      live_.cone = at(kDest<kCone>, s.cone);
      live_.brass = at(kDest<kBrass>, s.brass);
      live_.detune = at(kDest<kDetune>, s.detune);
      live_.air = at(kDest<kAir>, s.air);
      applyCharacter();
      if (attack != liveAttack_) {
         liveAttack_ = attack;
         attackCoef_ = coef(attack / 4.0 * attackScale());
      }
      if (release != liveRelease_) {
         liveRelease_ = release;
         releaseCoef_ = coef(release / 4.0);
      }
      if (glide != liveGlide_) {
         liveGlide_ = glide;
         glideCoef_ = coef(std::max(3.0, glide / 3.0));
      }
   }

   std::unique_ptr<Model> models_[kModelCount], hybrid_[kModelCount];
   double hlen_ = -1.0, hlenStep_ = 0.0, hgainNow_ = 0.0, hgainTarget_ = 0.0, hratio_ = 1.0;
   double human_[6] = {};
   double shake_ = 1.0, rough_ = 0.0, roughCoef_ = 0.03, flutterPhase_ = 0.0;
   Noise air_;
   OnePoleLP airLP_;
   VoiceSettings settings_, live_;
   float sources_[kModSourceCount] = {};
   float offsets_[kModDestCount] = {};
   const double *clock_ = nullptr;
   double lfoPhase_[kLfoCount] = {}, lfoHeld_[kLfoCount] = {}, lfoLast_[kLfoCount] = {};
   double swellAge_ = 0.0, draft_ = 0.0, random_ = 0.0, bend_ = 0.0, lift_ = 0.0, key_ = 60.0;
   double bus_[kBusCount] = {};
   double pitchMod_ = 0.0, levelMod_ = 1.0, liveAttack_ = -1.0, liveRelease_ = -1.0, liveGlide_ = -1.0;
   float hpX_ = 0.0f, hiEnv_ = 0.0f, hpCoef_ = 0.1f;
   Noise jitter_;
   double fs_ = 96000.0;
   bool active_ = false, releasing_ = false, stop_ = false;
   double breath_ = 0.0, levelNow_ = 0.0, hold_ = 1.0, speak_ = 0.0;
   bool pressureSeen_ = false;
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

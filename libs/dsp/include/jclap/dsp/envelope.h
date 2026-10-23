#pragma once

// An ADSR envelope with analog-style curves: the attack bows slightly toward the top, decay and
// release fall exponentially like a capacitor discharging. Each time is how long a full sweep takes
// (0 to 1 for the attack, 1 to 0 for decay and release), so a decay to a high sustain is quicker. A new gate starts the attack from wherever the level is, so retriggers never click.
//
//    Envelope env;
//    env.setSampleRate(sampleRate);
//    env.setAttack(0.01);
//    env.setDecay(0.3);
//    env.setSustain(0.6);
//    env.setRelease(0.5);
//    env.gateOn();               // note on
//    env.gateOff();              // note off
//    float level = env.process();
//    if (!env.isActive()) ...    // released and silent: the voice is free
//
// Looping, it runs attack and decay over and over while the gate is held, rising from the sustain
// level each time, like a gated LFO with an envelope's shape; release ends it as usual.

#include <algorithm>
#include <cmath>

namespace jclap::dsp {

class Envelope {
public:
   enum class Stage { Idle, Attack, Decay, Sustain, Release };

   void setSampleRate(double sampleRate) {
      sampleRate_ = sampleRate;
      attack_.set(attackTime_, sampleRate_, kAttackShape);
      decay_.set(decayTime_, sampleRate_, kFallShape);
      release_.set(releaseTime_, sampleRate_, kFallShape);
   }
   // Seconds.
   void setAttack(double seconds) { attack_.set(attackTime_ = seconds, sampleRate_, kAttackShape); }
   void setDecay(double seconds) { decay_.set(decayTime_ = seconds, sampleRate_, kFallShape); }
   void setRelease(double seconds) { release_.set(releaseTime_ = seconds, sampleRate_, kFallShape); }
   // 0..1.
   void setSustain(double level) { sustain_ = std::clamp(level, 0.0, 1.0); }
   void setLoop(bool loop) { loop_ = loop; }
   bool loop() const { return loop_; }

   void gateOn() { stage_ = Stage::Attack; }
   void gateOff() {
      if (stage_ != Stage::Idle) {
         stage_ = Stage::Release;
         releaseFrom_ = level_;
      }
   }
   // Straight to silence.
   void reset() {
      stage_ = Stage::Idle;
      level_ = 0.0;
   }

   float process() {
      switch (stage_) {
      case Stage::Attack:
         level_ = attack_.toward(level_, 1.0);
         if (level_ >= 1.0) {
            level_ = 1.0;
            stage_ = Stage::Decay;
         }
         break;
      case Stage::Decay:
         level_ = decay_.toward(level_, sustain_);
         if (level_ <= sustain_) {
            level_ = sustain_;
            stage_ = loop_ ? Stage::Attack : Stage::Sustain;
         }
         break;
      case Stage::Sustain:
         level_ = sustain_;
         if (loop_)
            stage_ = Stage::Attack;
         break;
      case Stage::Release:
         level_ = release_.toward(level_, 0.0);
         if (level_ <= 0.0) {
            level_ = 0.0;
            stage_ = Stage::Idle;
         }
         break;
      case Stage::Idle:
         break;
      }
      return static_cast<float>(level_);
   }

   float level() const { return static_cast<float>(level_); }
   Stage stage() const { return stage_; }
   bool isActive() const { return stage_ != Stage::Idle; }
   bool isReleasing() const { return stage_ == Stage::Release; }
   // The level the release started from, for drawing how far along it is.
   float releaseFrom() const { return static_cast<float>(releaseFrom_); }

private:
   // How far past the target each curve aims, as a fraction of a full sweep. Aiming past it is
   // what makes the curve land in finite time; the smaller the overshoot, the more curved.
   static constexpr double kAttackShape = 0.3;
   static constexpr double kFallShape = 0.001;

   struct Curve {
      double coef = 0.0, overshoot = 0.0;
      void set(double seconds, double sampleRate, double shape) {
         overshoot = shape;
         const double samples = std::max(1.0, seconds * sampleRate);
         coef = std::exp(-std::log((1.0 + shape) / shape) / samples);
      }
      // One sample of an RC curve from `from` toward a point just past `target`.
      double toward(double from, double target) const {
         const double aim = target + (target >= from ? overshoot : -overshoot);
         return aim + (from - aim) * coef;
      }
   };

   double sampleRate_ = 48000.0;
   double attackTime_ = 0.01, decayTime_ = 0.1, releaseTime_ = 0.1, sustain_ = 1.0;
   bool loop_ = false;
   Curve attack_, decay_, release_;
   Stage stage_ = Stage::Idle;
   double level_ = 0.0, releaseFrom_ = 0.0;
};

} // namespace jclap::dsp

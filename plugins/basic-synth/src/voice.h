#pragma once

// One synth voice, built from jclap::dsp: oscillator -> filter -> VCA. Two envelopes, each able to
// loop, send to the filter, the VCA and the pitch by their own amounts, and an LFO moves the cutoff
// and the VCA. Per-note expression (MPE) moves it too: bend sets the pitch, and pressure and timbre
// open the filter.

#include <jclap/dsp/envelope.h>
#include <jclap/dsp/filter.h>
#include <jclap/dsp/lfo.h>
#include <jclap/dsp/math.h>
#include <jclap/dsp/oscillator.h>
#include <jclap/dsp/smoother.h>
#include <jclap/dsp/vca.h>

#include <algorithm>
#include <cmath>

namespace basic_synth {

constexpr int kEnvCount = 2;

// What every voice shares, set from the params once per block.
struct VoiceSettings {
   jclap::dsp::Oscillator::Wave wave = jclap::dsp::Oscillator::Wave::Saw;
   double pulseWidth = 0.5;
   jclap::dsp::Filter::Mode filterMode = jclap::dsp::Filter::Mode::LowPass;
   jclap::dsp::Filter::Slope slope = jclap::dsp::Filter::Slope::Db24;
   double cutoffHz = 2000.0, resonance = 0.3;
   // An envelope: ADSR in seconds and sustain level, whether it loops, and where it goes. Filter
   // is in octaves, pitch in semitones, both either way; VCA is 0..1.
   struct Env {
      double attack = 0.005, decay = 0.4, sustain = 0.5, release = 0.3;
      bool loop = false;
      double toFilter = 0.0, toVca = 0.0, toPitch = 0.0;
   };
   Env env[kEnvCount];
   // How far MPE moves the cutoff, in octaves at full scale. Timbre is centred on 0.5.
   double pressureAmount = 2.0, timbreAmount = 2.0;
   // The LFO: octaves of cutoff at full swing, and tremolo depth 0..1.
   jclap::dsp::Lfo::Shape lfoShape = jclap::dsp::Lfo::Shape::Triangle;
   double lfoRate = 5.0, lfoToCutoff = 0.0, lfoToVca = 0.0;
};

class SynthVoice {
public:
   void setSampleRate(double sampleRate) {
      osc_.setSampleRate(sampleRate);
      filter_.setSampleRate(sampleRate);
      for (jclap::dsp::Envelope &e : env_)
         e.setSampleRate(sampleRate);
      lfo_.setSampleRate(sampleRate);
      vca_.setSampleRate(sampleRate);
      for (jclap::dsp::Smoother *s : {&pitch_, &pressure_, &timbre_, &cutoff_, &gate_}) {
         s->setSampleRate(sampleRate);
         s->setTime(0.005);
      }
      cutoff_.setTime(0.02);
      reset();
   }

   void configure(const VoiceSettings &s) {
      osc_.setWave(s.wave);
      osc_.setPulseWidth(s.pulseWidth);
      filter_.setMode(s.filterMode);
      filter_.setSlope(s.slope);
      filter_.setResonance(s.resonance);
      for (int i = 0; i < kEnvCount; ++i) {
         env_[i].setAttack(s.env[i].attack);
         env_[i].setDecay(s.env[i].decay);
         env_[i].setSustain(s.env[i].sustain);
         env_[i].setRelease(s.env[i].release);
         env_[i].setLoop(s.env[i].loop);
         sends_[i] = s.env[i];
      }
      cutoff_.setTarget(std::log2(s.cutoffHz));
      pressureAmount_ = s.pressureAmount;
      timbreAmount_ = s.timbreAmount;
      lfo_.setShape(s.lfoShape);
      lfo_.setRate(s.lfoRate);
      lfoToCutoff_ = s.lfoToCutoff;
      lfoToVca_ = s.lfoToVca;
   }

   // Gate on at a pitch in semitones (MIDI note numbers, fractional for bends). A voice that is still
   // sounding carries on from where it is, so stealing it does not click.
   void start(double pitch, double gain) {
      pitch_.setTarget(pitch);
      vca_.setGain(gain);
      if (!isActive()) {
         osc_.reset();
         filter_.reset();
         vca_.reset();
         lfo_.reset();
         gate_.reset(0.0);
         for (jclap::dsp::Smoother *s : {&pitch_, &pressure_, &timbre_, &cutoff_})
            s->reset(s->target());
      }
      released_ = false;
      gate_.setTarget(1.0);
      for (jclap::dsp::Envelope &e : env_)
         e.gateOn();
   }
   void release() {
      released_ = true;
      gate_.setTarget(0.0);
      for (jclap::dsp::Envelope &e : env_)
         e.gateOff();
   }
   void reset() {
      released_ = true;
      gate_.reset(0.0);
      for (jclap::dsp::Envelope &e : env_)
         e.reset();
      filter_.reset();
      level_ = 0.0f;
   }

   // Bends glide over a few ms, so stepped MIDI bends sound smooth.
   void setPitch(double pitch) { pitch_.setTarget(pitch); }
   void setGain(double gain) { vca_.setGain(gain); }
   // Pressure 0..1, timbre 0..1.
   void setExpression(double pressure, double timbre) {
      pressure_.setTarget(pressure);
      timbre_.setTarget(timbre);
   }

   float process() {
      if (!isActive())
         return 0.0f;
      double toFilter = 0.0, toPitch = 0.0, toVca = 0.0, vcaShare = 0.0;
      for (int i = 0; i < kEnvCount; ++i) {
         const double e = env_[i].process();
         toFilter += sends_[i].toFilter * e;
         toPitch += sends_[i].toPitch * e;
         toVca += sends_[i].toVca * e;
         vcaShare += sends_[i].toVca;
      }
      const double lfo = lfoValue_ = lfo_.process();
      osc_.setFrequency(jclap::dsp::noteToHz(pitch_.process() + toPitch));
      const double octaves = cutoff_.process() + toFilter + pressureAmount_ * pressure_.process() +
                             timbreAmount_ * 2.0 * (timbre_.process() - 0.5) + lfoToCutoff_ * lfo;
      filter_.setCutoff(std::exp2(std::clamp(octaves, 3.0, 15.0)));
      // What the envelopes leave of the VCA follows the gate: with no envelope sent to it, the VCA
      // is simply open while the key is held. Tremolo dips that by up to its depth.
      const double gate = gate_.process() * std::max(0.0, 1.0 - vcaShare);
      const double tremolo = 1.0 - lfoToVca_ * (0.5 - 0.5 * lfo);
      level_ = static_cast<float>(std::clamp(gate + toVca, 0.0, 1.0) * tremolo);
      return vca_.process(filter_.process(osc_.process()), level_);
   }

   // Sounding until released and silent: the gate has closed and every envelope sent to the VCA
   // has finished.
   bool isActive() const {
      if (!released_ || gate_.value() > 1e-4)
         return true;
      for (int i = 0; i < kEnvCount; ++i)
         if (sends_[i].toVca > 0.0 && env_[i].isActive())
            return true;
      return false;
   }
   bool isReleasing() const { return released_; }
   // The VCA's level, for picking the quietest voice to steal.
   float level() const { return level_; }
   // The LFO's latest output, -1..1.
   double lfoValue() const { return lfoValue_; }

private:
   jclap::dsp::Oscillator osc_;
   jclap::dsp::Filter filter_;
   jclap::dsp::Envelope env_[kEnvCount];
   VoiceSettings::Env sends_[kEnvCount];
   jclap::dsp::Lfo lfo_;
   jclap::dsp::Vca vca_;
   // Pitch in semitones; cutoff in octaves (log2 Hz). The gate glides over a few ms so a gate-only
   // VCA opens and closes without clicks.
   jclap::dsp::Smoother pitch_, pressure_, timbre_, cutoff_, gate_;
   double pressureAmount_ = 0.0, timbreAmount_ = 0.0;
   double lfoToCutoff_ = 0.0, lfoToVca_ = 0.0, lfoValue_ = 0.0;
   bool released_ = true;
   float level_ = 0.0f;
};

} // namespace basic_synth

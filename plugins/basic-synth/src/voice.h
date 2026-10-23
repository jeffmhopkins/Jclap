#pragma once

// One synth voice, built from jclap::dsp: oscillator -> filter -> VCA, with an envelope for each of
// the filter and the VCA, and an LFO that can move both. Per-note expression (MPE) moves it too: bend sets the pitch, and pressure
// and timbre open the filter.

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

// What every voice shares, set from the params once per block.
struct VoiceSettings {
   jclap::dsp::Oscillator::Wave wave = jclap::dsp::Oscillator::Wave::Saw;
   double pulseWidth = 0.5;
   jclap::dsp::Filter::Mode filterMode = jclap::dsp::Filter::Mode::LowPass;
   jclap::dsp::Filter::Slope slope = jclap::dsp::Filter::Slope::Db24;
   double cutoffHz = 2000.0, resonance = 0.3;
   // An ADSR: seconds, and the sustain level.
   struct Adsr {
      double attack = 0.005, decay = 0.4, sustain = 0.5, release = 0.3;
   };
   Adsr filterEnv, ampEnv;
   // How far each source moves the cutoff, in octaves at full scale. Timbre is centred on 0.5.
   double filterEnvAmount = 2.0, pressureAmount = 2.0, timbreAmount = 2.0;
   // How much the amp envelope shapes the VCA, 0..1. At 0 the VCA just follows the gate: full while
   // the note is held, closed on release.
   double ampEnvAmount = 1.0;
   // The LFO: octaves of cutoff at full swing, and tremolo depth 0..1.
   jclap::dsp::Lfo::Shape lfoShape = jclap::dsp::Lfo::Shape::Triangle;
   double lfoRate = 5.0, lfoToCutoff = 0.0, lfoToVca = 0.0;
};

class SynthVoice {
public:
   void setSampleRate(double sampleRate) {
      osc_.setSampleRate(sampleRate);
      filter_.setSampleRate(sampleRate);
      filterEnv_.setSampleRate(sampleRate);
      ampEnv_.setSampleRate(sampleRate);
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
      set(filterEnv_, s.filterEnv);
      set(ampEnv_, s.ampEnv);
      cutoff_.setTarget(std::log2(s.cutoffHz));
      filterEnvAmount_ = s.filterEnvAmount;
      pressureAmount_ = s.pressureAmount;
      timbreAmount_ = s.timbreAmount;
      ampEnvAmount_ = s.ampEnvAmount;
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
      gate_.setTarget(1.0);
      filterEnv_.gateOn();
      ampEnv_.gateOn();
   }
   void release() {
      filterEnv_.gateOff();
      ampEnv_.gateOff();
      gate_.setTarget(0.0);
   }
   void reset() {
      filterEnv_.reset();
      ampEnv_.reset();
      filter_.reset();
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
      const double filterEnv = filterEnv_.process(), ampEnv = ampEnv_.process();
      const double lfo = lfoValue_ = lfo_.process();
      osc_.setFrequency(jclap::dsp::noteToHz(pitch_.process()));
      const double octaves = cutoff_.process() + filterEnvAmount_ * filterEnv + pressureAmount_ * pressure_.process() +
                             timbreAmount_ * 2.0 * (timbre_.process() - 0.5) + lfoToCutoff_ * lfo;
      filter_.setCutoff(std::exp2(std::clamp(octaves, 3.0, 15.0)));
      // Tremolo dips the level by up to the depth, never above where the envelope has it.
      const double tremolo = 1.0 - lfoToVca_ * (0.5 - 0.5 * lfo);
      const double control = (ampEnvAmount_ * ampEnv + (1.0 - ampEnvAmount_) * gate_.process()) * tremolo;
      return vca_.process(filter_.process(osc_.process()), static_cast<float>(control));
   }

   // The amp envelope decides how long a voice lasts.
   bool isActive() const { return ampEnv_.isActive(); }
   bool isReleasing() const { return ampEnv_.isReleasing(); }
   float level() const { return ampEnv_.level(); }
   // The LFO's latest output, -1..1.
   double lfoValue() const { return lfoValue_; }

private:
   static void set(jclap::dsp::Envelope &env, const VoiceSettings::Adsr &adsr) {
      env.setAttack(adsr.attack);
      env.setDecay(adsr.decay);
      env.setSustain(adsr.sustain);
      env.setRelease(adsr.release);
   }

   jclap::dsp::Oscillator osc_;
   jclap::dsp::Filter filter_;
   jclap::dsp::Envelope filterEnv_, ampEnv_;
   jclap::dsp::Lfo lfo_;
   jclap::dsp::Vca vca_;
   // Pitch in semitones; cutoff in octaves (log2 Hz).
   // The gate glides over a few ms so a gate-only VCA opens and closes without clicks.
   jclap::dsp::Smoother pitch_, pressure_, timbre_, cutoff_, gate_;
   double filterEnvAmount_ = 0.0, pressureAmount_ = 0.0, timbreAmount_ = 0.0, ampEnvAmount_ = 1.0;
   double lfoToCutoff_ = 0.0, lfoToVca_ = 0.0, lfoValue_ = 0.0;
};

} // namespace basic_synth

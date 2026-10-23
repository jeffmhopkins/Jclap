#pragma once

// One synth voice, built from jclap::dsp: oscillator -> filter -> VCA, with one envelope opening both
// the VCA and the filter. Per-note expression (MPE) moves it too: bend sets the pitch, and pressure
// and timbre open the filter.

#include <jclap/dsp/envelope.h>
#include <jclap/dsp/filter.h>
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
   // How far each source moves the cutoff, in octaves at full scale. Timbre is centred on 0.5.
   double envAmount = 2.0, pressureAmount = 2.0, timbreAmount = 2.0;
   // Seconds, and the sustain level.
   double attack = 0.005, decay = 0.4, sustain = 0.5, release = 0.3;
};

class SynthVoice {
public:
   void setSampleRate(double sampleRate) {
      osc_.setSampleRate(sampleRate);
      filter_.setSampleRate(sampleRate);
      env_.setSampleRate(sampleRate);
      vca_.setSampleRate(sampleRate);
      for (jclap::dsp::Smoother *s : {&pitch_, &pressure_, &timbre_, &cutoff_}) {
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
      env_.setAttack(s.attack);
      env_.setDecay(s.decay);
      env_.setSustain(s.sustain);
      env_.setRelease(s.release);
      cutoff_.setTarget(std::log2(s.cutoffHz));
      envAmount_ = s.envAmount;
      pressureAmount_ = s.pressureAmount;
      timbreAmount_ = s.timbreAmount;
   }

   // Gate on at a pitch in semitones (MIDI note numbers, fractional for bends). A voice that is still
   // sounding carries on from where it is, so stealing it does not click.
   void start(double pitch, double gain) {
      pitch_.setTarget(pitch);
      vca_.setGain(gain);
      if (!env_.isActive()) {
         osc_.reset();
         filter_.reset();
         vca_.reset();
         for (jclap::dsp::Smoother *s : {&pitch_, &pressure_, &timbre_, &cutoff_})
            s->reset(s->target());
      }
      env_.gateOn();
   }
   void release() { env_.gateOff(); }
   void reset() {
      env_.reset();
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
      if (!env_.isActive())
         return 0.0f;
      const float env = env_.process();
      osc_.setFrequency(jclap::dsp::noteToHz(pitch_.process()));
      const double octaves = cutoff_.process() + envAmount_ * env + pressureAmount_ * pressure_.process() +
                             timbreAmount_ * 2.0 * (timbre_.process() - 0.5);
      filter_.setCutoff(std::exp2(std::clamp(octaves, 3.0, 15.0)));
      return vca_.process(filter_.process(osc_.process()), env);
   }

   bool isActive() const { return env_.isActive(); }
   bool isReleasing() const { return env_.isReleasing(); }
   float level() const { return env_.level(); }

private:
   jclap::dsp::Oscillator osc_;
   jclap::dsp::Filter filter_;
   jclap::dsp::Envelope env_;
   jclap::dsp::Vca vca_;
   // Pitch in semitones; cutoff in octaves (log2 Hz).
   jclap::dsp::Smoother pitch_, pressure_, timbre_, cutoff_;
   double envAmount_ = 0.0, pressureAmount_ = 0.0, timbreAmount_ = 0.0;
};

} // namespace basic_synth

#pragma once

// One synth voice, built from jclap::dsp: oscillator (or a detuned pair) -> filter -> VCA. Two envelopes send to the
// filter, the VCA and the pitch by their own amounts. A looping envelope is not the voice's own: the
// plugin runs one shared cycle and passes its value in, so every voice moves together, and an LFO moves the cutoff,
// the VCA and the pulse width. Velocity scales the VCA and opens the filter, each by its own amount. Per-note
// expression (MPE) moves it too: bend sets the pitch, and pressure and timbre open the filter, and the cutoff can
// follow the note.

#include <jclap/dsp/envelope.h>
#include <jclap/dsp/filter.h>
#include <jclap/dsp/lfo.h>
#include <jclap/dsp/math.h>
#include <jclap/dsp/oscillator.h>
#include <jclap/dsp/smoother.h>
#include <jclap/dsp/vca.h>

#include <algorithm>
#include <cmath>

#include "params.h"

namespace basic_synth {

constexpr int kEnvCount = 2;

// What every voice shares, set from the params once per block.
struct VoiceSettings {
   jclap::dsp::Oscillator::Wave wave = jclap::dsp::Oscillator::Wave::Saw;
   double pulseWidth = 0.5;
   // A second oscillator, the same wave, this many semitones from the first.
   bool dual = false;
   double detune = 0.1;
   jclap::dsp::Filter::Mode filterMode = jclap::dsp::Filter::Mode::LowPass;
   jclap::dsp::Filter::Slope slope = jclap::dsp::Filter::Slope::Db24;
   double cutoffHz = 2000.0, resonance = 0.3;
   // How far the cutoff follows the note's pitch, 0..1: at 1 it moves an octave per octave, from C4.
   double keyTrack = 0.0;
   // Octaves this note's velocity opens the cutoff (the Velocity to Filter amount times the velocity).
   double velocityOctaves = 0.0;
   // An envelope: ADSR in seconds and sustain level, where it goes, and whether it loops (then the
   // voice takes the shared cycle's value instead). Filter is in octaves, pitch in semitones, both
   // either way; VCA is 0..1.
   struct Env {
      double attack = 0.005, decay = 0.4, sustain = 0.5, release = 0.3;
      bool loop = false;
      double toFilter = 0.0, toVca = 0.0, toPitch = 0.0;
   };
   Env env[kEnvCount];
   // How far MPE moves the cutoff, in octaves at full scale. Timbre is centred on 0.5.
   double pressureAmount = 2.0, timbreAmount = 2.0;
   // The LFO: octaves of cutoff at full swing, tremolo depth 0..1, and pulse width depth 0..1 (at 1
   // it sweeps the width across its whole range).
   jclap::dsp::Lfo::Shape lfoShape = jclap::dsp::Lfo::Shape::Triangle;
   double lfoRate = 5.0, lfoToCutoff = 0.0, lfoToVca = 0.0, lfoToPulseWidth = 0.0;
};

class SynthVoice {
public:
   void setSampleRate(double sampleRate) {
      osc_.setSampleRate(sampleRate);
      osc2_.setSampleRate(sampleRate);
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
      amountGlide_ = 1.0 - std::exp(-1.0 / (0.005 * sampleRate));
      reset();
   }

   // Each voice's random LFO takes its own sequence.
   void setSeed(uint32_t seed) { lfo_.setSeed(seed); }

   void configure(const VoiceSettings &s) {
      for (jclap::dsp::Oscillator *o : {&osc_, &osc2_})
         o->setWave(s.wave);
      dual_ = s.dual;
      detune_ = s.detune;
      filter_.setMode(s.filterMode);
      filter_.setSlope(s.slope);
      for (int i = 0; i < kEnvCount; ++i) {
         env_[i].setAttack(s.env[i].attack);
         env_[i].setDecay(s.env[i].decay);
         env_[i].setSustain(s.env[i].sustain);
         env_[i].setRelease(s.env[i].release);
         sends_[i] = s.env[i];
         target_[kEnvToFilter + i] = s.env[i].toFilter;
         target_[kEnvToPitch + i] = s.env[i].toPitch;
         target_[kEnvToVca + i] = s.env[i].toVca;
      }
      // The amounts, resonance and pulse width glide to their new values (see process()).
      target_[kResonance] = s.resonance;
      target_[kPulseWidth] = s.pulseWidth;
      cutoff_.setTarget(std::log2(s.cutoffHz) + s.velocityOctaves);
      keyTrack_ = s.keyTrack;
      pressureAmount_ = s.pressureAmount;
      timbreAmount_ = s.timbreAmount;
      lfo_.setShape(s.lfoShape);
      lfo_.setRate(s.lfoRate);
      target_[kLfoToCutoff] = s.lfoToCutoff;
      target_[kLfoToVca] = s.lfoToVca;
      target_[kLfoToPulseWidth] = s.lfoToPulseWidth;
   }

   // Gate on at a pitch in semitones (MIDI note numbers, fractional for bends). A voice that is still
   // sounding carries on from where it is, so stealing it does not click.
   void start(double pitch, double gain) {
      pitch_.setTarget(pitch);
      vca_.setGain(gain);
      if (!isActive()) {
         osc_.reset();
         // Off phase from the first, so the pair doesn't start out cancelling or doubling.
         osc2_.reset(0.37);
         filter_.reset();
         vca_.reset();
         gate_.reset(0.0);
         for (jclap::dsp::Smoother *s : {&pitch_, &pressure_, &timbre_, &cutoff_})
            s->reset(s->target());
         for (int i = 0; i < kAmounts; ++i)
            now_[i] = target_[i];
         applyResonance(true);
         applyPulseWidth(now_[kPulseWidth], true);
         // An envelope that only goes to the filter or pitch can still be releasing from the last
         // note; a fresh one starts from zero all the same.
         for (jclap::dsp::Envelope &e : env_)
            e.reset();
      }
      // The LFO starts its cycle again with every note, retriggered or stolen ones too.
      lfo_.reset();
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

   // `shared` holds each looping envelope's current value (ignored for the others).
   float process(const double *shared) {
      if (!isActive())
         return 0.0f;
      const double gate = gate_.process();
      // Envelope and LFO amounts, resonance and pulse width glide over about 5 ms, so turning or
      // automating them doesn't step.
      for (int i = 0; i < kAmounts; ++i)
         now_[i] += amountGlide_ * (target_[i] - now_[i]);
      applyResonance(false);
      double toFilter = 0.0, toPitch = 0.0, toVca = 0.0, vcaShare = 0.0;
      for (int i = 0; i < kEnvCount; ++i) {
         const double own = env_[i].process();
         const double e = sends_[i].loop ? shared[i] : own;
         toFilter += now_[kEnvToFilter + i] * e;
         toPitch += now_[kEnvToPitch + i] * e;
         // The shared cycle never ends, so on the VCA it is let in by this voice's key.
         toVca += now_[kEnvToVca + i] * e * (sends_[i].loop ? gate : 1.0);
         vcaShare += now_[kEnvToVca + i];
      }
      const double lfo = lfoValue_ = lfo_.process();
      const double pitch = pitch_.process() + toPitch;
      // Pulse width modulation, about the knob's width and kept inside the oscillator's range.
      applyPulseWidth(std::clamp(now_[kPulseWidth] + now_[kLfoToPulseWidth] * kLfoWidthSwing * lfo, 0.05, 0.95), false);
      double source;
      if (dual_) {
         // The first stays on the note and the second is detuned from it, by a few cents for
         // beating up to an octave. Their sum is scaled by 1/sqrt(2): about the same loudness as
         // one, as the two drift in and out of phase.
         osc_.setFrequency(jclap::dsp::noteToHz(pitch));
         osc2_.setFrequency(jclap::dsp::noteToHz(pitch + detune_));
         source = (osc_.process() + osc2_.process()) * 0.7071067811865476;
      } else {
         osc_.setFrequency(jclap::dsp::noteToHz(pitch));
         source = osc_.process();
      }
      const double octaves = cutoff_.process() + toFilter + keyTrack_ * (pitch - 60.0) / 12.0 + pressureAmount_ * pressure_.process() +
                             timbreAmount_ * 2.0 * (timbre_.process() - 0.5) + now_[kLfoToCutoff] * lfo;
      filter_.setCutoff(std::exp2(std::clamp(octaves, 3.0, 15.0)));
      // What the envelopes leave of the VCA follows the gate: with no envelope sent to it, the VCA
      // is simply open while the key is held. Tremolo dips that by up to its depth.
      const double open = gate * std::max(0.0, 1.0 - vcaShare);
      const double tremolo = 1.0 - now_[kLfoToVca] * (0.5 - 0.5 * lfo);
      level_ = static_cast<float>(std::clamp(open + toVca, 0.0, 1.0) * tremolo);
      return vca_.process(filter_.process(static_cast<float>(source)), level_);
   }

   // Sounding until released and silent: the gate has closed and every envelope of its own sent to
   // the VCA has finished.
   bool isActive() const {
      if (!released_ || gate_.value() > 1e-4)
         return true;
      for (int i = 0; i < kEnvCount; ++i)
         if (!sends_[i].loop && sends_[i].toVca > 0.0 && env_[i].isActive())
            return true;
      return false;
   }
   bool envLooping(int i) const { return sends_[i].loop; }
   bool isReleasing() const { return released_; }
   // The VCA's level, for picking the quietest voice to steal.
   float level() const { return level_; }
   // Where envelope `i` is, for the panel.
   jclap::dsp::Envelope::Stage envStage(int i) const { return env_[i].stage(); }
   float envLevel(int i) const { return env_[i].level(); }
   float envReleaseFrom(int i) const { return env_[i].releaseFrom(); }
   // The LFO's latest output, -1..1.
   double lfoValue() const { return lfoValue_; }

private:
   // The filter and oscillators only hear a new resonance or width when it has moved.
   void applyResonance(bool always) {
      if (always || std::fabs(now_[kResonance] - resonanceSet_) > 1e-6) {
         filter_.setResonance(now_[kResonance]);
         resonanceSet_ = now_[kResonance];
      }
   }
   void applyPulseWidth(double width, bool always) {
      if (always || std::fabs(width - widthSet_) > 1e-7) {
         osc_.setPulseWidth(width);
         osc2_.setPulseWidth(width);
         widthSet_ = width;
      }
   }

   enum {
      kEnvToFilter = 0,
      kEnvToPitch = kEnvToFilter + kEnvCount,
      kEnvToVca = kEnvToPitch + kEnvCount,
      kLfoToCutoff = kEnvToVca + kEnvCount,
      kLfoToVca,
      kLfoToPulseWidth,
      kResonance,
      kPulseWidth,
      kAmounts
   };
   double target_[kAmounts] = {}, now_[kAmounts] = {};
   double amountGlide_ = 1.0, resonanceSet_ = -1.0, widthSet_ = -1.0;

   jclap::dsp::Oscillator osc_, osc2_;
   bool dual_ = false;
   double detune_ = 0.0; // semitones
   jclap::dsp::Filter filter_;
   jclap::dsp::Envelope env_[kEnvCount];
   VoiceSettings::Env sends_[kEnvCount];
   jclap::dsp::Lfo lfo_;
   jclap::dsp::Vca vca_;
   // Pitch in semitones; cutoff in octaves (log2 Hz). The gate glides over a few ms so a gate-only
   // VCA opens and closes without clicks.
   jclap::dsp::Smoother pitch_, pressure_, timbre_, cutoff_, gate_;
   double keyTrack_ = 0.0, pressureAmount_ = 0.0, timbreAmount_ = 0.0;
   double lfoValue_ = 0.0;
   bool released_ = true;
   float level_ = 0.0f;
};

} // namespace basic_synth

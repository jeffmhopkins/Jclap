#pragma once

// Basic Synth's presets: a factory bank compiled in, and the user's own saved through jclap::ui's
// preset files.
//
// A factory preset is written as what it changes from the defaults, then filled out with every other
// param at its default, so nothing from the previous preset carries over. The cutoff and LFO rate
// are given in Hz and the detune in semitones, through the same conversions the knobs use.

#include <jclap/ui/presets.h>

#include <vector>

#include "params.h"

namespace basic_synth {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"BasicSynth", 1, ".bspreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      // Waves, filter modes, slopes and LFO shapes by name, for readability.
      constexpr double kSine = 0, kTriangle = 1, kSaw = 2, kSquare = 3, kPulse = 4;
      constexpr double kHighPass = 1, kBandPass = 2;
      constexpr double k12 = 0, k24 = 1, k36 = 2;
      constexpr double kLfoSine = 0, kLfoTriangle = 1, kLfoSquare = 3, kLfoRandom = 4;
      constexpr double kOn = 1;
      const auto hz = cutoffValue;
      const auto rate = lfoValue;
      const auto detune = detuneValue;
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // Bass: Env 2 snaps the filter shut, so each note starts with a bite.
      p.push_back({"Analog Bass", "Bass",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.08)}, {kSlope, k24}, {kCutoff, hz(220)},
                    {kResonance, 0.25}, {kKeyTrack, 0.3}, {kEnv1Attack, 1}, {kEnv1Decay, 300}, {kEnv1Sustain, 0.8},
                    {kEnv1Release, 80}, {kEnv2Attack, 1}, {kEnv2Decay, 220}, {kEnv2Sustain, 0.1},
                    {kEnv2Release, 80}, {kEnv2ToFilter, 3}, {kVelocityToFilter, 1}, {kLevel, -10}}});
      p.push_back({"Acid Bass", "Bass",
                   {{kWave, kSaw}, {kSlope, k24}, {kCutoff, hz(260)}, {kResonance, 0.8}, {kEnv1Attack, 1},
                    {kEnv1Decay, 300}, {kEnv1Sustain, 1}, {kEnv1Release, 40}, {kEnv2Attack, 1}, {kEnv2Decay, 180},
                    {kEnv2Sustain, 0}, {kEnv2Release, 60}, {kEnv2ToFilter, 4}, {kVelocityToFilter, 1.5}}});
      p.push_back({"Sub Bass", "Bass",
                   {{kWave, kSine}, {kCutoff, hz(2000)}, {kResonance, 0}, {kEnv1Attack, 3}, {kEnv1Decay, 200},
                    {kEnv1Sustain, 1}, {kEnv1Release, 120}, {kEnv2ToFilter, 0}, {kPressureAmount, 0},
                    {kTimbreAmount, 0}, {kLevel, -8}}});
      p.push_back({"Pulse Bass", "Bass",
                   {{kWave, kPulse}, {kPulseWidth, 0.25}, {kSlope, k24}, {kCutoff, hz(380)}, {kResonance, 0.3},
                    {kEnv1Attack, 1}, {kEnv1Decay, 400}, {kEnv1Sustain, 0.7}, {kEnv1Release, 60}, {kEnv2Attack, 1},
                    {kEnv2Decay, 250}, {kEnv2Sustain, 0.15}, {kEnv2ToFilter, 2.5}, {kLevel, -10}}});
      p.push_back({"Wobble Bass", "Bass",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(-12)}, {kSlope, k24}, {kCutoff, hz(200)},
                    {kResonance, 0.5}, {kEnv1Attack, 2}, {kEnv1Sustain, 1}, {kEnv1Release, 100}, {kEnv2ToFilter, 0},
                    {kLfoShape, kLfoSine}, {kLfoRate, rate(3)}, {kLfoToCutoff, 3}, {kLevel, -10}}});

      // Leads: held notes that sing, with pressure and slide on the cutoff.
      p.push_back({"Saw Lead", "Leads",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.12)}, {kSlope, k12}, {kCutoff, hz(2400)},
                    {kResonance, 0.2}, {kKeyTrack, 0.5}, {kEnv1Attack, 5}, {kEnv1Decay, 400}, {kEnv1Sustain, 0.9},
                    {kEnv1Release, 250}, {kEnv2Attack, 10}, {kEnv2Decay, 400}, {kEnv2Sustain, 0.4},
                    {kEnv2ToFilter, 1.5}}});
      p.push_back({"Square Lead", "Leads",
                   {{kWave, kSquare}, {kSlope, k24}, {kCutoff, hz(1800)}, {kResonance, 0.35}, {kKeyTrack, 0.5},
                    {kEnv1Attack, 5}, {kEnv1Sustain, 0.85}, {kEnv1Release, 200}, {kEnv2Decay, 600},
                    {kEnv2Sustain, 0.5}, {kEnv2ToFilter, 2}}});
      p.push_back({"Blip Lead", "Leads",
                   {{kWave, kSaw}, {kSlope, k24}, {kCutoff, hz(1500)}, {kResonance, 0.3}, {kKeyTrack, 0.5},
                    {kEnv1Attack, 2}, {kEnv1Sustain, 0.9}, {kEnv1Release, 200}, {kEnv2Attack, 1}, {kEnv2Decay, 60},
                    {kEnv2Sustain, 0}, {kEnv2ToFilter, 2}, {kEnv2ToPitch, 12}}});
      p.push_back({"Pressure Lead", "Leads",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.1)}, {kSlope, k24}, {kCutoff, hz(300)},
                    {kResonance, 0.4}, {kKeyTrack, 0.5}, {kEnv1Attack, 20}, {kEnv1Sustain, 1}, {kEnv1Release, 400},
                    {kEnv2ToFilter, 0}, {kPressureAmount, 4}, {kTimbreAmount, 3}, {kVelocityToVca, 0.5}}});

      // Keys: velocity opens the filter, as a harder strike brightens a struck string.
      p.push_back({"Soft Keys", "Keys",
                   {{kWave, kTriangle}, {kSlope, k12}, {kCutoff, hz(1500)}, {kResonance, 0.1}, {kKeyTrack, 0.5},
                    {kEnv1Attack, 3}, {kEnv1Decay, 1200}, {kEnv1Sustain, 0.3}, {kEnv1Release, 400},
                    {kEnv2Attack, 1}, {kEnv2Decay, 800}, {kEnv2Sustain, 0.2}, {kEnv2ToFilter, 2},
                    {kVelocityToFilter, 1.5}}});
      p.push_back({"Organ", "Keys",
                   {{kWave, kSquare}, {kDual, kOn}, {kDetune, detune(12)}, {kCutoff, hz(3000)}, {kResonance, 0.1},
                    {kKeyTrack, 0.5}, {kEnv1ToVca, 0}, {kEnv2ToFilter, 0}, {kVelocityToVca, 0}, {kLfoShape, kLfoSine},
                    {kLfoRate, rate(6)}, {kLfoToVca, 0.08}}});
      p.push_back({"Pulse Clav", "Keys",
                   {{kWave, kPulse}, {kPulseWidth, 0.15}, {kSlope, k24}, {kCutoff, hz(1200)}, {kResonance, 0.4},
                    {kKeyTrack, 0.5}, {kEnv1Attack, 1}, {kEnv1Decay, 350}, {kEnv1Sustain, 0}, {kEnv1Release, 80},
                    {kEnv2Attack, 1}, {kEnv2Decay, 150}, {kEnv2Sustain, 0}, {kEnv2ToFilter, 3},
                    {kVelocityToFilter, 2}}});

      // Plucks: no sustain, so every note dies away by itself.
      p.push_back({"Bright Pluck", "Plucks",
                   {{kWave, kSaw}, {kSlope, k24}, {kCutoff, hz(300)}, {kResonance, 0.3}, {kKeyTrack, 0.5},
                    {kEnv1Attack, 1}, {kEnv1Decay, 500}, {kEnv1Sustain, 0}, {kEnv1Release, 300}, {kEnv2Attack, 1},
                    {kEnv2Decay, 200}, {kEnv2Sustain, 0}, {kEnv2Release, 200}, {kEnv2ToFilter, 4.5},
                    {kVelocityToFilter, 1}}});
      p.push_back({"Glass Pluck", "Plucks",
                   {{kWave, kSine}, {kDual, kOn}, {kDetune, detune(12)}, {kCutoff, hz(8000)}, {kResonance, 0},
                    {kEnv1Attack, 1}, {kEnv1Decay, 700}, {kEnv1Sustain, 0}, {kEnv1Release, 600}, {kEnv2ToFilter, 0},
                    {kEnv2ToPitch, 0}}});
      p.push_back({"Pizzicato", "Plucks",
                   {{kWave, kTriangle}, {kSlope, k12}, {kCutoff, hz(600)}, {kResonance, 0.15}, {kKeyTrack, 0.6},
                    {kEnv1Attack, 1}, {kEnv1Decay, 250}, {kEnv1Sustain, 0}, {kEnv1Release, 200}, {kEnv2Attack, 1},
                    {kEnv2Decay, 120}, {kEnv2Sustain, 0}, {kEnv2ToFilter, 3}}});

      // Pads: slow on both envelopes, with the LFO keeping them moving.
      p.push_back({"Warm Pad", "Pads",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.1)}, {kSlope, k12}, {kCutoff, hz(900)},
                    {kResonance, 0.15}, {kEnv1Attack, 800}, {kEnv1Decay, 1500}, {kEnv1Sustain, 0.8},
                    {kEnv1Release, 2500}, {kEnv2Attack, 1200}, {kEnv2Decay, 2000}, {kEnv2Sustain, 0.6},
                    {kEnv2Release, 2500}, {kEnv2ToFilter, 1.5}, {kLfoShape, kLfoTriangle}, {kLfoRate, rate(0.3)},
                    {kLfoToCutoff, 0.5}, {kVelocityToVca, 0.4}}});
      p.push_back({"PWM Pad", "Pads",
                   {{kWave, kPulse}, {kPulseWidth, 0.5}, {kSlope, k24}, {kCutoff, hz(1400)}, {kResonance, 0.2},
                    {kEnv1Attack, 600}, {kEnv1Decay, 1000}, {kEnv1Sustain, 0.85}, {kEnv1Release, 2000},
                    {kEnv2ToFilter, 0}, {kLfoShape, kLfoTriangle}, {kLfoRate, rate(0.6)}, {kLfoToPulseWidth, 0.7},
                    {kVelocityToVca, 0.4}}});
      p.push_back({"Glass Pad", "Pads",
                   {{kWave, kTriangle}, {kDual, kOn}, {kDetune, detune(12)}, {kSlope, k12}, {kCutoff, hz(3000)},
                    {kResonance, 0.1}, {kEnv1Attack, 1200}, {kEnv1Sustain, 1}, {kEnv1Release, 3000},
                    {kEnv2ToFilter, 0}, {kLfoShape, kLfoSine}, {kLfoRate, rate(4)}, {kLfoToVca, 0.15},
                    {kVelocityToVca, 0.3}}});
      p.push_back({"Breathing Pad", "Pads",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.15)}, {kSlope, k24}, {kCutoff, hz(400)},
                    {kResonance, 0.35}, {kEnv1Attack, 700}, {kEnv1Sustain, 1}, {kEnv1Release, 2500},
                    {kEnv2Attack, 1500}, {kEnv2Decay, 1500}, {kEnv2Sustain, 0}, {kEnv2Loop, kOn},
                    {kEnv2ToFilter, 2.5}, {kVelocityToVca, 0.3}}});

      // Motion: the LFO and looping envelopes play rhythms of their own while a key is held.
      p.push_back({"Trance Gate", "Motion",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.12)}, {kCutoff, hz(1500)}, {kResonance, 0.25},
                    {kEnv1Attack, 2}, {kEnv1Sustain, 1}, {kEnv1Release, 200}, {kEnv2ToFilter, 0},
                    {kLfoShape, kLfoSquare}, {kLfoRate, rate(8)}, {kLfoToVca, 1}}});
      p.push_back({"Looping Pulse", "Motion",
                   {{kWave, kSaw}, {kSlope, k24}, {kCutoff, hz(300)}, {kResonance, 0.5}, {kEnv1Attack, 1},
                    {kEnv1Decay, 150}, {kEnv1Sustain, 0}, {kEnv1Release, 150}, {kEnv1Loop, kOn}, {kEnv2Attack, 1},
                    {kEnv2Decay, 300}, {kEnv2Sustain, 0}, {kEnv2Loop, kOn}, {kEnv2ToFilter, 3}}});
      p.push_back({"Random Steps", "Motion",
                   {{kWave, kSaw}, {kSlope, k24}, {kCutoff, hz(800)}, {kResonance, 0.6}, {kEnv1Attack, 2},
                    {kEnv1Sustain, 1}, {kEnv1Release, 300}, {kEnv2ToFilter, 0}, {kLfoShape, kLfoRandom},
                    {kLfoRate, rate(6)}, {kLfoToCutoff, 2.5}}});
      p.push_back({"Band Sweep", "Motion",
                   {{kWave, kSaw}, {kDual, kOn}, {kDetune, detune(0.2)}, {kFilterMode, kBandPass}, {kSlope, k12},
                    {kCutoff, hz(900)}, {kResonance, 0.6}, {kEnv1Attack, 300}, {kEnv1Sustain, 1},
                    {kEnv1Release, 1500}, {kEnv2ToFilter, 0}, {kLfoShape, kLfoTriangle}, {kLfoRate, rate(0.15)},
                    {kLfoToCutoff, 2}, {kLevel, -8}}});

      // FX: the envelopes on the pitch.
      p.push_back({"Laser Zap", "FX",
                   {{kWave, kSquare}, {kCutoff, hz(4000)}, {kResonance, 0.2}, {kEnv1Attack, 1}, {kEnv1Decay, 400},
                    {kEnv1Sustain, 0}, {kEnv1Release, 200}, {kEnv2Attack, 1}, {kEnv2Decay, 300}, {kEnv2Sustain, 0},
                    {kEnv2ToFilter, 0}, {kEnv2ToPitch, 24}}});
      p.push_back({"Siren", "FX",
                   {{kWave, kSquare}, {kCutoff, hz(2500)}, {kResonance, 0.2}, {kEnv1Attack, 600}, {kEnv1Decay, 600},
                    {kEnv1Sustain, 0}, {kEnv1Loop, kOn}, {kEnv1ToVca, 0}, {kEnv1ToPitch, 12}, {kEnv2ToFilter, 0},
                    {kVelocityToVca, 0}, {kLevel, -16}}});
      p.push_back({"Thin Air", "FX",
                   {{kWave, kPulse}, {kPulseWidth, 0.1}, {kFilterMode, kHighPass}, {kSlope, k36},
                    {kCutoff, hz(2000)}, {kResonance, 0.5}, {kEnv1Attack, 900}, {kEnv1Sustain, 1},
                    {kEnv1Release, 2000}, {kEnv2ToFilter, 0}, {kLfoShape, kLfoRandom}, {kLfoRate, rate(3)},
                    {kLfoToCutoff, 1.5}, {kLfoToPulseWidth, 0.8}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace basic_synth

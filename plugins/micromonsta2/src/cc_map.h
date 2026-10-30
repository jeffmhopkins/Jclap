#pragma once

// MicroMonsta 2 MIDI CC map, from "MicroMonsta 2 User manual v2.8" section 7.1 (see
// docs/cc-map.csv). Every CC is a plain 0-127 value; the manual gives no units.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mm2 {

enum class Section { Oscillators, Mix, Filter, Envelopes, Lfo, ModMatrix, Effects, Voice, Performance };

struct CcParam {
   int cc;
   const char *name;  // full name shown by the host
   const char *label; // short knob label for the panel
   Section section;
   bool bipolar; // centered at 64 (tune, mod depth...)
};

// The CC number doubles as the CLAP parameter id, so ids stay stable if this list is reordered.
inline constexpr CcParam kCcParams[] = {
   {39, "Osc1 Shape", "SHAPE", Section::Oscillators, false},
   {40, "Osc1 Tune", "TUNE", Section::Oscillators, true},
   {41, "Osc1 Fine Tune", "FINE TUNE", Section::Oscillators, true},
   {42, "Osc2 Shape", "SHAPE", Section::Oscillators, false},
   {43, "Osc2 Tune", "TUNE", Section::Oscillators, true},
   {44, "Osc2 Fine Tune", "FINE TUNE", Section::Oscillators, true},
   {45, "Osc3 Shape", "SHAPE", Section::Oscillators, false},
   {46, "Osc3 Tune", "TUNE", Section::Oscillators, true},
   {47, "Osc3 Fine Tune", "FINE TUNE", Section::Oscillators, true},
   {48, "Osc1 Level", "OSC1 LEVEL", Section::Mix, false},
   {49, "Osc2 Level", "OSC2 LEVEL", Section::Mix, false},
   {50, "Osc3 Level", "OSC3 LEVEL", Section::Mix, false},
   {51, "Noise Level", "NOISE", Section::Mix, false},
   {74, "Filter Cutoff", "CUTOFF", Section::Filter, false},
   {71, "Filter Resonance", "RESONANCE", Section::Filter, false},
   {88, "Filter Env Amount", "ENV AMOUNT", Section::Filter, true},
   {89, "Filter Tracking", "TRACKING", Section::Filter, false},
   {90, "Filter FM", "FM", Section::Filter, false},
   {73, "Env1 Attack", "A", Section::Envelopes, false},
   {75, "Env1 Decay", "D", Section::Envelopes, false},
   {79, "Env1 Sustain", "S", Section::Envelopes, false},
   {72, "Env1 Release", "R", Section::Envelopes, false},
   {80, "Env2 Attack", "A", Section::Envelopes, false},
   {81, "Env2 Decay", "D", Section::Envelopes, false},
   {82, "Env2 Sustain", "S", Section::Envelopes, false},
   {83, "Env2 Release", "R", Section::Envelopes, false},
   {84, "Env3 Attack", "A", Section::Envelopes, false},
   {85, "Env3 Decay", "D", Section::Envelopes, false},
   {86, "Env3 Sustain", "S", Section::Envelopes, false},
   {87, "Env3 Release", "R", Section::Envelopes, false},
   {36, "VCA Env Velocity", "VCA VELO", Section::Envelopes, false},
   {37, "Filter Env Velocity", "FLT VELO", Section::Envelopes, false},
   {76, "LFO1 Speed", "LFO1 SPEED", Section::Lfo, false},
   {77, "LFO2 Speed", "LFO2 SPEED", Section::Lfo, false},
   {78, "LFO3 Speed", "LFO3 SPEED", Section::Lfo, false},
   {52, "Mod Matrix 1", "1", Section::ModMatrix, true},
   {53, "Mod Matrix 2", "2", Section::ModMatrix, true},
   {54, "Mod Matrix 3", "3", Section::ModMatrix, true},
   {55, "Mod Matrix 4", "4", Section::ModMatrix, true},
   {56, "Mod Matrix 5", "5", Section::ModMatrix, true},
   {57, "Mod Matrix 6", "6", Section::ModMatrix, true},
   {58, "Mod Matrix 7", "7", Section::ModMatrix, true},
   {59, "Mod Matrix 8", "8", Section::ModMatrix, true},
   {60, "Mod Matrix 9", "9", Section::ModMatrix, true},
   {61, "Mod Matrix 10", "10", Section::ModMatrix, true},
   {62, "Lag 1", "LAG 1", Section::ModMatrix, false},
   {26, "Delay Time", "TIME", Section::Effects, false},
   {27, "Delay Feedback", "FEEDBACK", Section::Effects, false},
   {28, "Delay Level", "LEVEL", Section::Effects, false},
   {29, "Reverb Decay", "DECAY", Section::Effects, false},
   {30, "Reverb Mod", "MOD", Section::Effects, false},
   {31, "Reverb Level", "LEVEL", Section::Effects, false},
   {25, "EQ", "LOW / HIGH", Section::Effects, true},
   {5, "Glide", "GLIDE", Section::Voice, false},
   {33, "Voice Detune", "DETUNE", Section::Voice, false},
   {34, "Osc Detune", "OSC DETUNE", Section::Voice, false},
   {91, "Drive", "DRIVE", Section::Voice, false},
   {35, "Pan Spread", "PAN SPREAD", Section::Voice, false},
   {1, "Mod Wheel", "MOD WHEEL", Section::Performance, false},
   {70, "Mod Knob", "MOD KNOB", Section::Performance, false},
   {7, "Volume", "VOLUME", Section::Performance, false},
};

inline constexpr int kCcParamCount = sizeof(kCcParams) / sizeof(kCcParams[0]);

// Extra, non-CC parameter: the MIDI channel the CCs go out on (1-16).
inline constexpr uint32_t kChannelParamId = 1000;
inline constexpr int kParamCount = kCcParamCount + 1;

// An init-patch style default: what SEND ALL and "reset to default" send for a knob nobody set, so
// they never mute the synth. Centre for bipolar knobs, open for the main level, cutoff and
// sustains, the usual 100 for Volume, mid speed for the LFOs, and 0 for the rest.
inline double defaultValue(const CcParam &p) {
   if (p.bipolar)
      return 64.0;
   switch (p.cc) {
   case 7: return 100.0;                     // Volume
   case 48: return 127.0;                    // Osc1 Level
   case 74: return 127.0;                    // Filter Cutoff
   case 79: case 82: case 86: return 127.0;  // Env sustains
   case 76: case 77: case 78: return 64.0;   // LFO speeds
   default: return 0.0;
   }
}

// Index into kCcParams for a CC number, or -1.
inline int indexOfCc(int cc) {
   for (int i = 0; i < kCcParamCount; ++i)
      if (kCcParams[i].cc == cc)
         return i;
   return -1;
}

// Host modulation (e.g. a Bitwig modulator) is an offset on top of the knob's setting.
inline double modulated(double base, double mod) { return std::clamp(base + mod, 0.0, 127.0); }

// NaN (which lround leaves unspecified) maps to 0.
inline int toCcValue(double value) {
   if (!(value > 0.0))
      return 0;
   return static_cast<int>(std::lround(std::min(value, 127.0)));
}

// Remembers what was last sent for each CC so only real changes go out. A parameter is only sent
// once it has been "touched" (set by the host, automation or the panel), so inserting the plugin
// never overwrites the synth's current sound with our defaults.
class CcSender {
public:
   CcSender() { reset(); }

   void reset() {
      for (int i = 0; i < kCcParamCount; ++i)
         lastSent_[i] = lastKnob_[i] = -1;
   }

   // Forget what was sent so every touched CC goes out again (e.g. after a channel change).
   void resendAll() { reset(); }

   // Returns the CC value to send for parameter index i, or -1 if nothing needs to go out. It only
   // counts as sent once sent() says so, so a CC the host's output queue refused goes out next time.
   // A knob that hasn't moved since it was last sent stays quiet even when the synth has since
   // heard another value for that CC (see heard()), so a passed-through controller isn't overridden.
   int pending(int i, double value, bool touched) {
      if (!touched)
         return -1;
      const int v = toCcValue(value);
      if (v == lastKnob_[i])
         return -1;
      if (v == lastSent_[i]) {
         lastKnob_[i] = v;
         return -1;
      }
      return v;
   }
   void sent(int i, int value) { lastSent_[i] = lastKnob_[i] = value; }
   // Sends parameter i again even if it hasn't moved (the synth's value was changed another way).
   void forget(int i) { lastSent_[i] = lastKnob_[i] = -1; }
   // The synth got `value` for this CC from elsewhere (a controller's CC passed through).
   void heard(int i, int value) { lastSent_[i] = value; }

private:
   int lastSent_[kCcParamCount]; // what the synth has, as far as we know
   int lastKnob_[kCcParamCount]; // the knob's CC value when it was last sent or found already there
};

} // namespace mm2

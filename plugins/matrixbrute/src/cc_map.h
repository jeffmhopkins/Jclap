#pragma once

// Arturia MatrixBrute MIDI CC map, from the MatrixBrute manual v2.0.3 section 7.1 (see
// docs/cc-map.csv). The controls the manual marks 7/14-bit can also send a fine byte; the manual
// doesn't say on which CC, so this assumes the usual one, 32 above (Steiner cutoff 23 + 55).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mb {

enum class Section { Vco1, Vco2, Vco3, AudioMod, Mixer, Steiner, Ladder, Filters, Env1, Env2, Env3, Lfo, Fx, Sequencer, Performance };

struct CcParam {
   int cc;
   const char *name;  // full name shown by the host
   const char *label; // short knob label for the panel
   Section section;
   bool bipolar; // centered at 64 (tune, env amount, the audio mod crossfades...)
   bool fine;    // can carry 14 bits: a fine byte on cc + 32
};

// The CC number doubles as the CLAP parameter id, so ids stay stable if this list is reordered.
inline constexpr CcParam kCcParams[] = {
   {9, "VCO1 Sub", "SUB", Section::Vco1, false, true},
   {10, "VCO1 Saw", "SAW", Section::Vco1, false, true},
   {12, "VCO1 Square", "SQUARE", Section::Vco1, false, true},
   {13, "VCO1 Triangle", "TRI", Section::Vco1, false, true},
   {66, "VCO1 Coarse", "COARSE", Section::Vco1, true, false},
   {65, "VCO1 Fine", "FINE", Section::Vco1, true, false},
   {68, "VCO1 Ultrasaw", "ULTRASAW", Section::Vco1, false, false},
   {69, "VCO1 Pulse Width", "PW", Section::Vco1, false, false},
   {70, "VCO1 Metalizer", "METAL", Section::Vco1, false, false},
   {67, "VCO1 Waveform Selection", "WAVE SEL", Section::Vco1, false, false},
   {14, "VCO2 Sub", "SUB", Section::Vco2, false, true},
   {15, "VCO2 Saw", "SAW", Section::Vco2, false, true},
   {16, "VCO2 Square", "SQUARE", Section::Vco2, false, true},
   {17, "VCO2 Triangle", "TRI", Section::Vco2, false, true},
   {72, "VCO2 Coarse", "COARSE", Section::Vco2, true, false},
   {71, "VCO2 Fine", "FINE", Section::Vco2, true, false},
   {74, "VCO2 Ultrasaw", "ULTRASAW", Section::Vco2, false, false},
   {75, "VCO2 Pulse Width", "PW", Section::Vco2, false, false},
   {76, "VCO2 Metalizer", "METAL", Section::Vco2, false, false},
   {73, "VCO2 Waveform Selection", "WAVE SEL", Section::Vco2, false, false},
   {22, "VCO3/LFO3 Coarse", "COARSE", Section::Vco3, true, true},
   {77, "Audio Mod VCO1 > VCO2", "VCO1 > VCO2", Section::AudioMod, false, false},
   {78, "Audio Mod VCO1 < VCO3 > VCO2", "VCO3 > 1 | 2", Section::AudioMod, true, false},
   {79, "Audio Mod VCF1 < VCO3 > VCF2", "VCO3 > F1 | F2", Section::AudioMod, true, false},
   {80, "Audio Mod VCO1 < Noise > VCF1", "NOISE > 1 | F1", Section::AudioMod, true, false},
   {18, "Mixer VCO1", "VCO 1", Section::Mixer, false, true},
   {19, "Mixer VCO2", "VCO 2", Section::Mixer, false, true},
   {20, "Mixer VCO3", "VCO 3", Section::Mixer, false, true},
   {21, "Mixer Noise", "NOISE", Section::Mixer, false, true},
   {89, "Mixer External", "EXT IN", Section::Mixer, false, false},
   {23, "Steiner Cutoff", "CUTOFF", Section::Steiner, false, true},
   {83, "Steiner Resonance", "RESONANCE", Section::Steiner, false, false},
   {82, "Steiner Brute Factor", "BRUTE", Section::Steiner, false, false},
   {81, "Steiner Drive", "DRIVE", Section::Steiner, false, false},
   {24, "Steiner Env1 Amount", "ENV1 AMT", Section::Steiner, true, true},
   {7, "Steiner Out", "OUT", Section::Steiner, false, true},
   {25, "Ladder Cutoff", "CUTOFF", Section::Ladder, false, true},
   {87, "Ladder Resonance", "RESONANCE", Section::Ladder, false, false},
   {86, "Ladder Brute Factor", "BRUTE", Section::Ladder, false, false},
   {85, "Ladder Drive", "DRIVE", Section::Ladder, false, false},
   {26, "Ladder Env1 Amount", "ENV1 AMT", Section::Ladder, true, true},
   {8, "Ladder Out", "OUT", Section::Ladder, false, true},
   {27, "Master Cutoff", "MASTER CUTOFF", Section::Filters, true, true},
   {102, "ENV1 Attack", "A", Section::Env1, false, false},
   {103, "ENV1 Decay", "D", Section::Env1, false, false},
   {28, "ENV1 Sustain", "S", Section::Env1, false, true},
   {104, "ENV1 Release", "R", Section::Env1, false, false},
   {94, "ENV1 Velocity to VCF", "VELO", Section::Env1, false, false},
   {105, "ENV2 Attack", "A", Section::Env2, false, false},
   {106, "ENV2 Decay", "D", Section::Env2, false, false},
   {29, "ENV2 Sustain", "S", Section::Env2, false, true},
   {107, "ENV2 Release", "R", Section::Env2, false, false},
   {95, "ENV2 Velocity to VCA", "VELO", Section::Env2, false, false},
   {108, "ENV3 Delay", "DELAY", Section::Env3, false, false},
   {109, "ENV3 Attack", "A", Section::Env3, false, false},
   {110, "ENV3 Decay", "D", Section::Env3, false, false},
   {30, "ENV3 Sustain", "S", Section::Env3, false, true},
   {111, "ENV3 Release", "R", Section::Env3, false, false},
   {91, "LFO1 Rate", "RATE", Section::Lfo, false, false},
   {90, "LFO1 Phase", "PHASE", Section::Lfo, false, false},
   {93, "LFO2 Rate", "RATE", Section::Lfo, false, false},
   {92, "LFO2 Delay", "DELAY", Section::Lfo, false, false},
   {31, "Analog FX Dry/Wet", "DRY/WET", Section::Fx, false, true},
   {112, "Analog FX Delay Time", "TIME", Section::Fx, false, false},
   {113, "Analog FX Regeneration", "REGEN", Section::Fx, false, false},
   {114, "Analog FX Tone/Rate", "TONE/RATE", Section::Fx, false, false},
   {115, "Analog FX Width/Depth", "WIDTH/DEPTH", Section::Fx, false, false},
   {116, "Sequencer Rate", "RATE", Section::Sequencer, false, false},
   {117, "Sequencer Swing", "SWING", Section::Sequencer, false, false},
   {118, "Sequencer Gate", "GATE", Section::Sequencer, false, false},
   {1, "Mod Wheel", "MOD WHEEL", Section::Performance, false, true},
   {5, "Glide", "GLIDE", Section::Performance, false, true},
   {119, "Bend Range", "BEND RANGE", Section::Performance, false, false},
   {11, "Macro M1", "M1", Section::Performance, false, true},
   {2, "Macro M2", "M2", Section::Performance, false, true},
   {4, "Macro M3", "M3", Section::Performance, false, true},
   {3, "Macro M4", "M4", Section::Performance, false, true},
};

inline constexpr int kCcParamCount = sizeof(kCcParams) / sizeof(kCcParams[0]);

// Extra, non-CC parameters: the MIDI channel (1-16), whether fine bytes go out (the synth's own
// "Enable 14-bit automation" setting should match), and whether CCs coming in (from the synth's
// MIDI out, or another controller) move the knobs.
inline constexpr uint32_t kChannelParamId = 1000;
inline constexpr uint32_t kFineParamId = 1001;
inline constexpr uint32_t kFollowParamId = 1002;
inline constexpr int kParamCount = kCcParamCount + 3;

// The fine byte rides 32 above its CC.
inline constexpr int kFineOffset = 32;

// An init-patch style default: what SEND ALL and "reset to default" send for a knob nobody set, so
// they never mute the synth. Centre for bipolar knobs; VCO1 saw, the VCO1 mixer level, both
// cutoffs, the filter outs and the sustains open; 0 for the rest.
inline double defaultValue(const CcParam &p) {
   if (p.bipolar)
      return 64.0;
   switch (p.cc) {
   case 10: case 18: return 127.0;           // VCO1 saw into the mixer
   case 23: case 25: return 127.0;           // cutoffs
   case 7: case 8: return 100.0;             // filter outs
   case 28: case 29: case 30: return 127.0;  // sustains
   case 69: return 64.0;                     // square pulse width
   case 91: case 93: case 116: return 64.0;  // LFO and sequencer rates
   case 118: return 64.0;                    // sequencer gate
   case 119: return 2.0;                     // bend range
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

// Index of the param whose fine byte rides on `cc`, or -1.
inline int indexOfFineCc(int cc) {
   const int i = cc >= kFineOffset ? indexOfCc(cc - kFineOffset) : -1;
   return i >= 0 && kCcParams[i].fine ? i : -1;
}

// Host modulation (e.g. a Bitwig modulator) is an offset on top of the knob's setting.
inline double modulated(double base, double mod) { return std::clamp(base + mod, 0.0, 127.0); }

// NaN (which lround leaves unspecified) maps to 0.
inline int toCcValue(double value) {
   if (!(value > 0.0))
      return 0;
   return static_cast<int>(std::lround(std::min(value, 127.0)));
}

// A knob's value (0-127, fractional) as 14 bits: coarse byte << 7 | fine byte. The coarse byte is
// the whole part and the fine byte the fraction in 128ths, so a value that arrived without a fine
// byte goes back out as the same coarse byte.
inline int toFineValue(double value) {
   if (!(value > 0.0))
      return 0;
   return std::min(16383, static_cast<int>(std::lround(std::min(value, 127.0) * 128.0)));
}
inline double fromWire(int wire) { return std::clamp(wire / 128.0, 0.0, 127.0); }

// What goes out for a knob: 14 bits when fine bytes are on and the CC has one, otherwise the
// coarse byte alone (shifted, so both kinds compare alike).
inline int toWire(double value, bool fine) { return fine ? toFineValue(value) : toCcValue(value) << 7; }

// Remembers what was last sent for each CC so only real changes go out. A parameter is only sent
// once it has been "touched" (set by the host, automation, the panel or the synth), so inserting
// the plugin never overwrites the synth's current sound with our defaults. Values are toWire()'s.
class CcSender {
public:
   CcSender() { reset(); }

   void reset() {
      for (int i = 0; i < kCcParamCount; ++i)
         lastSent_[i] = lastKnob_[i] = -1;
   }

   // Forget what was sent so every touched CC goes out again (e.g. after a channel change).
   void resendAll() { reset(); }

   // Returns the wire value to send for parameter index i, or -1 if nothing needs to go out. It only
   // counts as sent once sent() says so, so a CC the host's output queue refused goes out next time.
   // A knob that hasn't moved since it was last sent stays quiet even when the synth has since
   // heard another value for that CC (see heard()), so the synth's own knob isn't overridden.
   int pending(int i, int wire, bool touched) {
      if (!touched)
         return -1;
      if (wire == lastKnob_[i])
         return -1;
      if (wire == lastSent_[i]) {
         lastKnob_[i] = wire;
         return -1;
      }
      return wire;
   }
   void sent(int i, int wire) { lastSent_[i] = lastKnob_[i] = wire; }
   // The synth got `wire` for this CC from elsewhere (its own panel, or a controller passed through).
   void heard(int i, int wire) { lastSent_[i] = wire; }

private:
   int lastSent_[kCcParamCount]; // what the synth has, as far as we know
   int lastKnob_[kCcParamCount]; // the knob's value when it was last sent or found already there
};

// Puts a knob's incoming coarse and fine bytes back together. A coarse byte alone is the value
// with a fine byte of 0, as MIDI has it; a fine byte refines the coarse byte before it.
class CcReceiver {
public:
   CcReceiver() { reset(); }
   void reset() {
      for (int &c : coarse_)
         c = 0;
   }
   // Wire value for a coarse byte on param i.
   int coarse(int i, int value) {
      coarse_[i] = value & 0x7F;
      return coarse_[i] << 7;
   }
   // Wire value for a fine byte on param i.
   int fine(int i, int value) { return coarse_[i] << 7 | (value & 0x7F); }

private:
   int coarse_[kCcParamCount];
};

} // namespace mb

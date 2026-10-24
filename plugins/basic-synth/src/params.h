#pragma once

// Basic Synth's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace basic_synth {

// The ids are the host's names for the params, in automation and saved projects, so once released
// they stay put: a new param goes at the end, before kParamCount.
enum ParamId : clap_id {
   // Oscillator.
   kWave,
   kPulseWidth,
   kTune,
   kDual,
   kDetune,
   // Filter.
   kFilterMode,
   kSlope,
   kCutoff,
   kResonance,
   kKeyTrack,
   // Two envelopes, each the same eight params in the same order (see envParam()).
   kEnv1Attack,
   kEnv1Decay,
   kEnv1Sustain,
   kEnv1Release,
   kEnv1Loop,
   kEnv1ToFilter,
   kEnv1ToVca,
   kEnv1ToPitch,
   kEnv2Attack,
   kEnv2Decay,
   kEnv2Sustain,
   kEnv2Release,
   kEnv2Loop,
   kEnv2ToFilter,
   kEnv2ToVca,
   kEnv2ToPitch,
   // The LFO, restarted with each note.
   kLfoShape,
   kLfoRate,
   kLfoToCutoff,
   kLfoToVca,
   // Output, and what MPE expression does.
   kLevel,
   kBendRange,
   kPressureAmount,
   kTimbreAmount,
   // Added after 0.4.0: what velocity does, and the LFO on the pulse width.
   kVelocityToVca,
   kVelocityToFilter,
   kLfoToPulseWidth,
   kParamCount
};

// Every param can be automated and modulated from the host. Modulation can also be per voice (by note
// id, key or channel), which is what Bitwig's polyphonic modulators send. MPE Bend Range and the
// envelope loop switches are the params with no per-voice meaning.
constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = kStepped | CLAP_PARAM_IS_ENUM;
// A looping envelope is one shared cycle, so its switch has no per-voice meaning.
constexpr auto kGlobalChoice = kGlobalMod | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

// Envelope `env` (0 or 1)'s param, given Env 1's: envParam(1, kEnv1Decay) is kEnv2Decay.
constexpr int kEnvParamCount = kEnv2Attack - kEnv1Attack;
constexpr clap_id envParam(int env, ParamId env1Param) { return env1Param + env * kEnvParamCount; }

// The cutoff param runs 0..1 so a knob sweeps it evenly by ear: 20 Hz times 1000 to that power.
constexpr double kCutoffLow = 20.0;
constexpr double kCutoffSpan = 1000.0;
inline double cutoffHz(double value) { return kCutoffLow * std::pow(kCutoffSpan, value); }
inline double cutoffValue(double hz) { return std::log(hz / kCutoffLow) / std::log(kCutoffSpan); }
// The same for the LFO rate: 0.05 Hz times 400 to that power, so 0.05..20 Hz.
constexpr double kLfoLow = 0.05;
constexpr double kLfoSpan = 400.0;
inline double lfoHz(double value) { return kLfoLow * std::pow(kLfoSpan, value); }
inline double lfoValue(double hz) { return std::log(hz / kLfoLow) / std::log(kLfoSpan); }
// How far LFO to Pulse Width moves the width at full depth and full swing, either way.
constexpr double kLfoWidthSwing = 0.45;
// Detune runs -1..1, cubed and scaled to an octave: fine around the centre for beating (0.2 is
// about 10 cents), and a whole octave either way at the ends.
inline double detuneSemitones(double value) { return 12.0 * value * value * value; }
inline double detuneValue(double semitones) { return std::clamp(std::cbrt(semitones / 12.0), -1.0, 1.0); }

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kWave, "Wave", "Oscillator", 0.0, 4.0, 2.0, kChoice},
   {kPulseWidth, "Pulse Width", "Oscillator", 0.05, 0.95, 0.5, kMod},
   {kTune, "Tune", "Oscillator", -24.0, 24.0, 0.0, kStepped},
   {kDual, "Dual", "Oscillator", 0.0, 1.0, 0.0, kChoice},
   {kDetune, "Detune", "Oscillator", -1.0, 1.0, 0.2, kMod},
   {kFilterMode, "Filter Mode", "Filter", 0.0, 2.0, 0.0, kChoice},
   {kSlope, "Slope", "Filter", 0.0, 3.0, 1.0, kChoice},
   {kCutoff, "Cutoff", "Filter", 0.0, 1.0, 2.0 / 3.0, kMod},
   {kResonance, "Resonance", "Filter", 0.0, 1.0, 0.3, kMod},
   {kKeyTrack, "Key Track", "Filter", 0.0, 1.0, 0.0, kMod},
   // Env 1 shapes the VCA and Env 2 the filter, to start with; either can go anywhere.
   {kEnv1Attack, "Env 1 Attack", "Env 1", 1.0, 2000.0, 5.0, kMod},
   {kEnv1Decay, "Env 1 Decay", "Env 1", 1.0, 2000.0, 400.0, kMod},
   {kEnv1Sustain, "Env 1 Sustain", "Env 1", 0.0, 1.0, 0.7, kMod},
   {kEnv1Release, "Env 1 Release", "Env 1", 1.0, 4000.0, 300.0, kMod},
   {kEnv1Loop, "Env 1 Loop", "Env 1", 0.0, 1.0, 0.0, kGlobalChoice},
   {kEnv1ToFilter, "Env 1 to Filter", "Env 1", -5.0, 5.0, 0.0, kMod},
   {kEnv1ToVca, "Env 1 to VCA", "Env 1", 0.0, 1.0, 1.0, kMod},
   {kEnv1ToPitch, "Env 1 to Pitch", "Env 1", -24.0, 24.0, 0.0, kMod},
   {kEnv2Attack, "Env 2 Attack", "Env 2", 1.0, 2000.0, 5.0, kMod},
   {kEnv2Decay, "Env 2 Decay", "Env 2", 1.0, 2000.0, 300.0, kMod},
   {kEnv2Sustain, "Env 2 Sustain", "Env 2", 0.0, 1.0, 0.3, kMod},
   {kEnv2Release, "Env 2 Release", "Env 2", 1.0, 4000.0, 300.0, kMod},
   {kEnv2Loop, "Env 2 Loop", "Env 2", 0.0, 1.0, 0.0, kGlobalChoice},
   {kEnv2ToFilter, "Env 2 to Filter", "Env 2", -5.0, 5.0, 2.0, kMod},
   {kEnv2ToVca, "Env 2 to VCA", "Env 2", 0.0, 1.0, 0.0, kMod},
   {kEnv2ToPitch, "Env 2 to Pitch", "Env 2", -24.0, 24.0, 0.0, kMod},
   {kLfoShape, "LFO Shape", "LFO", 0.0, 4.0, 1.0, kChoice},
   {kLfoRate, "LFO Rate", "LFO", 0.0, 1.0, 0.6, kMod},
   {kLfoToCutoff, "LFO to Cutoff", "LFO", -4.0, 4.0, 0.0, kMod},
   {kLfoToVca, "LFO to VCA", "LFO", 0.0, 1.0, 0.0, kMod},
   {kLevel, "Level", "Output", -60.0, 0.0, -12.0, kMod},
   {kBendRange, "MPE Bend Range", "Expression", 1.0, 96.0, 48.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
   {kPressureAmount, "Pressure", "Expression", 0.0, 4.0, 2.0, kMod},
   {kTimbreAmount, "Timbre", "Expression", 0.0, 4.0, 2.0, kMod},
   // Full velocity on the VCA by default, as before it had a knob.
   {kVelocityToVca, "Velocity to VCA", "Velocity", 0.0, 1.0, 1.0, kMod},
   {kVelocityToFilter, "Velocity to Filter", "Velocity", 0.0, 4.0, 0.0, kMod},
   {kLfoToPulseWidth, "LFO to Pulse Width", "LFO", 0.0, 1.0, 0.0, kMod},
};

// Choice names, in value order.
inline const char *const kWaveNames[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
inline const char *const kFilterModeNames[] = {"Low Pass", "High Pass", "Band Pass"};
inline const char *const kSlopeNames[] = {"12 dB", "24 dB", "36 dB", "48 dB"};
inline const char *const kLoopNames[] = {"Off", "On"};
inline const char *const kLfoShapeNames[] = {"Sine", "Triangle", "Saw", "Square", "Random"};

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kWave:
      count = 5;
      return kWaveNames;
   case kFilterMode:
      count = 3;
      return kFilterModeNames;
   case kSlope:
      count = 4;
      return kSlopeNames;
   case kLfoShape:
      count = 5;
      return kLfoShapeNames;
   case kEnv1Loop:
   case kEnv2Loop:
   case kDual:
      count = 2;
      return kLoopNames;
   default:
      count = 0;
      return nullptr;
   }
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   switch (id) {
   case kPulseWidth:
   case kResonance:
   case kKeyTrack:
   case kEnv1Sustain:
   case kEnv2Sustain:
   case kEnv1ToVca:
   case kEnv2ToVca:
   case kLfoToVca:
   case kVelocityToVca:
   case kLfoToPulseWidth:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   case kTune:
   case kBendRange:
      std::snprintf(out, size, id == kTune ? "%+d st" : "%d st", static_cast<int>(std::lround(value)));
      break;
   case kCutoff: {
      const double hz = cutoffHz(value);
      if (hz < 999.5)
         std::snprintf(out, size, "%.0f Hz", hz);
      else
         std::snprintf(out, size, "%.2f kHz", hz / 1000.0);
      break;
   }
   case kEnv1ToFilter:
   case kEnv2ToFilter:
   case kLfoToCutoff:
      std::snprintf(out, size, "%+.1f oct", value);
      break;
   case kLfoRate: {
      const double hz = lfoHz(value);
      std::snprintf(out, size, hz < 1.0 ? "%.3f Hz" : "%.2f Hz", hz);
      break;
   }
   case kPressureAmount:
   case kTimbreAmount:
   case kVelocityToFilter:
      std::snprintf(out, size, "%.1f oct", value);
      break;
   case kEnv1ToPitch:
   case kEnv2ToPitch:
      std::snprintf(out, size, "%+.1f st", value);
      break;
   case kDetune: {
      // Cents under a semitone, semitones above.
      const double st = detuneSemitones(value);
      if (std::fabs(st) < 0.995)
         std::snprintf(out, size, "%+.0f ct", st * 100.0);
      else
         std::snprintf(out, size, "%+.2f st", st);
      break;
   }
   case kEnv1Attack:
   case kEnv1Decay:
   case kEnv1Release:
   case kEnv2Attack:
   case kEnv2Decay:
   case kEnv2Release:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   case kLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   default:
      std::snprintf(out, size, "%g", value);
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers in the same unit. Hosts check that the
// two round-trip.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (*text == ' ')
      ++text;
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      for (int i = 0; i < count; ++i)
         if (!std::strcmp(text, names[i])) {
            value = i;
            return true;
         }
   }
   char *end = nullptr;
   const double v = std::strtod(text, &end);
   if (end == text)
      return false;
   while (*end == ' ')
      ++end;
   switch (id) {
   case kPulseWidth:
   case kResonance:
   case kKeyTrack:
   case kEnv1Sustain:
   case kEnv2Sustain:
   case kEnv1ToVca:
   case kEnv2ToVca:
   case kLfoToVca:
   case kVelocityToVca:
   case kLfoToPulseWidth:
      value = v / 100.0;
      break;
   case kCutoff:
      // Hz, or kHz with a k.
      value = cutoffValue(std::fmax(1e-3, (*end == 'k' || *end == 'K') ? v * 1000.0 : v));
      break;
   case kLfoRate:
      value = lfoValue(std::fmax(1e-6, v));
      break;
   case kDetune:
      // Cents with a c, otherwise semitones.
      value = detuneValue(*end == 'c' || *end == 'C' ? v / 100.0 : v);
      break;
   default:
      // A stepped param takes whole values only.
      value = (kParams[id].flags & CLAP_PARAM_IS_STEPPED) ? std::round(v) : v;
      break;
   }
   return true;
}

} // namespace basic_synth

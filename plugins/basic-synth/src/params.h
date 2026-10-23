#pragma once

// Basic Synth's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace basic_synth {

enum ParamId : clap_id {
   // Oscillator.
   kWave,
   kPulseWidth,
   kTune,
   // Filter.
   kFilterMode,
   kSlope,
   kCutoff,
   kResonance,
   kEnvAmount,
   // Envelope, for both the VCA and the filter.
   kAttack,
   kDecay,
   kSustain,
   kRelease,
   // Output, and what MPE expression does.
   kLevel,
   kBendRange,
   kPressureAmount,
   kTimbreAmount,
   kParamCount
};

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

// The cutoff param runs 0..1 so a knob sweeps it evenly by ear: 20 Hz times 1000 to that power.
constexpr double kCutoffLow = 20.0;
constexpr double kCutoffSpan = 1000.0;
inline double cutoffHz(double value) { return kCutoffLow * std::pow(kCutoffSpan, value); }
inline double cutoffValue(double hz) { return std::log(hz / kCutoffLow) / std::log(kCutoffSpan); }

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kWave, "Wave", "Oscillator", 0.0, 4.0, 2.0, kChoice},
   {kPulseWidth, "Pulse Width", "Oscillator", 0.05, 0.95, 0.5, kMod},
   {kTune, "Tune", "Oscillator", -24.0, 24.0, 0.0, kAuto | CLAP_PARAM_IS_STEPPED},
   {kFilterMode, "Filter Mode", "Filter", 0.0, 2.0, 0.0, kChoice},
   {kSlope, "Slope", "Filter", 0.0, 3.0, 1.0, kChoice},
   {kCutoff, "Cutoff", "Filter", 0.0, 1.0, 2.0 / 3.0, kMod},
   {kResonance, "Resonance", "Filter", 0.0, 1.0, 0.3, kMod},
   {kEnvAmount, "Env Amount", "Filter", -5.0, 5.0, 2.0, kMod},
   {kAttack, "Attack", "Envelope", 1.0, 2000.0, 5.0, kMod},
   {kDecay, "Decay", "Envelope", 1.0, 2000.0, 400.0, kMod},
   {kSustain, "Sustain", "Envelope", 0.0, 1.0, 0.5, kMod},
   {kRelease, "Release", "Envelope", 1.0, 4000.0, 300.0, kMod},
   {kLevel, "Level", "Output", -60.0, 0.0, -12.0, kMod},
   {kBendRange, "MPE Bend Range", "Expression", 1.0, 96.0, 48.0, kAuto | CLAP_PARAM_IS_STEPPED},
   {kPressureAmount, "Pressure", "Expression", 0.0, 4.0, 2.0, kMod},
   {kTimbreAmount, "Timbre", "Expression", 0.0, 4.0, 2.0, kMod},
};

// Choice names, in value order.
inline const char *const kWaveNames[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
inline const char *const kFilterModeNames[] = {"Low Pass", "High Pass", "Band Pass"};
inline const char *const kSlopeNames[] = {"12 dB", "24 dB", "36 dB", "48 dB"};

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
   case kSustain:
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
   case kEnvAmount:
      std::snprintf(out, size, "%+.1f oct", value);
      break;
   case kPressureAmount:
   case kTimbreAmount:
      std::snprintf(out, size, "%.1f oct", value);
      break;
   case kAttack:
   case kDecay:
   case kRelease:
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
   case kSustain:
      value = v / 100.0;
      break;
   case kCutoff:
      // Hz, or kHz with a k.
      value = cutoffValue(std::fmax(1e-3, (*end == 'k' || *end == 'K') ? v * 1000.0 : v));
      break;
   default:
      value = v;
      break;
   }
   return true;
}

} // namespace basic_synth

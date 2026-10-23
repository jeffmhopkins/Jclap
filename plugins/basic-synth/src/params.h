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
   // Filter, and its envelope.
   kFilterMode,
   kSlope,
   kCutoff,
   kResonance,
   kFilterEnvAmount,
   kFilterAttack,
   kFilterDecay,
   kFilterSustain,
   kFilterRelease,
   // The VCA's envelope.
   kAmpEnvAmount,
   kAmpAttack,
   kAmpDecay,
   kAmpSustain,
   kAmpRelease,
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
   kParamCount
};

// Every param can be automated and modulated from the host. Modulation can also be per voice (by note
// id, key or channel), which is what Bitwig's polyphonic modulators send. MPE Bend Range is the one
// param with no per-voice meaning.
constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = kStepped | CLAP_PARAM_IS_ENUM;

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

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kWave, "Wave", "Oscillator", 0.0, 4.0, 2.0, kChoice},
   {kPulseWidth, "Pulse Width", "Oscillator", 0.05, 0.95, 0.5, kMod},
   {kTune, "Tune", "Oscillator", -24.0, 24.0, 0.0, kStepped},
   {kFilterMode, "Filter Mode", "Filter", 0.0, 2.0, 0.0, kChoice},
   {kSlope, "Slope", "Filter", 0.0, 3.0, 1.0, kChoice},
   {kCutoff, "Cutoff", "Filter", 0.0, 1.0, 2.0 / 3.0, kMod},
   {kResonance, "Resonance", "Filter", 0.0, 1.0, 0.3, kMod},
   {kFilterEnvAmount, "Filter Env Amount", "Filter Envelope", -5.0, 5.0, 2.0, kMod},
   {kFilterAttack, "Filter Attack", "Filter Envelope", 1.0, 2000.0, 5.0, kMod},
   {kFilterDecay, "Filter Decay", "Filter Envelope", 1.0, 2000.0, 300.0, kMod},
   {kFilterSustain, "Filter Sustain", "Filter Envelope", 0.0, 1.0, 0.3, kMod},
   {kFilterRelease, "Filter Release", "Filter Envelope", 1.0, 4000.0, 300.0, kMod},
   {kAmpEnvAmount, "Amp Env Amount", "Amp Envelope", 0.0, 1.0, 1.0, kMod},
   {kAmpAttack, "Amp Attack", "Amp Envelope", 1.0, 2000.0, 5.0, kMod},
   {kAmpDecay, "Amp Decay", "Amp Envelope", 1.0, 2000.0, 400.0, kMod},
   {kAmpSustain, "Amp Sustain", "Amp Envelope", 0.0, 1.0, 0.7, kMod},
   {kAmpRelease, "Amp Release", "Amp Envelope", 1.0, 4000.0, 300.0, kMod},
   {kLfoShape, "LFO Shape", "LFO", 0.0, 4.0, 1.0, kChoice},
   {kLfoRate, "LFO Rate", "LFO", 0.0, 1.0, 0.6, kMod},
   {kLfoToCutoff, "LFO to Cutoff", "LFO", -4.0, 4.0, 0.0, kMod},
   {kLfoToVca, "LFO to VCA", "LFO", 0.0, 1.0, 0.0, kMod},
   {kLevel, "Level", "Output", -60.0, 0.0, -12.0, kMod},
   {kBendRange, "MPE Bend Range", "Expression", 1.0, 96.0, 48.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
   {kPressureAmount, "Pressure", "Expression", 0.0, 4.0, 2.0, kMod},
   {kTimbreAmount, "Timbre", "Expression", 0.0, 4.0, 2.0, kMod},
};

// Choice names, in value order.
inline const char *const kWaveNames[] = {"Sine", "Triangle", "Saw", "Square", "Pulse"};
inline const char *const kFilterModeNames[] = {"Low Pass", "High Pass", "Band Pass"};
inline const char *const kSlopeNames[] = {"12 dB", "24 dB", "36 dB", "48 dB"};
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
   case kFilterSustain:
   case kAmpSustain:
   case kAmpEnvAmount:
   case kLfoToVca:
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
   case kFilterEnvAmount:
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
      std::snprintf(out, size, "%.1f oct", value);
      break;
   case kFilterAttack:
   case kFilterDecay:
   case kFilterRelease:
   case kAmpAttack:
   case kAmpDecay:
   case kAmpRelease:
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
   case kFilterSustain:
   case kAmpSustain:
   case kAmpEnvAmount:
   case kLfoToVca:
      value = v / 100.0;
      break;
   case kCutoff:
      // Hz, or kHz with a k.
      value = cutoffValue(std::fmax(1e-3, (*end == 'k' || *end == 'K') ? v * 1000.0 : v));
      break;
   case kLfoRate:
      value = lfoValue(std::fmax(1e-6, v));
      break;
   default:
      value = v;
      break;
   }
   return true;
}

} // namespace basic_synth

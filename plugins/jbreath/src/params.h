#pragma once

// JBreath's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jbreath {

enum ParamId : clap_id {
   // The instrument, and how it's blown.
   kModel,
   kBreath,
   kEmbouchure,
   kNoise,
   kEdge,
   // How a note starts and ends.
   kKick,
   kAttack,
   kRelease,
   // The bore.
   kTune,
   kFine,
   kBright,
   kGlide,
   // Vibrato.
   kVibRate,
   kVibDepth,
   kVibPitch,
   kVibDelay,
   // Voice and expression.
   kVoiceMode,
   kBreathSource,
   kCurve,
   kSlide,
   kBendRange,
   kLevel,
   // Added after the first build: an input to blow from (a CV, an LFO, automation).
   kBreathIn,
   kParamCount
};

constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
// The instrument, voice mode and breath source are the whole plugin's: no per-voice meaning.
constexpr auto kGlobalChoice = kGlobalMod | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

enum VoiceMode { kPoly, kMono, kLegato };
enum BreathSource { kSourceAuto, kSourcePressure, kSourceVelocity, kSourceInput, kSourceMix };

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kModel, "Instrument", "Exciter", 0.0, 4.0, 1.0, kGlobalChoice},
   {kBreath, "Breath", "Exciter", 0.25, 1.5, 1.0, kMod},
   {kEmbouchure, "Embouchure", "Exciter", 0.0, 1.0, 0.5, kMod},
   {kNoise, "Noise", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kEdge, "Edge", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kKick, "Kick", "Articulation", 0.0, 1.0, 0.25, kMod},
   {kAttack, "Attack", "Articulation", 1.0, 400.0, 25.0, kMod},
   {kRelease, "Release", "Articulation", 5.0, 2000.0, 90.0, kMod},
   {kTune, "Tune", "Bore", -24.0, 24.0, 0.0, kStepped},
   {kFine, "Fine", "Bore", -100.0, 100.0, 0.0, kMod},
   {kBright, "Bright", "Bore", -12.0, 12.0, 0.0, kGlobalMod},
   {kGlide, "Glide", "Bore", 0.0, 1000.0, 0.0, kMod},
   {kVibRate, "Vibrato Rate", "Vibrato", 2.0, 9.0, 5.1, kMod},
   {kVibDepth, "Vibrato Depth", "Vibrato", 0.0, 0.3, 0.05, kMod},
   {kVibPitch, "Vibrato Pitch", "Vibrato", 0.0, 50.0, 0.0, kMod},
   {kVibDelay, "Vibrato Delay", "Vibrato", 0.0, 2000.0, 350.0, kMod},
   {kVoiceMode, "Voice Mode", "Voice", 0.0, 2.0, 0.0, kGlobalChoice},
   {kBreathSource, "Breath Source", "Voice", 0.0, 4.0, 0.0, kGlobalChoice},
   {kCurve, "Press Curve", "Voice", 0.3, 3.0, 1.0, kMod},
   {kSlide, "Slide", "Voice", 0.0, 1.0, 0.5, kMod},
   {kBendRange, "MPE Bend Range", "Voice", 1.0, 96.0, 48.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
   {kLevel, "Level", "Output", -60.0, 12.0, 0.0, kMod},
   {kBreathIn, "Breath In", "Voice", 0.0, 1.0, 0.0, kMod},
};

inline const char *const kModelNames[] = {"Flute", "Clarinet", "Sax", "Oboe", "Trumpet"};
inline const char *const kVoiceModeNames[] = {"Poly", "Mono", "Legato"};
inline const char *const kBreathSourceNames[] = {"Auto", "Pressure", "Velocity", "Input", "Mix"};

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kModel:
      count = 5;
      return kModelNames;
   case kVoiceMode:
      count = 3;
      return kVoiceModeNames;
   case kBreathSource:
      count = 5;
      return kBreathSourceNames;
   default:
      count = 0;
      return nullptr;
   }
}

inline bool isPercent(clap_id id) {
   switch (id) {
   case kBreath:
   case kEmbouchure:
   case kNoise:
   case kEdge:
   case kKick:
   case kVibDepth:
   case kSlide:
   case kBreathIn:
      return true;
   default:
      return false;
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
   if (isPercent(id)) {
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return;
   }
   switch (id) {
   case kAttack:
   case kRelease:
   case kGlide:
   case kVibDelay:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   case kTune:
      std::snprintf(out, size, "%+d st", static_cast<int>(std::lround(value)));
      break;
   case kBendRange:
      std::snprintf(out, size, "%d st", static_cast<int>(std::lround(value)));
      break;
   case kFine:
      std::snprintf(out, size, "%+.0f ct", value);
      break;
   case kVibPitch:
      std::snprintf(out, size, "%.0f ct", value);
      break;
   case kBright:
      std::snprintf(out, size, "%+.1f dB", value);
      break;
   case kLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kVibRate:
      std::snprintf(out, size, "%.2f Hz", value);
      break;
   case kCurve:
      std::snprintf(out, size, "%.2f", value);
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
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v, nullptr))
      return false;
   if (isPercent(id))
      value = v / 100.0;
   else
      value = (kParams[id].flags & CLAP_PARAM_IS_STEPPED) ? std::round(v) : v;
   return true;
}

} // namespace jbreath

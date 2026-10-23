#pragma once

// The plugin's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plugin_template {

enum ParamId : clap_id {
   // The mixer: what happens to the audio passing through.
   kGain,
   kPan,
   kMute,
   kWidth,
   kRouting,
   // The test tone: a one-voice synth played from the panel's keyboard or the host's notes.
   kWave,
   kTranspose,
   kToneLevel,
   kAttack,
   kDecay,
   kSustain,
   kRelease,
   kParamCount
};

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kGain, "Gain", "Mixer", -60.0, 12.0, 0.0, kMod},
   {kPan, "Pan", "Mixer", -1.0, 1.0, 0.0, kMod},
   {kMute, "Mute", "Mixer", 0.0, 1.0, 0.0, kAuto | CLAP_PARAM_IS_STEPPED},
   {kWidth, "Width", "Mixer", 0.0, 2.0, 1.0, kMod},
   {kRouting, "Routing", "Mixer", 0.0, 3.0, 0.0, kChoice},
   {kWave, "Wave", "Test Tone", 0.0, 3.0, 0.0, kChoice},
   {kTranspose, "Transpose", "Test Tone", -24.0, 24.0, 0.0, kAuto | CLAP_PARAM_IS_STEPPED},
   {kToneLevel, "Tone Level", "Test Tone", -60.0, 0.0, -12.0, kMod},
   {kAttack, "Attack", "Test Tone", 1.0, 2000.0, 10.0, kMod},
   {kDecay, "Decay", "Test Tone", 1.0, 2000.0, 300.0, kMod},
   {kSustain, "Sustain", "Test Tone", 0.0, 1.0, 0.6, kMod},
   {kRelease, "Release", "Test Tone", 1.0, 4000.0, 500.0, kMod},
};

// Choice names, in value order.
inline const char *const kRoutingNames[] = {"Normal", "Swap L/R", "Left only", "Right only"};
inline const char *const kWaveNames[] = {"Sine", "Triangle", "Saw", "Square"};
constexpr int kRoutingCount = 4;
constexpr int kWaveCount = 4;

inline const char *const *choiceNames(clap_id id, int &count) {
   count = id == kRouting ? kRoutingCount : id == kWave ? kWaveCount : 0;
   return id == kRouting ? kRoutingNames : id == kWave ? kWaveNames : nullptr;
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
   case kGain:
   case kToneLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kPan:
      if (value > -0.005 && value < 0.005)
         std::snprintf(out, size, "C");
      else
         std::snprintf(out, size, "%.0f%c", (value < 0 ? -value : value) * 100.0, value < 0 ? 'L' : 'R');
      break;
   case kWidth:
   case kSustain:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   case kTranspose:
      std::snprintf(out, size, "%+d st", static_cast<int>(value + (value < 0 ? -0.5 : 0.5)));
      break;
   case kAttack:
   case kDecay:
   case kRelease:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   default:
      std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers. Hosts check that the two round-trip.
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
   if (id == kMute) {
      if (!std::strncmp(text, "On", 2) || !std::strncmp(text, "Off", 3)) {
         value = text[1] == 'n' ? 1.0 : 0.0;
         return true;
      }
   }
   if (id == kPan && (text[0] == 'C' || text[0] == 'c') && text[1] == '\0') {
      value = 0.0;
      return true;
   }
   char *end = nullptr;
   const double v = std::strtod(text, &end);
   if (end == text)
      return false;
   while (*end == ' ')
      ++end;
   if (id == kPan && (*end == 'L' || *end == 'l' || *end == 'R' || *end == 'r'))
      value = (*end == 'L' || *end == 'l' ? -v : v) / 100.0;
   else if (id == kWidth || id == kSustain)
      value = v / 100.0;
   else
      value = v;
   return true;
}

} // namespace plugin_template

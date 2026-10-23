#pragma once

// The plugin's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plugin_template {

enum ParamId : clap_id { kGain, kPan, kMute, kWidth, kRouting, kParamCount };

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kGain, "Gain", "", -60.0, 12.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE},
   {kPan, "Pan", "", -1.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE},
   {kMute, "Mute", "", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED},
   {kWidth, "Width", "", 0.0, 2.0, 1.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE},
   {kRouting, "Routing", "", 0.0, 3.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM},
};

// The Routing choices, in value order.
inline const char *const kRoutingNames[] = {"Normal", "Swap L/R", "Left only", "Right only"};
constexpr int kRoutingCount = 4;

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   switch (id) {
   case kGain:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kPan:
      if (value > -0.005 && value < 0.005)
         std::snprintf(out, size, "C");
      else
         std::snprintf(out, size, "%.0f%c", (value < 0 ? -value : value) * 100.0, value < 0 ? 'L' : 'R');
      break;
   case kWidth:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   case kRouting: {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", kRoutingNames[i < 0 ? 0 : i >= kRoutingCount ? kRoutingCount - 1 : i]);
      break;
   }
   default:
      std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers. Hosts check that the two round-trip.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (*text == ' ')
      ++text;
   if (id == kMute) {
      if (!std::strncmp(text, "On", 2) || !std::strncmp(text, "Off", 3)) {
         value = text[1] == 'n' ? 1.0 : 0.0;
         return true;
      }
   }
   if (id == kRouting) {
      for (int i = 0; i < kRoutingCount; ++i)
         if (!std::strcmp(text, kRoutingNames[i])) {
            value = i;
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
   else if (id == kWidth)
      value = v / 100.0;
   else
      value = v;
   return true;
}

} // namespace plugin_template

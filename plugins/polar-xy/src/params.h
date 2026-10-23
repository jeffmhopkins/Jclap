// Polar XY's params: ids, ranges and display text, shared by the plugin and the panel.
#pragma once

#include <clap/clap.h>

#include <cstdio>
#include <cstring>

namespace polar_xy {

enum ParamId : clap_id {
   kParamInX = 0,
   kParamInY,
   kParamOutAngle,
   kParamOutRadius,
   kParamAngleOffset,
   kParamDirection,
   kParamRadiusMode,
   kParamPassThrough,
   kParamCount
};

struct ParamInfo {
   const char *name;
   double min, max, def;
   bool stepped;
};

inline constexpr ParamInfo kParams[kParamCount] = {
   {"X In CC", 0, 127, 16, true},
   {"Y In CC", 0, 127, 17, true},
   {"Angle Out CC", 0, 127, 18, true},
   {"Radius Out CC", 0, 127, 19, true},
   {"Angle Offset", 0, 360, 0, false},
   {"Direction", 0, 1, 0, true},
   {"Radius Mode", 0, 1, 0, true},
   {"Pass Input CCs", 0, 1, 0, true},
};

// Labels for the two-state params, off first.
inline const char *const kDirectionLabels[] = {"Counter-clockwise", "Clockwise"};
inline const char *const kRadiusModeLabels[] = {"Clamp to circle", "Fill square"};
inline const char *const kOnOffLabels[] = {"Off", "On"};

inline const char *const *choiceLabels(clap_id id) {
   switch (id) {
   case kParamDirection:
      return kDirectionLabels;
   case kParamRadiusMode:
      return kRadiusModeLabels;
   case kParamPassThrough:
      return kOnOffLabels;
   default:
      return nullptr;
   }
}

inline void formatParam(clap_id id, double value, char *out, uint32_t size) {
   if (const char *const *labels = choiceLabels(id))
      std::snprintf(out, size, "%s", labels[value >= 0.5 ? 1 : 0]);
   else if (id == kParamAngleOffset)
      std::snprintf(out, size, "%.1f deg", value);
   else
      std::snprintf(out, size, "CC %d", static_cast<int>(value + 0.5));
}

inline bool parseParam(clap_id id, const char *text, double &value) {
   if (const char *const *labels = choiceLabels(id)) {
      for (int i = 0; i < 2; ++i)
         if (!std::strcmp(text, labels[i])) {
            value = i;
            return true;
         }
   }
   if (std::strncmp(text, "CC", 2) == 0)
      text += 2;
   return std::sscanf(text, "%lf", &value) == 1;
}

} // namespace polar_xy

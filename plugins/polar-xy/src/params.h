// Polar XY's params: ids, ranges and display text, shared by the plugin and the panel.
#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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
   kParamSmoothing,
   kParamInertia,
   kParamMotionPath,
   kParamResolution,
   kParamInertiaMode,
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
   {"Smoothing", 0, 2000, 0, false}, // ms
   {"Inertia", 0, 100, 0, false},    // %
   {"Motion Path", 0, 1, 0, true},
   {"Output Resolution", 0, 1, 0, true},
   {"Inertia Mode", 0, 1, 0, true},
};

// Labels for the two-state params, off first.
inline const char *const kDirectionLabels[] = {"Counter-clockwise", "Clockwise"};
inline const char *const kRadiusModeLabels[] = {"Clamp to circle", "Fill square"};
inline const char *const kOnOffLabels[] = {"Off", "On"};
inline const char *const kMotionPathLabels[] = {"Straight", "Around"};
inline const char *const kResolutionLabels[] = {"7-bit", "14-bit"};
inline const char *const kInertiaModeLabels[] = {"Pull", "Push"};

// 14-bit output sends each value's LSB on its CC + 32, so it needs both output CCs below 32; on
// higher ones the output stays 7-bit.
inline bool fineOutput(int outAngle, int outRadius, bool wanted) { return wanted && outAngle < 32 && outRadius < 32; }

inline const char *const *choiceLabels(clap_id id) {
   switch (id) {
   case kParamDirection:
      return kDirectionLabels;
   case kParamRadiusMode:
      return kRadiusModeLabels;
   case kParamPassThrough:
      return kOnOffLabels;
   case kParamMotionPath:
      return kMotionPathLabels;
   case kParamResolution:
      return kResolutionLabels;
   case kParamInertiaMode:
      return kInertiaModeLabels;
   default:
      return nullptr;
   }
}

inline void formatParam(clap_id id, double value, char *out, uint32_t size) {
   if (const char *const *labels = choiceLabels(id))
      std::snprintf(out, size, "%s", labels[value >= 0.5 ? 1 : 0]);
   else if (id == kParamAngleOffset) {
      // Rounded first, so 359.96 shows as 0.0, the angle it is, not 360.0.
      double shown = std::round(value * 10.0) / 10.0;
      if (shown >= 360.0)
         shown -= 360.0;
      std::snprintf(out, size, "%.1f deg", shown);
   }
   else if (id == kParamSmoothing)
      std::snprintf(out, size, value >= 1000.0 ? "%.2f s" : "%.0f ms", value >= 1000.0 ? value / 1000.0 : value);
   else if (id == kParamInertia)
      std::snprintf(out, size, "%.0f%%", value);
   else
      std::snprintf(out, size, "CC %d", static_cast<int>(value + 0.5));
}

inline bool sameText(const char *a, const char *b) {
   for (; *a && *b; ++a, ++b)
      if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
         return false;
   return *a == *b;
}

// Labels in any case, or a number ("CC 20", "cc20" and " 20" too), kept to the param's range. An
// angle offset wraps around the circle instead, so -90 is 270. Smoothing reads "1.5 s" as 1500 ms.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (std::isspace(static_cast<unsigned char>(*text)))
      ++text;
   if (const char *const *labels = choiceLabels(id)) {
      for (int i = 0; i < 2; ++i)
         if (sameText(text, labels[i])) {
            value = i;
            return true;
         }
   }
   if (std::tolower(static_cast<unsigned char>(text[0])) == 'c' && std::tolower(static_cast<unsigned char>(text[1])) == 'c')
      text += 2;
   double v;
   if (std::sscanf(text, "%lf", &v) != 1 || !std::isfinite(v))
      return false;
   if (id == kParamAngleOffset) {
      v = std::fmod(v, 360.0);
      value = v < 0.0 ? v + 360.0 : v + 0.0; // + 0.0 turns -0 into 0
      return true;
   }
   if (id == kParamSmoothing) {
      const char *unit = text;
      while (*unit && (std::isdigit(static_cast<unsigned char>(*unit)) || std::strchr("+-.eE", *unit)))
         ++unit;
      while (std::isspace(static_cast<unsigned char>(*unit)))
         ++unit;
      if (std::tolower(static_cast<unsigned char>(unit[0])) == 's')
         v *= 1000.0;
   }
   value = std::clamp(v, kParams[id].min, kParams[id].max);
   return true;
}

} // namespace polar_xy

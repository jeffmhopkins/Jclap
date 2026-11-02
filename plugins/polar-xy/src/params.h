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
   kParamTouchFilter,
   kParamTouchIn,
   kParamTouchOut,
   kParamPolarOut,
   kParamCenterSmoothing,
   kParamTouchJump,
   kParamTouchPress,
   kParamTouchAgree,
   kParamUnused21, // was Center Lag (until 0.18), then briefly Center Steady: now unused
   kParamTouchHeading,
   kParamCount
};

struct ParamInfo {
   const char *name;
   double min, max, def;
   bool stepped;
   bool hidden = false; // kept only so the params after it keep their ids; not shown or automatable
};

inline constexpr ParamInfo kParams[kParamCount] = {
   {"X In CC", 0, 127, 16, true},
   {"Y In CC", 0, 127, 17, true},
   {"Angle Out CC", 0, 127, 18, true},
   {"Radius Out CC", 0, 127, 19, true},
   {"Angle Offset", 0, 360, 0, false},
   {"Direction", 0, 1, 0, true},
   {"Radius Mode", 0, 1, 0, true},
   {"Pass Input CCs", 0, 2, 0, true}, // Off, Raw, Filtered (through Touch Filter)
   {"Smoothing", 0, 2000, 0, false}, // ms
   {"Inertia", 0, 100, 0, false},    // %
   {"Motion Path", 0, 1, 0, true},
   {"Output Resolution", 0, 1, 0, true},
   {"Inertia Mode", 0, 2, 0, true},
   {"Touch Filter", 0, 100, 0, true}, // ms; 0: off
   {"Touch In CC", -1, 127, -1, true},  // -1: none
   {"Touch Out CC", -1, 127, -1, true}, // -1: none
   {"Polar Out", 0, 1, 1, true},        // Off: no angle or radius goes out
   {"Center", 0, 50, 0, false},           // %: the centre zone, where the angle is steadied and Around motion goes straight
   {"Jump Limit", 8, 64, 16, true},       // CC steps one reading may jump before a second must agree
   {"Press Time", -1, 100, -1, true},     // ms a touch's press is held back; -1: the same as Touch Filter
   {"Agree", 1, 16, 4, true},             // CC steps two readings may differ by and still agree
   {"Unused", 0, 1, 0, true, true},       // was Center Lag; kept so Jump Check keeps its id
   {"Jump Check", 0, 1, 0, true},         // Distance (from the last reading) or Heading (from where it was going)
};

// Labels for the choice params, off (or the first) first: one per step of the param's range.
inline const char *const kDirectionLabels[] = {"Counter-clockwise", "Clockwise"};
inline const char *const kRadiusModeLabels[] = {"Clamp to circle", "Fill square"};
inline const char *const kPassLabels[] = {"Off", "Raw", "Filtered"};
inline const char *const kOnOffLabels[] = {"Off", "On"};
inline const char *const kMotionPathLabels[] = {"Straight", "Around"};
inline const char *const kResolutionLabels[] = {"7-bit", "14-bit"};
inline const char *const kInertiaModeLabels[] = {"Pull", "Coast", "Pad"};
inline const char *const kJumpCheckLabels[] = {"Distance", "Heading"};

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
      return kPassLabels;
   case kParamPolarOut:
      return kOnOffLabels;
   case kParamMotionPath:
      return kMotionPathLabels;
   case kParamResolution:
      return kResolutionLabels;
   case kParamInertiaMode:
      return kInertiaModeLabels;
   case kParamTouchHeading:
      return kJumpCheckLabels;
   default:
      return nullptr;
   }
}

inline void formatParam(clap_id id, double value, char *out, uint32_t size) {
   if (const char *const *labels = choiceLabels(id))
      std::snprintf(out, size, "%s", labels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kParams[id].max))]);
   else if (id == kParamAngleOffset) {
      // Rounded first, so 359.96 shows as 0.0, the angle it is, not 360.0.
      double shown = std::round(value * 10.0) / 10.0;
      if (shown >= 360.0)
         shown -= 360.0;
      std::snprintf(out, size, "%.1f deg", shown);
   }
   else if (id == kParamSmoothing)
      std::snprintf(out, size, value >= 1000.0 ? "%.2f s" : "%.0f ms", value >= 1000.0 ? value / 1000.0 : value);
   else if (id == kParamInertia || id == kParamCenterSmoothing)
      std::snprintf(out, size, value <= 0.0 && id == kParamCenterSmoothing ? "Off" : "%.0f%%", value);
   else if (id == kParamTouchJump || id == kParamTouchAgree)
      std::snprintf(out, size, std::lround(value) == 1 ? "%.0f step" : "%.0f steps", value);
   else if (id == kParamTouchPress && std::lround(value) < 0)
      std::snprintf(out, size, "Same");
   else if (id == kParamTouchPress)
      std::snprintf(out, size, "%.0f ms", value);
   else if (id == kParamTouchFilter)
      std::snprintf(out, size, std::lround(value) <= 0 ? "Off" : "%.0f ms", value);
   else if (std::lround(value) < 0)
      std::snprintf(out, size, "Off");
   else
      std::snprintf(out, size, "CC %d", static_cast<int>(value + 0.5));
}

// A touch CC param (Touch In or Out CC): its CC, or -1 for none.
inline int touchCC(double value) { return std::lround(value) < 0 ? -1 : static_cast<int>(std::lround(value)); }

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
   if ((id == kParamTouchIn || id == kParamTouchOut || id == kParamTouchFilter || id == kParamCenterSmoothing) &&
       (sameText(text, "off") || sameText(text, "none"))) {
      value = kParams[id].min;
      return true;
   }
   if (const char *const *labels = choiceLabels(id)) {
      for (int i = 0; i <= static_cast<int>(kParams[id].max); ++i)
         if (sameText(text, labels[i])) {
            value = i;
            return true;
         }
   }
   if (id == kParamTouchPress && sameText(text, "same")) {
      value = -1.0;
      return true;
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

#pragma once

// Polar XY's presets: a factory bank of Init (every param at its default, no filtering) and Geren, and the
// user's own saved through jclap::ui's preset files.

#include <jclap/ui/presets.h>

#include <vector>

#include "params.h"

namespace polar_xy {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"PolarXY", 1, ".pxypreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.def);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      // Geren: tuned on recordings of a resistive XY pad (an XYMIDI mini; see the README), with its
      // CCs: X 12, Y 13, touch 92, and angle and radius out on 107 and 108.
      std::vector<Preset> p{{"Init", "Init", {}},
                            {"Geren", "Controllers",
                             {{kParamInX, 12.0},
                              {kParamInY, 13.0},
                              {kParamOutAngle, 107.0},
                              {kParamOutRadius, 108.0},
                              {kParamTouchIn, 92.0},
                              {kParamTouchFilter, 50.0},
                              {kParamTouchJump, 32.0},
                              {kParamTouchHeading, 1.0},
                              {kParamCenterSmoothing, 15.0},
                              {kParamSmoothing, 150.0}}}};
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace polar_xy

#pragma once

// Polar XY's presets: a factory bank of Init (every param at its default) and Resistive Pad, and the
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
      // Resistive Pad: tuned on a resistive XY pad's recordings (see the README). Set Touch In CC to the
      // pad's touch CC too: the filter is far better with it.
      std::vector<Preset> p{{"Init", "Init", {}},
                            {"Resistive Pad", "Controllers",
                             {{kParamTouchFilter, 50.0},
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

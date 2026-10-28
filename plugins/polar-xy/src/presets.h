#pragma once

// Polar XY's presets: a factory bank of just Init (every param at its default), and the user's own
// saved through jclap::ui's preset files.

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
      std::vector<Preset> p{{"Init", "Init", {}}};
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace polar_xy

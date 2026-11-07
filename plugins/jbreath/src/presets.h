#pragma once

// JBreath's presets: a factory bank compiled in, and the user's own saved through jclap::ui's preset
// files. A factory preset lists what it changes from the defaults; every other param is filled in at
// its default, so nothing from the previous preset carries over.

#include <jclap/ui/presets.h>

#include <vector>

#include "params.h"

namespace jbreath {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"JBreath", 1, ".jbpreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      constexpr double kFluteModel = 0, kClarinetModel = 1, kSaxModel = 2, kOboeModel = 3, kTrumpetModel = 4;
      constexpr double kLegatoMode = 2, kMonoMode = 1;
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // Flutes: a breathy jet, vibrato on the breath (a flautist's comes from the diaphragm).
      p.push_back({"Night Flute", "Flute",
                   {{kModel, kFluteModel}, {kVibDepth, 0.07}, {kAttack, 35}, {kRelease, 120}}});
      p.push_back({"Bamboo Air", "Flute",
                   {{kModel, kFluteModel}, {kNoise, 1.9}, {kEdge, 0.5}, {kVibDepth, 0.12}, {kVibRate, 4.6},
                    {kVibDelay, 600}, {kAttack, 60}, {kRelease, 250}, {kVoiceMode, kLegatoMode}, {kGlide, 40}}});

      // Single reeds.
      p.push_back({"Wooden Reed", "Clarinet", {{kModel, kClarinetModel}, {kVibDepth, 0.0}, {kAttack, 20}}});
      p.push_back({"Legato Clarinet", "Clarinet",
                   {{kModel, kClarinetModel}, {kVibDepth, 0.02}, {kAttack, 20}, {kVoiceMode, kLegatoMode}}});
      p.push_back({"Smoke Tenor", "Sax",
                   {{kModel, kSaxModel}, {kVibDepth, 0.05}, {kVibPitch, 10}, {kAttack, 15}, {kKick, 0.4}, {kRelease, 110}}});
      p.push_back({"Growl Alto", "Sax",
                   {{kModel, kSaxModel}, {kTune, 7}, {kEdge, 2.0}, {kBreath, 1.15}, {kNoise, 1.5}, {kVibDepth, 0.04},
                    {kVibPitch, 14}, {kVibRate, 5.6}, {kKick, 0.4}, {kVoiceMode, kMonoMode}}});

      // Double reed.
      p.push_back({"Dusk Oboe", "Oboe",
                   {{kModel, kOboeModel}, {kVibDepth, 0.05}, {kVibPitch, 8}, {kAttack, 15}, {kKick, 0.4}, {kRelease, 100}}});

      // Brass: the lips' vibrato is in the pitch.
      p.push_back({"Brass Plume", "Trumpet",
                   {{kModel, kTrumpetModel}, {kVibDepth, 0.02}, {kVibPitch, 6}, {kAttack, 20}}});
      p.push_back({"Fanfare", "Trumpet",
                   {{kModel, kTrumpetModel}, {kEdge, 1.8}, {kKick, 0.5}, {kAttack, 10}, {kBright, 3}, {kVibDepth, 0.0},
                    {kVibPitch, 0}}});
      p.push_back({"Muted Night", "Trumpet",
                   {{kModel, kTrumpetModel}, {kEdge, 0.5}, {kBright, -8}, {kVibPitch, 8}, {kVibDepth, 0.03},
                    {kAttack, 30}, {kRelease, 160}, {kVoiceMode, kLegatoMode}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace jbreath

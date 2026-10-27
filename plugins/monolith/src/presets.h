#pragma once

// Monolith's presets: a factory bank compiled in, and the user's own saved through jclap::ui's preset
// files. A preset holds the params only, never the slab: the slab follows from the script params
// (seed, orthography, density, lexicon, grammar), so a preset always writes the same script, and a
// slab that has drifted or been carved by hand is kept with the project instead.
//
// A factory preset is written as what it changes from the defaults, then filled out with every other
// param at its default, so nothing from the previous preset carries over. The defaults were F
// Phrygian over 8 glyphs before they became A Aeolian over 2; the presets written then name F
// Phrygian themselves, so they still sound as they did.

#include <jclap/ui/presets.h>

#include <vector>

#include "params.h"

namespace monolith {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"Monolith", 1, ".mlpreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      // Orthographies, rates, directions, scales and spreads by name, for readability.
      constexpr double kRunic = 1, kCircuit = 2, kHalo = 3, kCuneiform = 4;
      constexpr double k32 = 0, k16 = 2, k8T = 3, k8 = 4, k4 = 5;
      constexpr double kReverse = 1, kPingPong = 2, kRandom = 3;
      constexpr double kMajor = 0, kMinor = 1, kDorian = 2, kPhrygian = 3, kLydian = 4, kMinorPent = 9,
                       kWholeTone = 10;
      constexpr double kSteps = 0, kFourths = 2, kFifths = 3;
      constexpr double kC = 0, kD = 2, kE = 4, kF = 5, kG = 7, kA = 9;
      std::vector<Preset> p;

      // Loops: the slab is held still, so a short length repeats exactly.
      p.push_back({"Init", "Loops", {}});
      p.push_back({"One Bar Riff", "Loops",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kLength, 1}, {kLock, 1}, {kDensity, 4}, {kSeed, 7}, {kGate, 0.7}}});
      p.push_back({"Two Bar Answer", "Loops",
                   {{kLength, 2}, {kLock, 1}, {kSeed, 42}, {kScale, kDorian}, {kRoot, kD}, {kDensity, 3}}});
      p.push_back({"Four Bar Phrase", "Loops", {{kLength, 4}, {kLock, 1}, {kSeed, 311}, {kScale, kMinor}, {kRoot, kA}}});
      p.push_back({"Circuit Bass", "Loops",
                   {{kOrthography, kCircuit}, {kLength, 2}, {kLock, 1}, {kSeed, 88}, {kDensity, 5}, {kOctave, -2},
                    {kSpread, kSteps}, {kScale, kMinorPent}, {kRoot, kE}, {kGate, 0.5}, {kVelocity, 0.9}}});
      p.push_back({"Runic March", "Loops",
                   {{kOrthography, kRunic}, {kLength, 4}, {kLock, 1}, {kSeed, 1066}, {kRate, k8}, {kDensity, 3},
                    {kScale, kMinor}, {kRoot, kG}, {kOctave, -1}, {kGate, 0.6}}});

      // Evolving: the slab drifts, so a phrase repeats, changing a glyph at a time.
      p.push_back({"Phrygian Engine", "Evolving",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kLength, 8}, {kDrift, 0.15}, {kSeed, 23}, {kDensity, 4},
                    {kSwing, 0.1}}});
      p.push_back({"Slow Drift", "Evolving",
                   {{kLength, 8}, {kDrift, 0.4}, {kGrammar, 0.15}, {kSeed, 512}, {kScale, kLydian}, {kRoot, kC},
                    {kRate, k8}, {kSpread, kFifths}, {kGate, 1.4}}});
      p.push_back({"Erosion", "Evolving",
                   {{kLength, 4}, {kDrift, 0}, {kErode, 0.35}, {kSeed, 9}, {kDensity, 6}, {kScale, kMinor},
                    {kRoot, kD}}});
      p.push_back({"Endless Script", "Evolving",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kLength, 55}, {kDrift, 0.6}, {kGrammar, 0.5}, {kLexicon, 48},
                    {kSeed, 2001}, {kHumanize, 0.3}}});
      p.push_back({"Wandering Ping-Pong", "Evolving",
                   {{kLength, 6}, {kDirection, kPingPong}, {kDrift, 0.2}, {kSeed, 77}, {kScale, kDorian},
                    {kRoot, kA}, {kSwing, 0.2}}});
      p.push_back({"Thirty-Two Bars", "Evolving",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kLength, 32}, {kDrift, 0.05}, {kLexicon, 24}, {kSeed, 3232}}});

      // Textures: slow or scattered lines for pads and atmospheres.
      p.push_back({"Halo Pad Lines", "Textures",
                   {{kOrthography, kHalo}, {kRate, k4}, {kDensity, 2}, {kLength, 8}, {kDrift, 0.3}, {kGate, 1.8},
                    {kSpread, kFourths}, {kScale, kMajor}, {kRoot, kD}, {kVelocity, 0.5}, {kSeed, 360}}});
      p.push_back({"Cuneiform Rain", "Textures",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kOrthography, kCuneiform}, {kRate, k32}, {kDensity, 6},
                    {kLength, 16}, {kDirection, kRandom}, {kDrift, 0.35}, {kGate, 0.4}, {kHumanize, 0.5}, {kOctave, 1}, {kSeed, 1750}}});
      p.push_back({"Whole Tone Murmur", "Textures",
                   {{kRoot, kF}, {kScale, kWholeTone}, {kRate, k8T}, {kDirection, kReverse}, {kLength, 12},
                    {kDrift, 0.25}, {kErode, 0.15}, {kGrammar, 0.7}, {kHumanize, 0.4}, {kVelocity, 0.6}, {kSeed, 4096}}});
      p.push_back({"Listening Stone", "Textures",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kMode, 1}, {kLength, 4}, {kLock, 1}, {kRate, k16}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace monolith

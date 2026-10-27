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
      // Orthographies, rates, directions, scales, spreads and roots by name, for readability.
      constexpr double kRunic = 1, kCircuit = 2, kHalo = 3, kCuneiform = 4;
      constexpr double k32 = 0, k16T = 1, k16 = 2, k8T = 3, k8 = 4, k4 = 5;
      constexpr double kReverse = 1, kPingPong = 2, kRandom = 3;
      constexpr double kMajor = 0, kMinor = 1, kDorian = 2, kPhrygian = 3, kLydian = 4, kMixolydian = 5,
                       kLocrian = 6, kHarmonicMinor = 7, kMajorPent = 8, kMinorPent = 9, kWholeTone = 10,
                       kChromatic = 11;
      constexpr double kSteps = 0, kThirds = 1, kFourths = 2, kFifths = 3;
      constexpr double kC = 0, kCs = 1, kD = 2, kEb = 3, kE = 4, kF = 5, kG = 7, kAb = 8, kA = 9, kBb = 10,
                       kB = 11;
      std::vector<Preset> p;

      // Loops: the slab is held still, so a short length repeats exactly.
      p.push_back({"Init", "Loops", {}});
      p.push_back({"One Bar Riff", "Loops",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kLength, 1}, {kLock, 1}, {kDensity, 4}, {kSeed, 95}, {kGate, 0.7}}});
      p.push_back({"Two Bar Answer", "Loops",
                   {{kLength, 2}, {kLock, 1}, {kSeed, 42}, {kScale, kDorian}, {kRoot, kD}, {kDensity, 3}}});
      p.push_back({"Four Bar Phrase", "Loops", {{kLength, 4}, {kLock, 1}, {kSeed, 311}, {kScale, kMinor}, {kRoot, kA}}});
      p.push_back({"Runic March", "Loops",
                   {{kOrthography, kRunic}, {kLength, 4}, {kLock, 1}, {kSeed, 1066}, {kRate, k8}, {kDensity, 3},
                    {kScale, kMinor}, {kRoot, kG}, {kOctave, -1}, {kGate, 0.6}}});
      p.push_back({"Pentatonic Hook", "Loops",
                   {{kLength, 2}, {kLock, 1}, {kSeed, 808}, {kScale, kMajorPent}, {kRoot, kC}, {kDensity, 4},
                    {kGate, 0.6}, {kSwing, 0.15}}});
      p.push_back({"Halo Waltz", "Loops",
                   {{kOrthography, kHalo}, {kLength, 3}, {kLock, 1}, {kSeed, 3}, {kRate, k8T}, {kScale, kMajor},
                    {kRoot, kG}, {kDensity, 3}, {kGate, 1.1}, {kVelocity, 0.65}}});
      p.push_back({"Eight Bar Song", "Loops",
                   {{kLength, 8}, {kLock, 1}, {kSeed, 1979}, {kScale, kMixolydian}, {kRoot, kD}, {kLexicon, 6},
                    {kGrammar, 0.1}, {kDensity, 3}, {kGate, 0.8}}});

      // Bass: low, short and locked, for a groove that holds.
      p.push_back({"Circuit Bass", "Bass",
                   {{kOrthography, kCircuit}, {kLength, 2}, {kLock, 1}, {kSeed, 88}, {kDensity, 5}, {kOctave, -2},
                    {kSpread, kSteps}, {kScale, kMinorPent}, {kRoot, kE}, {kGate, 0.5}, {kVelocity, 0.9}}});
      p.push_back({"Root Pulse", "Bass",
                   {{kLength, 1}, {kLock, 1}, {kSeed, 140}, {kDensity, 6}, {kOctave, -2}, {kSpread, kSteps},
                    {kScale, kMinor}, {kRoot, kA}, {kGate, 0.35}, {kVelocity, 0.95}, {kLexicon, 4}}});
      p.push_back({"Walking Stone", "Bass",
                   {{kOrthography, kRunic}, {kLength, 4}, {kLock, 1}, {kSeed, 512}, {kDensity, 6}, {kOctave, -2}, {kSpread, kSteps},
                    {kScale, kDorian}, {kRoot, kG}, {kGate, 0.8}, {kSwing, 0.2}}});
      p.push_back({"Acid Glyphs", "Bass",
                   {{kOrthography, kCircuit}, {kLength, 2}, {kDrift, 0.1}, {kSeed, 303}, {kDensity, 6}, {kOctave, -1},
                    {kSpread, kSteps}, {kScale, kPhrygian}, {kRoot, kC}, {kGate, 0.3}, {kHumanize, 0.2}}});
      p.push_back({"Wedge Sub", "Bass",
                   {{kOrthography, kCuneiform}, {kLength, 4}, {kLock, 1}, {kSeed, 64}, {kRate, k8}, {kDensity, 2},
                    {kOctave, -3}, {kSpread, kSteps}, {kScale, kMinor}, {kRoot, kF}, {kGate, 1.3}, {kVelocity, 1}}});

      // Arps and Leads: fast lines over a wide spread, held or slowly changing.
      p.push_back({"Glass Arp", "Arps and Leads",
                   {{kOrthography, kHalo}, {kLength, 2}, {kLock, 1}, {kSeed, 1024}, {kRate, k16}, {kDensity, 6},
                    {kSpread, kFifths}, {kScale, kLydian}, {kRoot, kE}, {kGate, 0.45}}});
      p.push_back({"Triplet Spiral", "Arps and Leads",
                   {{kLength, 4}, {kDirection, kPingPong}, {kDrift, 0.08}, {kSeed, 33}, {kRate, k16T}, {kDensity, 5},
                    {kSpread, kFourths}, {kScale, kHarmonicMinor}, {kRoot, kD}, {kGate, 0.55}}});
      p.push_back({"Thirty-Second Run", "Arps and Leads",
                   {{kOrthography, kCircuit}, {kLength, 1}, {kLock, 1}, {kSeed, 131}, {kRate, k32}, {kDensity, 6},
                    {kSpread, kThirds}, {kScale, kMinor}, {kRoot, kB}, {kGate, 0.6}, {kVelocity, 0.7}}});
      p.push_back({"Scribe Solo", "Arps and Leads",
                   {{kLength, 8}, {kDrift, 0.3}, {kGrammar, 0.6}, {kLexicon, 32}, {kSeed, 1492}, {kRate, k8},
                    {kDensity, 3}, {kScale, kDorian}, {kRoot, kA}, {kGate, 1.2}, {kHumanize, 0.35}, {kSwing, 0.1}}});
      p.push_back({"Key Follow Arp", "Arps and Leads",
                   {{kFollow, 1}, {kLength, 2}, {kLock, 1}, {kSeed, 21}, {kDensity, 5}, {kSpread, kThirds},
                    {kScale, kMajor}, {kGate, 0.5}}});
      p.push_back({"Runic Fanfare", "Arps and Leads",
                   {{kOrthography, kRunic}, {kLength, 2}, {kLock, 1}, {kSeed, 1215}, {kRate, k8T}, {kDensity, 4},
                    {kSpread, kFifths}, {kScale, kMixolydian}, {kRoot, kBb}, {kGate, 0.75}, {kVelocity, 0.9}}});

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
      p.push_back({"Few Words", "Evolving",
                   {{kLength, 8}, {kDrift, 0.5}, {kLexicon, 4}, {kGrammar, 0.05}, {kSeed, 444}, {kScale, kMajorPent},
                    {kRoot, kEb}, {kDensity, 3}, {kGate, 0.7}}});
      p.push_back({"Wear and Mend", "Evolving",
                   {{kOrthography, kCuneiform}, {kLength, 8}, {kDrift, 0.2}, {kErode, 0.2}, {kSeed, 1700},
                    {kDensity, 5}, {kScale, kHarmonicMinor}, {kRoot, kE}, {kGate, 0.6}, {kHumanize, 0.2}}});
      p.push_back({"Second Glyph Onward", "Evolving",
                   {{kStart, 13}, {kLength, 12}, {kDrift, 0.12}, {kSeed, 1300}, {kScale, kMinor}, {kRoot, kC},
                    {kSpread, kFourths}, {kRate, k8}, {kGate, 1.0}}});

      // Rhythmic: gate, swing and humanize make the groove; pitch stays close.
      p.push_back({"Swing Stabs", "Rhythmic",
                   {{kLength, 2}, {kLock, 1}, {kSeed, 95}, {kDensity, 5}, {kSwing, 0.45}, {kGate, 0.2},
                    {kSpread, kSteps}, {kScale, kDorian}, {kRoot, kF}, {kVelocity, 0.9}}});
      p.push_back({"Morse Circuit", "Rhythmic",
                   {{kOrthography, kCircuit}, {kLength, 4}, {kLock, 1}, {kSeed, 1844}, {kDensity, 6}, {kGate, 0.15},
                    {kSpread, kSteps}, {kScale, kChromatic}, {kRoot, kA}, {kOctave, 1}, {kLexicon, 8}}});
      p.push_back({"Tumbling Dice", "Rhythmic",
                   {{kLength, 4}, {kDirection, kRandom}, {kLock, 1}, {kSeed, 6}, {kDensity, 6}, {kRate, k16T},
                    {kGate, 0.3}, {kHumanize, 0.6}, {kScale, kMinorPent}, {kRoot, kG}}});
      p.push_back({"Shuffle Talk", "Rhythmic",
                   {{kOrthography, kRunic}, {kLength, 4}, {kDrift, 0.15}, {kSeed, 2525}, {kDensity, 4}, {kSwing, 0.3},
                    {kGate, 0.4}, {kScale, kMixolydian}, {kRoot, kA}, {kHumanize, 0.25}}});
      p.push_back({"Backwards Clock", "Rhythmic",
                   {{kLength, 2}, {kDirection, kReverse}, {kLock, 1}, {kSeed, 1200}, {kDensity, 6}, {kGate, 0.25},
                    {kSpread, kThirds}, {kScale, kLocrian}, {kRoot, kB}, {kVelocity, 0.75}}});

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
      p.push_back({"Long Shadows", "Textures",
                   {{kRate, k4}, {kDensity, 2}, {kLength, 16}, {kDrift, 0.2}, {kGate, 2}, {kSpread, kFifths},
                    {kScale, kMinor}, {kRoot, kCs}, {kOctave, -1}, {kVelocity, 0.45}, {kSeed, 7000}}});
      p.push_back({"Starfield", "Textures",
                   {{kOrthography, kHalo}, {kRate, k16}, {kDensity, 2}, {kLength, 24}, {kDirection, kRandom},
                    {kDrift, 0.4}, {kGate, 1.5}, {kSpread, kFifths}, {kScale, kLydian}, {kRoot, kAb},
                    {kVelocity, 0.4}, {kHumanize, 0.7}, {kSeed, 9001}}});
      p.push_back({"Faded Inscription", "Textures",
                   {{kOrthography, kRunic}, {kRate, k8}, {kDensity, 4}, {kLength, 16}, {kDrift, 0}, {kErode, 0.5},
                    {kGate, 1.6}, {kSpread, kFourths}, {kScale, kDorian}, {kRoot, kE}, {kVelocity, 0.55},
                    {kSeed, 1400}}});

      // Listen: plays nothing until you teach it, then reads your playing back.
      p.push_back({"Listening Stone", "Listen",
                   {{kScale, kPhrygian}, {kRoot, kF}, {kMode, 1}, {kLength, 4}, {kLock, 1}, {kRate, k16}}});
      p.push_back({"Echo Tablet", "Listen",
                   {{kMode, 1}, {kLength, 2}, {kLock, 1}, {kRate, k8}, {kScale, kMajor}, {kRoot, kC}, {kGate, 0.8}}});
      p.push_back({"Drifting Memory", "Listen",
                   {{kMode, 1}, {kLength, 4}, {kDrift, 0.1}, {kErode, 0.1}, {kRate, k16}, {kScale, kMinor},
                    {kRoot, kA}, {kHumanize, 0.2}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace monolith

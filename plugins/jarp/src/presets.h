#pragma once

// JArp's presets: a factory bank compiled in, and the user's own saved through jclap::ui's preset
// files. A preset file holds the params; a factory preset also has a pattern, which loads with it.
// A user preset keeps the settings only, and the pattern stays with the project.
//
// A factory preset is written as what it changes from the defaults, then filled out with every other
// param at its default, so nothing from the previous preset carries over.

#include <jclap/ui/presets.h>

#include <string>
#include <vector>

#include "params.h"
#include "shared.h"

namespace jarp {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"JArp", 1, ".jarppreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

// A pattern written out: each note as {row, step, length in quarter steps} (rows and steps from 1,
// as the panel numbers them), and the lanes for the steps that differ.
struct LaneEdit {
   int step;
   Lane lane;
};
inline Pattern makePattern(std::initializer_list<Note> notes, std::initializer_list<LaneEdit> lanes = {}) {
   Pattern p;
   for (const Note &n : notes)
      p.add(n.row - 1, n.step - 1, n.length);
   for (const LaneEdit &e : lanes)
      p.lanes[e.step - 1] = e.lane;
   return p;
}

// The pattern a new instance starts with: up through four held notes and back.
inline Pattern defaultPattern() {
   return makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {1, 7, 4}, {2, 8, 4}});
}

struct FactoryPattern {
   const char *name;
   Pattern pattern;
};

inline const std::vector<FactoryPattern> &factoryPatterns() {
   static const std::vector<FactoryPattern> patterns = {
      {"Init", defaultPattern()},
      // The 1st and 5th of whatever is held, alternating (Degree rows).
      {"Root Fifth", makePattern({{1, 1, 4}, {5, 2, 4}, {1, 3, 4}, {5, 4, 4}, {1, 5, 4}, {5, 6, 4}, {1, 7, 4}, {5, 8, 4}})},
      // Broken thirds up the scale: 1 3 2 4 3 5 4 6 (Degree rows).
      {"Broken Thirds", makePattern({{1, 1, 4}, {3, 2, 4}, {2, 3, 4}, {4, 4, 4}, {3, 5, 4}, {5, 6, 4}, {4, 7, 4}, {6, 8, 4}})},
      // Row 1 held under the whole pattern, the rest climbing over it.
      {"Pedal Climb",
       makePattern({{1, 1, 64}, {2, 1, 1}, {3, 2, 4}, {4, 3, 3}, {2, 5, 4}, {3, 6, 2}, {4, 7, 6}, {2, 9, 2},
                    {3, 11, 8}, {2, 13, 4}, {4, 14, 4}, {3, 15, 2}, {4, 16, 4}},
                   {{1, {0, 1, 0, 12}}, {4, {0, 1, 0, -20}}, {5, {0, 1, 0, 8}}, {7, {1, 1, 0, 0}}, {8, {0, 1, 0, -12}},
                    {9, {0, 1, 0, 12}}, {11, {0, 3, 0, 0}}, {12, {0, 1, 0, -20}}, {13, {0, 1, 0, 8}},
                    {14, {0, 2, 0, 0}}, {16, {1, 1, 0, -12}}})},
      // Steps with P lit roll faster the harder each finger presses.
      {"Press Roll",
       makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {1, 5, 4}, {2, 6, 4}, {3, 7, 4}, {4, 8, 4}},
                   {{1, {0, 1, 0, 10}}, {2, {0, 1, 1, 0}}, {3, {0, 1, 0, -16}}, {4, {0, 2, 1, 0}},
                    {5, {0, 1, 0, 10}}, {6, {0, 1, 1, 0}}, {7, {0, 1, 0, -16}}, {8, {0, 2, 0, 0}}})},
      // Up through all eight rows, wrapping round the notes held, an octave up on the way back.
      {"Wrap Run", makePattern({{1, 1, 2}, {2, 2, 2}, {3, 3, 2}, {4, 4, 2}, {5, 5, 2}, {6, 6, 2}, {7, 7, 2}, {8, 8, 2}},
                               {{5, {1, 1, 0, 0}}, {6, {1, 1, 0, 0}}, {7, {1, 1, 0, 0}}, {8, {1, 1, 0, 0}}})},
      // Triplets: low, high, middle, with a roll to end the bar.
      {"Gallop", makePattern({{1, 1, 4}, {3, 2, 2}, {2, 3, 2}, {1, 4, 4}, {3, 5, 2}, {2, 6, 2}, {1, 7, 4}, {3, 8, 2},
                              {2, 9, 2}, {1, 10, 4}, {3, 11, 2}, {4, 12, 4}},
                             {{1, {0, 1, 0, 16}}, {4, {0, 1, 0, 8}}, {7, {0, 1, 0, 16}}, {10, {0, 1, 0, 8}},
                              {12, {0, 3, 0, 0}}})},
      // Chord stabs: every note at once, then a gap, with octave jumps.
      {"Stabs", makePattern({{1, 1, 2}, {2, 1, 2}, {3, 1, 2}, {4, 1, 2}, {1, 4, 1}, {2, 4, 1}, {3, 4, 1}, {4, 4, 1},
                             {1, 7, 2}, {2, 7, 2}, {3, 7, 2}, {4, 7, 2}},
                            {{4, {1, 1, 0, -20}}, {7, {0, 2, 0, 0}}})},
   };
   return patterns;
}

// The pattern of a factory preset, or null for one that has none (a user preset).
inline const Pattern *factoryPattern(const std::string &name) {
   for (const FactoryPattern &p : factoryPatterns())
      if (name == p.name)
         return &p.pattern;
   return nullptr;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      constexpr double k16T = 1, k16 = 2, k8 = 4;
      constexpr double kDegree = 1, kWrap = 1, kBar = 2;
      std::vector<Preset> p;
      p.push_back({"Init", "Basics", {}});
      p.push_back({"Root Fifth", "Basics", {{kRowsBy, kDegree}, {kSwing, 0.54}}});
      p.push_back({"Broken Thirds", "Basics", {{kRowsBy, kDegree}, {kRate, k16}}});
      p.push_back({"Pedal Climb", "Grooves", {{kLength, 16}, {kSwing, 0.54}, {kPressVelocity, 0.7}}});
      p.push_back({"Press Roll", "Pressure", {{kPressVelocity, 0.7}, {kPressRatchet, 0.6}, {kMaxHits, 4}, {kSwing, 0.54}}});
      p.push_back({"Wrap Run", "Basics", {{kEmptyRows, kWrap}, {kRate, k16}}});
      p.push_back({"Gallop", "Grooves", {{kRate, k16T}, {kLength, 12}, {kRestart, kBar}}});
      p.push_back({"Stabs", "Grooves", {{kRate, k8}, {kPressVelocity, 0.8}}});
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace jarp

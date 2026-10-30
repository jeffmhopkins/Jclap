#pragma once

// The program settings no CC reaches: the mod matrix routing, the filter type and character, the
// oscillator algorithms, the LFO waves and trigger modes and the lag source. The synth takes them as
// NRPN on the part's channel (CC 99 = NRPN >> 7, CC 98 = NRPN & 127, CC 6 = value), the way the
// Monstaguru editor sends its live edits, and stores them at fixed bytes of the program dump (see
// program_dump.h). Each is a stepped host parameter: value i is choice i.

#include <cstdint>
#include <string>
#include <vector>

#include "patches.h"

namespace mm2 {

enum class Choices { Source, Destination, LagSource, FilterType, FilterChar, Osc1Algo, OscAlgo, LfoWave, LfoMode };

struct Setting {
   int nrpn;
   const char *name;  // for the host
   int byte;          // in the 176-byte program dump
   Choices choices;
   int defaultValue;  // the synth's INIT program's
};

// CLAP ids are kSettingIdBase + NRPN, so they stay put if this list changes.
inline constexpr uint32_t kSettingIdBase = 2000;

inline constexpr Setting kSettings[] = {
   {27, "Osc1 Algorithm", 35, Choices::Osc1Algo, 6},
   {31, "Osc2 Algorithm", 39, Choices::OscAlgo, 6},
   {35, "Osc3 Algorithm", 43, Choices::OscAlgo, 6},
   {125, "Filter Type", 150, Choices::FilterType, 3},
   {126, "Filter Character", 151, Choices::FilterChar, 3},
   {61, "LFO1 Wave", 71, Choices::LfoWave, 0},
   {63, "LFO1 Trigger", 74, Choices::LfoMode, 3},
   {64, "LFO2 Wave", 75, Choices::LfoWave, 0},
   {66, "LFO2 Trigger", 78, Choices::LfoMode, 0},
   {67, "LFO3 Wave", 79, Choices::LfoWave, 0},
   {69, "LFO3 Trigger", 82, Choices::LfoMode, 0},
   {100, "Lag Source", 123, Choices::LagSource, 0},
   // Mod matrix slot k: source NRPN 70 + 3k at byte 83 + 4k, destination 71 + 3k at 84 + 4k.
   {70, "Mod 1 Source", 83, Choices::Source, 21},   {71, "Mod 1 Destination", 84, Choices::Destination, 1},
   {73, "Mod 2 Source", 87, Choices::Source, 0},    {74, "Mod 2 Destination", 88, Choices::Destination, 0},
   {76, "Mod 3 Source", 91, Choices::Source, 0},    {77, "Mod 3 Destination", 92, Choices::Destination, 0},
   {79, "Mod 4 Source", 95, Choices::Source, 0},    {80, "Mod 4 Destination", 96, Choices::Destination, 0},
   {82, "Mod 5 Source", 99, Choices::Source, 0},    {83, "Mod 5 Destination", 100, Choices::Destination, 0},
   {85, "Mod 6 Source", 103, Choices::Source, 0},   {86, "Mod 6 Destination", 104, Choices::Destination, 0},
   {88, "Mod 7 Source", 107, Choices::Source, 0},   {89, "Mod 7 Destination", 108, Choices::Destination, 0},
   {91, "Mod 8 Source", 111, Choices::Source, 0},   {92, "Mod 8 Destination", 112, Choices::Destination, 0},
   {94, "Mod 9 Source", 115, Choices::Source, 0},   {95, "Mod 9 Destination", 116, Choices::Destination, 0},
   {97, "Mod 10 Source", 119, Choices::Source, 0},  {98, "Mod 10 Destination", 120, Choices::Destination, 0},
};
inline constexpr int kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

inline constexpr uint32_t settingId(int nrpn) { return kSettingIdBase + static_cast<uint32_t>(nrpn); }
inline constexpr int sourceNrpn(int slot) { return 70 + 3 * slot; }
inline constexpr int destinationNrpn(int slot) { return 71 + 3 * slot; }

// Index into kSettings for an NRPN, or -1.
inline int indexOfSetting(int nrpn) {
   for (int i = 0; i < kSettingCount; ++i)
      if (kSettings[i].nrpn == nrpn)
         return i;
   return -1;
}
inline int indexOfSettingId(uint32_t id) {
   return id >= kSettingIdBase && id < kSettingIdBase + 256 ? indexOfSetting(static_cast<int>(id - kSettingIdBase)) : -1;
}

// The choices' names, as the synth's screen and the Monstaguru editor name them.
inline std::vector<std::string> choiceNames(Choices c) {
   static const char *const filterTypes[] = {"Lowpass 1", "Lowpass 2", "Lowpass 3", "Lowpass 4",
                                             "Highpass 2", "Bandpass 2", "Notch", "Phaser"};
   static const char *const filterChars[] = {"Soft", "Mild", "Hard", "Mean"};
   static const char *const algos[] = {"Sine", "Sine Full", "Sine Half", "Tri>Pulse", "Tri>Saw", "Tri>Square",
                                       "Saw", "Supersaw 1", "Supersaw 2", "Synced Saw", "Square>Pulse",
                                       "Synced Square"};
   static const char *const fm[] = {"FM Sine", "FM Triangle", "FM Saw", "FM Square"};
   static const char *const waves[] = {"Sine", "Triangle", "Saw", "Square", "S&H Bipolar", "Random Bipolar",
                                       "Attack A", "Attack B", "Attack C", "Decay A", "Decay B", "Decay C",
                                       "Att-Dec A", "Att-Dec B", "Att-Dec C", "Square 1>0", "Square 0>1",
                                       "S&H Unipolar", "Random Unipolar"};
   static const char *const modes[] = {"Voice", "Once", "Key", "Free"};
   std::vector<std::string> out;
   const auto add = [&](const char *const *names, int n) { out.insert(out.end(), names, names + n); };
   switch (c) {
   case Choices::Source:
      for (const ModName &m : kModSources)
         out.push_back(m.label);
      break;
   case Choices::LagSource: // the sources up to LFO3×Env3: no lag, mults or offset
      for (int i = 0; i < 36; ++i)
         out.push_back(kModSources[i].label);
      break;
   case Choices::Destination:
      for (const ModName &m : kModDestinations)
         out.push_back(m.label);
      break;
   case Choices::FilterType: add(filterTypes, 8); break;
   case Choices::FilterChar: add(filterChars, 4); break;
   case Choices::Osc1Algo: add(algos, 12); add(fm, 4); break;
   case Choices::OscAlgo:
      add(algos, 12);
      for (int i = 1; i <= 8; ++i)
         out.push_back("Wavetable " + std::to_string(i));
      break;
   case Choices::LfoWave: add(waves, 19); break;
   case Choices::LfoMode: add(modes, 4); break;
   }
   return out;
}

inline constexpr int choiceCount(Choices c) {
   switch (c) {
   case Choices::Source: return kModSourceCount;
   case Choices::Destination: return kModDestinationCount;
   case Choices::LagSource: return 36;
   case Choices::FilterType: return 8;
   case Choices::FilterChar: return 4;
   case Choices::Osc1Algo: return 16;
   case Choices::OscAlgo: return 20;
   case Choices::LfoWave: return 19;
   case Choices::LfoMode: return 4;
   }
   return 1;
}

} // namespace mm2

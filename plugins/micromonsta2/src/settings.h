#pragma once

// The program settings no CC reaches: the mod matrix routing, the oscillator algorithms, filter type
// and character, LFO waves, trigger modes and tempo sync, the lag and mult operators, the voice
// modes, bend range, effect modes, tempo, volume, the arpeggiator and the panel's knob assignments.
// The synth takes them as NRPN on the part's channel (CC 99 = NRPN >> 7, CC 98 = NRPN & 127, CC 6 =
// value, and CC 38 = the low 7 bits for 14-bit ones), the way the Monstaguru editor sends its live
// edits, and stores them at fixed bytes of the program dump (see program_dump.h). The NRPN numbers,
// ranges, bytes and names are the Monstaguru editor's. Each is a stepped host parameter whose value
// is what the synth stores: a choice's index, or a number in [min, max].

#include <cstdint>
#include <string>
#include <vector>

#include "patches.h"

namespace mm2 {

enum class Choices {
   Number, // no names: min..max, shown as value - offset
   Source,
   Destination,
   OpSource, // the lag and mult operators' sources: the matrix's up to LFO3×Env3
   FilterType,
   FilterChar,
   Osc1Algo,
   OscAlgo,
   LfoWave,
   LfoMode,
   LfoSync,   // tempo sync of an LFO's speed (NRPN value 127 + choice); 0 = free, the speed knob
   DelaySync, // the same for the delay time
   OnOff,
   Category,
   KnobAssign,
   VoiceMode,
   Unison,
   SpreadMode,
   ChorusMode,
   DelayMode,
   ArpStyle,
   ArpSpeed,
   ArpStep,
};

enum class Group { Oscillators, Filter, Envelopes, Lfo, ModMatrix, Voice, Effects, Arp, Program };

struct Setting {
   int nrpn;
   const char *name; // for the host
   int byte;         // in the 176-byte program dump (the high byte's, for a wide one)
   Choices choices;
   Group group;
   int defaultValue; // the synth's INIT program's
   int min = 0, max = 0; // Number only; the others run 0 to their choice count - 1
   int offset = 0;       // Number only: shown as value - offset
   bool wide = false;    // 14-bit: two bytes in the dump, and CC 38 after CC 6
};

// CLAP ids are kSettingIdBase + NRPN, so they stay put if this list changes.
inline constexpr uint32_t kSettingIdBase = 2000;

// clang-format off
inline constexpr Setting kSettings[] = {
   {27, "Osc1 Algorithm", 35, Choices::Osc1Algo, Group::Oscillators, 6},
   {31, "Osc2 Algorithm", 39, Choices::OscAlgo, Group::Oscillators, 6},
   {35, "Osc3 Algorithm", 43, Choices::OscAlgo, Group::Oscillators, 6},
   {124, "Osc Phase Reset", 149, Choices::OnOff, Group::Oscillators, 0},
   {125, "Filter Type", 150, Choices::FilterType, Group::Filter, 3},
   {126, "Filter Character", 151, Choices::FilterChar, Group::Filter, 3},
   {120, "Env1 Reset to Zero", 145, Choices::OnOff, Group::Envelopes, 0},
   {121, "Env2 Reset to Zero", 146, Choices::OnOff, Group::Envelopes, 0},
   {122, "Env3 Reset to Zero", 147, Choices::OnOff, Group::Envelopes, 0},
   {61, "LFO1 Wave", 71, Choices::LfoWave, Group::Lfo, 0},
   {62, "LFO1 Sync", 72, Choices::LfoSync, Group::Lfo, 0, 0, 0, 0, true},
   {63, "LFO1 Trigger", 74, Choices::LfoMode, Group::Lfo, 3},
   {64, "LFO2 Wave", 75, Choices::LfoWave, Group::Lfo, 0},
   {65, "LFO2 Sync", 76, Choices::LfoSync, Group::Lfo, 0, 0, 0, 0, true},
   {66, "LFO2 Trigger", 78, Choices::LfoMode, Group::Lfo, 0},
   {67, "LFO3 Wave", 79, Choices::LfoWave, Group::Lfo, 0},
   {68, "LFO3 Sync", 80, Choices::LfoSync, Group::Lfo, 0, 0, 0, 0, true},
   {69, "LFO3 Trigger", 82, Choices::LfoMode, Group::Lfo, 0},
   {100, "Lag Source", 123, Choices::OpSource, Group::ModMatrix, 0},
   {102, "Mult 1 Source A", 125, Choices::OpSource, Group::ModMatrix, 0},
   {103, "Mult 1 Source B", 126, Choices::OpSource, Group::ModMatrix, 0},
   {104, "Mult 2 Source A", 127, Choices::OpSource, Group::ModMatrix, 0},
   {105, "Mult 2 Source B", 128, Choices::OpSource, Group::ModMatrix, 0},
   // Mod matrix slot k: source NRPN 70 + 3k at byte 83 + 4k, destination 71 + 3k at 84 + 4k.
   {70, "Mod 1 Source", 83, Choices::Source, Group::ModMatrix, 21},  {71, "Mod 1 Destination", 84, Choices::Destination, Group::ModMatrix, 1},
   {73, "Mod 2 Source", 87, Choices::Source, Group::ModMatrix, 0},   {74, "Mod 2 Destination", 88, Choices::Destination, Group::ModMatrix, 0},
   {76, "Mod 3 Source", 91, Choices::Source, Group::ModMatrix, 0},   {77, "Mod 3 Destination", 92, Choices::Destination, Group::ModMatrix, 0},
   {79, "Mod 4 Source", 95, Choices::Source, Group::ModMatrix, 0},   {80, "Mod 4 Destination", 96, Choices::Destination, Group::ModMatrix, 0},
   {82, "Mod 5 Source", 99, Choices::Source, Group::ModMatrix, 0},   {83, "Mod 5 Destination", 100, Choices::Destination, Group::ModMatrix, 0},
   {85, "Mod 6 Source", 103, Choices::Source, Group::ModMatrix, 0},  {86, "Mod 6 Destination", 104, Choices::Destination, Group::ModMatrix, 0},
   {88, "Mod 7 Source", 107, Choices::Source, Group::ModMatrix, 0},  {89, "Mod 7 Destination", 108, Choices::Destination, Group::ModMatrix, 0},
   {91, "Mod 8 Source", 111, Choices::Source, Group::ModMatrix, 0},  {92, "Mod 8 Destination", 112, Choices::Destination, Group::ModMatrix, 0},
   {94, "Mod 9 Source", 115, Choices::Source, Group::ModMatrix, 0},  {95, "Mod 9 Destination", 116, Choices::Destination, Group::ModMatrix, 0},
   {97, "Mod 10 Source", 119, Choices::Source, Group::ModMatrix, 0}, {98, "Mod 10 Destination", 120, Choices::Destination, Group::ModMatrix, 0},
   {117, "Voice Mode", 142, Choices::VoiceMode, Group::Voice, 0},
   {118, "Unison", 143, Choices::Unison, Group::Voice, 0},
   {119, "Pan Spread Mode", 144, Choices::SpreadMode, Group::Voice, 0},
   {23, "Bend Down", 31, Choices::Number, Group::Voice, 62, 52, 64, 64},
   {24, "Bend Up", 32, Choices::Number, Group::Voice, 2, 0, 12},
   {127, "Chorus Mode", 152, Choices::ChorusMode, Group::Effects, 0},
   {128, "Delay Mode", 153, Choices::DelayMode, Group::Effects, 3},
   {108, "Delay Sync", 132, Choices::DelaySync, Group::Effects, 0, 0, 0, 0, true},
   {130, "Arp On", 155, Choices::OnOff, Group::Arp, 0},
   {131, "Arp Latch", 156, Choices::OnOff, Group::Arp, 0},
   {114, "Arp Style", 139, Choices::ArpStyle, Group::Arp, 0},
   {116, "Arp Speed", 141, Choices::ArpSpeed, Group::Arp, 7},
   {115, "Arp Gate", 140, Choices::Number, Group::Arp, 64, 0, 127},
   {132, "Arp Octaves", 157, Choices::Number, Group::Arp, 0, 0, 3, -1},
   {133, "Arp Length", 158, Choices::Number, Group::Arp, 15, 0, 15, -1},
   {134, "Arp Step 1", 159, Choices::ArpStep, Group::Arp, 0},   {135, "Arp Step 2", 160, Choices::ArpStep, Group::Arp, 0},
   {136, "Arp Step 3", 161, Choices::ArpStep, Group::Arp, 0},   {137, "Arp Step 4", 162, Choices::ArpStep, Group::Arp, 0},
   {138, "Arp Step 5", 163, Choices::ArpStep, Group::Arp, 0},   {139, "Arp Step 6", 164, Choices::ArpStep, Group::Arp, 0},
   {140, "Arp Step 7", 165, Choices::ArpStep, Group::Arp, 0},   {141, "Arp Step 8", 166, Choices::ArpStep, Group::Arp, 0},
   {142, "Arp Step 9", 167, Choices::ArpStep, Group::Arp, 0},   {143, "Arp Step 10", 168, Choices::ArpStep, Group::Arp, 0},
   {144, "Arp Step 11", 169, Choices::ArpStep, Group::Arp, 0},  {145, "Arp Step 12", 170, Choices::ArpStep, Group::Arp, 0},
   {146, "Arp Step 13", 171, Choices::ArpStep, Group::Arp, 0},  {147, "Arp Step 14", 172, Choices::ArpStep, Group::Arp, 0},
   {148, "Arp Step 15", 173, Choices::ArpStep, Group::Arp, 0},  {149, "Arp Step 16", 174, Choices::ArpStep, Group::Arp, 0},
   {18, "Tempo", 25, Choices::Number, Group::Program, 80, 0, 255, -40, true},
   {17, "Program Volume", 24, Choices::Number, Group::Program, 67, 40, 88, 64},
   {9, "Category", 16, Choices::Category, Group::Program, 13},
   {10, "F Knob", 17, Choices::KnobAssign, Group::Program, 0},
   {11, "Q Knob", 18, Choices::KnobAssign, Group::Program, 0},
   {12, "M Knob", 19, Choices::KnobAssign, Group::Program, 0},
   {13, "Encoder 1", 20, Choices::KnobAssign, Group::Program, 0},
   {14, "Encoder 2", 21, Choices::KnobAssign, Group::Program, 0},
   {15, "Encoder 3", 22, Choices::KnobAssign, Group::Program, 0},
   {16, "Encoder 4", 23, Choices::KnobAssign, Group::Program, 0},
};
// clang-format on
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

inline bool isRouting(const Setting &s) { return s.choices == Choices::Source || s.choices == Choices::Destination; }
// A tempo sync choice is sent and stored as 127 + the choice: above the free speeds (0-127), which
// the CC knob sets.
inline bool isSync(const Setting &s) { return s.choices == Choices::LfoSync || s.choices == Choices::DelaySync; }
// The CC knob whose speed a sync setting overrides.
inline int syncedCc(const Setting &s) {
   switch (s.nrpn) {
   case 62: return 76;
   case 65: return 77;
   case 68: return 78;
   case 108: return 26;
   default: return -1;
   }
}

// The choices' names, as the synth's screen and the Monstaguru editor name them. Empty for Number.
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
   static const char *const lfoSync[] = {"Free", "32/1", "24/1", "16/1", "12/1", "8/1", "6/1", "4/1", "3/1", "2/1",
                                         "3/2", "1/1", "2d", "1t", "1/2", "4d", "2t", "1/4", "8d", "4t", "1/8",
                                         "16d", "8t", "1/16", "32d", "16t", "1/32", "64d", "32t", "1/64", "64t"};
   static const char *const delaySync[] = {"Free", "1/16", "1/8t", "1/8", "1/4t", "1/8d", "1/4"};
   static const char *const onOff[] = {"Off", "On"};
   static const char *const categories[] = {"Bass", "Lead", "Pad", "Keys", "Organ", "Strings", "Brass", "Percussion",
                                            "Drone", "Noise", "SFX", "Arp", "Misc", "User 1", "User 2", "User 3"};
   static const char *const knobs[] = {
      "System", "Mod Knob", "Tempo", "Volume", "Pan", "Glide", "Shape 1", "Tune 1", "Shape 2", "Tune 2",
      "Shape 3", "Tune 3", "Mix 1", "Mix 2", "Mix 3", "Noise", "Cutoff", "Resonance", "Filter Env", "Filter FM",
      "Drive", "Attack 1", "Decay 1", "Sustain 1", "Release 1", "Attack 2", "Decay 2", "Sustain 2", "Release 2",
      "Attack 3", "Decay 3", "Sustain 3", "Release 3", "LFO 1 Speed", "LFO 2 Speed", "LFO 3 Speed", "Matrix 1",
      "Matrix 2", "Matrix 3", "Matrix 4", "Matrix 5", "Matrix 6", "Matrix 7", "Matrix 8", "Matrix 9", "Matrix 10",
      "Lag", "Delay Time", "Delay Feedb", "Delay Send", "Rev Decay", "Rev Mod", "Rev Send", "Arp Enable",
      "Arp Latch", "Arp Gate", "Arp Octaves", "Arp Speed"};
   static const char *const voiceModes[] = {"Modern", "Vintage", "Mono", "Legato"};
   static const char *const unison[] = {"Off", "2", "3", "6"};
   static const char *const spread[] = {"Diffused", "Balanced"};
   static const char *const chorus[] = {"Off", "Type 1", "Type 2", "Type 1+2"};
   static const char *const delay[] = {"Clean", "Lowpass 1", "Lowpass 2", "Modulated 1", "Modulated 2"};
   static const char *const arpStyles[] = {"Up", "3 Up", "Down", "3 Down", "Up/Down", "Random", "As Played"};
   static const char *const arpSpeeds[] = {"1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32"};
   static const char *const steps[] = {"Auto", "Off", "Tie", "< -1", "1st", "2nd", "3rd", "4th", "5th", "6th",
                                       "> +1", "Chord"};
   std::vector<std::string> out;
   const auto add = [&](const char *const *names, size_t n) { out.insert(out.end(), names, names + n); };
#define MM2_ADD(list) add(list, sizeof(list) / sizeof(list[0]))
   switch (c) {
   case Choices::Number: break;
   case Choices::Source:
      for (const ModName &m : kModSources)
         out.push_back(m.label);
      break;
   case Choices::OpSource: // up to LFO3×Env3: no lag, mults or offset
      for (int i = 0; i < 36; ++i)
         out.push_back(kModSources[i].label);
      break;
   case Choices::Destination:
      for (const ModName &m : kModDestinations)
         out.push_back(m.label);
      break;
   case Choices::FilterType: MM2_ADD(filterTypes); break;
   case Choices::FilterChar: MM2_ADD(filterChars); break;
   case Choices::Osc1Algo: MM2_ADD(algos); MM2_ADD(fm); break;
   case Choices::OscAlgo:
      MM2_ADD(algos);
      for (int i = 1; i <= 8; ++i)
         out.push_back("Wavetable " + std::to_string(i));
      break;
   case Choices::LfoWave: MM2_ADD(waves); break;
   case Choices::LfoMode: MM2_ADD(modes); break;
   case Choices::LfoSync: MM2_ADD(lfoSync); break;
   case Choices::DelaySync: MM2_ADD(delaySync); break;
   case Choices::OnOff: MM2_ADD(onOff); break;
   case Choices::Category: MM2_ADD(categories); break;
   case Choices::KnobAssign: MM2_ADD(knobs); break;
   case Choices::VoiceMode: MM2_ADD(voiceModes); break;
   case Choices::Unison: MM2_ADD(unison); break;
   case Choices::SpreadMode: MM2_ADD(spread); break;
   case Choices::ChorusMode: MM2_ADD(chorus); break;
   case Choices::DelayMode: MM2_ADD(delay); break;
   case Choices::ArpStyle: MM2_ADD(arpStyles); break;
   case Choices::ArpSpeed: MM2_ADD(arpSpeeds); break;
   case Choices::ArpStep: MM2_ADD(steps); break;
   }
#undef MM2_ADD
   return out;
}

inline constexpr int choiceCount(Choices c) {
   switch (c) {
   case Choices::Number: return 0;
   case Choices::Source: return kModSourceCount;
   case Choices::Destination: return kModDestinationCount;
   case Choices::OpSource: return 36;
   case Choices::FilterType: return 8;
   case Choices::FilterChar: return 4;
   case Choices::Osc1Algo: return 16;
   case Choices::OscAlgo: return 20;
   case Choices::LfoWave: return 19;
   case Choices::LfoMode: return 4;
   case Choices::LfoSync: return 31;
   case Choices::DelaySync: return 7;
   case Choices::OnOff: return 2;
   case Choices::Category: return 16;
   case Choices::KnobAssign: return 58;
   case Choices::VoiceMode: return 4;
   case Choices::Unison: return 4;
   case Choices::SpreadMode: return 2;
   case Choices::ChorusMode: return 4;
   case Choices::DelayMode: return 5;
   case Choices::ArpStyle: return 7;
   case Choices::ArpSpeed: return 10;
   case Choices::ArpStep: return 12;
   }
   return 1;
}

inline constexpr int settingMin(const Setting &s) { return s.choices == Choices::Number ? s.min : 0; }
inline constexpr int settingMax(const Setting &s) {
   return s.choices == Choices::Number ? s.max : choiceCount(s.choices) - 1;
}

// What the panel and the host show for value v: a choice's name, or the number less its offset.
inline std::string settingText(const Setting &s, int v) {
   if (s.choices == Choices::Number)
      return std::to_string(v - s.offset);
   const std::vector<std::string> names = choiceNames(s.choices);
   return v >= 0 && v < static_cast<int>(names.size()) ? names[v] : std::to_string(v);
}

// The value the synth gets (and stores) for setting value v.
inline int nrpnValue(const Setting &s, int v) { return isSync(s) && v > 0 ? 127 + v : v; }
// A setting's value from what the synth stores: for a sync, 0 (free) when the speed is a free one.
inline int fromStored(const Setting &s, int stored) {
   if (isSync(s))
      return stored > 127 && stored - 127 <= settingMax(s) ? stored - 127 : 0;
   return stored < settingMin(s) ? settingMin(s) : stored > settingMax(s) ? settingMax(s) : stored;
}

} // namespace mm2

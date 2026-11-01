#pragma once

// Presets stored on the MatrixBrute itself. Picking one sends Bank Select and Program Change so the
// synth loads it, and moves every knob to that preset's values without sending them (the synth
// already has them).

#include <jclap/ui/presets.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cc_map.h"

namespace mb {

struct HardwarePatch {
   int preset = 0; // 0-255: bank A-P times 16, plus slot 1-16 minus 1
   std::string name;
   // {cc, 0-127} for every CC the preset stores. The mod wheel and the sequencer's rate, swing and
   // gate aren't part of a preset, so they're left alone.
   std::vector<std::pair<int, double>> values;
   // {indicator id, setting} for the buttons that aren't on MIDI (see SWITCHES in
   // bitwig/MatrixBrute.control.js). The ones left out stay unknown.
   std::vector<std::pair<std::string, int>> switches;
   // The routed mod matrix cells, {source row 0-15, destination 0-27}: the 12 fixed columns, then
   // the user banks (columns 13-16) of four routes each. Empty if the read didn't include them.
   std::vector<std::pair<int, int>> matrix;
   bool hasMatrix = false;
   // LFO 1's and LFO 2's custom shapes, 16 steps each from -1 to 1. Empty if the read didn't
   // include them.
   std::vector<float> customLfo[2];
};

inline constexpr int kPresetCount = 256;

// The synth's 256 presets are 16 banks (A-P) of 16. Over MIDI, Bank Select (CC 0) 0-15 picks the
// bank and Program Change 0-15 the preset in it; a Program Change above 15 does nothing. The manual
// doesn't say so; it was checked on a real unit (CC 0 = 1 then PC 5 loads B06). The synth only
// takes Program Changes with its Program Change setting on (MIDI Control Center, Device Settings).
inline int bankSelectOf(int preset) { return preset / 16; }
inline int programChangeOf(int preset) { return preset % 16; }

// "A01" to "P16", as the synth's display shows it.
inline std::string presetCode(int preset) {
   char code[8];
   std::snprintf(code, sizeof(code), "%c%02d", 'A' + (preset / 16) % 16, preset % 16 + 1);
   return code;
}

// "A01 Juggernaut": unique in the bank, and what the preset bar shows.
inline std::string displayName(const HardwarePatch &p) { return presetCode(p.preset) + " " + p.name; }

// The presets stored on the synth: every named one from a full dump of a real MatrixBrute (read the
// way MIDI Control Center reads them), decoded by tools/mb_bank.py. That's the factory bank plus
// whatever its owner saved over it. Arturia hasn't published the preset format, so the values come
// from that dump and calibration reads, not from a document.
inline const std::vector<HardwarePatch> &factoryBank() {
   static const std::vector<HardwarePatch> bank = {
#include "factory_bank.inc"
   };
   return bank;
}

inline const HardwarePatch *findPatch(const std::vector<HardwarePatch> &patches, const std::string &name) {
   for (const HardwarePatch &p : patches)
      if (displayName(p) == name)
         return &p;
   return nullptr;
}

inline const HardwarePatch *findFactoryPatch(const std::string &name) { return findPatch(factoryBank(), name); }

// User presets are the panel's knobs saved to a file. They don't hold a synth preset: loading one
// sends its CCs to whatever the synth has loaded.
inline const jclap::ui::PresetFormat kPresetFormat{"MatrixBrute CC", 1, ".mbpreset", 128};

// One per param id below kPresetFormat.paramCount: each knob's default, 0 for ids that aren't CCs.
inline std::vector<double> presetDefaults() {
   std::vector<double> d(kPresetFormat.paramCount, 0.0);
   for (const CcParam &p : kCcParams)
      d[p.cc] = defaultValue(p);
   return d;
}

// Synth presets for the preset bar, filed by bank.
inline std::vector<jclap::ui::Preset> toPresets(const std::vector<HardwarePatch> &patches) {
   std::vector<jclap::ui::Preset> out;
   for (const HardwarePatch &p : patches) {
      jclap::ui::Preset preset{displayName(p), std::string("Bank ") + static_cast<char>('A' + p.preset / 16), {}};
      for (auto [cc, v] : p.values)
         preset.values.emplace_back(static_cast<clap_id>(cc), v);
      out.push_back(std::move(preset));
   }
   return out;
}

inline const std::vector<jclap::ui::Preset> &factoryPresets() {
   static const std::vector<jclap::ui::Preset> presets = toPresets(factoryBank());
   return presets;
}

} // namespace mb

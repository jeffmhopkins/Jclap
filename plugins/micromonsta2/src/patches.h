#pragma once

// Programs stored on the MicroMonsta 2 itself. Picking one sends Bank Select and Program Change so
// the synth loads it, and moves every knob to that program's values without sending them (the synth
// already has them). A program also says where its 10 mod matrix slots are routed, which CCs can't
// change, so the panel labels the mod matrix knobs with it.
//
// The mod source and destination names are the synth's own display names, from "MicroMonsta 2 User
// manual v2.8" sections 6.8.4 and 6.8.5, in the synth's order.

#include <jclap/ui/presets.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cc_map.h"

namespace mm2 {

inline constexpr const char *kModSources[] = {
   "off",      "note",     "note+bnd", "velocity", "aftertch", "modwheel", "mod knob", "pitchbnd",
   "random1",  "rnd1*mod", "random2",  "rnd2*knb", "env1",     "env1*vel", "env2",     "env2*vel",
   "env3",     "env3*vel", "lfo1",     "lfo1*vel", "lfo1*aft", "lfo1*whl", "lfo1*knb", "lfo1*en1",
   "lfo2",     "lfo2*vel", "lfo2*aft", "lfo2*whl", "lfo2*knb", "lfo2*en2", "lfo3",     "lfo3*vel",
   "lfo3*aft", "lfo3*whl", "lfo3*knb", "lfo3*env3", "lag",      "mult1",    "mult2",    "offset",
};
inline constexpr int kModSourceCount = sizeof(kModSources) / sizeof(kModSources[0]);

inline constexpr const char *kModDestinations[] = {
   "off",  "ptch", "tun1", "tun2", "tun3", "shp1", "shp2", "shp3", "mix1", "mix2", "mix3", "xnze",
   "cuto", "reso", "f.fm", "driv", "glid", "vca",  "pan",  "atk1", "atk2", "atk3", "atks", "dcy1",
   "dcy2", "dcy3", "dcys", "rel1", "rel2", "rel3", "rels", "lfo1", "lfo2", "lfo3", "lfos",
};
inline constexpr int kModDestinationCount = sizeof(kModDestinations) / sizeof(kModDestinations[0]);

inline constexpr int kModSlots = 10;

// One mod matrix slot's routing, as indexes into kModSources and kModDestinations. CC 52 + slot
// sets its level.
struct ModRoute {
   uint8_t source = 0;
   uint8_t destination = 0;

   bool operator==(const ModRoute &o) const { return source == o.source && destination == o.destination; }
};

inline const char *sourceName(const ModRoute &r) { return r.source < kModSourceCount ? kModSources[r.source] : "?"; }
inline const char *destinationName(const ModRoute &r) {
   return r.destination < kModDestinationCount ? kModDestinations[r.destination] : "?";
}

struct HardwarePatch {
   int program = 1; // 1-300, as the synth shows it (P001-P300)
   std::string name;
   std::string category;
   // {cc, 0-127} for every sound CC the program sets. The header knobs (mod wheel, mod knob,
   // volume) aren't part of a program and are left alone.
   std::vector<std::pair<int, int>> values;
   ModRoute routes[kModSlots];
};

inline constexpr int kProgramCount = 300;

// The synth has 300 program slots in banks of 100: Bank Select LSB (CC 32) 0-2, then Program Change
// 0-99. The manual doesn't say so; it was checked on a real unit (CC 32 = 1, program 5 loads P106).
inline int bankOf(int program) { return (program - 1) / 100; }
inline int programChangeOf(int program) { return (program - 1) % 100; }

// "P012 OctaBass": unique in the bank, and what the preset bar shows.
inline std::string displayName(const HardwarePatch &p) {
   char num[8];
   std::snprintf(num, sizeof(num), "P%03d", p.program);
   return std::string(num) + " " + p.name;
}

// The programs stored on the synth: every named program (P001-P300, skipping "INIT") from a full dump
// (UTILITY U02) of a real MicroMonsta 2, decoded by tools/mm2_bank.py. That's the factory bank plus
// whatever its owner saved over it. Audiothingies hasn't published the bank or the SysEx program
// format, so the values come from that dump, not from a document.
inline const std::vector<HardwarePatch> &factoryBank() {
   static const std::vector<HardwarePatch> bank = {
#include "factory_bank.inc"
   };
   return bank;
}

inline const HardwarePatch *findFactoryPatch(const std::string &name) {
   for (const HardwarePatch &p : factoryBank())
      if (displayName(p) == name)
         return &p;
   return nullptr;
}

// User presets are the panel's knobs saved to a file. They don't hold a program or its routing:
// loading one sends its CCs to whatever program the synth has.
inline const jclap::ui::PresetFormat kPresetFormat{"MicroMonsta 2 CC", 1, ".mm2preset", 128};

// One per param id below kPresetFormat.paramCount: each knob's default, 0 for ids that aren't CCs.
inline std::vector<double> presetDefaults() {
   std::vector<double> d(kPresetFormat.paramCount, 0.0);
   for (const CcParam &p : kCcParams)
      d[p.cc] = defaultValue(p);
   return d;
}

// The factory bank as presets for the preset bar.
inline const std::vector<jclap::ui::Preset> &factoryPresets() {
   static const std::vector<jclap::ui::Preset> presets = [] {
      std::vector<jclap::ui::Preset> out;
      for (const HardwarePatch &p : factoryBank()) {
         jclap::ui::Preset preset{displayName(p), p.category, {}};
         for (auto [cc, v] : p.values)
            preset.values.emplace_back(static_cast<clap_id>(cc), v);
         out.push_back(std::move(preset));
      }
      return out;
   }();
   return presets;
}

} // namespace mm2

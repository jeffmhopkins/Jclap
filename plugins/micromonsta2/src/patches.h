#pragma once

// Programs stored on the MicroMonsta 2 itself. Picking one sends Bank Select and Program Change so
// the synth loads it, and moves every knob to that program's values without sending them (the synth
// already has them). A program also says where its 10 mod matrix slots are routed, which CCs can't
// change, so the panel labels the mod matrix knobs with it.
//
// The mod source and destination names are the synth's own display names, from "MicroMonsta 2 User
// manual v2.8" sections 6.8.4 and 6.8.5, in the synth's order.

#include <jclap/ui/presets.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "cc_map.h"

namespace mm2 {

// A mod source or destination: the synth's own display name, and plain English for the panel and the
// host. The meanings are the manual's own (sections 6.8.4 and 6.8.5).
struct ModName {
   const char *code;  // as the synth's screen shows it
   const char *label; // short, for the panel's captions and the host's names
   const char *text;  // for a sentence: "<source> modulates <destination>"
   const char *knob = nullptr; // shorter still, over a mod matrix knob (destinations only)
};

inline constexpr ModName kModSources[] = {
   {"off", "Off", "nothing"},
   {"note", "Note", "the note played"},
   {"note+bnd", "Note+Bend", "the note played plus pitch bend"},
   {"velocity", "Velocity", "velocity"},
   {"aftertch", "Aftertouch", "aftertouch"},
   {"modwheel", "Mod Wheel", "the mod wheel"},
   {"mod knob", "Mod Knob", "the mod knob (M)"},
   {"pitchbnd", "Pitch Bend", "the pitch bend lever"},
   {"random1", "Random 1", "random value 1"},
   {"rnd1*mod", "Rnd1×Wheel", "random value 1 scaled by the mod wheel"},
   {"random2", "Random 2", "random value 2"},
   {"rnd2*knb", "Rnd2×Knob", "random value 2 scaled by the mod knob"},
   {"env1", "Env 1", "envelope 1"},
   {"env1*vel", "Env1×Velo", "envelope 1 scaled by velocity"},
   {"env2", "Env 2", "envelope 2"},
   {"env2*vel", "Env2×Velo", "envelope 2 scaled by velocity"},
   {"env3", "Env 3", "envelope 3"},
   {"env3*vel", "Env3×Velo", "envelope 3 scaled by velocity"},
   {"lfo1", "LFO 1", "LFO 1"},
   {"lfo1*vel", "LFO1×Velo", "LFO 1 scaled by velocity"},
   {"lfo1*aft", "LFO1×Touch", "LFO 1 scaled by aftertouch"},
   {"lfo1*whl", "LFO1×Wheel", "LFO 1 scaled by the mod wheel"},
   {"lfo1*knb", "LFO1×Knob", "LFO 1 scaled by the mod knob"},
   {"lfo1*en1", "LFO1×Env1", "LFO 1 scaled by envelope 1"},
   {"lfo2", "LFO 2", "LFO 2"},
   {"lfo2*vel", "LFO2×Velo", "LFO 2 scaled by velocity"},
   {"lfo2*aft", "LFO2×Touch", "LFO 2 scaled by aftertouch"},
   {"lfo2*whl", "LFO2×Wheel", "LFO 2 scaled by the mod wheel"},
   {"lfo2*knb", "LFO2×Knob", "LFO 2 scaled by the mod knob"},
   {"lfo2*en2", "LFO2×Env2", "LFO 2 scaled by envelope 2"},
   {"lfo3", "LFO 3", "LFO 3"},
   {"lfo3*vel", "LFO3×Velo", "LFO 3 scaled by velocity"},
   {"lfo3*aft", "LFO3×Touch", "LFO 3 scaled by aftertouch"},
   {"lfo3*whl", "LFO3×Wheel", "LFO 3 scaled by the mod wheel"},
   {"lfo3*knb", "LFO3×Knob", "LFO 3 scaled by the mod knob"},
   {"lfo3*env3", "LFO3×Env3", "LFO 3 scaled by envelope 3"},
   {"lag", "Lag", "the lag operator (a smoothed source)"},
   {"mult1", "Mult 1", "multiply operator 1 (one source times another)"},
   {"mult2", "Mult 2", "multiply operator 2 (one source times another)"},
   {"offset", "Offset", "a fixed offset"},
};
inline constexpr int kModSourceCount = sizeof(kModSources) / sizeof(kModSources[0]);

inline constexpr ModName kModDestinations[] = {
   {"off", "Off", "nothing"},
   {"ptch", "Pitch", "the pitch of all oscillators"},
   {"tun1", "Osc1 Pitch", "the pitch of oscillator 1"},
   {"tun2", "Osc2 Pitch", "the pitch of oscillator 2"},
   {"tun3", "Osc3 Pitch", "the pitch of oscillator 3"},
   {"shp1", "Osc1 Shape", "the shape of oscillator 1"},
   {"shp2", "Osc2 Shape", "the shape of oscillator 2"},
   {"shp3", "Osc3 Shape", "the shape of oscillator 3"},
   {"mix1", "Osc1 Level", "the level of oscillator 1"},
   {"mix2", "Osc2 Level", "the level of oscillator 2"},
   {"mix3", "Osc3 Level", "the level of oscillator 3"},
   {"xnze", "Noise", "the white noise level"},
   {"cuto", "Cutoff", "the filter cutoff"},
   {"reso", "Resonance", "the filter resonance"},
   {"f.fm", "Filter FM", "the filter FM amount"},
   {"driv", "Drive", "the voice drive"},
   {"glid", "Glide", "the glide rate"},
   {"vca", "Volume", "the voice volume (it can only turn it down)"},
   {"pan", "Pan", "the voice pan"},
   {"atk1", "Env1 Attack", "envelope 1's attack time", "E1 ATTACK"},
   {"atk2", "Env2 Attack", "envelope 2's attack time", "E2 ATTACK"},
   {"atk3", "Env3 Attack", "envelope 3's attack time", "E3 ATTACK"},
   {"atks", "All Attacks", "the attack time of all three envelopes", "ATTACKS"},
   {"dcy1", "Env1 Decay", "envelope 1's decay time", "E1 DECAY"},
   {"dcy2", "Env2 Decay", "envelope 2's decay time", "E2 DECAY"},
   {"dcy3", "Env3 Decay", "envelope 3's decay time", "E3 DECAY"},
   {"dcys", "All Decays", "the decay time of all three envelopes", "DECAYS"},
   {"rel1", "Env1 Release", "envelope 1's release time", "E1 RELEASE"},
   {"rel2", "Env2 Release", "envelope 2's release time", "E2 RELEASE"},
   {"rel3", "Env3 Release", "envelope 3's release time", "E3 RELEASE"},
   {"rels", "All Releases", "the release time of all three envelopes", "RELEASES"},
   {"lfo1", "LFO1 Speed", "LFO 1's speed"},
   {"lfo2", "LFO2 Speed", "LFO 2's speed"},
   {"lfo3", "LFO3 Speed", "LFO 3's speed"},
   {"lfos", "LFO Speeds", "the speed of all three LFOs"},
};
inline constexpr int kModDestinationCount = sizeof(kModDestinations) / sizeof(kModDestinations[0]);

inline constexpr int kModSlots = 10;

// One mod matrix slot's routing, as indexes into kModSources and kModDestinations. CC 52 + slot
// sets its level.
struct ModRoute {
   uint8_t source = 0;
   uint8_t destination = 0;

   bool operator==(const ModRoute &o) const { return source == o.source && destination == o.destination; }
   // A slot with no source or no destination does nothing, whatever the other one says.
   bool used() const {
      return source != 0 && destination != 0 && source < kModSourceCount && destination < kModDestinationCount;
   }
};

inline const ModName &source(const ModRoute &r) { return kModSources[r.source < kModSourceCount ? r.source : 0]; }
inline const ModName &destination(const ModRoute &r) {
   return kModDestinations[r.destination < kModDestinationCount ? r.destination : 0];
}

// The host's name for mod matrix slot 0-9: "Mod 1: LFO 2 → Pitch" once a program is loaded.
inline std::string modParamName(int slot, bool programLoaded, const ModRoute &r) {
   const std::string base = "Mod " + std::to_string(slot + 1);
   if (!programLoaded)
      return "Mod Matrix " + std::to_string(slot + 1);
   if (!r.used())
      return base + ": Off";
   return base + ": " + source(r).label + " \xE2\x86\x92 " + destination(r).label;
}

// The hover help for a mod matrix knob.
inline std::string modSentence(int slot, bool programLoaded, const ModRoute &r) {
   const std::string cc = " (CC " + std::to_string(52 + slot) + ")";
   const std::string name = "Mod slot " + std::to_string(slot + 1);
   if (!programLoaded)
      return name + " amount" + cc + ". Pick a program to see what it modulates.";
   if (!r.used())
      return name + " is off in this program, so this amount does nothing" + cc + ".";
   std::string text = source(r).text;
   text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
   return name + ": " + text + " modulates " + destination(r).text +
          ". The slider sets how much: none at the centre, reversed below it" + cc + ".";
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

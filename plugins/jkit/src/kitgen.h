#pragma once

// GENERATE KIT: one style for the whole kit, and a sound for each pad. The pads follow General MIDI
// from note 36 (C1), so a drum part written for any GM kit plays the right sounds: kick on 36, snare
// on 38, closed hat on 42, open hat on 46, crash on 49. The three hats share choke group 1, so a
// closed or pedal hat cuts the open one off.
//
// Each pad's prompt is its sound, then the style: "closed hi-hat, dusty lo-fi boom bap, tape
// saturation". With no style typed, the kit gets one random style and treatment, the same for every
// pad, so the kit hangs together.

#include <cstdint>
#include <string>

#include "prompts.h"

namespace jkit {

struct KitSlot {
   const char *name;  // the pad's name
   Role role;         // its colour and role
   const char *sound; // what to ask for
   double seconds;    // how long
   int choke;         // its choke group (0 none)
};

constexpr int kKitSlots = 16;

inline const KitSlot &kitSlot(int pad) {
   static const KitSlot kSlots[kKitSlots] = {
      {"Kick", kKick, "kick drum", 0.8, 0},             // 36
      {"Rim", kPerc, "rimshot, side stick", 0.4, 0},    // 37
      {"Snare", kSnare, "snare drum", 0.8, 0},          // 38
      {"Clap", kClap, "hand clap", 0.8, 0},             // 39
      {"Snare 2", kSnare, "tight snare hit", 0.6, 0},   // 40
      {"Low Tom", kTom, "low floor tom", 1.2, 0},       // 41
      {"Closed Hat", kHat, "closed hi-hat", 0.3, 1},    // 42
      {"Mid Tom", kTom, "mid tom", 1.0, 0},             // 43
      {"Pedal Hat", kHat, "pedal hi-hat chick", 0.3, 1}, // 44
      {"High Tom", kTom, "high tom", 0.9, 0},           // 45
      {"Open Hat", kHat, "open hi-hat", 1.0, 1},        // 46
      {"Perc", kPerc, "conga hit", 0.6, 0},             // 47
      {"Shaker", kPerc, "shaker", 0.5, 0},              // 48
      {"Crash", kHat, "crash cymbal", 2.5, 0},          // 49
      {"Kick 2", kKick, "deep sub kick", 1.0, 0},       // 50
      {"FX", kFx, "impact hit", 1.5, 0},                // 51
   };
   return kSlots[pad < 0 || pad >= kKitSlots ? 0 : pad];
}

// A style for a kit with none typed: a style of music and a treatment.
inline std::string randomKitStyle(uint64_t seed) {
   prompts::Rng rng(seed * 2654435761u + 99u);
   return std::string(rng.pick(prompts::styles())) + ", " + rng.pick(prompts::treatments());
}

// The prompt for a pad of a kit in `style`.
inline std::string kitPrompt(int pad, const std::string &style) {
   std::string p = kitSlot(pad).sound;
   if (!style.empty())
      p += ", " + style;
   return p;
}

} // namespace jkit

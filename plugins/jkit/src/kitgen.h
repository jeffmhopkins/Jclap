#pragma once

// GENERATE KIT: one style for the whole kit, and a sound for each pad, as a drum kit, hand
// percussion, effects or animals. A drum kit's pads follow General MIDI from note 36 (C1), so a drum part written for
// any GM kit plays the right sounds: kick on 36, snare on 38, closed hat on 42, open hat on 46, crash
// on 49. The three hats share choke group 1, so a closed or pedal hat cuts the open one off. Hand
// percussion puts its low strokes where the kicks are and its slaps where the snares are.
//
// Each pad's prompt is its sound, then the style: "closed hi-hat, dusty lo-fi boom bap, tape
// saturation". With no style typed, the kit gets one random style and treatment, the same for every
// pad, so the kit hangs together.

#include <algorithm>
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

// What the sixteen pads are: a drum kit, or hand percussion from around the world.
enum KitLayout : int { kDrumKitLayout, kHandPercussionLayout, kFxLayout, kAnimalLayout, kKitLayoutCount };
inline const char *const kKitLayoutNames[kKitLayoutCount] = {"Drum kit", "Percussion", "FX", "Animals"};

inline const KitSlot &kitSlot(int pad, int layout = kDrumKitLayout) {
   static const KitSlot kDrums[kKitSlots] = {
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
   // Hand drums: the low strokes where a kit's kicks are, the slaps where its snares are, and the
   // open tones where its toms are, so a GM drum part still makes sense on them. Each sound is one
   // stroke of one drum, played by hand.
   static const KitSlot kHands[kKitSlots] = {
      {"Cajon Bass", kKick, "cajon bass tone, one hand stroke", 0.7, 0},           // 36
      {"Djembe Slap", kPerc, "djembe slap, one hand stroke", 0.5, 0},              // 37
      {"Cajon Slap", kSnare, "cajon slap, snare wires, one hand stroke", 0.5, 0},  // 38
      {"Palmas", kClap, "flamenco palmas hand clap", 0.5, 0},                      // 39
      {"Djembe Bass", kKick, "djembe bass tone, one hand stroke", 0.8, 0},         // 40
      {"Low Conga", kTom, "low conga open tone, tumba, one hand stroke", 0.8, 0},  // 41
      {"Shaker", kHat, "shaker, one shake", 0.4, 0},                               // 42
      {"High Conga", kTom, "high conga open tone, one hand stroke", 0.7, 0},       // 43
      {"Conga Slap", kPerc, "conga slap, one hand stroke", 0.5, 0},                // 44
      {"Low Bongo", kTom, "low bongo hembra, one finger stroke", 0.5, 0},          // 45
      {"High Bongo", kPerc, "high bongo macho, one finger stroke", 0.4, 0},        // 46
      {"Djembe Tone", kTom, "djembe open tone, one hand stroke", 0.7, 0},          // 47
      {"Darbuka Dum", kKick, "darbuka doum, deep center stroke", 0.7, 0},          // 48
      {"Darbuka Tek", kPerc, "darbuka tek, sharp rim stroke", 0.4, 0},             // 49
      {"Tabla", kPerc, "tabla na stroke, ringing", 0.8, 0},                        // 50
      {"Frame Drum", kTom, "frame drum, bendir, open hand stroke", 0.9, 0},        // 51
   };
   // Effects: bleeps, bloops, zaps and hits, low ones where the kicks are and short bright ones where
   // the hats are.
   static const KitSlot kEffects[kKitSlots] = {
      {"Sub Boom", kKick, "deep sub boom impact", 1.2, 0},       // 36
      {"Blip", kPerc, "short synth blip", 0.3, 0},               // 37
      {"Zap", kSnare, "laser zap", 0.5, 0},                      // 38
      {"Glitch", kClap, "digital glitch stutter", 0.6, 0},       // 39
      {"Noise Hit", kSnare, "white noise burst hit", 0.5, 0},    // 40
      {"Bloop", kTom, "low bubbly synth bloop", 0.6, 0},         // 41
      {"Tick", kHat, "tiny digital tick", 0.2, 0},               // 42
      {"Boing", kTom, "cartoon boing spring", 0.8, 0},           // 43
      {"Chirp", kHat, "short robot chirp", 0.4, 0},              // 44
      {"Pew", kTom, "descending laser pew", 0.6, 0},             // 45
      {"Sweep", kHat, "short filtered noise sweep", 0.8, 0},     // 46
      {"Coin", kPerc, "8-bit video game coin", 0.5, 0},          // 47
      {"Wub", kPerc, "wobbly bass wub", 0.8, 0},                 // 48
      {"Riser", kFx, "short riser", 2.0, 0},                     // 49
      {"Impact", kKick, "cinematic impact hit", 1.5, 0},         // 50
      {"Reverse", kFx, "reverse cymbal swell", 2.0, 0},          // 51
   };
   // Animals: low calls where the kicks are, barks and quacks where the snares and claps are, small
   // chirps and rattles where the hats are.
   static const KitSlot kAnimals[kKitSlots] = {
      {"Bullfrog", kKick, "bullfrog croak, low", 0.7, 0},        // 36
      {"Woodpecker", kPerc, "woodpecker knock", 0.4, 0},         // 37
      {"Dog", kSnare, "dog bark, single bark", 0.5, 0},          // 38
      {"Duck", kClap, "duck quack", 0.5, 0},                     // 39
      {"Cat", kSnare, "cat meow", 0.8, 0},                       // 40
      {"Cow", kTom, "cow moo", 1.5, 0},                          // 41
      {"Cricket", kHat, "cricket chirp", 0.4, 0},                // 42
      {"Goat", kTom, "goat bleat", 0.9, 0},                      // 43
      {"Bird", kHat, "small bird chirp", 0.4, 0},                // 44
      {"Sheep", kTom, "sheep baa", 0.9, 0},                      // 45
      {"Rattlesnake", kHat, "rattlesnake rattle", 0.8, 0},       // 46
      {"Monkey", kPerc, "monkey screech", 0.8, 0},               // 47
      {"Horse", kPerc, "horse neigh", 1.5, 0},                   // 48
      {"Elephant", kFx, "elephant trumpet", 2.0, 0},             // 49
      {"Lion", kKick, "lion roar", 2.0, 0},                      // 50
      {"Wolf", kFx, "wolf howl", 2.5, 0},                        // 51
   };
   const KitSlot *table = layout == kHandPercussionLayout ? kHands
                          : layout == kFxLayout           ? kEffects
                          : layout == kAnimalLayout       ? kAnimals
                                                          : kDrums;
   return table[pad < 0 || pad >= kKitSlots ? 0 : pad];
}

// Styles for hand percussion: a tradition, then how it was recorded.
inline const prompts::List &percussionStyles() {
   static const prompts::List k = {
      "Afro-Cuban rumba", "Afro-Cuban salsa", "West African djembe ensemble", "Guinean dundun",
      "Middle Eastern", "Arabic maqsum", "Egyptian baladi", "Turkish", "Persian tombak", "North African gnawa",
      "Indian classical", "Bollywood", "Brazilian samba batucada", "Brazilian bossa nova", "Bahia axe",
      "flamenco", "Peruvian afro-Peruvian", "Caribbean soca", "Jamaican nyabinghi", "Balinese",
      "Japanese taiko ensemble", "Celtic", "tribal", "ambient world", "Afrobeat", "Afro house",
      "Latin jazz", "reggaeton", "cumbia", "Andean folk"};
   return k;
}
inline const prompts::List &percussionRecordings() {
   static const prompts::List k = {
      "warm natural room", "dry close miked", "outdoor village recording", "big wooden hall",
      "crisp studio recording", "lo-fi sampled from vinyl", "live on a street corner", "intimate, soft hands",
      "bright and punchy", "dark and earthy", "with a short room tail", "tape saturated"};
   return k;
}

inline const prompts::List &fxStyles() {
   static const prompts::List k = {
      "retro 8-bit video game", "sci-fi spaceship", "analog modular synth", "cartoon", "glitchy IDM",
      "circuit bent toy", "cyberpunk", "vaporwave", "dubstep", "1950s sci-fi theremin", "arcade machine",
      "robot", "underwater", "alien planet", "Commodore 64 chip", "broken radio", "laser battle",
      "Game Boy", "space station computer", "haunted electronics", "dial-up modem", "acid techno 303"};
   return k;
}
inline const prompts::List &fxTreatments() {
   static const prompts::List k = {"dry", "with a short echo", "bitcrushed", "wide stereo", "with spring reverb",
                                   "lo-fi", "clean digital", "tape saturated", "distorted", "squelchy"};
   return k;
}
inline const prompts::List &animalStyles() {
   static const prompts::List k = {"farm animals", "jungle animals", "African savanna", "forest at dawn",
                                   "cartoon animals", "pets at home", "barnyard", "nature documentary",
                                   "zoo", "swamp at night", "rainforest", "mountain wildlife"};
   return k;
}
inline const prompts::List &animalRecordings() {
   static const prompts::List k = {"close up field recording", "isolated one-shot", "natural outdoor ambience",
                                   "studio foley", "dry", "distant, with echo", "crisp and clear"};
   return k;
}

// Plain one or two word styles for the GENERIC end of the slider: "909" makes "909 kick drum".
inline const prompts::List &plainKitStyles(int layout) {
   static const prompts::List kDrums = {"808", "909", "707", "acoustic", "LinnDrum", "vintage", "rock", "jazz",
                                        "hip hop", "techno", "house", "trap", "lo-fi", "funk", "breakbeat",
                                        "electro", "disco", "metal", "SP-1200", "analog"};
   static const prompts::List kHands = {"Afro-Cuban", "West African", "Middle Eastern", "Indian", "Brazilian",
                                        "flamenco", "Latin", "Turkish", "Egyptian", "Caribbean"};
   static const prompts::List kFx = {"8-bit", "sci-fi", "retro", "cartoon", "arcade",
                                     "synth", "glitch", "robot", "analog", "chiptune"};
   static const prompts::List kAnimals = {"farm", "jungle", "forest", "zoo", "pet", "barnyard", "wild", "savanna"};
   return layout == kHandPercussionLayout ? kHands : layout == kFxLayout ? kFx : layout == kAnimalLayout ? kAnimals : kDrums;
}

// A style for a kit with none typed, or from the dice: for a drum kit, a style of music and a
// treatment; for the others, their own kind of style and how it was recorded. `strange` is the
// GENERIC - STRANGE slider: at the left one plain word ("909"), in the middle a style and a
// treatment, and to the right odd materials and processes on top.
inline std::string randomKitStyle(uint64_t seed, int layout = kDrumKitLayout, double strange = 0.3) {
   prompts::Rng rng(seed * 2654435761u + 99u + static_cast<uint64_t>(layout) * 7u);
   strange = strange < 0 ? 0 : strange > 1 ? 1 : strange;
   if (strange < 0.2)
      return rng.pick(plainKitStyles(layout));
   std::string style;
   if (layout == kHandPercussionLayout)
      style = std::string(rng.pick(percussionStyles())) + " percussion, " + rng.pick(percussionRecordings());
   else if (layout == kFxLayout)
      style = std::string(rng.pick(fxStyles())) + " sound effects, " + rng.pick(fxTreatments());
   else if (layout == kAnimalLayout)
      style = std::string(rng.pick(animalStyles())) + ", " + rng.pick(animalRecordings());
   else
      style = std::string(rng.pick(prompts::styles())) + ", " + rng.pick(prompts::treatments());
   if (strange >= 0.55) {
      style = std::string(rng.pick(prompts::oddTraits())) + " " + style;
      style += std::string(", ") + rng.pick(prompts::oddTreatments());
   }
   if (strange >= 0.85) {
      const std::string more = rng.pick(prompts::oddTreatments());
      if (style.find(more) == std::string::npos)
         style += ", " + more;
   }
   return style;
}

// The order WHOLE KIT makes the pads in: the backbone first, so you can play along as it fills.
inline const int *kitOrder(int layout) {
   static const int kDrums[kKitSlots] = {0, 2, 6, 10, 3, 1, 4, 8, 13, 5, 7, 9, 11, 12, 14, 15};
   static const int kHands[kKitSlots] = {0, 2, 6, 5, 7, 8, 10, 9, 4, 11, 1, 12, 13, 14, 15, 3};
   static const int kOthers[kKitSlots] = {0, 2, 6, 3, 1, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15};
   return layout == kHandPercussionLayout ? kHands : layout == kDrumKitLayout ? kDrums : kOthers;
}

// The prompt for a pad of a kit in `style`. A short style (one or two words, no comma) goes in front,
// "909 kick drum"; a longer one after, "closed hi-hat, dusty lo-fi boom bap, tape saturation".
// `strange` (the slider) gives each pad its own odd twist as it rises: a strange material, what it's
// made of, and strange processes, so at the right a pad reads "glassy kick drum, made of a door
// slammed in a cathedral, techno, granular smear, recorded in a cave". The pad's own sound always
// stays in, so it still fits its note. Same arguments, same prompt.
inline std::string kitPrompt(int pad, const std::string &style, int layout = kDrumKitLayout, double strange = 0,
                             uint64_t seed = 0) {
   using namespace prompts;
   strange = strange < 0 ? 0 : strange > 1 ? 1 : strange;
   const KitSlot &slot = kitSlot(pad, layout);
   Rng rng(seed * 6364136223846793005u + static_cast<uint64_t>(pad) * 1442695040888963407u +
           static_cast<uint64_t>(layout) * 31u + static_cast<uint64_t>(strange * 1000.0));
   std::string sound = slot.sound;
   if (strange >= 0.35 && rng.chance(strange))
      sound = std::string(rng.pick(oddTraits())) + " " + sound;
   const bool shortStyle = !style.empty() && style.find(',') == std::string::npos &&
                           std::count(style.begin(), style.end(), ' ') <= 1;
   std::string p = shortStyle ? style + " " + sound : sound;
   if (strange >= 0.5 && rng.chance(strange * 1.2 - 0.3)) {
      static const char *const kMadeOf[] = {"made of", "made from", "sampled from", "like"};
      p += std::string(", ") + kMadeOf[rng.next() % 4] + " " + rng.pick(roleWords(slot.role).oddities);
   }
   if (!style.empty() && !shortStyle)
      p += ", " + style;
   const int odd = strange < 0.45 ? 0 : strange < 0.85 ? 1 : 2;
   for (int i = 0; i < odd; ++i) {
      const std::string t = rng.pick(oddTreatments());
      if (p.find(t) == std::string::npos)
         p += ", " + t;
   }
   return p;
}

} // namespace jkit

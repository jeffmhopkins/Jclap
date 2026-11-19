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

// A style for a kit with none typed, or from the card's dice: for a drum kit, a style of music and a
// treatment; for the others, their own kind of style and how it was recorded.
inline std::string randomKitStyle(uint64_t seed, int layout = kDrumKitLayout) {
   prompts::Rng rng(seed * 2654435761u + 99u + static_cast<uint64_t>(layout) * 7u);
   if (layout == kHandPercussionLayout)
      return std::string(rng.pick(percussionStyles())) + " percussion, " + rng.pick(percussionRecordings());
   if (layout == kFxLayout)
      return std::string(rng.pick(fxStyles())) + " sound effects, " + rng.pick(fxTreatments());
   if (layout == kAnimalLayout)
      return std::string(rng.pick(animalStyles())) + ", " + rng.pick(animalRecordings());
   return std::string(rng.pick(prompts::styles())) + ", " + rng.pick(prompts::treatments());
}

// The order WHOLE KIT makes the pads in: the backbone first, so you can play along as it fills.
inline const int *kitOrder(int layout) {
   static const int kDrums[kKitSlots] = {0, 2, 6, 10, 3, 1, 4, 8, 13, 5, 7, 9, 11, 12, 14, 15};
   static const int kHands[kKitSlots] = {0, 2, 6, 5, 7, 8, 10, 9, 4, 11, 1, 12, 13, 14, 15, 3};
   static const int kOthers[kKitSlots] = {0, 2, 6, 3, 1, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15};
   return layout == kHandPercussionLayout ? kHands : layout == kDrumKitLayout ? kDrums : kOthers;
}

// The prompt for a pad of a kit in `style`.
inline std::string kitPrompt(int pad, const std::string &style, int layout = kDrumKitLayout) {
   std::string p = kitSlot(pad, layout).sound;
   if (!style.empty())
      p += ", " + style;
   return p;
}

} // namespace jkit

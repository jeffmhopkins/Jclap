#pragma once

// JBreath's presets: a factory bank compiled in, and the user's own saved through jclap::ui's preset
// files. A factory preset lists what it changes from the defaults; every other param is filled in at
// its default, so nothing from the previous preset carries over.

#include <jclap/ui/presets.h>

#include <vector>

#include "params.h"

namespace jbreath {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"JBreath", 1, ".jbpreset", kPresetParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   // Not the look's: a preset leaves the AIR view as it is.
   for (clap_id id = 0; id < kPresetParamCount; ++id)
      d.push_back(kParams[id].defaultValue);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      constexpr double kFluteModel = 0, kClarinetModel = 1, kSaxModel = 2, kOboeModel = 3, kTrumpetModel = 4;
      constexpr double kLegatoMode = 2, kMonoMode = 1;
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // Each family runs from clean to grungy, airy, bass and its oddities. Flutes: a breathy jet,
      // vibrato on the breath (a flautist's comes from the diaphragm).
      const double F = kFluteModel;
      p.push_back({"Concert Flute", "Flute", {{kModel, F}, {kNoise, 0.6}, {kVibDepth, 0.06}, {kAttack, 30}, {kRelease, 110}}});
      p.push_back({"Night Flute", "Flute", {{kModel, F}, {kVibDepth, 0.07}, {kAttack, 35}, {kRelease, 120}}});
      p.push_back({"Bamboo Air", "Flute",
                   {{kModel, F}, {kNoise, 1.9}, {kEdge, 0.5}, {kVibDepth, 0.12}, {kVibRate, 4.6}, {kVibDelay, 600},
                    {kAttack, 60}, {kRelease, 250}, {kVoiceMode, kLegatoMode}, {kGlide, 40}}});
      p.push_back({"Breath Pad", "Flute",
                   {{kModel, F}, {kNoise, 2.0}, {kEdge, 0.3}, {kBreath, 0.7}, {kAttack, 300}, {kRelease, 900},
                    {kVibDepth, 0.04}, {kVibRate, 3.5}, {kBright, -3}}});
      p.push_back({"Tin Whistle", "Flute",
                   {{kModel, F}, {kTune, 12}, {kEdge, 1.4}, {kNoise, 1.2}, {kKick, 0.35}, {kAttack, 8}, {kRelease, 60},
                    {kVibDepth, 0.0}, {kVibPitch, 12}, {kVibRate, 6.2}, {kVibDelay, 250}, {kVoiceMode, kLegatoMode},
                    {kGlide, 25}, {kBright, 2}}});
      p.push_back({"Piccolo", "Flute",
                   {{kModel, F}, {kTune, 24}, {kNoise, 0.7}, {kEdge, 1.2}, {kAttack, 15}, {kVibDepth, 0.05}, {kBright, -2}}});
      p.push_back({"Bass Flute", "Flute",
                   {{kModel, F}, {kTune, -12}, {kNoise, 1.4}, {kEdge, 0.6}, {kAttack, 45}, {kRelease, 200},
                    {kVibDepth, 0.05}, {kVibRate, 4.4}, {kBright, -2}}});
      p.push_back({"Overblown Jet", "Flute",
                   {{kModel, F}, {kBreath, 1.4}, {kEdge, 2.0}, {kNoise, 1.6}, {kKick, 0.7}, {kAttack, 10},
                    {kVibDepth, 0.08}, {kVibRate, 6.5}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Shakuhachi Bend", "Flute",
                   {{kModel, F}, {kNoise, 1.8}, {kEdge, 0.9}, {kEmbouchure, 0.35}, {kAttack, 80}, {kRelease, 300},
                    {kVibDepth, 0.1}, {kVibPitch, 15}, {kVibRate, 4.0}, {kVibDelay, 800}, {kVoiceMode, kLegatoMode},
                    {kGlide, 120}, {kSlide, 1.0}}});
      p.push_back({"Glass Ocarina", "Flute",
                   {{kModel, F}, {kTune, 12}, {kNoise, 0.2}, {kEdge, 0.2}, {kBreath, 0.8}, {kAttack, 40},
                    {kRelease, 180}, {kVibDepth, 0.03}, {kBright, -6}}});

      // Clarinets: a closed cylinder, odd harmonics; a clarinettist plays with little or no vibrato.
      const double C = kClarinetModel;
      p.push_back({"Wooden Reed", "Clarinet", {{kModel, C}, {kVibDepth, 0.0}, {kAttack, 20}}});
      p.push_back({"Legato Clarinet", "Clarinet",
                   {{kModel, C}, {kVibDepth, 0.02}, {kAttack, 20}, {kVoiceMode, kLegatoMode}}});
      p.push_back({"Chalumeau", "Clarinet",
                   {{kModel, C}, {kBreath, 0.85}, {kEdge, 0.5}, {kNoise, 0.5}, {kBright, -4}, {kVibDepth, 0.0},
                    {kAttack, 30}, {kRelease, 140}}});
      p.push_back({"Bass Clarinet", "Clarinet",
                   {{kModel, C}, {kTune, -12}, {kEdge, 0.8}, {kAttack, 30}, {kRelease, 150}, {kVibDepth, 0.0}}});
      p.push_back({"Low Rasp Reed", "Clarinet",
                   {{kModel, C}, {kTune, -12}, {kEdge, 1.7}, {kBreath, 1.1}, {kNoise, 1.4}, {kKick, 0.5},
                    {kAttack, 15}, {kRelease, 100}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Klezmer Cry", "Clarinet",
                   {{kModel, C}, {kEdge, 1.6}, {kBreath, 1.1}, {kVibDepth, 0.06}, {kVibPitch, 18}, {kVibRate, 5.8},
                    {kVibDelay, 200}, {kVoiceMode, kLegatoMode}, {kGlide, 90}, {kKick, 0.4}}});
      p.push_back({"Bright Eb", "Clarinet",
                   {{kModel, C}, {kTune, 12}, {kEdge, 1.3}, {kBright, 2}, {kAttack, 12}, {kVibDepth, 0.0}}});
      p.push_back({"Breathy Reed", "Clarinet",
                   {{kModel, C}, {kNoise, 2.0}, {kBreath, 0.8}, {kEdge, 0.4}, {kAttack, 70}, {kRelease, 260},
                    {kVibDepth, 0.04}, {kBright, -3}}});
      p.push_back({"Squeal Reed", "Clarinet",
                   {{kModel, C}, {kBreath, 1.35}, {kEdge, 2.0}, {kNoise, 1.5}, {kKick, 0.8}, {kAttack, 8},
                    {kVoiceMode, kMonoMode}, {kVibDepth, 0.0}, {kBright, 3}}});
      p.push_back({"Hollow Pad", "Clarinet",
                   {{kModel, C}, {kBreath, 0.8}, {kEdge, 0.3}, {kNoise, 0.8}, {kAttack, 350}, {kRelease, 1200},
                    {kVibDepth, 0.05}, {kVibRate, 3.2}, {kBright, -6}}});

      // Saxes: a cone with a mouthpiece honk; the player's vibrato is in the jaw, so in the pitch too.
      const double S = kSaxModel;
      p.push_back({"Smoke Tenor", "Sax",
                   {{kModel, S}, {kVibDepth, 0.05}, {kVibPitch, 10}, {kAttack, 15}, {kKick, 0.4}, {kRelease, 110}}});
      p.push_back({"Growl Alto", "Sax",
                   {{kModel, S}, {kTune, 7}, {kEdge, 2.0}, {kBreath, 1.15}, {kNoise, 1.5}, {kVibDepth, 0.04},
                    {kVibPitch, 14}, {kVibRate, 5.6}, {kKick, 0.4}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Clean Alto", "Sax",
                   {{kModel, S}, {kEdge, 0.5}, {kNoise, 0.4}, {kVibDepth, 0.03}, {kVibPitch, 6}, {kAttack, 20}}});
      p.push_back({"Subtone Ballad", "Sax",
                   {{kModel, S}, {kBreath, 0.75}, {kNoise, 1.9}, {kEdge, 0.3}, {kBright, -5}, {kAttack, 50},
                    {kRelease, 250}, {kVibDepth, 0.05}, {kVibPitch, 12}, {kVibRate, 4.8}, {kVibDelay, 500},
                    {kVoiceMode, kLegatoMode}, {kGlide, 60}}});
      p.push_back({"Baritone", "Sax",
                   {{kModel, S}, {kTune, -12}, {kEdge, 1.2}, {kKick, 0.4}, {kAttack, 20}, {kVibPitch, 6}}});
      p.push_back({"Bass Honk", "Sax",
                   {{kModel, S}, {kTune, -24}, {kEdge, 1.6}, {kBreath, 1.1}, {kKick, 0.6}, {kAttack, 10},
                    {kRelease, 70}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Soprano Line", "Sax",
                   {{kModel, S}, {kTune, 12}, {kEdge, 0.8}, {kVibDepth, 0.04}, {kVibPitch, 9}, {kVibRate, 5.4},
                    {kVoiceMode, kLegatoMode}, {kGlide, 30}}});
      p.push_back({"Gutbucket", "Sax",
                   {{kModel, S}, {kBreath, 1.2}, {kEdge, 2.0}, {kNoise, 2.0}, {kKick, 0.8}, {kAttack, 6},
                    {kBright, 3}, {kVibPitch, 20}, {kVibRate, 6.5}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Section Pad", "Sax",
                   {{kModel, S}, {kEdge, 0.6}, {kBreath, 0.85}, {kAttack, 180}, {kRelease, 700}, {kVibDepth, 0.04},
                    {kVibPitch, 6}, {kVibRate, 4.5}, {kBright, -3}}});
      p.push_back({"Airy Tenor", "Sax",
                   {{kModel, S}, {kNoise, 2.0}, {kBreath, 0.8}, {kEdge, 0.4}, {kAttack, 90}, {kRelease, 400},
                    {kVibDepth, 0.07}, {kVibPitch, 8}, {kBright, -2}}});

      // Double reeds: a narrow cone and a stiff reed; a quick, singing vibrato.
      const double O = kOboeModel;
      p.push_back({"Dusk Oboe", "Oboe",
                   {{kModel, O}, {kVibDepth, 0.05}, {kVibPitch, 8}, {kAttack, 15}, {kKick, 0.4}, {kRelease, 100}}});
      p.push_back({"Pastoral", "Oboe",
                   {{kModel, O}, {kEdge, 0.6}, {kNoise, 0.5}, {kVibDepth, 0.05}, {kVibPitch, 6}, {kVibRate, 5.6},
                    {kAttack, 20}, {kVoiceMode, kLegatoMode}, {kGlide, 25}}});
      p.push_back({"Cor Anglais", "Oboe",
                   {{kModel, O}, {kTune, -5}, {kEdge, 0.7}, {kBright, -4}, {kVibDepth, 0.05}, {kVibPitch, 7},
                    {kAttack, 30}, {kRelease, 160}}});
      p.push_back({"Bassoon", "Oboe",
                   {{kModel, O}, {kTune, -24}, {kEdge, 1.0}, {kKick, 0.4}, {kAttack, 20}, {kVibDepth, 0.03},
                    {kVibPitch, 5}, {kBright, -2}}});
      p.push_back({"Contra Buzz", "Oboe",
                   {{kModel, O}, {kTune, -24}, {kEdge, 2.0}, {kBreath, 1.15}, {kNoise, 1.4}, {kKick, 0.6},
                    {kAttack, 8}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Shawm", "Oboe",
                   {{kModel, O}, {kEdge, 2.0}, {kBreath, 1.2}, {kNoise, 1.2}, {kKick, 0.6}, {kAttack, 8},
                    {kBright, 4}, {kVibDepth, 0.0}, {kVibPitch, 4}}});
      p.push_back({"Musette", "Oboe",
                   {{kModel, O}, {kTune, 12}, {kEdge, 1.4}, {kKick, 0.5}, {kAttack, 10}, {kVibPitch, 10},
                    {kVibRate, 6.4}, {kVibDelay, 150}, {kVoiceMode, kLegatoMode}, {kGlide, 20}}});
      p.push_back({"Duduk Air", "Oboe",
                   {{kModel, O}, {kTune, -12}, {kNoise, 2.0}, {kEdge, 0.3}, {kBreath, 0.8}, {kBright, -6},
                    {kAttack, 120}, {kRelease, 500}, {kVibDepth, 0.06}, {kVibPitch, 14}, {kVibRate, 4.6},
                    {kVibDelay, 700}, {kVoiceMode, kLegatoMode}, {kGlide, 140}}});
      p.push_back({"Reed Choir", "Oboe",
                   {{kModel, O}, {kEdge, 0.5}, {kBreath, 0.85}, {kAttack, 220}, {kRelease, 900}, {kVibDepth, 0.04},
                    {kVibPitch, 5}, {kVibRate, 4.2}, {kBright, -4}}});
      p.push_back({"Snake Charmer", "Oboe",
                   {{kModel, O}, {kEdge, 1.6}, {kNoise, 1.3}, {kVibPitch, 25}, {kVibRate, 6.8}, {kVibDelay, 100},
                    {kVoiceMode, kLegatoMode}, {kGlide, 180}, {kKick, 0.4}}});

      // Brass: the lips' vibrato is in the pitch.
      const double T = kTrumpetModel;
      p.push_back({"Brass Plume", "Trumpet", {{kModel, T}, {kVibDepth, 0.02}, {kVibPitch, 6}, {kAttack, 20}}});
      p.push_back({"Fanfare", "Trumpet",
                   {{kModel, T}, {kEdge, 1.8}, {kKick, 0.4}, {kAttack, 10}, {kBright, 3}, {kVibDepth, 0.0}, {kVibPitch, 0}}});
      p.push_back({"Muted Night", "Trumpet",
                   {{kModel, T}, {kEdge, 0.5}, {kBright, -8}, {kVibPitch, 8}, {kVibDepth, 0.03}, {kAttack, 30},
                    {kRelease, 160}, {kVoiceMode, kLegatoMode}}});
      p.push_back({"Flugel Glow", "Trumpet",
                   {{kModel, T}, {kEdge, 0.3}, {kBright, -6}, {kBreath, 0.85}, {kAttack, 40}, {kRelease, 200},
                    {kVibPitch, 6}, {kVibDepth, 0.03}, {kVoiceMode, kLegatoMode}, {kGlide, 30}}});
      p.push_back({"French Horn", "Trumpet",
                   {{kModel, T}, {kTune, -12}, {kEdge, 0.5}, {kBright, -7}, {kAttack, 45}, {kRelease, 250},
                    {kVibPitch, 4}, {kVibDepth, 0.02}}});
      p.push_back({"Tuba", "Trumpet",
                   {{kModel, T}, {kTune, -12}, {kEdge, 0.7}, {kBright, -6}, {kKick, 0.4}, {kAttack, 30},
                    {kRelease, 120}, {kVibDepth, 0.0}, {kVibPitch, 3}}});
      p.push_back({"Trombone Slide", "Trumpet",
                   {{kModel, T}, {kTune, -12}, {kEdge, 1.3}, {kAttack, 25}, {kVoiceMode, kLegatoMode},
                    {kGlide, 160}, {kVibPitch, 8}, {kVibDelay, 500}}});
      p.push_back({"Blat", "Trumpet",
                   {{kModel, T}, {kBreath, 1.4}, {kEdge, 2.0}, {kNoise, 1.6}, {kKick, 0.9}, {kAttack, 5},
                    {kBright, 4}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Airy Mute", "Trumpet",
                   {{kModel, T}, {kNoise, 2.0}, {kEdge, 0.3}, {kBreath, 0.8}, {kBright, -10}, {kAttack, 60},
                    {kRelease, 300}, {kVibPitch, 10}, {kVibDepth, 0.05}}});
      p.push_back({"Piccolo Trumpet", "Trumpet",
                   {{kModel, T}, {kTune, 12}, {kEdge, 1.2}, {kKick, 0.35}, {kAttack, 10}, {kBright, 1},
                    {kVibDepth, 0.0}, {kVibPitch, 4}}});

      // Alien: twin reeds on coiled bores. The breath brings the ghost bore's inharmonic voice up out
      // of the tone, Bite is how hard the two reeds pull on each other, Cone how tight the coils are,
      // and the slide (embouchure) tunes the ghost against the note.
      constexpr double kAlienModel = 5;
      const double A = kAlienModel;
      p.push_back({"First Contact", "Alien",
                   {{kModel, A}, {kBreath, 1.25}, {kAttack, 30}, {kRelease, 220}, {kVibDepth, 0.03},
                    {kRoom, 0.3}}});
      p.push_back({"Ghost Helix", "Alien",
                   {{kModel, A}, {kBreath, 1.35}, {kEdge, 1.4}, {kSlide, 1.0}, {kAttack, 40}, {kRelease, 400},
                    {kVibDepth, 0.0}, {kVibPitch, 6}, {kVoiceMode, kLegatoMode}, {kGlide, 60}, {kRoom, 0.35}}});
      p.push_back({"Xeno Choir", "Alien",
                   {{kModel, A}, {kBreath, 1.15}, {kCone, 0.8}, {kEdge, 0.8}, {kAttack, 260}, {kRelease, 1200},
                    {kVibDepth, 0.06}, {kVibRate, 3.8}, {kBright, -3}, {kUnison, 3}, {kUnisonDetune, 14},
                    {kRoom, 0.5}, {kRoomDecay, 4.0}}});
      p.push_back({"Tractor Beam", "Alien",
                   {{kModel, A}, {kTune, -12}, {kBreath, 1.5}, {kEdge, 2.0}, {kKick, 0.6}, {kAttack, 15},
                    {kRelease, 300}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}, {kGlide, 80}}});
      p.push_back({"Coil Whistle", "Alien",
                   {{kModel, A}, {kTune, 12}, {kCone, 1.0}, {kEdge, 0.6}, {kNoise, 0.5}, {kAttack, 20},
                    {kRelease, 350}, {kVibPitch, 12}, {kVibRate, 6.5}, {kVibDelay, 200}, {kRoom, 0.4}}});
      p.push_back({"Mothership", "Alien",
                   {{kModel, A}, {kTune, -12}, {kBreath, 1.15}, {kEdge, 1.3}, {kCone, 0.3}, {kAttack, 400},
                    {kRelease, 2000}, {kVibDepth, 0.04}, {kVibRate, 2.4}, {kRoom, 0.6}, {kRoomDecay, 6.0}}});
      p.push_back({"Signal Lost", "Alien",
                   {{kModel, A}, {kBreath, 1.5}, {kEdge, 2.0}, {kNoise, 1.8}, {kCone, 0.15}, {kFlutter, 0.25},
                    {kAttack, 10}, {kRelease, 150}, {kVibPitch, 30}, {kVibRate, 7.5}, {kVibDelay, 0},
                    {kVoiceMode, kMonoMode}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace jbreath

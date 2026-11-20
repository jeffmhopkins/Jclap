#pragma once

// J2D2's factory presets, and the user's own through jclap::ui's preset files. Each factory preset
// lists what it changes from the defaults; completePresets() fills in the rest, so nothing from the
// previous preset carries over.

#include <jclap/ui/presets.h>

#include <string>
#include <vector>

#include "params.h"

namespace j2d2 {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"J2D2", 1, ".j2d2preset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

struct FactoryPreset {
   const char *name, *category, *blurb;
   std::vector<std::pair<clap_id, double>> values;
};

// The drone's five layers, ATONAL, MOTION, the amp envelope's attack and release, and SPACE.
inline std::vector<std::pair<clap_id, double>> drone(double tone, double partials, double air, double metal, double sub,
                                                     double pAtonal, double mAtonal, double evolve, double drift, double spread,
                                                     double breath, double attack, double release, double size, double mix) {
   return {{kLayer0 + kLayerTone, tone},
           {kLayer0 + kLayerPartials, partials},
           {kLayer0 + kLayerAir, air},
           {kLayer0 + kLayerMetal, metal},
           {kLayer0 + kLayerSub, sub},
           {kPartialsAtonal, pAtonal},
           {kMetalAtonal, mAtonal},
           {kEvolve, evolve},
           {kDrift, drift},
           {kSpread, spread},
           {kBreath, breath},
           {kAttack, attack},
           {kRelease, release},
           {kSpaceSize, size},
           {kSpaceDecay, 0.6 + 6.0 * size * size},
           {kSpacePreDelay, 8.0 + 30.0 * size},
           {kSpaceMix, mix}};
}

inline std::vector<std::pair<clap_id, double>> operator+(std::vector<std::pair<clap_id, double>> a,
                                                         const std::vector<std::pair<clap_id, double>> &b) {
   a.insert(a.end(), b.begin(), b.end());
   return a;
}

inline const std::vector<FactoryPreset> &factoryBank() {
   static const std::vector<FactoryPreset> bank = {
      // ---- Droid: every note speaks a phrase ----
      {"Init", "Init", "Neutral droid, mid velocity response, no drone. Start here.", {}},
      {"Astromech", "Droid", "The classic: bright chirps, step runs and warbles, cheerful and busy.",
       {{kEnergy, 0.35}, {kFeeling, 0.55}, {kVelResponse, 0.7}, {kTalk, 0.75}}},
      {"Question Mark", "Droid", "Curious. Phrases end on a rising question, with short pauses to listen.",
       {{kEnergy, 0.05}, {kFeeling, 0.4}, {kTalk, 0.5}}},
      {"Overjoyed", "Droid", "Excited: fast and high, with big upward leaps and a squeal at the end.",
       {{kEnergy, 0.95}, {kFeeling, 0.85}, {kVelResponse, 0.8}, {kTalk, 0.95}}},
      {"Nervous Wreck", "Droid", "Worried: trembling warbles, quick and bright; the pitch never settles.",
       {{kEnergy, 0.55}, {kFeeling, -0.6}, {kVelResponse, 0.8}, {kPressResponse, 0.7}, {kTalk, 0.85}}},
      {"Grumble", "Droid", "Grumpy: buzz blocks and ring crossings, driven hard. Velocity adds bite.",
       {{kEnergy, -0.05}, {kFeeling, -0.85}, {kVelResponse, 0.9}, {kPressResponse, 0.8}, {kTalk, 0.6}}},
      {"Little Sigh", "Droid", "Sad: slow, low and narrow; every sound droops and the phrase ends in a sigh.",
       {{kEnergy, -0.85}, {kFeeling, -0.65}, {kVelResponse, 0.4}, {kTalk, 0.35}}},
      {"Low Battery", "Droid", "Sleepy: long glides and soft tweets, with long pauses.",
       {{kEnergy, -0.9}, {kFeeling, 0.3}, {kVelResponse, 0.3}, {kPressResponse, 0.3}, {kTalk, 0.3}}},
      // ---- Blend: droid over drone ----
      {"Hangar Bay", "Blend", "A calm drone underneath. Lean into a held note and bleeps pop through.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 0.55}, {kEnergy, 0.2}, {kFeeling, 0.3}, {kPop, 9.0}, {kTalk, 0.3}} +
          drone(0.7, 0.6, 0.35, 0.3, 0.55, 0.2, 0.2, 1.0, 0.9, 0.9, 0.5, 2.5, 4.0, 0.4, 0.2)},
      {"Night Watch", "Blend", "A low, dark bed, with a sad droid murmuring over it now and then.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 0.6}, {kEnergy, -0.7}, {kFeeling, -0.5}, {kPop, 6.0}, {kTalk, 0.25}} +
          drone(0.5, 0.5, 0.6, 0.55, 0.6, 0.35, 0.4, 0.8, 1.1, 1.0, 0.6, 4.0, 6.0, 0.7, 0.3)},
      {"Workshop", "Blend", "Busy and bright: metal partials ring under a chatty, happy droid.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 0.4}, {kEnergy, 0.5}, {kFeeling, 0.6}, {kPop, 9.0}, {kTalk, 0.6}} +
          drone(0.5, 0.5, 0.25, 0.7, 0.4, 0.2, 0.3, 1.5, 1.0, 0.9, 0.4, 1.5, 3.0, 0.4, 0.2)},
      {"Distress Call", "Blend", "Worried chatter over an uneasy drone. Press hard for alarm.",
       std::vector<std::pair<clap_id, double>>{
          {kMorph, 0.5}, {kEnergy, 0.6}, {kFeeling, -0.55}, {kVelResponse, 0.85}, {kPressResponse, 0.8}, {kPop, 12.0}, {kTalk, 0.5}} +
          drone(0.55, 0.6, 0.5, 0.6, 0.5, 0.55, 0.6, 1.8, 1.4, 1.0, 0.8, 2.5, 4.0, 0.4, 0.2)},
      // ---- Drone: every held note hums its own line ----
      {"Choir of Circuits", "Drone", "Tuned and warm: TONE and PARTIALS in just intonation, breathing slowly.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kFeeling, 0.3}, {kPop, 0.0}} +
          drone(0.75, 0.7, 0.3, 0.2, 0.5, 0.05, 0.05, 0.7, 0.5, 0.7, 0.4, 3.0, 5.0, 0.6, 0.3)},
      {"Reactor Hum", "Drone", "Heavy SUB and saw TONE with a slow hand on the lowpass. Barely moves.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kEnergy, -0.4}, {kFeeling, -0.2}, {kPop, 0.0}} +
          drone(0.85, 0.3, 0.2, 0.15, 0.85, 0.1, 0.1, 0.4, 0.4, 0.5, 0.3, 2.0, 4.0, 0.4, 0.2)},
      {"Deep Space", "Drone", "AIR and PARTIALS drifting wide, with a long attack and release and a big room.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kEnergy, -0.3}, {kFeeling, 0.2}, {kPop, 0.0}} +
          drone(0.35, 0.75, 0.7, 0.35, 0.5, 0.3, 0.25, 1.2, 1.2, 1.0, 0.7, 5.0, 7.0, 0.85, 0.4)},
      {"Rust Bells", "Drone", "METAL forward and leaning atonal, TONE pulled down: industrial, not pretty.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kEnergy, -0.2}, {kFeeling, -0.6}, {kPop, 0.0}} +
          drone(0.25, 0.55, 0.75, 0.9, 0.6, 0.6, 0.85, 1.4, 1.6, 1.0, 0.7, 3.0, 5.0, 0.6, 0.3)},
      {"Thermal Drift", "Drone", "Everything moving: fast evolve, wide drift and spread, deep breath.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kEnergy, 0.4}, {kFeeling, 0.2}, {kPop, 0.0}} +
          drone(0.7, 0.7, 0.4, 0.45, 0.55, 0.35, 0.3, 1.9, 1.5, 1.0, 0.9, 4.0, 5.0, 0.6, 0.3)},
      {"Glass Partials", "Drone", "PARTIALS alone, pure, high in the range. Glassy and still.",
       std::vector<std::pair<clap_id, double>>{{kMorph, 1.0}, {kFeeling, 0.6}, {kPop, 0.0}} +
          drone(0.0, 0.9, 0.2, 0.3, 0.0, 0.0, 0.0, 0.9, 0.6, 1.0, 0.3, 3.0, 6.0, 0.75, 0.35)},
   };
   return bank;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      std::vector<Preset> p;
      for (const FactoryPreset &f : factoryBank())
         p.push_back({f.name, f.category, f.values});
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

// A factory preset's one-line description, or "" for a user preset.
inline std::string factoryBlurb(const std::string &name) {
   for (const FactoryPreset &f : factoryBank())
      if (name == f.name)
         return f.blurb;
   return {};
}

} // namespace j2d2

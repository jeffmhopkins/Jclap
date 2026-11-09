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
      constexpr double kFluteModel = 0, kClarinetModel = 1, kSaxModel = 2, kOboeModel = 3, kTrumpetModel = 4,
                       kAlienModel = 5;
      constexpr double kLegatoMode = 2, kMonoMode = 1;
      constexpr double kSoft = kTongueSoft, kDouble = kTongueDouble, kFlutterTongue = kTongueFlutter,
                       kBreathTongue = kTongueBreath;
      constexpr double kJaw = kVibJaw, kLip = kVibLip;
      // The Holes, Tube, Mute and Hybrid Wave choices, by index.
      constexpr double kLattice3 = 1, kLattice5 = 2, kLattice9 = 4, kOpen = 5;
      constexpr double kBoxwood = 1, kMaple = 2, kBamboo = 3, kBrassTube = 4, kSilver = 5, kGlass = 7;
      constexpr double kStraight = 1, kCup = 2, kHarmon = 3, kPlunger = 4, kBucket = 5;
      constexpr double kTriangle = 1, kSaw = 2, kSquare = 3, kPulse = 4;
      std::vector<Preset> p;

      p.push_back({"Init", "Init", {}});

      // Each family starts from its most natural sound (the model's defaults, tuned against the Iowa
      // recordings, with a player's vibrato and a little room) and runs through its range, darker and
      // breathier, brighter and harder, its relatives by Transpose, and its oddities.

      // Flutes: an air jet, nearly pure when soft and rich in the 2nd and 3rd harmonics as it's
      // blown harder; the vibrato comes from the diaphragm (the breath), with a touch of pitch.
      const double F = kFluteModel;
      p.push_back({"Concert Flute", "Flute",
                   {{kModel, F}, {kTube, kSilver}, {kVibDepth, 0.07}, {kVibPitch, 5}, {kVibRate, 5.2}, {kVibDelay, 300},
                    {kRelease, 120}, {kRoom, 0.2}}});
      p.push_back({"Night Flute", "Flute",
                   {{kModel, F}, {kNoise, 1.25}, {kEdge, 0.7}, {kAperture, 0.65}, {kBright, -3}, {kVibDepth, 0.08},
                    {kVibRate, 4.8}, {kAttack, 30}, {kRelease, 160}, {kTongueType, kBreathTongue}, {kLevel, 3},
                    {kRoom, 0.3}}});
      p.push_back({"Bamboo Air", "Flute",
                   {{kModel, F}, {kTube, kBamboo}, {kNoise, 1.4}, {kEdge, 0.8}, {kLevel, 4}, {kVibDepth, 0.1}, {kVibRate, 4.6},
                    {kVibDelay, 500}, {kAttack, 45}, {kRelease, 220}, {kVoiceMode, kLegatoMode}, {kGlide, 50},
                    {kRoom, 0.25}}});
      p.push_back({"Shakuhachi Bend", "Flute",
                   {{kModel, F}, {kTube, kBamboo}, {kNoise, 1.5}, {kEdge, 1.2}, {kSlide, 1.0}, {kFine, -6}, {kLevel, 4},
                    {kTongueType, kSoft}, {kAttack, 60}, {kRelease, 300}, {kVibDepth, 0.06}, {kVibPitch, 15},
                    {kVibRate, 4.0}, {kVibDelay, 700}, {kVoiceMode, kLegatoMode}, {kGlide, 120}, {kRoom, 0.3}}});
      p.push_back({"Tin Whistle", "Flute",
                   {{kModel, F}, {kTune, 12}, {kFine, 6}, {kTube, kBrassTube}, {kHoles, kOpen}, {kEdge, 1.3}, {kNoise, 1.05},
                    {kKick, 0.35}, {kAttack, 10}, {kRelease, 70}, {kVibDepth, 0.0}, {kVibPitch, 10}, {kVibRate, 6.0},
                    {kVibDelay, 250}, {kVoiceMode, kLegatoMode}, {kGlide, 20}}});
      p.push_back({"Piccolo", "Flute",
                   {{kModel, F}, {kTune, 12}, {kEdge, 1.1}, {kAttack, 15}, {kVibDepth, 0.05}, {kVibRate, 5.6},
                    {kVibDelay, 250}, {kRoom, 0.2}}});
      p.push_back({"Alto Flute", "Flute",
                   {{kModel, F}, {kTune, -5}, {kNoise, 1.1}, {kBright, -1}, {kAttack, 35}, {kRelease, 140},
                    {kVibDepth, 0.06}, {kVibRate, 4.8}, {kRoom, 0.2}}});
      p.push_back({"Bass Flute", "Flute",
                   {{kModel, F}, {kTune, -12}, {kNoise, 1.25}, {kEdge, 0.7}, {kAperture, 0.6}, {kAttack, 45},
                    {kRelease, 200}, {kVibDepth, 0.05}, {kVibRate, 4.4}, {kRoom, 0.25}}});
      p.push_back({"Overblown Jet", "Flute",
                   {{kModel, F}, {kBreath, 1.3}, {kEdge, 1.6}, {kNoise, 1.25}, {kTongueType, kFlutterTongue},
                    {kFlutter, 0.6}, {kGrowl, 0.3}, {kKick, 0.4}, {kVibDepth, 0.06}, {kVibRate, 6.0},
                    {kVoiceMode, kMonoMode}}});
      p.push_back({"Glass Ocarina", "Flute",
                   {{kModel, F}, {kTune, 12}, {kTube, kGlass}, {kHoles, kLattice3}, {kNoise, 0.3}, {kEdge, 0.4},
                    {kBreath, 0.85}, {kAttack, 35}, {kRelease, 200}, {kVibDepth, 0.04}, {kVibRate, 5.0},
                    {kRoom, 0.3}}});

      // Clarinets: a cylinder closed at the reed, so odd harmonics below the tone holes' cutoff; a
      // clarinettist plays with little or no vibrato.
      const double C = kClarinetModel;
      p.push_back({"Wooden Reed", "Clarinet", {{kModel, C}, {kVibDepth, 0.0}, {kRoom, 0.2}}});
      p.push_back({"Legato Clarinet", "Clarinet",
                   {{kModel, C}, {kVibDepth, 0.02}, {kVoiceMode, kLegatoMode}, {kGlide, 35}, {kRoom, 0.2}}});
      p.push_back({"Chalumeau", "Clarinet",
                   {{kModel, C}, {kHoles, kLattice5}, {kEdge, 0.7}, {kBreath, 0.9}, {kBright, -2}, {kVibDepth, 0.0},
                    {kAttack, 30}, {kRelease, 140}, {kRoom, 0.2}}});
      p.push_back({"Bass Clarinet", "Clarinet",
                   {{kModel, C}, {kTune, -12}, {kBarrel, 0.6}, {kEdge, 0.8}, {kNoise, 1.1}, {kAttack, 30},
                    {kRelease, 150}, {kVibDepth, 0.0}, {kRoom, 0.2}}});
      p.push_back({"Bright Eb", "Clarinet",
                   {{kModel, C}, {kTune, 5}, {kEdge, 1.3}, {kHoles, kLattice9}, {kAttack, 15}, {kVibDepth, 0.0}}});
      p.push_back({"Klezmer Cry", "Clarinet",
                   {{kModel, C}, {kEdge, 1.5}, {kBreath, 1.1}, {kSlide, 1.0}, {kVibDepth, 0.03}, {kVibPitch, 18},
                    {kVibRate, 5.8}, {kVibDelay, 200}, {kVoiceMode, kLegatoMode}, {kGlide, 90}, {kKick, 0.4}}});
      p.push_back({"Breathy Reed", "Clarinet",
                   {{kModel, C}, {kNoise, 1.45}, {kBreath, 0.85}, {kAperture, 0.35}, {kEdge, 0.5}, {kAttack, 50},
                    {kRelease, 220}, {kTongueType, kSoft}, {kVibDepth, 0.03}, {kBright, -2}, {kRoom, 0.25}}});
      p.push_back({"Low Rasp Reed", "Clarinet",
                   {{kModel, C}, {kTune, -12}, {kGrowl, 0.5}, {kEdge, 1.6}, {kBreath, 1.1}, {kKick, 0.45},
                    {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      // A felt bucket over the bell: the highs soaked up, a soft, covered tone.
      p.push_back({"Felt Clarinet", "Clarinet",
                   {{kModel, C}, {kMute, kBucket}, {kMuteAmount, 0.8}, {kBreath, 0.9}, {kVibDepth, 0.02},
                    {kRelease, 180}, {kRoom, 0.3}}});

      // Saxes: a reed on a truncated cone, every harmonic; Cone a little towards the cylinder fills in
      // the low register as the Iowa alto's. The vibrato is in the jaw, so in the pitch too. Tenor is a
      // fifth below the alto, baritone an octave, soprano a fourth above.
      const double S = kSaxModel;
      constexpr double kSaxCone = 0.35;
      p.push_back({"Clean Alto", "Sax",
                   {{kModel, S}, {kCone, kSaxCone}, {kVibDepth, 0.02}, {kVibPitch, 8}, {kVibRate, 5.2},
                    {kVibDelay, 400}, {kRoom, 0.2}}});
      p.push_back({"Smoke Tenor", "Sax",
                   {{kModel, S}, {kTune, -7}, {kCone, kSaxCone}, {kNoise, 1.2}, {kEdge, 1.1}, {kBright, -1},
                    {kVibType, kJaw}, {kVibDepth, 0.03}, {kVibPitch, 10}, {kVibRate, 5.0}, {kKick, 0.35}, {kRelease, 120}, {kRoom, 0.2}}});
      p.push_back({"Growl Alto", "Sax",
                   {{kModel, S}, {kCone, kSaxCone}, {kGrowl, 0.55}, {kEdge, 1.7}, {kBreath, 1.15}, {kNoise, 1.2},
                    {kVibDepth, 0.03}, {kVibPitch, 12}, {kVibRate, 5.6}, {kKick, 0.4}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Subtone Ballad", "Sax",
                   {{kModel, S}, {kTune, -7}, {kCone, kSaxCone}, {kBreath, 0.8}, {kNoise, 1.45}, {kEdge, 0.4},
                    {kAperture, 0.35}, {kBright, -4}, {kTongueType, kSoft}, {kAttack, 45}, {kRelease, 250},
                    {kVibDepth, 0.04}, {kVibPitch, 12}, {kVibRate, 4.8}, {kVibDelay, 500}, {kVoiceMode, kLegatoMode},
                    {kGlide, 60}, {kRoom, 0.25}}});
      p.push_back({"Soprano Line", "Sax",
                   {{kModel, S}, {kTune, 5}, {kCone, 0.4}, {kEdge, 0.9}, {kVibDepth, 0.03}, {kVibPitch, 9},
                    {kVibRate, 5.4}, {kVoiceMode, kLegatoMode}, {kGlide, 30}, {kRoom, 0.2}}});
      p.push_back({"Baritone", "Sax",
                   {{kModel, S}, {kTune, -12}, {kCone, kSaxCone}, {kEdge, 1.2}, {kKick, 0.35}, {kVibPitch, 6},
                    {kRoom, 0.2}}});
      p.push_back({"Bass Honk", "Sax",
                   {{kModel, S}, {kTune, -24}, {kEdge, 1.6}, {kBreath, 1.1}, {kKick, 0.5}, {kAttack, 12},
                    {kRelease, 80}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Gutbucket", "Sax",
                   {{kModel, S}, {kCone, kSaxCone}, {kRadiate, 0.9}, {kBreath, 1.25}, {kEdge, 2.0}, {kNoise, 1.4}, {kGrowl, 0.4},
                    {kKick, 0.6}, {kAttack, 8}, {kBright, 2}, {kVibPitch, 18}, {kVibRate, 6.5}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Flutter Tenor", "Sax",
                   {{kModel, S}, {kTune, -7}, {kCone, kSaxCone}, {kTongueType, kFlutterTongue}, {kFlutter, 0.6},
                    {kEdge, 1.3}, {kVibPitch, 8}, {kVoiceMode, kMonoMode}}});
      // A hat over the bell, as the swing bands' saxes did: dark and close.
      p.push_back({"Hat Alto", "Sax",
                   {{kModel, S}, {kCone, kSaxCone}, {kMute, kBucket}, {kMuteAmount, 0.7}, {kBright, -1},
                    {kVibDepth, 0.02}, {kVibPitch, 8}, {kVibDelay, 300}, {kRoom, 0.25}}});

      // Double reeds: a stiff reed beating on a narrow cone, its formant near 1.3 kHz; a quick, singing
      // vibrato. A fuller cone and the tone holes' cutoff a little up bring the middle register's
      // formant (H3 to H5) up towards the Iowa oboe's. Below the oboe's range (the cor anglais a fifth
      // down, the bassoon an octave, so the keyboard's middle is its tenor register) the plain cone
      // starts cleaner.
      const double O = kOboeModel;
      constexpr double kOboeCone = 0.6, kOboeCutoff = 1500;
      p.push_back({"Dusk Oboe", "Oboe",
                   {{kModel, O}, {kCone, kOboeCone}, {kHoleCutoff, kOboeCutoff}, {kVibDepth, 0.04}, {kVibPitch, 8},
                    {kVibRate, 5.4}, {kVibDelay, 300}, {kRoom, 0.2}}});
      p.push_back({"Pastoral", "Oboe",
                   {{kModel, O}, {kCone, kOboeCone}, {kHoleCutoff, kOboeCutoff}, {kEdge, 0.8}, {kNoise, 0.6},
                    {kVibDepth, 0.05}, {kVibPitch, 6}, {kVibRate, 5.6}, {kVoiceMode, kLegatoMode}, {kGlide, 25},
                    {kRoom, 0.25}}});
      p.push_back({"Oboe d'Amore", "Oboe",
                   {{kModel, O}, {kTune, -3}, {kCone, kOboeCone}, {kBright, -1.5},
                    {kVibDepth, 0.04}, {kVibPitch, 7}, {kVibRate, 5.2}, {kRelease, 130}, {kRoom, 0.2}}});
      p.push_back({"Cor Anglais", "Oboe",
                   {{kModel, O}, {kTune, -7}, {kHoles, kLattice5}, {kBright, -3}, {kKick, 0.15}, {kVibDepth, 0.04},
                    {kVibPitch, 7}, {kAttack, 30}, {kRelease, 160}, {kRoom, 0.25}}});
      p.push_back({"Bassoon", "Oboe",
                   {{kModel, O}, {kTune, -12}, {kKick, 0.2}, {kVibDepth, 0.03}, {kVibPitch, 5}, {kRoom, 0.2}}});
      p.push_back({"Contra Buzz", "Oboe",
                   {{kModel, O}, {kTune, -24}, {kTube, kMaple}, {kEdge, 2.0}, {kBreath, 1.15}, {kNoise, 1.2},
                    {kKick, 0.5}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}}});
      p.push_back({"Shawm", "Oboe",
                   {{kModel, O}, {kTube, kBoxwood}, {kHoles, kOpen}, {kEdge, 2.0}, {kBreath, 1.2}, {kNoise, 1.1},
                    {kKick, 0.5}, {kBright, 3}, {kVibDepth, 0.0}}});
      p.push_back({"Musette", "Oboe",
                   {{kModel, O}, {kTune, 5}, {kCone, kOboeCone}, {kEdge, 1.3}, {kKick, 0.4}, {kVibPitch, 10},
                    {kVibRate, 6.4}, {kVibDelay, 150}, {kVoiceMode, kLegatoMode}, {kGlide, 20}}});
      p.push_back({"Duduk Air", "Oboe",
                   {{kModel, O}, {kTune, -12}, {kTube, kBoxwood}, {kNoise, 1.5}, {kEdge, 0.3}, {kBreath, 0.8},
                    {kBright, -5}, {kTongueType, kSoft}, {kAttack, 100}, {kRelease, 450}, {kVibDepth, 0.05},
                    {kVibPitch, 14}, {kVibRate, 4.6}, {kVibDelay, 700}, {kVoiceMode, kLegatoMode}, {kGlide, 140},
                    {kRoom, 0.3}}});
      // A gourd over the reed, half covering it: the pungi's nasal cup.
      p.push_back({"Snake Charmer", "Oboe",
                   {{kModel, O}, {kMute, kCup}, {kMuteAmount, 0.6}, {kEdge, 1.6}, {kNoise, 1.15}, {kVibPitch, 25},
                    {kVibRate, 6.8}, {kVibDelay, 100}, {kVoiceMode, kLegatoMode}, {kGlide, 180}, {kKick, 0.4}}});

      // Brass: the lips on a flared horn, as the Iowa trumpet at the defaults; the vibrato is in the
      // lips (the pitch), and a fuller cone goes towards flugelhorn and horn. The mutes are the
      // jazz player's: straight (nasal, thin), cup (round, dark), Harmon (the buzzing stem), plunger
      // (a wah, here on the slide) and bucket (felt, soft).
      const double T = kTrumpetModel;
      p.push_back({"Brass Plume", "Trumpet",
                   {{kModel, T}, {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 5}, {kVibDelay, 450}, {kRoom, 0.2}}});
      p.push_back({"Fanfare", "Trumpet",
                   {{kModel, T}, {kEdge, 1.6}, {kBrass, 0.8}, {kKick, 0.4}, {kTongueType, kDouble}, {kAttack, 10}, {kBright, 2},
                    {kVibDepth, 0.0}, {kVibPitch, 0}, {kRoom, 0.25}}});
      p.push_back({"Straight Mute", "Trumpet",
                   {{kModel, T}, {kMute, kStraight}, {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 5},
                    {kRoom, 0.2}}});
      p.push_back({"Cup Mute", "Trumpet",
                   {{kModel, T}, {kMute, kCup}, {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 5}, {kRelease, 120},
                    {kRoom, 0.2}}});
      p.push_back({"Muted Night", "Trumpet",
                   {{kModel, T}, {kMute, kHarmon}, {kBreath, 0.9}, {kNoise, 1.15}, {kVibType, kLip}, {kVibDepth, 0.0},
                    {kVibPitch, 6}, {kRelease, 160}, {kVoiceMode, kLegatoMode}, {kRoom, 0.25}}});
      p.push_back({"Plunger Wah", "Trumpet",
                   {{kModel, T}, {kMute, kPlunger}, {kMuteAmount, 0.9}, {kSlide, 0.2}, {kGrowl, 0.25}, {kLevel, 3},
                    {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 6},
                    {routeParam(0, kRouteSource), kSrcSlide},
                    {routeParam(0, kRouteDestination), kDest<kMuteAmount>},
                    {routeParam(0, kRouteAmount), -0.8}}});
      p.push_back({"Bucket Ballad", "Trumpet",
                   {{kModel, T}, {kMute, kBucket}, {kBreath, 0.9}, {kLevel, 4}, {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 7},
                    {kVoiceMode, kLegatoMode}, {kGlide, 30}, {kRelease, 180}, {kRoom, 0.25}}});
      p.push_back({"Flugel Glow", "Trumpet",
                   {{kModel, T}, {kCone, 0.9}, {kBright, -2}, {kBreath, 0.9}, {kLevel, 4}, {kAttack, 35}, {kRelease, 200},
                    {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 6}, {kVoiceMode, kLegatoMode}, {kGlide, 30},
                    {kRoom, 0.25}}});
      p.push_back({"French Horn", "Trumpet",
                   {{kModel, T}, {kTune, -7}, {kCone, 0.8}, {kRadiate, 0.35}, {kBright, -4}, {kLevel, 3}, {kAttack, 40},
                    {kRelease, 250}, {kVibDepth, 0.0}, {kVibPitch, 0}, {kRoom, 0.3}}});
      p.push_back({"Trombone Slide", "Trumpet",
                   {{kModel, T}, {kTune, -12}, {kEdge, 1.2}, {kVoiceMode, kLegatoMode}, {kGlide, 160},
                    {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 8}, {kVibDelay, 500}, {kRoom, 0.2}}});
      p.push_back({"Piccolo Trumpet", "Trumpet",
                   {{kModel, T}, {kTune, 12}, {kEdge, 1.2}, {kKick, 0.35}, {kAttack, 10}, {kVibDepth, 0.0},
                    {kVibPitch, 4}, {kRoom, 0.25}}});

      const double A = kAlienModel;
      // The Alien: a voice that sings words, a syllable a note (its own, or what's typed on the
      // ALIEN tab), through a throat that can be made stranger: Accent (kAlienVoice), Spread, Flip,
      // Follow, Wobble (kAlienMorph), Shift and Sharp. Pace: 0 Free, 1 1/16, 2 1/8, 3 1/4, 4 1/2,
      // 5 1 Bar, 6 2 Bars. Word Length (kAlienOrder): 0 Short, 1 Mixed, 2 Long, 3 Chant.
      p.push_back({"Alien Speech", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.6}, {kAlienVoice, 0.3}, {kAlienDrift, 0.25}, {kAlienSeed, 7},
                    {kAttack, 30}, {kRelease, 200}, {kVibDepth, 0.03}, {kVoiceMode, kLegatoMode}, {kGlide, 60},
                    {kRoom, 0.3}}});
      // A plain human voice for your words: a syllable a note, nothing strange.
      p.push_back({"First Contact", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienVoice, 0.0}, {kAlienDrift, 0.1}, {kAlienBreathy, 0.2},
                    {kAlienLinger, 0.35}, {kAttack, 35}, {kRelease, 260}, {kVibDepth, 0.02}, {kVibPitch, 18},
                    {kVibDelay, 350}, {kRoom, 0.3}}});
      // A bowed string singing the words; MPE slide bends its accent.
      p.push_back({"Talking Bow", "Alien",
                   {{kModel, A}, {kLevel, 2}, {kAlienBow, 0.75}, {kAlienTalk, 0.5}, {kAlienVoice, 0.15}, {kBreath, 1.0},
                    {kAttack, 30}, {kRelease, 220}, {kVibDepth, 0.0}, {kVibPitch, 6}, {kAlienDrift, 0.15},
                    {kAlienSharp, 0.6}, {kVoiceMode, kLegatoMode}, {kGlide, 50},
                    {routeParam(0, kRouteSource), kSrcSlide},
                    {routeParam(0, kRouteDestination), kDest<kAlienVoice>},
                    {routeParam(0, kRouteAmount), 0.5}, {kRoom, 0.3}}});
      // Narrow resonances ringing over a low drone, as a throat singer's overtones, slowly turning.
      p.push_back({"Throat Singer", "Alien",
                   {{kModel, A}, {kLevel, -4}, {kAlienTalk, 0.3}, {kAlienPace, 6}, {kAlienLinger, 0.9}, {kTune, -12},
                    {kBreath, 1.05}, {kAlienMorph, 0.08}, {kAlienDrift, 0.3}, {kAlienSharp, 0.92}, {kAlienShift, -0.15},
                    {kAlienStrength, 0.4}, {kAlienGlide, 200}, {kAttack, 80}, {kRelease, 600}, {kVibDepth, 0.0},
                    {kVoiceMode, kLegatoMode}, {kGlide, 90}, {kRoom, 0.4}, {kRoomDecay, 3.5}}});
      p.push_back({"Xeno Choir", "Alien",
                   {{kModel, A}, {kLevel, -5}, {kAlienTalk, 0.45}, {kAlienPace, 4}, {kAlienLinger, 0.5}, {kBreath, 1.0},
                    {kAlienVoice, 0.45}, {kAlienFollow, 0.2}, {kAlienDrift, 0.4}, {kAlienBreathy, 0.4}, {kAttack, 200},
                    {kRelease, 1200}, {kVibDepth, 0.06}, {kVibRate, 3.8}, {kUnison, 3}, {kUnisonDetune, 12},
                    {kRoom, 0.5}, {kRoomDecay, 4.0}}});
      // A huge, squeezed throat over a heavy bow, a word every beat.
      p.push_back({"Tractor Beam", "Alien",
                   {{kModel, A}, {kAlienBow, 0.45}, {kAlienTalk, 0.5}, {kAlienPace, 3}, {kTune, -12}, {kBreath, 1.2},
                    {kEdge, 1.6}, {kAlienShift, -0.6}, {kAlienSpread, -0.5}, {kAlienVoice, 0.3}, {kAttack, 15},
                    {kRelease, 300}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}, {kGlide, 80}}});
      // A small, whining throat, its resonances pulled apart and following the note up.
      p.push_back({"Insect Hymn", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.75}, {kAlienLinger, 0.2}, {kTune, 12}, {kAlienShift, 0.45},
                    {kAlienSpread, 0.6}, {kAlienSharp, 0.8}, {kAlienFollow, 0.4}, {kAlienDrift, 0.3}, {kAlienSeed, 31},
                    {kAlienOrder, 0}, {kAttack, 25}, {kRelease, 350}, {kVibPitch, 12}, {kVibRate, 6.5},
                    {kVibDelay, 200}, {kRoom, 0.4}}});
      p.push_back({"Mothership", "Alien",
                   {{kModel, A}, {kLevel, -6}, {kAlienTalk, 0.35}, {kAlienPace, 5}, {kAlienLinger, 0.7}, {kTune, -12},
                    {kBreath, 1.1}, {kAlienShift, -0.4}, {kAlienFlip, 0.3}, {kAlienMorph, 0.06}, {kAlienDrift, 0.6},
                    {kAlienSharp, 0.65}, {kAlienGlide, 160}, {kAttack, 400}, {kRelease, 2000}, {kVibDepth, 0.04},
                    {kVibRate, 2.4}, {kRoom, 0.6}, {kRoomDecay, 6.0}}});
      // Gabbling, every vowel swirling, over a stiff scraped wire.
      p.push_back({"Signal Lost", "Alien",
                   {{kModel, A}, {kLevel, -2}, {kAlienBow, 0.3}, {kAlienTalk, 0.95}, {kAlienLinger, 0.1}, {kBreath, 1.3},
                    {kEdge, 1.8}, {kCone, 0.85}, {kFlutter, 0.2}, {kAlienMorph, 0.55}, {kAlienVoice, 0.7},
                    {kAlienDrift, 0.6}, {kAttack, 10}, {kRelease, 150}, {kVibPitch, 30}, {kVibRate, 7.5}, {kVibDelay, 0},
                    {kVoiceMode, kMonoMode}}});
      // A rolled tongue chopping a voice turned a quarter round.
      p.push_back({"Ion Flutter", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.5}, {kBreath, 1.1}, {kTongueType, kFlutterTongue}, {kFlutter, 0.5},
                    {kFlutterRate, 18}, {kAlienFlip, 0.45}, {kAlienSharp, 0.75}, {kAlienMorph, 0.1}, {kAttack, 20},
                    {kRelease, 250}, {kVibDepth, 0.0}, {kRoom, 0.3}}});
      // Strange throats: each of the panel's odd knobs taken a long way.
      // Every vowel where its opposite is.
      p.push_back({"Inside Out", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.55}, {kAlienFlip, 1.0}, {kAlienSharp, 0.55}, {kAlienDrift, 0.15},
                    {kAttack, 25}, {kRelease, 250}, {kVibDepth, 0.02}, {kVoiceMode, kLegatoMode}, {kGlide, 40},
                    {kRoom, 0.3}}});
      // Resonances pushed far apart and narrowed: a voice of glass.
      p.push_back({"Glass Mouth", "Alien",
                   {{kModel, A}, {kLevel, -3}, {kAlienTalk, 0.5}, {kAlienSpread, 0.85}, {kAlienSharp, 0.85},
                    {kAlienShift, 0.15}, {kAlienBreathy, 0.1}, {kAttack, 30}, {kRelease, 400}, {kVibPitch, 10},
                    {kVibRate, 5.5}, {kRoom, 0.45}, {kRoomDecay, 3.0}}});
      // Squeezed down to a hum, every vowel a muffled moan.
      p.push_back({"Muffled Moan", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.4}, {kAlienSpread, -0.85}, {kAlienShift, -0.3}, {kAlienBreathy, 0.35},
                    {kAlienLinger, 0.6}, {kAttack, 60}, {kRelease, 500}, {kVibDepth, 0.03}, {kRoom, 0.4}}});
      // Every resonance swinging on its own wave: a warbling crowd.
      p.push_back({"Warble Choir", "Alien",
                   {{kModel, A}, {kLevel, -4}, {kAlienTalk, 0.3}, {kAlienPace, 5}, {kAlienMorph, 0.35},
                    {kAlienVoice, 0.35}, {kAlienBreathy, 0.3}, {kAttack, 150}, {kRelease, 900}, {kUnison, 3},
                    {kUnisonDetune, 10}, {kVibDepth, 0.03}, {kRoom, 0.5}, {kRoomDecay, 3.5}}});
      // The throat follows the note all the way: tiny up high, a giant down low.
      p.push_back({"Tiny Giant", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.6}, {kAlienFollow, 1.0}, {kAlienVoice, 0.2}, {kAlienSharp, 0.6},
                    {kAttack, 20}, {kRelease, 250}, {kVibDepth, 0.02}, {kRoom, 0.3}}});

      // A living creature: rough, growling, a wet throat, an octave under the note in its rasp.
      p.push_back({"Swamp Creature", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.55}, {kAlienRough, 0.75}, {kAlienGrowl, 0.6}, {kAlienWet, 0.55},
                    {kAlienShift, -0.25}, {kAlienVoice, 0.3}, {kAlienBreathy, 0.35}, {kAttack, 30}, {kRelease, 300},
                    {kVibDepth, 0.0}, {kRoom, 0.3}}});

      // Talkers: the same voice set to speak in different ways, from crisp to drawled.
      // Crisp: each vowel snapped to, nothing wandering, no pitch slide: the clearest diction (play
      // it hard for s, t, k).
      p.push_back({"Crisp Diction", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.85}, {kAlienGlide, 15}, {kAlienLinger, 0.15}, {kAlienSharp, 0.55},
                    {kAlienDrift, 0.05}, {kAlienBreathy, 0.15}, {kAttack, 8}, {kRelease, 120}, {kVibDepth, 0.0},
                    {kRoom, 0.2}}});
      // Fast chatter, every 1/8 on a held note a new syllable.
      p.push_back({"Chatterbox", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.9}, {kAlienGlide, 15}, {kAlienPace, 2}, {kAlienLinger, 0.1},
                    {kAlienDrift, 0.1}, {kAlienOrder, 0}, {kTune, 12}, {kAttack, 10}, {kRelease, 150},
                    {kVibDepth, 0.0}, {kRoom, 0.25}}});
      // A machine reading out its words: every 1/4 a new syllable, snapped, dead steady.
      p.push_back({"Envoy Unit", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.75}, {kAlienGlide, 10}, {kAlienPace, 3}, {kAlienLinger, 0.0},
                    {kAlienSharp, 0.8}, {kAlienDrift, 0.0}, {kAlienBreathy, 0.0}, {kAlienSpread, 0.2}, {kAlienRough, 0.0},
                    {kAlienWet, 0.0}, {kAttack, 5},
                    {kRelease, 90}, {kVibDepth, 0.0}, {kVoiceMode, kMonoMode}, {kRoom, 0.15}}});
      // Slow and slurred: long glides between vowels, a slide between notes.
      p.push_back({"Slow Drawl", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.6}, {kAlienGlide, 180}, {kAlienPace, 3}, {kAlienLinger, 0.8},
                    {kAlienSharp, 0.55}, {kAlienDrift, 0.3}, {kTune, -12}, {kAttack, 60}, {kRelease, 400},
                    {kVibDepth, 0.04}, {kVoiceMode, kLegatoMode}, {kGlide, 120}, {kRoom, 0.35}}});
      // Breathy and close, the consonants' hiss up front.
      p.push_back({"Whisper Tongue", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.8}, {kAlienGlide, 20}, {kAlienLinger, 0.3}, {kAlienSharp, 0.4},
                    {kAlienBreathy, 0.9}, {kAlienDrift, 0.15}, {kNoise, 1.6}, {kBreath, 0.9}, {kAttack, 15},
                    {kRelease, 200}, {kVibDepth, 0.0}, {kRoom, 0.3}}});

      // Singers: a syllable a note (type the words, or let it sing its own), the vowel held steady
      // for the whole note and a diphthong's first vowel held as a singer holds it, with a vibrato
      // that comes in late.
      p.push_back({"Hymn Singer", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienGlide, 45}, {kAlienLinger, 0.8}, {kAlienSharp, 0.55},
                    {kAlienDrift, 0.1}, {kAlienBreathy, 0.2}, {kAttack, 45}, {kRelease, 350}, {kVibDepth, 0.02},
                    {kVibPitch, 22}, {kVibRate, 5.3}, {kVibDelay, 450}, {kVoiceMode, kLegatoMode}, {kGlide, 25},
                    {kRoom, 0.45}, {kRoomDecay, 2.8}}});
      p.push_back({"Ballad Voice", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienGlide, 55}, {kAlienLinger, 0.7}, {kAlienSharp, 0.5},
                    {kAlienDrift, 0.15}, {kAlienBreathy, 0.3}, {kTune, -12}, {kAttack, 40}, {kRelease, 400},
                    {kVibDepth, 0.03}, {kVibPitch, 28}, {kVibRate, 5.1}, {kVibDelay, 350}, {kRoom, 0.35},
                    {kRoomDecay, 2.2}}});
      // Plainer and quicker: words sung on the beat.
      p.push_back({"Folk Tenor", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienGlide, 35}, {kAlienLinger, 0.5}, {kAlienSharp, 0.55},
                    {kAlienDrift, 0.1}, {kAttack, 25}, {kRelease, 250}, {kVibDepth, 0.02}, {kVibPitch, 15},
                    {kVibRate, 5.6}, {kVibDelay, 500}, {kVoiceMode, kMonoMode}, {kRoom, 0.25}}});
      // A low, larger throat, the vowels slow and wide, a word chanted over and over.
      p.push_back({"Baritone Chant", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienGlide, 70}, {kAlienLinger, 0.9}, {kAlienSharp, 0.6},
                    {kAlienShift, -0.15}, {kAlienOrder, 3}, {kAlienDrift, 0.1}, {kTune, -12}, {kAttack, 70},
                    {kRelease, 600}, {kVibDepth, 0.02}, {kVibPitch, 18}, {kVibRate, 4.8}, {kVibDelay, 600},
                    {kVoiceMode, kLegatoMode}, {kGlide, 40}, {kRoom, 0.55}, {kRoomDecay, 4.0}}});
      // Soft and breathy, sung close.
      p.push_back({"Lullaby", "Alien",
                   {{kModel, A}, {kAlienTalk, 0.1}, {kAlienGlide, 80}, {kAlienLinger, 0.8}, {kAlienSharp, 0.45},
                    {kAlienDrift, 0.1}, {kAlienBreathy, 0.55}, {kBreath, 0.85}, {kAttack, 70}, {kRelease, 500},
                    {kVibDepth, 0.02}, {kVibPitch, 14}, {kVibRate, 4.9}, {kVibDelay, 500}, {kVoiceMode, kLegatoMode},
                    {kGlide, 30}, {kRoom, 0.4}, {kRoomDecay, 2.5}}});

      // Pads: the HYBRID voice as a pad under the instrument. Its Follow is low, so its envelope (not
      // the breath) plays it: it swells in behind the note and rings on after the breath stops.
      p.push_back({"Flute Halo", "Pad",
                   {{kModel, F}, {kVibDepth, 0.06}, {kRelease, 250}, {kHybridModel, kTriangle}, {kHybridMix, 0.4},
                    {kHybridAttack, 0.78}, {kHybridRelease, 0.82},
                    {kHybridFollow, 0.1}, {kHybridCutoff, 1600}, {kHybridFilterEnv, 0.2}, {kRoom, 0.4},
                    {kRoomDecay, 3.0}}});
      p.push_back({"Hollow Pad", "Pad",
                   {{kModel, C}, {kVibDepth, 0.02}, {kRelease, 200}, {kHybridModel, kSquare}, {kHybridMix, 0.4},
                    {kHybridAttack, 0.75}, {kHybridRelease, 0.85}, {kHybridFollow, 0.15}, {kHybridCutoff, 900},
                    {kHybridResonance, 0.3}, {kHybridFilterEnv, 0.35}, {kRoom, 0.4}}});
      p.push_back({"Section Pad", "Pad",
                   {{kModel, S}, {kCone, kSaxCone}, {kVibPitch, 6}, {kRelease, 200}, {kHybridModel, kSaw},
                    {kHybridMix, 0.35}, {kHybridDetune, 7}, {kHybridAttack, 0.72}, {kHybridRelease, 0.8},
                    {kHybridFollow, 0.2}, {kHybridCutoff, 1200}, {kHybridFilterEnv, 0.3}, {kRoom, 0.35}}});
      p.push_back({"Reed Choir", "Pad",
                   {{kModel, O}, {kCone, kOboeCone}, {kHoleCutoff, kOboeCutoff}, {kVibPitch, 5}, {kRelease, 250},
                    {kHybridModel, kSaw}, {kHybridMix, 0.35}, {kHybridOctave, -1}, {kHybridAttack, 0.75},
                    {kHybridRelease, 0.85}, {kHybridFollow, 0.1}, {kHybridCutoff, 1000}, {kRoom, 0.45},
                    {kRoomDecay, 3.5}}});
      p.push_back({"Brass Swell", "Pad",
                   {{kModel, T}, {kVibPitch, 4}, {kRelease, 200}, {kHybridModel, kSaw}, {kHybridMix, 0.45},
                    {kHybridAttack, 0.7}, {kHybridRelease, 0.8}, {kHybridFollow, 0.25}, {kHybridCutoff, 1400},
                    {kHybridResonance, 0.25}, {kHybridFilterEnv, 0.5}, {kRoom, 0.4}}});
      p.push_back({"Nebula", "Pad",
                   {{kModel, A}, {kBreath, 1.15}, {kRelease, 400}, {kVibDepth, 0.03}, {kHybridModel, kPulse},
                    {kHybridMix, 0.45}, {kHybridOctave, -1}, {kHybridAttack, 0.85}, {kHybridRelease, 0.9},
                    {kHybridFollow, 0.0}, {kHybridCutoff, 700}, {kHybridResonance, 0.45}, {kHybridFilterEnv, 0.6},
                    {kRoom, 0.6}, {kRoomDecay, 6.0}}});

      // Space: the delay and the reverb. Synced echoes follow the host's tempo; free ones are set in ms.
      p.push_back({"Echo Flute", "Space",
                   {{kModel, F}, {kVibDepth, 0.07}, {kVibRate, 5.2}, {kDelayOn, 1}, {kDelaySync, 7}, {kDelayMix, 0.3},
                    {kDelayFeedback, 0.45}, {kDelayTone, 0.6}, {kDelaySpread, 0.7}, {kRoom, 0.35}}});
      p.push_back({"Canyon Trumpet", "Space",
                   {{kModel, T}, {kVibType, kLip}, {kVibDepth, 0.0}, {kVibPitch, 5}, {kDelayOn, 1}, {kDelaySync, 0},
                    {kDelayTime, 0.705}, {kDelayMix, 0.35}, {kDelayFeedback, 0.35}, {kDelayTone, 0.55},
                    {kDelaySpread, 0.4}, {kRoom, 0.45}, {kRoomSize, 0.9}, {kRoomDecay, 4.5}}});
      p.push_back({"Dub Tenor", "Space",
                   {{kModel, S}, {kTune, -7}, {kCone, kSaxCone}, {kVibPitch, 8}, {kDelayOn, 1}, {kDelaySync, 9},
                    {kDelayMix, 0.35}, {kDelayFeedback, 0.55}, {kDelayTone, 0.4}, {kDelaySpread, 0.8},
                    {kRoom, 0.25}}});
      p.push_back({"Night Cathedral", "Space",
                   {{kModel, C}, {kVibDepth, 0.0}, {kRelease, 200}, {kRoom, 0.6}, {kRoomSize, 0.95},
                    {kRoomDecay, 7.0}, {kRoomDamp, 0.35}, {kRoomPreDelay, 40}}});
      p.push_back({"Slapback Bari", "Space",
                   {{kModel, S}, {kTune, -12}, {kCone, kSaxCone}, {kEdge, 1.3}, {kKick, 0.35}, {kDelayOn, 1},
                    {kDelaySync, 0}, {kDelayTime, 0.425}, {kDelayMix, 0.3}, {kDelayFeedback, 0.1},
                    {kDelaySpread, 0.2}, {kRoom, 0.15}}});
      p.push_back({"Star Signal", "Space",
                   {{kModel, A}, {kBreath, 1.25}, {kVibPitch, 6}, {kDelayOn, 1}, {kDelaySync, 3}, {kDelayMix, 0.4},
                    {kDelayFeedback, 0.7}, {kDelayTone, 0.8}, {kDelaySpread, 1.0}, {kRoom, 0.5}, {kRoomDecay, 5.0}}});

      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

} // namespace jbreath

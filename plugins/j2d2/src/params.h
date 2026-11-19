#pragma once

// J2D2's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace j2d2 {

// The droid's eight kinds of sound, measured from real droid clips (see the README).
enum Syllable { kChirp, kBell, kGlide, kSteps, kWarble, kBuzz, kRing, kTweet, kSyllableCount };

// The drone's five layers.
enum Layer { kLayerTone, kLayerPartials, kLayerAir, kLayerMetal, kLayerSub, kLayerCount };

enum ParamId : clap_id {
   // VOICE: the DROID <-> DRONE morph and the personality.
   kMorph,
   kEnergy,
   kFeeling,
   kPressToEnergy,
   kSlideToFeeling,
   kVelToFeeling,
   kRandomMood,
   // How the droid voice plays.
   kVelResponse,
   kPressResponse,
   kTalk,
   kPop,
   kRegister,
   kKeyTrack,
   // The syllable mix: each kind of sound, 0 (never) .. 2 (twice as often as the mood picks it).
   kSyllable0,
   kSyllableLast = kSyllable0 + kSyllableCount - 1,
   // DRONES: the five layers' levels.
   kLayer0,
   kLayerLast = kLayer0 + kLayerCount - 1,
   kPartialsAtonal,
   kMetalAtonal,
   // MOTION.
   kEvolve,
   kDrift,
   kSpread,
   kBreath,
   kTuning,
   // The drone's amp envelope (seconds, sustain 0..1).
   kAttack,
   kDecay,
   kSustain,
   kRelease,
   // Two envelopes and two shape LFOs, each sent to one destination.
   kEnv1Attack,
   kEnv1Decay,
   kEnv1Sustain,
   kEnv1Release,
   kEnv1Dest,
   kEnv1Amount,
   kEnv2Attack,
   kEnv2Decay,
   kEnv2Sustain,
   kEnv2Release,
   kEnv2Dest,
   kEnv2Amount,
   kLfo1Rate,
   kLfo1Shape,
   kLfo1Dest,
   kLfo1Amount,
   kLfo2Rate,
   kLfo2Shape,
   kLfo2Dest,
   kLfo2Amount,
   // SPACE.
   kSpaceSize,
   kSpaceMix,
   kSpaceDamp,
   // MASTER.
   kMasterLevel,
   kWidth,
   kPoly,
   kVoices,
   // Added in 0.2.0.
   kLength,
   kPerKey,
   kParamCount
};

// Where an envelope or LFO goes.
enum Dest {
   kDestNone,
   kDestMorph,
   kDestEnergy,
   kDestFeeling,
   kDestTone,
   kDestPartials,
   kDestAir,
   kDestMetal,
   kDestSub,
   kDestPartialsAtonal,
   kDestMetalAtonal,
   kDestCutoff,
   kDestEvolve,
   kDestDrift,
   kDestPan,
   kDestCount
};

enum LfoShape { kShapeSine, kShapeTriangle, kShapeSaw, kShapeSquare, kShapeSampleHold, kShapeSmooth, kShapeCount };

constexpr int kMaxVoices = 8;
constexpr int kModSources = 4; // ENV 1, ENV 2, LFO 1, LFO 2

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kStepped = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kMorph, "Droid/Drone", "Voice", 0.0, 1.0, 0.0, kMod},
   {kEnergy, "Energy", "Voice", -1.0, 1.0, 0.0, kMod},
   {kFeeling, "Feeling", "Voice", -1.0, 1.0, 0.0, kMod},
   {kPressToEnergy, "Press to Energy", "Voice", -1.0, 1.0, 0.45, kMod},
   {kSlideToFeeling, "Slide to Feeling", "Voice", -1.0, 1.0, 0.6, kMod},
   {kVelToFeeling, "Velocity to Feeling", "Voice", -1.0, 1.0, 0.2, kMod},
   {kRandomMood, "Random Mood", "Voice", 0.0, 1.0, 0.25, kMod},
   {kVelResponse, "Velocity", "Voice", 0.0, 1.0, 0.6, kMod},
   {kPressResponse, "Pressure", "Voice", 0.0, 1.0, 0.5, kMod},
   {kTalk, "Talk", "Voice", 0.0, 1.0, 0.5, kMod},
   {kPop, "Pop", "Voice", 0.0, 20.0, 9.0, kMod},
   {kRegister, "Register", "Voice", -24.0, 24.0, 0.0, kMod},
   {kKeyTrack, "Key Track", "Voice", 0.0, 1.0, 0.5, kMod},
   {kSyllable0 + kChirp, "Chirp", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kBell, "Bell", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kGlide, "Glide", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kSteps, "Steps", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kWarble, "Warble", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kBuzz, "Buzz", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kRing, "Ring", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kSyllable0 + kTweet, "Tweet", "Syllables", 0.0, 2.0, 1.0, kMod},
   {kLayer0 + kLayerTone, "Tone", "Drones", 0.0, 1.0, 0.7, kMod},
   {kLayer0 + kLayerPartials, "Partials", "Drones", 0.0, 1.0, 0.6, kMod},
   {kLayer0 + kLayerAir, "Air", "Drones", 0.0, 1.0, 0.35, kMod},
   {kLayer0 + kLayerMetal, "Metal", "Drones", 0.0, 1.0, 0.3, kMod},
   {kLayer0 + kLayerSub, "Sub", "Drones", 0.0, 1.0, 0.55, kMod},
   {kPartialsAtonal, "Partials Atonal", "Drones", 0.0, 1.0, 0.2, kMod},
   {kMetalAtonal, "Metal Atonal", "Drones", 0.0, 1.0, 0.2, kMod},
   {kEvolve, "Evolve", "Motion", 0.0, 2.0, 1.0, kMod},
   {kDrift, "Drift", "Motion", 0.0, 2.0, 1.0, kMod},
   {kSpread, "Spread", "Motion", 0.0, 1.0, 0.8, kMod},
   {kBreath, "Breath", "Motion", 0.0, 1.0, 0.5, kMod},
   {kTuning, "Tuning", "Motion", 0.0, 1.0, 0.0, kChoice},
   {kAttack, "Attack", "Amp", 0.005, 10.0, 2.5, kMod},
   {kDecay, "Decay", "Amp", 0.01, 10.0, 1.0, kMod},
   {kSustain, "Sustain", "Amp", 0.0, 1.0, 1.0, kMod},
   {kRelease, "Release", "Amp", 0.01, 15.0, 4.0, kMod},
   {kEnv1Attack, "Env 1 Attack", "Env 1", 0.005, 20.0, 4.0, kMod},
   {kEnv1Decay, "Env 1 Decay", "Env 1", 0.01, 20.0, 6.0, kMod},
   {kEnv1Sustain, "Env 1 Sustain", "Env 1", 0.0, 1.0, 0.4, kMod},
   {kEnv1Release, "Env 1 Release", "Env 1", 0.01, 20.0, 5.0, kMod},
   {kEnv1Dest, "Env 1 Dest", "Env 1", 0.0, kDestCount - 1, kDestPartials, kChoice},
   {kEnv1Amount, "Env 1 Amount", "Env 1", -1.0, 1.0, 0.3, kMod},
   {kEnv2Attack, "Env 2 Attack", "Env 2", 0.005, 20.0, 0.05, kMod},
   {kEnv2Decay, "Env 2 Decay", "Env 2", 0.01, 20.0, 1.2, kMod},
   {kEnv2Sustain, "Env 2 Sustain", "Env 2", 0.0, 1.0, 0.0, kMod},
   {kEnv2Release, "Env 2 Release", "Env 2", 0.01, 20.0, 1.0, kMod},
   {kEnv2Dest, "Env 2 Dest", "Env 2", 0.0, kDestCount - 1, kDestAir, kChoice},
   {kEnv2Amount, "Env 2 Amount", "Env 2", -1.0, 1.0, 0.0, kMod},
   {kLfo1Rate, "LFO 1 Rate", "LFO 1", 0.01, 20.0, 1.0 / 12.0, kMod},
   {kLfo1Shape, "LFO 1 Shape", "LFO 1", 0.0, kShapeCount - 1, kShapeSine, kChoice},
   {kLfo1Dest, "LFO 1 Dest", "LFO 1", 0.0, kDestCount - 1, kDestPartialsAtonal, kChoice},
   {kLfo1Amount, "LFO 1 Amount", "LFO 1", -1.0, 1.0, 0.45, kMod},
   {kLfo2Rate, "LFO 2 Rate", "LFO 2", 0.01, 20.0, 1.0 / 7.5, kMod},
   {kLfo2Shape, "LFO 2 Shape", "LFO 2", 0.0, kShapeCount - 1, kShapeSmooth, kChoice},
   {kLfo2Dest, "LFO 2 Dest", "LFO 2", 0.0, kDestCount - 1, kDestMetalAtonal, kChoice},
   {kLfo2Amount, "LFO 2 Amount", "LFO 2", -1.0, 1.0, 0.5, kMod},
   {kSpaceSize, "Space Size", "Space", 0.0, 1.0, 0.5, kMod},
   {kSpaceMix, "Space Mix", "Space", 0.0, 1.0, 0.25, kMod},
   {kSpaceDamp, "Space Damp", "Space", 0.0, 1.0, 0.4, kMod},
   {kMasterLevel, "Master", "Master", -60.0, 6.0, -3.0, kMod},
   {kWidth, "Width", "Master", 0.0, 2.0, 1.0, kMod},
   {kPoly, "Poly", "Master", 0.0, 1.0, 0.0, kChoice},
   {kVoices, "Voices", "Master", 1.0, kMaxVoices, kMaxVoices, kStepped},
   {kLength, "Length", "Voice", 0.03, 4.0, 0.25, kMod},
   {kPerKey, "Per Key", "Voice", 0.0, 1.0, 0.0, kChoice},
};

// Choice names, in value order.
inline const char *const kTuningNames[] = {"Just", "Equal"};
inline const char *const kPolyNames[] = {"Poly", "Mono"};
// What one key press speaks: one syllable, or a whole phrase.
inline const char *const kPerKeyNames[] = {"Syllable", "Phrase"};
inline const char *const kDestNames[] = {"None",    "Droid/Drone", "Energy", "Feeling",         "Tone",
                                         "Partials", "Air",        "Metal",  "Sub",             "Partials Atonal",
                                         "Metal Atonal", "Cutoff", "Evolve", "Drift",           "Pan"};
inline const char *const kShapeNames[] = {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth"};
inline const char *const kSyllableNames[] = {"Chirp", "Bell", "Glide", "Steps", "Warble", "Buzz", "Ring", "Tweet"};
inline const char *const kLayerNames[] = {"Tone", "Partials", "Air", "Metal", "Sub"};

inline bool isDest(clap_id id) { return id == kEnv1Dest || id == kEnv2Dest || id == kLfo1Dest || id == kLfo2Dest; }
inline bool isShape(clap_id id) { return id == kLfo1Shape || id == kLfo2Shape; }
inline bool isSeconds(clap_id id) {
   return id == kLength || (id >= kAttack && id <= kRelease && id != kSustain) ||
          (id >= kEnv1Attack && id <= kEnv1Release && id != kEnv1Sustain) ||
          (id >= kEnv2Attack && id <= kEnv2Release && id != kEnv2Sustain);
}

inline const char *const *choiceNames(clap_id id, int &count) {
   if (id == kTuning) {
      count = 2;
      return kTuningNames;
   }
   if (id == kPoly) {
      count = 2;
      return kPolyNames;
   }
   if (id == kPerKey) {
      count = 2;
      return kPerKeyNames;
   }
   if (isDest(id)) {
      count = kDestCount;
      return kDestNames;
   }
   if (isShape(id)) {
      count = kShapeCount;
      return kShapeNames;
   }
   count = 0;
   return nullptr;
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   if (isSeconds(id)) {
      if (value < 0.9995)
         std::snprintf(out, size, "%.0f ms", value * 1000.0);
      else
         std::snprintf(out, size, "%.2f s", value);
      return;
   }
   switch (id) {
   case kMasterLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kRegister:
      std::snprintf(out, size, "%+d st", static_cast<int>(std::lround(value)));
      break;
   case kPop:
      std::snprintf(out, size, "%.1f /s", value);
      break;
   case kLfo1Rate:
   case kLfo2Rate:
      // Slow rates read as a period. The switch sits below 1 Hz so the shortest period shown is
      // "1.1 s", which parses back to a rate that still shows as a period.
      if (value < 0.95)
         std::snprintf(out, size, "%.1f s", 1.0 / value);
      else
         std::snprintf(out, size, "%.2f Hz", value);
      break;
   case kVoices:
      std::snprintf(out, size, "%d", static_cast<int>(std::lround(value)));
      break;
   case kEnergy:
   case kFeeling:
   case kPressToEnergy:
   case kSlideToFeeling:
   case kVelToFeeling:
   case kEnv1Amount:
   case kEnv2Amount:
   case kLfo1Amount:
   case kLfo2Amount:
      std::snprintf(out, size, "%+.0f%%", value * 100.0);
      break;
   default:
      // Everything else is a 0..1 (or 0..2) amount, shown in percent.
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   }
}

// The reverse of formatParam: choice names, or numbers in the units it prints (percent for amounts,
// seconds or "ms" for times, dB, semitones, Hz or "s" for LFO rates). NaN and infinity are refused.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (*text == ' ')
      ++text;
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      for (int i = 0; i < count; ++i)
         if (!std::strcmp(text, names[i])) {
            value = i;
            return true;
         }
   }
   double v = 0.0;
   const char *end = nullptr;
   if (!jclap::core::parseNumber(text, v, &end))
      return false;
   while (*end == ' ')
      ++end;
   if (choiceNames(id, count) || id == kMasterLevel || id == kRegister || id == kPop || id == kVoices)
      value = v;
   else if (isSeconds(id))
      value = (end[0] == 'm' && end[1] == 's') ? v / 1000.0 : v;
   else if (id == kLfo1Rate || id == kLfo2Rate)
      value = (*end == 's' && v > 0.0) ? 1.0 / v : v;
   else
      value = v / 100.0;
   return std::isfinite(value);
}

} // namespace j2d2

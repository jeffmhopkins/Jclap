#pragma once

// JSynth's parameters: a global block, then two species (parts A and B) with the same layout. A part's
// params are written once, as offsets into a part (PartParam and the per-module enums below), and
// partId() turns an offset into the host's id for part A or B. Each module (oscillator, filter,
// envelope, LFO, route) is a block of ids with room left to grow, so ids can stay put once released:
// a new param goes in its block's spare room, never between two that exist.
//
// The ids have gaps, so kParams() is the list of params that exist and paramIndex() finds one by id.
//
// Every note is a creature, and each part's creatures flock. Modulation is a list of routes per part
// (source, destination, amount), which the panel's MAP mode writes: there is no matrix page.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace jsynth {

constexpr int kPartCount = 2;
constexpr int kOscCount = 3;
constexpr int kFilterCount = 2;
constexpr int kEnvCount = 3;
// Wingbeat and Wind move the flock as well as modulating; LFO 1 and LFO 2 only modulate.
constexpr int kMotionCount = 2;
constexpr int kLfoCount = 4;
// Routes per part: a pool the panel shows only the used ones of, so it reads as an open-ended list.
constexpr int kRouteCount = 64;
constexpr int kMacroCount = 4;
// The touch dimensions, in MPE order: velocity, pressure, bend, CC 74 and release velocity.
constexpr int kTouchCount = 5;
constexpr int kMaxPolyphony = 16;

// ---- ids ----

// Global params: how notes reach the parts, the touch response, macros and the output.
enum GlobalParam : clap_id {
   kPlayMode,
   kSplitKey,
   kVelocitySplit,
   kCrossfadeSource,
   kQuality,
   kMasterLevel,
   kManagerBendRange,
   kGlideRounding,
   kRoundingTime,
   kMacro1,
   // Each touch dimension's response curve and smoothing.
   kTouchCurve1 = kMacro1 + kMacroCount,
   kTouchSmooth1 = kTouchCurve1 + kTouchCount,
   // The sky both species fly in.
   kSkySize = kTouchSmooth1 + kTouchCount,
   kSkyWalls,
   // The output's stereo width: 0 is mono, 100% as is, 200% wider.
   kOutputWidth,
   kGlobalEnd
};

constexpr clap_id kPartBase = 64;
constexpr clap_id kPartStride = 1024;
constexpr clap_id kParamIdEnd = kPartBase + kPartCount * kPartStride;

// Offsets within a part. The voice settings come first, then one block per module.
enum PartParam : clap_id {
   kPartLevel,
   kPartPan,
   kPartTranspose,
   kPartFine,
   kKeyLow,
   kKeyHigh,
   kPolyphony,
   kVoiceMode,
   kGlideTime,
   kGlideMode,
   kBendRange,
   kDrift,
   kVelocityToAmp,
   kPartOutput,
   // How far a creature's place left to right in the sky pans its voice.
   kSkyPan,

   kOscBase = 16,
   kOscStride = 16,
   kFmBase = kOscBase + kOscCount * kOscStride, // 64
   kMixBase = kFmBase + 16,                      // 80
   kVoiceTailBase = kMixBase + 16,               // 96: drive and filter routing
   kFilterBase = kVoiceTailBase + 16,            // 112
   kFilterStride = 16,
   kEnvBase = kFilterBase + kFilterCount * kFilterStride, // 144
   kEnvStride = 16,
   kLfoBase = kEnvBase + kEnvCount * kEnvStride, // 192
   kLfoStride = 16,
   kFlockBase = kLfoBase + kLfoCount * kLfoStride, // 256
   kRouteBase = kFlockBase + 32,                   // 288
   kRouteStride = 4,
   kPartEnd = kRouteBase + kRouteCount * kRouteStride // 544, inside the 1024 stride
};

enum OscParam : clap_id {
   kOscWave,
   kOscShape,
   kOscPitchMode,
   kOscCoarse,
   kOscFine,
   kOscRatio,
   kOscFixed,
   kOscSync,
   kOscRetrigger,
   kOscPhase,
   kOscBend,
   kOscKeyTrack,
};

// The FM grid: kFmCell + from * 3 + to, where a cell on the diagonal is that oscillator's feedback.
enum FmParam : clap_id {
   kFmCell = 0,
   kFmType = kOscCount * kOscCount,
};

enum MixParam : clap_id {
   kMixLevel = 0, // one per oscillator
   kMixPan = kMixLevel + kOscCount,
   kNoiseLevel = kMixPan + kOscCount,
   kNoiseColor,
};

enum VoiceTailParam : clap_id {
   kDriveType,
   kDriveAmount,
   kFilterRouting,
   kFilterBalance,
};

enum FilterParam : clap_id {
   kFilterType,
   kFilterSlope,
   kFilterCutoff,
   kFilterResonance,
   kFilterDrive,
   kFilterKeyTrack,
};

enum EnvParam : clap_id {
   kEnvDelay,
   kEnvAttack,
   kEnvHold,
   kEnvDecay,
   kEnvSustain,
   kEnvRelease,
   kEnvAttackCurve,
   kEnvDecayCurve,
   kEnvReleaseCurve,
   kEnvLoop,
   kEnvVelocity,
};

// The first two LFOs move the flock: the wingbeat, one per creature, and the wind, one gust for the
// whole sky. The other two, LFO 1 and LFO 2, are plain LFOs, sources for routes only.
enum LfoParam : clap_id {
   kLfoShape,
   kLfoRate,
   kLfoSync,
   kLfoDivision,
   kLfoTrigger,
   kLfoPhase,
   kLfoDelay,
   kLfoPolarity,
   // Wingbeat and Wind only: how hard it moves the creatures. A wingbeat bobs each one, the wind
   // pushes them all one way.
   kLfoPush,
};

// How a species flies, and what flying near another creature does to its sound.
enum FlockParam : clap_id {
   kBehaviour,
   kCohesion,   // pull toward the middle of the neighbours
   kAlignment,  // turn to fly the way the neighbours fly
   kSeparation, // keep clear of the nearest
   kSpeed,
   kWander,
   kLure,       // how hard a finger pulls its own creature
   kRange,      // how near counts as a neighbour
   kNeighbourFm,  // the nearest neighbour's Osc 1 into this creature's Osc 2
   kTunePull,     // creatures flying the same way drift toward pure intervals
   kBite,         // ring mod while a hunter has caught this creature
   kTailLength,   // how long the ink tail lasts
   kTailSpread,   // how far the ink blooms as it ages
   kWake,         // how much flying through another creature's ink stirs this one
};

// A route: kRouteSource (Off for none), kRouteDestination (an index into destinations()) and its
// signed depth.
enum RouteParam : clap_id {
   kRouteSource,
   kRouteDestination,
   kRouteAmount,
};

// A part param's offset, for module `index`.
constexpr clap_id oscParam(int osc, OscParam p) { return kOscBase + osc * kOscStride + p; }
constexpr clap_id fmCell(int from, int to) { return kFmBase + kFmCell + from * kOscCount + to; }
constexpr clap_id mixParam(MixParam p, int osc = 0) { return kMixBase + p + osc; }
constexpr clap_id tailParam(VoiceTailParam p) { return kVoiceTailBase + p; }
constexpr clap_id filterParam(int filter, FilterParam p) { return kFilterBase + filter * kFilterStride + p; }
constexpr clap_id envParam(int env, EnvParam p) { return kEnvBase + env * kEnvStride + p; }
constexpr clap_id lfoParam(int lfo, LfoParam p) { return kLfoBase + lfo * kLfoStride + p; }
constexpr clap_id flockParam(FlockParam p) { return kFlockBase + p; }
constexpr clap_id routeParam(int route, RouteParam p) { return kRouteBase + route * kRouteStride + p; }

// The host's id for a part param.
constexpr clap_id partId(int part, clap_id offset) { return kPartBase + part * kPartStride + offset; }
// Which part an id belongs to (-1 for a global), and its offset there.
constexpr int partOf(clap_id id) { return id < kPartBase || id >= kParamIdEnd ? -1 : static_cast<int>((id - kPartBase) / kPartStride); }
constexpr clap_id offsetOf(clap_id id) { return (id - kPartBase) % kPartStride; }

// ---- choices ----

inline const std::vector<std::string> kPlayModeNames = {"Single", "Layer", "Split", "MPE Zones", "Velocity", "Expression"};
inline const std::vector<std::string> kCrossfadeNames = {"Slide", "Press"};
inline const std::vector<std::string> kQualityNames = {"1x", "2x", "4x"};
inline const std::vector<std::string> kVoiceModeNames = {"Poly", "Mono", "Legato"};
inline const std::vector<std::string> kGlideModeNames = {"Always", "Legato"};
inline const std::vector<std::string> kOutputNames = {"Main", "Own"};
inline const std::vector<std::string> kWaveNames = {"Sine",      "Triangle",   "Saw",      "Square",       "Pulse",
                                                    "Half Sine", "Abs Sine",   "Quarter",  "Alt Sine",     "Alt Half",
                                                    "Camel",     "Alt Camel",  "White",    "Pink"};
inline const std::vector<std::string> kPitchModeNames = {"Note", "Ratio", "Fixed"};
inline const std::vector<std::string> kOnOffNames = {"Off", "On"};
inline const std::vector<std::string> kFmTypeNames = {"Phase", "Exponential"};
inline const std::vector<std::string> kDriveTypeNames = {"Soft", "Hard", "Tube"};
inline const std::vector<std::string> kRoutingNames = {"Serial", "Parallel", "Split"};
inline const std::vector<std::string> kFilterTypeNames = {"Low Pass", "High Pass", "Band Pass", "Notch",
                                                          "Peak",     "Ladder",    "Comb +",    "Comb -"};
inline const std::vector<std::string> kSlopeNames = {"12 dB", "24 dB", "36 dB", "48 dB"};
inline const std::vector<std::string> kLoopNames = {"Off", "Held", "Free"};
inline const std::vector<std::string> kLfoShapeNames = {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth"};
inline const std::vector<std::string> kDivisionNames = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8",  "1/4T",
                                                        "1/8D", "1/4",   "1/2T", "1/4D", "1/2",   "1/1", "2/1", "4/1"};
inline const std::vector<std::string> kTriggerNames = {"Retrigger", "Free", "Global"};
inline const std::vector<std::string> kPolarityNames = {"Bipolar", "Unipolar"};
// Shoal flies with its own kind, Scatter keeps apart, Hunt chases the other species and Flee runs
// from it.
inline const std::vector<std::string> kBehaviourNames = {"Shoal", "Scatter", "Hunt", "Flee"};
inline const std::vector<std::string> kWallNames = {"Soft", "Wrap", "Open"};
// Route sources. Off is no route. Speed to Sky Y are the flock's own: how fast this creature flies, how
// many neighbours it has, how near the nearest is, which way it heads, whether a hunter has it, how
// much ink it is flying through, and where it is in the sky (left to right, and bottom to top).
inline const std::vector<std::string> kSourceNames = {
    "Off",   "Env 1",   "Env 2",    "Env 3",   "Wingbeat", "Wind",   "LFO 1",  "LFO 2",  "Strike",    "Press",
    "Glide", "Slide",   "Lift",     "Key",     "Speed",    "Crowd",  "Nearest",   "Heading",
    "Caught", "Wake",   "Sky X",    "Sky Y",   "Random",   "Mod Wheel", "Macro 1", "Macro 2", "Macro 3", "Macro 4"};
enum Source {
   kSourceOff,
   kSourceEnv1,
   kSourceWingbeat = kSourceEnv1 + kEnvCount,
   kSourceWind,
   kSourceLfo1,
   kSourceStrike = kSourceLfo1 + kLfoCount - kMotionCount,
   kSourceKey = kSourceStrike + kTouchCount,
   kSourceSpeed,
   kSourceCrowd,
   kSourceNearest,
   kSourceHeading,
   kSourceCaught,
   kSourceWake,
   kSourceSkyX,
   kSourceSkyY,
   kSourceRandom,
   kSourceModWheel,
   kSourceMacro1,
   kSourceCount = kSourceMacro1 + kMacroCount
};
inline const char *const kTouchNames[kTouchCount] = {"Strike", "Press", "Glide", "Slide", "Lift"};

// ---- value mappings ----

// Params that sweep a wide range run 0..1 so a knob moves evenly by ear.
// Cutoff: 20 Hz times 1000 to that power. LFO rate: 0.02 Hz times 2500 (to 50 Hz). Fixed pitch: 1 Hz
// to 10 kHz. Times: 0 ms to 20 s, about 100 ms in the middle.
inline double cutoffHz(double v) { return 20.0 * std::pow(1000.0, v); }
inline double cutoffValue(double hz) { return std::log(hz / 20.0) / std::log(1000.0); }
inline double lfoHz(double v) { return 0.02 * std::pow(2500.0, v); }
inline double lfoValue(double hz) { return std::log(hz / 0.02) / std::log(2500.0); }
inline double fixedHz(double v) { return std::pow(10000.0, v); }
inline double fixedValue(double hz) { return std::log(hz) / std::log(10000.0); }
inline double timeMs(double v) { return 0.5 * std::pow(40000.0, v) - 0.5; }
inline double timeValue(double ms) { return std::log((ms + 0.5) / 0.5) / std::log(40000.0); }

// How a param prints and parses.
enum class Unit {
   Choice,
   Percent,        // 0..1 shown as 0..100%
   Bipolar,        // -1..1 shown as -100..+100%
   Semitones,      // signed, stepped
   Range,          // a bend range in semitones
   Cents,
   Ratio,
   CutoffHz,
   LfoHz,
   FixedHz,
   Time,
   Decibels,
   Note,
   Count,
   Velocity,
   Degrees,
   Curve,          // -1..1, shown as a signed number
};

struct Param {
   jclap::core::ParamDef def;
   Unit unit;
   const std::vector<std::string> *choices = nullptr;
   // Without the part's letter: "Osc 1 Coarse". The panel's labels and the route destinations
   // use it.
   const char *shortName;
};

// Every param can be automated and modulated by the host, and most per voice (by note id, key or
// channel), as Bitwig's polyphonic modulators do. Play mode, route sources and other settings
// with no per-voice meaning are global only.
constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = kStepped | CLAP_PARAM_IS_ENUM;
constexpr auto kGlobalStepped = kGlobalMod | CLAP_PARAM_IS_STEPPED;
constexpr auto kGlobalChoice = kGlobalStepped | CLAP_PARAM_IS_ENUM;

namespace detail {

struct Builder {
   std::vector<Param> params;
   std::deque<std::string> text; // names stay put as the list grows

   const char *keep(std::string s) { return text.emplace_back(std::move(s)).c_str(); }

   void add(clap_id id, const std::string &name, const std::string &module, double min, double max, double def,
            clap_param_info_flags flags, Unit unit, const std::vector<std::string> *choices = nullptr) {
      const char *shortName = keep(name);
      const int part = partOf(id);
      const char *fullName = part < 0 ? shortName : keep(std::string(part == 0 ? "A " : "B ") + name);
      const char *fullModule = keep(part < 0 ? module : std::string(part == 0 ? "Part A/" : "Part B/") + module);
      params.push_back({{id, fullName, fullModule, min, max, def, flags}, unit, choices, shortName});
   }
   void choice(clap_id id, const std::string &name, const std::string &module, const std::vector<std::string> &names,
               int def, clap_param_info_flags flags) {
      add(id, name, module, 0.0, static_cast<double>(names.size() - 1), def, flags, Unit::Choice, &names);
   }
};

// What a route can move: every continuous part param a voice can use, as part offsets. MAP mode
// can map any knob showing one of these.
inline std::vector<clap_id> buildDestinations() {
   std::vector<clap_id> d = {kPartLevel, kPartPan, kSkyPan, kPartFine, kGlideTime, kDrift};
   for (int o = 0; o < kOscCount; ++o)
      for (OscParam p : {kOscShape, kOscCoarse, kOscFine, kOscRatio, kOscFixed, kOscPhase})
         d.push_back(oscParam(o, p));
   for (int from = 0; from < kOscCount; ++from)
      for (int to = 0; to < kOscCount; ++to)
         d.push_back(fmCell(from, to));
   for (int o = 0; o < kOscCount; ++o)
      for (MixParam p : {kMixLevel, kMixPan})
         d.push_back(mixParam(p, o));
   for (MixParam p : {kNoiseLevel, kNoiseColor})
      d.push_back(mixParam(p));
   for (VoiceTailParam p : {kDriveAmount, kFilterBalance})
      d.push_back(tailParam(p));
   for (int f = 0; f < kFilterCount; ++f)
      for (FilterParam p : {kFilterCutoff, kFilterResonance, kFilterDrive, kFilterKeyTrack})
         d.push_back(filterParam(f, p));
   for (int e = 0; e < kEnvCount; ++e)
      for (EnvParam p : {kEnvDelay, kEnvAttack, kEnvHold, kEnvDecay, kEnvSustain, kEnvRelease})
         d.push_back(envParam(e, p));
   for (int l = 0; l < kLfoCount; ++l)
      for (LfoParam p : {kLfoRate, kLfoPhase, kLfoDelay, kLfoPush})
         if (p != kLfoPush || l < kMotionCount)
            d.push_back(lfoParam(l, p));
   // The flock itself can be modulated: Press can make a creature break formation, say.
   for (FlockParam p : {kCohesion, kAlignment, kSeparation, kSpeed, kWander, kLure, kRange, kNeighbourFm, kTunePull,
                        kBite, kTailLength, kTailSpread, kWake})
      d.push_back(flockParam(p));
   // A route's depth can be moved by another route.
   for (int r = 0; r < kRouteCount; ++r)
      d.push_back(routeParam(r, kRouteAmount));
   return d;
}

} // namespace detail

inline const std::vector<clap_id> &destinations() {
   static const std::vector<clap_id> d = detail::buildDestinations();
   return d;
}
// Where a part offset is in destinations(), or -1 when routes can't move it.
inline int destinationIndex(clap_id offset) {
   const auto &d = destinations();
   const auto it = std::find(d.begin(), d.end(), offset);
   return it == d.end() ? -1 : static_cast<int>(it - d.begin());
}

// The destination menu's names, filled in from the params once they exist (see kParams()).
inline std::vector<std::string> &destinationNames() {
   static std::vector<std::string> names;
   return names;
}

namespace detail {

inline std::string num(const char *prefix, int i, const char *suffix = "") {
   return std::string(prefix) + std::to_string(i + 1) + suffix;
}

inline void addPart(Builder &b, int part) {
   const auto id = [part](clap_id offset) { return partId(part, offset); };
   const std::string voice = "Voice";
   b.add(id(kPartLevel), "Level", voice, -60.0, 6.0, 0.0, kMod, Unit::Decibels);
   b.add(id(kPartPan), "Pan", voice, -1.0, 1.0, 0.0, kMod, Unit::Bipolar);
   b.add(id(kPartTranspose), "Transpose", voice, -48.0, 48.0, part == 1 ? -12.0 : 0.0, kStepped, Unit::Semitones);
   b.add(id(kPartFine), "Fine", voice, -100.0, 100.0, 0.0, kMod, Unit::Cents);
   b.add(id(kKeyLow), "Key Low", voice, 0.0, 127.0, 0.0, kGlobalStepped, Unit::Note);
   b.add(id(kKeyHigh), "Key High", voice, 0.0, 127.0, 127.0, kGlobalStepped, Unit::Note);
   b.add(id(kPolyphony), "Polyphony", voice, 1.0, kMaxPolyphony, 8.0, kGlobalStepped, Unit::Count);
   b.choice(id(kVoiceMode), "Voice Mode", voice, kVoiceModeNames, 0, kGlobalChoice);
   b.add(id(kGlideTime), "Glide", voice, 0.0, 1.0, 0.0, kMod, Unit::Time);
   b.choice(id(kGlideMode), "Glide Mode", voice, kGlideModeNames, 0, kGlobalChoice);
   b.add(id(kBendRange), "MPE Bend Range", voice, 1.0, 96.0, 48.0, kGlobalStepped, Unit::Range);
   b.add(id(kDrift), "Drift", voice, 0.0, 1.0, 0.1, kMod, Unit::Percent);
   b.add(id(kVelocityToAmp), "Velocity", voice, 0.0, 1.0, 1.0, kMod, Unit::Percent);
   b.choice(id(kPartOutput), "Output", voice, kOutputNames, 0, kGlobalChoice);
   b.add(id(kSkyPan), "Sky Pan", voice, 0.0, 1.0, 0.5, kMod, Unit::Percent);

   for (int o = 0; o < kOscCount; ++o) {
      const std::string m = num("Osc ", o), n = m + " ";
      const auto p = [&](OscParam q) { return id(oscParam(o, q)); };
      b.choice(p(kOscWave), n + "Wave", m, kWaveNames, o == 0 ? 2 : 0, kChoice);
      b.add(p(kOscShape), n + "Shape", m, 0.0, 1.0, 0.5, kMod, Unit::Percent);
      b.choice(p(kOscPitchMode), n + "Pitch Mode", m, kPitchModeNames, o == 0 ? 0 : 1, kChoice);
      b.add(p(kOscCoarse), n + "Coarse", m, -48.0, 48.0, 0.0, kStepped, Unit::Semitones);
      b.add(p(kOscFine), n + "Fine", m, -100.0, 100.0, 0.0, kMod, Unit::Cents);
      b.add(p(kOscRatio), n + "Ratio", m, 0.25, 16.0, o == 2 ? 3.0 : 1.0, kMod, Unit::Ratio);
      b.add(p(kOscFixed), n + "Fixed", m, 0.0, 1.0, fixedValue(440.0), kMod, Unit::FixedHz);
      b.choice(p(kOscSync), n + "Sync", m, kOnOffNames, 0, kChoice);
      b.choice(p(kOscRetrigger), n + "Retrigger", m, kOnOffNames, 1, kChoice);
      b.add(p(kOscPhase), n + "Phase", m, 0.0, 360.0, 0.0, kMod, Unit::Degrees);
      b.add(p(kOscBend), n + "Bend", m, 0.0, 1.0, 1.0, kMod, Unit::Percent);
      b.choice(p(kOscKeyTrack), n + "Key Track", m, kOnOffNames, 1, kChoice);
   }

   for (int from = 0; from < kOscCount; ++from)
      for (int to = 0; to < kOscCount; ++to) {
         const std::string name = from == to ? num("FM Feedback ", from) : num("FM ", from) + num(" > ", to);
         b.add(id(fmCell(from, to)), name, "FM", 0.0, 1.0, 0.0, kMod, Unit::Percent);
      }
   b.choice(id(kFmBase + kFmType), "FM Type", "FM", kFmTypeNames, 0, kGlobalChoice);

   for (int o = 0; o < kOscCount; ++o) {
      const std::string n = num("Osc ", o, " ");
      b.add(id(mixParam(kMixLevel, o)), n + "Level", "Mixer", 0.0, 1.0, o == 0 ? 1.0 : 0.0, kMod, Unit::Percent);
      b.add(id(mixParam(kMixPan, o)), n + "Pan", "Mixer", -1.0, 1.0, 0.0, kMod, Unit::Bipolar);
   }
   b.add(id(mixParam(kNoiseLevel)), "Noise Level", "Mixer", 0.0, 1.0, 0.0, kMod, Unit::Percent);
   b.add(id(mixParam(kNoiseColor)), "Noise Color", "Mixer", -1.0, 1.0, 0.0, kMod, Unit::Bipolar);

   b.choice(id(tailParam(kDriveType)), "Drive Type", "Drive", kDriveTypeNames, 0, kChoice);
   b.add(id(tailParam(kDriveAmount)), "Drive", "Drive", 0.0, 1.0, 0.0, kMod, Unit::Percent);
   b.choice(id(tailParam(kFilterRouting)), "Filter Routing", "Filters", kRoutingNames, 0, kChoice);
   b.add(id(tailParam(kFilterBalance)), "Filter Balance", "Filters", -1.0, 1.0, 0.0, kMod, Unit::Bipolar);

   for (int f = 0; f < kFilterCount; ++f) {
      const std::string m = num("Filter ", f), n = m + " ";
      const auto p = [&](FilterParam q) { return id(filterParam(f, q)); };
      // Filter 2 starts as an open ladder, so serial routing changes nothing until it is turned down.
      b.choice(p(kFilterType), n + "Type", m, kFilterTypeNames, f == 0 ? 0 : 5, kChoice);
      b.choice(p(kFilterSlope), n + "Slope", m, kSlopeNames, 1, kChoice);
      b.add(p(kFilterCutoff), n + "Cutoff", m, 0.0, 1.0, f == 0 ? 0.6 : 1.0, kMod, Unit::CutoffHz);
      b.add(p(kFilterResonance), n + "Resonance", m, 0.0, 1.0, 0.2, kMod, Unit::Percent);
      b.add(p(kFilterDrive), n + "Drive", m, 0.0, 1.0, 0.0, kMod, Unit::Percent);
      b.add(p(kFilterKeyTrack), n + "Key Track", m, 0.0, 1.0, 0.0, kMod, Unit::Percent);
   }

   for (int e = 0; e < kEnvCount; ++e) {
      const std::string m = num("Env ", e), n = m + " ";
      const auto p = [&](EnvParam q) { return id(envParam(e, q)); };
      b.add(p(kEnvDelay), n + "Delay", m, 0.0, 1.0, 0.0, kMod, Unit::Time);
      b.add(p(kEnvAttack), n + "Attack", m, 0.0, 1.0, timeValue(5.0), kMod, Unit::Time);
      b.add(p(kEnvHold), n + "Hold", m, 0.0, 1.0, 0.0, kMod, Unit::Time);
      b.add(p(kEnvDecay), n + "Decay", m, 0.0, 1.0, timeValue(e == 0 ? 400.0 : 300.0), kMod, Unit::Time);
      b.add(p(kEnvSustain), n + "Sustain", m, 0.0, 1.0, e == 0 ? 0.7 : 0.3, kMod, Unit::Percent);
      b.add(p(kEnvRelease), n + "Release", m, 0.0, 1.0, timeValue(300.0), kMod, Unit::Time);
      b.add(p(kEnvAttackCurve), n + "Attack Curve", m, -1.0, 1.0, 0.0, kMod, Unit::Curve);
      b.add(p(kEnvDecayCurve), n + "Decay Curve", m, -1.0, 1.0, 0.0, kMod, Unit::Curve);
      b.add(p(kEnvReleaseCurve), n + "Release Curve", m, -1.0, 1.0, 0.0, kMod, Unit::Curve);
      b.choice(p(kEnvLoop), n + "Loop", m, kLoopNames, 0, kGlobalChoice);
      b.add(p(kEnvVelocity), n + "Velocity", m, 0.0, 1.0, 0.0, kMod, Unit::Percent);
   }

   for (int l = 0; l < kLfoCount; ++l) {
      const std::string m = l == 0 ? "Wingbeat" : l == 1 ? "Wind" : num("LFO ", l - kMotionCount), n = m + " ";
      const auto p = [&](LfoParam q) { return id(lfoParam(l, q)); };
      b.choice(p(kLfoShape), n + "Shape", m, kLfoShapeNames, l == 1 ? 5 : l == 3 ? 1 : 0, kChoice);
      static const double rates[kLfoCount] = {5.0, 0.2, 4.0, 0.5};
      b.add(p(kLfoRate), n + "Rate", m, 0.0, 1.0, lfoValue(rates[l]), kMod, Unit::LfoHz);
      b.choice(p(kLfoSync), n + "Sync", m, kOnOffNames, 0, kGlobalChoice);
      b.choice(p(kLfoDivision), n + "Division", m, kDivisionNames, 8, kChoice);
      // The wind is one gust for the whole sky, so it starts Global.
      b.choice(p(kLfoTrigger), n + "Trigger", m, kTriggerNames, l == 1 ? 2 : 0, kGlobalChoice);
      b.add(p(kLfoPhase), n + "Phase", m, 0.0, 360.0, 0.0, kMod, Unit::Degrees);
      b.add(p(kLfoDelay), n + "Delay", m, 0.0, 1.0, 0.0, kMod, Unit::Time);
      b.choice(p(kLfoPolarity), n + "Polarity", m, kPolarityNames, 0, kChoice);
      if (l < kMotionCount)
         b.add(p(kLfoPush), n + "Push", m, 0.0, 1.0, l == 0 ? 0.2 : 0.3, kMod, Unit::Percent);
   }

   // Part A shoals and part B hunts it, so a layered patch has a predator from the start.
   const std::string fl = "Flock";
   b.choice(id(flockParam(kBehaviour)), "Behaviour", fl, kBehaviourNames, part == 0 ? 0 : 2, kGlobalChoice);
   b.add(id(flockParam(kCohesion)), "Cohesion", fl, 0.0, 1.0, 0.5, kMod, Unit::Percent);
   b.add(id(flockParam(kAlignment)), "Alignment", fl, 0.0, 1.0, 0.5, kMod, Unit::Percent);
   b.add(id(flockParam(kSeparation)), "Separation", fl, 0.0, 1.0, 0.4, kMod, Unit::Percent);
   b.add(id(flockParam(kSpeed)), "Speed", fl, 0.0, 1.0, part == 0 ? 0.4 : 0.55, kMod, Unit::Percent);
   b.add(id(flockParam(kWander)), "Wander", fl, 0.0, 1.0, 0.2, kMod, Unit::Percent);
   b.add(id(flockParam(kLure)), "Lure", fl, 0.0, 1.0, 0.6, kMod, Unit::Percent);
   b.add(id(flockParam(kRange)), "Range", fl, 0.0, 1.0, 0.35, kMod, Unit::Percent);
   b.add(id(flockParam(kNeighbourFm)), "Neighbour FM", fl, 0.0, 1.0, 0.25, kMod, Unit::Percent);
   b.add(id(flockParam(kTunePull)), "Tune Pull", fl, 0.0, 1.0, 0.0, kMod, Unit::Percent);
   b.add(id(flockParam(kBite)), "Bite", fl, 0.0, 1.0, part == 1 ? 0.5 : 0.0, kMod, Unit::Percent);
   b.add(id(flockParam(kTailLength)), "Tail Length", fl, 0.0, 1.0, 0.5, kMod, Unit::Percent);
   b.add(id(flockParam(kTailSpread)), "Tail Spread", fl, 0.0, 1.0, 0.5, kMod, Unit::Percent);
   b.add(id(flockParam(kWake)), "Wake", fl, 0.0, 1.0, 0.2, kMod, Unit::Percent);

   // The starting routes: Env 2 opens Filter 1, Press makes a creature flap faster and brighter,
   // Speed opens the cutoff and Crowd thickens the 2 > 1 FM, so a tight flock rings.
   struct Route {
      int source;
      double amount;
      clap_id destination;
   };
   const Route routes[] = {{kSourceEnv1 + 1, 0.3, filterParam(0, kFilterCutoff)},
                           {kSourceStrike + 1, 0.4, flockParam(kSpeed)},
                           {kSourceStrike + 1, 0.2, filterParam(0, kFilterCutoff)},
                           {kSourceSpeed, 0.25, filterParam(0, kFilterCutoff)},
                           {kSourceCrowd, 0.35, fmCell(1, 0)},
                           {kSourceWingbeat, 0.15, oscParam(0, kOscShape)}};
   for (int r = 0; r < kRouteCount; ++r) {
      const std::string m = num("Route ", r), n = m + " ";
      const auto p = [&](RouteParam q) { return id(routeParam(r, q)); };
      const Route route = r < static_cast<int>(std::size(routes)) ? routes[r] : Route{kSourceOff, 0.0, filterParam(0, kFilterCutoff)};
      b.choice(p(kRouteSource), n + "Source", m, kSourceNames, route.source, kGlobalChoice);
      // The destination names come from the params, so the choice list is filled in afterwards.
      b.add(p(kRouteDestination), n + "Destination", m, 0.0, static_cast<double>(destinations().size() - 1),
            destinationIndex(route.destination), kGlobalChoice, Unit::Choice, &destinationNames());
      b.add(p(kRouteAmount), n + "Amount", m, -1.0, 1.0, route.amount, kMod, Unit::Bipolar);
   }
}

inline std::vector<Param> buildParams() {
   static Builder b;
   b.choice(kPlayMode, "Play Mode", "Play", kPlayModeNames, 0, kGlobalChoice);
   b.add(kSplitKey, "Split Key", "Play", 0.0, 127.0, 60.0, kGlobalStepped, Unit::Note);
   b.add(kVelocitySplit, "Velocity Split", "Play", 1.0, 127.0, 80.0, kGlobalStepped, Unit::Velocity);
   b.choice(kCrossfadeSource, "Crossfade Source", "Play", kCrossfadeNames, 0, kGlobalChoice);
   b.choice(kQuality, "Quality", "Output", kQualityNames, 1, kGlobalChoice);
   b.add(kMasterLevel, "Master Level", "Output", -60.0, 6.0, -6.0, kGlobalMod, Unit::Decibels);
   b.add(kManagerBendRange, "Manager Bend Range", "Expression", 1.0, 96.0, 2.0, kGlobalStepped, Unit::Range);
   b.add(kGlideRounding, "Glide Rounding", "Expression", 0.0, 1.0, 0.0, kGlobalMod, Unit::Percent);
   b.add(kRoundingTime, "Rounding Time", "Expression", 0.0, 1.0, timeValue(150.0), kGlobalMod, Unit::Time);
   for (int i = 0; i < kMacroCount; ++i)
      b.add(kMacro1 + i, num("Macro ", i), "Macros", 0.0, 1.0, 0.0, kMod, Unit::Percent);
   for (int i = 0; i < kTouchCount; ++i) {
      b.add(kTouchCurve1 + i, std::string(kTouchNames[i]) + " Curve", "Expression", -1.0, 1.0, 0.0, kGlobalMod, Unit::Curve);
      // Bend is smoothed less by default, so slides track the finger.
      b.add(kTouchSmooth1 + i, std::string(kTouchNames[i]) + " Smoothing", "Expression", 0.0, 1.0,
            timeValue(i == 2 ? 2.0 : 8.0), kGlobalMod, Unit::Time);
   }
   b.add(kSkySize, "Sky Size", "Sky", 0.0, 1.0, 0.5, kGlobalMod, Unit::Percent);
   b.choice(kSkyWalls, "Sky Walls", "Sky", kWallNames, 0, kGlobalChoice);
   b.add(kOutputWidth, "Output Width", "Output", 0.0, 2.0, 1.0, kGlobalMod, Unit::Percent);
   for (int part = 0; part < kPartCount; ++part)
      addPart(b, part);
   // The destination menu reads each destination's name from part A.
   for (clap_id offset : destinations())
      for (const Param &p : b.params)
         if (p.def.id == partId(0, offset))
            destinationNames().push_back(p.shortName);
   std::sort(b.params.begin(), b.params.end(), [](const Param &x, const Param &y) { return x.def.id < y.def.id; });
   return b.params;
}

} // namespace detail

// Every param, in id order.
inline const std::vector<Param> &kParams() {
   static const std::vector<Param> params = detail::buildParams();
   return params;
}

inline size_t paramCount() { return kParams().size(); }

// The param with this id, or null.
inline const Param *findParam(clap_id id) {
   static const std::vector<int> index = [] {
      std::vector<int> i(kParamIdEnd, -1);
      for (size_t n = 0; n < kParams().size(); ++n)
         i[kParams()[n].def.id] = static_cast<int>(n);
      return i;
   }();
   return id < kParamIdEnd && index[id] >= 0 ? &kParams()[index[id]] : nullptr;
}
inline int paramIndex(clap_id id) {
   const Param *p = findParam(id);
   return p ? static_cast<int>(p - kParams().data()) : -1;
}

// ---- text ----

inline std::string noteName(int note) {
   static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
   note = std::clamp(note, 0, 127);
   return names[note % 12] + std::to_string(note / 12 - 1);
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   const Param *p = findParam(id);
   if (!p) {
      std::snprintf(out, size, "%g", value);
      return;
   }
   switch (p->unit) {
   case Unit::Choice: {
      const auto &names = *p->choices;
      const int i = std::clamp(static_cast<int>(value + 0.5), 0, static_cast<int>(names.size()) - 1);
      std::snprintf(out, size, "%s", names.empty() ? "" : names[i].c_str());
      break;
   }
   case Unit::Percent:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   case Unit::Bipolar:
      std::snprintf(out, size, "%+.0f%%", value * 100.0);
      break;
   case Unit::Semitones:
      std::snprintf(out, size, "%+d st", static_cast<int>(std::lround(value)));
      break;
   case Unit::Range:
      std::snprintf(out, size, "%d st", static_cast<int>(std::lround(value)));
      break;
   case Unit::Cents:
      std::snprintf(out, size, "%+.0f ct", value);
      break;
   case Unit::Ratio:
      std::snprintf(out, size, "x%.3g", value);
      break;
   case Unit::CutoffHz:
   case Unit::LfoHz:
   case Unit::FixedHz: {
      const double hz = p->unit == Unit::CutoffHz ? cutoffHz(value) : p->unit == Unit::LfoHz ? lfoHz(value) : fixedHz(value);
      if (hz >= 999.5)
         std::snprintf(out, size, "%.2f kHz", hz / 1000.0);
      else if (hz >= 10.0)
         std::snprintf(out, size, "%.0f Hz", hz);
      else
         std::snprintf(out, size, "%.2f Hz", hz);
      break;
   }
   case Unit::Time: {
      const double ms = timeMs(value);
      if (ms >= 999.5)
         std::snprintf(out, size, "%.2f s", ms / 1000.0);
      else if (ms >= 10.0)
         std::snprintf(out, size, "%.0f ms", ms);
      else
         std::snprintf(out, size, "%.1f ms", ms);
      break;
   }
   case Unit::Decibels:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case Unit::Note:
      std::snprintf(out, size, "%s", noteName(static_cast<int>(std::lround(value))).c_str());
      break;
   case Unit::Count:
   case Unit::Velocity:
      std::snprintf(out, size, "%d", static_cast<int>(std::lround(value)));
      break;
   case Unit::Degrees:
      std::snprintf(out, size, "%.0f deg", value);
      break;
   case Unit::Curve:
      std::snprintf(out, size, "%+.2f", value);
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers in the same unit. Hosts check that the
// two round-trip.
inline bool parseParam(clap_id id, const char *text, double &value) {
   const Param *p = findParam(id);
   if (!p)
      return false;
   while (*text == ' ')
      ++text;
   if (p->unit == Unit::Choice) {
      const auto &names = *p->choices;
      for (size_t i = 0; i < names.size(); ++i)
         if (names[i] == text) {
            value = static_cast<double>(i);
            return true;
         }
   }
   if (p->unit == Unit::Note) {
      // A name such as C4 or F#2, or a MIDI note number.
      static const int offsets[7] = {9, 11, 0, 2, 4, 5, 7}; // A..G
      const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
      if (letter >= 'A' && letter <= 'G') {
         int note = offsets[letter - 'A'];
         const char *c = text + 1;
         if (*c == '#')
            ++note, ++c;
         else if (*c == 'b')
            --note, ++c;
         char *end = nullptr;
         const long octave = std::strtol(c, &end, 10);
         if (end == c)
            return false;
         value = std::clamp(static_cast<double>(note + 12 * (octave + 1)), p->def.min, p->def.max);
         return true;
      }
   }
   if (p->unit == Unit::Ratio && (*text == 'x' || *text == 'X'))
      ++text;
   double v = 0.0;
   const char *end = nullptr;
   if (!jclap::core::parseNumber(text, v, &end))
      return false;
   while (*end == ' ')
      ++end;
   const bool kilo = *end == 'k' || *end == 'K';
   switch (p->unit) {
   case Unit::Percent:
   case Unit::Bipolar:
      value = v / 100.0;
      break;
   case Unit::CutoffHz:
      value = cutoffValue(std::fmax(1e-3, kilo ? v * 1000.0 : v));
      break;
   case Unit::LfoHz:
      value = lfoValue(std::fmax(1e-6, kilo ? v * 1000.0 : v));
      break;
   case Unit::FixedHz:
      value = fixedValue(std::fmax(1e-3, kilo ? v * 1000.0 : v));
      break;
   case Unit::Time:
      // Milliseconds, or seconds with an s.
      value = timeValue(std::fmax(0.0, *end == 's' || *end == 'S' ? v * 1000.0 : v));
      break;
   default:
      value = (p->def.flags & CLAP_PARAM_IS_STEPPED) ? std::round(v) : v;
      break;
   }
   value = std::clamp(value, p->def.min, p->def.max);
   return true;
}

} // namespace jsynth

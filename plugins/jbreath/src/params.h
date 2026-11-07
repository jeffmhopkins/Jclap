#pragma once

// JBreath's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jbreath {

enum ParamId : clap_id {
   // The instrument, and how it's blown.
   kModel,
   kBreath,
   kEmbouchure,
   kNoise,
   kEdge,
   // How a note starts and ends.
   kKick,
   kAttack,
   kRelease,
   // The bore.
   kTune,
   kFine,
   kBright,
   kGlide,
   // Vibrato.
   kVibRate,
   kVibDepth,
   kVibPitch,
   kVibDelay,
   // Voice and expression.
   kVoiceMode,
   kBreathSource,
   kCurve,
   kSlide,
   kBendRange,
   kLevel,
   // Added after the first build: an input to blow from (a CV, an LFO, automation).
   kBreathIn,
   // Modulation (added with MAP): two LFOs, the Swell envelope, four buses and a pool of routes.
   kLfo1Rate,
   kLfo1Shape,
   kLfo1Sync,
   kLfo2Rate,
   kLfo2Shape,
   kLfo2Sync,
   kSwellShape,
   kSwellTime,
   kSwellLoop,
   kBusBase,
   kRouteBase = kBusBase + 4 * 5,
   kParamCount = kRouteBase + 32 * 3
};

constexpr int kLfoCount = 2, kBusCount = 4, kRouteCount = 32;
constexpr int kBusStride = 5, kRouteStride = 3;
enum BusParam : clap_id { kBusAmount, kBusMode, kBusCurve, kBusPolarity, kBusLag };
enum RouteParam : clap_id { kRouteSource, kRouteDestination, kRouteAmount };
constexpr clap_id busParam(int bus, BusParam p) { return kBusBase + bus * kBusStride + p; }
constexpr clap_id routeParam(int route, RouteParam p) { return kRouteBase + route * kRouteStride + p; }
constexpr clap_id lfoParam(int lfo, int p) { return (lfo ? kLfo2Rate : kLfo1Rate) + p; }

// What a route can come from. The header's five are the player's hands (MPE), the next six are
// read off each voice's own breath loop and sound, then the generators and the buses.
enum ModSource {
   kSrcOff,
   kSrcStrike,   // the note's velocity
   kSrcPress,    // its pressure
   kSrcGlide,    // its pitch bend, -1..1 over the bend range
   kSrcSlide,    // its slide (CC 74)
   kSrcLift,     // its release velocity
   kSrcLoop,     // how big its breath loop is: the breath blowing
   kSrcKnot,     // how knotted: the brightness of its sound
   kSrcFold,     // how far past where the instrument overblows
   kSrcRegister, // how high it plays in the instrument's range
   kSrcVapor,    // how much breath noise rides on it
   kSrcDraft,    // a slow draught of air: a wandering -1..1 of its own
   kSrcSwell,
   kSrcLfo1,
   kSrcLfo2,
   kSrcKey,      // the note, -1..1 around middle C
   kSrcRandom,   // a fresh -1..1 for each note
   kSrcBus1,
   kModSourceCount = kSrcBus1 + kBusCount
};
inline const char *const kModSourceNames[kModSourceCount] = {
   "Off", "Strike", "Press", "Glide", "Slide", "Lift", "Loop", "Knot", "Fold", "Register", "Vapor", "Draft",
   "Swell", "LFO 1", "LFO 2", "Key", "Random", "Bus 1", "Bus 2", "Bus 3", "Bus 4"};
inline const unsigned kModSourceColors[kModSourceCount] = {
   0xff6b7280, 0xfff5d76e, 0xffff5fc8, 0xffa78bfa, 0xff7ee6a0, 0xff6fb3a0, 0xffffb547, 0xff5cc8ff, 0xff9b7bff,
   0xffff7a6b, 0xffc9d3e6, 0xff5fd0b0, 0xff7fd3ff, 0xffffa64d, 0xffffd166, 0xffb0b8c8, 0xffe0e0e0,
   0xff8fe8a8, 0xff8fe8a8, 0xff8fe8a8, 0xff8fe8a8};
// Sources that swing both ways (-1..1); the rest run 0..1.
inline bool bipolarSource(int s) {
   return s == kSrcGlide || s == kSrcDraft || s == kSrcLfo1 || s == kSrcLfo2 || s == kSrcKey || s == kSrcRandom ||
          s >= kSrcBus1;
}

// What a route can move: these params, by index. A bus's Amount knob is where its inputs go.
inline const clap_id kModDestinations[] = {
   kBreath, kEmbouchure, kNoise, kEdge, kKick, kAttack, kRelease, kTune, kFine, kGlide, kVibRate, kVibDepth,
   kVibPitch, kVibDelay, kCurve, kLevel, kLfo1Rate, kLfo2Rate, kSwellTime,
   busParam(0, kBusAmount), busParam(1, kBusAmount), busParam(2, kBusAmount), busParam(3, kBusAmount)};
constexpr int kModDestCount = static_cast<int>(sizeof(kModDestinations) / sizeof(kModDestinations[0]));
constexpr int kFirstBusDest = kModDestCount - kBusCount;
inline int destinationOf(clap_id id) {
   for (int i = 0; i < kModDestCount; ++i)
      if (kModDestinations[i] == id)
         return i;
   return -1;
}

enum LfoShape { kLfoSine, kLfoTriangle, kLfoSaw, kLfoSquare, kLfoSampleHold, kLfoSmooth };
inline const char *const kLfoShapeNames[] = {"Sine", "Triangle", "Saw", "Square", "S&H", "Smooth"};
inline const char *const kLfoSyncNames[] = {"Note", "Free"};
// The Swell's contours, each over its Time: a rise and fall, a rise to stay, a fall, two humps, a
// quick bloom that settles, and a slow breath in and out.
enum SwellShape { kSwellArch, kSwellRise, kSwellFall, kSwellWave, kSwellBloom, kSwellTide };
inline const char *const kSwellShapeNames[] = {"Arch", "Rise", "Fall", "Waves", "Bloom", "Tide"};
inline const char *const kSwellLoopNames[] = {"Once", "Loop"};
enum BusMode { kBusSum, kBusMultiply, kBusMin, kBusMax };
inline const char *const kBusModeNames[] = {"Sum", "Multiply", "Min", "Max"};
inline const char *const kBusCurveNames[] = {"Linear", "Exp", "Log", "S-Curve", "Absolute"};
inline const char *const kBusPolarityNames[] = {"As Is", "Unipolar", "Bipolar", "Invert"};


constexpr auto kGlobalMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kMod = kGlobalMod | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY |
                      CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL | CLAP_PARAM_IS_MODULATABLE_PER_PORT;
constexpr auto kStepped = kMod | CLAP_PARAM_IS_STEPPED;
// The instrument, voice mode and breath source are the whole plugin's: no per-voice meaning.
constexpr auto kGlobalChoice = kGlobalMod | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

enum VoiceMode { kPoly, kMono, kLegato };
enum BreathSource { kSourceAuto, kSourcePressure, kSourceVelocity, kSourceInput, kSourceMix };

namespace detail {
inline std::array<jclap::core::ParamDef, kParamCount> buildParams() {
   std::array<jclap::core::ParamDef, kParamCount> p{};
   const jclap::core::ParamDef fixed[] = {
   {kModel, "Instrument", "Exciter", 0.0, 4.0, 1.0, kGlobalChoice},
   {kBreath, "Breath", "Exciter", 0.25, 1.5, 1.0, kMod},
   {kEmbouchure, "Embouchure", "Exciter", 0.0, 1.0, 0.5, kMod},
   {kNoise, "Noise", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kEdge, "Edge", "Exciter", 0.0, 2.0, 1.0, kMod},
   {kKick, "Kick", "Articulation", 0.0, 1.0, 0.25, kMod},
   {kAttack, "Attack", "Articulation", 1.0, 400.0, 25.0, kMod},
   {kRelease, "Release", "Articulation", 5.0, 2000.0, 90.0, kMod},
   {kTune, "Tune", "Bore", -24.0, 24.0, 0.0, kStepped},
   {kFine, "Fine", "Bore", -100.0, 100.0, 0.0, kMod},
   {kBright, "Bright", "Bore", -12.0, 12.0, 0.0, kGlobalMod},
   {kGlide, "Glide", "Bore", 0.0, 1000.0, 0.0, kMod},
   {kVibRate, "Vibrato Rate", "Vibrato", 2.0, 9.0, 5.1, kMod},
   {kVibDepth, "Vibrato Depth", "Vibrato", 0.0, 0.3, 0.05, kMod},
   {kVibPitch, "Vibrato Pitch", "Vibrato", 0.0, 50.0, 0.0, kMod},
   {kVibDelay, "Vibrato Delay", "Vibrato", 0.0, 2000.0, 350.0, kMod},
   {kVoiceMode, "Voice Mode", "Voice", 0.0, 2.0, 0.0, kGlobalChoice},
   {kBreathSource, "Breath Source", "Voice", 0.0, 4.0, 0.0, kGlobalChoice},
   {kCurve, "Press Curve", "Voice", 0.3, 3.0, 1.0, kMod},
   {kSlide, "Slide", "Voice", 0.0, 1.0, 0.5, kMod},
   {kBendRange, "MPE Bend Range", "Voice", 1.0, 96.0, 48.0, kGlobalMod | CLAP_PARAM_IS_STEPPED},
   {kLevel, "Level", "Output", -60.0, 12.0, 0.0, kMod},
   {kBreathIn, "Breath In", "Voice", 0.0, 1.0, 0.0, kMod},
      {kLfo1Rate, "LFO 1 Rate", "LFO", 0.02, 20.0, 1.0, kMod},
      {kLfo1Shape, "LFO 1 Shape", "LFO", 0.0, 5.0, 0.0, kGlobalChoice},
      {kLfo1Sync, "LFO 1 Sync", "LFO", 0.0, 1.0, 0.0, kGlobalChoice},
      {kLfo2Rate, "LFO 2 Rate", "LFO", 0.02, 20.0, 0.25, kMod},
      {kLfo2Shape, "LFO 2 Shape", "LFO", 0.0, 5.0, 5.0, kGlobalChoice},
      {kLfo2Sync, "LFO 2 Sync", "LFO", 0.0, 1.0, 1.0, kGlobalChoice},
      {kSwellShape, "Swell Shape", "Swell", 0.0, 5.0, 0.0, kGlobalChoice},
      {kSwellTime, "Swell Time", "Swell", 0.05, 30.0, 3.0, kMod},
      {kSwellLoop, "Swell Loop", "Swell", 0.0, 1.0, 0.0, kGlobalChoice},
   };
   for (const auto &d : fixed)
      p[d.id] = d;
   // Generated names live as long as the program.
   static std::vector<std::string> names;
   names.reserve(kBusCount * kBusStride + kRouteCount * kRouteStride);
   const auto name = [](std::string n) { names.push_back(std::move(n)); return names.back().c_str(); };
   constexpr auto kNone = static_cast<clap_param_info_flags>(0);
   for (int b = 0; b < kBusCount; ++b) {
      const std::string n = "Bus " + std::to_string(b + 1);
      p[busParam(b, kBusAmount)] = {busParam(b, kBusAmount), name(n + " Amount"), "Bus", -1.0, 1.0, 0.0, kMod};
      p[busParam(b, kBusMode)] = {busParam(b, kBusMode), name(n + " Mode"), "Bus", 0.0, 3.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusCurve)] = {busParam(b, kBusCurve), name(n + " Curve"), "Bus", 0.0, 4.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusPolarity)] = {busParam(b, kBusPolarity), name(n + " Polarity"), "Bus", 0.0, 3.0, 0.0, kGlobalChoice};
      p[busParam(b, kBusLag)] = {busParam(b, kBusLag), name(n + " Lag"), "Bus", 0.0, 2000.0, 0.0, kMod};
   }
   for (int r = 0; r < kRouteCount; ++r) {
      const std::string n = "Route " + std::to_string(r + 1);
      // Which source and destination aren't things to automate; the amount is.
      p[routeParam(r, kRouteSource)] = {routeParam(r, kRouteSource), name(n + " Source"), "Routes", 0.0,
                                        static_cast<double>(kModSourceCount - 1), 0.0, kNone | CLAP_PARAM_IS_STEPPED};
      p[routeParam(r, kRouteDestination)] = {routeParam(r, kRouteDestination), name(n + " Destination"), "Routes", 0.0,
                                             static_cast<double>(kModDestCount - 1), 0.0, kNone | CLAP_PARAM_IS_STEPPED};
      p[routeParam(r, kRouteAmount)] = {routeParam(r, kRouteAmount), name(n + " Amount"), "Routes", -1.0, 1.0, 0.0, kGlobalMod};
   }
   return p;
}
} // namespace detail

inline const std::array<jclap::core::ParamDef, kParamCount> kParams = detail::buildParams();

inline const char *const kModelNames[] = {"Flute", "Clarinet", "Sax", "Oboe", "Trumpet"};
inline const char *const kVoiceModeNames[] = {"Poly", "Mono", "Legato"};
inline const char *const kBreathSourceNames[] = {"Auto", "Pressure", "Velocity", "Input", "Mix"};

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kModel:
      count = 5;
      return kModelNames;
   case kVoiceMode:
      count = 3;
      return kVoiceModeNames;
   case kBreathSource:
      count = 5;
      return kBreathSourceNames;
   case kLfo1Shape:
   case kLfo2Shape:
      count = 6;
      return kLfoShapeNames;
   case kLfo1Sync:
   case kLfo2Sync:
      count = 2;
      return kLfoSyncNames;
   case kSwellShape:
      count = 6;
      return kSwellShapeNames;
   case kSwellLoop:
      count = 2;
      return kSwellLoopNames;
   default:
      if (id >= kBusBase && id < kRouteBase) {
         switch ((id - kBusBase) % kBusStride) {
         case kBusMode:
            count = 4;
            return kBusModeNames;
         case kBusCurve:
            count = 5;
            return kBusCurveNames;
         case kBusPolarity:
            count = 4;
            return kBusPolarityNames;
         default:
            break;
         }
      }
      if (id >= kRouteBase && id < kParamCount && (id - kRouteBase) % kRouteStride == kRouteSource) {
         count = kModSourceCount;
         return kModSourceNames;
      }
      count = 0;
      return nullptr;
   }
}

inline bool isPercent(clap_id id) {
   switch (id) {
   case kBreath:
   case kEmbouchure:
   case kNoise:
   case kEdge:
   case kKick:
   case kVibDepth:
   case kSlide:
   case kBreathIn:
      return true;
   default:
      if (id >= kBusBase && id < kRouteBase)
         return (id - kBusBase) % kBusStride == kBusAmount;
      if (id >= kRouteBase && id < kParamCount)
         return (id - kRouteBase) % kRouteStride == kRouteAmount;
      return false;
   }
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   if (isPercent(id)) {
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      return;
   }
   if (id >= kRouteBase && id < kParamCount && (id - kRouteBase) % kRouteStride == kRouteDestination) {
      const int d = std::clamp(static_cast<int>(value + 0.5), 0, kModDestCount - 1);
      std::snprintf(out, size, "%s", kParams[kModDestinations[d]].name);
      return;
   }
   if (id >= kBusBase && id < kRouteBase && (id - kBusBase) % kBusStride == kBusLag) {
      std::snprintf(out, size, "%.0f ms", value);
      return;
   }
   switch (id) {
   case kLfo1Rate:
   case kLfo2Rate:
      std::snprintf(out, size, value < 1.0 ? "%.2f Hz" : "%.1f Hz", value);
      break;
   case kSwellTime:
      std::snprintf(out, size, value < 10.0 ? "%.2f s" : "%.1f s", value);
      break;
   case kAttack:
   case kRelease:
   case kGlide:
   case kVibDelay:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   case kTune:
      std::snprintf(out, size, "%+d st", static_cast<int>(std::lround(value)));
      break;
   case kBendRange:
      std::snprintf(out, size, "%d st", static_cast<int>(std::lround(value)));
      break;
   case kFine:
      std::snprintf(out, size, "%+.0f ct", value);
      break;
   case kVibPitch:
      std::snprintf(out, size, "%.0f ct", value);
      break;
   case kBright:
      std::snprintf(out, size, "%+.1f dB", value);
      break;
   case kLevel:
      std::snprintf(out, size, "%.1f dB", value);
      break;
   case kVibRate:
      std::snprintf(out, size, "%.2f Hz", value);
      break;
   case kCurve:
      std::snprintf(out, size, "%.2f", value);
      break;
   default:
      std::snprintf(out, size, "%g", value);
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers in the same unit. Hosts check that the
// two round-trip.
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
   if (id >= kRouteBase && id < kParamCount && (id - kRouteBase) % kRouteStride == kRouteDestination)
      for (int d = 0; d < kModDestCount; ++d)
         if (!std::strcmp(text, kParams[kModDestinations[d]].name)) {
            value = d;
            return true;
         }
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v, nullptr))
      return false;
   if (isPercent(id))
      value = v / 100.0;
   else
      value = (kParams[id].flags & CLAP_PARAM_IS_STEPPED) ? std::round(v) : v;
   return true;
}

} // namespace jbreath

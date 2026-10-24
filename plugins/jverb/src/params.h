#pragma once

// JVerb's parameters. Ids are indexes into kParams, so keep them in order; once released, only
// append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jverb {

constexpr int kMaxBodies = 6;

enum ParamId : clap_id {
   // The space: how big and how long.
   kDecay,
   kSize,
   kGravity,
   kBodies,
   kMotion,
   kAttack,
   kDiffusion,
   kEarlyLate,
   kPredelay,
   kDampHigh,
   kBassMult,
   kBassXover,
   kLowCut,
   kHighCut,
   // The orbit clock and how audio pushes the bodies.
   kRun,
   kClock,
   kSyncBars,
   kReact,
   // Per body, four params each, body 1 first.
   kBody1Mass,
   kBody1Velocity,
   kBody1Target,
   kBody1Amount,
   kBodyParamsEnd = kBody1Mass + 4 * kMaxBodies,
   // The outer feedback loop.
   kFeedback = kBodyParamsEnd,
   kShimmer,
   kShimmerPitch,
   kTilt,
   kSmear,
   kSafety,
   kFreeze,
   // Drive, which sits in one of three places.
   kDriveOn,
   kDriveWhere,
   kDriveType,
   kDrive,
   kDriveTone,
   kDriveBias,
   kDriveMix,
   kDriveTrim,
   // The echo: a long delay, free or synced, in one of three places.
   kEchoOn,
   kEchoWhere,
   kEchoTime,
   kEchoSync,
   kEchoFeedback,
   kEchoTone,
   kEchoPingPong,
   kEchoMix,
   // Output.
   kWidth,
   kDuck,
   kMix,
   kLevel,
   // How much the orbit moves each body's share of the tail: where it sits, its tone, its phase.
   kOrbitPan,
   kOrbitTone,
   kOrbitSwirl,
   kParamCount
};

// Body `body`'s param: bodyParam(2, kBody1Mass) is body 3's mass.
constexpr clap_id bodyParam(int body, clap_id body1Param) { return body1Param + 4 * body; }

// What a body's motion can move. None of them bend pitch: the orbits change the tail's colour and
// shape, never its tuning.
enum class Target { Off, Decay, Damping, Diffusion, EarlyLate, Drive, Shimmer, Feedback, Tilt, Width, Count };
enum class Clock { Free, Sync, Retrig, Count };
enum class DriveWhere { Pre, Loop, Post, Count };
enum class DriveType { Tape, Tube, Fuzz, Fold, Crush, Count };
// Into: echoes feed the tank, so each repeat is reverberated. Loop: the delay sits in the outer
// loop's return, so the tail comes back around in long, spaced passes. After: echoes of the reverb.
enum class EchoWhere { Into, Loop, After, Count };

// Echo note values, in beats; the first entry means free-running milliseconds.
inline const char *const kEchoSyncNames[] = {"Free", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
                                             "1/4",  "1/2T", "1/4D", "1/2",   "1/2D", "1 Bar", "2 Bars"};
constexpr double kEchoSyncBeats[] = {0.0,       0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75,
                                     1.0,       4.0 / 3.0, 1.5, 2.0, 3.0, 4.0,       8.0};
constexpr int kEchoSyncCount = static_cast<int>(sizeof(kEchoSyncBeats) / sizeof(kEchoSyncBeats[0]));
// The longest echo, in seconds; synced values longer than this (slow tempos) are halved until they fit.
constexpr double kEchoMaxSeconds = 4.0;

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kToggle = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;

// Bodies start routed nowhere, so nothing moves the sound that the panel doesn't show. The amount
// waits at 30%, so picking a target does something straight away.
constexpr double kDefaultTarget[kMaxBodies] = {0, 0, 0, 0, 0, 0};
constexpr double kDefaultAmount[kMaxBodies] = {0.3, 0.3, 0.3, 0.3, 0.3, 0.3};
// Unequal masses: three equal ones would stay on the figure-eight they start on, which is stable.
// These break it up within a few laps into motion that never repeats.
constexpr double kDefaultMass[kMaxBodies] = {1.0, 1.25, 0.8, 1.0, 1.0, 1.0};

inline const char *const kBodyNames[kMaxBodies] = {"IO", "KEPLER", "VESTA", "TITAN", "RHEA", "LUNA"};

struct ParamTable {
   jclap::core::ParamDef defs[kParamCount];
   char names[kMaxBodies][4][24];
   ParamTable() {
      const jclap::core::ParamDef fixed[] = {
         {kDecay, "Decay", "Space", 0.2, 60.0, 4.0, kMod},
         {kSize, "System Size", "Space", 0.25, 4.0, 1.0, kMod},
         {kGravity, "Gravity", "Space", 0.25, 4.0, 1.0, kMod},
         {kBodies, "Bodies", "Space", 2.0, 6.0, 3.0, kToggle},
         {kMotion, "Motion", "Space", 0.0, 2.0, 0.4, kMod},
         {kAttack, "Attack", "Space", -1.0, 1.0, 0.0, kMod},
         {kDiffusion, "Diffusion", "Space", 0.0, 1.0, 0.7, kMod},
         {kEarlyLate, "Early/Late", "Space", 0.0, 1.0, 0.75, kMod},
         {kPredelay, "Predelay", "Space", 0.0, 500.0, 20.0, kMod},
         {kDampHigh, "Damp High", "Space", 1000.0, 20000.0, 7000.0, kMod},
         {kBassMult, "Bass Mult", "Space", 0.25, 2.5, 1.2, kMod},
         {kBassXover, "Bass Xover", "Space", 80.0, 800.0, 250.0, kMod},
         {kLowCut, "Low Cut", "Space", 20.0, 1000.0, 40.0, kMod},
         {kHighCut, "High Cut", "Space", 1000.0, 20000.0, 16000.0, kMod},
         {kRun, "Run", "Orbit", 0.0, 1.0, 1.0, kToggle},
         {kClock, "Clock", "Orbit", 0.0, 2.0, 0.0, kChoice},
         {kSyncBars, "Sync Bars", "Orbit", 1.0, 16.0, 4.0, kToggle},
         {kReact, "React", "Orbit", 0.0, 1.0, 0.3, kMod},
      };
      for (const auto &d : fixed)
         defs[d.id] = d;
      static const char *const kSuffix[4] = {"Mass", "Velocity", "Target", "Amount"};
      for (int b = 0; b < kMaxBodies; ++b) {
         for (int k = 0; k < 4; ++k)
            std::snprintf(names[b][k], sizeof(names[b][k]), "%s %s", kBodyNamesTitle(b), kSuffix[k]);
         defs[bodyParam(b, kBody1Mass)] = {bodyParam(b, kBody1Mass), names[b][0], "Bodies", 0.1, 3.0, kDefaultMass[b], kMod};
         defs[bodyParam(b, kBody1Velocity)] = {bodyParam(b, kBody1Velocity), names[b][1], "Bodies", 0.5, 1.5, 1.0, kMod};
         defs[bodyParam(b, kBody1Target)] = {bodyParam(b, kBody1Target), names[b][2], "Bodies", 0.0,
                                             static_cast<double>(Target::Count) - 1.0, kDefaultTarget[b], kChoice};
         defs[bodyParam(b, kBody1Amount)] = {bodyParam(b, kBody1Amount), names[b][3], "Bodies", -1.0, 1.0,
                                             kDefaultAmount[b], kMod};
      }
      const jclap::core::ParamDef tail[] = {
         {kFeedback, "Feedback", "Loop", 0.0, 1.1, 0.0, kMod},
         {kShimmer, "Shimmer", "Loop", 0.0, 1.0, 0.0, kMod},
         {kShimmerPitch, "Shimmer Pitch", "Loop", -24.0, 24.0, 12.0, kToggle},
         {kTilt, "Tilt", "Loop", -1.0, 1.0, 0.0, kMod},
         {kSmear, "Smear", "Loop", 0.0, 1.0, 0.3, kMod},
         {kSafety, "Safety", "Loop", 0.0, 1.0, 0.0, kChoice},
         {kFreeze, "Freeze", "Loop", 0.0, 1.0, 0.0, kToggle},
         {kDriveOn, "Drive On", "Drive", 0.0, 1.0, 0.0, kToggle},
         {kDriveWhere, "Drive Slot", "Drive", 0.0, 2.0, 1.0, kChoice},
         {kDriveType, "Drive Type", "Drive", 0.0, 4.0, 1.0, kChoice},
         {kDrive, "Drive", "Drive", 0.0, 36.0, 12.0, kMod},
         {kDriveTone, "Drive Tone", "Drive", -1.0, 1.0, 0.0, kMod},
         {kDriveBias, "Drive Bias", "Drive", 0.0, 1.0, 0.2, kMod},
         {kDriveMix, "Drive Mix", "Drive", 0.0, 1.0, 1.0, kMod},
         {kDriveTrim, "Drive Trim", "Drive", -18.0, 6.0, 0.0, kMod},
         {kEchoOn, "Echo On", "Echo", 0.0, 1.0, 0.0, kToggle},
         {kEchoWhere, "Echo Slot", "Echo", 0.0, 2.0, 0.0, kChoice},
         {kEchoTime, "Echo Time", "Echo", 10.0, 4000.0, 375.0, kMod},
         {kEchoSync, "Echo Sync", "Echo", 0.0, kEchoSyncCount - 1.0, 6.0, kChoice},
         {kEchoFeedback, "Echo Feedback", "Echo", 0.0, 1.05, 0.45, kMod},
         {kEchoTone, "Echo Tone", "Echo", -1.0, 1.0, -0.2, kMod},
         {kEchoPingPong, "Echo Ping-Pong", "Echo", 0.0, 1.0, 1.0, kToggle},
         {kEchoMix, "Echo Mix", "Echo", 0.0, 1.0, 0.5, kMod},
         {kWidth, "Width", "Output", 0.0, 2.0, 1.0, kMod},
         {kDuck, "Duck", "Output", 0.0, 1.0, 0.0, kMod},
         {kMix, "Mix", "Output", 0.0, 1.0, 0.35, kMod},
         {kLevel, "Level", "Output", -24.0, 12.0, 0.0, kMod},
         {kOrbitPan, "Orbit Pan", "Orbit", 0.0, 2.0, 0.75, kMod},
         {kOrbitTone, "Orbit Tone", "Orbit", 0.0, 2.0, 0.6, kMod},
         {kOrbitSwirl, "Orbit Swirl", "Orbit", 0.0, 2.0, 0.6, kMod},
      };
      for (const auto &d : tail)
         defs[d.id] = d;
   }

private:
   static const char *kBodyNamesTitle(int b) {
      static const char *const kTitle[kMaxBodies] = {"Io", "Kepler", "Vesta", "Titan", "Rhea", "Luna"};
      return kTitle[b];
   }
};

inline const ParamTable &paramTable() {
   static const ParamTable table;
   return table;
}

// kParams[id] for the param's definition.
struct ParamsView {
   const jclap::core::ParamDef &operator[](clap_id id) const { return paramTable().defs[id]; }
};
inline constexpr ParamsView kParams{};

// What a body param is: -1 if `id` isn't one.
inline int bodyOf(clap_id id) {
   return id >= kBody1Mass && id < kBodyParamsEnd ? static_cast<int>(id - kBody1Mass) / 4 : -1;
}
inline clap_id bodyRole(clap_id id) { return kBody1Mass + (id - kBody1Mass) % 4; }

// Choice names, in value order.
inline const char *const kTargetNames[] = {"Off",   "Decay",   "Damping", "Diffusion", "Early/Late",
                                           "Drive", "Shimmer", "Feedback", "Tilt",      "Width"};
inline const char *const kClockNames[] = {"Free", "Sync", "Retrig"};
inline const char *const kWhereNames[] = {"Pre", "Loop", "Post"};
inline const char *const kTypeNames[] = {"Tape", "Tube", "Fuzz", "Fold", "Crush"};
inline const char *const kSafetyNames[] = {"Soft", "Hard"};
inline const char *const kEchoWhereNames[] = {"Into", "Loop", "After"};

inline const char *const *choiceNames(clap_id id, int &count) {
   if (bodyOf(id) >= 0 && bodyRole(id) == kBody1Target) {
      count = static_cast<int>(Target::Count);
      return kTargetNames;
   }
   switch (id) {
   case kClock:
      count = static_cast<int>(Clock::Count);
      return kClockNames;
   case kDriveWhere:
      count = static_cast<int>(DriveWhere::Count);
      return kWhereNames;
   case kDriveType:
      count = static_cast<int>(DriveType::Count);
      return kTypeNames;
   case kSafety:
      count = 2;
      return kSafetyNames;
   case kEchoWhere:
      count = static_cast<int>(EchoWhere::Count);
      return kEchoWhereNames;
   case kEchoSync:
      count = kEchoSyncCount;
      return kEchoSyncNames;
   default:
      count = 0;
      return nullptr;
   }
}

inline bool isPercent(clap_id id) {
   if (bodyOf(id) >= 0)
      return bodyRole(id) == kBody1Amount;
   switch (id) {
   case kMotion:
   case kOrbitPan:
   case kOrbitTone:
   case kOrbitSwirl:
   case kDiffusion:
   case kReact:
   case kFeedback:
   case kShimmer:
   case kTilt:
   case kSmear:
   case kDriveTone:
   case kDriveBias:
   case kDriveMix:
   case kEchoFeedback:
   case kEchoTone:
   case kEchoMix:
   case kWidth:
   case kDuck:
   case kMix:
      return true;
   default:
      return false;
   }
}

inline bool isToggle(clap_id id) {
   return id == kRun || id == kFreeze || id == kDriveOn || id == kEchoOn || id == kEchoPingPong;
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   if (isToggle(id)) {
      std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
      return;
   }
   if (isPercent(id)) {
      std::snprintf(out, size, kParams[id].min < 0.0 ? "%+.0f%%" : "%.0f%%", value * 100.0);
      return;
   }
   const int body = bodyOf(id);
   if (body >= 0) {
      std::snprintf(out, size, bodyRole(id) == kBody1Velocity ? "x%.2f" : "%.2f", value);
      return;
   }
   switch (id) {
   case kDecay:
      // The format is chosen from the value as it will print, so 9.996 shows as 10.0 s, not 10.00 s.
      std::snprintf(out, size, value < 9.995 ? "%.2f s" : "%.1f s", value);
      break;
   case kSize:
      std::snprintf(out, size, "%.2f AU", value);
      break;
   case kGravity:
      std::snprintf(out, size, "G x%.2f", value);
      break;
   case kBodies:
   case kSyncBars:
      std::snprintf(out, size, "%d", static_cast<int>(value + 0.5));
      break;
   case kAttack:
      if (std::fabs(value) < 0.005)
         std::snprintf(out, size, "Off");
      else
         std::snprintf(out, size, "%s %.0f%%", value < 0 ? "Reverse" : "Swell", std::fabs(value) * 100.0);
      break;
   case kEarlyLate:
      std::snprintf(out, size, "%.0f%% late", value * 100.0);
      break;
   case kPredelay:
   case kEchoTime:
      std::snprintf(out, size, "%.0f ms", value);
      break;
   case kDampHigh:
   case kBassXover:
   case kLowCut:
   case kHighCut:
      if (value >= 999.5)
         std::snprintf(out, size, "%.1f kHz", value / 1000.0);
      else
         std::snprintf(out, size, "%.0f Hz", value);
      break;
   case kBassMult:
      std::snprintf(out, size, "x%.2f", value);
      break;
   case kShimmerPitch:
      std::snprintf(out, size, "%+d st", static_cast<int>(value + (value < 0 ? -0.5 : 0.5)));
      break;
   case kDrive:
   case kDriveTrim:
   case kLevel:
      std::snprintf(out, size, "%+.1f dB", value);
      break;
   default:
      std::snprintf(out, size, "%.2f", value);
      break;
   }
}

// The reverse of formatParam, also accepting plain numbers. Hosts check that the two round-trip.
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
   if (isToggle(id) && (!std::strcmp(text, "On") || !std::strcmp(text, "Off"))) {
      value = text[1] == 'n' ? 1.0 : 0.0;
      return true;
   }
   if (id == kAttack) {
      if (!std::strcmp(text, "Off")) {
         value = 0.0;
         return true;
      }
      const bool reverse = !std::strncmp(text, "Reverse", 7), swell = !std::strncmp(text, "Swell", 5);
      if (reverse || swell) {
         value = std::strtod(text + (reverse ? 7 : 5), nullptr) / 100.0 * (reverse ? -1.0 : 1.0);
         return true;
      }
   }
   // Skip a leading "x" or "G x".
   if (!std::strncmp(text, "G x", 3))
      text += 3;
   else if (*text == 'x')
      ++text;
   char *end = nullptr;
   const double v = std::strtod(text, &end);
   if (end == text)
      return false;
   while (*end == ' ')
      ++end;
   if (isPercent(id) || id == kEarlyLate)
      value = v / 100.0;
   else if (*end == 'k' || *end == 'K')
      value = v * 1000.0;
   else
      value = v;
   return true;
}

} // namespace jverb

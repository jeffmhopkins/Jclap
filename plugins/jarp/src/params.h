#pragma once

// JArp's parameters, shared by the plugin and the panel. Ids are indexes into kParams, so keep them
// in order; once released, only append so saved projects keep working. The patterns themselves (the
// notes on the grid, the lanes under it and each one's length) are not params: they are saved with
// the project. Which pattern plays, and how the chain advances, are.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace jarp {

enum ParamId : clap_id {
   // Timing.
   kRate,
   kSwing,
   // Patterns: the slot selected (1..8), and how playing moves along the slots with notes in them.
   kSlot,
   kAdvance,
   // Playback.
   kLatch,
   kRestart,
   // Rows: how held notes map to the grid's rows.
   kRowsBy,
   kEmptyRows,
   kKey,
   kScale,
   // Pressure, from each finger.
   kPressVelocity,
   kCurve,
   kPressRatchet,
   kMaxHits,
   kParamCount
};

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kStep = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kRate, "Rate", "Timing", 0, 5, 2, kChoice},
   {kSwing, "Swing", "Timing", 0.5, 0.75, 0.5, kMod},
   {kSlot, "Pattern", "Patterns", 1, 8, 1, kStep},
   {kAdvance, "Advance", "Patterns", 0, 2, 0, kChoice},
   {kLatch, "Latch", "Playback", 0, 1, 0, kChoice},
   {kRestart, "Restart", "Playback", 0, 2, 1, kChoice},
   {kRowsBy, "Rows By", "Rows", 0, 3, 0, kChoice},
   {kEmptyRows, "Empty Rows", "Rows", 0, 2, 0, kChoice},
   {kKey, "Scale Key", "Rows", 0, 11, 0, kChoice},
   {kScale, "Scale", "Rows", 0, 1, 0, kChoice},
   {kPressVelocity, "Pressure to Velocity", "Pressure", 0, 1, 0.5, kMod},
   {kCurve, "Pressure Curve", "Pressure", -1, 1, 0, kMod},
   {kPressRatchet, "Pressure to Ratchet", "Pressure", 0, 1, 0.6, kMod},
   {kMaxHits, "Max Hits", "Pressure", 1, 8, 4, kStep},
};

// ---- choices ----

inline const char *const kRateNames[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};
// Each rate's step length in beats (quarter notes).
inline const double kRateBeats[] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0};
inline const char *const kOffOnNames[] = {"Off", "On"};
inline const char *const kAdvanceNames[] = {"Off", "Chord", "Loop"};
inline const char *const kRestartNames[] = {"Song", "Note", "Bar"};
// Pitch, Degree, Degree with only the degrees held playing (Held Degree), and Spread.
inline const char *const kRowsByNames[] = {"Pitch", "Degree", "Held Degree", "Spread"};
// Rows above the notes held: silent, the held notes again, or the held notes again an octave up
// each time round (as an arpeggiator's octave range).
inline const char *const kEmptyRowsNames[] = {"Rest", "Wrap", "Octave"};
inline const char *const kKeyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const char *const kScaleNames[] = {"Major", "Minor"};

// Where the pattern starts: locked to the song position (so a chord played half way through a
// pattern starts half way through it, and a Loop chain is sequenced along the song), on the first
// note, or on the bar.
enum class Restart { Song, Note, Bar };
// How playing moves along the patterns with notes in them: not at all (the selected one plays), to
// the next with each new chord (after every finger has let go), or each time the pattern ends.
enum class Advance { Off, Chord, Loop };

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kRate: count = 6; return kRateNames;
   case kAdvance: count = 3; return kAdvanceNames;
   case kLatch: count = 2; return kOffOnNames;
   case kRestart: count = 3; return kRestartNames;
   case kRowsBy: count = 4; return kRowsByNames;
   case kEmptyRows: count = 3; return kEmptyRowsNames;
   case kKey: count = 12; return kKeyNames;
   case kScale: count = 2; return kScaleNames;
   default: count = 0; return nullptr;
   }
}

// The Pattern param's value (1..8, as the panel numbers the slots) as a slot index, 0..7.
inline int slotIndex(double value) { return std::clamp(static_cast<int>(std::lround(value)) - 1, 0, 7); }

inline int choiceIndex(double value, int count) {
   return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// ---- text ----

inline bool isPercent(clap_id id) { return id == kSwing || id == kPressVelocity || id == kPressRatchet; }

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      std::snprintf(out, size, "%s", names[choiceIndex(value, count)]);
      return;
   }
   const long whole = std::lround(value);
   if (isPercent(id))
      std::snprintf(out, size, "%.0f%%", value * 100.0);
   else if (id == kCurve) {
      const long amount = std::lround(std::abs(value) * 100.0);
      if (amount == 0)
         std::snprintf(out, size, "Linear");
      else
         std::snprintf(out, size, "%s %ld%%", value < 0.0 ? "Soft" : "Hard", amount);
   }
   else
      std::snprintf(out, size, "%ld", whole);
}

// The reverse of formatParam. Plain numbers are read in the units it prints (percent for the
// percentages; Curve as Soft/Hard and a percent, or -100..100). NaN and infinity are refused.
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
   double sign = 1.0;
   if (id == kCurve) {
      if (!std::strncmp(text, "Linear", 6)) {
         value = 0.0;
         return true;
      }
      if (!std::strncmp(text, "Soft", 4)) {
         sign = -1.0;
         text += 4;
      }
      else if (!std::strncmp(text, "Hard", 4))
         text += 4;
   }
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v))
      return false;
   value = isPercent(id) || id == kCurve ? sign * v / 100.0 : v;
   return true;
}

// Pressure (0..1) through the curve: soft reaches high values with a light touch, hard needs a
// firm press.
inline double curvePressure(double pressure, double curve) {
   return std::pow(std::clamp(pressure, 0.0, 1.0), std::exp2(std::clamp(curve, -1.0, 1.0) * 1.5));
}

} // namespace jarp

#pragma once

// JKit's parameters: the master section first, then sixteen blocks of the same per-pad params. Ids
// are indexes into params(), so keep them in order; once released, only append (new master params
// would go after the pads, new pad params in a new block of sixteen after that).
//
// Pad p's param f has id kPadBase + p * kPadParamCount + f (padParam(p, f)). Knobs whose sound
// follows a curve (times, frequencies) are stored 0..1 and mapped here, so the knob moves evenly
// through the musical range: decayMs(), attackMs(), lowCutHz(), midFreqHz().

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <jclap/dsp/stereo_delay.h>

namespace jkit {

constexpr int kPads = 16;
// Pad 1 plays on MIDI note 36, pad 16 on 51.
constexpr int kFirstNote = 36;

enum MasterParam : clap_id {
   kMasterLow,
   kMasterMid,
   kMasterMidFreq,
   kMasterHigh,
   kReverbType,
   kReverbSize,
   kReverbDecay,
   kReverbReturn,
   kDelaySync,
   kDelayFeedback,
   kDelayTone,
   kDelayReturn,
   kWidth,
   kBoom,
   kVolume,
   kPadBase
};

enum PadParam : clap_id {
   kLevel,
   kPan,
   kPitch,
   kDecay,
   kStart,
   kEnd,
   kAttack,
   kVelocity,
   kChoke,
   kPlayMode,
   kReverse,
   kLowCut,
   kLow,
   kMid,
   kMidFreq,
   kHigh,
   kReverbSend,
   kDelaySend,
   kPadParamCount
};

constexpr clap_id kParamCount = kPadBase + kPads * kPadParamCount;

constexpr clap_id padParam(int pad, PadParam f) { return kPadBase + static_cast<clap_id>(pad) * kPadParamCount + f; }
// Which pad and field an id is; false for a master param.
inline bool padField(clap_id id, int &pad, PadParam &field) {
   if (id < kPadBase || id >= kParamCount)
      return false;
   pad = static_cast<int>((id - kPadBase) / kPadParamCount);
   field = static_cast<PadParam>((id - kPadBase) % kPadParamCount);
   return true;
}

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

inline const char *const kReverbTypeNames[] = {"Room", "Hall", "Plate", "Dark"};
constexpr int kReverbTypeCount = 4;
// The delay's note values: JSynth's list without Free.
inline const char *const *delaySyncNames() { return jclap::dsp::kDelaySyncNames + 1; }
constexpr int kDelaySyncChoices = jclap::dsp::kDelaySyncCount - 1;
inline const char *const kChokeNames[] = {"None", "1", "2", "3", "4"};
constexpr int kChokeCount = 5;
inline const char *const kPlayModeNames[] = {"One shot", "Gate"};
constexpr int kPlayModeCount = 2;

// ---- the curves ----

// Decay: 10 ms to 10 s, and at the top, Full (the sound plays out with no fade).
inline double decayMs(double v) { return 10.0 * std::pow(1000.0, std::clamp(v, 0.0, 1.0)); }
inline bool decayFull(double v) { return v >= 0.999; }
inline double decayValue(double ms) { return std::clamp(std::log(ms / 10.0) / std::log(1000.0), 0.0, 1.0); }
// Attack: 0 to 200 ms, finer at the short end.
inline double attackMs(double v) { return 200.0 * v * v; }
inline double attackValue(double ms) { return std::sqrt(std::clamp(ms / 200.0, 0.0, 1.0)); }
// Low cut: off at 0, then 20 Hz to 2 kHz.
inline double lowCutHz(double v) { return 20.0 * std::pow(100.0, std::clamp(v, 0.0, 1.0)); }
inline bool lowCutOff(double v) { return v <= 0.001; }
inline double lowCutValue(double hz) { return std::clamp(std::log(hz / 20.0) / std::log(100.0), 0.0, 1.0); }
// The mid band's centre: 200 Hz to 8 kHz.
inline double midFreqHz(double v) { return 200.0 * std::pow(40.0, std::clamp(v, 0.0, 1.0)); }
inline double midFreqValue(double hz) { return std::clamp(std::log(hz / 200.0) / std::log(40.0), 0.0, 1.0); }

// ---- the table ----

namespace detail {
inline const char *keep(std::string s) {
   static std::deque<std::string> names;
   names.push_back(std::move(s));
   return names.back().c_str();
}

inline std::vector<jclap::core::ParamDef> makeParams() {
   std::vector<jclap::core::ParamDef> p = {
      {kMasterLow, "Master Low", "Master", -15.0, 15.0, 0.0, kMod},
      {kMasterMid, "Master Mid", "Master", -15.0, 15.0, 0.0, kMod},
      {kMasterMidFreq, "Master Mid Freq", "Master", 0.0, 1.0, midFreqValue(1000.0), kMod},
      {kMasterHigh, "Master High", "Master", -15.0, 15.0, 0.0, kMod},
      {kReverbType, "Reverb Type", "Reverb", 0.0, kReverbTypeCount - 1, 0.0, kChoice},
      {kReverbSize, "Reverb Size", "Reverb", 0.0, 1.0, 0.4, kMod},
      {kReverbDecay, "Reverb Decay", "Reverb", 0.2, 8.0, 1.2, kMod},
      {kReverbReturn, "Reverb Return", "Reverb", -60.0, 6.0, -6.0, kMod},
      {kDelaySync, "Delay Time", "Delay", 0.0, kDelaySyncChoices - 1, 6.0, kChoice},
      {kDelayFeedback, "Delay Feedback", "Delay", 0.0, 0.9, 0.3, kMod},
      {kDelayTone, "Delay Tone", "Delay", 0.0, 1.0, 0.5, kMod},
      {kDelayReturn, "Delay Return", "Delay", -60.0, 6.0, -9.0, kMod},
      {kWidth, "Width", "Output", 0.0, 2.0, 1.0, kMod},
      {kBoom, "Boom", "Output", 0.0, 1.0, 0.0, kMod},
      {kVolume, "Volume", "Output", -60.0, 6.0, 0.0, kMod},
   };
   struct Field {
      const char *name;
      double min, max, def;
      clap_param_info_flags flags;
   };
   static const Field fields[kPadParamCount] = {
      {"Level", -60.0, 12.0, 0.0, kMod},
      {"Pan", -1.0, 1.0, 0.0, kMod},
      {"Pitch", -24.0, 24.0, 0.0, kMod},
      {"Decay", 0.0, 1.0, 1.0, kMod},
      {"Start", 0.0, 1.0, 0.0, kMod},
      {"End", 0.0, 1.0, 1.0, kMod},
      {"Attack", 0.0, 1.0, 0.0, kMod},
      {"Velocity", 0.0, 1.0, 0.6, kMod},
      {"Choke", 0.0, kChokeCount - 1, 0.0, kChoice},
      {"Play", 0.0, kPlayModeCount - 1, 0.0, kChoice},
      {"Reverse", 0.0, 1.0, 0.0, kAuto | CLAP_PARAM_IS_STEPPED},
      {"Low Cut", 0.0, 1.0, 0.0, kMod},
      {"Low", -15.0, 15.0, 0.0, kMod},
      {"Mid", -15.0, 15.0, 0.0, kMod},
      {"Mid Freq", 0.0, 1.0, midFreqValue(1000.0), kMod},
      {"High", -15.0, 15.0, 0.0, kMod},
      {"Reverb Send", 0.0, 1.0, 0.0, kMod},
      {"Delay Send", 0.0, 1.0, 0.0, kMod},
   };
   for (int pad = 0; pad < kPads; ++pad) {
      const char *module = keep("Pad " + std::to_string(pad + 1));
      for (int f = 0; f < static_cast<int>(kPadParamCount); ++f) {
         const Field &d = fields[f];
         p.push_back({padParam(pad, static_cast<PadParam>(f)), keep(std::string(module) + " " + d.name), module, d.min,
                      d.max, d.def, d.flags});
      }
   }
   return p;
}
} // namespace detail

inline const std::vector<jclap::core::ParamDef> &params() {
   static const std::vector<jclap::core::ParamDef> p = detail::makeParams();
   return p;
}
inline const jclap::core::ParamDef &param(clap_id id) { return params()[id]; }

inline const char *const *choiceNames(clap_id id, int &count) {
   int pad;
   PadParam f;
   if (padField(id, pad, f)) {
      count = f == kChoke ? kChokeCount : f == kPlayMode ? kPlayModeCount : 0;
      return f == kChoke ? kChokeNames : f == kPlayMode ? kPlayModeNames : nullptr;
   }
   count = id == kReverbType ? kReverbTypeCount : id == kDelaySync ? kDelaySyncChoices : 0;
   return id == kReverbType ? kReverbTypeNames : id == kDelaySync ? delaySyncNames() : nullptr;
}

// What kind of value an id holds, for formatting.
enum class Unit { Db, Pan, Semitones, Decay, Attack, Percent, LowCut, Hz, Seconds, OnOff, Choice };
inline Unit unitOf(clap_id id) {
   int pad;
   PadParam f;
   if (padField(id, pad, f)) {
      switch (f) {
      case kLevel:
      case kLow:
      case kMid:
      case kHigh:
         return Unit::Db;
      case kPan:
         return Unit::Pan;
      case kPitch:
         return Unit::Semitones;
      case kDecay:
         return Unit::Decay;
      case kAttack:
         return Unit::Attack;
      case kLowCut:
         return Unit::LowCut;
      case kMidFreq:
         return Unit::Hz;
      case kReverse:
         return Unit::OnOff;
      case kChoke:
      case kPlayMode:
         return Unit::Choice;
      default:
         return Unit::Percent; // start, end, velocity, sends
      }
   }
   switch (id) {
   case kMasterLow:
   case kMasterMid:
   case kMasterHigh:
   case kReverbReturn:
   case kDelayReturn:
   case kVolume:
      return Unit::Db;
   case kMasterMidFreq:
      return Unit::Hz;
   case kReverbDecay:
      return Unit::Seconds;
   case kReverbType:
   case kDelaySync:
      return Unit::Choice;
   default:
      return Unit::Percent; // size, feedback, tone, width, boom
   }
}

inline void formatHz(double hz, char *out, size_t size) {
   if (hz >= 1000.0)
      std::snprintf(out, size, "%.*f kHz", hz >= 9950.0 ? 0 : 1, hz / 1000.0);
   else
      std::snprintf(out, size, "%.0f Hz", hz);
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      const int i = static_cast<int>(value + 0.5);
      std::snprintf(out, size, "%s", names[i < 0 ? 0 : i >= count ? count - 1 : i]);
      return;
   }
   switch (unitOf(id)) {
   case Unit::Db:
      if (value <= -59.95)
         std::snprintf(out, size, "-inf dB");
      else if (std::fabs(value) < 0.05)
         std::snprintf(out, size, "0.0 dB");
      else
         std::snprintf(out, size, "%+.1f dB", value);
      break;
   case Unit::Pan: {
      const double percent = std::round(std::fabs(value) * 100.0);
      if (percent == 0.0)
         std::snprintf(out, size, "C");
      else
         std::snprintf(out, size, "%.0f%c", percent, value < 0 ? 'L' : 'R');
      break;
   }
   case Unit::Semitones: {
      const double r = std::round(value * 10.0) / 10.0;
      if (std::fabs(r) < 1e-6)
         std::snprintf(out, size, "0 st");
      else if (std::fabs(r - std::round(r)) < 1e-6)
         std::snprintf(out, size, "%+d st", static_cast<int>(std::round(r)));
      else
         std::snprintf(out, size, "%+.1f st", r);
      break;
   }
   case Unit::Decay: {
      if (decayFull(value)) {
         std::snprintf(out, size, "Full");
         break;
      }
      const double ms = decayMs(value);
      if (ms >= 1000.0)
         std::snprintf(out, size, "%.2f s", ms / 1000.0);
      else
         std::snprintf(out, size, "%.0f ms", ms);
      break;
   }
   case Unit::Attack:
      std::snprintf(out, size, "%.*f ms", attackMs(value) < 10.0 ? 1 : 0, attackMs(value));
      break;
   case Unit::LowCut:
      if (lowCutOff(value))
         std::snprintf(out, size, "Off");
      else
         formatHz(lowCutHz(value), out, size);
      break;
   case Unit::Hz:
      formatHz(midFreqHz(value), out, size);
      break;
   case Unit::Seconds:
      std::snprintf(out, size, "%.1f s", value);
      break;
   case Unit::Percent:
      std::snprintf(out, size, "%.0f%%", value * 100.0);
      break;
   default:
      std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
      break;
   }
}

// The reverse of formatParam, also taking plain numbers in the units it prints. Hosts check that
// the two round-trip.
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
   const Unit unit = unitOf(id);
   if (unit == Unit::OnOff && (!std::strcmp(text, "On") || !std::strcmp(text, "Off"))) {
      value = text[1] == 'n' ? 1.0 : 0.0;
      return true;
   }
   if (unit == Unit::Pan && (text[0] == 'C' || text[0] == 'c') && text[1] == '\0') {
      value = 0.0;
      return true;
   }
   if (unit == Unit::Decay && !std::strcmp(text, "Full")) {
      value = 1.0;
      return true;
   }
   if (unit == Unit::LowCut && !std::strcmp(text, "Off")) {
      value = 0.0;
      return true;
   }
   if (unit == Unit::Db && !std::strncmp(text, "-inf", 4)) {
      value = -60.0;
      return true;
   }
   double v = 0.0;
   const char *end = nullptr;
   if (!jclap::core::parseNumber(text, v, &end))
      return false;
   while (*end == ' ')
      ++end;
   switch (unit) {
   case Unit::Pan:
      if (*end == 'L' || *end == 'l' || *end == 'R' || *end == 'r')
         value = (*end == 'L' || *end == 'l' ? -std::fabs(v) : std::fabs(v)) / 100.0;
      else
         value = v / 100.0;
      return true;
   case Unit::Percent:
      value = v / 100.0;
      return true;
   case Unit::Decay:
      value = decayValue(*end == 's' ? v * 1000.0 : v);
      // "10 s" is Full's neighbour; anything that long is Full.
      return true;
   case Unit::Attack:
      value = attackValue(v);
      return true;
   case Unit::LowCut:
   case Unit::Hz: {
      const double hz = (*end == 'k' || *end == 'K') ? v * 1000.0 : v;
      value = unit == Unit::LowCut ? lowCutValue(hz) : midFreqValue(hz);
      return true;
   }
   default:
      value = v;
      return true;
   }
}

} // namespace jkit

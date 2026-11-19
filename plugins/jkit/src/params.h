#pragma once

// JKit's parameters: the master section first, then sixteen blocks of the same per-pad params. Ids
// are indexes into params(), so keep them in order; once released, only append (new master params
// would go after the pads, new pad params in a new block of sixteen after that).
//
// Pad p's first 18 params (Level to Delay Send, JKit 0.1) have ids kPadBase + p * 18 + f; the tone
// params added in 0.5 (the filter, drive and the EQ's frequencies) come in a second block of sixteen
// after them. padParam(p, f) and padField() hide the split. Knobs whose sound follows a curve (times,
// frequencies) are stored 0..1 and mapped here, so the knob moves evenly through the musical range:
// decayMs(), attackMs(), lowCutHz(), midFreqHz(), cutoffHz() and so on.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <jclap/dsp/stereo_delay.h>

#include "tone.h"

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
   // The second block (0.4): PAD TONE's six points (type, frequency, gain, Q each; pointParam()),
   // the envelope and velocity on its filters, and drive.
   kPointBase,
   kFilterEnv = kPointBase + kTonePoints * 4,
   kFilterDecay,
   kVelFilter,
   kDrive,
   kPadParamCount
};
enum PointField { kPointType, kPointFreq, kPointGain, kPointQ };
constexpr PadParam pointParam(int point, PointField f) { return static_cast<PadParam>(kPointBase + point * 4 + f); }
// Which point and field a pad param is; false if it isn't one.
inline bool pointField(PadParam f, int &point, PointField &field) {
   if (f < kPointBase || f >= kFilterEnv)
      return false;
   point = (f - kPointBase) / 4;
   field = static_cast<PointField>((f - kPointBase) % 4);
   return true;
}
// The first block's EQ (0.1 to 0.3), kept so old projects and automation load, but hidden: loading
// moves it onto points (migrateLegacyEq()).
constexpr bool legacyEq(PadParam f) { return f >= kLowCut && f <= kHigh; }

constexpr clap_id kPadBlock1 = kPointBase, kPadBlock2 = kPadParamCount - kPointBase;
constexpr clap_id kPadBase2 = kPadBase + kPads * kPadBlock1;
constexpr clap_id kParamCount = kPadBase2 + kPads * kPadBlock2;

constexpr clap_id padParam(int pad, PadParam f) {
   return f < kPadBlock1 ? kPadBase + static_cast<clap_id>(pad) * kPadBlock1 + f
                         : kPadBase2 + static_cast<clap_id>(pad) * kPadBlock2 + (f - kPadBlock1);
}
// Which pad and field an id is; false for a master param.
inline bool padField(clap_id id, int &pad, PadParam &field) {
   if (id < kPadBase || id >= kParamCount)
      return false;
   if (id < kPadBase2) {
      pad = static_cast<int>((id - kPadBase) / kPadBlock1);
      field = static_cast<PadParam>((id - kPadBase) % kPadBlock1);
   } else {
      pad = static_cast<int>((id - kPadBase2) / kPadBlock2);
      field = static_cast<PadParam>(kPadBlock1 + (id - kPadBase2) % kPadBlock2);
   }
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
// The filter envelope's decay: 5 ms to 2 s.
inline double filterDecayMs(double v) { return 5.0 * std::pow(400.0, std::clamp(v, 0.0, 1.0)); }
inline double filterDecayValue(double ms) { return std::clamp(std::log(ms / 5.0) / std::log(400.0), 0.0, 1.0); }
// How far the envelope and velocity move the cutoff, at full: octaves.
constexpr double kFilterEnvOctaves = 6.0, kVelFilterOctaves = 4.0;
// A new pad's points: a high pass at 20 Hz and a low pass at 20 kHz (both all but open), then four
// unused, spread across the range so the first one added lands somewhere useful.
inline TonePoint defaultPoint(int i) {
   static const double hz[kTonePoints] = {20.0, 20000.0, 100.0, 400.0, 1600.0, 6000.0};
   TonePoint p;
   p.type = i == 0 ? kToneHp12 : i == 1 ? kToneLp12 : kToneOff;
   p.hz = hz[std::clamp(i, 0, kTonePoints - 1)];
   return p;
}

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
   static const Field fields[kPadBlock1] = {
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
   std::vector<Field> all(fields, fields + kPadBlock1);
   for (PadParam f = kLowCut; f <= kHigh; f = static_cast<PadParam>(f + 1))
      all[f].flags |= CLAP_PARAM_IS_HIDDEN;
   for (int i = 0; i < kTonePoints; ++i) {
      const TonePoint d = defaultPoint(i);
      const std::string n = "Point " + std::to_string(i + 1);
      all.push_back({keep(n + " Type"), 0.0, kToneTypeCount - 1, static_cast<double>(d.type), kChoice});
      all.push_back({keep(n + " Freq"), 0.0, 1.0, toneHzValue(d.hz), kMod});
      all.push_back({keep(n + " Gain"), -kToneMaxDb, kToneMaxDb, 0.0, kMod});
      all.push_back({keep(n + " Q"), 0.0, 1.0, toneQValue(0.7071), kMod});
   }
   all.push_back({"Filter Env", -1.0, 1.0, 0.0, kMod});
   all.push_back({"Filter Decay", 0.0, 1.0, filterDecayValue(200.0), kMod});
   all.push_back({"Velocity to Filter", -1.0, 1.0, 0.0, kMod});
   all.push_back({"Drive", 0.0, 1.0, 0.0, kMod});
   // Both blocks in id order: every pad's first block, then every pad's second.
   for (const auto &[first, last] : {std::pair<int, int>{0, kPadBlock1}, {kPadBlock1, kPadParamCount}})
      for (int pad = 0; pad < kPads; ++pad) {
         const char *module = keep("Pad " + std::to_string(pad + 1));
         for (int f = first; f < last; ++f) {
            const Field &d = all[static_cast<size_t>(f)];
            p.push_back({padParam(pad, static_cast<PadParam>(f)), keep(std::string(module) + " " + d.name), module,
                         d.min, d.max, d.def, d.flags});
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

// Moves a pad's 0.1 to 0.3 EQ (low cut, low shelf, mid bell, high shelf) onto its points, as they
// sounded, and puts the old params back to their defaults. Does nothing when they're at their
// defaults already, so it's safe to run on every load. get(id) and set(id, value) reach the params.
template <typename Get, typename Set> void migrateLegacyEq(int pad, Get get, Set set) {
   const auto at = [&](PadParam f) { return get(padParam(pad, f)); };
   const auto def = [&](PadParam f) { return param(padParam(pad, f)).defaultValue; };
   bool used = false;
   for (PadParam f : {kLowCut, kLow, kMid, kMidFreq, kHigh})
      used = used || at(f) != def(f);
   if (!used)
      return;
   const auto put = [&](int point, int type, double hz, double db, double q) {
      set(padParam(pad, pointParam(point, kPointType)), static_cast<double>(type));
      set(padParam(pad, pointParam(point, kPointFreq)), toneHzValue(hz));
      set(padParam(pad, pointParam(point, kPointGain)), db);
      set(padParam(pad, pointParam(point, kPointQ)), toneQValue(q));
   };
   const auto freePoint = [&]() {
      for (int i = 2; i < kTonePoints; ++i)
         if (static_cast<int>(get(padParam(pad, pointParam(i, kPointType))) + 0.5) == kToneOff)
            return i;
      return -1;
   };
   if (!lowCutOff(at(kLowCut)))
      put(0, kToneHp12, lowCutHz(at(kLowCut)), 0.0, 0.7071);
   const auto add = [&](int type, double hz, double db, double q) {
      if (std::fabs(db) < 0.05)
         return;
      const int i = freePoint();
      if (i >= 0)
         put(i, type, hz, db, q);
   };
   add(kToneLowShelf, 120.0, at(kLow), 0.7071);
   add(kToneBell, midFreqHz(at(kMidFreq)), at(kMid), 0.9);
   add(kToneHighShelf, 7000.0, at(kHigh), 0.7071);
   for (PadParam f : {kLowCut, kLow, kMid, kMidFreq, kHigh})
      set(padParam(pad, f), def(f));
}

inline const char *const *choiceNames(clap_id id, int &count) {
   int pad;
   PadParam f;
   if (padField(id, pad, f)) {
      int point;
      PointField pf;
      if (pointField(f, point, pf) && pf == kPointType) {
         count = kToneTypeCount;
         return kToneTypeNames;
      }
      count = f == kChoke ? kChokeCount : f == kPlayMode ? kPlayModeCount : 0;
      return f == kChoke ? kChokeNames : f == kPlayMode ? kPlayModeNames : nullptr;
   }
   count = id == kReverbType ? kReverbTypeCount : id == kDelaySync ? kDelaySyncChoices : 0;
   return id == kReverbType ? kReverbTypeNames : id == kDelaySync ? delaySyncNames() : nullptr;
}

// What kind of value an id holds, for formatting.
enum class Unit {
   Db, Pan, Semitones, Decay, Attack, Percent, LowCut, Hz, Seconds, OnOff, Choice,
   ToneHz, Octaves, FilterDecay, Q
};
inline Unit unitOf(clap_id id) {
   int pad;
   PadParam f;
   if (padField(id, pad, f)) {
      int point;
      PointField pf;
      if (pointField(f, point, pf))
         return pf == kPointType ? Unit::Choice : pf == kPointFreq ? Unit::ToneHz : pf == kPointGain ? Unit::Db : Unit::Q;
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
      case kFilterEnv:
      case kVelFilter:
         return Unit::Octaves;
      case kFilterDecay:
         return Unit::FilterDecay;
      default:
         return Unit::Percent; // start, end, velocity, sends, drive
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

// The octaves a pad's Filter Env or Velocity to Filter moves the cutoff at full.
inline double filterOctaves(clap_id id) {
   int pad;
   PadParam f;
   return padField(id, pad, f) && f == kVelFilter ? kVelFilterOctaves : kFilterEnvOctaves;
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
   case Unit::ToneHz:
      formatHz(toneHz(value), out, size);
      break;
   case Unit::Octaves: {
      const double o = std::round(value * (filterOctaves(id)) * 10.0) / 10.0;
      if (std::fabs(o) < 0.05)
         std::snprintf(out, size, "0 oct");
      else
         std::snprintf(out, size, "%+.1f oct", o);
      break;
   }
   case Unit::FilterDecay: {
      const double ms = filterDecayMs(value);
      if (ms >= 1000.0)
         std::snprintf(out, size, "%.2f s", ms / 1000.0);
      else
         std::snprintf(out, size, "%.0f ms", ms);
      break;
   }
   case Unit::Q:
      std::snprintf(out, size, "Q %.2f", toneQ(value));
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
   if (unit == Unit::Q && (text[0] == 'Q' || text[0] == 'q')) {
      ++text;
      double q = 0.0;
      if (!jclap::core::parseNumber(text, q, nullptr))
         return false;
      value = toneQValue(q);
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
   case Unit::Hz:
   case Unit::ToneHz: {
      const double hz = (*end == 'k' || *end == 'K') ? v * 1000.0 : v;
      value = unit == Unit::LowCut ? lowCutValue(hz) : unit == Unit::Hz ? midFreqValue(hz) : toneHzValue(hz);
      return true;
   }
   case Unit::Octaves:
      value = std::clamp(v / filterOctaves(id), -1.0, 1.0);
      return true;
   case Unit::Q:
      value = toneQValue(v);
      return true;
   case Unit::FilterDecay:
      value = filterDecayValue(*end == 's' ? v * 1000.0 : v);
      return true;
   default:
      value = v;
      return true;
   }
}

} // namespace jkit

#pragma once

// Monolith's parameters, shared by the plugin and the panel. Ids are indexes into kParams, so keep
// them in order; once released, only append so saved projects keep working.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace monolith {

enum ParamId : clap_id {
   // Scribe: what gets written.
   kOrthography,
   kSeed,
   kLexicon,
   kDensity,
   kGrammar,
   // Slab: what happens to it once written.
   kDrift,
   kErode,
   kLock,
   kLength,
   // Voice: how it is read.
   kRate,
   kDirection,
   kMode,
   kSwing,
   // Dialect: what the rows sound as.
   kRoot,
   kScale,
   kSpread,
   kOctave,
   // Output.
   kVelocity,
   kGate,
   kHumanize,
   kChannel,
   kFollow,
   // Slab, appended after release: the first glyph read.
   kStart,
   kParamCount
};

constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kStep = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
constexpr auto kModStep = kMod | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;

inline const jclap::core::ParamDef kParams[kParamCount] = {
   {kOrthography, "Orthography", "Scribe", 0, 4, 0, kChoice},
   {kSeed, "Seed", "Scribe", 0, 9999, 1, kStep},
   {kLexicon, "Lexicon", "Scribe", 4, 64, 16, kStep},
   {kDensity, "Density", "Scribe", 1, 6, 3, kStep},
   {kGrammar, "Grammar", "Scribe", 0, 1, 0.3, kMod},
   {kDrift, "Drift", "Slab", 0, 1, 0.25, kMod},
   {kErode, "Erode", "Slab", 0, 1, 0, kMod},
   {kLock, "Lock", "Slab", 0, 1, 0, kStep},
   {kLength, "Length", "Slab", 1, 55, 2, kStep},
   {kRate, "Rate", "Voice", 0, 5, 2, kChoice},
   {kDirection, "Direction", "Voice", 0, 3, 0, kChoice},
   {kMode, "Mode", "Voice", 0, 1, 0, kChoice},
   {kSwing, "Swing", "Voice", 0, 0.75, 0, kMod},
   {kRoot, "Root", "Dialect", 0, 11, 9, kChoice},
   {kScale, "Scale", "Dialect", 0, 11, 1, kChoice},
   {kSpread, "Spread", "Dialect", 0, 3, 1, kChoice},
   {kOctave, "Octave", "Dialect", -3, 3, 0, kStep},
   {kVelocity, "Velocity", "Output", 0, 1, 0.8, kMod},
   {kGate, "Gate", "Output", 0.1, 2, 0.9, kMod},
   {kHumanize, "Humanize", "Output", 0, 1, 0, kMod},
   {kChannel, "MIDI Channel", "Output", 1, 16, 1, kStep},
   {kFollow, "Key Follow", "Output", 0, 1, 0, kStep},
   {kStart, "Start", "Slab", 1, 55, 1, kModStep},
};

// ---- choices ----

inline const char *const kOrthographyNames[] = {"Monolith", "Runic", "Circuit", "Halo", "Cuneiform"};
inline const char *const kRateNames[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};
// Each rate's step length in beats (quarter notes).
inline const double kRateBeats[] = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0};
inline const char *const kDirectionNames[] = {"Forward", "Reverse", "Ping-pong", "Random"};
inline const char *const kModeNames[] = {"Speak", "Listen"};
inline const char *const kRootNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline const char *const kScaleNames[] = {"Major",          "Minor",           "Dorian",           "Phrygian",
                                          "Lydian",         "Mixolydian",      "Locrian",          "Harmonic Minor",
                                          "Major Pentatonic", "Minor Pentatonic", "Whole Tone",     "Chromatic"};
inline const char *const kSpreadNames[] = {"Steps", "Thirds", "Fourths", "Fifths"};

enum class Direction { Forward, Reverse, PingPong, Random };
enum class Mode { Speak, Listen };

struct Scale {
   int size;
   int intervals[12];
};

inline const Scale kScales[] = {
   {7, {0, 2, 4, 5, 7, 9, 11}}, {7, {0, 2, 3, 5, 7, 8, 10}}, {7, {0, 2, 3, 5, 7, 9, 10}},
   {7, {0, 1, 3, 5, 7, 8, 10}}, {7, {0, 2, 4, 6, 7, 9, 11}}, {7, {0, 2, 4, 5, 7, 9, 10}},
   {7, {0, 1, 3, 5, 6, 8, 10}}, {7, {0, 2, 3, 5, 7, 8, 11}}, {5, {0, 2, 4, 7, 9}},
   {5, {0, 3, 5, 7, 10}},       {6, {0, 2, 4, 6, 8, 10}},    {12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};

// How many scale degrees apart neighbouring glyph rows are, for each Spread. The root is on the
// third row from the bottom, so two rows reach below it and five above: Steps spans about an
// octave, Thirds two, Fourths three and Fifths four.
inline const int kRowSpreads[] = {1, 2, 3, 4};
constexpr int kRootRow = 5;

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kOrthography: count = 5; return kOrthographyNames;
   case kRate: count = 6; return kRateNames;
   case kDirection: count = 4; return kDirectionNames;
   case kMode: count = 2; return kModeNames;
   case kRoot: count = 12; return kRootNames;
   case kScale: count = 12; return kScaleNames;
   case kSpread: count = 4; return kSpreadNames;
   default: count = 0; return nullptr;
   }
}

inline int choiceIndex(double value, int count) {
   return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// The MIDI note a glyph row plays (row 0 the top, highest, of eight), with the root in octave 4
// (F4 = 65).
inline int rowPitch(int row, int root, int scale, int spread, int octave) {
   const Scale &s = kScales[std::clamp(scale, 0, 11)];
   const int degree = kRowSpreads[std::clamp(spread, 0, 3)] * (kRootRow - std::clamp(row, 0, 7));
   const int octaves = degree >= 0 ? degree / s.size : -((-degree + s.size - 1) / s.size);
   const int index = degree - octaves * s.size;
   return std::clamp(60 + root + 12 * (octave + octaves) + s.intervals[index], 0, 127);
}

// ---- text ----

inline bool isOnOff(clap_id id) { return id == kLock || id == kFollow; }
inline bool isPercent(clap_id id) {
   return id == kGrammar || id == kDrift || id == kErode || id == kSwing || id == kVelocity || id == kGate ||
          id == kHumanize;
}

// Shared by the host's display and the panel.
inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      std::snprintf(out, size, "%s", names[choiceIndex(value, count)]);
      return;
   }
   const long whole = std::lround(value);
   if (isOnOff(id))
      std::snprintf(out, size, "%s", value >= 0.5 ? "On" : "Off");
   else if (isPercent(id))
      std::snprintf(out, size, "%.0f%%", value * 100.0);
   else if (id == kLength)
      std::snprintf(out, size, whole == 1 ? "%ld glyph" : "%ld glyphs", whole);
   else if (id == kStart)
      std::snprintf(out, size, "Glyph %ld", whole);
   else if (id == kLexicon)
      std::snprintf(out, size, "%ld glyphs", whole);
   else if (id == kDensity)
      std::snprintf(out, size, whole == 1 ? "%ld stroke" : "%ld strokes", whole);
   else if (id == kOctave)
      std::snprintf(out, size, "%+ld", whole);
   else if (id == kChannel)
      std::snprintf(out, size, "Ch %ld", whole);
   else
      std::snprintf(out, size, "%ld", whole);
}

// The reverse of formatParam. Plain numbers are read in the units it prints (percent for the
// percentages). NaN and infinity are refused.
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
   if (isOnOff(id) && (!std::strcmp(text, "On") || !std::strcmp(text, "Off"))) {
      value = text[1] == 'n' ? 1.0 : 0.0;
      return true;
   }
   if (id == kChannel && std::strncmp(text, "Ch", 2) == 0)
      text += 2;
   if (id == kStart && std::strncmp(text, "Glyph", 5) == 0)
      text += 5;
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v))
      return false;
   value = isPercent(id) ? v / 100.0 : v;
   return true;
}

} // namespace monolith

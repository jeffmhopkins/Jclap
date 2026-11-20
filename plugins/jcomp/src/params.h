#pragma once

// JComp's parameters, shared by the plugin and the panel. Ids are indexes into kParams, so keep them
// in order; once released, only append. The song itself (every section's chords and takes) is not a
// param: it is saved with the project. The recipe it was made from is, so a preset can remake it.
//
// The recipe params (genre to section energies) aren't automatable: changing one remakes the song,
// which is the panel's job. Everything about playing it is.

#include <jclap/core/helpers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "harmony.h"
#include "players.h"
#include "shared.h"
#include "styles.h"

namespace jcomp {

// What a section note can do: pick a section, play a fill, pick a take, move along, and the held
// actions (RUNS off while held, HOLD CHORD keeps the chord sounding, HOLD keeps the same dice).
enum Action {
   kActIntro, kActVerse, kActPre, kActChorus, kActBridge, kActBreak, kActOutro,
   kActFill,
   kActTake1, kActTake2, kActTake3, kActTake4,
   kActNext, kActPrev, kActRestart, kActStop,
   kActRunsOff, kActHoldChord,
   kActionCount
};
inline const char *const kActionNames[kActionCount] = {
   "Intro", "Verse", "Pre", "Chorus", "Bridge", "Break", "Outro", "Fill", "Take 1", "Take 2", "Take 3", "Take 4",
   "Next", "Prev", "Restart", "Stop", "Runs Off", "Hold Chord"};
// Where each action sits on the keyboard map, in semitones above NOTES FROM: the sections on the
// white keys of the first octave with FILL and the takes on its black keys, then NEXT, PREV,
// RESTART and STOP on the next white keys and the held actions on the black ones (as JDrummer's).
inline const int kActionKey[kActionCount] = {0, 2, 4, 5, 7, 9, 11, 1, 3, 6, 8, 10, 12, 14, 16, 17, 13, 15};

enum ParamId : clap_id {
   // The recipe.
   kGenre,
   kStyle,
   kBlendGenre, // 0 none, else the genre + 1
   kBlend,
   kPlayer,
   kEnergy,
   kWeird,
   kSeed,
   kMutation,
   kSectionEnergy, // seven: one per section
   // Chords.
   kKey = kSectionEnergy + kSections,
   kScale,
   kChordsFrom,
   kChangeAt,
   kLatch,
   kSlashBass,
   kVoiceHeld,
   kChordSplit,
   // Voicing.
   kShape,
   kVoices,
   kCenter,
   kLead,
   kTopMove,
   kMaxSpan,
   kRangeLow,
   kRangeHigh,
   kFloor,
   kDoubleMiddle,
   kNoRubs,
   kSameShape,
   // Playing.
   kSection,
   kTake,
   kSwitchAt,
   kSwitchBy,
   kSongMode,
   kSameDice,
   kAutoFill,
   kSync,
   kPlay,
   kFreeTempo,
   kSwing,
   kSwingFeel,
   // Feel.
   kTiming,
   kVelocityVar,
   kTopVoice,
   kLengths,
   kOnsets,
   kVary,
   // Passing runs.
   kRuns,
   kRunEvery,
   kRunLand,
   kRunHalfStep,
   kRunShorten,
   // Output.
   kChannel,
   kRhythmFrom,
   kBassChannel,
   kFollow,
   // Section notes.
   kNoteMap,
   kNoteOctave,
   kNoteChannel,
   kFillInto,
   kVelEnergy,
   kHeldOnly,
   kPassNotes,
   kLearnNote, // one per action, for the CUSTOM map
   kParamCount = kLearnNote + kActionCount
};

constexpr auto kSaved = 0u; // kept with presets and the project, not automated
constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kStep = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kSavedChoice = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kSavedStep = CLAP_PARAM_IS_STEPPED;

inline int genreMax() { return genreCount() - 1; }

inline const jclap::core::ParamDef *makeParams() {
   static jclap::core::ParamDef p[kParamCount];
   static char energyNames[kSections][32];
   static char learnNames[kActionCount][32];
   const double gmax = genreMax();
   p[kGenre] = {kGenre, "Genre", "Generate", 0, gmax, 0, kSavedChoice};
   p[kStyle] = {kStyle, "Style", "Generate", 0, 15, 0, kSavedStep};
   p[kBlendGenre] = {kBlendGenre, "Blend Genre", "Generate", 0, gmax + 1, 0, kSavedChoice};
   p[kBlend] = {kBlend, "Blend", "Generate", 0, 1, 0.25, kSaved};
   p[kPlayer] = {kPlayer, "Player", "Generate", 0, kPlayers - 1, 0, kSavedChoice};
   p[kEnergy] = {kEnergy, "Energy", "Generate", 0, 1, 0.5, kSaved};
   p[kWeird] = {kWeird, "Weird", "Generate", 0, 1, 0.2, kSaved};
   p[kSeed] = {kSeed, "Seed", "Generate", 0, 99999, 1, kSavedStep};
   p[kMutation] = {kMutation, "Mutation", "Generate", 0, 1, 0.4, kSaved};
   for (int i = 0; i < kSections; ++i) {
      std::snprintf(energyNames[i], sizeof(energyNames[i]), "%s Energy", kSectionNames[i]);
      p[kSectionEnergy + i] = {static_cast<clap_id>(kSectionEnergy + i), energyNames[i], "Generate", 0, 1,
                               kArcEnergy[i], kSaved};
   }
   p[kKey] = {kKey, "Key", "Chords", 0, 11, 4, kSavedChoice};
   p[kScale] = {kScale, "Scale", "Chords", 0, kScaleCount - 1, kMixolydian, kSavedChoice};
   p[kChordsFrom] = {kChordsFrom, "Chords From", "Chords", 0, 2, 2, kChoice};
   p[kChangeAt] = {kChangeAt, "New Chord Starts", "Chords", 0, 3, 2, kChoice};
   p[kLatch] = {kLatch, "Latch", "Chords", 0, 1, 1, kChoice};
   p[kSlashBass] = {kSlashBass, "Lowest Note Is The Bass", "Chords", 0, 1, 1, kChoice};
   p[kVoiceHeld] = {kVoiceHeld, "Voice What I Hold", "Chords", 0, 2, 0, kChoice};
   p[kChordSplit] = {kChordSplit, "Chord Keys From", "Chords", 24, 96, 48, kStep};
   p[kShape] = {kShape, "Shape", "Voicing", 0, kShapeCount - 1, kDrop2, kChoice};
   p[kVoices] = {kVoices, "Voices", "Voicing", 3, 5, 4, kStep};
   p[kCenter] = {kCenter, "Center", "Voicing", 48, 76, 62, kStep};
   p[kLead] = {kLead, "Lead", "Voicing", 0, 1, 0.85, kMod};
   p[kTopMove] = {kTopMove, "Top Move", "Voicing", 1, 12, 4, kStep};
   p[kMaxSpan] = {kMaxSpan, "Spread", "Voicing", 7, 30, 17, kStep};
   p[kRangeLow] = {kRangeLow, "Range Low", "Voicing", 36, 72, 50, kStep};
   p[kRangeHigh] = {kRangeHigh, "Range High", "Voicing", 60, 96, 76, kStep};
   p[kFloor] = {kFloor, "Bass Floor", "Voicing", 28, 60, 43, kStep};
   p[kDoubleMiddle] = {kDoubleMiddle, "Double In The Middle", "Voicing", 0, 1, 1, kChoice};
   p[kNoRubs] = {kNoRubs, "No Low Rubs", "Voicing", 0, 1, 1, kChoice};
   p[kSameShape] = {kSameShape, "Same Shape For Repeats", "Voicing", 0, 1, 1, kChoice};
   p[kSection] = {kSection, "Section", "Play", 0, kSections - 1, 0, kChoice};
   p[kTake] = {kTake, "Take", "Play", 0, kTakes - 1, 0, kChoice};
   p[kSwitchAt] = {kSwitchAt, "Switch At", "Play", 0, 4, 1, kChoice};
   p[kSwitchBy] = {kSwitchBy, "Switch By", "Play", 0, 2, 0, kChoice};
   p[kSongMode] = {kSongMode, "Song Mode", "Play", 0, 1, 0, kChoice};
   p[kSameDice] = {kSameDice, "Same Dice Every", "Play", 0, 4, 3, kChoice};
   p[kAutoFill] = {kAutoFill, "Auto Fills", "Play", 0, 3, 2, kChoice};
   p[kSync] = {kSync, "Sync", "Transport", 0, 2, 0, kChoice};
   p[kPlay] = {kPlay, "Play", "Transport", 0, 1, 0, kChoice};
   p[kFreeTempo] = {kFreeTempo, "Tempo When Free", "Transport", 40, 300, 100, kStep};
   p[kSwing] = {kSwing, "Swing", "Feel", 0.5, 0.75, 0.53, kMod};
   p[kSwingFeel] = {kSwingFeel, "Swing Feel", "Feel", 0, 1, 0, kChoice};
   p[kTiming] = {kTiming, "Timing", "Feel", -20, 40, 6, kMod};
   p[kVelocityVar] = {kVelocityVar, "Velocity", "Feel", 0, 24, 8, kMod};
   p[kTopVoice] = {kTopVoice, "Top Voice", "Feel", 0, 24, 6, kMod};
   p[kLengths] = {kLengths, "Lengths", "Feel", 0, 0.6, 0.25, kMod};
   p[kOnsets] = {kOnsets, "Chord Notes Start", "Feel", 0, 3, 0, kChoice};
   p[kVary] = {kVary, "Vary", "Feel", 0, 1, 0.2, kMod};
   p[kRuns] = {kRuns, "Runs", "Runs", 0, 1, 0.25, kMod};
   p[kRunEvery] = {kRunEvery, "Runs At Most", "Runs", 0, 2, 1, kChoice};
   p[kRunLand] = {kRunLand, "Runs Land On", "Runs", 0, 1, 0, kChoice};
   p[kRunHalfStep] = {kRunHalfStep, "Half Step From Below", "Runs", 0, 1, 1, kChoice};
   p[kRunShorten] = {kRunShorten, "Shorten The Chord Before", "Runs", 0, 1, 1, kChoice};
   p[kChannel] = {kChannel, "Channel", "Output", 1, 16, 1, kStep};
   p[kRhythmFrom] = {kRhythmFrom, "Rhythm From", "Output", 0, 2, 0, kChoice};
   p[kBassChannel] = {kBassChannel, "Bass Channel", "Output", 1, 16, 2, kStep};
   p[kFollow] = {kFollow, "Follow", "Output", 0, 2, 1, kChoice};
   p[kNoteMap] = {kNoteMap, "Note Map", "Notes", 0, 1, 0, kChoice};
   p[kNoteOctave] = {kNoteOctave, "Notes From", "Notes", -1, 7, 0, kStep};
   p[kNoteChannel] = {kNoteChannel, "Notes Channel", "Notes", 0, 16, 0, kStep};
   p[kFillInto] = {kFillInto, "Fill Into New Section", "Notes", 0, 1, 1, kChoice};
   p[kVelEnergy] = {kVelEnergy, "Velocity Sets Energy", "Notes", 0, 1, 0, kChoice};
   p[kHeldOnly] = {kHeldOnly, "Play Only While A Chord Is Held", "Notes", 0, 1, 0, kChoice};
   p[kPassNotes] = {kPassNotes, "Pass Other Notes", "Notes", 0, 1, 0, kChoice};
   for (int i = 0; i < kActionCount; ++i) {
      std::snprintf(learnNames[i], sizeof(learnNames[i]), "Note: %s", kActionNames[i]);
      p[kLearnNote + i] = {static_cast<clap_id>(kLearnNote + i), learnNames[i], "Notes", 0, 127,
                           static_cast<double>(12 + kActionKey[i]), kSavedStep};
   }
   return p;
}

inline const jclap::core::ParamDef *const kParams = makeParams();

// ---- choices ----

inline const char *const kOffOnNames[] = {"Off", "On"};
inline const char *const kTakeNames[] = {"1", "2", "3", "4"};
inline const char *const kSwitchAtNames[] = {"Next Beat", "Next Bar", "Next 2 Bars", "Next 4 Bars", "Section End"};
inline const char *const kSwitchByNames[] = {"Note + Param", "Note", "Param"};
inline const char *const kSongModeNames[] = {"Loop Section", "Play Song"};
inline const char *const kSameDiceNames[] = {"Never", "1 Bar", "2 Bars", "4 Bars", "8 Bars"};
inline const int kSameDiceBars[] = {0, 1, 2, 4, 8};
inline const char *const kAutoFillNames[] = {"Off", "Every 4", "Every 8", "Every 16"};
inline const int kAutoFillBars[] = {0, 4, 8, 16};
inline const char *const kSyncNames[] = {"Auto", "Host", "Free"};
inline const char *const kNoteMapNames[] = {"Keyboard", "Custom"};
inline const char *const kSwingFeelNames[] = {"16ths", "8ths"};
inline const char *const kChordsFromNames[] = {"Progression", "Held Notes", "Both"};
inline const char *const kChangeAtNames[] = {"At Once", "Next 16th", "Next Beat", "Next Bar"};
inline const char *const kVoiceHeldNames[] = {"Its Own Way", "As I Hold It", "My Top Note"};
inline const char *const kOnsetNames[] = {"Together", "Rolled", "Strum Down", "Strum Both"};
inline const char *const kRunEveryNames[] = {"Every Bar", "Every 2 Bars", "Every 4 Bars"};
inline const int kRunEveryBars[] = {1, 2, 4};
inline const char *const kRunLandNames[] = {"Top Note", "Any Note"};
inline const char *const kRhythmFromNames[] = {"Own", "The Bass", "Both"};
inline const char *const kFollowNames[] = {"All", "Strong", "One"};
inline const char *const kKeyNames[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

inline const char *const *genreNames() {
   static std::vector<const char *> names;
   if (names.empty())
      for (int i = 0; i < genreCount(); ++i)
         names.push_back(genreAt(i).name);
   return names.data();
}
inline const char *const *blendNames() {
   static std::vector<const char *> names;
   if (names.empty()) {
      names.push_back("None");
      for (int i = 0; i < genreCount(); ++i)
         names.push_back(genreAt(i).name);
   }
   return names.data();
}
inline const char *const *playerNames() {
   static const char *names[kPlayers];
   for (int i = 0; i < kPlayers; ++i)
      names[i] = kPlayerList[i].name;
   return names;
}

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kGenre: count = genreCount(); return genreNames();
   case kBlendGenre: count = genreCount() + 1; return blendNames();
   case kPlayer: count = kPlayers; return playerNames();
   case kKey: count = 12; return kKeyNames;
   case kScale: count = kScaleCount; return kScaleNames;
   case kChordsFrom: count = 3; return kChordsFromNames;
   case kChangeAt: count = 4; return kChangeAtNames;
   case kVoiceHeld: count = 3; return kVoiceHeldNames;
   case kShape: count = kShapeCount; return kShapeNames;
   case kSection: count = kSections; return kSectionNames;
   case kTake: count = 4; return kTakeNames;
   case kSwitchAt: count = 5; return kSwitchAtNames;
   case kSwitchBy: count = 3; return kSwitchByNames;
   case kSongMode: count = 2; return kSongModeNames;
   case kSameDice: count = 5; return kSameDiceNames;
   case kAutoFill: count = 4; return kAutoFillNames;
   case kSync: count = 3; return kSyncNames;
   case kSwingFeel: count = 2; return kSwingFeelNames;
   case kOnsets: count = 4; return kOnsetNames;
   case kRunEvery: count = 3; return kRunEveryNames;
   case kRunLand: count = 2; return kRunLandNames;
   case kRhythmFrom: count = 3; return kRhythmFromNames;
   case kFollow: count = 3; return kFollowNames;
   case kNoteMap: count = 2; return kNoteMapNames;
   case kPlay: case kLatch: case kSlashBass: case kDoubleMiddle: case kNoRubs: case kSameShape: case kRunHalfStep:
   case kRunShorten: case kFillInto: case kVelEnergy: case kHeldOnly: case kPassNotes:
      count = 2;
      return kOffOnNames;
   default: count = 0; return nullptr;
   }
}

inline int choiceIndex(double value, int count) {
   return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// A note as JComp names it, middle C (60) as C4.
inline std::string noteText(int note) {
   note = std::clamp(note, 0, 127);
   return std::string(kSharpNames[note % 12]) + std::to_string(note / 12 - 1);
}

inline bool isPercent(clap_id id) {
   return id == kBlend || id == kEnergy || id == kWeird || id == kMutation || id == kVary || id == kLead || id == kRuns ||
          id == kSwing || (id >= kSectionEnergy && id < kSectionEnergy + kSections);
}
inline bool isNote(clap_id id) {
   return id == kCenter || id == kRangeLow || id == kRangeHigh || id == kFloor || id == kChordSplit ||
          (id >= kLearnNote && id < kLearnNote + kActionCount);
}

inline void formatParam(clap_id id, double value, char *out, size_t size) {
   int count = 0;
   if (const char *const *names = choiceNames(id, count)) {
      std::snprintf(out, size, "%s", names[choiceIndex(value, count)]);
      return;
   }
   const long whole = std::lround(value);
   if (isPercent(id))
      std::snprintf(out, size, "%.0f%%", value * 100.0);
   else if (isNote(id))
      std::snprintf(out, size, "%s", noteText(static_cast<int>(whole)).c_str());
   else if (id == kFreeTempo)
      std::snprintf(out, size, "%ld BPM", whole);
   else if (id == kTiming)
      std::snprintf(out, size, "%+ld ms", whole);
   else if (id == kVelocityVar)
      std::snprintf(out, size, "\xC2\xB1%ld", whole);
   else if (id == kTopVoice)
      std::snprintf(out, size, "+%ld", whole);
   else if (id == kLengths)
      std::snprintf(out, size, "\xC2\xB1%.0f%%", value * 100.0);
   else if (id == kTopMove || id == kMaxSpan)
      std::snprintf(out, size, "%ld st", whole);
   else if (id == kNoteChannel && whole == 0)
      std::snprintf(out, size, "Any");
   else if (id == kNoteOctave)
      std::snprintf(out, size, "Octave %ld", whole);
   else
      std::snprintf(out, size, "%ld", whole);
}

inline bool parseParam(clap_id id, const char *text, double &value) {
   while (*text == ' ')
      ++text;
   int count = 0;
   if (const char *const *names = choiceNames(id, count))
      for (int i = 0; i < count; ++i)
         if (!std::strcmp(text, names[i])) {
            value = i;
            return true;
         }
   if (id == kNoteChannel && !std::strcmp(text, "Any")) {
      value = 0;
      return true;
   }
   if (!std::strncmp(text, "\xC2\xB1", 2)) // VELOCITY and LENGTHS read "±8", "±25%"
      text += 2;
   if (id == kNoteOctave && !std::strncmp(text, "Octave", 6))
      text += 6;
   if (isNote(id)) {
      // A note name (C2, F#0) or a number.
      static const char letters[] = "C D EF G A B";
      const char *at = std::strchr(letters, text[0]);
      if (text[0] && at && *at != ' ') {
         int note = static_cast<int>(at - letters);
         const char *rest = text + 1;
         if (*rest == '#') {
            ++note;
            ++rest;
         }
         double octave = 0;
         if (!jclap::core::parseNumber(rest, octave))
            return false;
         value = note + (octave + 1) * 12;
         return true;
      }
   }
   double v = 0.0;
   if (!jclap::core::parseNumber(text, v))
      return false;
   value = isPercent(id) ? v / 100.0 : id == kLengths ? v / 100.0 : v;
   return true;
}

} // namespace jcomp

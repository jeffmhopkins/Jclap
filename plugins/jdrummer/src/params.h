#pragma once

// JDrummer's parameters, shared by the plugin and the panel. Ids are indexes into kParams, so keep
// them in order; once released, only append. The song itself (every section's patterns) is not a
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

#include "shared.h"
#include "players.h"
#include "styles.h"

namespace jdrummer {

// What a section note can do: pick a section, play a fill, pick a variation, move along, and the
// held actions.
enum Action {
   kActIntro, kActVerse, kActPre, kActChorus, kActBridge, kActBreak, kActOutro,
   kActFill,
   kActVar1, kActVar2, kActVar3, kActVar4,
   kActNext, kActPrev, kActRestart, kActStop,
   kActMuteHats, kActMuteKick, kActHold,
   kActionCount
};
inline const char *const kActionNames[kActionCount] = {
   "Intro", "Verse", "Pre", "Chorus", "Bridge", "Break", "Outro", "Fill", "Var 1", "Var 2", "Var 3", "Var 4",
   "Next", "Prev", "Restart", "Stop", "Mute Hats", "Mute Kick", "Hold"};
// Where each action sits on the keyboard map, in semitones above NOTES FROM: the sections on the
// white keys of the first octave with FILL and the variations on its black keys, then NEXT, PREV,
// RESTART and STOP on the next white keys and the held actions on the black ones.
inline const int kActionKey[kActionCount] = {0, 2, 4, 5, 7, 9, 11, 1, 3, 6, 8, 10, 12, 14, 16, 17, 13, 15, 18};

enum ParamId : clap_id {
   // The recipe.
   kGenre,
   kStyle,
   kBlendGenre, // 0 none, else the genre + 1
   kBlend,
   kPlayer,
   kPercussionist,
   kEnergy,
   kWeird,
   kSeed,
   kMutation,
   kSectionEnergy, // seven: one per section
   // Playing.
   kSection = kSectionEnergy + kSections,
   kVariation,
   kSwitchAt,
   kSwitchBy,
   kSongMode,
   kVary,
   kGhosts,
   kSameDice,
   kAutoFill,
   kSwing,
   kSync,
   kPlay,
   kFreeTempo,
   // Output.
   kKit,
   kChannel,
   kPercChannel,
   // Section notes.
   kNoteMap,
   kNoteOctave,
   kNoteChannel,
   kFillInto,
   kVelEnergy,
   kHeldOnly,
   kPassNotes,
   kLearnNote, // one per action, for the CUSTOM map
   // Added in 0.2.0.
   kSwingFeel = kLearnNote + kActionCount, // what SWING swings: sixteenths or eighths
   kParamCount
};

constexpr auto kSaved = 0u; // kept with presets and the project, not automated
constexpr auto kAuto = CLAP_PARAM_IS_AUTOMATABLE;
constexpr auto kMod = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
constexpr auto kStep = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
constexpr auto kChoice = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kSavedChoice = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
constexpr auto kSavedStep = CLAP_PARAM_IS_STEPPED;

constexpr int kGenreMax = 33;

inline const jclap::core::ParamDef *makeParams() {
   static jclap::core::ParamDef p[kParamCount];
   static char energyNames[kSections][32];
   static char learnNames[kActionCount][32];
   p[kGenre] = {kGenre, "Genre", "Generate", 0, kGenreMax, 0, kSavedChoice};
   p[kStyle] = {kStyle, "Style", "Generate", 0, 15, 0, kSavedStep};
   p[kBlendGenre] = {kBlendGenre, "Blend Genre", "Generate", 0, kGenreMax + 1, 0, kSavedChoice};
   p[kBlend] = {kBlend, "Blend", "Generate", 0, 1, 0.3, kSaved};
   p[kPlayer] = {kPlayer, "Drummer", "Generate", 0, 5, 5, kSavedChoice};
   p[kPercussionist] = {kPercussionist, "Percussionist", "Generate", 0, 6, 0, kSavedChoice};
   p[kEnergy] = {kEnergy, "Energy", "Generate", 0, 1, 0.5, kSaved};
   p[kWeird] = {kWeird, "Weird", "Generate", 0, 1, 0.15, kSaved};
   p[kSeed] = {kSeed, "Seed", "Generate", 0, 99999, 1, kSavedStep};
   p[kMutation] = {kMutation, "Mutation", "Generate", 0, 1, 0.4, kSaved};
   for (int i = 0; i < kSections; ++i) {
      std::snprintf(energyNames[i], sizeof(energyNames[i]), "%s Energy", kSectionNames[i]);
      p[kSectionEnergy + i] = {static_cast<clap_id>(kSectionEnergy + i), energyNames[i], "Generate", 0, 1,
                               kArcEnergy[i], kSaved};
   }
   p[kSection] = {kSection, "Section", "Play", 0, kSections - 1, 0, kChoice};
   p[kVariation] = {kVariation, "Variation", "Play", 0, kVariations - 1, 0, kChoice};
   p[kSwitchAt] = {kSwitchAt, "Switch At", "Play", 0, 4, 1, kChoice};
   p[kSwitchBy] = {kSwitchBy, "Switch By", "Play", 0, 2, 0, kChoice};
   p[kSongMode] = {kSongMode, "Song Mode", "Play", 0, 1, 0, kChoice};
   p[kVary] = {kVary, "Vary", "Life", 0, 1, 0.2, kMod};
   p[kGhosts] = {kGhosts, "Ghosts", "Life", 0, 1, 0.0, kMod};
   p[kSameDice] = {kSameDice, "Same Dice Every", "Life", 0, 4, 3, kChoice};
   p[kAutoFill] = {kAutoFill, "Auto Fills", "Life", 0, 3, 2, kChoice};
   p[kSwing] = {kSwing, "Swing", "Life", 0.5, 0.75, 0.5, kMod};
   p[kSync] = {kSync, "Sync", "Transport", 0, 2, 0, kChoice};
   p[kPlay] = {kPlay, "Play", "Transport", 0, 1, 0, kChoice};
   p[kFreeTempo] = {kFreeTempo, "Tempo When Free", "Transport", 40, 300, 120, kStep};
   p[kKit] = {kKit, "Kit", "Output", 0, kKitCount - 1, 0, kChoice};
   p[kChannel] = {kChannel, "Channel", "Output", 1, 16, 10, kStep};
   p[kPercChannel] = {kPercChannel, "Percussion Channel", "Output", 1, 16, 11, kStep};
   p[kNoteMap] = {kNoteMap, "Note Map", "Notes", 0, 1, 0, kChoice};
   p[kNoteOctave] = {kNoteOctave, "Notes From", "Notes", -1, 7, 0, kStep};
   p[kNoteChannel] = {kNoteChannel, "Notes Channel", "Notes", 0, 16, 0, kStep};
   p[kFillInto] = {kFillInto, "Fill Into New Section", "Notes", 0, 1, 1, kChoice};
   p[kVelEnergy] = {kVelEnergy, "Velocity Sets Energy", "Notes", 0, 1, 0, kChoice};
   p[kHeldOnly] = {kHeldOnly, "Play Only While Held", "Notes", 0, 1, 0, kChoice};
   p[kPassNotes] = {kPassNotes, "Pass Other Notes", "Notes", 0, 1, 1, kChoice};
   for (int i = 0; i < kActionCount; ++i) {
      std::snprintf(learnNames[i], sizeof(learnNames[i]), "Note: %s", kActionNames[i]);
      p[kLearnNote + i] = {static_cast<clap_id>(kLearnNote + i), learnNames[i], "Notes", 0, 127,
                           static_cast<double>(12 + kActionKey[i]), kSavedStep};
   }
   p[kSwingFeel] = {kSwingFeel, "Swing Feel", "Life", 0, 1, 0, kChoice};
   return p;
}

inline const jclap::core::ParamDef *const kParams = makeParams();

// ---- choices ----

inline const char *const kOffOnNames[] = {"Off", "On"};
inline const char *const kVariationNames[] = {"1", "2", "3", "4"};
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

inline const char *const *genreNames() {
   static const char *names[kGenreMax + 1];
   for (int i = 0; i <= kGenreMax; ++i)
      names[i] = genreAt(i).name;
   return names;
}
inline const char *const *blendNames() {
   static const char *names[kGenreMax + 2];
   names[0] = "None";
   for (int i = 0; i <= kGenreMax; ++i)
      names[i + 1] = genreAt(i).name;
   return names;
}
inline const char *const *playerNames() {
   static const char *names[kDrummers];
   for (int i = 0; i < kDrummers; ++i)
      names[i] = kDrummerList[i].name;
   return names;
}
inline const char *const *percussionistNames() {
   static const char *names[kPercussionists + 1];
   names[0] = "None";
   for (int i = 0; i < kPercussionists; ++i)
      names[i + 1] = kPercussionistList[i].name;
   return names;
}

inline const char *const *choiceNames(clap_id id, int &count) {
   switch (id) {
   case kGenre: count = kGenreMax + 1; return genreNames();
   case kBlendGenre: count = kGenreMax + 2; return blendNames();
   case kPlayer: count = kDrummers; return playerNames();
   case kPercussionist: count = kPercussionists + 1; return percussionistNames();
   case kSection: count = kSections; return kSectionNames;
   case kVariation: count = 4; return kVariationNames;
   case kSwitchAt: count = 5; return kSwitchAtNames;
   case kSwitchBy: count = 3; return kSwitchByNames;
   case kSongMode: count = 2; return kSongModeNames;
   case kSameDice: count = 5; return kSameDiceNames;
   case kAutoFill: count = 4; return kAutoFillNames;
   case kSync: count = 3; return kSyncNames;
   case kPlay: case kFillInto: case kVelEnergy: case kHeldOnly: case kPassNotes: count = 2; return kOffOnNames;
   case kKit: count = kKitCount; return kKitNames;
   case kNoteMap: count = 2; return kNoteMapNames;
   case kSwingFeel: count = 2; return kSwingFeelNames;
   default: count = 0; return nullptr;
   }
}

inline int choiceIndex(double value, int count) {
   return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// A note as JKit's panel names it, middle C (60) as C4: note 36 is C2, 12 is C0.
inline std::string noteText(int note) {
   static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
   note = std::clamp(note, 0, 127);
   return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
}

inline bool isPercent(clap_id id) {
   return id == kBlend || id == kEnergy || id == kWeird || id == kMutation || id == kVary || id == kGhosts ||
          id == kSwing || (id >= kSectionEnergy && id < kSectionEnergy + kSections);
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
   else if (id == kFreeTempo)
      std::snprintf(out, size, "%ld BPM", whole);
   else if (id == kNoteChannel && whole == 0)
      std::snprintf(out, size, "Any");
   else if (id == kNoteOctave)
      std::snprintf(out, size, "Octave %ld", whole);
   else if (id >= kLearnNote && id < kLearnNote + kActionCount)
      std::snprintf(out, size, "%s", noteText(static_cast<int>(whole)).c_str());
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
   if (id == kNoteOctave && !std::strncmp(text, "Octave", 6))
      text += 6;
   if (id >= kLearnNote && id < kLearnNote + kActionCount) {
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
   value = isPercent(id) ? v / 100.0 : v;
   return true;
}

} // namespace jdrummer

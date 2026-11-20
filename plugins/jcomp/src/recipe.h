#pragma once

// The recipe the params hold, and the factory list of styles: what a preset, the panel's style
// picks and the tests share. No UI here.

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "generator.h"
#include "params.h"
#include "styles.h"

namespace jcomp {

// A style's swing: its genre's, more with SHUFFLE.
inline double styleSwing(const Genre &g, const Style &s) {
   double sw = g.swing + (s.mods & kShuffle ? 0.08 : 0.0);
   return std::clamp(sw, 0.5, 0.7);
}

// The player a style is played by: its own, or its genre's.
inline int stylePlayer(const Genre &g, const Style &s) { return s.player >= 0 ? s.player : g.player; }

// The params a style sets when it is picked, on the panel or as a preset: the genre and style, the
// tempo and swing, the player and how that player voices and feels (shape, notes, register, how
// the chord notes start, how far behind the beat), and the scale its progressions are in.
inline std::vector<std::pair<clap_id, double>> styleValues(int genre, int style) {
   const Genre &g = genreAt(genre);
   const Style &s = styleAt(g, style);
   const Player &p = kPlayerList[stylePlayer(g, s)];
   const ProgSet &ps = g.progs[static_cast<size_t>(std::clamp(s.progSet, 0, static_cast<int>(g.progs.size()) - 1))];
   const bool own = s.shape >= 0; // a voicing the style needs, whatever the player
   return {{kGenre, genre},
           {kStyle, std::clamp(style, 0, static_cast<int>(g.styles.size()) - 1)},
           {kSwing, styleSwing(g, s)},
           {kSwingFeel, g.swingFeel},
           {kFreeTempo, s.bpm},
           {kPlayer, stylePlayer(g, s)},
           {kScale, ps.scale},
           {kShape, own ? s.shape : p.shape},
           {kVoices, own ? s.voices : p.voices},
           {kRangeLow, own ? s.lo : p.lo},
           {kRangeHigh, own ? s.hi : p.hi},
           {kCenter, own ? s.center : p.center},
           {kOnsets, p.onsets},
           {kTiming, p.laidBack},
           {kRuns, std::min(1.0, p.runs + (s.mods & kRunsIn ? 0.3 : 0.0))}};
}

// What picking a player sets: how it voices and feels.
inline std::vector<std::pair<clap_id, double>> playerValues(int player) {
   const Player &p = kPlayerList[std::clamp(player, 0, kPlayers - 1)];
   return {{kPlayer, player},  {kShape, p.shape},   {kVoices, p.voices},  {kRangeLow, p.lo}, {kRangeHigh, p.hi},
           {kCenter, p.center}, {kOnsets, p.onsets}, {kTiming, p.laidBack}, {kRuns, p.runs}};
}

struct FactoryEntry {
   std::string name;
   int genre, style;
};

// Every style, named uniquely (a name two genres share gets its genre after it).
inline const std::vector<FactoryEntry> &factoryEntries() {
   static const std::vector<FactoryEntry> entries = [] {
      std::vector<FactoryEntry> e;
      std::set<std::string> seen;
      for (int gi = 0; gi < genreCount(); ++gi) {
         const Genre &g = genreAt(gi);
         for (size_t si = 0; si < g.styles.size(); ++si) {
            std::string name = g.styles[si].name;
            if (!seen.insert(name).second) {
               name += std::string(" (") + g.name + ")";
               seen.insert(name);
            }
            e.push_back({name, gi, static_cast<int>(si)});
         }
      }
      return e;
   }();
   return entries;
}

// How chords are voiced, from the params.
template <typename Get>
VoiceSettings voiceSettings(Get value) {
   const auto whole = [&](clap_id id) { return static_cast<int>(std::lround(value(id))); };
   VoiceSettings v;
   v.shape = std::clamp(whole(kShape), 0, kShapeCount - 1);
   v.voices = std::clamp(whole(kVoices), 3, 5);
   // LOWEST and HIGHEST set the window (at least an octave), OFFSET slides it and its center.
   const int offset = whole(kOffset);
   v.lo = std::clamp(whole(kRangeLow) + offset, 24, 96);
   v.hi = std::clamp(std::max(whole(kRangeLow) + 12, whole(kRangeHigh)) + offset, v.lo + 12, 108);
   v.center = std::clamp(whole(kCenter) + offset, v.lo, v.hi);
   v.floor = std::min(whole(kFloor), v.lo);
   v.topMove = whole(kTopMove);
   v.maxSpan = whole(kMaxSpan);
   v.lead = value(kLead);
   v.doubleMiddle = whole(kDoubleMiddle) != 0;
   v.noRubs = whole(kNoRubs) != 0;
   v.sameShape = whole(kSameShape) != 0;
   return v;
}

// The recipe the params hold.
template <typename Get>
Recipe recipeFrom(Get value) {
   Recipe r;
   r.genre = std::clamp(static_cast<int>(std::lround(value(kGenre))), 0, genreMax());
   r.style = static_cast<int>(std::lround(value(kStyle)));
   r.genre2 = static_cast<int>(std::lround(value(kBlendGenre))) - 1;
   r.blend = value(kBlend);
   r.player = std::clamp(static_cast<int>(std::lround(value(kPlayer))), 0, kPlayers - 1);
   r.energy = value(kEnergy);
   r.weird = value(kWeird);
   r.seed = static_cast<uint32_t>(std::lround(value(kSeed)));
   r.key = std::clamp(static_cast<int>(std::lround(value(kKey))), 0, 11);
   for (int i = 0; i < kSections; ++i)
      r.sections[static_cast<size_t>(i)] = value(static_cast<clap_id>(kSectionEnergy + i));
   return r;
}

} // namespace jcomp

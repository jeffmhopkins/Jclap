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

namespace jdrummer {

// A style's swing: its genre's, more with SHUFFLE; swung in eighths with SWING8 (a straight genre
// shuffled in eighths swings to a triplet feel).
inline double styleSwing(const Genre &g, const Style &s) {
   double sw = g.swing + (s.mods & kShuffle ? 0.08 : 0.0);
   if (s.mods & kSwing8)
      sw = std::max(sw, 0.66);
   // A jazz ride swings harder slow and evens out fast: about 3:1 at a ballad, 2:1 at 140, near
   // even at 220 and up.
   if (s.mods & kSwing8 && std::string(g.name) == "Jazz")
      sw = std::clamp(0.75 - (s.bpm - 70) * 0.001, 0.58, 0.75);
   if (s.mods & kStraight)
      sw = 0.54;
   return std::min(s.mods & kSwing8 ? 0.75 : 0.70, sw);
}
inline int styleSwingFeel(const Style &s) { return s.mods & kSwing8 ? 1 : 0; }

// The params a style sets when it is picked, on the panel or as a preset.
inline std::vector<std::pair<clap_id, double>> styleValues(int genre, int style) {
   const Genre &g = genreAt(genre);
   const Style &s = g.styles[static_cast<size_t>(std::clamp(style, 0, static_cast<int>(g.styles.size()) - 1))];
   return {{kGenre, genre},
           {kStyle, style},
           {kSwing, styleSwing(g, s)},
           {kSwingFeel, styleSwingFeel(s)},
           {kFreeTempo, s.bpm},
           {kPlayer, g.player},
           {kPercussionist, g.percussionist},
           {kKit, g.kit}};
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

// The recipe the params hold.
template <typename Get>
Recipe recipeFrom(Get value) {
   Recipe r;
   r.genre = std::clamp(static_cast<int>(std::lround(value(kGenre))), 0, kGenreMax);
   r.style = static_cast<int>(std::lround(value(kStyle)));
   r.genre2 = static_cast<int>(std::lround(value(kBlendGenre))) - 1;
   r.blend = value(kBlend);
   r.player = static_cast<int>(std::lround(value(kPlayer)));
   r.percussionist = static_cast<int>(std::lround(value(kPercussionist)));
   r.energy = value(kEnergy);
   r.weird = value(kWeird);
   r.seed = static_cast<uint32_t>(std::lround(value(kSeed)));
   for (int i = 0; i < kSections; ++i)
      r.sections[static_cast<size_t>(i)] = value(static_cast<clap_id>(kSectionEnergy + i));
   return r;
}

} // namespace jdrummer

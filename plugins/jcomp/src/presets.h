#pragma once

// JComp's presets. Every style in the library is a factory preset (filed by family and genre),
// holding the recipe: the genre and style, the player that suits it and how it voices, its tempo,
// swing and scale, and a seed. Loading one remakes the song from it. A user preset also keeps the song
// itself, with every edit and mutation, in a file beside its own (see songFile()).

#include <jclap/ui/presets.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "generator.h"
#include "recipe.h"
#include "params.h"
#include "shared.h"
#include "styles.h"

namespace jcomp {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"JComp", 1, ".jcomppreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (size_t i = 0; i < kParamCount; ++i)
      d.push_back(kParams[i].defaultValue);
   return d;
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      std::vector<Preset> p;
      for (const FactoryEntry &e : factoryEntries()) {
         const Genre &g = genreAt(e.genre);
         Preset preset;
         preset.name = e.name;
         preset.category = std::string(g.family) + ": " + g.name;
         for (const auto &[id, v] : styleValues(e.genre, e.style))
            preset.values.push_back({id, v});
         preset.values.push_back({kSeed, static_cast<double>(1 + mix(static_cast<uint32_t>(e.genre), static_cast<uint32_t>(e.style)) % 99999)});
         p.push_back(preset);
      }
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

// Where a user preset keeps its song: beside the preset file.
inline std::filesystem::path songFile(std::filesystem::path presetFile) {
   return presetFile.replace_extension(".jcompsong");
}
inline bool writeSong(const std::filesystem::path &file, const Song &song) {
   const std::vector<uint8_t> data = song.save();
   std::ofstream out(file, std::ios::binary);
   out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
   return out.good();
}
inline bool readSong(const std::filesystem::path &file, Song &song, const Recipe &r) {
   std::ifstream in(file, std::ios::binary);
   if (!in)
      return false;
   const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   if (!song.load(data.data(), data.size()))
      return false;
   completeSong(r, song, data);
   return true;
}

} // namespace jcomp

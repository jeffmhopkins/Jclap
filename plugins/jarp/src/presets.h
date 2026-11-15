#pragma once

// JArp's presets: a factory bank compiled in, and the user's own saved through jclap::ui's preset
// files. A preset holds the params and every pattern slot: a factory preset has its patterns compiled
// in, and a user preset keeps them in a small file beside its own (see patternFile()), since a
// preset file holds params only.
//
// A factory preset is written as what it changes from the defaults, then filled out with every other
// param at its default, so nothing from the previous preset carries over.

#include <jclap/ui/presets.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "params.h"
#include "library.h"
#include "shared.h"

namespace jarp {

using jclap::ui::Preset;

inline const jclap::ui::PresetFormat kPresetFormat{"JArp", 1, ".jarppreset", kParamCount};

inline std::vector<double> defaultValues() {
   std::vector<double> d;
   for (const auto &p : kParams)
      d.push_back(p.defaultValue);
   return d;
}

using library::makePattern;

// The pattern a new instance starts with: up through four held notes and back.
inline Pattern defaultPattern() {
   return makePattern({{1, 1, 4}, {2, 2, 4}, {3, 3, 4}, {4, 4, 4}, {3, 5, 4}, {2, 6, 4}, {1, 7, 4}, {2, 8, 4}});
}

// A bank with these patterns from slot 1 on, the rest empty.
inline Bank makeBank(const std::vector<Pattern> &patterns) {
   Bank b;
   for (size_t i = 0; i < patterns.size() && i < kSlots; ++i)
      b.slots[i] = patterns[i];
   return b;
}

// The factory library, built once.
inline const std::vector<library::Entry> &factoryLibrary() {
   static const std::vector<library::Entry> entries = library::build();
   return entries;
}

struct FactoryPattern {
   std::string name;
   Bank bank;
};

inline const std::vector<FactoryPattern> &factoryPatterns() {
   static const std::vector<FactoryPattern> patterns = [] {
      std::vector<FactoryPattern> p;
      for (const library::Entry &e : factoryLibrary())
         p.push_back({e.name, makeBank(e.patterns)});
      return p;
   }();
   return patterns;
}

// The patterns of a factory preset, or null for one that has none (a user preset).
inline const Bank *factoryBank(const std::string &name) {
   for (const FactoryPattern &p : factoryPatterns())
      if (name == p.name)
         return &p.bank;
   return nullptr;
}

// Where a user preset keeps its patterns: beside the preset file, as a Bank.
inline std::filesystem::path patternFile(std::filesystem::path presetFile) {
   return presetFile.replace_extension(".jarppatterns");
}
inline bool writeBank(const std::filesystem::path &file, const Bank &bank) {
   const std::vector<uint8_t> data = bank.save();
   std::ofstream out(file, std::ios::binary);
   out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
   return out.good();
}
inline bool readBank(const std::filesystem::path &file, Bank &bank) {
   std::ifstream in(file, std::ios::binary);
   if (!in)
      return false;
   const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   return bank.load(data.data(), data.size());
}

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      std::vector<Preset> p;
      for (const library::Entry &e : factoryLibrary())
         p.push_back({e.name, e.category, e.params});
      jclap::ui::completePresets(p, defaultValues());
      return p;
   }();
   return presets;
}

// The factory categories, in library order.
inline const std::vector<std::string> &factoryCategories() {
   static const std::vector<std::string> categories = [] {
      std::vector<std::string> c;
      for (const library::Entry &e : factoryLibrary())
         if (std::find(c.begin(), c.end(), e.category) == c.end())
            c.push_back(e.category);
      return c;
   }();
   return categories;
}

} // namespace jarp

#pragma once

// JSynth's presets, as JVerb's: a factory bank compiled in, and the user's own saved as small text
// files.
//
// A factory preset is written as what it changes from the defaults, so the bank stays readable, and
// loading it sets every other param to its default, so nothing from the previous patch (a route, say)
// carries over. A user preset saves every param. Loading one is a normal edit of every param that
// changes, so the host sees each and can undo it.
//
// A user preset file is plain text: a "JSynth preset 1" line, then one "<param id> <value>" per line.
// Ids it doesn't know are skipped, and params it doesn't list load at their defaults.

#include <jclap/ui/params.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "params.h"

namespace jsynth {

struct Preset {
   std::string name;
   std::string category;
   std::vector<std::pair<clap_id, double>> values;
};

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      using Values = std::vector<std::pair<clap_id, double>>;
      // Part params by offset: a(oscParam(0, kOscWave), 2) is part A's.
      const auto a = [](clap_id offset, double v) { return std::pair<clap_id, double>{partId(0, offset), v}; };
      const auto b = [](clap_id offset, double v) { return std::pair<clap_id, double>{partId(1, offset), v}; };
      const auto g = [](clap_id id, double v) { return std::pair<clap_id, double>{id, v}; };
      const auto route = [](int part, int r, int source, clap_id dest, double amount) {
         return Values{{partId(part, routeParam(r, kRouteSource)), source},
                       {partId(part, routeParam(r, kRouteDestination)), destinationIndex(dest)},
                       {partId(part, routeParam(r, kRouteAmount)), amount}};
      };
      const auto join = [](std::initializer_list<Values> parts) {
         Values all;
         for (const Values &p : parts)
            all.insert(all.end(), p.begin(), p.end());
         return all;
      };
      constexpr int kScatter = 1, kHunt = 2, kFlee = 3;
      constexpr int kSine = 0, kSaw = 2, kPulse = 4, kHalfSine = 5, kAltSine = 8;
      constexpr int kRatio = 1;
      const int press = kSourceStrike + 1, slide = kSourceStrike + 3;
      std::vector<Preset> p;

      p.push_back({"Init", "Flocks", {}});
      p.push_back({"Starling Bells", "Flocks",
                   join({{a(oscParam(0, kOscWave), kSine), a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio),
                          a(oscParam(1, kOscRatio), 3.5), a(fmCell(1, 0), 0.4), a(envParam(0, kEnvAttack), timeValue(2)),
                          a(envParam(0, kEnvDecay), timeValue(1800)), a(envParam(0, kEnvSustain), 0.0),
                          a(envParam(0, kEnvRelease), timeValue(1500)), a(flockParam(kNeighbourFm), 0.7),
                          a(flockParam(kTunePull), 0.4), a(flockParam(kCohesion), 0.7), a(flockParam(kTailLength), 0.7)},
                         route(0, 6, kSourceCrowd, fmCell(1, 0), 0.3)})});
      p.push_back({"Murmuration Pad", "Flocks",
                   join({{a(oscParam(0, kOscWave), kSaw), a(oscParam(1, kOscWave), kSaw), a(oscParam(1, kOscFine), 9),
                          a(mixParam(kMixLevel, 1), 0.8), a(mixParam(kMixPan, 0), -0.5), a(mixParam(kMixPan, 1), 0.5),
                          a(envParam(0, kEnvAttack), timeValue(900)), a(envParam(0, kEnvRelease), timeValue(3000)),
                          a(filterParam(0, kFilterCutoff), 0.45), a(flockParam(kAlignment), 0.8),
                          a(flockParam(kSpeed), 0.25), a(flockParam(kTailLength), 0.9), a(flockParam(kTailSpread), 0.8)},
                         route(0, 6, kSourceHeading, kPartPan, 0.6)})});
      p.push_back({"Fireflies", "Flocks",
                   {a(flockParam(kBehaviour), kScatter), a(oscParam(0, kOscWave), kHalfSine), a(envParam(0, kEnvAttack), timeValue(1)),
                    a(envParam(0, kEnvDecay), timeValue(220)), a(envParam(0, kEnvSustain), 0.0),
                    a(flockParam(kSpeed), 0.8), a(flockParam(kWander), 0.7), a(flockParam(kTailLength), 0.15),
                    a(flockParam(kTailSpread), 0.2), a(lfoParam(0, kLfoRate), lfoValue(9.0))}});
      p.push_back({"Ink Ribbons", "Flocks",
                   join({{a(oscParam(0, kOscWave), kAltSine), a(fmCell(0, 0), 0.25), a(flockParam(kTailLength), 1.0),
                          a(flockParam(kTailSpread), 0.9), a(flockParam(kWake), 0.8), a(flockParam(kLure), 0.35)},
                         route(0, 6, kSourceWake, filterParam(0, kFilterCutoff), 0.5),
                         route(0, 7, kSourceWake, fmCell(1, 0), 0.4)})});
      p.push_back({"Shark Bass", "Predators",
                   join({{g(kPlayMode, 2.0), g(kSplitKey, 48.0), b(flockParam(kBehaviour), kHunt), b(oscParam(0, kOscWave), kSaw),
                          b(oscParam(1, kOscWave), kPulse), b(mixParam(kMixLevel, 1), 0.7), b(filterParam(0, kFilterCutoff), 0.35),
                          b(filterParam(0, kFilterResonance), 0.5), b(flockParam(kSpeed), 0.85), b(flockParam(kBite), 0.8)},
                         route(1, 6, kSourceSpeed, filterParam(0, kFilterCutoff), 0.45)})});
      p.push_back({"Predator and Prey", "Predators",
                   join({{g(kPlayMode, 1.0), a(flockParam(kBehaviour), kFlee), b(flockParam(kBehaviour), kHunt),
                          b(oscParam(0, kOscWave), kPulse), b(kPartTranspose, -12), b(flockParam(kBite), 0.9),
                          a(flockParam(kSpeed), 0.6), b(flockParam(kSpeed), 0.7)},
                         route(0, 6, kSourceCaught, filterParam(0, kFilterCutoff), 0.6)})});
      p.push_back({"Wind Choir", "Weather",
                   join({{a(oscParam(0, kOscWave), kSaw), a(mixParam(kNoiseLevel), 0.3), a(filterParam(0, kFilterType), 2),
                          a(filterParam(0, kFilterResonance), 0.6), a(lfoParam(1, kLfoPush), 0.9),
                          a(lfoParam(1, kLfoRate), lfoValue(0.1)), a(envParam(0, kEnvAttack), timeValue(600))},
                         route(0, 6, kSourceWind, filterParam(0, kFilterCutoff), 0.4),
                         route(0, 7, slide, filterParam(0, kFilterResonance), 0.3)})});
      p.push_back({"Pressure Front", "Weather",
                   join({{a(oscParam(1, kOscWave), kSine), a(oscParam(1, kOscPitchMode), kRatio), a(oscParam(1, kOscRatio), 2.0),
                          a(flockParam(kNeighbourFm), 0.5), a(lfoParam(2, kLfoRate), lfoValue(6.0))},
                         route(0, 6, press, fmCell(1, 0), 0.6), route(0, 7, kSourceLfo1, kPartFine, 0.15)})});
      return p;
   }();
   return presets;
}

// Every param of `p`, in kParams() order: its own values, and every other param at its default.
inline std::vector<double> presetValues(const Preset &p) {
   std::vector<double> v(paramCount());
   for (size_t i = 0; i < paramCount(); ++i)
      v[i] = kParams()[i].def.defaultValue;
   for (const auto &[id, value] : p.values) {
      const int i = paramIndex(id);
      if (i >= 0 && std::isfinite(value))
         v[i] = std::clamp(value, kParams()[i].def.min, kParams()[i].def.max);
   }
   return v;
}

// The folder user presets live in: the OS's per-user application data folder.
inline std::filesystem::path userPresetFolder() {
#if defined(_WIN32)
   const char *appData = std::getenv("APPDATA");
   return std::filesystem::path(appData ? appData : ".") / "JSynth" / "Presets";
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / "JSynth" / "Presets";
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / "JSynth" / "Presets";
#endif
}

constexpr const char *kPresetHeader = "JSynth preset 1";
constexpr const char *kPresetExtension = ".jspreset";

// Reads a user preset; false if the file isn't one.
inline bool readPreset(const std::filesystem::path &file, Preset &out) {
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || line != kPresetHeader)
      return false;
   out = {file.stem().string(), "User", {}};
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      unsigned id = 0;
      double v = 0.0;
      if (fields >> id >> v && findParam(id) && std::isfinite(v))
         out.values.emplace_back(static_cast<clap_id>(id), v);
   }
   return true;
}

// Saves every param, so a preset sounds the same even if a later version changes a default; false if
// the file can't be written. It is written beside its final name and then renamed over it, so a
// failed save never leaves a half-written preset behind.
inline bool writePreset(const std::filesystem::path &file, const jclap::ui::ParamAccess &access) {
   std::error_code ec;
   std::filesystem::create_directories(file.parent_path(), ec);
   std::filesystem::path temp = file;
   temp += ".tmp";
   {
      std::ofstream out(temp);
      if (!out)
         return false;
      out << kPresetHeader << "\n";
      char line[64];
      for (const Param &p : kParams()) {
         std::snprintf(line, sizeof(line), "%u %.17g\n", static_cast<unsigned>(p.def.id), access.paramValue(p.def.id));
         out << line;
      }
      out.flush();
      if (!out) {
         out.close();
         std::filesystem::remove(temp, ec);
         return false;
      }
   }
   std::filesystem::rename(temp, file, ec);
   if (ec) {
      std::filesystem::remove(temp, ec);
      return false;
   }
   return true;
}

// The user's presets in `folder`, sorted by name.
inline std::vector<Preset> userPresets(const std::filesystem::path &folder = userPresetFolder()) {
   std::vector<Preset> found;
   std::error_code ec;
   for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      Preset p;
      if (it->path().extension() == kPresetExtension && readPreset(it->path(), p))
         found.push_back(std::move(p));
   }
   std::sort(found.begin(), found.end(), [](const Preset &x, const Preset &y) { return x.name < y.name; });
   return found;
}

// A name made safe for a file name: letters, digits, spaces, dashes and underscores only.
inline std::string presetFileName(const std::string &name) {
   std::string safe;
   for (char ch : name)
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_')
         safe += ch;
   while (!safe.empty() && safe.back() == ' ')
      safe.pop_back();
   return safe.empty() ? "Untitled" : safe;
}

} // namespace jsynth

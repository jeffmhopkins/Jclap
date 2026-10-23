#pragma once

// JVerb's presets: a factory bank compiled in, and the user's own saved as small text files.
//
// A preset lists only what it changes from the defaults, so presets stay short and readable and
// params added later fall back to their defaults. Loading one is a normal edit of every param (the
// host sees each change, and can undo it) followed by a restart of the orbits from the new masses
// and velocities.
//
// A user preset file is plain text: a "JVerb preset 1" line, then one "<param id> <value>" per line.

#include <jclap/ui/params.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "params.h"

namespace jverb {

struct Preset {
   std::string name;
   std::string category;
   std::vector<std::pair<clap_id, double>> values;
};

inline const std::vector<Preset> &factoryPresets() {
   static const std::vector<Preset> presets = [] {
      // Body params by role: body(b, kBody1Target, Target::Echo)...
      const auto body = [](int b, clap_id role, double v) { return std::pair<clap_id, double>{bodyParam(b, role), v}; };
      const auto target = [&](int b, Target t, double amount) {
         return std::vector<std::pair<clap_id, double>>{body(b, kBody1Target, static_cast<double>(t)),
                                                       body(b, kBody1Amount, amount)};
      };
      const auto with = [](std::vector<std::pair<clap_id, double>> a, const std::vector<std::pair<clap_id, double>> &b) {
         a.insert(a.end(), b.begin(), b.end());
         return a;
      };
      constexpr double kInto = 0, kLoopSlot = 1, kAfter = 2;
      constexpr double kPreSlot = 0, kInLoop = 1, kPostSlot = 2;
      constexpr double kTape = 0, kTube = 1, kFuzz = 2, kFold = 3, kCrush = 4;
      std::vector<Preset> p;

      p.push_back({"Init", "Basics", {}});
      p.push_back({"Small Orbit", "Basics",
                   {{kDecay, 0.9}, {kSize, 0.4}, {kBodies, 4}, {kMotion, 0.2}, {kDiffusion, 0.8}, {kEarlyLate, 0.55},
                    {kPredelay, 4}, {kDampHigh, 9500}, {kMix, 0.25}}});
      p.push_back({"Figure-Eight Plate", "Basics",
                   {{kDecay, 2.6}, {kSize, 0.6}, {kMotion, 0.25}, {kDiffusion, 0.95}, {kEarlyLate, 0.95},
                    {kPredelay, 10}, {kDampHigh, 12500}, {kLowCut, 120}, {kMix, 0.3}}});
      p.push_back({"Cathedral Drift", "Basics",
                   {{kDecay, 11}, {kSize, 2.6}, {kGravity, 0.7}, {kMotion, 0.5}, {kPredelay, 45}, {kDampHigh, 5200},
                    {kBassMult, 1.5}, {kEarlyLate, 0.85}, {kMix, 0.4}}});
      p.push_back({"Binary Star", "Basics",
                   with({{kDecay, 3.2}, {kBodies, 2}, {kMotion, 0.7}, {kSize, 1.2}, {kMix, 0.35},
                         body(0, kBody1Mass, 1.4), body(1, kBody1Mass, 1.1)},
                        target(1, Target::Width, 0.6))});

      p.push_back({"Deep Field", "Ambient",
                   {{kDecay, 28}, {kSize, 3.4}, {kGravity, 0.5}, {kBodies, 6}, {kMotion, 0.75}, {kAttack, 0.35},
                    {kDampHigh, 6000}, {kBassMult, 1.1}, {kPredelay, 60}, {kMix, 0.5}}});
      p.push_back({"Shimmer Halo", "Ambient",
                   {{kDecay, 8}, {kSize, 2}, {kShimmer, 0.45}, {kFeedback, 0.9}, {kShimmerPitch, 12}, {kTilt, 0.2},
                    {kAttack, 0.3}, {kMix, 0.42}}});
      p.push_back({"Octave Under", "Ambient",
                   {{kDecay, 10}, {kSize, 2.4}, {kShimmer, 0.4}, {kShimmerPitch, -12}, {kFeedback, 0.85},
                    {kDampHigh, 4000}, {kMix, 0.4}}});
      p.push_back({"Swell Machine", "Ambient",
                   {{kDecay, 6}, {kSize, 1.6}, {kAttack, 0.85}, {kDiffusion, 0.9}, {kMotion, 0.5}, {kMix, 0.45}}});
      p.push_back({"Slow Ballet", "Ambient",
                   {{kDecay, 7}, {kSize, 1.8}, {kGravity, 0.35}, {kMotion, 0.65}, {kSmear, 0.6}, {kMix, 0.4}}});
      p.push_back({"Frozen Ring", "Ambient",
                   {{kDecay, 45}, {kSize, 3}, {kDiffusion, 0.9}, {kDampHigh, 8000}, {kMotion, 0.3}, {kMix, 0.6},
                    {kDuck, 0.3}}});

      p.push_back({"Dotted Echo Hall", "Echo",
                   {{kEchoOn, 1}, {kEchoWhere, kInto}, {kEchoSync, 6}, {kEchoFeedback, 0.45}, {kEchoMix, 0.55},
                    {kDecay, 3}, {kSize, 1.4}, {kMix, 0.35}}});
      p.push_back({"Ping-Pong Space", "Echo",
                   {{kEchoOn, 1}, {kEchoWhere, kAfter}, {kEchoSync, 7}, {kEchoFeedback, 0.5}, {kEchoPingPong, 1},
                    {kEchoMix, 0.5}, {kDecay, 2.5}, {kMix, 0.4}}});
      p.push_back({"Tape Loop Orbit", "Echo",
                   with({{kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 12}, {kEchoFeedback, 0.35},
                         {kEchoTone, -0.5}, {kEchoMix, 0.8}, {kDriveOn, 1}, {kDriveWhere, kInLoop},
                         {kDriveType, kTape}, {kDrive, 8}, {kFeedback, 0.85}, {kDecay, 5}, {kMix, 0.45}},
                        target(2, Target::Echo, 0.25))});
      p.push_back({"Infinite Return", "Echo",
                   {{kEchoOn, 1}, {kEchoWhere, kLoopSlot}, {kEchoSync, 10}, {kEchoFeedback, 0.7}, {kEchoMix, 0.7},
                    {kFeedback, 0.92}, {kShimmer, 0.2}, {kDecay, 8}, {kSize, 2}, {kMix, 0.45}}});
      p.push_back({"Warped Slapback", "Echo",
                   with({{kEchoOn, 1}, {kEchoWhere, kInto}, {kEchoTime, 140}, {kEchoFeedback, 0.25},
                         {kEchoPingPong, 0}, {kEchoTone, 0.2}, {kEchoMix, 0.7}, {kDecay, 1.4}, {kSize, 0.7},
                         {kMix, 0.3}},
                        target(0, Target::Echo, 0.5))});

      p.push_back({"Burning Loop", "Drive",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kTube}, {kDrive, 18}, {kFeedback, 0.9},
                    {kTilt, -0.3}, {kDecay, 5}, {kMix, 0.4}}});
      p.push_back({"Fuzz Cloud", "Drive",
                   {{kDriveOn, 1}, {kDriveWhere, kPostSlot}, {kDriveType, kFuzz}, {kDrive, 24}, {kDriveMix, 0.6},
                    {kDriveTrim, -6}, {kDecay, 7}, {kSize, 2}, {kMix, 0.45}}});
      p.push_back({"Crushed Satellites", "Drive",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kCrush}, {kDriveBias, 0.5}, {kDrive, 14},
                    {kFeedback, 0.8}, {kBodies, 5}, {kDecay, 4}, {kMix, 0.4}}});
      p.push_back({"Folded Space", "Drive",
                   {{kDriveOn, 1}, {kDriveWhere, kInLoop}, {kDriveType, kFold}, {kDrive, 20}, {kShimmer, 0.25},
                    {kFeedback, 0.85}, {kDecay, 6}, {kMix, 0.4}}});
      p.push_back({"Hot Input", "Drive",
                   {{kDriveOn, 1}, {kDriveWhere, kPreSlot}, {kDriveType, kTape}, {kDrive, 15}, {kDriveTone, 0.3},
                    {kDecay, 2.2}, {kMix, 0.35}}});

      p.push_back({"Chaos Engine", "Orbits",
                   with(with({{kBodies, 6}, {kGravity, 2.5}, {kMotion, 0.9}, {kReact, 0.8}, {kDecay, 4},
                              {kFeedback, 0.3}, {kMix, 0.45}},
                             target(0, Target::Size, 0.6)),
                        target(3, Target::Feedback, 0.6))});
      p.push_back({"Synced Orbit", "Orbits",
                   {{kClock, 1}, {kSyncBars, 2}, {kMotion, 0.8}, {kDecay, 3.5}, {kMix, 0.4}}});
      p.push_back({"Heavy Moon", "Orbits",
                   {{kBodies, 3}, {kMotion, 0.7}, {kDecay, 5}, {kSize, 1.5},
                    {bodyParam(0, kBody1Mass), 2.8}, {bodyParam(1, kBody1Mass), 0.3}, {bodyParam(2, kBody1Mass), 0.4},
                    {kMix, 0.4}}});
      p.push_back({"Drum Reactor", "Orbits",
                   {{kReact, 1}, {kDecay, 1.8}, {kSize, 0.9}, {kMotion, 0.6}, {kDuck, 0.4}, {kMix, 0.3}}});
      return p;
   }();
   return presets;
}

// Applies a preset through `host` (anything with setParams(const double *)): every param to its
// default, then the preset's values.
template <class Host>
void applyPreset(const Preset &preset, Host &host) {
   double values[kParamCount];
   for (clap_id id = 0; id < kParamCount; ++id)
      values[id] = kParams[id].defaultValue;
   for (const auto &[id, v] : preset.values)
      if (id < kParamCount)
         values[id] = std::clamp(v, kParams[id].min, kParams[id].max);
   host.setParams(values);
}

// Where user presets live: per platform, under the user's own settings folder.
inline std::filesystem::path userPresetFolder() {
#if defined(_WIN32)
   const char *base = std::getenv("APPDATA");
   return std::filesystem::path(base ? base : ".") / "JVerb" / "Presets";
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / "JVerb" / "Presets";
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / "JVerb" / "Presets";
#endif
}

constexpr const char *kPresetHeader = "JVerb preset 1";
constexpr const char *kPresetExtension = ".jvpreset";

// Reads a user preset; false if the file isn't one.
inline bool readPreset(const std::filesystem::path &file, Preset &out) {
   std::ifstream in(file);
   std::string line;
   if (!in || !std::getline(in, line) || line.rfind(kPresetHeader, 0) != 0)
      return false;
   out = {file.stem().string(), "User", {}};
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      unsigned id = 0;
      double v = 0.0;
      if (fields >> id >> v && id < kParamCount && std::isfinite(v))
         out.values.emplace_back(static_cast<clap_id>(id), v);
   }
   return true;
}

// Saves every param that isn't at its default; returns false if the file can't be written. The
// file is written beside its final name and then renamed over it, so a failed save never leaves a
// half-written preset behind.
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
      for (clap_id id = 0; id < kParamCount; ++id) {
         const double v = access.paramValue(id);
         if (v != kParams[id].defaultValue) {
            std::snprintf(line, sizeof(line), "%u %.9g\n", static_cast<unsigned>(id), v);
            out << line;
         }
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

// The user's presets, sorted by name.
inline std::vector<Preset> userPresets() {
   std::vector<Preset> found;
   std::error_code ec;
   const auto folder = userPresetFolder();
   for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      Preset p;
      if (it->path().extension() == kPresetExtension && readPreset(it->path(), p))
         found.push_back(std::move(p));
   }
   std::sort(found.begin(), found.end(), [](const Preset &a, const Preset &b) { return a.name < b.name; });
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

} // namespace jverb

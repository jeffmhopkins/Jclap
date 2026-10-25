#include "jclap/ui/presets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace jclap::ui {

void completePresets(std::vector<Preset> &presets, const std::vector<double> &defaults) {
   for (Preset &preset : presets) {
      std::vector<bool> given(defaults.size(), false);
      for (const auto &[id, v] : preset.values)
         if (id < given.size())
            given[id] = true;
      for (clap_id id = 0; id < defaults.size(); ++id)
         if (!given[id])
            preset.values.push_back({id, defaults[id]});
   }
}

std::filesystem::path userPresetFolder(const PresetFormat &format) {
#if defined(_WIN32)
   const char *base = std::getenv("APPDATA");
   return std::filesystem::path(base ? base : ".") / format.name / "Presets";
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / format.name / "Presets";
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / format.name / "Presets";
#endif
}

bool readPresetFile(const std::filesystem::path &file, const PresetFormat &format, Preset &out, int *version) {
   std::ifstream in(file);
   std::string line;
   const std::string prefix = format.name + " preset ";
   if (!in || !std::getline(in, line) || line.rfind(prefix, 0) != 0)
      return false;
   const int v = std::atoi(line.c_str() + prefix.size());
   if (v < 1 || v > format.version)
      return false;
   if (version)
      *version = v;
   out = {file.stem().string(), "User", {}};
   while (std::getline(in, line)) {
      std::istringstream fields(line);
      unsigned id = 0;
      double value = 0.0;
      if (fields >> id >> value && id < format.paramCount && std::isfinite(value))
         out.values.emplace_back(static_cast<clap_id>(id), value);
   }
   return true;
}

bool writePresetFile(const std::filesystem::path &file, const PresetFormat &format, const ParamAccess &params) {
   std::error_code ec;
   std::filesystem::create_directories(file.parent_path(), ec);
   std::filesystem::path temp = file;
   temp += ".tmp";
   {
      std::ofstream out(temp);
      if (!out)
         return false;
      out << format.header() << "\n";
      char line[64];
      for (clap_id id = 0; id < format.paramCount; ++id) {
         std::snprintf(line, sizeof(line), "%u %.9g\n", static_cast<unsigned>(id), params.paramValue(id));
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

std::vector<Preset> userPresets(const PresetFormat &format) {
   std::vector<Preset> found;
   std::error_code ec;
   const auto folder = userPresetFolder(format);
   for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      Preset p;
      if (it->path().extension() == format.extension && readPresetFile(it->path(), format, p))
         found.push_back(std::move(p));
   }
   std::sort(found.begin(), found.end(), [](const Preset &a, const Preset &b) { return a.name < b.name; });
   return found;
}

std::string presetFileName(const std::string &name) {
   std::string safe;
   for (char ch : name)
      if (std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-' || ch == '_')
         safe += ch;
   while (!safe.empty() && safe.back() == ' ')
      safe.pop_back();
   while (!safe.empty() && safe.front() == ' ')
      safe.erase(safe.begin());
   return safe.empty() ? "Untitled" : safe;
}

} // namespace jclap::ui

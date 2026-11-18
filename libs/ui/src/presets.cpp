#include "jclap/ui/presets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

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

namespace {
// Where a "Family: Style" category splits: the separator's position, or npos for a category
// without a family (no separator, or nothing on one side of it).
constexpr std::string_view kFamilySeparator = ": ";
size_t familySplit(const std::string &category) {
   const size_t at = category.find(kFamilySeparator);
   return at == 0 || at == std::string::npos || at + kFamilySeparator.size() >= category.size() ? std::string::npos
                                                                                               : at;
}
} // namespace

std::string categoryFamily(const std::string &category) {
   const size_t at = familySplit(category);
   return at == std::string::npos ? category : category.substr(0, at);
}

std::string categoryStyle(const std::string &category) {
   const size_t at = familySplit(category);
   return at == std::string::npos ? category : category.substr(at + kFamilySeparator.size());
}

std::vector<PresetFolder> presetFolders(const std::vector<std::string> &categories) {
   std::vector<PresetFolder> folders;
   for (const std::string &category : categories) {
      const std::string family = categoryFamily(category);
      auto it = std::find_if(folders.begin(), folders.end(), [&](const PresetFolder &f) { return f.name == family; });
      if (it == folders.end())
         folders.push_back({family, {category}});
      else if (std::find(it->categories.begin(), it->categories.end(), category) == it->categories.end())
         it->categories.push_back(category);
   }
   return folders;
}

std::vector<std::string> presetCategories(const std::vector<Preset> &presets) {
   std::vector<std::string> categories;
   for (const Preset &p : presets)
      if (std::find(categories.begin(), categories.end(), p.category) == categories.end())
         categories.push_back(p.category);
   return categories;
}

std::filesystem::path userFolder(const std::string &name) {
#if defined(_WIN32)
   const char *base = std::getenv("APPDATA");
   return std::filesystem::path(base ? base : ".") / name;
#elif defined(__APPLE__)
   const char *home = std::getenv("HOME");
   return std::filesystem::path(home ? home : ".") / "Library" / "Application Support" / name;
#else
   const char *config = std::getenv("XDG_CONFIG_HOME");
   const char *home = std::getenv("HOME");
   const std::filesystem::path base = config && *config ? std::filesystem::path(config)
                                                        : std::filesystem::path(home ? home : ".") / ".config";
   return base / name;
#endif
}

std::filesystem::path userPresetFolder(const PresetFormat &format) { return userFolder(format.name) / "Presets"; }

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

PresetFavorites::PresetFavorites(std::filesystem::path file) : file_(std::move(file)) { reload(); }

std::filesystem::path PresetFavorites::fileFor(const std::string &name) { return userFolder(name) / "Favorites.txt"; }

void PresetFavorites::reload() {
   names_.clear();
   std::ifstream in(file_);
   std::string line;
   while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      if (!line.empty() && !contains(line))
         names_.push_back(line);
   }
}

bool PresetFavorites::contains(const std::string &name) const {
   return std::find(names_.begin(), names_.end(), name) != names_.end();
}

bool PresetFavorites::toggle(const std::string &name) {
   reload();
   const auto it = std::find(names_.begin(), names_.end(), name);
   const bool starred = it == names_.end();
   if (starred)
      names_.push_back(name);
   else
      names_.erase(it);
   save();
   return starred;
}

void PresetFavorites::remove(const std::string &name) {
   reload();
   const auto it = std::find(names_.begin(), names_.end(), name);
   if (it == names_.end())
      return;
   names_.erase(it);
   save();
}

// Written beside the file and renamed over it, like a preset, so a failed save keeps the old stars.
bool PresetFavorites::save() const {
   std::error_code ec;
   std::filesystem::create_directories(file_.parent_path(), ec);
   std::filesystem::path temp = file_;
   temp += ".tmp";
   {
      std::ofstream out(temp);
      if (!out)
         return false;
      for (const std::string &name : names_)
         out << name << "\n";
      out.flush();
      if (!out) {
         out.close();
         std::filesystem::remove(temp, ec);
         return false;
      }
   }
   std::filesystem::rename(temp, file_, ec);
   if (ec) {
      std::filesystem::remove(temp, ec);
      return false;
   }
   return true;
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
